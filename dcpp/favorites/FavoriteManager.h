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

#ifndef DCPLUSPLUS_DCPP_FAVORITE_MANAGER_H
#define DCPLUSPLUS_DCPP_FAVORITE_MANAGER_H

#include <functional>
#include <optional>

#include <dcpp/settings/SettingsManager.h>

#include <dcpp/core/thread/CriticalSection.h>
#include <dcpp/connection/http/HttpManagerListener.h>
#include <dcpp/hub/user_command/UserCommand.h>
#include <dcpp/favorites/FavoriteUser.h>
#include <dcpp/core/Singleton.h>
#include <dcpp/hub/ClientManagerListener.h>
#include <dcpp/favorites/FavoriteManagerListener.h>
#include <dcpp/favorites/HubEntry.h>
#include <dcpp/favorites/FavHubGroup.h>
#include <dcpp/user/User.h>

namespace dcpp {

using std::optional;

class SimpleXML;

/**
 * Public hub list, favorites (hub&user). Assumed to be called only by UI thread.
 */
class FavoriteManager : public Speaker<FavoriteManagerListener>, public Singleton<FavoriteManager>,
	private ClientManagerListener, private HttpManagerListener, private SettingsManagerListener
{
public:
// Public Hubs
	enum HubTypes {
		TYPE_NORMAL,
		TYPE_BZIP2
	};
	StringList getHubLists();
	void setHubList(int aHubList);
	int getSelectedHubList() { return lastServer; }
	void refresh(bool forceDownload = false, bool forceCached = false);
	HubTypes getHubListType() { return listType; }
	HubEntryList getPublicHubs() {
		Lock l(cs);
		return publicListMatrix[publicListServer];
	}
	bool isDownloading() { return (useHttp && running); }
	const string& getCurrentHubList() const { return publicListServer; }
	/// @return ref to the reason string the current list is blacklisted for; or empty string otherwise
	const string& blacklisted() const;
	/// @param domain a domain name with 2 words max, such as "example.com"
	void addBlacklist(const string& domain, const string& reason);

// Favorite Users
	typedef unordered_map<CID, FavoriteUser> FavoriteMap;
	FavoriteMap getFavoriteUsers() { Lock l(cs); return users; }

	void addFavoriteUser(const UserPtr& aUser);
	bool isFavoriteUser(const UserPtr& aUser) const { Lock l(cs); return users.find(aUser->getCID()) != users.end(); }
	void removeFavoriteUser(const UserPtr& aUser);

	optional<FavoriteUser> getFavoriteUser(const UserPtr& aUser) const;
	bool hasSlot(const UserPtr& aUser) const;
	void setUserDescription(const UserPtr& aUser, const string& description);
	void setAutoGrant(const UserPtr& aUser, bool grant);
	void userUpdated(const OnlineUser& info);
	time_t getLastSeen(const UserPtr& aUser) const;
	std::string getUserURL(const UserPtr& aUser) const;

// Favorite Hubs
	const FavoriteHubEntryList& getFavoriteHubs() const { return favoriteHubs; }
	FavoriteHubEntryList& getFavoriteHubs() { return favoriteHubs; }

	void addFavorite(const FavoriteHubEntry& aEntry);
	void removeFavorite(FavoriteHubEntry* entry);
	bool isFavoriteHub(const std::string& aUrl);
	FavoriteHubEntryPtr getFavoriteHubEntry(const string& aServer) const;
	/**
	 * Return true when the favorite has an explicit share profile. An empty
	 * returned set is meaningful and represents a valid, empty file list.
	 * Network threads call this for every search and upload, so the profile of
	 * any favorite for the hub applies however its address is written, and an
	 * address a favorite had earlier in this session keeps that favorite's profile.
	 */
	bool getHubShareDirectories(const string& aServer, std::set<string>& directories) const;
	/** Change a favorite hub while holding the favorites lock, then save. Share
	 * lookups on network threads never see a half-changed entry. An entry that is
	 * not stored yet is just changed (and not saved). */
	void editFavorite(FavoriteHubEntry* entry, const std::function<void (FavoriteHubEntry&)>& edit);
	/** Replace the order of the favorite hubs; the list must hold the same entries. */
	void reorderFavorites(const FavoriteHubEntryList& order);
	/** Remember the failover addresses a favorite hub announces. Returns whether they changed. */
	bool setFailoverServers(const string& aServer, const StringList& urls);

	void mergeHubSettings(const FavoriteHubEntry& entry, HubSettings& settings) const;

// Favorite hub groups
	const FavHubGroups& getFavHubGroups() const { return favHubGroups; }
	void setFavHubGroups(const FavHubGroups& favHubGroups_) { favHubGroups = favHubGroups_; }

	FavoriteHubEntryList getFavoriteHubs(const string& group) const;

// Favorite Directories
	bool addFavoriteDir(const string& aDirectory, const string& aName);
	bool removeFavoriteDir(const string& aName);
	bool renameFavoriteDir(const string& aName, const string& anotherName);
	StringPairList getFavoriteDirs() { return favoriteDirs; }

// User Commands
	UserCommand addUserCommand(int type, int ctx, int flags, const string& name, const string& command, const string& to, const string& hub);
	bool getUserCommand(int cid, UserCommand& uc);
	int findUserCommand(const string& aName, const string& aUrl);
	bool moveUserCommand(int cid, int pos);
	void updateUserCommand(const UserCommand& uc);
	void removeUserCommand(int cid);
	void removeUserCommand(const string& srv);
	void removeHubUserCommands(int ctx, const string& hub);

	UserCommand::List getUserCommands() { Lock l(cs); return userCommands; }
	UserCommand::List getUserCommands(int ctx, const StringList& hub);

	void load();
	void save();

	void shutdown();

private:
	FavoriteHubEntryList favoriteHubs;
	/* Canonical keys (Util::hubUrlKey) of every favorite's address and failovers,
	 * rebuilt whenever they change, so share lookups don't parse addresses per search. */
	struct ServerKey {
		string key;
		FavoriteHubEntry* hub;
	};
	std::vector<ServerKey> serverKeys;
	/* Addresses favorites had earlier in this session. An open hub window keeps
	 * the address it was opened with, so its share profile must not lapse when
	 * the favorite's address is edited. Used only for share profiles: a favorite's
	 * password must never follow it to an address it no longer has. */
	struct PreviousServer {
		string url;
		string key;
		FavoriteHubEntry* hub;
	};
	std::vector<PreviousServer> previousServers;
	void rebuildServerKeys();
	FavHubGroups favHubGroups;
	StringPairList favoriteDirs;
	UserCommand::List userCommands;
	int lastId;

	FavoriteMap users;

	mutable CriticalSection cs;

	// Public Hubs
	typedef unordered_map<string, HubEntryList> PubListMap;
	PubListMap publicListMatrix;
	string publicListServer;
	bool useHttp, running;
	HttpConnection* c;
	int lastServer;
	HubTypes listType;
	StringMap blacklist;
	string cachedFileName;

	/** Used during loading to prevent saving. */
	bool dontSave;

	friend class Singleton<FavoriteManager>;

	FavoriteManager();
	virtual ~FavoriteManager();

	FavoriteHubEntryPtr getFavoriteHub(const string& aServer);

	// ClientManagerListener
	void on(UserUpdated, const OnlineUser& user) noexcept;
	void on(UserConnected, const UserPtr& user) noexcept;
	void on(UserDisconnected, const UserPtr& user) noexcept;

	// HttpManagerListener
	void on(HttpManagerListener::Added, HttpConnection*) noexcept;
	void on(HttpManagerListener::Failed, HttpConnection*, const string&) noexcept;
	void on(HttpManagerListener::Complete, HttpConnection*, OutputStream*) noexcept;

	bool onHttpFinished(const string& buf) noexcept;

	// SettingsManagerListener
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
		load(xml);
	}

	void load(SimpleXML& aXml);

	static string getConfigFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "Favorites.xml"; }
};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)
