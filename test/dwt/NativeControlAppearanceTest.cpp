// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/FontDialog.h>
#include <dwt/util/win32/NativeDialogAppearance.h>
#include <dwt/resources/Brush.h>
#include <dwt/dwt_vsstyle.h>
#include <uxtheme.h>
#include "../../dwt/src/widgets/AppearanceDraw.h"
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}
struct Surface {
	HDC dc = CreateCompatibleDC(nullptr);
	HBITMAP bitmap;
	HGDIOBJ previous;
	Surface(int width, int height) {
		BITMAPINFO info {};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		void* bits;
		bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		check(bitmap != nullptr, "Cannot create appearance test bitmap");
		previous = SelectObject(dc, bitmap);
	}
	~Surface() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
};

void checkCheckbox(dwt::Window* window, dwt::Application& app) {
	auto seed = dwt::CheckBox::Seed();
	seed.style = (seed.style & ~BS_TYPEMASK) | BS_AUTO3STATE;
	auto box = window->addChild(seed);
	auto theme = OpenThemeData(nullptr, L"Button");
	check(theme != nullptr, "Checkbox comparison needs Windows visual styles");
	for(unsigned dpi : { 96u, 192u }) {
		box->sendMessage(WM_DPICHANGED, MAKELONG(dpi, dpi));
		const int width = box->scale(40), height = box->scale(24), glyphSize = box->scale(13);
		box->resize(dwt::Rectangle(0, 0, width, height));
		Surface actual(width, height), expected(width, height);
		for(int state : { BST_UNCHECKED, BST_CHECKED, BST_INDETERMINATE }) {
			for(int interaction = 0; interaction < 4; ++interaction) {
				box->setEnabled(interaction != 3);
				box->sendMessage(BM_SETCHECK, state);
				box->sendMessage(BM_SETSTATE, interaction == 2);
				NMCUSTOMDRAW draw {};
				draw.dwDrawStage = CDDS_PREPAINT;
				draw.hdc = actual.dc;
				draw.rc = { 0, 0, width, height };
				draw.uItemState = interaction == 3 ? CDIS_DISABLED : interaction == 2 ? CDIS_SELECTED : interaction == 1 ? CDIS_HOT : 0;
				const auto& palette = app.getAppearance().getPalette();
				dwt::appearance_detail::drawButton(*box, draw, palette);
				RECT all { 0, 0, width, height };
				dwt::Brush background(palette.background);
				FillRect(expected.dc, &all, background.handle());
				RECT glyph { 1, (height - glyphSize) / 2, 1 + glyphSize, (height + glyphSize) / 2 };
				const int nativeState = (state == BST_INDETERMINATE ? CBS_MIXEDNORMAL :
					state == BST_CHECKED ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL) + interaction;
				DrawThemeBackground(theme, expected.dc, BP_CHECKBOX, nativeState, &glyph, nullptr);
				for(int y = 0; y < height; ++y) for(int x = 0; x < width; ++x)
					check(GetPixel(actual.dc, x, y) == GetPixel(expected.dc, x, y),
						"Custom checkbox pixels differ from the Windows checkbox glyph");
			}
		}
	}
	CloseThemeData(theme);
	DestroyWindow(box->handle());
}

HWND checkScroll(HWND list, COLORREF surface) {
	auto overlay = reinterpret_cast<HWND>(GetProp(list, L"dwt.Appearance.VerticalScrollDecoration"));
	if(!overlay || !IsWindowVisible(overlay)) {
		SCROLLBARINFO diagnostic { sizeof(diagnostic) };
		GetScrollBarInfo(list, OBJID_VSCROLL, &diagnostic);
		wchar_t name[128] {};
		GetClassName(list, name, 128);
		std::fwprintf(stderr, L"Missing scroll decoration: %ls, host visible=%d, scrollbar state=%lu, overlay=%p\n",
			name, IsWindowVisible(list), diagnostic.rgstate[0], overlay);
	}
	check(overlay && IsWindowVisible(overlay), "Native list scrollbar has no visible dark decoration");
	SCROLLBARINFO scroll { sizeof(scroll) };
	check(GetScrollBarInfo(list, OBJID_VSCROLL, &scroll), "Cannot inspect native scrollbar");
	RECT bounds {};
	GetWindowRect(overlay, &bounds);
	check(EqualRect(&bounds, &scroll.rcScrollBar), "Dark scrollbar does not follow its native geometry");
	check(SendMessage(overlay, WM_NCHITTEST, 0, MAKELPARAM(bounds.left + 2, bounds.top + 2)) == HTTRANSPARENT,
		"Dark scrollbar intercepts native mouse input");
	const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
	Surface painted(width, height);
	SendMessage(overlay, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(painted.dc), PRF_CLIENT);
	unsigned colored = 0;
	for(int y = 0; y < height; ++y) for(int x = 0; x < width; ++x)
		if(GetPixel(painted.dc, x, y) == surface) ++colored;
	check(colored > static_cast<unsigned>(width * height / 4), "Native scrollbar kept system-light track pixels");
	return overlay;
}

