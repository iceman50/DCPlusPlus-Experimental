/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include <win32/util/IconManager.h>
#include <win32/util/CountryFlags.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <dcpp/core/io/compress/PackageCache.h>
#include <dcpp/core/io/File.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/core/io/xml/SimpleXML.h>
#include <dcpp/util/text/StringTokenizer.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/util/Util.h>

#include <dwt/DWTException.h>
#include <dwt/resources/Icon.h>

#include <win32/resource.h>

using namespace dcpp;

namespace {

constexpr int64_t MAX_ICON_ARCHIVE_SIZE = 32 * 1024 * 1024;
constexpr size_t MAX_ICON_FILE_SIZE = 4 * 1024 * 1024;
constexpr size_t MAX_MANIFEST_SIZE = 256 * 1024;
constexpr size_t MAX_PACKAGE_ICONS = 512;
constexpr size_t MAX_METADATA_LENGTH = 128;
constexpr size_t MAX_ARCHIVE_PATH_LENGTH = 512;
/** Limits on what a package may extract to: room for every icon, and a few times what the
 * bundled packages hold. */
constexpr size_t MAX_ARCHIVE_ENTRIES = 1024;
constexpr uint64_t MAX_EXTRACTED_SIZE = 128 * 1024 * 1024;
constexpr const char* DARK_PACK_FILE = "Dark.dcico";

struct IconDefinition {
	unsigned id;
	const char* fileName;
};

constexpr IconDefinition ICONS[] = {
	{ IDI_DCPP, "DCPlusPlus.ico" }, { IDI_PUBLICHUBS, "PublicHubs.ico" },
	{ IDI_SEARCH, "Search.ico" }, { IDI_FAVORITE_HUBS, "FavoriteHubs.ico" },
	{ IDI_PRIVATE, "UserOn.ico" }, { IDI_DIRECTORY, "Directory.ico" },
	{ IDI_HUB, "HubOn.ico" }, { IDI_NOTEPAD, "Notepad.ico" },
	{ IDI_QUEUE, "Queue.ico" }, { IDI_FINISHED_DL, "FinishedDL.ico" },
	{ IDI_FINISHED_UL, "FinishedUL.ico" }, { IDI_ADLSEARCH, "ADLSearch.ico" },
	{ IDI_USERS, "Users.ico" }, { IDI_NET_STATS, "NetStats.ico" },
	{ IDI_MAGNET, "Magnet.ico" }, { IDI_HUB_OFF, "HubOff.ico" },
	{ IDI_PRIVATE_OFF, "UserOff.ico" }, { IDI_GROUPED_BY_FILES, "GroupedByFiles.ico" },
	{ IDI_GROUPED_BY_USERS, "GroupedByUsers.ico" }, { IDI_EXIT, "Exit.ico" },
	{ IDI_CHAT, "Chat.ico" }, { IDI_HELP, "Help.ico" },
	{ IDI_OPEN_DL_DIR, "OpenDLDir.ico" }, { IDI_OPEN_FILE_LIST, "OpenFileList.ico" },
	{ IDI_RECONNECT, "Reconnect.ico" }, { IDI_SETTINGS, "Settings.ico" },
	{ IDI_TRAY_PM, "TrayPM.ico" }, { IDI_TRUSTED, "Trusted.ico" },
	{ IDI_SECURE, "Secure.ico" }, { IDI_RECENTS, "Recents.ico" },
	{ IDI_WHATS_THIS, "WhatsThis.ico" }, { IDI_DOWNLOAD, "Download.ico" },
	{ IDI_UPLOAD, "Upload.ico" }, { IDI_CHANGELOG, "Changelog.ico" },
	{ IDI_DONATE, "Donate.ico" }, { IDI_GET_STARTED, "GetStarted.ico" },
	{ IDI_INDEXING, "Indexing.ico" }, { IDI_LINKS, "Links.ico" },
	{ IDI_REFRESH, "Refresh.ico" }, { IDI_SLOTS, "Slots.ico" },
	{ IDI_OK, "OK.ico" }, { IDI_CANCEL, "Cancel.ico" },
	{ IDI_LEFT, "Left.ico" }, { IDI_RIGHT, "Right.ico" },
	{ IDI_USER, "User.ico" }, { IDI_USER_AWAY, "UserAway.ico" },
	{ IDI_USER_BOT, "UserBot.ico" }, { IDI_USER_NOCON, "UserNoCon.ico" },
	{ IDI_USER_NOSLOT, "UserNoSlot.ico" }, { IDI_USER_OP, "UserOp.ico" },
	{ IDI_UP, "Up.ico" }, { IDI_FILE, "File.ico" },
	{ IDI_EXEC, "Exec.ico" }, { IDI_FAVORITE_USER_ON, "FavoriteUserOn.ico" },
	{ IDI_FAVORITE_USER_OFF, "FavoriteUserOff.ico" }, { IDI_GREEN_BALL, "BallGreen.ico" },
	{ IDI_RED_BALL, "BallRed.ico" }, { IDI_SLOTS_FULL, "SlotsFull.ico" },
	{ IDI_ADVANCED, "Advanced.ico" }, { IDI_CLOCK, "Clock.ico" },
	{ IDI_STYLES, "Styles.ico" }, { IDI_BW_LIMITER, "BandwidthLimiter.ico" },
	{ IDI_CONN_GREY, "ConnGrey.ico" }, { IDI_CONN_BLUE, "ConnBlue.ico" },
	{ IDI_EXPERT, "Expert.ico" }, { IDI_FAVORITE_DIRS, "FavoriteDirs.ico" },
	{ IDI_LOGS, "Logs.ico" }, { IDI_NOTIFICATIONS, "Notifications.ico" },
	{ IDI_PROXY, "Proxy.ico" }, { IDI_TABS, "Tabs.ico" },
	{ IDI_WINDOWS, "Windows.ico" }, { IDI_BALLOON, "Balloon.ico" },
	{ IDI_SOUND, "Sound.ico" }, { IDI_ULIMIT, "ULimit.ico" },
	{ IDI_DLIMIT, "DLimit.ico" }, { IDI_OPEN_OWN_FILE_LIST, "OpenOwnFileList.ico" },
	{ IDI_PLUGINS, "Plugins.ico" }, { IDI_UPLOAD_FILTERING, "UploadFiltering.ico" },
	{ IDI_USER_REG, "UserReg.ico" }, { IDI_DELETE, "Remove.ico" },
	{ IDI_PAUSE, "Pause.ico" }, { IDI_PLAY, "Play.ico" },
	{ IDI_INCREMENT, "Increment.ico" }, { IDI_DECREMENT, "Decrement.ico" },
	{ IDI_REMOVEQUEUE, "RemoveQueue.ico" }, { IDI_DCPP_WARNING, "Warning.ico" }
};

struct State {
	struct CachedIcon {
		dwt::IconPtr icon;
		bool fromPackage;
	};
	std::mutex mutex;
	std::unordered_map<uint64_t, CachedIcon> cache;
	std::unordered_map<string, string> entries;
	string activePack;
	/** Where the active package is extracted. */
	string activeFolder;
	IconManager::Package package {};
	bool initialized = false;
	bool darkMode = false;
	bool packResolved = false;
};

State& state() {
	static State value;
	return value;
}

uint64_t cacheKey(unsigned resourceId, long size) noexcept {
	return (static_cast<uint64_t>(resourceId) << 32) | static_cast<uint32_t>(size);
}

bool safeRelativePath(string path) {
	if(path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != string::npos) return false;
	std::replace(path.begin(), path.end(), '\\', '/');
	StringTokenizer<string> parts(path, '/');
	for(const auto& part: parts.getTokens()) {
		if(part.empty() || part == "." || part == "..") return false;
	}
	return true;
}

string packDirectory(Util::Paths path) {
	return Util::getPath(path) + "IconPacks" PATH_SEPARATOR_STR;
}

/** A manifest path (with / or \) as a path below an extracted package's folder. */
string nativePath(string path) {
	std::replace(path.begin(), path.end(), '/', PATH_SEPARATOR);
	std::replace(path.begin(), path.end(), '\\', PATH_SEPARATOR);
	return path;
}

/** Read a package's manifest, from its extracted copy (see PackageCache: a package is extracted the
 * first time it is read); `folder` receives where that is. */
IconManager::Package readManifest(const string& path, std::unordered_map<string, string>* entries, string* folder = nullptr) {
	if(Text::toLower(Util::getFileExt(path)) != ".dcico") throw Exception(_("Invalid icon package extension"));
	const auto archiveSize = File::getSize(path);
	if(archiveSize <= 0 || archiveSize > MAX_ICON_ARCHIVE_SIZE) throw Exception(_("Invalid icon package size"));

	const auto directory = PackageCache::extract(path, "IconPacks", { MAX_ARCHIVE_ENTRIES, MAX_ICON_FILE_SIZE, MAX_EXTRACTED_SIZE });
	const auto manifest = directory + "info.xml";
	const auto manifestSize = File::getSize(manifest);
	if(manifestSize <= 0 || manifestSize > static_cast<int64_t>(MAX_MANIFEST_SIZE)) throw Exception(_("Invalid icon package"));
	SimpleXML xml;
	xml.fromXML(File(manifest, File::READ, File::OPEN).read());
	if(!xml.findChild("dcico")) throw Exception(_("Invalid icon package"));
	xml.stepIn();
	if(!xml.findChild("Name") || xml.getChildData().empty() || xml.getChildData().size() > MAX_METADATA_LENGTH) throw Exception(_("Invalid icon package"));
	const auto name = xml.getChildData();
	xml.resetCurrentChild();
	if(!xml.findChild("Version") || xml.getChildData().size() > MAX_METADATA_LENGTH || Util::toDouble(xml.getChildData()) <= 0) throw Exception(_("Invalid icon package"));
	const auto version = xml.getChildData();
	xml.resetCurrentChild();
	string scheme = "any";
	if(xml.findChild("Scheme") && !xml.getChildData().empty()) {
		if(xml.getChildData().size() > MAX_METADATA_LENGTH) throw Exception(_("Invalid icon package"));
		scheme = Text::toLower(xml.getChildData());
	}
	xml.resetCurrentChild();
	if(!xml.findChild("Icons")) throw Exception(_("Invalid icon package"));
	xml.stepIn();

	std::unordered_set<string> names;
	size_t iconCount = 0;
	while(xml.findChild("Icon")) {
		auto nameValue = xml.getChildAttrib("Name");
		auto fileValue = xml.getChildAttrib("File");
		if(nameValue.empty() || nameValue.size() > MAX_METADATA_LENGTH || fileValue.size() > MAX_ARCHIVE_PATH_LENGTH ||
			!safeRelativePath(fileValue) || Text::toLower(Util::getFileExt(fileValue)) != ".ico") continue;
		const auto normalizedName = Text::toLower(nameValue);
		if(!names.insert(normalizedName).second) throw Exception(_("Duplicate icon package entry"));
		if(++iconCount > MAX_PACKAGE_ICONS) throw Exception(_("Too many icon package entries"));
		if(entries) (*entries)[normalizedName] = fileValue;
	}
	if(!iconCount) throw Exception(_("Icon package contains no usable icons"));
	if(folder) *folder = directory;
	return { name, version, scheme, path, iconCount };
}

vector<IconManager::Package> discoverPackages() {
	vector<IconManager::Package> packages;
	auto loadDirectory = [&packages](const string& directory) {
		for(const auto& path: File::findFiles(directory, "*.dcico")) {
			try {
				auto package = readManifest(path, nullptr);
				auto existing = std::find_if(packages.begin(), packages.end(), [&path](const auto& item) {
					return Util::stricmp(Util::getFileName(item.path), Util::getFileName(path)) == 0;
				});
				if(existing == packages.end()) packages.push_back(std::move(package));
				else *existing = std::move(package);
			} catch(const Exception&) {
			}
		}
	};

	const auto applicationDirectory = packDirectory(Util::PATH_GLOBAL_CONFIG);
	const auto userDirectory = packDirectory(Util::PATH_USER_CONFIG);
	File::ensureDirectory(userDirectory);
	loadDirectory(applicationDirectory);
	if(Util::stricmp(applicationDirectory, userDirectory) != 0) loadDirectory(userDirectory);
	std::sort(packages.begin(), packages.end(), [](const auto& lhs, const auto& rhs) {
		const auto nameOrder = Util::stricmp(lhs.name, rhs.name);
		return nameOrder == 0 ? Util::stricmp(lhs.path, rhs.path) < 0 : nameOrder < 0;
	});
	return packages;
}

string resolvePack(const string& configured, bool darkMode) {
	if(configured == IconManager::EMBEDDED_PACK) return string();
	if(!configured.empty() && File::getSize(configured) > 0) return configured;
	const auto wanted = configured.empty() ? (darkMode ? DARK_PACK_FILE : "") : Util::getFileName(configured);
	if(wanted.empty()) return string();
	// As discoverPackages finds it, a valid copy in the user's folder before the bundled one, but
	// without reading (and so extracting) every other package.
	for(const auto& directory: { packDirectory(Util::PATH_USER_CONFIG), packDirectory(Util::PATH_GLOBAL_CONFIG) }) {
		const auto path = directory + wanted;
		if(File::getSize(path) <= 0) continue;
		try {
			readManifest(path, nullptr);
			return path;
		} catch(const Exception&) {
		}
	}
	return string();
}

void resolveActivePack(State& current) {
	if(current.packResolved) return;
	current.packResolved = true;
	current.activePack = resolvePack(SETTING(ICON_PACK), current.darkMode);
	current.entries.clear();
	current.activeFolder.clear();
	if(current.activePack.empty()) return;
	try {
		current.package = readManifest(current.activePack, &current.entries, &current.activeFolder);
	} catch(const Exception&) {
		current.activePack.clear();
		current.activeFolder.clear();
		current.entries.clear();
	}
}

dwt::IconPtr loadPackIcon(const string& folder, const string& entry, long size) {
	try {
		return new dwt::Icon(Text::toT(folder + nativePath(entry)), dwt::Point(size, size));
	} catch(const dwt::DWTException&) {
		return dwt::IconPtr();
	}
}

}

