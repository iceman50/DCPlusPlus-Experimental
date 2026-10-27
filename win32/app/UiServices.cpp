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

#include <win32/app/UiServices.h>
#include <win32/appearance/AppAppearance.h>
#include <win32/appearance/ControlSeeds.h>
#include <win32/appearance/ImageLists.h>
#include <win32/appearance/UiFonts.h>
#include <win32/services/HelpService.h>
#include <win32/shell/ShellIntegration.h>
#include <win32/forward.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/version.h>
#include <win32/app/MainWindow.h>
#include <dwt/widgets/MessageBox.h>

namespace UiServices {

MainWindow* mainWindow = nullptr;
tstring tth;
void init() {
	AppAppearance::init();
	UiFonts::initDefault();
	initFont();
	UiFonts::updateUploadFont();
	UiFonts::updateDownloadFont();
	UiFonts::initUserMatching();
	AppAppearance::initStyleDefaults();
	ImageLists::init();
	ShellIntegration::registerHubHandlers();
	ShellIntegration::registerMagnetHandler();
	ShellIntegration::registerDcextHandler();
	HelpService::init();
}
void uninit() { HelpService::uninit(); }
void initFont() {
	UiFonts::updateFont(UiFonts::font, SettingsManager::MAIN_FONT);

	ControlSeeds::init();
}

void setStaticWindowState(const string& id, bool open) {
	mainWindow->setStaticWindowState(id, open);
}

bool checkNick() {
	if(SETTING(NICK).empty()) {
		dwt::MessageBox(mainWindow).show(T_("Please enter a nickname in the settings dialog!"),
			_T(APPNAME) _T(" ") _T(VERSIONSTRING), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
		mainWindow->handleSettings();
		return false;
	}

	return true;
}

} // namespace UiServices
