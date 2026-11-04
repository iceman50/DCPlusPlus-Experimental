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

#include "stdinc.h"
#include <dcpp/plugins/PluginManager.h>

#include <dcpp/storage/Archive.h>
#include <dcpp/hub/Client.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/system/File.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/base/ScopedFunctor.h>
#include <dcpp/text/SimpleXML.h>
#include <dcpp/connection/UserConnection.h>
#include <dcpp/version.h>

#include <utility>
#include <filesystem>
#include <cmath>
#include <dcpp/plugins/PluginHttp.h>

#ifdef _WIN32
# define PLUGIN_EXT "*.dll"

# define LOAD_LIBRARY(filename) ::LoadLibraryExW(File::toNativePath(filename).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
# define FREE_LIBRARY(lib) ::FreeLibrary(lib)
# define GET_ADDRESS(lib, name) ::GetProcAddress(lib, name)
# define GET_ERROR() Util::translateError(GetLastError())
#else
# include "dlfcn.h"
# define PLUGIN_EXT "*.so"

# define LOAD_LIBRARY(filename) ::dlopen(filename.c_str(), RTLD_NOW | RTLD_LOCAL)
# define FREE_LIBRARY(lib) ::dlclose(lib)
# define GET_ADDRESS(lib, name) ::dlsym(lib, name)
# define GET_ERROR() ::dlerror()
#endif

namespace dcpp {

using std::make_unique;
using std::swap;
namespace {
thread_local vector<HookSubscriber*> callbackFrames;
thread_local string activeOwner;
thread_local vector<const void*> callbackObjects;
struct Changing {
	std::atomic<bool>& busy;
	explicit Changing(std::atomic<bool>& flag) : busy(flag) {
		if(busy.exchange(true)) throw Exception("A plugin lifecycle operation is already in progress");
	}
	~Changing() { busy = false; }
};
bool safePluginId(const string& id) {
	return Archive::isPackagePath(id) && id.find('/') == string::npos && id.size() <= 80;
}
}


PluginManager::CallbackScope::CallbackScope(const string& owner) : previous(activeOwner) { activeOwner = owner; }
PluginManager::CallbackScope::~CallbackScope() { activeOwner = std::move(previous); }
string PluginManager::callbackOwner() { return activeOwner; }
bool PluginManager::isCallbackObject(const void* object) {
	return object && std::find(callbackObjects.begin(), callbackObjects.end(), object) != callbackObjects.end();
}

string PluginManager::ownerForAddress(const void* address) {
	if(!activeOwner.empty()) return activeOwner;
#ifdef _WIN32
	HMODULE module = nullptr;
	if(address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(address), &module)) {
		Lock l(csHook);
		auto i = moduleOwners.find(module);
		if(i != moduleOwners.end()) return i->second;
	}
#endif
	return {};
}

void PluginManager::revokeOwner(const string& owner) {
	PluginHttp::cancelOwner(owner);
	vector<HookSubscriber*> subscriptions;
	vector<PluginHook*> ownedHooks;
	{
		Lock l(csHook);
		revokedOwners.insert(owner);
		for(const auto& hook: hooks) {
			if(hook.second->owner == owner) ownedHooks.push_back(hook.second.get());
			for(const auto& sub: hook.second->subscribers) if(sub->owner == owner) subscriptions.push_back(sub.get());
		}
		for(auto i = interfaceOwners.begin(); i != interfaceOwners.end();) {
			if(i->second == owner) { releaseInterface(i->first); i = interfaceOwners.begin(); }
			else ++i;
		}
	}
	for(auto sub: subscriptions) releaseHook(sub);
	for(auto hook: ownedHooks) destroyHook(hook);
}

PluginManager::PluginManager() : dcCore(), shutdown(false), secNum(Util::rand()) {
}

PluginManager::~PluginManager() {
}

