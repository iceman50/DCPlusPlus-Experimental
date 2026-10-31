// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/ToolBar.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}
dwt::Appearance::Palette palette;
bool foundDialog, correctBackground;
bool manualPalette, correctRows, correctText, foundIcon;
bool stableHover;
HWND hoverButton;
struct PaintCounts { unsigned dialog = 0, button = 0; };
unsigned listCount;
UINT_PTR timer;

LRESULT CALLBACK countPaints(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	if(message == WM_PAINT) ++*reinterpret_cast<unsigned*>(data);
	return DefSubclassProc(window, message, wParam, lParam);
}

bool checkHoverPainting(HWND dialog) {
	if(!hoverButton) return false;
	PaintCounts paints;
	SetWindowSubclass(dialog, countPaints, 1, reinterpret_cast<DWORD_PTR>(&paints.dialog));
	SetWindowSubclass(hoverButton, countPaints, 1, reinterpret_cast<DWORD_PTR>(&paints.button));
	RedrawWindow(dialog, nullptr, nullptr, RDW_VALIDATE | RDW_NOERASE | RDW_NOFRAME | RDW_NOINTERNALPAINT | RDW_ALLCHILDREN);
	bool idleHover = true;
	for(int crossing = 0; crossing < 20; ++crossing) {
		// Cross the background into a button, then move within that same button.
		SendMessage(dialog, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2));
		UpdateWindow(dialog);
		SendMessage(dialog, WM_MOUSELEAVE, 0, 0);
		UpdateWindow(dialog);
		SendMessage(hoverButton, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
		UpdateWindow(hoverButton);
		const auto afterEnter = paints.button;
		for(int move = 0; move < 10; ++move) {
			SendMessage(hoverButton, WM_MOUSEMOVE, 0, MAKELPARAM(6 + move, 5));
			UpdateWindow(hoverButton);
		}
		idleHover = idleHover && paints.button == afterEnter;
		SendMessage(hoverButton, WM_MOUSELEAVE, 0, 0);
		UpdateWindow(hoverButton);
		UpdateWindow(dialog);
	}
	RemoveWindowSubclass(hoverButton, countPaints, 1);
	RemoveWindowSubclass(dialog, countPaints, 1);
	std::printf("Mouse crossings: dialog paints=%u, button paints=%u, stable button hover=%d\n",
		paints.dialog, paints.button, idleHover);
	return paints.dialog == 0 && paints.button == 40 && idleHover &&
		(GetWindowLongPtr(dialog, GWL_STYLE) & WS_CLIPCHILDREN);
}

