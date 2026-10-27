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

#include <win32/services/Notifications.h>
#include <win32/app/UiServices.h>
#include <win32/appearance/IconManager.h>
#include <win32/forward.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/system/File.h>
#include <dcpp/text/Text.h>
#include <win32/app/MainWindow.h>
#include <win32/resource.h>
#include <mmsystem.h>

namespace Notifications {

Notification notifications[NOTIFICATION_LAST] = {
	{ SettingsManager::SOUND_FINISHED_DL, SettingsManager::BALLOON_FINISHED_DL, N_("Download finished"), IDI_DOWNLOAD },
	{ SettingsManager::SOUND_FINISHED_FL, SettingsManager::BALLOON_FINISHED_FL, N_("File list downloaded"), IDI_DIRECTORY },
	{ SettingsManager::SOUND_MAIN_CHAT, SettingsManager::BALLOON_MAIN_CHAT, N_("Main chat message received"), IDI_BALLOON },
	{ SettingsManager::SOUND_PM, SettingsManager::BALLOON_PM, N_("Private message received"), IDI_PRIVATE },
	{ SettingsManager::SOUND_PM_WINDOW, SettingsManager::BALLOON_PM_WINDOW, N_("Private message window opened"), IDI_PRIVATE }
};

void notify(NotificationType notification, const tstring& balloonText, const std::function<void ()>& balloonCallback) {
	const auto& n = notifications[notification];

	const string& s = SettingsManager::getInstance()->get((SettingsManager::StrSetting)n.sound);
	if(!s.empty()) {
		playSound(Text::toT(s));
	}

	int b = SettingsManager::getInstance()->get((SettingsManager::IntSetting)n.balloon);
	if(b == SettingsManager::BALLOON_ALWAYS || (b == SettingsManager::BALLOON_BACKGROUND && !UiServices::mainWindow->onForeground())) {
		UiServices::mainWindow->notify(T_(n.title), balloonText, balloonCallback, IconManager::load(n.icon, 16));
	}
}

void playSound(const tstring& sound) {
	if(sound == _T("beep")) {
		::MessageBeep(MB_OK);
	} else {
		::PlaySound(File::toNativePath(Text::fromT(sound)).c_str(), 0, SND_FILENAME | SND_ASYNC);
	}
}

} // namespace Notifications