DcextInfo PluginManager::extract(const string& path) {
	Archive archive(path);
	const auto entries = archive.packageEntries(1024, 64 * 1024 * 1024, 128 * 1024 * 1024);
	SimpleXML xml;
	xml.fromXML(archive.readFile("info.xml", 256 * 1024));

	DcextInfo info { };

	if(xml.findChild("dcext")) {
		xml.stepIn();

		auto parse = [&xml](string tag, string& out) {
			xml.resetCurrentChild();
			if(xml.findChild(tag)) {
				out = xml.getChildData();
				if(xml.findChild(tag)) throw Exception("Duplicate plugin metadata field");
			}
		};

		auto checkPlatform = [&xml](bool emptyAllowed) {
			auto platform = xml.getChildAttrib("Platform");
			if(emptyAllowed && platform.empty()) {
				return true;
			}
#if defined(_WIN32) && defined(_WIN64)
			return platform == "pe-x64";
#elif defined(_WIN32)
			return platform == "pe-x86";
#elif defined(__x86_64__)
			return platform == "elf-x64";
#elif defined(__i386__)
			return platform == "elf-x86";
#else
#error Unknown platform
#endif
		};

		string version;
		parse("ApiVersion", version);
		if(version != Util::toString(DCAPI_CORE_VER)) {
			throw Exception(str(F_("%1% is not compatible with %2%") % Util::getFileName(path) % APPNAME));
		}

		parse("UUID", info.uuid);
		parse("Name", info.name);
		version.clear(); parse("Version", version);
		try {
			size_t end = 0;
			info.version = std::stod(version, &end);
			if(end != version.size()) throw Exception("Invalid plugin version");
		} catch(const std::exception&) { throw Exception("Invalid plugin version"); }
		parse("Author", info.author);
		parse("Description", info.description);
		parse("Website", info.website);

		xml.resetCurrentChild();
		while(xml.findChild("Plugin")) {
			if(checkPlatform(false)) {
				if(!info.plugin.empty()) throw Exception("Multiple plugin binaries for this platform");
				info.plugin = xml.getChildData();
			}
		}

		xml.resetCurrentChild();
		if(xml.findChild("Files")) {
			xml.stepIn();

			while(xml.findChild("File")) {
				if(checkPlatform(true)) {
					info.files.push_back(xml.getChildData());
				}
			}

			xml.stepOut();
		}

		xml.stepOut();
	}

	if(!safePluginId(info.uuid) || info.name.empty() || !std::isfinite(info.version) || info.version <= 0) {
		throw Exception(str(F_("%1% is not a valid plugin") % Util::getFileName(path)));
	}
	if(info.plugin.empty()) {
		throw Exception(str(F_("%1% is not compatible with %2%") % Util::getFileName(path) % APPNAME));
	}

	{
		Lock l(cs);

		auto dupe = findPlugin(info.uuid);
		if(dupe) {
			if(dupe->version < info.version) {
				info.updating = true;
			} else {
				throw Exception(str(F_("%1% is already installed") % dupe->name));
			}
		}
	}

	auto package = std::make_shared<DcextInfo::Package>();
	package->uuid = info.uuid;
	package->plugin = info.plugin;
	package->version = info.version;
	auto files = info.files;
	files.push_back(info.plugin);
	std::set<string> targets;
	for(const auto& name: files) {
		if(!Archive::isPackagePath(name) || std::find(entries.begin(), entries.end(), name) == entries.end() || package->contents.count(name))
			throw Exception("Invalid or duplicate plugin manifest path");
		auto target = name == info.plugin ? Util::getFileName(name, '/') : name;
		std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
		if(!targets.insert(target).second) throw Exception("Conflicting plugin installation paths");
		package->contents.emplace(name, archive.readFile(name, 64 * 1024 * 1024));
	}
	for(const auto& target: targets) {
		for(auto slash = target.find('/'); slash != string::npos; slash = target.find('/', slash + 1))
			if(targets.count(target.substr(0, slash))) throw Exception("Conflicting plugin installation paths");
	}
	info.package = package; // immutable bytes bind the review dialog to this package

	return info;
}

