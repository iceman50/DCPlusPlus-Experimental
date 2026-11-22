// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// Menu items DWT doesn't draw (those a shell extension adds to a context menu) follow a dark
// palette: Windows used to draw them light, with a white border and separators and gray text,
// on the dark menu background.
#include <dwt/widgets/Window.h>
#include <dwt/util/win32/UxTheme.h>
#include <dwt/util/win32/Version.h>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

int brightness(COLORREF color) {
	return (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000;
}

struct Capture {
	int width = 0, height = 0;
	std::vector<DWORD> pixels;
	COLORREF at(int x, int y) const {
		const auto pixel = pixels[static_cast<size_t>(y) * width + x];
		return RGB((pixel >> 16) & 0xff, (pixel >> 8) & 0xff, pixel & 0xff);
	}
};

Capture capture;
int ticks = 0;

void CALLBACK capturePopup(HWND, UINT, UINT_PTR timer, DWORD) {
	if(++ticks == 3) {
		for(auto popup = FindWindowEx(nullptr, nullptr, L"#32768", nullptr); popup; popup = FindWindowEx(nullptr, popup, L"#32768", nullptr)) {
			if(GetWindowThreadProcessId(popup, nullptr) != GetCurrentThreadId()) continue;
			RECT rect;
			GetWindowRect(popup, &rect);
			capture.width = rect.right - rect.left;
			capture.height = rect.bottom - rect.top;
			auto screen = GetDC(nullptr);
			auto memory = CreateCompatibleDC(screen);
			BITMAPINFO info {};
			info.bmiHeader = { sizeof(BITMAPINFOHEADER), capture.width, -capture.height, 1, 32 };
			void* bits = nullptr;
			auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
			auto previous = SelectObject(memory, bitmap);
			// The menu window paints only on the screen (it ignores WM_PRINT's non-client part).
			BitBlt(memory, 0, 0, capture.width, capture.height, screen, rect.left, rect.top, SRCCOPY);
			GdiFlush();
			capture.pixels.assign(static_cast<DWORD*>(bits), static_cast<DWORD*>(bits) + capture.width * capture.height);
			SelectObject(memory, previous);
			DeleteObject(bitmap);
			DeleteDC(memory);
			ReleaseDC(nullptr, screen);
			break;
		}
		EndMenu();
	}
	if(ticks > 10) { EndMenu(); KillTimer(nullptr, timer); }
}

/** Open a menu of items Windows draws itself, on the menu background DWT gives a dark palette. */
Capture openNativeMenu(HWND owner, COLORREF background) {
	auto menu = CreatePopupMenu();
	auto brush = CreateSolidBrush(background);
	MENUINFO info { sizeof(info), MIM_BACKGROUND };
	info.hbrBack = brush;
	SetMenuInfo(menu, &info);
	for(auto text : { L"Open", L"Edit with a long name", L"Print" }) AppendMenu(menu, MF_STRING, 100, text);
	AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenu(menu, MF_STRING, 101, L"Properties");
	capture = {};
	ticks = 0;
	SetForegroundWindow(owner);
	const auto timer = SetTimer(nullptr, 0, 100, capturePopup);
	check(timer != 0, "Could not supervise the menu");
	TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, 200, 200, 0, owner, nullptr);
	KillTimer(nullptr, timer);
	DestroyMenu(menu);
	DeleteObject(brush);
	check(capture.width > 20 && capture.height > 40, "The menu window was not found");
	return capture;
}

/** The lightest and darkest brightness along a column of the menu, inside its border. */
std::pair<int, int> columnRange(const Capture& menu, int x) {
	int lightest = 0, darkest = 255;
	for(int y = 4; y < menu.height - 4; ++y) {
		const auto value = brightness(menu.at(x, y));
		lightest = std::max(lightest, value);
		darkest = std::min(darkest, value);
	}
	return { lightest, darkest };
}
}

int dwtMain(dwt::Application& app) {
	try {
		if(!dwt::util::win32::ensureVersion(dwt::util::win32::TEN_1909)) {
			std::puts("SKIP: Windows draws its own menus dark from Windows 10 1903");
			return 0;
		}
		auto window = new dwt::Window();
		dwt::Window::Seed seed;
		seed.caption = L"Native menu appearance test";
		seed.location = dwt::Rectangle(100, 100, 200, 100);
		window->create(seed);

		// A dark custom palette, as the user's themes are.
		const dwt::Appearance::Palette dark = { RGB(0x06, 0x07, 0x0D), RGB(0x10, 0x13, 0x21), RGB(0xE6, 0xF1, 0xFF),
			RGB(0x88, 0x96, 0xB8), RGB(0x30, 0x3B, 0x59), RGB(0x02, 0x80, 0xCA), RGB(0x06, 0x07, 0x0D) };
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, dark, dwt::Appearance::Rendering::CustomPalette);
		auto menu = openNativeMenu(window->handle(), dark.surface);
		// The border, the icon column and the separator: nothing light anywhere left of the text.
		for(int x = 0; x < 3; ++x) {
			check(brightness(menu.at(x, menu.height / 2)) < 110, "A dark palette's native menu has a light border");
		}
		check(columnRange(menu, 8).first < 110, "A dark palette's native menu has a light icon column");
		bool lightText = false;
		for(int x = menu.width / 4; x < menu.width / 2 && !lightText; ++x) {
			lightText = columnRange(menu, x).first > 180;
		}
		check(lightText, "A dark palette's native menu draws its text dark");

		// A light palette leaves Windows' own menus light.
		const dwt::Appearance::Palette light = { RGB(0xFA, 0xFA, 0xF7), RGB(0xF0, 0xF1, 0xEC), RGB(0x27, 0x31, 0x3A),
			RGB(0x62, 0x6C, 0x72), RGB(0xB5, 0xBD, 0xBB), RGB(0x31, 0x5E, 0x9C), RGB(0xFF, 0xFF, 0xFF) };
		app.getAppearance().configure(dwt::Appearance::Mode::Light, light, dwt::Appearance::Rendering::CustomPalette);
		menu = openNativeMenu(window->handle(), light.surface);
		check(columnRange(menu, 8).second > 150, "A light palette's native menu has a dark icon column");

		DestroyWindow(window->handle());
		std::puts("PASS: menus Windows draws itself follow a dark palette, and stay light with a light one");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}