void IconManager::initialize() noexcept {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	current.initialized = true;
	current.cache.clear();
	current.entries.clear();
	current.activePack.clear();
	current.activeFolder.clear();
	current.packResolved = false;
}

void IconManager::setDarkMode(bool enabled) noexcept {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	if(current.darkMode != enabled) {
		current.darkMode = enabled;
		current.cache.clear();
		current.entries.clear();
		current.activePack.clear();
		current.activeFolder.clear();
		current.packResolved = false;
	}
}

void IconManager::reload() noexcept {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	current.cache.clear();
	current.entries.clear();
	current.activePack.clear();
	current.activeFolder.clear();
	current.packResolved = false;
}

dwt::IconPtr IconManager::load(unsigned resourceId, long size) {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	// SplashWindow loads the application icon before startup() constructs SettingsManager.
	// Do not consult package settings or cache that fallback: once initialize() runs, the
	// same resource may resolve to the user's selected package.
	if(!current.initialized) return new dwt::Icon(resourceId, dwt::Point(size, size));
	const auto key = cacheKey(resourceId, size);
	const auto cached = current.cache.find(key);
	if(cached != current.cache.end()) return cached->second.icon;

	resolveActivePack(current);
	dwt::IconPtr icon;
	const auto fileName = getFileName(resourceId);
	if(!current.activePack.empty() && fileName) {
		const auto entry = current.entries.find(Text::toLower(fileName));
		if(entry != current.entries.end()) icon = loadPackIcon(current.activeFolder, entry->second, size);
	}
	const bool fromPackage = !!icon;
	if(!icon) icon = new dwt::Icon(resourceId, dwt::Point(size, size));
	if(current.cache.size() < (std::size(ICONS) + CountryFlags::count()) * 8)
		current.cache.emplace(key, State::CachedIcon { icon, fromPackage });
	return icon;
}

