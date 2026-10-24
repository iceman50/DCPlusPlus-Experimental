// Copyright (C) 2026 iceman50
// Exercise failed subclass acquisition using the production implementation.
#include <dwt/Application.h>
#include <dwt/util/win32/NativeDialogAppearance.h>
#include <dwt/WindowsHeaders.h>

#include <cstdio>
#include <stdexcept>

namespace {
bool failSubclass = false;
unsigned failedAttempts = 0;
}

BOOL WINAPI auditSetWindowSubclass(HWND window, SUBCLASSPROC procedure,
	UINT_PTR id, DWORD_PTR data)
{
	if(failSubclass) {
		++failedAttempts;
		return FALSE;
	}
	return ::SetWindowSubclass(window, procedure, id, data);
}

#define SetWindowSubclass auditSetWindowSubclass
#include "../../dwt/src/util/win32/NativeDialogAppearance.cpp"
#undef SetWindowSubclass

int dwtMain(dwt::Application& app) {
	app.getAppearance().configure(dwt::Appearance::Mode::Dark,
		dwt::Appearance::defaultPalette());
	HWND window = ::CreateWindowEx(0, WC_STATIC, _T("Resource audit"),
		WS_POPUP, 0, 0, 20, 20, nullptr, nullptr, ::GetModuleHandle(nullptr), nullptr);
	if(!window) return 1;
	int result = 0;
	try {
		dwt::util::win32::NativeDialogAppearance appearance(app.getAppearance());
		if(!appearance.active()) throw std::runtime_error("Run with Windows high contrast disabled");
		failSubclass = true;
		// Warm up USER32's invalidation bookkeeping for this hidden window.
		for(int i = 0; i < 100; ++i) appearance.apply(window);
		::GdiFlush();
		failedAttempts = 0;
		const auto before = ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
		for(int i = 0; i < 1000; ++i) appearance.apply(window);
		::GdiFlush();
		failSubclass = false;
		const auto after = ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
		std::printf("Subclass attempts=%u, GDI before=%lu after=%lu\n", failedAttempts, before, after);
		if(failedAttempts != 1000 || before != after)
			throw std::runtime_error("Subclass failures leaked appearance resources or were not exercised");
		appearance.apply(window);
		::DestroyWindow(window);
		window = nullptr;
		if(::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS) > before)
			throw std::runtime_error("Successful subclass destruction leaked appearance resources");
		std::puts("PASS: 1000 failed native subclass acquisitions and successful subclass destruction; stable GDI count");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	if(window) ::DestroyWindow(window);
	return result;
}
