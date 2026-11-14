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

#ifndef DCPLUSPLUS_DCPP_PLUGIN_MANAGER_H
#define DCPLUSPLUS_DCPP_PLUGIN_MANAGER_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <set>

#include <dcpp/core/header/typedefs.h>

#include <dcpp/hub/ClientManagerListener.h>
#include <dcpp/plugins/PluginDefs.h>
#include <dcpp/plugins/PluginEntity.h>
#include <dcpp/queue/QueueManagerListener.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/core/Singleton.h>
#include <dcpp/message/Tagger.h>
#include <dcpp/core/timer/TimerManager.h>

#ifdef _WIN32
typedef HMODULE PluginHandle;
#else
typedef void* PluginHandle;
#endif

namespace dcpp {

using std::function;
using std::map;
using std::string;
using std::unique_ptr;
using std::vector;

// Represents a plugin in hook context
struct HookSubscriber {
	DCHOOK hookProc;
	void* common;
	string owner;
	bool removed = false;
	size_t active = 0;
};

// Hookable event
struct PluginHook {
	string guid;
	string owner;
	DCHOOK defProc;
	std::shared_ptr<HookSubscriber> defaultSubscription;

	vector<std::shared_ptr<HookSubscriber>> subscribers;
	bool removed = false;
	CriticalSection cs;
};

/** Information about a registered plugin. It may or may not be loaded. */
struct Plugin {
	string guid;
	string name;
	double version;
	string author;
	string description;
	string website;
	string path;

	PluginHandle handle; /// identifies the state (enabled / disabled) of this plugin.
	DCMAIN dcMain;

	StringMap settings;
	bool enabled = true; // desired state on the next application start
};

/** Information about a dcext-packaged plugin that has just been extracted. */
struct DcextInfo {
	string uuid;
	string name;
	double version;
	string author;
	string description;
	string website;
	string plugin;
	StringList files;
	bool updating;
	struct Package {
		string uuid, plugin;
		double version;
		std::map<string, string> contents;
	};
private:
	std::shared_ptr<const Package> package;
	friend class PluginManager;
};

class PluginManager : public Singleton<PluginManager>, private TimerManagerListener,
	private ClientManagerListener, private QueueManagerListener, private SettingsManagerListener
{
public:
	PluginManager();
	~PluginManager();

	/** Extract a dcext-packaged plugin. Throws on errors. */
	DcextInfo extract(const string& path);
	void install(const DcextInfo& info);

	void loadPlugins(function<void (const string&)> f);
	void unloadPlugins();

	/** Install a plain plugin (not dcext-packaged). Throws on errors. */
	void addPlugin(const string& path);
	bool configPlugin(const string& guid, dcptr_t data);
	void enablePlugin(const string& guid);
	void disablePlugin(const string& guid);
	void movePlugin(const string& guid, int delta);
	void removePlugin(const string& guid);

	bool isLoaded(const string& guid) const;

	StringList getPluginList() const;
	Plugin getPlugin(const string& guid) const;

	DCCorePtr getCore() { return &dcCore; }
	class CallbackScope {
	public:
		explicit CallbackScope(const string& owner);
		~CallbackScope();
	private:
		string previous;
	};
	static string callbackOwner();
	static bool isCallbackObject(const void* object);

	// Functions that call the plugin
	bool onUDP(bool out, const string& ip, const string& port, const string& data);
	bool onChatTags(Tagger& tagger, OnlineUser* from = nullptr);
	bool onChatDisplay(string& htmlMessage, OnlineUser* from = nullptr);
	bool onChatCommand(Client* client, const string& line);
	bool onChatCommandPM(const HintedUser& user, const string& line);

	// runHook wrappers for host calls
	bool runHook(const string& guid, dcptr_t pObject, dcptr_t pData = NULL);

	template<class T>
	bool runHook(const string& guid, PluginEntity<T>* entity, dcptr_t pData = NULL) {
		if(entity) {
			Lock l(entity->cs);
			return runHook(guid, entity->getPluginObject(), pData);
		}
		return runHook(guid, nullptr, pData);
	}

	template<class T>
	bool runHook(const string& guid, PluginEntity<T>* entity, const string& data) {
		return runHook<T>(guid, entity, reinterpret_cast<dcptr_t>(const_cast<char*>(data.c_str())));
	}

	// Plugin interface registry
	intfHandle registerInterface(const string& guid, dcptr_t funcs);
	dcptr_t queryInterface(const string& guid);
	bool releaseInterface(intfHandle hInterface);

	// Plugin Hook system
	PluginHook* createHook(const string& guid, DCHOOK defProc);
	bool destroyHook(PluginHook* hook);

	HookSubscriber* bindHook(const string& guid, DCHOOK hookProc, void* pCommon);
	bool runHook(PluginHook* hook, dcptr_t pObject, dcptr_t pData);
	size_t releaseHook(HookSubscriber* subscription);
	bool drainHook(HookSubscriber* subscription);

	// Plugin configuration
	void setPluginSetting(const string& guid, const string& setting, const string& value);
	string getPluginSetting(const string& guid, const string& setting);
	bool tryGetPluginSetting(const string& guid, const string& setting, string& value);
	void removePluginSetting(const string& guid, const string& setting);

	static string getInstallPath(const string& uuid);

private:
	void enable(Plugin& plugin, bool install, bool runtime);
	string ownerForAddress(const void* address);
	void revokeOwner(const string& owner);

	void loadSettings() noexcept;
	void saveSettings() noexcept;

	const Plugin* findPlugin(const string& guid) const;
	Plugin* findPlugin(const string& guid);
	vector<Plugin>::const_iterator findPluginIter(const string& guid) const;
	vector<Plugin>::iterator findPluginIter(const string& guid);

	// Listeners
	void on(TimerManagerListener::Second, uint64_t ticks) noexcept { runHook(HOOK_TIMER_SECOND, NULL, &ticks); }
	void on(TimerManagerListener::Minute, uint64_t ticks) noexcept { runHook(HOOK_TIMER_MINUTE, NULL, &ticks); }

	void on(ClientManagerListener::ClientConnected, Client* aClient) noexcept;
	void on(ClientManagerListener::ClientDisconnected, Client* aClient) noexcept;

	void on(QueueManagerListener::Added, QueueItem* qi) noexcept;
	void on(QueueManagerListener::Moved, QueueItem* qi, const string& /*aSource*/) noexcept;
	void on(QueueManagerListener::Removed, QueueItem* qi) noexcept;
	void on(QueueManagerListener::Finished, QueueItem* qi, const string& /*dir*/, int64_t /*speed*/) noexcept;

	void on(SettingsManagerListener::Save, SimpleXML& /*xml*/) noexcept;

	vector<Plugin> plugins;
	vector<PluginHandle> inactive;

	map<string, std::shared_ptr<PluginHook>> hooks;
	// Keep retired handle addresses unique for the entire manager lifetime.
	vector<std::shared_ptr<PluginHook>> retiredHooks;
	vector<std::shared_ptr<HookSubscriber>> retiredSubscriptions;
	std::condition_variable_any hookCV;
	map<string, dcptr_t> interfaces;
	map<intfHandle, string> interfaceHandles;
	map<intfHandle, string> interfaceOwners;
	map<PluginHandle, string> moduleOwners;
	std::set<string> revokedOwners;

	DCCore dcCore;
	mutable CriticalSection cs, csHook;
	std::atomic<bool> shutdown;
	std::atomic<bool> changing { false };

	uintptr_t secNum;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_PLUGIN_MANAGER_H)
