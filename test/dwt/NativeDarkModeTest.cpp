// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// Native dark mode (Rendering::NativeDark): Windows draws dark what it can (the caption, the menus
// and the controls with a dark visual style) and DWT paints the rest in Windows' dark colours; a
// light mode, or the Automatic rendering, undoes all of it.
#include <dwt/Application.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/ListBox.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/Window.h>
#include <dwt/util/win32/DarkMode.h>
#include <dwt/util/win32/Dwm.h>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using dwt::util::win32::darkmode::Theme;

void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void dispatch(dwt::Application& app) {
	for(unsigned i = 0; i < 1000 && app.dispatch(); ++i) { }
}

/** The dark visual style Appearance gave a window (Theme::None when it has its usual one). */
Theme nativeTheme(HWND window) {
	return static_cast<Theme>(reinterpret_cast<UINT_PTR>(GetProp(window, L"dwt.Appearance.NativeTheme")));
}

bool ownerDrawn(HMENU menu, UINT position) {
	MENUITEMINFO info = { sizeof(MENUITEMINFO), MIIM_FTYPE };
	check(GetMenuItemInfo(menu, position, TRUE, &info), "Could not read a menu item");
	return (info.fType & MFT_OWNERDRAW) != 0;
}

bool darkCaption(HWND window) {
	BOOL dark = FALSE;
	return SUCCEEDED(dwt::util::win32::dwm::getWindowAttribute(window, dwt::util::win32::dwm::darkModeAttribute,
		&dark, sizeof(dark))) && dark;
}

int brightness(DWORD pixel) {
	return (((pixel >> 16) & 0xff) * 299 + ((pixel >> 8) & 0xff) * 587 + (pixel & 0xff) * 114) / 1000;
}

/** The lightest and darkest pixels of a header as it paints itself (custom draw included). */
std::pair<int, int> headerBrightness(HWND header) {
	RECT client = { 0 };
	GetClientRect(header, &client);
	check(client.right > 20 && client.bottom > 4, "The header has no size");
	BITMAPINFO info {};
	info.bmiHeader = { sizeof(BITMAPINFOHEADER), client.right, -client.bottom, 1, 32 };
	void* bits = nullptr;
	auto screen = GetDC(nullptr);
	auto dc = CreateCompatibleDC(screen);
	auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	auto previous = SelectObject(dc, bitmap);
	SendMessage(header, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
	GdiFlush();
	int lightest = 0, darkest = 255;
	const auto pixels = static_cast<const DWORD*>(bits);
	for(long i = 0, n = client.right * client.bottom; i < n; ++i) {
		lightest = std::max(lightest, brightness(pixels[i]));
		darkest = std::min(darkest, brightness(pixels[i]));
	}
	SelectObject(dc, previous);
	DeleteObject(bitmap);
	DeleteDC(dc);
	ReleaseDC(nullptr, screen);
	return { lightest, darkest };
}

/** The brightness in the middle of a window's vertical scroll bar, as the window paints itself. */
int scrollBarBrightness(HWND window) {
	RECT bounds = { 0 };
	GetWindowRect(window, &bounds);
	const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
	SCROLLBARINFO bar = { sizeof(SCROLLBARINFO) };
	check(GetScrollBarInfo(window, OBJID_VSCROLL, &bar) && !(bar.rgstate[0] & STATE_SYSTEM_INVISIBLE),
		"The window has no vertical scroll bar");
	BITMAPINFO info {};
	info.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32 };
	void* bits = nullptr;
	auto screen = GetDC(nullptr);
	auto dc = CreateCompatibleDC(screen);
	auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	auto previous = SelectObject(dc, bitmap);
	PrintWindow(window, dc, 0);
	GdiFlush();
	// Halfway down the track, between the arrows: no thumb in a short, unscrolled text.
	const int x = (bar.rcScrollBar.left + bar.rcScrollBar.right) / 2 - bounds.left;
	const int y = bar.rcScrollBar.bottom - bounds.top - (bar.dxyLineButton * 3) / 2;
	const auto value = brightness(static_cast<const DWORD*>(bits)[static_cast<size_t>(y) * width + x]);
	SelectObject(dc, previous);
	DeleteObject(bitmap);
	DeleteDC(dc);
	ReleaseDC(nullptr, screen);
	return value;
}