void PluginManager::install(const DcextInfo& info) {
	if(shutdown) throw Exception("Plugin manager is shutting down");
	Changing change(changing);
	if(!info.package || info.uuid != info.package->uuid || info.plugin != info.package->plugin || info.version != info.package->version)
		throw Exception("Plugin package approval does not match the installation request");
	{
		Lock l(cs);
		auto old = findPlugin(info.uuid);
		if(old && (old->handle || old->version >= info.version))
			throw Exception("Disable the installed plugin and restart before updating it");
	}
	namespace fs = std::filesystem;
	fs::path target;
	try {
		const auto base = fs::absolute(fs::u8path(getInstallPath(info.uuid))).lexically_normal();
		// Reject reparse points/symlinks in the destination ancestry.
		for(auto part = base; !part.empty(); part = part.parent_path()) {
#ifdef _WIN32
			const auto attributes = GetFileAttributesW(part.c_str());
			if(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) throw Exception("Plugin destination is a reparse point");
#else
			if(fs::is_symlink(fs::symlink_status(part))) throw Exception("Plugin destination is a symlink");
#endif
			if(part == part.parent_path()) break;
		}
		fs::create_directories(base);
		for(unsigned attempt = 0; attempt < 32; ++attempt) {
			auto candidate = base / ("package-" + Util::toString(Util::rand()));
			if(fs::create_directory(candidate)) { target = candidate; break; }
		}
		if(target.empty()) throw Exception("Cannot create a private plugin installation directory");
		for(const auto& entry: info.package->contents) {
			auto file = target / fs::u8path(entry.first == info.plugin ? Util::getFileName(info.plugin, '/') : entry.first);
			fs::create_directories(file.parent_path());
			File output(file.u8string(), File::WRITE, File::CREATE | File::TRUNCATE);
			output.write(entry.second);
		}
		Plugin plugin {};
		plugin.path = (target / fs::u8path(Util::getFileName(info.plugin, '/'))).u8string();
		plugin.guid = info.uuid;
		plugin.version = info.version;
		plugin.name = info.name;
		enable(plugin, true, true);
	} catch(...) {
		// Loaded DLLs are pinned: failure cleanup may leave a DLL until exit.
		if(!target.empty()) { std::error_code ignored; fs::remove_all(target, ignored); }
		try { throw; } catch(const fs::filesystem_error& e) { throw Exception(e.what()); }
	}
}

void PluginManager::loadPlugins(function<void (const string&)> f) {
	TimerManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
	SettingsManager::getInstance()->addListener(this);

	loadSettings();

	for(auto& plugin: plugins) {
		if(!plugin.enabled) { continue; }
		if(f) { f(plugin.name); }
		try { enable(plugin, false, false); }
		catch(const Exception& e) { LogManager::getInstance()->message(e.getError(), LogMessage::SEV_ERROR, _("Plugins")); }
	}
}

void PluginManager::unloadPlugins() {
	if(shutdown.exchange(true)) return;
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);

	PluginHttp::stop(); // join requests before callbacks or plugin shutdown
	{
		Lock l(csHook);
		hookCV.wait(l, [this] {
			for(const auto& h: hooks) {
				if(h.second->defaultSubscription && h.second->defaultSubscription->active) return false;
				for(const auto& sub: h.second->subscribers) if(sub->active) return false;
			}
			for(const auto& sub: retiredSubscriptions) if(sub->active) return false;
			return true;
		});
	}
	saveSettings();
	for(auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
		if(it->handle && it->dcMain) {
			try { it->dcMain(ON_UNLOAD, nullptr, nullptr); } catch(...) { }
		}
	}
	// ABI 8 cannot establish worker-thread quiescence. Modules remain mapped
	// until process exit, including plugins that refuse ON_UNLOAD.
	Lock l(csHook);
	hooks.clear();
	interfaces.clear();
	interfaceHandles.clear();
}

void PluginManager::addPlugin(const string& path) {
	Changing change(changing);
	Plugin plugin {};
	plugin.name = Util::getFileName(path);
	plugin.path = path;
	enable(plugin, true, true);
}

