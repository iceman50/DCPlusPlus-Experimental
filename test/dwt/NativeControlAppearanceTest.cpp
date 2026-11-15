// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/RadioButton.h>
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

void checkRadio(dwt::Window* window, dwt::Application& app) {
	auto radio = window->addChild(dwt::RadioButton::Seed());
	for(bool light : { false, true }) {
		auto palette = app.getAppearance().getPalette();
		if(light) {
			palette.background = RGB(245, 239, 225);
			palette.surface = RGB(255, 250, 240);
			palette.border = RGB(95, 85, 75);
			palette.accent = RGB(155, 45, 100);
		}
		for(unsigned dpi : { 96u, 120u, 144u, 192u }) {
			radio->sendMessage(WM_DPICHANGED, MAKELONG(dpi, dpi));
			const int width = radio->scale(40), height = radio->scale(24), size = radio->scale(13);
			radio->resize(dwt::Rectangle(0, 0, width, height));
			Surface surface(width, height);
			for(bool checked : { false, true }) for(int interaction = 0; interaction < 4; ++interaction) {
				radio->setEnabled(interaction != 3);
				radio->sendMessage(BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED);
				radio->sendMessage(BM_SETSTATE, interaction == 2);
				NMCUSTOMDRAW draw {};
				draw.dwDrawStage = CDDS_PREPAINT;
				draw.hdc = surface.dc;
				draw.rc = { 0, 0, width, height };
				draw.uItemState = interaction == 3 ? CDIS_DISABLED : interaction == 2 ? CDIS_SELECTED : interaction == 1 ? CDIS_HOT : 0;
				dwt::appearance_detail::drawButton(*radio, draw, palette);
				const int left = 1, top = (height - size) / 2;
				unsigned blended = 0;
				for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) {
					const auto pixel = GetPixel(surface.dc, left + x, top + y);
					check(pixel == GetPixel(surface.dc, left + size - 1 - x, top + y) &&
						pixel == GetPixel(surface.dc, left + x, top + size - 1 - y),
						"Radio ring or dot is not centered and symmetric");
					if(pixel != palette.background && pixel != palette.surface &&
						pixel != palette.border && pixel != palette.accent) ++blended;
				}
				check(blended >= static_cast<unsigned>(size), "Radio edges have no antialiasing coverage");
				check(GetPixel(surface.dc, left, top) == palette.background,
					"Radio corners do not match the surrounding background");
				check(GetPixel(surface.dc, 0, 0) == palette.background,
					"Radio drawing escaped its glyph bounds");
				if(interaction == 0)
					check(GetPixel(surface.dc, left + size / 2, top + size / 2) ==
						(checked ? palette.accent : palette.surface), "Radio selection dot has the wrong state or color");
			}
		}
	}
	DestroyWindow(radio->handle());
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

void checkFilenameCombo(dwt::Application& app) {
	auto parent = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WC_STATIC, L"Filename combo test",
		WS_POPUP, 20, 20, 400, 240, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
	SetLayeredWindowAttributes(parent, 0, 0, LWA_ALPHA);
	ShowWindow(parent, SW_HIDE);
	auto comboEx = CreateWindowEx(0, WC_COMBOBOXEX, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
		5, 5, 320, 160, parent, nullptr, GetModuleHandle(nullptr), nullptr);
	check(comboEx != nullptr, "Cannot create filename ComboBoxEx fixture");
	SendMessage(comboEx, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
	for(auto text : { L"Readable file list.xml.bz2", L"Another file list.xml.zst", L"Third file.xml" }) {
		COMBOBOXEXITEM item {};
		item.mask = CBEIF_TEXT;
		item.iItem = -1;
		item.pszText = const_cast<LPWSTR>(text);
		check(SendMessage(comboEx, CBEM_INSERTITEM, 0, reinterpret_cast<LPARAM>(&item)) >= 0, "Cannot add filename history item");
	}
	dwt::util::win32::NativeDialogAppearance scope(app.getAppearance(), true);
	scope.apply(parent);
	ShowWindow(parent, SW_SHOWNOACTIVATE);
	auto combo = reinterpret_cast<HWND>(SendMessage(comboEx, CBEM_GETCOMBOCONTROL, 0, 0));
	SendMessage(combo, CB_SHOWDROPDOWN, TRUE, 0);
	COMBOBOXINFO info { sizeof(info) };
	check(GetComboBoxInfo(combo, &info), "Cannot inspect filename dropdown");
	RECT bounds {}, row {};
	GetClientRect(info.hwndList, &bounds);
	SendMessage(info.hwndList, LB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&row));
	Surface image(bounds.right, bounds.bottom);
	SendMessage(info.hwndList, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(image.dc), PRF_CLIENT);
	const auto& palette = app.getAppearance().getPalette();
	unsigned textPixels = 0, surfacePixels = 0, whitePixels = 0;
	for(int y = row.top; y < row.bottom; ++y) for(int x = row.left; x < row.right; ++x) {
		const auto pixel = GetPixel(image.dc, x, y);
		if(pixel == palette.text) ++textPixels;
		if(pixel == palette.surface) ++surfacePixels;
		if(pixel == RGB(255, 255, 255)) ++whitePixels;
	}
	if(textPixels <= 10 || surfacePixels <= 100)
		std::printf("Filename dropdown: text=%u, surface=%u, white=%u\n", textPixels, surfacePixels, whitePixels);
	check(textPixels > 10 && surfacePixels > 100, "Filename history dropdown text/background does not use the readable theme palette");
	SendMessage(combo, CB_SETCURSEL, 0, 0);
	SendMessage(combo, WM_KEYDOWN, VK_DOWN, 0);
	check(SendMessage(combo, CB_GETCURSEL, 0, 0) == 1, "Filename dropdown lost keyboard selection");
	SendMessage(info.hwndList, LB_GETITEMRECT, 1, reinterpret_cast<LPARAM>(&row));
	SendMessage(info.hwndList, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(image.dc), PRF_CLIENT);
	textPixels = surfacePixels = 0;
	for(int y = row.top; y < row.bottom; ++y) for(int x = row.left; x < row.right; ++x) {
		const auto pixel = GetPixel(image.dc, x, y);
		if(pixel == palette.highlightText) ++textPixels;
		if(pixel == palette.accent) ++surfacePixels;
	}
	check(textPixels > 10 && surfacePixels > 100, "Selected filename history row is unreadable");
	SendMessage(combo, CB_SHOWDROPDOWN, FALSE, 0);
	SetWindowText(comboEx, L"Typed file list.xml.bz2");
	wchar_t typed[100] {};
	GetWindowText(comboEx, typed, 100);
	check(std::wstring(typed) == L"Typed file list.xml.bz2", "Filename field lost editable text");
	DestroyWindow(parent);
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
		checkRadio(window, app);
		std::puts("Radio antialiasing, symmetry, states, palettes and DPI passed.");
		checkFilenameCombo(app);
		auto lightPalette = dwt::Appearance::defaultPalette();
		lightPalette.background = RGB(245, 239, 225);
		lightPalette.surface = RGB(255, 250, 240);
		lightPalette.text = RGB(35, 30, 25);
		app.getAppearance().configure(dwt::Appearance::Mode::Light, lightPalette, dwt::Appearance::Rendering::CustomPalette);
		checkFilenameCombo(app);
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		std::puts("Filename history dropdown palette, selection and editing passed.");
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