IconManager::RuntimeInfo IconManager::getRuntimeInfo() {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	RuntimeInfo info {};
	info.initialized = current.initialized;
	info.packResolved = current.packResolved;
	if(!current.activePack.empty()) info.package = current.package;
	for(const auto& item: current.cache) {
		if(item.second.fromPackage) ++info.cachedPackageIcons;
		else ++info.cachedEmbeddedIcons;
	}
	return info;
}

const char* IconManager::getFileName(unsigned resourceId) noexcept {
	for(const auto& icon: ICONS) {
		if(icon.id == resourceId) return icon.fileName;
	}
	return CountryFlags::fileName(resourceId);
}

IconManager::Package IconManager::inspectPackage(const string& path) {
	return readManifest(path, nullptr);
}

vector<unsigned> IconManager::getIconIds() {
	vector<unsigned> ids;
	ids.reserve(std::size(ICONS));
	for(const auto& icon: ICONS) {
		ids.push_back(icon.id);
	}
	return ids;
}

string IconManager::resolvePackage(const string& setting) {
	bool darkMode = false;
	{
		auto& current = state();
		std::lock_guard<std::mutex> lock(current.mutex);
		darkMode = current.darkMode;
	}
	// Outside the lock: finding the automatic package lists the package folders.
	return resolvePack(setting, darkMode);
}

