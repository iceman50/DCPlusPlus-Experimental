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

#ifndef DCPLUSPLUS_DCPP_USER_MATCH_MANAGER_H
#define DCPLUSPLUS_DCPP_USER_MATCH_MANAGER_H

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <dcpp/forward.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/base/Singleton.h>
#include <dcpp/user/UserMatch.h>

namespace dcpp {

using std::string;
using std::unordered_set;
using std::vector;

/** This class manages user matching definitions. Online users are matched against these
definitions to find properties to apply (such as chat ignoring, custom colors...). */
class UserMatchManager :
	public Singleton<UserMatchManager>,
	private SettingsManagerListener
{
	typedef vector<UserMatch> UserMatches;

public:
	/** Retrieve the list of user matching definitions. */
	UserMatches getList() const;
	/** Resolve a session-local definition identity, never a display name. */
	Style getStyle(const string& styleId) const;
	/** Resolve current visual overrides for an already matched identity snapshot. */
	Style resolveStyle(const Style& snapshot) const;
	/** Assign a new list of user matching definitions. */
	void setList(UserMatches&& newList, bool updateUsers = true);

	/** Match the given user against current user matching definitions. The user's identity object
	will be modified accordingly. */
	void match(OnlineUser& user) const;

	/** Helper function that tells whether predefined definitions for favorites (pair.first) and
	operators (pair.second) are existing. */
	pair<bool, bool> checkPredef() const;
	/** Get a list of the fonts required by current user matching definitions. */
	unordered_set<string> getFonts() const;
	/** Add a user matching definition at the top of the list to match the given user and force her
	to be ignored or un-ignored. If an untampered previously generated definition is already
	existing, it is removed. */
	void ignoreChat(const HintedUser& user, bool ignore);

private:
	friend class Singleton<UserMatchManager>;

	const UserMatches list; // const to make sure only setList can change this.
	std::unordered_map<string, size_t> styleIndices;
	mutable std::shared_mutex mutex; // shared to allow multiple readers (each hub).

	UserMatchManager();
	virtual ~UserMatchManager();

	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;
};

} // namespace dcpp

#endif
