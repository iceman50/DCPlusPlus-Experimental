// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/Rebar.h>
#include <dwt/widgets/ToolBar.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/StatusBar.h>
#include <dwt/resources/Bitmap.h>
#include <dwt/resources/Icon.h>
#include <cstdio>
#include <stdexcept>

namespace {

void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void pump() {
	for(unsigned i = 0; i < 1000 && dwt::Application::instance().dispatch(); ++i) { }
}

struct Search {
	const wchar_t* text;
	HWND result;
};

BOOL CALLBACK findButton(HWND window, LPARAM param) {
	auto& search = *reinterpret_cast<Search*>(param);
	wchar_t name[64], text[128];
	GetClassName(window, name, 64);
	GetWindowText(window, text, 128);
	if(dwt::hwnd_cast<dwt::Button*>(window) && wcscmp(text, search.text) == 0) { search.result = window; return FALSE; }
	return TRUE;
}

HWND button(HWND window, const wchar_t* text) {
	Search search = { text, nullptr };
	EnumChildWindows(window, findButton, reinterpret_cast<LPARAM>(&search));
	if(!search.result) std::fwprintf(stderr, L"Missing button: %ls\n", text);
	check(search.result != nullptr, "Missing accessible caption/menu button");
	return search.result;
}

COLORREF background(HWND window, int x = 200, int y = 2) {
	auto dc = GetDC(window);
	auto memory = CreateCompatibleDC(dc);
	dwt::Bitmap bitmap(CreateCompatibleBitmap(dc, 800, 200));
	auto previous = SelectObject(memory, bitmap.handle());
	SendMessage(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
	const auto color = GetPixel(memory, x, y);
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(window, dc);
	return color;
}

void checkSeparator(HWND window, COLORREF border) {
	RECT bounds;
	GetClientRect(window, &bounds);
	auto dc = GetDC(window);
	auto memory = CreateCompatibleDC(dc);
	dwt::Bitmap bitmap(CreateCompatibleBitmap(dc, bounds.right, bounds.bottom));
	auto previous = SelectObject(memory, bitmap.handle());
	SendMessage(window, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
	bool continuous = true;
	for(int x = 0; x < bounds.right; ++x) {
		if(GetPixel(memory, x, bounds.bottom - 1) != border) { continuous = false; break; }
	}
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(window, dc);
	check(continuous, "Caption/menu buttons covered the bottom separator");
}

void snapshot(HWND window, const char* path = "build/client-frame-preview.bmp") {
	RECT rect;
	GetClientRect(window, &rect);
	auto dc = GetDC(window);
	auto memory = CreateCompatibleDC(dc);
	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = rect.right;
	info.bmiHeader.biHeight = -rect.bottom;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	void* pixels = nullptr;
	dwt::Bitmap bitmap(CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0));
	check(bitmap.handle() != nullptr, "Cannot capture client frame");
	auto previous = SelectObject(memory, bitmap.handle());
	SendMessage(window, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
	BITMAPFILEHEADER header = {};
	header.bfType = 0x4d42;
	header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
	header.bfSize = header.bfOffBits + rect.right * rect.bottom * 4;
	if(auto output = std::fopen(path, "wb")) {
		std::fwrite(&header, sizeof(header), 1, output);
		std::fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, output);
		std::fwrite(pixels, rect.right * rect.bottom * 4, 1, output);
		std::fclose(output);
	}
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(window, dc);
}

COLORREF buttonPixel(HWND window, UINT state, int x, int y) {
	NMCUSTOMDRAW draw = {};
	draw.hdr.hwndFrom = window;
	draw.hdr.idFrom = GetDlgCtrlID(window);
	draw.hdr.code = NM_CUSTOMDRAW;
	draw.dwDrawStage = CDDS_PREPAINT;
	draw.uItemState = state;
	GetClientRect(window, &draw.rc);
	auto dc = GetDC(window);
	draw.hdc = CreateCompatibleDC(dc);
	dwt::Bitmap bitmap(CreateCompatibleBitmap(dc, draw.rc.right, draw.rc.bottom));
	auto previous = SelectObject(draw.hdc, bitmap.handle());
	const auto result = SendMessage(GetParent(window), WM_NOTIFY, draw.hdr.idFrom, reinterpret_cast<LPARAM>(&draw));
	const auto color = GetPixel(draw.hdc, x, y);
	SelectObject(draw.hdc, previous);
	DeleteDC(draw.hdc);
	ReleaseDC(window, dc);
	check(result == CDRF_SKIPDEFAULT, "Caption button did not handle custom drawing");
	return color;
}

HWND popupOwner;
unsigned popupTicks;
bool selectPopup;
bool switchPopup;
COLORREF previousMenuBackground;
COLORREF currentMenuBackground;
COLORREF returnedMenuBackground;
COLORREF departedMenuBackground;
bool systemMenuDrawn;
bool systemMenuSelected;
COLORREF systemMenuColor;
COLORREF systemMenuHighlight;
COLORREF disabledSystemMenuColor;
UINT systemRestoreState;
UINT systemMinimizeState;
LRESULT systemMenuPaintResult;
COLORREF systemMenuPixel(UINT state) {
	MENUITEMINFO item = { sizeof(MENUITEMINFO), MIIM_DATA | MIIM_FTYPE };
	GetMenuItemInfo(GetSystemMenu(popupOwner, FALSE), SC_CLOSE, FALSE, &item);
	systemMenuDrawn = (item.fType & MFT_OWNERDRAW) != 0;
	if(!systemMenuDrawn) return CLR_INVALID;
	auto dc = GetDC(popupOwner);
	auto memory = CreateCompatibleDC(dc);
	dwt::Bitmap bitmap(CreateCompatibleBitmap(dc, 240, 28));
	auto previous = SelectObject(memory, bitmap.handle());
	DRAWITEMSTRUCT draw = {};
	draw.CtlType = ODT_MENU;
	draw.itemID = SC_CLOSE;
	draw.itemAction = ODA_DRAWENTIRE;
	draw.itemState = state;
	draw.hwndItem = reinterpret_cast<HWND>(GetSystemMenu(popupOwner, FALSE));
	draw.hDC = memory;
	draw.rcItem = { 0, 0, 240, 28 };
	draw.itemData = item.dwItemData;
	systemMenuPaintResult = SendMessage(popupOwner, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
	const auto color = GetPixel(memory, 100, 2);
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(popupOwner, dc);
	return color;
}
void CALLBACK closeSystemPopup(HWND, UINT, UINT_PTR timer, DWORD) {
	++popupTicks;
	if(popupTicks == 1) {
		systemMenuColor = systemMenuPixel(0);
		systemMenuHighlight = systemMenuPixel(ODS_SELECTED);
		disabledSystemMenuColor = systemMenuPixel(ODS_SELECTED | ODS_DISABLED | ODS_GRAYED);
		systemRestoreState = GetMenuState(GetSystemMenu(popupOwner, FALSE), SC_RESTORE, MF_BYCOMMAND);
		systemMinimizeState = GetMenuState(GetSystemMenu(popupOwner, FALSE), SC_MINIMIZE, MF_BYCOMMAND);
		for(auto popup = FindWindowEx(nullptr, nullptr, L"#32768", nullptr); popup; popup = FindWindowEx(nullptr, popup, L"#32768", nullptr)) {
			if(GetWindowThreadProcessId(popup, nullptr) == GetCurrentThreadId()) { snapshot(popup, "build/system-menu-preview.bmp"); break; }
		}
		PostMessage(popupOwner, WM_KEYDOWN, systemMenuSelected ? VK_UP : VK_ESCAPE, 0);
	}
	if(popupTicks == 2 && systemMenuSelected) PostMessage(popupOwner, WM_KEYDOWN, VK_RETURN, 0);
	if(popupTicks > 5) { EndMenu(); KillTimer(nullptr, timer); }
}
void CALLBACK closePopup(HWND, UINT, UINT_PTR timer, DWORD) {
	++popupTicks;
	if(popupTicks == 1) PostMessage(popupOwner, WM_KEYDOWN, switchPopup ? VK_RIGHT : selectPopup ? VK_RETURN : VK_ESCAPE, 0);
	if(popupTicks == 2 && switchPopup) {
		previousMenuBackground = background(button(popupOwner, L"&File"), 2, 2);
		currentMenuBackground = background(button(popupOwner, L"&View"), 2, 2);
		PostMessage(popupOwner, WM_KEYDOWN, VK_RETURN, 0);
	}
	if(popupTicks > 5) { EndMenu(); KillTimer(nullptr, timer); }
}

void CALLBACK hoverPopup(HWND, UINT, UINT_PTR timer, DWORD) {
	++popupTicks;
	if(popupTicks == 2) {
		previousMenuBackground = background(button(popupOwner, L"&File"), 2, 2);
		currentMenuBackground = background(button(popupOwner, L"&View"), 2, 2);
	}
	if(popupTicks <= 2) {
		RECT heading;
		GetWindowRect(button(popupOwner, popupTicks == 1 ? L"&View" : L"&File"), &heading);
		MSG hover = {};
		hover.hwnd = popupOwner;
		hover.message = WM_MOUSEMOVE;
		hover.pt = { (heading.left + heading.right) / 2, (heading.top + heading.bottom) / 2 };
		CallMsgFilter(&hover, MSGF_MENU);
	} else if(popupTicks == 3) {
		returnedMenuBackground = background(button(popupOwner, L"&File"), 2, 2);
		departedMenuBackground = background(button(popupOwner, L"&View"), 2, 2);
		EndMenu();
	}
	if(popupTicks > 5) { EndMenu(); KillTimer(nullptr, timer); }
}

}

int dwtMain(dwt::Application& app) {
	try {
		auto palette = dwt::Appearance::defaultPalette();
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, palette);
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed(L"Client frame test");
		seed.location = dwt::Rectangle(-32000, -32000, 650, 450);
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		const auto hwnd = window->handle();
		ShowWindow(hwnd, SW_SHOWNOACTIVATE);
		pump();
		check(window->usesClientFrame(), "Dark window did not select client framing");
		check(!(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CAPTION), "Native caption can still paint");
		check((GetWindowLongPtr(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0, "Resize/snap style was lost");
		RECT outer, client;
		GetWindowRect(hwnd, &outer);
		GetClientRect(hwnd, &client);
		check(outer.right - outer.left == client.right && outer.bottom - outer.top == client.bottom, "Restored frame still has a non-client area");
		const auto caption = GetParent(button(hwnd, L"Close"));
		check(background(caption) == palette.surface, "Caption background does not use the palette");
		const auto close = button(hwnd, L"Close");
		check(buttonPixel(close, CDIS_HOT, 2, 2) == RGB(232, 17, 35), "Close hover background is not red");
		check(buttonPixel(close, CDIS_SELECTED, 2, 2) == RGB(197, 15, 31), "Close pressed background is not darker red");
		RECT closeBounds;
		GetClientRect(close, &closeBounds);
		check(buttonPixel(close, CDIS_HOT, closeBounds.right / 2, closeBounds.bottom / 2) == RGB(255, 255, 255), "Close hover glyph is not white");
		check(buttonPixel(close, 0, 2, 2) == palette.surface, "Close hover background did not clear");
		check(buttonPixel(button(hwnd, L"Minimize"), CDIS_HOT, 2, 2) == dwt::Appearance::blend(palette.surface, palette.accent, 64), "Close hover color leaked into other caption buttons");
		const auto content = window->getContentRect();
		check(content.top() >= 20 && content.bottom() < client.bottom && content.left() > 0 && content.right() < client.right, "Content rectangle does not reserve the caption and resize gutters");
		POINT point = { 250, 18 };
		ClientToScreen(hwnd, &point);
		check(SendMessage(hwnd, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == HTCAPTION, "Caption cannot drag the window");
		point = { 1, 1 };
		ClientToScreen(hwnd, &point);
		check(SendMessage(hwnd, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == HTTOPLEFT, "Corner resizing was lost");

		unsigned command = 0;
		for(auto action: { SC_MINIMIZE, SC_MAXIMIZE, SC_RESTORE, SC_CLOSE }) {
			window->onRaw([&command](WPARAM value, LPARAM) -> LRESULT { command = static_cast<unsigned>(value) & 0xfff0; return 0; }, dwt::Message(WM_SYSCOMMAND, action));
		}
		SendMessage(button(hwnd, L"Minimize"), BM_CLICK, 0, 0);
		pump();
		check(command == SC_MINIMIZE, "Minimize bypassed the owner's system-command handler");
		SendMessage(button(hwnd, L"Close"), BM_CLICK, 0, 0);
		pump();
		check(command == SC_CLOSE && IsWindow(hwnd), "Close bypassed the owner's close policy");
		EnableWindow(hwnd, FALSE);
		check(!IsWindowEnabled(button(hwnd, L"Close")), "Disabled owner retained enabled caption controls");
		check(buttonPixel(close, CDIS_HOT, 2, 2) == palette.surface, "Disabled close button retained a hover highlight");
		EnableWindow(hwnd, TRUE);
		check(IsWindowEnabled(button(hwnd, L"Close")), "Caption did not re-enable");
		window->setText(L"New caption");
		check(window->getText() == L"New caption", "Window title metadata was lost");
		window->setMinimizeBox(false);
		check(!IsWindowEnabled(button(hwnd, L"Minimize")), "Minimize style change was ignored");
		window->setMinimizeBox(true);
		// Exercise maximized layout without moving the test onto the user's desktop.
		const auto restoredStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
		SetWindowLongPtr(hwnd, GWL_STYLE, restoredStyle | WS_MAXIMIZE);
		window->refreshClientFrame();
		check(IsWindowEnabled(button(hwnd, L"Restore")), "Maximized caption disabled Restore");
		SendMessage(button(hwnd, L"Restore"), BM_CLICK, 0, 0);
		pump();
		check(command == SC_RESTORE, "Restore did not issue SC_RESTORE");
		NCCALCSIZE_PARAMS params = {};
		GetWindowRect(hwnd, &params.rgrc[0]);
		window->sendMessage(WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&params));
		MONITORINFO monitor = { sizeof(MONITORINFO) };
		GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
		check(EqualRect(&params.rgrc[0], &monitor.rcWork), "Maximized client can cover the taskbar");
		SetWindowLongPtr(hwnd, GWL_STYLE, restoredStyle);
		window->refreshClientFrame();

		popupOwner = hwnd;
		MENUITEMINFO originalClose = { sizeof(MENUITEMINFO), MIIM_FTYPE | MIIM_DATA | MIIM_BITMAP | MIIM_ID };
		GetMenuItemInfo(GetSystemMenu(hwnd, FALSE), SC_CLOSE, FALSE, &originalClose);
		for(bool chooseClose : { true, false }) {
			window->setMinimizeBox(chooseClose);
			popupTicks = 0;
			systemMenuDrawn = false;
			systemMenuSelected = chooseClose;
			command = 0;
			const auto systemTimer = SetTimer(nullptr, 0, 100, closeSystemPopup);
			check(systemTimer != 0, "Could not supervise system menu test");
			window->sendMessage(WM_SYSCOMMAND, SC_KEYMENU, VK_SPACE);
			KillTimer(nullptr, systemTimer);
			pump();
			check(systemMenuDrawn && systemMenuPaintResult == TRUE, "System menu bypassed DWT owner drawing");
			check(systemMenuColor == palette.surface && systemMenuHighlight == palette.accent, "System menu did not use the dark palette");
			check(disabledSystemMenuColor == palette.surface, "Disabled system menu item acquired a selection highlight");
			check((systemRestoreState & (MF_DISABLED | MF_GRAYED)) != 0, "Restored window exposed an enabled Restore command");
			check(((systemMinimizeState & (MF_DISABLED | MF_GRAYED)) == 0) == chooseClose, "System menu lost the native minimize enable state");
			if(systemMenuSelected) check(command == SC_CLOSE && IsWindow(hwnd), "System menu close bypassed the owner's system-command policy");
			MENUITEMINFO restored = { sizeof(MENUITEMINFO), MIIM_FTYPE | MIIM_DATA | MIIM_BITMAP | MIIM_ID };
			GetMenuItemInfo(GetSystemMenu(hwnd, FALSE), SC_CLOSE, FALSE, &restored);
			check(restored.fType == originalClose.fType && restored.dwItemData == originalClose.dwItemData && restored.hbmpItem == originalClose.hbmpItem && restored.wID == SC_CLOSE, "System menu retained borrowed painting data after closing");
		}
		window->setMinimizeBox(true);

		auto edit = window->addChild(dwt::TextBox::Seed());
		edit->resize(dwt::Rectangle(20, content.top() + 20, 200, 30));
		SetFocus(edit->handle());
		auto menuSeed = dwt::Menu::Seed();
		menuSeed.popup = false;
		auto menu = window->addChild(menuSeed);
		bool invoked = false;
		auto file = menu->appendPopup(L"&File", new dwt::Icon(LoadIcon(nullptr, IDI_APPLICATION), false));
		file->appendItem(L"Run", [&invoked] { invoked = true; });
		bool viewInvoked = false;
		menu->appendPopup(L"&View")->appendItem(L"Entry", [&viewInvoked] { viewInvoked = true; });
		menu->setMenu();
		pump();
		check(GetMenu(hwnd) == menu->handle(), "Native menu handle/command routing was lost");
		check(window->getContentRect().top() > content.top(), "Menu overlaps window content");
		checkSeparator(caption, palette.border);
		unsigned menuDraws = 0;
		window->addCallback(dwt::Message(WM_DRAWITEM), [&menuDraws, &menu](const MSG& msg, LRESULT&) {
			const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(msg.lParam);
			if(draw && draw->CtlType == ODT_MENU && draw->hwndItem == reinterpret_cast<HWND>(menu->handle())) ++menuDraws;
			return false;
		});
		background(button(hwnd, L"&File"));
		check(menuDraws > 0, "Client menu bypassed the menu's owner-draw renderer");
		MENUITEMINFO itemInfo = { sizeof(MENUITEMINFO), MIIM_ID | MIIM_DATA };
		check(GetMenuItemInfo(menu->handle(), 0, TRUE, &itemInfo), "Cannot query menu heading");
		MEASUREITEMSTRUCT measure = {};
		measure.CtlType = ODT_MENU;
		measure.itemID = itemInfo.wID;
		measure.itemData = itemInfo.dwItemData;
		check(window->sendMessage(WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)), "Cannot measure owner-drawn heading");
		RECT heading;
		GetClientRect(button(hwnd, L"&File"), &heading);
		check(heading.right >= static_cast<long>(measure.itemWidth) && heading.bottom >= static_cast<long>(measure.itemHeight), "Menu heading clipped its measured text/icon size");
		window->sendMessage(WM_SYSCOMMAND, SC_KEYMENU, 0);
		check(window->isMenuBarFocused() && GetFocus() == button(hwnd, L"&File"), "Alt/F10 cannot focus the menu bar");
		SendMessage(GetFocus(), WM_KEYDOWN, VK_RIGHT, 0);
		check(GetFocus() == button(hwnd, L"&View"), "Menu arrow navigation failed");
		SendMessage(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
		check(GetFocus() == edit->handle(), "Menu did not restore keyboard focus");
		popupOwner = hwnd;
		popupTicks = 0;
		selectPopup = true;
		const auto timer = SetTimer(nullptr, 0, 100, closePopup);
		check(timer != 0, "Could not supervise popup test");
		window->sendMessage(WM_SYSCOMMAND, SC_KEYMENU, L'f');
		KillTimer(nullptr, timer);
		pump();
		check(invoked, "Client menu lost WM_MENUCOMMAND dispatch");
		check(GetFocus() == edit->handle(), "Popup did not restore focus");
		popupTicks = 0;
		switchPopup = true;
		const auto switchTimer = SetTimer(nullptr, 0, 100, closePopup);
		window->sendMessage(WM_SYSCOMMAND, SC_KEYMENU, L'f');
		KillTimer(nullptr, switchTimer);
		pump();
		check(viewInvoked, "Popup arrow navigation did not switch top-level menus");
		check(previousMenuBackground == palette.surface && currentMenuBackground == palette.accent, "Switching menus retained a highlight on the previous heading");
		switchPopup = false;
		popupTicks = 0;
		previousMenuBackground = currentMenuBackground = returnedMenuBackground = departedMenuBackground = CLR_INVALID;
		const auto hoverTimer = SetTimer(nullptr, 0, 100, hoverPopup);
		check(hoverTimer != 0, "Could not supervise menu hover test");
		window->sendMessage(WM_SYSCOMMAND, SC_KEYMENU, L'f');
		KillTimer(nullptr, hoverTimer);
		check(previousMenuBackground == palette.surface && currentMenuBackground == palette.accent, "Mouse menu switching left the original heading highlighted");
		check(returnedMenuBackground == palette.accent && departedMenuBackground == palette.surface, "Moving back between menus retained a stale highlight");
		SetFocus(button(hwnd, L"&File"));
		popupTicks = 0;
		selectPopup = false;
		const auto mouseTimer = SetTimer(nullptr, 0, 100, closePopup);
		SendMessage(button(hwnd, L"&File"), BM_CLICK, 0, 0);
		KillTimer(nullptr, mouseTimer);
		check(GetFocus() == edit->handle(), "Mouse menu activation lost the previous content focus");
		window->sendMessage(WM_SYSCOMMAND, SC_KEYMENU, 0);
		const auto otherWindow = CreateWindowEx(WS_EX_TOOLWINDOW, L"STATIC", L"Activation test", WS_POPUP, -32000, -32000, 100, 100, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
		check(otherWindow != nullptr, "Cannot create alternate activation target");
		ShowWindow(otherWindow, SW_SHOWNOACTIVATE);
		SetActiveWindow(otherWindow);
		check(GetActiveWindow() == otherWindow, "Test window did not deactivate before closing its menu");
		SendMessage(button(hwnd, L"&File"), WM_KEYDOWN, VK_ESCAPE, 0);
		const bool keptActivation = GetActiveWindow() == otherWindow;
		DestroyWindow(otherWindow);
		check(keptActivation, "Closing an inactive menu stole activation");
		SetFocus(edit->handle());
		window->setMenuBar(nullptr);
		pump();
		check(window->getContentRect().top() == content.top(), "Hidden menu still occupies client space");
		checkSeparator(caption, palette.border);
		menu->setMenu();
		pump();
		menu->appendPopup(L"&Long menu heading for wrapping");
		menu->setMenu();
		GetWindowRect(hwnd, &outer);
		const auto menuTop = window->getContentRect().top();
		SetWindowPos(hwnd, nullptr, 0, 0, 220, outer.bottom - outer.top, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
		check(window->getContentRect().top() > menuTop, "Narrow test window did not wrap menu headings");
		checkSeparator(caption, palette.border);
		SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
		menu->remove(2);
		menu->setMenu();
		auto rebar = window->addChild(dwt::Rebar::Seed());
		auto toolbar = window->addChild(dwt::ToolBar::Seed());
		check(toolbar->getPreferredSize() == dwt::Point(), "Empty toolbar returned an undefined preferred size");
		toolbar->addButton("test", -1, L"Test", true, 0, [] { });
		toolbar->setLayout({ "test" });
		rebar->add(toolbar);
		dwt::ComboBox::Seed themeSeed;
		themeSeed.style = (themeSeed.style & ~CBS_DROPDOWN) | CBS_DROPDOWNLIST;
		auto theme = window->addChild(themeSeed);
		theme->addValue(L"Theme one");
		theme->addValue(L"Theme two");
		theme->setSelected(0);
		const auto comboStyle = GetWindowLongPtr(theme->handle(), GWL_STYLE);
		rebar->add(theme, RBBS_NOGRIPPER, L"Theme");
		check(GetWindowLongPtr(theme->handle(), GWL_STYLE) == comboStyle, "Rebar changed combo-box behavior flags");
		unsigned selections = 0;
		theme->onSelectionChanged([&] { ++selections; });
		theme->setSelected(1);
		SendMessage(rebar->handle(), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(theme->handle()), CBN_SELENDOK), reinterpret_cast<LPARAM>(theme->handle()));
		check(selections == 1 && theme->getSelected() == 1, "Rebar failed to route a committed theme selection");
		SendMessage(rebar->handle(), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(theme->handle()), CBN_SELENDCANCEL), reinterpret_cast<LPARAM>(theme->handle()));
		check(selections == 1, "Cancelled combo-box selection triggered a theme change");
		rebar->refresh();
		RECT band;
		GetWindowRect(rebar->handle(), &band);
		MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&band), 2);
		check(band.top >= window->getContentRect().top(), "Rebar overlaps caption or menu");
		check(band.bottom > band.top, "Rebar lost its band height");
		auto status = window->addChild(dwt::StatusBar::Seed(2, 0));
		status->setText(0, L"Ready");
		status->refresh();
		GetWindowRect(status->handle(), &band);
		MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&band), 2);
		const auto statusContent = window->getContentRect();
		check(band.left == statusContent.left() && band.right == statusContent.right() && band.bottom == statusContent.bottom(), "Status bar covers the resize gutters");

		for(auto dpi: { 144, 192, 96 }) {
			GetWindowRect(hwnd, &outer);
			window->sendMessage(WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&outer));
			check(window->getContentRect().top() > content.top(), "DPI update lost menu/caption layout");
			checkSeparator(caption, palette.border);
		}
		for(int i = 0; i < 5; ++i) {
			app.getAppearance().configure(dwt::Appearance::Mode::Light, palette);
			check(!window->usesClientFrame() && (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION, "Light mode did not restore the native frame");
			check(!IsWindowVisible(caption) && window->getContentRect().top() == 0, "Light mode retained custom chrome space");
			palette.surface = RGB(20 + i, 38, 52);
			app.getAppearance().configure(dwt::Appearance::Mode::Dark, palette);
			check(IsWindowVisible(caption) && background(caption) == palette.surface, "Live palette update did not reach the caption");
			checkSeparator(caption, palette.border);
			window->sendMessage(WM_THEMECHANGED);
			window->sendMessage(WM_DWMCOMPOSITIONCHANGED);
			check(!(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CAPTION), "Theme/composition change restored native buttons");
		}
		const auto lightPalette = dwt::Appearance::Palette {
			RGB(245, 245, 245), RGB(255, 255, 255), RGB(25, 25, 25),
			RGB(110, 110, 110), RGB(175, 175, 175), RGB(0, 90, 180), RGB(255, 255, 255)
		};
		const auto customBounds = window->getContentRect();
		app.getAppearance().configure(dwt::Appearance::Mode::Light, lightPalette,
			dwt::Appearance::Rendering::CustomPalette);
		check(!app.getAppearance().isDark() && window->usesClientFrame(), "Custom light mode lost client framing");
		check(background(caption) == lightPalette.surface, "Custom light caption ignored the palette");
		check(buttonPixel(close, 0, 2, 2) == lightPalette.surface, "Custom light caption button ignored the palette");
		check(buttonPixel(close, CDIS_HOT, 2, 2) == RGB(232, 17, 35), "Custom light mode changed close-button hover handling");
		checkSeparator(caption, lightPalette.border);
		check(window->getContentRect().top() == customBounds.top(), "Palette-only switch changed the caption layout");
		for(int i = 0; i < 3; ++i) {
			edit->setFocus();
			if(i == 1) window->sendMessage(WM_SYSCOMMAND, SC_KEYMENU, 0);
			if(i == 2) SetFocus(close);
			window->setFrameRendering(dwt::Frame::FrameRendering::Native);
			pump();
			check(!window->usesClientFrame() && !IsWindowVisible(caption), "Native frame retained custom caption controls");
			check(window->getContentRect().top() == 0, "Native frame retained a client caption offset");
			check(GetMenu(hwnd) == menu->handle() && IsWindowEnabled(hwnd), "Frame switch lost menu or enabled state");
			check(GetFocus() == edit->handle(), "Native frame switch stole content focus");
			window->sendMessage(WM_THEMECHANGED);
			window->sendMessage(WM_DWMCOMPOSITIONCHANGED);
			check(!window->usesClientFrame(), "System notification discarded native frame choice");
			window->setFrameRendering(dwt::Frame::FrameRendering::Automatic);
			pump();
			check(window->usesClientFrame() && background(caption) == lightPalette.surface,
				"Returning to automatic framing lost caption colors");
			check(window->getContentRect().top() == customBounds.top(), "Frame switch changed caption/menu layout");
			check(GetFocus() == edit->handle(), "Client frame switch stole content focus");
		}
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, palette);
		rebar->refresh();
		status->refresh();
		edit->resize(dwt::Rectangle(window->getContentRect().left() + 12, window->getContentRect().top() + rebar->getWindowSize().y + 12, 200, 30));
		snapshot(hwnd);
		window->setAppearancePolicy(dwt::AppearancePolicy::Native);
		check(!window->usesClientFrame(), "Native appearance override retained a custom frame");
		window->setMenuBar(nullptr);
		menu.reset();
		DestroyWindow(hwnd);