bool PluginManager::configPlugin(const string& guid, dcptr_t data) {
	if(shutdown) return false;
	DCMAIN callback = nullptr;
	{
		Lock l(cs);
		auto p = findPlugin(guid);
		if(p && p->handle) callback = p->dcMain;
	}
	if(!callback) return false;
	CallbackScope scope(guid);
	try { return callback(ON_CONFIGURE, &dcCore, data) != False; } catch(...) { return false; }
}

void PluginManager::enablePlugin(const string& guid) {
	Changing change(changing);
	Plugin* p;
	{
		Lock l(cs);
		p = findPlugin(guid);
		if(!p) return;
		p->enabled = true;
		if(p->handle) return;
	}
	enable(*p, false, true);
}

void PluginManager::disablePlugin(const string& guid) {
	Lock l(cs);
	auto p = findPlugin(guid);
	if(p) p->enabled = false;
}

void PluginManager::movePlugin(const string& guid, int delta) {
	Changing change(changing);
	Lock l(cs);
	auto i = findPluginIter(guid);
	const auto index = i - plugins.begin();
	const auto target = static_cast<int64_t>(index) + delta;
	if(i != plugins.end() && target >= 0 && target < static_cast<int64_t>(plugins.size())) swap(*i, plugins[static_cast<size_t>(target)]);
}

void PluginManager::removePlugin(const string& guid) {
	Changing change(changing);
	Lock l(cs);
	auto i = findPluginIter(guid);
	if(i == plugins.end()) return;
	if(i->handle) throw Exception("Disable the plugin and restart before removing it");
	plugins.erase(i);
}

bool PluginManager::isLoaded(const string& guid) const {
	if(shutdown) return false;
	Lock l(cs);
	auto p = findPlugin(guid);
	return p ? bool(p->handle) : false;
}

StringList PluginManager::getPluginList() const {
	Lock l(cs);
	StringList ret(plugins.size());
	std::transform(plugins.begin(), plugins.end(), ret.begin(), [](const Plugin& p) { return p.guid; });
	return ret;
};

Plugin PluginManager::getPlugin(const string& guid) const {
	Lock l(cs);
	auto p = findPlugin(guid);
	return p ? *p : Plugin();
}

// Functions that call the plugin
bool PluginManager::onUDP(bool out, const string& ip, const string& port, const string& data) {
	UDPData udp = { ip.c_str(), static_cast<uint16_t>(Util::toUInt(port)) };
	DataArray payload { data.data(), data.size() };
	if(runHook(out ? HOOK_NETWORK_UDP_OUT2 : HOOK_NETWORK_UDP_IN2, &udp, &payload)) return true;
	return runHook(out ? HOOK_NETWORK_UDP_OUT : HOOK_NETWORK_UDP_IN, &udp,
		reinterpret_cast<dcptr_t>(const_cast<char*>(data.c_str())));
}

bool PluginManager::onChatTags(Tagger& tagger, OnlineUser* from) {
	TagData data = { reinterpret_cast<dcptr_t>(&tagger), True };
	return runHook(HOOK_UI_CHAT_TAGS, from, &data);
}

bool PluginManager::onChatDisplay(string& htmlMessage, OnlineUser* from) {
	StringData data = { htmlMessage.c_str() };
	bool handled = runHook(HOOK_UI_CHAT_DISPLAY, from, &data);
	if(handled && data.out) {
		htmlMessage = data.out;
		return true;
	}
	return false;
}

bool PluginManager::onChatCommand(Client* client, const string& line) {
	string cmd, param;
	auto si = line.find(' ');
	if(si != string::npos) {
		param = line.substr(si + 1);
		cmd = line.substr(1, si - 1);
	} else {
		cmd = line.substr(1);
	}

	CommandData data = { cmd.c_str(), param.c_str() };
	return runHook(HOOK_UI_CHAT_COMMAND, client, &data);
}

