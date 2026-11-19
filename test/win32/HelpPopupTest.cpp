// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// The context help popup (F1 and the settings dialog's help tooltips) must use the
// same colors as tooltips: the palette in dark and custom appearances, the system
// tooltip colors otherwise.
#include "stdafx.h"
#include <win32/SingleInstance.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/HelpService.h>
#include <win32/util/UiServices.h>
#include <win32/resource.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dwt/Application.h>
#include <dwt/widgets/RichTextBox.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/Window.h>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 191;

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

HWND findPopup() {
	HWND found = nullptr;
	EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
		if(IsWindowVisible(window) && (GetWindowLongPtr(window, GWL_STYLE) & WS_POPUP) &&
			(GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) && dwt::hwnd_cast<dwt::RichTextBox*>(window))
		{
			*reinterpret_cast<HWND*>(data) = window;
			return FALSE;
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&found));
	return found;
}

void checkPopup(dwt::Application& app, dwt::TextBoxPtr box, const char* name, bool manual) {
	while(app.dispatch()) { }
	HelpService::helpId(box, IDH_CSHELP_BEGIN);
	// The popup sizes, colors and shows itself when Rich Edit requests a size.
	while(app.dispatch()) { }
	const auto popup = findPopup();
	check(popup != nullptr, "The help popup did not open");
	auto control = dwt::hwnd_cast<dwt::RichTextBox*>(popup);
	const auto& appearance = app.getAppearance();
	check(appearance.isManual() == manual, "Unexpected appearance mode");
	const auto expectedText = manual ? appearance.getPalette().text : GetSysColor(COLOR_INFOTEXT);
	const auto expectedBack = manual ? appearance.getPalette().surface : GetSysColor(COLOR_INFOBK);
	check(control->getTextColor() == expectedText, "The help popup text color does not match");
	if(control->getBgColor() != expectedBack)
		std::fprintf(stderr, "%s: background %06lX, expected %06lX\n", name,
			static_cast<unsigned long>(control->getBgColor()), static_cast<unsigned long>(expectedBack));
	check(control->getBgColor() == expectedBack, "The help popup background does not match");

	const auto exStyle = GetWindowLongPtr(popup, GWL_EXSTYLE);
	const auto style = GetWindowLongPtr(popup, GWL_STYLE);
	if(manual) {
		check(!(exStyle & WS_EX_CLIENTEDGE) && (style & WS_BORDER), "The palette popup kept the system 3D edge");
		RedrawWindow(popup, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
		auto dc = GetWindowDC(popup);
		const auto corner = GetPixel(dc, 0, 0);
		ReleaseDC(popup, dc);
		check(corner == appearance.getPalette().border, "The help popup border is not drawn in the palette");
	} else {
		check((exStyle & WS_EX_CLIENTEDGE) != 0, "The system popup lost its edge");
	}
	std::printf("%s: help popup text %06lX on %06lX\n", name,
		static_cast<unsigned long>(control->getTextColor()), static_cast<unsigned long>(control->getBgColor()));
	HelpService::killHelpTooltip();
	while(app.dispatch()) { }
	check(findPopup() == nullptr, "The help popup did not close");
}
}

int dwtMain(dwt::Application& app) {
	try {
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		UiServices::initFont();
		ControlSeeds::init();
		auto window = new dwt::Window();
		dwt::Window::Seed seed;
		seed.location = dwt::Rectangle(100, 100, 400, 300);
		window->create(seed);
		auto box = window->addChild(ControlSeeds::Dialog::textBox);
		box->resize(dwt::Rectangle(10, 10, 200, 24));
		box->setFocus();

		auto& appearance = app.getAppearance();
		appearance.configure(dwt::Appearance::Mode::Light, dwt::Appearance::defaultPalette());
		if(appearance.isHighContrast()) {
			std::puts("SKIP: high contrast uses system colors only");
			return 0;
		}
		checkPopup(app, box, "light", false);

		appearance.configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		checkPopup(app, box, "dark", true);

		auto custom = dwt::Appearance::defaultPalette();
		custom.surface = RGB(10, 40, 70);
		custom.text = RGB(250, 220, 120);
		custom.border = RGB(200, 60, 60);
		appearance.configure(dwt::Appearance::Mode::Light, custom, dwt::Appearance::Rendering::CustomPalette);
		checkPopup(app, box, "custom palette", true);

		DestroyWindow(window->handle());
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("PASS: help popups use the palette in dark and custom appearances and system tooltip colors otherwise");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}