		// Create maximized but hidden: exercise USER32's actual outer/client sizing
		// without displaying a maximized test window over the user's desktop.
		auto maximized = new dwt::Window();
		auto maximizedSeed = dwt::Window::Seed(L"Maximized bounds test");
		maximizedSeed.location = dwt::Rectangle(50, 50, 650, 450);
		maximizedSeed.style = (maximizedSeed.style | WS_MAXIMIZE) & ~WS_VISIBLE;
		maximized->create(maximizedSeed);
		check(IsZoomed(maximized->handle()) && !IsWindowVisible(maximized->handle()), "Hidden maximized test did not retain its state");
		GetMonitorInfo(MonitorFromWindow(maximized->handle(), MONITOR_DEFAULTTONEAREST), &monitor);
		GetWindowRect(maximized->handle(), &outer);
		GetClientRect(maximized->handle(), &client);
		MapWindowPoints(maximized->handle(), nullptr, reinterpret_cast<POINT*>(&client), 2);
		std::printf("Maximized outer: %ld,%ld,%ld,%ld; client: %ld,%ld,%ld,%ld; work: %ld,%ld,%ld,%ld\n", outer.left, outer.top, outer.right, outer.bottom, client.left, client.top, client.right, client.bottom, monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom);
		check(EqualRect(&outer, &monitor.rcWork), "Maximized outer window covers the taskbar outside its painted client");
		check(EqualRect(&client, &outer), "Maximized window retained an unpainted non-client strip");
		MINMAXINFO limits = {};
		maximized->sendMessage(WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
		check(limits.ptMaxPosition.x == monitor.rcWork.left - monitor.rcMonitor.left && limits.ptMaxPosition.y == monitor.rcWork.top - monitor.rcMonitor.top, "Maximize position does not respect the monitor work-area offset");
		check(limits.ptMaxSize.x == monitor.rcWork.right - monitor.rcWork.left && limits.ptMaxSize.y == monitor.rcWork.bottom - monitor.rcWork.top, "Maximize size includes the taskbar area");
		for(int i = 0; i < 2; ++i) {
			app.getAppearance().configure(dwt::Appearance::Mode::Light, palette);
			app.getAppearance().configure(dwt::Appearance::Mode::Dark, palette);
			GetWindowRect(maximized->handle(), &outer);
			GetClientRect(maximized->handle(), &client);
			MapWindowPoints(maximized->handle(), nullptr, reinterpret_cast<POINT*>(&client), 2);
			check(EqualRect(&outer, &monitor.rcWork) && EqualRect(&client, &outer), "Maximized theme transition restored the unpainted taskbar strip");
			check(!IsWindowVisible(maximized->handle()), "Reframing exposed the hidden maximized window");
		}
		auto maximizedStatus = maximized->addChild(dwt::StatusBar::Seed(2, 0));
		maximizedStatus->refresh();
		RECT statusBounds;
		GetWindowRect(maximizedStatus->handle(), &statusBounds);
		check(statusBounds.left == monitor.rcWork.left && statusBounds.right == monitor.rcWork.right && statusBounds.bottom == monitor.rcWork.bottom, "Maximized status bar does not reach the bottom of the work area");
		DestroyWindow(maximized->handle());

		// RTL uses physical resize edges and places close buttons opposite the icon.
		auto rtlWindow = new dwt::Window();
		seed.exStyle = WS_EX_LAYOUTRTL;
		rtlWindow->create(seed);
		ShowWindow(rtlWindow->handle(), SW_SHOWNOACTIVATE);
		GetWindowRect(rtlWindow->handle(), &outer);
		check(rtlWindow->sendMessage(WM_NCHITTEST, 0, MAKELPARAM(outer.left + 1, outer.top + 1)) == HTTOPLEFT, "RTL reversed the resize corners");
		RECT closeRect;
		GetWindowRect(button(rtlWindow->handle(), L"Close"), &closeRect);
		check(closeRect.left < outer.left + 50, "RTL caption buttons were mirrored twice");
		check(rtlWindow->sendMessage(WM_NCHITTEST, 0, MAKELPARAM(outer.right - 18, outer.top + 18)) == HTSYSMENU, "RTL system icon hit area is misplaced");
		DestroyWindow(rtlWindow->handle());
		std::puts("Client caption ownership, pixels, buttons, menus, focus, DPI and live theme transitions passed.");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
