/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdafx.h"

#include <win32/appearance/AppAppearance.h>
#include <dcpp/util/Util.h>
#include <win32/appearance/ColorUtil.h>
#include <win32/appearance/IconManager.h>
#include <win32/forward.h>
#include <dcpp/settings/SettingsManager.h>
#include <dwt/Appearance.h>
#include <dwt/Application.h>
#include <dwt/resources/Brush.h>
#include <win32/controls/RichTextBox.h>
#include <win32/resource.h>

namespace AppAppearance {

dwt::BrushPtr bgBrush;
COLORREF textColor = 0, bgColor = 0;

namespace {
dwt::Appearance::Palette themePalette() {
	return {
		static_cast<COLORREF>(SETTING(THEME_BACKGROUND_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_SURFACE_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_TEXT_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_DISABLED_TEXT_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_BORDER_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_ACCENT_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_HIGHLIGHT_TEXT_COLOR))
	};
}

dwt::Appearance::Mode themeMode() {
	const auto setting = SETTING(THEME_MODE);
	return setting == SettingsManager::THEME_LIGHT ? dwt::Appearance::Mode::Light :
		setting == SettingsManager::THEME_DARK ? dwt::Appearance::Mode::Dark :
		dwt::Appearance::Mode::System;
}

BOOL CALLBACK updateWidgetColors(HWND hwnd, LPARAM) {
	if(auto widget = dwt::hwnd_cast<dwt::Control*>(hwnd)) {
		widget->sendCommand(ID_UPDATECOLOR);
	}
	return TRUE;
}

BOOL CALLBACK updateWindowColors(HWND hwnd, LPARAM) {
	updateWidgetColors(hwnd, 0);
	::EnumChildWindows(hwnd, updateWidgetColors, 0);
	return TRUE;
}

void updateApplicationColors() {
	const auto& appearance = dwt::Application::instance().getAppearance();
	IconManager::setDarkMode(appearance.isDark());
	textColor = appearance.isHighContrast() ?
		::GetSysColor(COLOR_WINDOWTEXT) :
		static_cast<COLORREF>(SETTING(TEXT_COLOR));
	bgColor = appearance.isHighContrast() ?
		::GetSysColor(COLOR_WINDOW) :
		static_cast<COLORREF>(SETTING(BACKGROUND_COLOR));
	bgBrush = new dwt::Brush(bgColor);
	::EnumThreadWindows(::GetCurrentThreadId(), updateWindowColors, 0);
}

}
void init() {
	auto& appearance = dwt::Application::instance().getAppearance();
	IconManager::initialize();
	appearance.configure(themeMode(), themePalette());

	SettingsManager::getInstance()->setDefault(SettingsManager::BACKGROUND_COLOR, dwt::Color::predefined(COLOR_WINDOW));
	SettingsManager::getInstance()->setDefault(SettingsManager::TEXT_COLOR, dwt::Color::predefined(COLOR_WINDOWTEXT));

	// Theme editing writes both the interface palette and content styles. Keep
	// their persisted settings intact when Windows changes its appearance mode.
	updateApplicationColors();
	appearance.onChanged(updateApplicationColors);

}
void initStyleDefaults() {
	{
		// default link color: shift the hue, set lum & sat to middle values
		auto hls = ColorUtil::RGB2HLS(textColor);
		SettingsManager::getInstance()->setDefault(SettingsManager::LINK_COLOR, ColorUtil::HLS2RGB(HLS((HLS_H(hls) + 60) % 239, 120, 120)));
	}

	{
		// default log color: more grey than the text color
		auto hls = ColorUtil::RGB2HLS(textColor);
		SettingsManager::getInstance()->setDefault(SettingsManager::LOG_COLOR, ColorUtil::HLS2RGB(HLS(HLS_H(hls), 120, HLS_S(hls) / 2)));
	}

	// Semantic chat colors inherit the active Windows/application palette unless customized.
	SettingsManager::getInstance()->setDefault(SettingsManager::LINK_BG_COLOR, bgColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::LOG_BG_COLOR, bgColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::SYSTEM_LOG_VERBOSE_COLOR, SETTING(LOG_COLOR));
	SettingsManager::getInstance()->setDefault(SettingsManager::SYSTEM_LOG_INFO_COLOR, RGB(0, 120, 215));
	SettingsManager::getInstance()->setDefault(SettingsManager::SYSTEM_LOG_WARNING_COLOR, RGB(210, 125, 0));
	SettingsManager::getInstance()->setDefault(SettingsManager::SYSTEM_LOG_ERROR_COLOR, RGB(210, 45, 45));
	SettingsManager::getInstance()->setDefault(SettingsManager::SYSTEM_LOG_AREA_COLOR, SETTING(LINK_COLOR));
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_TEXT_COLOR, textColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_TEXT_BG_COLOR, bgColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_TIMESTAMP_COLOR, SETTING(LOG_COLOR));
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_TIMESTAMP_BG_COLOR, bgColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_NICK_COLOR, SETTING(LINK_COLOR));
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_NICK_BG_COLOR, bgColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_OWN_TEXT_COLOR, textColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_OWN_TEXT_BG_COLOR, bgColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_OWN_TIMESTAMP_COLOR, SETTING(LOG_COLOR));
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_OWN_TIMESTAMP_BG_COLOR, bgColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_OWN_NICK_COLOR, SETTING(LINK_COLOR));
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_OWN_NICK_BG_COLOR, bgColor);
	SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_SYSTEM_BG_COLOR, bgColor);
	{
		auto hls = ColorUtil::RGB2HLS(textColor);
		SettingsManager::getInstance()->setDefault(SettingsManager::CHAT_SYSTEM_COLOR,
			ColorUtil::HLS2RGB(HLS((HLS_H(hls) + 30) % 239, HLS_L(hls), std::max<int>(80, HLS_S(hls)))));
	}

}
void refreshTheme() {
	dwt::Application::instance().getAppearance().configure(
		themeMode(), themePalette());
}

void setColor(dwt::Control* widget) {
	widget->setColor(textColor, bgColor);

	widget->onCommand([widget] {
		widget->setColor(textColor, bgColor);
		if(auto chat = dynamic_cast<RichTextBox*>(widget)) chat->refreshMessageStyles();
		widget->redraw(true);
	}, ID_UPDATECOLOR);
}

} // namespace AppAppearance
