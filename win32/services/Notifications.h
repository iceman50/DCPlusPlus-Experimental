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

#ifndef DCPLUSPLUS_WIN32_NOTIFICATIONS_H
#define DCPLUSPLUS_WIN32_NOTIFICATIONS_H

#include <dwt/tstring.h>
#include <functional>

namespace Notifications {
enum NotificationType {
	NOTIFICATION_FINISHED_DL,
	NOTIFICATION_FINISHED_FL,
	NOTIFICATION_MAIN_CHAT,
	NOTIFICATION_PM,
	NOTIFICATION_PM_WINDOW,

	NOTIFICATION_LAST
};

struct Notification {
	int sound;
	int balloon;
	const char* title;
	unsigned icon;
};

extern Notification notifications[NOTIFICATION_LAST];

void notify(NotificationType notification, const dwt::tstring& balloonText, const std::function<void ()>& balloonCallback = nullptr);
void playSound(const dwt::tstring& sound);
}

#endif