namespace {

/** An icon from the bytes of an .ico file: the image closest to `size` (the smallest larger one,
 * or else the largest), with the most colors. Null when the file isn't a valid icon. */
dwt::IconPtr iconFromIco(const string& data, long size) {
	auto byte = [&data](size_t i) { return static_cast<uint8_t>(data[i]); };
	auto word = [&](size_t i) { return static_cast<uint32_t>(byte(i) | byte(i + 1) << 8); };
	auto dword = [&](size_t i) { return word(i) | word(i + 2) << 16; };
	if(data.size() < 6 || word(0) != 0 || word(2) != 1) {
		return dwt::IconPtr();
	}
	const auto count = word(4);
	if(data.size() < 6 + 16 * static_cast<size_t>(count)) {
		return dwt::IconPtr();
	}

	int best = -1;
	long bestSize = 0;
	uint32_t bestDepth = 0;
	for(uint32_t i = 0; i < count; ++i) {
		const size_t entry = 6 + 16 * i;
		const long width = byte(entry) ? byte(entry) : 256;
		const auto depth = word(entry + 6);
		const auto bytes = dword(entry + 8);
		const auto offset = dword(entry + 12);
		if(!bytes || offset > data.size() || bytes > data.size() - offset) continue;
		// Closer is better: an exact size, then the smallest larger one, then the largest smaller one.
		const bool better = best < 0 ||
			(width >= size ? (bestSize < size || width < bestSize || (width == bestSize && depth > bestDepth)) :
				(bestSize < size && (width > bestSize || (width == bestSize && depth > bestDepth))));
		if(better) {
			best = static_cast<int>(i);
			bestSize = width;
			bestDepth = depth;
		}
	}
	if(best < 0) {
		return dwt::IconPtr();
	}
	const size_t entry = 6 + 16 * static_cast<size_t>(best);
	auto image = reinterpret_cast<PBYTE>(const_cast<char*>(data.data())) + dword(entry + 12);
	const auto icon = ::CreateIconFromResourceEx(image, dword(entry + 8), TRUE, 0x00030000,
		static_cast<int>(size), static_cast<int>(size), LR_DEFAULTCOLOR);
	return icon ? dwt::IconPtr(new dwt::Icon(icon, true)) : dwt::IconPtr();
}

}