void checkNativeList(dwt::Application& app) {
	auto parent = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WC_STATIC, L"Scroll test",
		WS_POPUP, 20, 20, 300, 240, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
	SetLayeredWindowAttributes(parent, 0, 0, LWA_ALPHA);
	ShowWindow(parent, SW_HIDE);
	auto list = CreateWindowEx(0, WC_LISTBOX, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
		5, 5, 220, 160, parent, nullptr, GetModuleHandle(nullptr), nullptr);
	for(int i = 0; i < 80; ++i) SendMessage(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Scrollable row"));
	{
		dwt::util::win32::NativeDialogAppearance scope(app.getAppearance());
		scope.apply(parent);
	}
	// The subclasses retain their resources after the appearance scope ends.
	ShowWindow(parent, SW_SHOWNOACTIVATE);
	UpdateWindow(list);
	auto overlay = checkScroll(list, app.getAppearance().getPalette().surface);
	SendMessage(list, WM_VSCROLL, SB_LINEDOWN, 0);
	check(SendMessage(list, LB_GETTOPINDEX, 0, 0) > 0, "Themed scrollbar broke native scrolling");
	SetWindowPos(parent, nullptr, 80, 90, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
	checkScroll(list, app.getAppearance().getPalette().surface);
	ShowWindow(parent, SW_HIDE);
	check(!IsWindowVisible(overlay), "Hiding a dialog left a scrollbar decoration visible");
	ShowWindow(parent, SW_SHOWNOACTIVATE);
	checkScroll(list, app.getAppearance().getPalette().surface);
	DestroyWindow(list);
	check(!IsWindow(overlay), "Destroying a native list left its scrollbar decoration alive");
	DestroyWindow(parent);
}

UINT_PTR fontTimer;
unsigned fontLists;
std::string fontError;
COLORREF fontSurface;
BOOL CALLBACK inspectFontChild(HWND child, LPARAM) {
	wchar_t name[128] {};
	GetClassName(child, name, 128);
	if(std::wstring(name) != L"ComboBox" || SendMessage(child, CB_GETCOUNT, 0, 0) < 12) return TRUE;
	SendMessage(child, CB_SHOWDROPDOWN, TRUE, 0);
	COMBOBOXINFO combo { sizeof(combo) };
	GetComboBoxInfo(child, &combo);
	auto list = combo.hwndList;
	SCROLLBARINFO info { sizeof(info) };
	if(list && IsWindowVisible(list) && GetScrollBarInfo(list, OBJID_VSCROLL, &info) && !(info.rgstate[0] & STATE_SYSTEM_INVISIBLE)) {
		UpdateWindow(list);
		try { checkScroll(list, fontSurface); ++fontLists; }
		catch(const std::exception& e) { fontError = e.what(); }
	}
	SendMessage(child, CB_SHOWDROPDOWN, FALSE, 0);
	return TRUE;
}
BOOL CALLBACK inspectFont(HWND dialog, LPARAM) {
	wchar_t name[128] {};
	GetClassName(dialog, name, 128);
	if(std::wstring(name) != L"#32770" || !IsWindowVisible(dialog)) return TRUE;
	KillTimer(nullptr, fontTimer);
	EnumChildWindows(dialog, inspectFontChild, 0);
	PostMessage(dialog, WM_COMMAND, IDCANCEL, 0);
	return FALSE;
}
void CALLBACK fontTick(HWND, UINT, UINT_PTR, DWORD) { EnumThreadWindows(GetCurrentThreadId(), inspectFont, 0); }
}

int dwtMain(dwt::Application& app) {
	try {
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed();
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		checkCheckbox(window, app);
		std::puts("Checkbox glyph comparison passed.");
		checkNativeList(app);
		std::puts("Native list scrolling and lifetime passed.");
		fontSurface = app.getAppearance().getPalette().surface;
		fontTimer = SetTimer(nullptr, 0, 150, fontTick);
		check(fontTimer != 0, "Cannot inspect font dialog");
		LOGFONT font {};
		COLORREF color = RGB(255, 255, 255);
		dwt::FontDialog(window).open(font, color);
		KillTimer(nullptr, fontTimer);
		check(fontError.empty(), fontError.c_str());
		check(fontLists > 0, "Font dialog did not exercise native scrolling lists");
		DestroyWindow(window->handle());
		std::puts("PASS: Windows checkbox pixels/states/DPI, native scrollbar palette/input/lifetime, and font picker lists");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}