bool PluginManager::onChatCommandPM(const HintedUser& user, const string& line) {
	// Hopefully this is safe...
	bool res = false;
	auto lock = ClientManager::getInstance()->lock();
	OnlineUser* ou = ClientManager::getInstance()->findOnlineUser(user.user->getCID(), user.hint);

	if(ou) {
		string cmd, param;
		auto si = line.find(' ');
		if(si != string::npos) {
			param = line.substr(si + 1);
			cmd = line.substr(1, si - 1);
		} else {
			cmd = line.substr(1);
		}

		CommandData data = { cmd.c_str(), param.c_str() };
		res = runHook(HOOK_UI_CHAT_COMMAND_PM, ou, &data);
	}

	return res;
}

// Plugin interface registry
intfHandle PluginManager::registerInterface(const string& guid, dcptr_t funcs) {
	Lock l(csHook);
	if(shutdown || guid.empty() || !funcs || interfaces.count(guid) || revokedOwners.count(ownerForAddress(funcs))) return nullptr;
	interfaces.emplace(guid, funcs);
	// Monotonic opaque tokens are never derived from a provider's address.
	auto handle = reinterpret_cast<intfHandle>(++secNum);
	interfaceHandles.emplace(handle, guid);
	interfaceOwners.emplace(handle, ownerForAddress(funcs));
	return handle;
}

dcptr_t PluginManager::queryInterface(const string& guid) {
	Lock l(csHook);
	auto i = interfaces.find(guid);
	return shutdown || i == interfaces.end() ? nullptr : i->second;
}

bool PluginManager::releaseInterface(intfHandle handle) {
	Lock l(csHook);
	auto i = interfaceHandles.find(handle);
	if(i == interfaceHandles.end()) return false;
	interfaces.erase(i->second);
	interfaceHandles.erase(i);
	interfaceOwners.erase(handle);
	return true;
}

PluginHook* PluginManager::createHook(const string& guid, DCHOOK defProc) {
	Lock l(csHook);
	if(shutdown || guid.empty() || hooks.count(guid) || revokedOwners.count(ownerForAddress(reinterpret_cast<const void*>(defProc)))) return nullptr;
	auto hook = std::make_shared<PluginHook>();
	hook->guid = guid;
	hook->defProc = defProc;
	hook->owner = ownerForAddress(reinterpret_cast<const void*>(defProc));
	if(defProc) {
		hook->defaultSubscription = std::make_shared<HookSubscriber>();
		hook->defaultSubscription->hookProc = defProc;
		hook->defaultSubscription->common = nullptr;
		hook->defaultSubscription->owner = hook->owner;
	}
	hooks.emplace(guid, hook);
	return hook.get();
}

bool PluginManager::destroyHook(PluginHook* handle) {
	Lock l(csHook);
	for(auto i = hooks.begin(); i != hooks.end(); ++i) {
		if(i->second.get() != handle) continue;
		auto hook = i->second;
		hook->removed = true;
		if(hook->defaultSubscription) hook->subscribers.push_back(hook->defaultSubscription);
		for(auto& sub: hook->subscribers) { sub->removed = true; retiredSubscriptions.push_back(sub); }
		retiredHooks.push_back(hook);
		hooks.erase(i);
		for(const auto& sub: hook->subscribers) {
			const auto depth = std::count(callbackFrames.begin(), callbackFrames.end(), sub.get());
			if(callbackFrames.empty()) hookCV.wait(l, [&] { return sub->active <= static_cast<size_t>(depth); });
		}
		return true;
	}
	return false;
}

HookSubscriber* PluginManager::bindHook(const string& guid, DCHOOK callback, void* common) {
	Lock l(csHook);
	auto i = hooks.find(guid);
	if(shutdown || i == hooks.end() || !callback || revokedOwners.count(ownerForAddress(reinterpret_cast<const void*>(callback)))) return nullptr;
	auto sub = std::make_shared<HookSubscriber>();
	sub->hookProc = callback;
	sub->common = common;
	sub->owner = ownerForAddress(reinterpret_cast<const void*>(callback));
	i->second->subscribers.push_back(sub);
	return sub.get();
}

bool PluginManager::runHook(const string& guid, dcptr_t object, dcptr_t data) {
	PluginHook* handle;
	{
		Lock l(csHook);
		auto i = hooks.find(guid);
		if(shutdown || i == hooks.end()) return false;
		handle = i->second.get();
	}
	return runHook(handle, object, data);
}

