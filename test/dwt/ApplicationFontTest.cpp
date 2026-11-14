// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/ToolTip.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}
dwt::FontPtr makeFont(int height) {
	LOGFONT value {};
	value.lfHeight = height;
	value.lfWeight = FW_BOLD;
	value.lfItalic = TRUE;
	value.lfUnderline = TRUE;
	_tcscpy(value.lfFaceName, _T("Consolas"));
	return new dwt::Font(value);
}
void checkFont(HFONT font, int height) {
	LOGFONT value {};
	check(GetObject(font, sizeof(value), &value) == sizeof(value), "Missing control font");
	if(value.lfHeight != height) std::fprintf(stderr, "Font height: expected %d, got %ld\n", height, value.lfHeight);
	check(value.lfHeight == height && value.lfWeight == FW_BOLD && value.lfItalic &&
		value.lfUnderline && _tcscmp(value.lfFaceName, _T("Consolas")) == 0,
		"Global font family, size or effects did not reach a control");
}
UINT menuHeight(dwt::Window* window, dwt::Menu* menu) {
	MENUITEMINFO item { sizeof(item), MIIM_DATA | MIIM_ID };
	check(GetMenuItemInfo(menu->handle(), 0, TRUE, &item), "Missing menu item");
	MEASUREITEMSTRUCT measure {};
	measure.CtlType = ODT_MENU;
	measure.itemID = item.wID;
	measure.itemData = item.dwItemData;
	check(window->sendMessage(WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)), "Menu measurement failed");
	return measure.itemHeight;
}
void dispatch(dwt::Application& app) {
	for(unsigned i = 0; i < 1000 && app.dispatch(); ++i) { }
}
}

int dwtMain(dwt::Application& app) {
	try {
		auto& appearance = app.getAppearance();
		appearance.configure(dwt::Appearance::Mode::Light, dwt::Appearance::defaultPalette(),
			dwt::Appearance::Rendering::CustomPalette);
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed(_T("Global typography"));
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		const auto dpi = window->getDpi();
		auto small = makeFont(-12);
		appearance.setFont(small, dpi);
		auto label = window->addChild(dwt::Label::Seed(_T("Inherited label")));
		auto button = window->addChild(dwt::Button::Seed(_T("Inherited button")));
		auto tip = window->addChild(dwt::ToolTip::Seed());
		tip->setText(label, _T("Inherited tooltip"));
		auto explicitLabel = window->addChild(dwt::Label::Seed(_T("Explicit content style")));
		auto explicitFont = makeFont(-17);
		explicitLabel->setFont(explicitFont);
		auto other = new dwt::Window();
		other->create(seed);
		auto otherLabel = other->addChild(dwt::Label::Seed(_T("Another top-level window")));
		auto menuSeed = dwt::Menu::Seed();
		menuSeed.popup = false;
		auto menu = window->addChild(menuSeed);
		auto popup = menu->appendPopup(_T("&File"));
		popup->appendItem(_T("Open"), [] {});
		menu->setMenu();
		dispatch(app);
		checkFont(label->getFont()->handle(), -12);
		checkFont(tip->getFont()->handle(), -12);
		const auto oldCaption = window->getContentRect().top();
		const auto oldMenuHeight = menuHeight(window, popup);
		auto large = makeFont(-40);
		appearance.setFont(large, dpi);
		dispatch(app);
		checkFont(label->getFont()->handle(), -40);
		checkFont(button->getFont()->handle(), -40);
		checkFont(tip->getFont()->handle(), -40);
		checkFont(otherLabel->getFont()->handle(), -40);
		check(explicitLabel->getFont() == explicitFont, "Global update replaced an explicit font");
		check(window->getContentRect().top() > oldCaption, "Caption/menu layout did not grow for large text");
		check(window->getContentRect().top() >= 2 * button->getTextSize(_T("Mg")).y,
			"Title bar text did not receive enough height independently of the menu row");
		check(menuHeight(window, popup) > oldMenuHeight, "Popup retained its old font metrics");
		auto later = other->addChild(dwt::Button::Seed(_T("Created after the change")));
		checkFont(later->getFont()->handle(), -40);
		const unsigned scaledDpi = dpi * 2;
		// Synthetic DPI messages must target the control itself: USER32 does not
		// propagate them to children as it does for a real monitor transition.
		label->sendMessage(WM_DPICHANGED, MAKELONG(scaledDpi, scaledDpi));
		checkFont(label->getFont()->handle(), -80);
		label->sendMessage(WM_DPICHANGED, MAKELONG(dpi, dpi));
		checkFont(label->getFont()->handle(), -40);
		appearance.setFont(small, dpi);
		dispatch(app);
		checkFont(label->getFont()->handle(), -12);
		checkFont(appearance.getFontForDpi(scaledDpi)->handle(), -24);
		// Windows light rendering must not require giving up the global font.
		appearance.configure(dwt::Appearance::Mode::Light, dwt::Appearance::defaultPalette());
		dispatch(app);
		check(!appearance.isManual() && window->usesClientFrame(), "Native light controls lost the styled caption");
		checkFont(label->getFont()->handle(), -12);
		check(menuHeight(window, popup) == oldMenuHeight, "Native light menus lost the global font metrics");
		appearance.setFont(large, dpi);
		dispatch(app);
		check(menuHeight(window, popup) > oldMenuHeight && window->getContentRect().top() >=
			2 * button->getTextSize(_T("Mg")).y, "Native light chrome did not update its global font");
		DestroyWindow(other->handle());
		DestroyWindow(tip->handle());
		DestroyWindow(window->handle());
		appearance.setFont(nullptr, dpi);
		std::puts("PASS: global font inheritance, live updates, menus, captions, tooltips, explicit styles and DPI scaling");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
