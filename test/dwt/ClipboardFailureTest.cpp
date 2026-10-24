// Copyright (C) 2026 iceman50
// Compile the real clipboard routine against deterministic failure injections.
// These tests never open or change the system clipboard.
#include <dwt/Clipboard.h>
#include <dwt/util/check.h>
#include <dwt/widgets/Window.h>

#include <cstdio>
#include <stdexcept>

namespace {
enum class Failure { None, Open, Empty, Allocate, Lock, Transfer };
struct State {
	Failure failure = Failure::None;
	int closes = 0, frees = 0, locks = 0, unlocks = 0, transfers = 0;
	TCHAR data[32] {};
} state;
}

BOOL WINAPI auditOpenClipboard(HWND) { return state.failure != Failure::Open; }
BOOL WINAPI auditEmptyClipboard() { return state.failure != Failure::Empty; }
BOOL WINAPI auditCloseClipboard() { ++state.closes; return TRUE; }
HGLOBAL WINAPI auditGlobalAlloc(UINT, SIZE_T) {
	return state.failure == Failure::Allocate ? nullptr : reinterpret_cast<HGLOBAL>(&state);
}
LPVOID WINAPI auditGlobalLock(HGLOBAL) {
	++state.locks;
	return state.failure == Failure::Lock ? nullptr : state.data;
}
BOOL WINAPI auditGlobalUnlock(HGLOBAL) { ++state.unlocks; return TRUE; }
HGLOBAL WINAPI auditGlobalFree(HGLOBAL) { ++state.frees; return nullptr; }
HANDLE WINAPI auditSetClipboardData(UINT, HANDLE handle) {
	++state.transfers;
	return state.failure == Failure::Transfer ? nullptr : handle;
}

#define OpenClipboard auditOpenClipboard
#define EmptyClipboard auditEmptyClipboard
#define CloseClipboard auditCloseClipboard
#define GlobalAlloc auditGlobalAlloc
#define GlobalLock auditGlobalLock
#define GlobalUnlock auditGlobalUnlock
#define GlobalFree auditGlobalFree
#define SetClipboardData auditSetClipboardData
#define setData setDataUnderTest
#include "../../dwt/src/Clipboard.cpp"
#undef setData
#undef SetClipboardData
#undef GlobalFree
#undef GlobalUnlock
#undef GlobalLock
#undef GlobalAlloc
#undef CloseClipboard
#undef EmptyClipboard
#undef OpenClipboard

int dwtMain(dwt::Application&) {
	// No HWND is required: the injected OpenClipboard accepts this uncreated widget.
	dwt::Window window;
	int failures = 0;
	for(auto failure: {Failure::None, Failure::Open, Failure::Empty,
		Failure::Allocate, Failure::Lock, Failure::Transfer}) {
		state = State();
		state.failure = failure;
		try { dwt::Clipboard::setDataUnderTest(_T("test"), &window); }
		catch(const dwt::Win32Exception&) {
			// OpenClipboard's debug-only diagnostic is expected in debug builds.
			if(failure != Failure::Open) ++failures;
		}
		const bool allocated = failure == Failure::None || failure == Failure::Lock || failure == Failure::Transfer;
		const bool copied = failure == Failure::None || failure == Failure::Transfer;
		const bool ok = state.closes == (failure == Failure::Open ? 0 : 1) &&
			state.frees == (failure == Failure::Lock || failure == Failure::Transfer ? 1 : 0) &&
			state.locks == (allocated ? 1 : 0) && state.unlocks == (copied ? 1 : 0) &&
			state.transfers == (copied ? 1 : 0) &&
			(!copied || dwt::tstring(state.data) == _T("test"));
		if(!ok) {
			std::fprintf(stderr, "FAIL: clipboard failure mode %d\n", static_cast<int>(failure));
			++failures;
		}
	}
	if(!failures) std::puts("PASS: clipboard success and open/empty/allocate/lock/transfer failures; no system clipboard changes");
	return failures ? 1 : 0;
}