/** The lightest and darkest pixels of a list box's selected item, as it paints itself. */
std::pair<int, int> selectedItemBrightness(HWND list) {
	RECT item = { 0 };
	check(SendMessage(list, LB_GETITEMRECT, SendMessage(list, LB_GETCURSEL, 0, 0), reinterpret_cast<LPARAM>(&item)) != LB_ERR,
		"The list box has no selected item");
	RECT client = { 0 };
	GetClientRect(list, &client);
	BITMAPINFO info {};
	info.bmiHeader = { sizeof(BITMAPINFOHEADER), client.right, -client.bottom, 1, 32 };
	void* bits = nullptr;
	auto screen = GetDC(nullptr);
	auto dc = CreateCompatibleDC(screen);
	auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	auto previous = SelectObject(dc, bitmap);
	SendMessage(list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
	GdiFlush();
	int lightest = 0, darkest = 255;
	for(int y = item.top; y < item.bottom && y < client.bottom; ++y) {
		for(int x = 0; x < client.right; ++x) {
			const auto value = brightness(static_cast<const DWORD*>(bits)[static_cast<size_t>(y) * client.right + x]);
			lightest = std::max(lightest, value);
			darkest = std::min(darkest, value);
		}
	}
	SelectObject(dc, previous);
	DeleteObject(bitmap);
	DeleteDC(dc);
	ReleaseDC(nullptr, screen);
	return { lightest, darkest };
}

/** Have the window paint its menu bar's background (WM_UAHDRAWMENU) into a bitmap, and return the
 * colour in the middle of the bar, or CLR_INVALID if the window left it to Windows. */
COLORREF menuBarColor(HWND window) {
	RECT windowRect = { 0 };
	GetWindowRect(window, &windowRect);
	MENUBARINFO bar = { sizeof(MENUBARINFO) };
	check(GetMenuBarInfo(window, OBJID_MENU, 0, &bar) && bar.rcBar.right > bar.rcBar.left, "No menu bar");
	const auto width = windowRect.right - windowRect.left, height = windowRect.bottom - windowRect.top;
	BITMAPINFO info {};
	info.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32 };
	void* bits = nullptr;
	auto screen = GetDC(nullptr);
	auto dc = CreateCompatibleDC(screen);
	auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	auto previous = SelectObject(dc, bitmap);
	std::fill_n(static_cast<DWORD*>(bits), static_cast<size_t>(width) * height, 0x00ff00ff);
	dwt::util::win32::darkmode::UAHMENU menu = { GetMenu(window), dc, 0 };
	const auto handled = SendMessage(window, dwt::util::win32::darkmode::WM_UAHDRAWMENU, 0, reinterpret_cast<LPARAM>(&menu));
	GdiFlush();
	const int x = (bar.rcBar.left + bar.rcBar.right) / 2 - windowRect.left;
	const int y = (bar.rcBar.top + bar.rcBar.bottom) / 2 - windowRect.top;
	const auto pixel = static_cast<DWORD*>(bits)[static_cast<size_t>(y) * width + x];
	SelectObject(dc, previous);
	DeleteObject(bitmap);
	DeleteDC(dc);
	ReleaseDC(nullptr, screen);
	return handled ? RGB((pixel >> 16) & 0xff, (pixel >> 8) & 0xff, pixel & 0xff) : CLR_INVALID;
}

}

