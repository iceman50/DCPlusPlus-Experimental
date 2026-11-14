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

#include <win32/util/UserPresentation.h>
#include <dcpp/user/HintedUser.h>
#include <win32/forward.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/util/Util.h>

namespace UserPresentation {

tstring getNicks(const CID& cid) {
	return Text::toT(Util::toString(ClientManager::getInstance()->getNicks(cid)));
}

pair<tstring, bool> getHubNames(const CID& cid) {
	auto hubs = ClientManager::getInstance()->getHubNames(cid);
	if(hubs.empty()) {
		return make_pair(T_("Offline"), false);
	} else {
		return make_pair(Text::toT(Util::toString(hubs)), true);
	}
}

tstring getNick(const HintedUser& user) {
	return Text::toT(ClientManager::getInstance()->getNick(user));
}

tstring getHubName(const HintedUser& user) {
	return Text::toT(ClientManager::getInstance()->getHubName(user));
}

void getHubStatus(const string& url, tstring& statusText, int& statusIcon)
{
	enum {
		HUB_ON_ICON,
		HUB_OFF_ICON
	};

	statusText = Util::emptyStringT;
	statusIcon = -1;

	// This checks if the hub is opened at all, but does not say whether it's connected or not.
	if(ClientManager::getInstance()->isConnected(url))
	{
		// This will check the actual connectivity to the hub.
		if(ClientManager::getInstance()->isHubConnected(url))
		{
			statusText = T_("Connected");
			statusIcon = HUB_ON_ICON;
		}
		else
		{
			statusText = T_("Disconnected");
			statusIcon = HUB_OFF_ICON;
		}
	}
}

} // namespace UserPresentation