IconManager::Preview::Preview(const string& package) {
	if(package.empty() || package == EMBEDDED_PACK) {
		return;
	}
	try {
		readManifest(package, &entries, &folder);
		path = package;
	} catch(const Exception&) {
		entries.clear();
		folder.clear();
	}
}

IconManager::Preview::~Preview() {
}

dwt::IconPtr IconManager::Preview::load(unsigned resourceId, long size) {
	const auto fileName = getFileName(resourceId);
	if(!folder.empty() && fileName) {
		const auto entry = entries.find(Text::toLower(fileName));
		if(entry != entries.end()) {
			try {
				if(auto icon = iconFromIco(File(folder + nativePath(entry->second), File::READ, File::OPEN).read(), size)) {
					return icon;
				}
			} catch(const Exception&) {
			}
		}
	}
	return new dwt::Icon(resourceId, dwt::Point(size, size));
}

vector<IconManager::Package> IconManager::getPackages() {
	// Packages removed or replaced since they were last listed leave their extracted copies.
	PackageCache::clean("IconPacks");
	return discoverPackages();
}

string IconManager::getDirectory() {
	const auto directory = packDirectory(Util::PATH_USER_CONFIG);
	File::ensureDirectory(directory);
	return directory;
}

dwt::IconPtr IconManager::toolbarIcon(unsigned id) {
	return load(id, SETTING(TOOLBAR_SIZE));
}