void capture(HWND window) {
	RECT rect;
	GetClientRect(window, &rect);
	auto dc = GetDC(window);
	auto memory = CreateCompatibleDC(dc);
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = rect.right;
	info.bmiHeader.biHeight = -rect.bottom;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	void* pixels = nullptr;
	auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
	auto previous = SelectObject(memory, bitmap);
	SendMessage(window, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
	BITMAPFILEHEADER header {};
	header.bfType = 0x4D42;
	header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
	header.bfSize = header.bfOffBits + rect.right * rect.bottom * 4;
	if(auto file = std::fopen(manualPalette ? "build/toolbar-customize-dark.bmp" : "build/toolbar-customize-light.bmp", "wb")) {
		std::fwrite(&header, sizeof(header), 1, file);
		std::fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, file);
		std::fwrite(pixels, rect.right * rect.bottom * 4, 1, file);
		std::fclose(file);
	}
	SelectObject(memory, previous);
	DeleteObject(bitmap);
	DeleteDC(memory);
	ReleaseDC(window, dc);
}
BOOL CALLBACK inspectChild(HWND child, LPARAM) {
	wchar_t name[128];
	GetClassName(child, name, 128);
	if(!hoverButton && std::wstring(name) == L"Button" && IsWindowEnabled(child)) hoverButton = child;
	if(std::wstring(name) == L"ListBox") {
		++listCount;
		auto dc = GetDC(child);
		auto memory = CreateCompatibleDC(dc);
		auto bitmap = CreateCompatibleBitmap(dc, 240, 40);
		auto previous = SelectObject(memory, bitmap);
		DRAWITEMSTRUCT item { ODT_LISTBOX, static_cast<UINT>(GetDlgCtrlID(child)), 1, ODA_DRAWENTIRE,
			0, child, memory, { 0, 0, 240, 40 }, static_cast<ULONG_PTR>(SendMessage(child, LB_GETITEMDATA, 1, 0)) };
		SendMessage(GetParent(child), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
		const auto background = manualPalette ? palette.surface : GetSysColor(COLOR_WINDOW);
		correctRows = correctRows && GetPixel(memory, 230, 20) == background;
		bool hasText = false;
		for(int y = 1; y < 39; ++y) for(int x = 25; x < 210; ++x) {
			if(GetPixel(memory, x, y) == (manualPalette ? palette.text : GetSysColor(COLOR_WINDOWTEXT))) hasText = true;
		}
		// In light mode comctl32 owns disabled placeholder text colors.
		if(manualPalette) correctText = correctText && hasText;
		wchar_t label[256] {};
		SendMessage(child, LB_GETTEXT, 1, reinterpret_cast<LPARAM>(label));
		if(std::wstring(label) == L"Second button") {
			for(int y = 12; y < 28; ++y) for(int x = 2; x < 18; ++x) {
				if(GetPixel(memory, x, y) != background) foundIcon = true;
			}
		}
		item.itemState = ODS_SELECTED;
		SendMessage(GetParent(child), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
		if(manualPalette) correctRows = correctRows && GetPixel(memory, 230, 20) == palette.accent;
		SelectObject(memory, previous);
		DeleteObject(bitmap);
		DeleteDC(memory);
		ReleaseDC(child, dc);
	}
	return TRUE;
}
BOOL CALLBACK inspectDialog(HWND window, LPARAM) {
	wchar_t name[128];
	GetClassName(window, name, 128);
	if(std::wstring(name) != L"#32770") return TRUE;
	KillTimer(nullptr, timer);
	foundDialog = true;
	SetWindowPos(window, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	auto dc = GetDC(window);
	auto brush = reinterpret_cast<HBRUSH>(SendMessage(window, WM_CTLCOLORDLG, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(window)));
	LOGBRUSH value {};
	correctBackground = GetObject(brush, sizeof(value), &value) &&
		value.lbColor == (manualPalette ? palette.background : GetSysColor(COLOR_BTNFACE));
	ReleaseDC(window, dc);
	EnumChildWindows(window, inspectChild, 0);
	if(manualPalette) stableHover = checkHoverPainting(window);
	capture(window);
	PostMessage(window, WM_CLOSE, 0, 0);
	return FALSE;
}
void CALLBACK inspectTimer(HWND, UINT, UINT_PTR, DWORD) {
	EnumThreadWindows(GetCurrentThreadId(), inspectDialog, 0);
}
}

int dwtMain(dwt::Application& app) {
	try {
		palette = dwt::Appearance::defaultPalette();
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, palette);
		auto window = new dwt::Window();
		window->create();
		window->resize(dwt::Rectangle(-32000, -32000, 600, 300));
		auto toolbar = window->addChild(dwt::ToolBar::Seed());
		dwt::ImageListPtr images = new dwt::ImageList(dwt::Point(16, 16));
		dwt::IconPtr icon = new dwt::Icon(LoadIcon(nullptr, IDI_APPLICATION), false);
		images->add(*icon);
		toolbar->setNormalImageList(images);
		toolbar->addButton("first", 0, L"First button", true, 0, [] { });
		toolbar->addButton("second", 0, L"Second button", true, 0, [] { });
		toolbar->setLayout({ "first" });
		toolbar->resize(dwt::Rectangle(0, 0, 580, 35));
		ShowWindow(window->handle(), SW_SHOWNOACTIVATE);
		for(int scenario = 0; scenario < 3; ++scenario) {
			palette.background = RGB(18, 28, 38);
			palette.surface = RGB(28, 38, 48);
			palette.text = RGB(225, 235, 245);
			palette.accent = RGB(135, 45, 165);
			if(scenario == 2) {
				palette = { RGB(245, 245, 245), RGB(255, 255, 255), RGB(25, 25, 25),
					RGB(110, 110, 110), RGB(175, 175, 175), RGB(0, 90, 180), RGB(255, 255, 255) };
			}
			app.getAppearance().configure(scenario == 0 ? dwt::Appearance::Mode::Dark : dwt::Appearance::Mode::Light,
				palette, scenario == 2 ? dwt::Appearance::Rendering::CustomPalette : dwt::Appearance::Rendering::Automatic);
			palette = app.getAppearance().getPalette();
			manualPalette = app.getAppearance().isManual();
			for(bool doubleClick : { false, true }) {
				foundDialog = correctBackground = false;
				correctRows = correctText = true;
				foundIcon = false;
				stableHover = true;
				hoverButton = nullptr;
				listCount = 0;
				timer = SetTimer(nullptr, 0, 100, inspectTimer);
				check(timer != 0, "Cannot observe toolbar customization");
				if(doubleClick) toolbar->sendMessage(WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(500, 15));
				else toolbar->customize();
				KillTimer(nullptr, timer);
				check(foundDialog, "Customize did not open the native dialog");
				check(correctBackground, "Customize dialog did not inherit the dark palette");
				check(listCount == 2, "Customize dialog lost its button lists");
				check(correctRows, "Customize list rows retained system background/selection colors");
				check(correctText, "Customize list labels did not inherit text colors");
				check(foundIcon, "Customize list lost toolbar icons");
				check(stableHover, "Mouse movement repainted the dialog or failed to isolate button hover painting");
				check(toolbar->getLayout() == std::vector<std::string> { "first" }, "Closing customize changed the toolbar layout");
			}
		}
		DestroyWindow(window->handle());
		std::puts("Toolbar customization palette and double-click entry checks passed.");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
