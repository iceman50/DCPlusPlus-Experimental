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

#include <win32/util/UserActions.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/user/HintedUser.h>
#include <dcpp/util/Util.h>
#include <win32/util/IconManager.h>
#include <win32/util/UserPresentation.h>
#include <win32/forward.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <win32/frames/private_chat/PrivateFrame.h>
#include <win32/resource.h>
#include <dwt/widgets/Menu.h>

namespace UserActions {

namespace {

typedef std::function<void(const HintedUser&, const string&)> UserFunction;

void eachUser(const HintedUserList& list, const StringList& dirs, const UserFunction& f) {
	size_t j = 0;
	for(auto& i: list) {
		try {
			f(i, (j < dirs.size()) ? dirs[j] : string());
		} catch (const Exception& e) {
			LogManager::getInstance()->message(e.getError(), LogMessage::SEV_ERROR, _("Integration"));
		}
		++j;
	}
}

void addUsers(Menu* menu, const tstring& text, const HintedUserList& users, const UserFunction& f,
	const dwt::IconPtr& icon = dwt::IconPtr(), const StringList& dirs = StringList())
{
	if(users.empty())
		return;

	if(users.size() > 1) {
		menu = menu->appendPopup(text, icon);
		menu->appendItem(T_("All"), [=] { eachUser(users, dirs, f); });

		menu->appendSeparator();

		for(size_t i = 0, iend = users.size(); i < iend; ++i) {
			menu->appendItem(UserPresentation::getNicks(users[i]), [=] { eachUser(HintedUserList(1, users[i]),
				StringList(1, (i < dirs.size()) ? dirs[i] : Util::emptyString), f); });
		}
	} else {
		menu->appendItem(text, [=] { eachUser(users, dirs, f); }, icon);
	}
}

template<typename F>
HintedUserList filter(const HintedUserList& l, F f) {
	HintedUserList ret;
	for(auto& i: l) {
		if(f(i.user)) {
			ret.push_back(i);
		}
	}
	return ret;
}

bool isFav(const UserPtr& u) {
	return !FavoriteManager::getInstance()->isFavoriteUser(u);
}

} // unnamed namespace

void addUserItems(Menu* menu, const HintedUserList& users, TabViewPtr parent, const StringList& dirs) {
	QueueManager* qm = QueueManager::getInstance();

	addUsers(menu, T_("&Get file list"), users, [=](const HintedUser &u, const string& s) {
		qm->addList(u, QueueItem::FLAG_CLIENT_VIEW, s); }, dwt::IconPtr(), dirs);

	addUsers(menu, T_("&Browse file list"), users, [=](const HintedUser &u, const string& s) {
		qm->addList(u, QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_PARTIAL_LIST, s); }, dwt::IconPtr(), dirs);

	addUsers(menu, T_("&Match queue"), users, [=](const HintedUser &u, const string& s) {
		qm->addList(u, QueueItem::FLAG_MATCH_QUEUE, Util::emptyString); });

	addUsers(menu, T_("&Send private message"), users, [parent](const HintedUser& u, const string&) {
		PrivateFrame::openWindow(parent, u); });

	addUsers(menu, T_("Add To &Favorites"), filter(users, &isFav), [=](const HintedUser &u, const string& s) {
		FavoriteManager::getInstance()->addFavoriteUser(u); }, IconManager::menuIcon(IDI_FAVORITE_USER_ON));

	addUsers(menu, T_("Grant &extra slot"), users, [=](const HintedUser &u, const string& s) {
		UploadManager::getInstance()->reserveSlot(u); });

	addUsers(menu, T_("Remove user from queue"), users, [=](const HintedUser &u, const string& s) {
		qm->removeSource(u, QueueItem::Source::FLAG_REMOVED); });
}

} // namespace UserActions
