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
	int opens = 0, closes = 0, frees = 0, locks = 0, unlocks = 0, transfers = 0;
	TCHAR data[32] {};
} state;
}

BOOL WINAPI auditOpenClipboard(HWND) { ++state.opens; return state.failure != Failure::Open; }
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
	// setData needs a window (it checks IsWindow); the injected OpenClipboard never uses it.
	auto window = new dwt::Window;
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	int failures = 0;
	for(auto failure: {Failure::None, Failure::Open, Failure::Empty,
		Failure::Allocate, Failure::Lock, Failure::Transfer}) {
		state = State();
		state.failure = failure;
		try { dwt::Clipboard::setDataUnderTest(_T("test"), window); }
		catch(const dwt::Win32Exception&) {
			// Each failure's debug-only diagnostic is expected in debug builds.
			if(failure == Failure::None) ++failures;
		}
		// The text is copied into its allocation before the clipboard is opened (retried while
		// another application holds it), and the allocation is freed unless the clipboard took it.
		const bool allocated = failure != Failure::Allocate;
		const bool copied = allocated && failure != Failure::Lock;
		const bool opened = copied && failure != Failure::Open;
		const bool emptied = opened && failure != Failure::Empty;
		const bool ok = state.opens == (!copied ? 0 : failure == Failure::Open ? 5 : 1) &&
			state.closes == (opened ? 1 : 0) &&
			state.frees == (allocated && failure != Failure::None ? 1 : 0) &&
			state.locks == (allocated ? 1 : 0) && state.unlocks == (copied ? 1 : 0) &&
			state.transfers == (emptied ? 1 : 0) &&
			(!copied || dwt::tstring(state.data) == _T("test"));
		if(!ok) {
			std::fprintf(stderr, "FAIL: clipboard failure mode %d (opens %d, closes %d, frees %d, locks %d, unlocks %d, transfers %d)\n",
				static_cast<int>(failure), state.opens, state.closes, state.frees, state.locks, state.unlocks, state.transfers);
			++failures;
		}
	}
	::DestroyWindow(window->handle());
	if(!failures) std::puts("PASS: clipboard success and open (retried)/empty/allocate/lock/transfer failures free what they took; no system clipboard changes");
	return failures ? 1 : 0;
}