int dwtMain(dwt::Application& app) {
	try {
		using Mode = dwt::Appearance::Mode;
		using Rendering = dwt::Appearance::Rendering;
		auto& appearance = app.getAppearance();
		if(!dwt::Appearance::supportsNativeDark()) {
			check(!dwt::util::win32::darkmode::setApplicationDark(true), "Native dark mode half available");
			std::puts("SKIP: Windows draws dark itself from Windows 10 1903");
			return 0;
		}

		// A custom dark palette, as the user's themes are; native dark mode doesn't use it.
		const dwt::Appearance::Palette palette = { RGB(0x06, 0x07, 0x0D), RGB(0x10, 0x13, 0x21), RGB(0xE6, 0xF1, 0xFF),
			RGB(0x88, 0x96, 0xB8), RGB(0x30, 0x3B, 0x59), RGB(0x02, 0x80, 0xCA), RGB(0x06, 0x07, 0x0D) };
		appearance.configure(Mode::Dark, palette);

		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed(L"Native dark mode test");
		seed.location = dwt::Rectangle(100, 100, 500, 400);
		window->create(seed);
		auto button = window->addChild(dwt::Button::Seed(L"Button"));
		auto checkBox = window->addChild(dwt::CheckBox::Seed(L"Check box"));
		auto combo = window->addChild(dwt::ComboBox::Seed());
		auto table = window->addChild(dwt::Table::Seed());
		table->resize(dwt::Rectangle(10, 60, 400, 200));
		table->addColumn(L"Header text", 300);
		auto label = window->addChild(dwt::Label::Seed(L"Label"));
		auto list = window->addChild(dwt::ListBox::Seed());
		list->resize(dwt::Rectangle(320, 270, 150, 90));
		for(auto text : { L"First", L"Second item", L"Third" }) list->addValue(text);
		SendMessage(list->handle(), LB_SETCURSEL, 1, 0);
		auto edit = window->addChild(dwt::TextBox::Seed(L"Single line"));
		auto editSeed = dwt::TextBox::Seed(L"Several lines");
		editSeed.style |= ES_MULTILINE | WS_VSCROLL;
		auto multiline = window->addChild(editSeed);
		multiline->resize(dwt::Rectangle(10, 270, 300, 90));
		auto menuSeed = dwt::Menu::Seed();
		menuSeed.popup = false;
		auto menu = window->addChild(menuSeed);
		menu->appendPopup(L"&File")->appendItem(L"Open", [] {});
		menu->setMenu();
		dispatch(app);
		check(window->usesClientFrame() && nativeTheme(button->handle()) == Theme::None && ownerDrawn(GetMenu(window->handle()), 0),
			"The palette's dark mode changed");

		for(int pass = 0; pass < 2; ++pass) {
			appearance.configure(Mode::Dark, palette, Rendering::NativeDark);
			dispatch(app);
			check(appearance.isNativeDark() && appearance.isManual() && appearance.isDark(), "Native dark mode is off");
			check(appearance.getPalette().background == dwt::Appearance::nativeDarkPalette().background &&
				appearance.getConfiguredPalette().background == palette.background,
				"Native dark mode paints in the configured palette, or lost it");

			// Windows draws the caption, dark, and the menus.
			const auto hwnd = window->handle();
			check(!window->usesClientFrame() && darkCaption(hwnd), "The caption isn't Windows' dark one");
			check(!ownerDrawn(GetMenu(hwnd), 0), "The menu bar is still drawn by DWT");
			check(menuBarColor(hwnd) == appearance.getPalette().background, "The menu bar isn't painted dark");

			// Controls with a dark visual style get it; the rest keeps DWT's painting.
			check(nativeTheme(button->handle()) == Theme::Explorer, "A push button isn't dark");
			check(nativeTheme(combo->handle()) == Theme::Cfd, "A combo box isn't dark");
			check(nativeTheme(table->handle()) == Theme::Explorer, "A list isn't dark");
			check(nativeTheme(ListView_GetHeader(table->handle())) == Theme::ItemsView, "A list's header isn't dark");
			// Windows draws its text black unless told otherwise.
			const auto header = headerBrightness(ListView_GetHeader(table->handle()));
			check(header.second < 80 && header.first > 180, "A list's header isn't light text on a dark background");
			check(nativeTheme(checkBox->handle()) ==
				(dwt::util::win32::darkmode::hasDarkTheme() ? Theme::DarkTheme : Theme::None),
				"A check box has the wrong style for this Windows");
			check(nativeTheme(label->handle()) == Theme::None, "A label got a visual style");
			check(nativeTheme(edit->handle()) == Theme::Cfd, "A text box isn't dark");
			// DarkMode_CFD has light scroll bars.
			check(nativeTheme(multiline->handle()) == Theme::Explorer, "A multi-line text box's scroll bars aren't dark");
			check(scrollBarBrightness(multiline->handle()) < 80, "A multi-line text box's scroll bar is drawn light");
			// Windows' dark selection is a white bar, under the palette's white text.
			const auto selected = selectedItemBrightness(list->handle());
			check(selected.first - selected.second > 100, "A list box's selected item can't be read");
			check(ListView_GetBkColor(table->handle()) == appearance.getPalette().background,
				"Windows draws the list, but not in the palette's colours");

			// A widget created afterwards is dark at once.
			auto later = window->addChild(dwt::Button::Seed(L"Later"));
			check(nativeTheme(later->handle()) == Theme::Explorer, "A new push button isn't dark");
			DestroyWindow(later->handle());

			// Light mode leaves everything to Windows' light drawing, as before.
			appearance.configure(Mode::Light, palette, Rendering::NativeDark);
			dispatch(app);
			check(!appearance.isNativeDark() && !appearance.isManual(), "Light mode is still dark");
			check(!window->usesClientFrame() && !darkCaption(hwnd), "Light mode kept the dark caption");
			check(nativeTheme(button->handle()) == Theme::None && nativeTheme(table->handle()) == Theme::None &&
				nativeTheme(ListView_GetHeader(table->handle())) == Theme::None, "Light mode kept dark visual styles");
			check(ownerDrawn(GetMenu(hwnd), 0) && menuBarColor(hwnd) == CLR_INVALID, "Light mode kept native dark menus");

			// The palette's dark mode, the default, paints everything itself again.
			appearance.configure(Mode::Dark, palette);
			dispatch(app);
			check(!appearance.isNativeDark() && appearance.getPalette().background == palette.background,
				"Automatic rendering kept native dark mode");
			check(window->usesClientFrame() && ownerDrawn(GetMenu(hwnd), 0) &&
				nativeTheme(button->handle()) == Theme::None, "Automatic rendering kept Windows' dark drawing");
		}

		DestroyWindow(window->handle());
		std::puts("PASS: native dark mode gives Windows the caption, menus and dark-styled controls, and undoes it all");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}