bool PluginManager::runHook(PluginHook* handle, dcptr_t object, dcptr_t data) {
	std::shared_ptr<PluginHook> hook;
	vector<std::shared_ptr<HookSubscriber>> snapshot;
	{
		Lock l(csHook);
		if(shutdown) return false;
		for(const auto& entry: hooks) if(entry.second.get() == handle) { hook = entry.second; break; }
		if(!hook) return false;
		snapshot = hook->subscribers;
		if(snapshot.empty() && hook->defaultSubscription) snapshot.push_back(hook->defaultSubscription);
	}
	Bool stop = False;
	bool handled = false;
	for(const auto& sub: snapshot) {
		{
			Lock l(csHook);
			if(shutdown || hook->removed || sub->removed) continue;
			++sub->active;
		}
		CallbackScope scope(sub->owner);
		callbackFrames.push_back(sub.get());
		callbackObjects.push_back(object);
		try { if(sub->hookProc(object, data, sub->common, &stop) != False) handled = true; } catch(...) { }
		callbackObjects.pop_back();
		callbackFrames.pop_back();
		{
			Lock l(csHook);
			--sub->active;
			hookCV.notify_all();
		}
		if(stop) break;
	}
	return handled;
}

size_t PluginManager::releaseHook(HookSubscriber* handle) {
	Lock l(csHook);
	for(auto& entry: hooks) {
		auto& subscriptions = entry.second->subscribers;
		for(auto i = subscriptions.begin(); i != subscriptions.end(); ++i) {
			if(i->get() != handle) continue;
			auto sub = *i;
			sub->removed = true;
			retiredSubscriptions.push_back(sub);
			subscriptions.erase(i);
			const auto remaining = subscriptions.size();
			const auto depth = std::count(callbackFrames.begin(), callbackFrames.end(), handle);
			if(callbackFrames.empty()) hookCV.wait(l, [&] { return sub->active <= static_cast<size_t>(depth); });
			return remaining;
		}
	}
	return 0;
}

bool PluginManager::drainHook(HookSubscriber* handle) {
	if(!callbackFrames.empty()) return false; // cross-callback waits can deadlock
	Lock l(csHook);
	for(auto sub: retiredSubscriptions) {
		if(sub.get() != handle) continue;
		hookCV.wait(l, [&] { return sub->active == 0; });
		return true;
	}
	return false;
}

// Plugin configuration
void PluginManager::setPluginSetting(const string& guid, const string& setting, const string& value) {
	Lock l(cs);
	auto p = findPlugin(guid);
	if(p) {
		p->settings[setting] = value;
	}
}

string PluginManager::getPluginSetting(const string& guid, const string& setting) {
	string value;
	tryGetPluginSetting(guid, setting, value);
	return value;
}

bool PluginManager::tryGetPluginSetting(const string& guid, const string& setting, string& value) {
	Lock l(cs);
	auto p = findPlugin(guid);
	if(!p) return false;
	auto i = p->settings.find(setting);
	if(i == p->settings.end()) return false;
	value = i->second;
	return true;
}

void PluginManager::removePluginSetting(const string& guid, const string& setting) {
	Lock l(cs);

	auto p = findPlugin(guid);
	if(p) {
		auto i = p->settings.find(setting);
		if(i != p->settings.end()) {
			p->settings.erase(i);
		}
	}
}

string PluginManager::getInstallPath(const string& uuid) {
	if(!safePluginId(uuid)) throw Exception("Invalid plugin identifier");
	return Util::getPath(Util::PATH_USER_LOCAL) + "Plugins" PATH_SEPARATOR_STR + uuid + PATH_SEPARATOR_STR;
}

void PluginManager::enable(Plugin& original, bool install, bool runtime) {
	if(shutdown) throw Exception("Plugin manager is shutting down");
	Plugin candidate = original;
	try { candidate.path = std::filesystem::absolute(std::filesystem::u8path(candidate.path)).u8string(); }
	catch(const std::filesystem::filesystem_error& e) { throw Exception(e.what()); }
	candidate.handle = LOAD_LIBRARY(candidate.path);
	if(!candidate.handle) throw Exception(str(F_("Error loading %1%: %2%") % candidate.name % GET_ERROR()));
	// Even a rejected native plugin can have started threads in its initializer.
	// Retain every successful loader reference until process exit.
	inactive.push_back(candidate.handle);
	DCMAIN (DCAPI *initialize)(MetaDataPtr) = nullptr;
	const auto address = GET_ADDRESS(candidate.handle, "pluginInit");
	static_assert(sizeof(initialize) == sizeof(address), "Plugin function pointer size");
	memcpy(&initialize, &address, sizeof(initialize));
	MetaData info {};
	try { candidate.dcMain = initialize ? initialize(&info) : nullptr; }
	catch(...) { throw Exception("Plugin metadata initialization failed"); }
	if(!candidate.dcMain || !info.guid || !safePluginId(info.guid) || !info.name || !*info.name ||
		!std::isfinite(info.version) || info.version <= 0 || info.apiVersion != DCAPI_CORE_VER ||
		(info.numDependencies && !info.dependencies) || info.numDependencies > 128)
		throw Exception("Invalid or incompatible plugin metadata");
	if(!original.guid.empty() && (original.guid != info.guid || (install && original.version != info.version)))
		throw Exception("Plugin binary identity does not match its manifest or installed identity");
	for(decltype(info.numDependencies) i = 0; i < info.numDependencies; ++i) {
		if(!info.dependencies[i] || !isLoaded(info.dependencies[i])) throw Exception("Missing plugin dependency");
	}
	candidate.guid = info.guid;
	candidate.name = info.name;
	candidate.version = info.version;
	candidate.author = info.author ? info.author : "";
	candidate.description = info.description ? info.description : "";
	candidate.website = info.web ? info.web : "";
	candidate.enabled = true;
	Plugin previous {};
	bool added = false;
	{
		Lock l(cs);
		auto existing = findPlugin(candidate.guid);
		if(install && existing && (existing->handle || existing->version >= candidate.version))
			throw Exception("Disable the installed plugin and restart before updating it");
		if(existing) {
			previous = *existing;
			candidate.settings = existing->settings;
			*existing = candidate;
			install = false;
		} else { plugins.push_back(candidate); added = true; }
	}
	{ Lock l(csHook); moduleOwners[candidate.handle] = candidate.guid; revokedOwners.erase(candidate.guid); }
	CallbackScope scope(candidate.guid);
	bool accepted = false;
	try { accepted = candidate.dcMain(install ? ON_INSTALL : runtime ? ON_LOAD_RUNTIME : ON_LOAD, &dcCore, nullptr) != False; }
	catch(...) { }
	if(!accepted) {
		revokeOwner(candidate.guid);
		Lock l(cs);
		auto it = findPluginIter(candidate.guid);
		if(added) plugins.erase(it); else *it = previous;
		throw Exception("Plugin rejected initialization: " + candidate.name);
	}
}

void PluginManager::loadSettings() noexcept {
	Lock l(cs);

	plugins.clear();

	try {
		Util::migrate(Util::getPath(Util::PATH_USER_CONFIG) + "Plugins.xml");

		SimpleXML xml;
		xml.fromXML(File(Util::getPath(Util::PATH_USER_CONFIG) + "Plugins.xml", File::READ, File::OPEN).read());
		if(xml.findChild("Plugins")) {
			xml.stepIn();

			while(xml.findChild("Plugin")) {
				Plugin plugin { xml.getChildAttrib("Guid"), xml.getChildAttrib("Name"),
					Util::toDouble(xml.getChildAttrib("Version")), xml.getChildAttrib("Author"),
					xml.getChildAttrib("Description"), xml.getChildAttrib("Website"),
					xml.getChildAttrib("Path"), nullptr,
					nullptr };
				plugin.enabled = xml.getBoolChildAttrib("Enabled");

				if(plugin.guid.empty() || plugin.path.empty()) { continue; }
				if(plugin.name.empty()) { plugin.name = Util::getFileName(plugin.path); }

				xml.stepIn();
				for(auto& i: xml.getCurrentChildren()) {
					plugin.settings[i.first] = i.second;
				}
				xml.stepOut();

				plugins.push_back(move(plugin));
			}

			xml.stepOut();
		}
	} catch(const Exception& e) {
		dcdebug("PluginManager::loadSettings: %s\n", e.getError().c_str());
	}
}

void PluginManager::saveSettings() noexcept {
	Lock l(cs);

	try {
		SimpleXML xml;
		xml.addTag("Plugins");
		xml.stepIn();

		for(auto& plugin: plugins) {
			xml.addTag("Plugin");

			xml.addChildAttrib("Guid", plugin.guid);
			xml.addChildAttrib("Name", plugin.name);
			xml.addChildAttrib("Version", plugin.version);
			xml.addChildAttrib("Author", plugin.author);
			xml.addChildAttrib("Description", plugin.description);
			xml.addChildAttrib("Website", plugin.website);
			xml.addChildAttrib("Path", plugin.path);
			xml.addChildAttrib("Enabled", plugin.enabled);

			xml.stepIn();
			for(auto& i: plugin.settings) {
				xml.addTag(i.first, i.second);
			}
			xml.stepOut();
		}

		xml.stepOut();

		string fname = Util::getPath(Util::PATH_USER_CONFIG) + "Plugins.xml";
		File f(fname + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
		f.write(SimpleXML::utf8Header);
		f.write(xml.toXML());
		f.close();
		#ifdef _WIN32
		if(!MoveFileExW(File::toNativePath(fname + ".tmp").c_str(), File::toNativePath(fname).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			throw Exception(Util::translateError(GetLastError()));
#else
		std::error_code error;
		std::filesystem::rename(std::filesystem::u8path(fname + ".tmp"), std::filesystem::u8path(fname), error);
		if(error) throw Exception(error.message());
#endif
	} catch(const Exception& e) {
		dcdebug("PluginManager::saveSettings: %s\n", e.getError().c_str());
	}
}

const Plugin* PluginManager::findPlugin(const string& guid) const {
	auto it = findPluginIter(guid);
	return it != plugins.end() ? &*it : nullptr;
}

Plugin* PluginManager::findPlugin(const string& guid) {
	auto it = findPluginIter(guid);
	return it != plugins.end() ? &*it : nullptr;
}

vector<Plugin>::const_iterator PluginManager::findPluginIter(const string& guid) const {
	return std::find_if(plugins.begin(), plugins.end(), [&guid](const Plugin& p) { return p.guid == guid; });
}

vector<Plugin>::iterator PluginManager::findPluginIter(const string& guid) {
	return std::find_if(plugins.begin(), plugins.end(), [&guid](const Plugin& p) { return p.guid == guid; });
}

// Listeners
void PluginManager::on(ClientManagerListener::ClientConnected, Client* aClient) noexcept {
	runHook(HOOK_HUB_ONLINE, aClient);
}

void PluginManager::on(ClientManagerListener::ClientDisconnected, Client* aClient) noexcept {
	runHook(HOOK_HUB_OFFLINE, aClient);
}

void PluginManager::on(QueueManagerListener::Added, QueueItem* qi) noexcept {
	runHook(HOOK_QUEUE_ADDED, qi);
}

void PluginManager::on(QueueManagerListener::Moved, QueueItem* qi, const string& /*aSource*/) noexcept {
	runHook(HOOK_QUEUE_MOVED, qi);
}

void PluginManager::on(QueueManagerListener::Removed, QueueItem* qi) noexcept {
	runHook(HOOK_QUEUE_REMOVED, qi);
}

void PluginManager::on(QueueManagerListener::Finished, QueueItem* qi, const string& /*dir*/, int64_t /*speed*/) noexcept {
	runHook(HOOK_QUEUE_FINISHED, qi);
}

void PluginManager::on(SettingsManagerListener::Save, SimpleXML& /*xml*/) noexcept {
	saveSettings();
}

} // namespace dcpp
