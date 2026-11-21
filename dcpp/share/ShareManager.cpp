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
#include <dcpp/share/ShareManager.h>
#include <dcpp/transfer/private/PrivateTransferStorage.h>

#include <dcpp/hub/AdcHub.h>
#include <dcpp/core/io/compress/BZUtils.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/core/crypto/CryptoManager.h>
#include <dcpp/transfer/download/Download.h>
#include <dcpp/core/io/File.h>
#include <dcpp/core/io/stream/FilteredFile.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/hash/value/HashBloom.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/core/classes/ScopedFunctor.h>
#include <dcpp/search/SearchResult.h>
#include <dcpp/core/io/xml/SimpleXML.h>
#include <dcpp/core/io/db/SQLiteDB.h>
#include <dcpp/util/text/StringTokenizer.h>
#include <dcpp/transfer/Transfer.h>
#include <dcpp/connection/UserConnection.h>
#include <dcpp/core/version.h>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#endif

#include <limits>
#include <memory>

// define this to 1 to measure the time taken by searches to complete.
#ifndef DCPP_TIME_SEARCHES
#define DCPP_TIME_SEARCHES 0
#endif

namespace dcpp {

using std::numeric_limits;

namespace {
const int SHARE_CACHE_SCHEMA_VERSION = 3;
const size_t MAX_SHARE_CACHE_NAME = 4096;
// Windows extended paths are limited in UTF-16 code units, while cached paths
// are UTF-8. Allow the worst-case UTF-8 representation without dropping an
// otherwise valid long path from the share cache.
const size_t MAX_SHARE_CACHE_PATH = 32768 * 4;

// The fingerprint is not a security boundary; it is a compact invalidation key
// for share roots and settings that change what may be advertised or uploaded.
void fnvAppend(uint64_t& hash, const string& value) noexcept {
	for(auto c: value) {
		hash ^= static_cast<uint8_t>(c);
		hash *= 1099511628211ULL;
	}
	hash ^= 0xff;
	hash *= 1099511628211ULL;
}

void fnvAppend(uint64_t& hash, int64_t value) noexcept {
	fnvAppend(hash, Util::toString(value));
}

bool hasPathSeparator(const string& value) noexcept {
	return value.find('\\') != string::npos || value.find('/') != string::npos;
}

bool isValidCachedName(const string& name) noexcept {
	return !name.empty() && name.size() <= MAX_SHARE_CACHE_NAME && name != "." && name != ".." && !hasPathSeparator(name);
}

bool isValidCachedPath(const string& path) noexcept {
	return !path.empty() && path.size() <= MAX_SHARE_CACHE_PATH;
}

bool isUnderSharedRoot(const string& path, const map<string, string>& shares) noexcept {
	for(const auto& share: shares) {
		if(Util::strnicmp(path, share.first, share.first.size()) == 0) {
			return true;
		}
	}
	return false;
}

uint64_t countRows(SQLiteDB& db, const char* sql) {
	auto stmt = db.prepare(sql);
	return stmt.step() ? static_cast<uint64_t>(stmt.columnInt64(0)) : 0;
}

string getMetadata(SQLiteDB& db, const string& key) {
	auto stmt = db.prepare("SELECT value FROM metadata WHERE key=?1");
	stmt.bind(1, key);
	return stmt.step() ? stmt.columnText(0) : string();
}
}

std::atomic_flag ShareManager::refreshing = ATOMIC_FLAG_INIT;
std::atomic<bool> ShareManager::refreshActive { false };

ShareManager::ShareManager() : hits(0), refreshDirs(false), update(false), listN(0),
	lastFullUpdate(GET_TICK()), bloom(1<<20)
{
	SettingsManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);
}

ShareManager::~ShareManager() {
	// A scan of a large network share can take hours; don't make shutdown wait for it.
	stopping = true;
	SettingsManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	HashManager::getInstance()->removeListener(this);
	{
		Lock l(chatAttachmentCs);
		pendingChatAttachments.clear();
		activeChatAttachments.clear();
		cancelledChatAttachments.clear();
	}

	join();

	for(auto& list: fileLists) {
		list.second.ref.reset();
		if(!list.second.path.empty()) {
			staleListFiles.push_back(list.second.path);
		}
	}
	fileLists.clear();
	removeStaleLists();
}

ShareManager::Directory::Directory(const string& aName, const ShareManager::Directory::Ptr& aParent, time_t aLastWrite) :
	size(0),
	name(aName),
	parent(aParent.get()),
	lastWrite(aLastWrite)
{
}

const string& ShareManager::Directory::getRealName() const noexcept {
	return realName ? *realName : name;
}

string ShareManager::Directory::getADCPath() const noexcept {
	if(!getParent())
		return '/' + name + '/';
	return getParent()->getADCPath() + name + '/';
}

string ShareManager::Directory::getFullName() const noexcept {
	if(!getParent())
		return getName() + '\\';
	return getParent()->getFullName() + getName() + '\\';
}

string ShareManager::Directory::getRealPath(const std::string& path) const {
	if(getParent()) {
		return getParent()->getRealPath(getRealName() + PATH_SEPARATOR_STR + path);
	} else {
		return ShareManager::getInstance()->findRealRoot(getRealName(), path);
	}
}

string ShareManager::findRealRoot(const string& virtualRoot, const string& virtualPath) const {
	// A name shared from one folder: the path is that folder's, no need to look on disk (a
	// network round trip per upload request on a NAS). Only merged names are probed.
	const string* single = nullptr;
	size_t matches = 0;
	for(auto& i: shares) {
		if(Util::stricmp(i.second, virtualRoot) == 0) {
			single = &i.first;
			++matches;
		}
	}
	if(matches == 1) {
		return *single + virtualPath;
	}

	for(auto& i: shares) {
		if(Util::stricmp(i.second, virtualRoot) == 0) {
			std::string name = i.first + virtualPath;
			dcdebug("Matching %s\n", name.c_str());
			if(FileFindIter(name) != FileFindIter()) {
				return name;
			}
		}
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

int64_t ShareManager::Directory::getSize() const noexcept {
	int64_t tmp = size;
	for(auto& i: directories)
		tmp += i.second->getSize();
	return tmp;
}

string ShareManager::toVirtual(const TTHValue& tth) const {
	{
		FastLock l(fileListsCs);
		auto list = fileLists.find(Util::emptyString);
		if(list != fileLists.end()) {
			if(list->second.bzRoot && tth == *list->second.bzRoot) {
				return Transfer::USER_LIST_NAME_BZ;
			} else if(list->second.xmlRoot && tth == *list->second.xmlRoot) {
				return Transfer::USER_LIST_NAME;
			}
		}
	}

	RLock l(cs);
	auto i = tthIndex.find(tth);
	if(i != tthIndex.end()) {
		return i->second->getADCPath();
	} else if(findTempShare(tth)) {
		return "TTH/" + tth.toBase32();
	} else {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
}

ShareManager::ShareAccess ShareManager::getShareAccess(const string& hubUrl) const {
	ShareAccess access;
	if(hubUrl.empty()) {
		return access;
	}

	if(!FavoriteManager::getInstance()->getHubShareDirectories(hubUrl, access.directories)) {
		access.unrestricted = true;
	}
	return access;
}

bool ShareManager::hasCustomShare(const string& hubUrl) const {
	std::set<string> directories;
	return !hubUrl.empty() && FavoriteManager::getInstance()->getHubShareDirectories(hubUrl, directories);
}

bool ShareManager::isVirtualAllowed(const string& virtualName, const ShareAccess& access) const {
	if(access.unrestricted) {
		return true;
	}

	bool found = false;
	for(const auto& share: shares) {
		if(Util::stricmp(share.second, virtualName) != 0) {
			continue;
		}

		found = true;
		auto allowed = find_if(access.directories.begin(), access.directories.end(), [&share](const string& path) {
			return Util::stricmp(path, share.first) == 0;
		});
		if(allowed == access.directories.end()) {
			return false;
		}
	}
	return found;
}

bool ShareManager::isFileAllowed(const Directory::File& file, const ShareAccess& access) const {
	auto directory = file.getParent();
	while(directory->getParent()) {
		directory = directory->getParent();
	}
	return isVirtualAllowed(directory->getName(), access);
}

string ShareManager::toVirtual(const TTHValue& tth, const string& hubUrl, const CID& user) const {
	auto access = getShareAccess(hubUrl);
	RLock l(cs);
	auto i = tthIndex.find(tth);
	if(i != tthIndex.end() && isFileAllowed(*i->second, access)) {
		return i->second->getADCPath();
	}
	if(findTempShare(tth, &hubUrl, nullptr, user)) return "TTH/" + tth.toBase32();
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

string ShareManager::toReal(const string& virtualFile) {
	return toRealWithSize(virtualFile).first;
}

string ShareManager::toReal(const string& virtualFile, const string& hubUrl, const CID& user) {
	return toRealWithSize(virtualFile, hubUrl, user).first;
}

void ShareManager::checkNotMarked(const string& realPath) {
	// A shared file that gained the Private Send marker after it was scanned (the marker changes
	// its modification time, so the next scan drops its hash) is never served meanwhile. One
	// stream open per upload request, outside the share lock.
	if(pft::hasPrivateMarker(realPath)) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
}

pair<string, int64_t> ShareManager::toRealWithSize(const string& virtualFile) {
	if(virtualFile == "MyList.DcLst") {
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");
	}
	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		return make_pair(getList(ShareAccess { true, { } }).path, 0);
	}

	auto result = [&] {
		RLock l(cs);
		if(virtualFile.compare(0, 4, "TTH/") == 0) {
			const TTHValue tth(virtualFile.substr(4));
			auto i = tthIndex.find(tth);
			if(i != tthIndex.end() && !pft::isPrivateLocation(i->second->getRealPath()))
				return make_pair(i->second->getRealPath(), i->second->getSize());
			if(const auto temp = findTempShare(tth))
				if(!pft::isPrivateLocation(temp->realPath))
					return make_pair(temp->realPath, temp->size);
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		auto& f = findFile(virtualFile);
		if(pft::isPrivateLocation(f.getRealPath()))
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		return make_pair(f.getRealPath(), f.getSize());
	}();
	checkNotMarked(result.first);
	return result;
}

pair<string, int64_t> ShareManager::toRealWithSize(const string& virtualFile, const string& hubUrl, const CID& user) {
	auto info = resolveFile(virtualFile, hubUrl, user);
	return make_pair(std::move(info.realPath), info.size);
}

ShareManager::ResolvedFileInfo ShareManager::resolveFile(const string& virtualFile, const string& hubUrl, const CID& user) {
	auto info = resolveFileLocked(virtualFile, hubUrl, user);
	checkNotMarked(info.realPath);
	return info;
}

ShareManager::ResolvedFileInfo ShareManager::resolveFileLocked(const string& virtualFile, const string& hubUrl, const CID& user) {
	auto access = getShareAccess(hubUrl);
	RLock l(cs);

	if(virtualFile == "MyList.DcLst" || virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		const TTHValue tth(virtualFile.substr(4));
		auto i = tthIndex.find(tth);
		if(i != tthIndex.end() && isFileAllowed(*i->second, access) && !pft::isPrivateLocation(i->second->getRealPath())) {
			return { i->second->getRealPath(), i->second->getSize(), tth, false, 0, false };
		}
		if(const auto temp = findTempShare(tth, &hubUrl, nullptr, user)) {
			if(pft::isPrivateLocation(temp->realPath))
				throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
			return { temp->realPath, temp->size, temp->tth, true, temp->timestamp, temp->exactOnly };
		}
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	auto& f = findFile(virtualFile);
	if(!isFileAllowed(f, access) || pft::isPrivateLocation(f.getRealPath()))
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	return { f.getRealPath(), f.getSize(), f.tth, false, 0, false };
}

StringList ShareManager::getRealPaths(const string& virtualPath) {
	if(virtualPath.empty())
		throw ShareException("empty virtual path");

	if(*(virtualPath.end() - 1) != '/') {
		// A file: resolved (and checked for the Private Send marker) without the share lock held,
		// as a file list name builds the list, which must never happen under the share lock.
		return { toReal(virtualPath) };
	}

	StringList ret;
	StringList roots;
	{
		RLock l(cs);
		Directory::Ptr d = splitVirtual(virtualPath).first;

		// imitate Directory::getRealPath
		if(d->getParent()) {
			ret.push_back(d->getParent()->getRealPath(d->getRealName()));
		} else {
			roots = getRealRoots(d->getName());
		}
	}

	// Which of the folders shared under the name are there, checked without the share lock held.
	for(const auto& root: roots) {
		// remove the trailing path sep
		if(FileFindIter(root.substr(0, root.size() - 1)) != FileFindIter()) {
			ret.push_back(root);
		}
	}
	return ret;
}

StringList ShareManager::getRealPaths(const TTHValue& tth, const string& hubUrl, const CID& user) const {
	StringList ret;
	auto access = getShareAccess(hubUrl);

	{
		RLock l(cs);
		// During refresh recovery the original cached path may no longer exist, but
		// the same content can still be shared from another directory. Walk the live
		// tree and keep hub-specific share rules intact before UploadManager retries.
		function<void (const Directory&)> collect = [&](const Directory& dir) {
			for(const auto& file: dir.files) {
				if(file.tth && *file.tth == tth && isFileAllowed(file, access) && !pft::isPrivateLocation(file.getRealPath())) {
					ret.push_back(file.getRealPath());
				}
			}

			for(const auto& child: dir.directories) {
				collect(*child.second);
			}
		};

		for(const auto& dir: directories) {
			collect(*dir.second);
		}
		for(const auto& temp: tempShares) {
			if(tempShareExpired(temp, GET_TICK()))
				continue;
			if(pft::isPrivateLocation(temp.realPath))
				continue;
			if(!temp.exactOnly && !SETTING(ENABLE_RTF_TEMP_SHARES))
				continue;
			if(temp.tth == tth && Util::stricmp(temp.hubUrl, hubUrl) == 0 && tempShareVisibleTo(temp, user) &&
				std::none_of(ret.begin(), ret.end(), [&temp](const string& path) {
					return Util::stricmp(path, temp.realPath) == 0;
				}))
			{
				ret.push_back(temp.realPath);
			}
		}

	}

	ret.erase(std::remove_if(ret.begin(), ret.end(), [](const string& path) { return pft::hasPrivateMarker(path); }), ret.end());
	return ret;
}

optional<TTHValue> ShareManager::getTTHFromReal(const string& realPath) noexcept {
	RLock l(cs);
	auto f = getFile(realPath);
	if (f) {
		return f->get().tth;
	}
	return nullopt;
}

bool ShareManager::addTempShare(const string& realPath, int64_t size, uint32_t timestamp,
	const TTHValue& tth, const string& hubUrl, const CID& recipient) noexcept
{
	if(!SETTING(ENABLE_RTF_TEMP_SHARES) || realPath.empty() || hubUrl.empty() || size < 0) return false;
	try {
		// Retain a write-denying handle through the table commit so the validated
		// bytes cannot be replaced between the snapshot check and publication.
		File file(realPath, File::READ, File::OPEN);
		if(file.getSize() != size || file.getLastModified() != timestamp) return false;
		const auto current = HashManager::getInstance()->getTTH(realPath, size, timestamp);
		if(!current || *current != tth) return false;
		return addValidatedTempShare(realPath, size, timestamp, tth, hubUrl, false, recipient);
	} catch(...) {
		return false;
	}
}

bool ShareManager::addTTHOnlyShare(const string& realPath, int64_t size, uint32_t timestamp,
	const TTHValue& tth, const string& hubUrl) noexcept
{
	if(realPath.empty() || hubUrl.empty() || size < 0) return false;
	try {
		File file(realPath, File::READ, File::OPEN);
		if(file.getSize() != size || file.getLastModified() != timestamp) return false;
		file.close();
		auto current = HashManager::getInstance()->getTTH(realPath, size, timestamp);
		if(!current) {
			if(!HashManager::getInstance()->verifyFileTTH(realPath, size, tth)) return false;
			current = HashManager::getInstance()->getTTH(realPath, size, timestamp);
		}
		if(!current || *current != tth) return false;
		return addValidatedTempShare(realPath, size, timestamp, tth, hubUrl, true);
	} catch(...) {
		return false;
	}
}

bool ShareManager::addValidatedTempShare(const string& realPath, int64_t size, uint32_t timestamp,
	const TTHValue& tth, const string& hubUrl, bool exactOnly, const CID& recipient) noexcept
{
	if((!exactOnly && !SETTING(ENABLE_RTF_TEMP_SHARES)) || realPath.empty() || hubUrl.empty() || size < 0) return false;
	try {
		if(pft::isPrivatePath(realPath))
			return false;
	} catch(...) {
		return false;
	}
	WLock l(cs);
	// Protocol documents are for the whole hub; only chat attachments have a recipient.
	const auto scope = exactOnly ? CID() : recipient;
	auto existing = std::find_if(tempShares.begin(), tempShares.end(), [&](const TempShareInfo& item) {
		return item.tth == tth && item.exactOnly == exactOnly && Util::stricmp(item.hubUrl, hubUrl) == 0 &&
			item.recipient == scope;
	});
	if(existing != tempShares.end()) {
		// Reuse is an access: move the entry to the newest end so it isn't the next
		// item evicted immediately after being inserted into another draft.
		tempShares.erase(existing);
		tempShares.push_back({ realPath, hubUrl, tth, size, timestamp, exactOnly, GET_TICK(), scope });
	} else {
		const auto maxTempShares = exactOnly ? size_t(1024) :
			static_cast<size_t>(std::max(1, SETTING(RTF_TEMP_SHARE_LIMIT)));
		auto count = static_cast<size_t>(std::count_if(tempShares.begin(), tempShares.end(),
			[exactOnly](const TempShareInfo& item) { return item.exactOnly == exactOnly; }));
		while(count >= maxTempShares) {
			auto oldest = std::find_if(tempShares.begin(), tempShares.end(),
				[exactOnly](const TempShareInfo& item) { return item.exactOnly == exactOnly; });
			if(oldest == tempShares.end()) break;
			tempShares.erase(oldest);
			--count;
		}
		tempShares.push_back({ realPath, hubUrl, tth, size, timestamp, exactOnly, GET_TICK(), scope });
	}
	return true;
}

bool ShareManager::isTempShare(const TTHValue& tth, const string& realPath,
	const string& hubUrl, const CID& recipient) const noexcept
{
	RLock l(cs);
	return std::any_of(tempShares.begin(), tempShares.end(), [&](const TempShareInfo& item) {
		return !item.exactOnly && !tempShareExpired(item, GET_TICK()) && item.tth == tth && Util::stricmp(item.realPath, realPath) == 0 &&
			Util::stricmp(item.hubUrl, hubUrl) == 0 && item.recipient == recipient;
	});
}

vector<ShareManager::TempShareInfo> ShareManager::getTempShares() const {
	RLock l(cs);
	vector<TempShareInfo> result;
	for(const auto& item : tempShares)
		if(!item.exactOnly && !tempShareExpired(item, GET_TICK()))
			result.push_back(item);
	return result;
}

bool ShareManager::removeTempShare(const string& realPath, const TTHValue& tth,
	const string& hubUrl, const CID& recipient) noexcept
{
	WLock l(cs);
	const auto oldSize = tempShares.size();
	tempShares.erase(std::remove_if(tempShares.begin(), tempShares.end(), [&](const TempShareInfo& item) {
		return !item.exactOnly && item.tth == tth && Util::stricmp(item.realPath, realPath) == 0 &&
			Util::stricmp(item.hubUrl, hubUrl) == 0 && item.recipient == recipient;
	}), tempShares.end());
	return tempShares.size() != oldSize;
}

bool ShareManager::removeTTHOnlyShare(const string& realPath, const TTHValue& tth,
	const string& hubUrl) noexcept
{
	WLock l(cs);
	const auto oldSize = tempShares.size();
	tempShares.erase(std::remove_if(tempShares.begin(), tempShares.end(), [&](const TempShareInfo& item) {
		return item.exactOnly && item.tth == tth && Util::stricmp(item.realPath, realPath) == 0 &&
			Util::stricmp(item.hubUrl, hubUrl) == 0;
	}), tempShares.end());
	return tempShares.size() != oldSize;
}

size_t ShareManager::clearTempShares() noexcept {
	vector<PendingChatAttachment> cancelled;
	size_t count = 0;
	{
		Lock l(chatAttachmentCs);
		++chatAttachmentGeneration;
		for(auto& request: pendingChatAttachments) {
			activeChatAttachments.insert(request.first);
			cancelled.push_back(std::move(request.second));
		}
		pendingChatAttachments.clear();

		// Keep the generation change and clearing atomic with attachment insertion.
		// A finisher either adds before this lock and is cleared here, or observes the
		// new generation and cannot add after Clear has completed.
		WLock shareLock(cs);
		count = static_cast<size_t>(std::count_if(tempShares.begin(), tempShares.end(),
			[](const TempShareInfo& item) { return !item.exactOnly; }));
		tempShares.erase(std::remove_if(tempShares.begin(), tempShares.end(),
			[](const TempShareInfo& item) { return !item.exactOnly; }), tempShares.end());
	}
	for(auto& request: cancelled) {
		deliverChatAttachmentCallback(std::move(request), nullopt,
			_("Attachment preparation was cancelled when temporary shares were cleared"));
	}
	return count;
}

uint64_t ShareManager::prepareChatAttachment(const string& realPath, const string& hubUrl,
	ChatAttachmentCallback callback) noexcept
{
	return prepareChatAttachment(realPath, hubUrl, CID(), std::move(callback));
}

uint64_t ShareManager::prepareChatAttachment(const string& realPath, const string& hubUrl,
	const CID& recipient, ChatAttachmentCallback callback) noexcept
{
	auto requestId = nextChatAttachmentRequest.fetch_add(1);
	if(requestId == 0) requestId = nextChatAttachmentRequest.fetch_add(1);
	uint64_t generation;
	{
		Lock l(chatAttachmentCs);
		generation = chatAttachmentGeneration;
	}
	PendingChatAttachment request { requestId, realPath, hubUrl, -1, 0, generation, 0, std::move(callback), recipient };

	if(!request.callback) return requestId;
	if(realPath.empty() || hubUrl.empty()) {
		deliverChatAttachmentCallback(std::move(request), nullopt, _("The attachment path or chat route is empty"));
		return requestId;
	}

	try {
		File file(realPath, File::READ, File::OPEN | File::SHARED);
		request.size = file.getSize();
		request.timestamp = file.getLastModified();
		file.close();
		if(request.size < 0 || request.timestamp == 0 || Util::getFileName(realPath).empty()) {
			deliverChatAttachmentCallback(std::move(request), nullopt, _("The selected attachment is not a readable file"));
			return requestId;
		}
	} catch(const Exception& e) {
		deliverChatAttachmentCallback(std::move(request), nullopt, e.getError());
		return requestId;
	} catch(...) {
		deliverChatAttachmentCallback(std::move(request), nullopt, _("The selected attachment could not be read"));
		return requestId;
	}

	if(!SETTING(ENABLE_RTF_TEMP_SHARES)) {
		bool inNormalShare = false;
		{
			RLock l(cs);
			inNormalShare = static_cast<bool>(getFile(realPath));
		}
		if(!inNormalShare) {
			deliverChatAttachmentCallback(std::move(request), nullopt,
				_("Temporary attachment sharing is disabled and the selected file is not shared"));
			return requestId;
		}
	}

	const auto snapshotSize = request.size;
	const auto snapshotTimestamp = request.timestamp;
	bool cleared = false;
	optional<PendingChatAttachment> completed;
	optional<TTHValue> exactTth;
	{
		Lock l(chatAttachmentCs);
		if(request.generation != chatAttachmentGeneration) {
			cleared = true;
		} else {
			auto inserted = pendingChatAttachments.emplace(requestId, std::move(request));
			auto hashRequest = HashManager::getInstance()->requestTTH(
				realPath, snapshotSize, snapshotTimestamp);
			if(hashRequest.tth) {
				activeChatAttachments.insert(requestId);
				completed = std::move(inserted.first->second);
				pendingChatAttachments.erase(inserted.first);
				exactTth = std::move(hashRequest.tth);
			} else {
				inserted.first->second.hashJobId = hashRequest.jobId;
			}
		}
	}
	if(cleared) {
		deliverChatAttachmentCallback(std::move(request), nullopt,
			_("Attachment preparation was cancelled when temporary shares were cleared"));
		return requestId;
	}
	if(completed) finishChatAttachment(std::move(*completed), *exactTth);
	return requestId;
}

void ShareManager::cancelChatAttachment(uint64_t requestId) noexcept {
	Lock l(chatAttachmentCs);
	if(pendingChatAttachments.erase(requestId) == 0 && activeChatAttachments.count(requestId)) {
		cancelledChatAttachments.insert(requestId);
	}
}

ShareManager::ChatAttachmentRequestState ShareManager::getChatAttachmentRequestState(
	uint64_t requestId, uint64_t generation) const noexcept
{
	Lock l(chatAttachmentCs);
	if(cancelledChatAttachments.count(requestId)) return ChatAttachmentRequestState::CANCELLED;
	if(generation != chatAttachmentGeneration) return ChatAttachmentRequestState::CLEARED;
	return ChatAttachmentRequestState::READY;
}

ShareManager::ChatAttachmentRequestState ShareManager::beginChatAttachmentCallback(
	uint64_t requestId, uint64_t generation) noexcept
{
	Lock l(chatAttachmentCs);
	auto state = ChatAttachmentRequestState::READY;
	if(cancelledChatAttachments.count(requestId)) {
		state = ChatAttachmentRequestState::CANCELLED;
	} else if(generation != chatAttachmentGeneration) {
		state = ChatAttachmentRequestState::CLEARED;
	}
	activeChatAttachments.erase(requestId);
	cancelledChatAttachments.erase(requestId);
	return state;
}

void ShareManager::deliverChatAttachmentCallback(PendingChatAttachment&& request,
	optional<ChatAttachmentInfo> attachment, string error) noexcept
{
	const auto state = beginChatAttachmentCallback(request.requestId, request.generation);
	if(state == ChatAttachmentRequestState::CANCELLED) return;
	if(state == ChatAttachmentRequestState::CLEARED) {
		attachment = nullopt;
		error = _("Attachment preparation was cancelled when temporary shares were cleared");
	}
	invokeChatAttachmentCallback(std::move(request), std::move(attachment), std::move(error));
}

void ShareManager::invokeChatAttachmentCallback(PendingChatAttachment&& request,
	optional<ChatAttachmentInfo> attachment, string error) noexcept
{
	try {
		request.callback({ request.requestId, std::move(attachment), std::move(error) });
	} catch(...) {
		// Attachment consumers may be UI objects; never unwind them through hashing.
	}
}

void ShareManager::finishChatAttachment(PendingChatAttachment request, const TTHValue& tth) noexcept {
	const auto requestId = request.requestId;
	auto stopIfUnavailable = [this, requestId, &request] {
		const auto state = getChatAttachmentRequestState(requestId, request.generation);
		if(state == ChatAttachmentRequestState::READY) return false;
		deliverChatAttachmentCallback(std::move(request), nullopt, Util::emptyString);
		return true;
	};
	if(stopIfUnavailable()) return;
	try {
		auto access = getShareAccess(request.hubUrl);
		bool permanentlyVisible = false;
		{
			RLock l(cs);
			auto i = tthIndex.find(tth);
			permanentlyVisible = i != tthIndex.end() && i->second->getSize() == request.size &&
				isFileAllowed(*i->second, access);
		}

		if(permanentlyVisible) {
			if(stopIfUnavailable()) return;
			ChatAttachmentInfo attachment { request.realPath, request.hubUrl, tth,
				request.size, request.timestamp, false, request.recipient };
			deliverChatAttachmentCallback(std::move(request), std::move(attachment), Util::emptyString);
			return;
		}

		if(!SETTING(ENABLE_RTF_TEMP_SHARES)) {
			if(stopIfUnavailable()) return;
			deliverChatAttachmentCallback(std::move(request), nullopt,
				_("The selected file is not exposed by the share configured for this chat route"));
			return;
		}

		// File access (including network paths) stays outside chatAttachmentCs so
		// Cancel, route changes, frame teardown and Clear remain responsive. The
		// handle denies writes until the state check and temp-table commit finish.
		File attachmentFile(request.realPath, File::READ, File::OPEN);
		if(attachmentFile.getSize() != request.size ||
			attachmentFile.getLastModified() != request.timestamp)
		{
			if(stopIfUnavailable()) return;
			deliverChatAttachmentCallback(std::move(request), nullopt,
				_("The attachment changed or could not be added to the temporary share"));
			return;
		}
		const auto current = HashManager::getInstance()->getTTH(
			request.realPath, request.size, request.timestamp);
		if(!current || *current != tth) {
			if(stopIfUnavailable()) return;
			deliverChatAttachmentCallback(std::move(request), nullopt,
				_("The attachment changed or could not be added to the temporary share"));
			return;
		}
		bool shared = false;
		bool callbackClaimed = false;
		{
			Lock l(chatAttachmentCs);
			if(!cancelledChatAttachments.count(requestId) &&
				request.generation == chatAttachmentGeneration)
			{
				shared = addValidatedTempShare(
					request.realPath, request.size, request.timestamp, tth, request.hubUrl, false,
					request.recipient);
				if(shared) {
					// Temp-share insertion and callback ownership are one operation. A
					// later cancellation cannot suppress the result while leaving an
					// unowned route share behind.
					activeChatAttachments.erase(requestId);
					cancelledChatAttachments.erase(requestId);
					callbackClaimed = true;
				}
			}
		}
		if(callbackClaimed) {
			ChatAttachmentInfo attachment { request.realPath, request.hubUrl, tth,
				request.size, request.timestamp, true, request.recipient };
			invokeChatAttachmentCallback(std::move(request), std::move(attachment), Util::emptyString);
			return;
		}
		if(stopIfUnavailable()) return;
		if(!shared) {
			deliverChatAttachmentCallback(std::move(request), nullopt,
				_("The attachment changed or could not be added to the temporary share"));
			return;
		}
	} catch(const Exception& e) {
		if(stopIfUnavailable()) return;
		deliverChatAttachmentCallback(std::move(request), nullopt, e.getError());
	} catch(...) {
		if(stopIfUnavailable()) return;
		deliverChatAttachmentCallback(std::move(request), nullopt, _("Unable to prepare the attachment"));
	}
}

void ShareManager::completeChatAttachments(const string& realPath, const TTHValue& tth,
	int64_t size, uint32_t timestamp, uint64_t hashJobId) noexcept
{
	vector<PendingChatAttachment> completed;
	{
		Lock l(chatAttachmentCs);
		for(auto i = pendingChatAttachments.begin(); i != pendingChatAttachments.end();) {
			if(Util::stricmp(i->second.realPath, realPath) == 0 && i->second.size == size &&
				i->second.timestamp == timestamp && i->second.hashJobId == hashJobId)
			{
				activeChatAttachments.insert(i->first);
				completed.push_back(std::move(i->second));
				i = pendingChatAttachments.erase(i);
			} else {
				++i;
			}
		}
	}
	for(auto& request: completed) finishChatAttachment(std::move(request), tth);
}

void ShareManager::failChatAttachments(const string& realPath, int64_t size, uint32_t timestamp,
	uint64_t hashJobId, const string& error) noexcept
{
	vector<PendingChatAttachment> failed;
	{
		Lock l(chatAttachmentCs);
		for(auto i = pendingChatAttachments.begin(); i != pendingChatAttachments.end();) {
			if(Util::stricmp(i->second.realPath, realPath) == 0 && i->second.size == size &&
				i->second.timestamp == timestamp && i->second.hashJobId == hashJobId)
			{
				activeChatAttachments.insert(i->first);
				failed.push_back(std::move(i->second));
				i = pendingChatAttachments.erase(i);
			} else {
				++i;
			}
		}
	}
	for(auto& request: failed) {
		deliverChatAttachmentCallback(std::move(request), nullopt,
			error.empty() ? _("Hashing the attachment failed") : error);
	}
}

bool ShareManager::validateChatAttachment(const TTHValue& tth, int64_t size,
	const string& hubUrl, const CID& user) const noexcept
{
	try {
		auto access = getShareAccess(hubUrl);
		optional<TempShareInfo> temporary;
		{
			RLock l(cs);
			auto i = tthIndex.find(tth);
			if(i != tthIndex.end() && i->second->getSize() == size &&
				(hubUrl.empty() || isFileAllowed(*i->second, access))) {
				return true;
			}
			if(!hubUrl.empty()) {
				const bool chatOnly = false;
				if(const auto item = findTempShare(tth, &hubUrl, &chatOnly, user); item && item->size == size) temporary = *item;
			}
		}
		if(!temporary) return false;

		File file(temporary->realPath, File::READ, File::OPEN | File::SHARED);
		if(file.getSize() != temporary->size || file.getLastModified() != temporary->timestamp) return false;
		file.close();
		const auto current = HashManager::getInstance()->getTTH(
			temporary->realPath, temporary->size, temporary->timestamp);
		if(!current || *current != tth) return false;

		RLock l(cs);
		const bool chatOnly = false;
		const auto item = findTempShare(tth, &hubUrl, &chatOnly, user);
		return item && item->size == temporary->size && item->timestamp == temporary->timestamp &&
			Util::stricmp(item->realPath, temporary->realPath) == 0 && item->recipient == temporary->recipient;
	} catch(...) {
		return false;
	}
}

optional<TTHValue> ShareManager::getTTH(const string& virtualFile) const {
	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		FastLock l(fileListsCs);
		auto list = fileLists.find(Util::emptyString);
		if(list == fileLists.end()) {
			return nullopt;
		}
		return virtualFile == Transfer::USER_LIST_NAME_BZ ? list->second.bzRoot : list->second.xmlRoot;
	}

	RLock l(cs);
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		const TTHValue tth(virtualFile.substr(4));
		if(tthIndex.find(tth) != tthIndex.end() || findTempShare(tth)) return tth;
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
	return findFile(virtualFile).tth;
}

optional<TTHValue> ShareManager::getTTH(const string& virtualFile, const string& hubUrl, const CID& user) const {
	auto access = getShareAccess(hubUrl);
	RLock l(cs);
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		const TTHValue tth(virtualFile.substr(4));
		auto i = tthIndex.find(tth);
		if((i != tthIndex.end() && isFileAllowed(*i->second, access)) || findTempShare(tth, &hubUrl, nullptr, user)) return tth;
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
	auto& file = findFile(virtualFile);
	if(!isFileAllowed(file, access)) throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	return file.tth;
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile) const {
	TigerTree tree;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		if(!HashManager::getInstance()->getTree(TTHValue(virtualFile.substr(4)), tree))
			return nullptr;
	} else {
		try {
			auto tth = getTTH(virtualFile);
			if(!tth) { return nullptr; }
			HashManager::getInstance()->getTree(*tth, tree);
		} catch(const Exception&) {
			return nullptr;
		}
	}

	ByteVector buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile, const string& hubUrl, const CID& user) const {
	TigerTree tree;
	try {
		auto tth = getTTH(virtualFile, hubUrl, user);
		if(!tth || !HashManager::getInstance()->getTree(*tth, tree)) {
			return nullptr;
		}
	} catch(...) {
		return nullptr;
	}

	ByteVector buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

AdcCommand ShareManager::getListInfo(const string& aFile, const ShareAccess& access) {
	const auto list = getList(access);
	const bool bz = aFile == Transfer::USER_LIST_NAME_BZ;
	const auto& root = bz ? list.bzRoot : list.xmlRoot;
	if(!root) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	AdcCommand cmd(AdcCommand::CMD_RES);
	cmd.addParam("FN", aFile);
	cmd.addParam("SI", Util::toString(bz ? list.bzLen : list.xmlLen));
	cmd.addParam("TR", root->toBase32());
	return cmd;
}

AdcCommand ShareManager::getFileInfo(const string& aFile) {
	if(aFile == Transfer::USER_LIST_NAME || aFile == Transfer::USER_LIST_NAME_BZ) {
		return getListInfo(aFile, ShareAccess { true, { } });
	}

	if(aFile.compare(0, 4, "TTH/") != 0)
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);

	TTHValue val(aFile.substr(4));
	RLock l(cs);
	auto i = tthIndex.find(val);
	if(i == tthIndex.end()) {
		if(const auto temp = findTempShare(val)) {
			AdcCommand cmd(AdcCommand::CMD_RES);
			cmd.addParam("FN", aFile);
			cmd.addParam("SI", Util::toString(temp->size));
			cmd.addParam("TR", val.toBase32());
			return cmd;
		}
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	const Directory::File& f = *i->second;
	AdcCommand cmd(AdcCommand::CMD_RES);
	cmd.addParam("FN", f.getADCPath());
	cmd.addParam("SI", Util::toString(f.getSize()));
	cmd.addParam("TR", f.tth->toBase32());
	return cmd;
}

AdcCommand ShareManager::getFileInfo(const string& aFile, const string& hubUrl, const CID& user) {
	auto access = getShareAccess(hubUrl);
	if(aFile == Transfer::USER_LIST_NAME || aFile == Transfer::USER_LIST_NAME_BZ) {
		return getListInfo(aFile, access);
	}

	if(aFile.compare(0, 4, "TTH/") != 0) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	TTHValue val(aFile.substr(4));
	RLock l(cs);
	auto i = tthIndex.find(val);
	if(i == tthIndex.end() || !isFileAllowed(*i->second, access)) {
		if(const auto temp = findTempShare(val, &hubUrl, nullptr, user)) {
			AdcCommand cmd(AdcCommand::CMD_RES);
			cmd.addParam("FN", aFile);
			cmd.addParam("SI", Util::toString(temp->size));
			cmd.addParam("TR", val.toBase32());
			return cmd;
		}
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	const Directory::File& f = *i->second;
	AdcCommand cmd(AdcCommand::CMD_RES);
	cmd.addParam("FN", f.getADCPath());
	cmd.addParam("SI", Util::toString(f.getSize()));
	cmd.addParam("TR", f.tth->toBase32());
	return cmd;
}

pair<ShareManager::Directory::Ptr, string> ShareManager::splitVirtual(const string& virtualPath) const {
	if(virtualPath.empty() || virtualPath[0] != '/') {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	auto i = virtualPath.find('/', 1);
	if(i == string::npos || i == 1) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	auto dmi = directories.find(virtualPath.substr(1, i - 1));
	if(dmi == directories.end()) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	auto d = dmi->second;

	auto j = i + 1;
	while((i = virtualPath.find('/', j)) != string::npos) {
		auto mi = d->directories.find(virtualPath.substr(j, i - j));
		j = i + 1;
		if(mi == d->directories.end())
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		d = mi->second;
	}

	return make_pair(d, virtualPath.substr(j));
}

const ShareManager::Directory::File& ShareManager::findFile(const string& virtualFile) const {
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		auto i = tthIndex.find(TTHValue(virtualFile.substr(4)));
		if(i == tthIndex.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
		return *i->second;
	}

	auto v = splitVirtual(virtualFile);
	auto it = v.first->findFile(v.second);
	if(it == v.first->files.end())
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	return *it;
}

bool ShareManager::tempShareExpired(const TempShareInfo& item, uint64_t tick) noexcept {
	const auto minutes = SETTING(RTF_TEMP_SHARE_EXPIRATION);
	return !item.exactOnly && minutes > 0 && tick >= item.added && tick - item.added >= static_cast<uint64_t>(minutes) * 60 * 1000;
}

bool ShareManager::tempShareVisibleTo(const TempShareInfo& item, const CID& user) noexcept {
	// An attachment sent in a private chat is for its recipient only; a missing
	// (zero) user, as in searches and hub-less lookups, never matches one.
	return !item.recipient || (user && item.recipient == user);
}

const ShareManager::TempShareInfo* ShareManager::findTempShare(const TTHValue& tth,
	const string* hubUrl, const bool* exactOnly, const CID& user) const noexcept
{
	for(auto i = tempShares.rbegin(); i != tempShares.rend(); ++i) {
		if(tempShareExpired(*i, GET_TICK()))
			continue;
		if(!i->exactOnly && !SETTING(ENABLE_RTF_TEMP_SHARES)) continue;
		if(!tempShareVisibleTo(*i, user)) continue;
		if(i->tth == tth && (!hubUrl || Util::stricmp(i->hubUrl, *hubUrl) == 0) &&
			(!exactOnly || i->exactOnly == *exactOnly)) return &*i;
	}
	return nullptr;
}

string ShareManager::validateVirtual(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

bool ShareManager::hasVirtual(const string& virtualName) const noexcept {
	RLock l(cs);
	return directories.find(virtualName) != directories.end();
}

void ShareManager::load(SimpleXML& aXml) {
	WLock l(cs);

	aXml.resetCurrentChild();
	if(aXml.findChild("Share")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			string realPath = aXml.getChildData();
			if(realPath.empty()) {
				continue;
			}
			// make sure realPath ends with a PATH_SEPARATOR
			if(realPath[realPath.size() - 1] != PATH_SEPARATOR) {
				realPath += PATH_SEPARATOR;
			}

			const string& virtualName = aXml.getChildAttrib("Virtual");
			string vName = validateVirtual(virtualName.empty() ? Util::getLastDir(realPath) : virtualName);
			shares[move(realPath)] = vName;
			if(directories.find(vName) == directories.end()) {
				directories[vName] = Directory::create(vName);
			}
		}
		aXml.stepOut();
	}
}

void ShareManager::save(SimpleXML& aXml) {
	RLock l(cs);

	aXml.addTag("Share");
	aXml.stepIn();
	for(auto& i: shares) {
		aXml.addTag("Directory", i.first);
		aXml.addChildAttrib("Virtual", i.second);
	}
	aXml.stepOut();
}

void ShareManager::addDirectory(const string& realPath, const string& virtualName) {
	if(pft::isPrivatePath(realPath))
		throw ShareException("Private Send storage cannot be shared");
	if(realPath.empty() || virtualName.empty()) {
		throw ShareException(_("No directory specified"));
	}

	if (!checkHidden(realPath)) {
		throw ShareException(_("Directory is hidden"));
	}

	if(Util::stricmp(SETTING(TEMP_DOWNLOAD_DIRECTORY), realPath) == 0) {
		throw ShareException(_("The temporary download directory cannot be shared"));
	}

	string vName = validateVirtual(virtualName);
	{
		WLock l(cs);

		for(auto& i: shares) {
			if(Util::strnicmp(realPath, i.first, i.first.length()) == 0) {
				// Trying to share an already shared directory
				throw ShareException(_("Directory already shared"));
			} else if(Util::strnicmp(realPath, i.first, realPath.length()) == 0) {
				// Trying to share a parent directory
				throw ShareException(_("Remove all subdirectories before adding this one"));
			}
		}

		// Scanning a large folder can take hours. Register it now and let the refresh thread
		// build its tree, so the caller (the settings dialog) returns at once.
		shares[realPath] = move(vName);
		pendingDirectories.push_back(realPath);
		setDirty();
	}

	startPendingScan();
}

bool ShareManager::hasPendingDirectories() const noexcept {
	RLock l(cs);
	return !pendingDirectories.empty() || !scanningDirectory.empty();
}

bool ShareManager::isDirectoryPending(const string& realPath) const noexcept {
	RLock l(cs);
	return scanningDirectory == realPath ||
		std::find(pendingDirectories.begin(), pendingDirectories.end(), realPath) != pendingDirectories.end();
}

void ShareManager::startPendingScan() noexcept {
	// A running refresh scans pending folders before it finishes (see run()).
	if(refreshing.test_and_set()) {
		return;
	}
	refreshActive = true;
	update = true;
	refreshDirs = false;

	join();
	try {
		start();
		setThreadPriority(Thread::LOW);
	} catch(const ThreadException& e) {
		LogManager::getInstance()->message(str(F_("File list refresh failed: %1%") % e.getError()),
			LogMessage::SEV_ERROR, _("Sharing"));
		refreshActive = false;
		refreshing.clear();
	}
}

bool ShareManager::scanPendingDirectories() {
	bool scanned = false;
	const pft::PrivatePathScanner privacy;
	while(!stopping) {
		string realPath, vName;
		{
			WLock l(cs);
			if(pendingDirectories.empty()) {
				break;
			}
			realPath = pendingDirectories.front();
			pendingDirectories.erase(pendingDirectories.begin());
			auto i = shares.find(realPath);
			if(i == shares.end()) {
				continue;
			}
			vName = i->second;
			scanningDirectory = realPath;
		}
		ScopedFunctor([this] { WLock l(cs); scanningDirectory.clear(); });

		updateFilterCache();
		Directory::Ptr dp;
		if(!scanRoot(privacy, realPath, vName, dp)) {
			continue;
		}

		WLock l(cs);
		auto i = shares.find(realPath);
		// Removed, renamed or queued again while it was scanned: that change has its own scan.
		if(i == shares.end() || i->second != vName ||
			std::find(pendingDirectories.begin(), pendingDirectories.end(), realPath) != pendingDirectories.end())
		{
			continue;
		}
		merge(dp, realPath);
		afterTreeChange();
		scanned = true;
	}
	return scanned;
}

void ShareManager::merge(const Directory::Ptr& directory, const string& realPath) {
	auto i = directories.find(directory->getName());
	if(i != directories.end()) {
		dcdebug("Merging directory <%s> into %s\n", realPath.c_str(), directory->getName().c_str());
		// The name was shared from one folder, whose files don't store their path (it follows from
		// the folder); from now on it is merged, so they need it.
		StringList others;
		for(const auto& share: shares) {
			if(share.first != realPath && Util::stricmp(share.second, directory->getName()) == 0) {
				others.push_back(share.first);
			}
		}
		if(others.size() == 1) {
			keepRealPaths(*i->second, others.front());
		}
		i->second->merge(directory, realPath);

	} else {
		dcdebug("Adding new directory %s\n", directory->getName().c_str());
		directories[directory->getName()] = directory;
	}
}

void ShareManager::Directory::merge(const Directory::Ptr& source, const string& realPath) {
	lastWrite = (std::max)(lastWrite, source->getLastWrite());

	// merge directories
	for(auto& i: source->directories) {
		auto subSource = i.second;

		auto ti = directories.find(subSource->getName());
		if(ti == directories.end()) {
			// the directory doesn't exist; create it.
			directories.emplace(subSource->getName(), subSource);
			subSource->parent = this;

			auto f = findFile(subSource->getName());
			if(f != files.end()) {
				// we have a file that has the same name as the dir being merged; rename it.
				const_cast<File&>(*f).validateName(Util::getFilePath(f->getRealPath()), true);
			}

		} else {
			// the directory was already existing; merge into it.
			auto subTarget = ti->second;
			subTarget->merge(subSource, realPath + subSource->getRealName() + PATH_SEPARATOR);
		}
	}

	// merge files
	for(auto& i: source->files) {
		auto& file = const_cast<File&>(i);

		file.setParent(this);
		// Files of a merged name always keep their path: it can't be told from the folders.
		file.validateName(realPath, true);

		files.insert(move(file));
	}
}

bool ShareManager::Directory::nameInUse(const string& name) const {
	return findFile(name) != files.end() || directories.find(name) != directories.end();
}

void ShareManager::keepRealPaths(Directory& dir, const string& dirRealPath) {
	for(auto& f: dir.files) {
		if(!f.realPath) {
			const_cast<Directory::File&>(f).realPath = dirRealPath + f.getName();
		}
	}
	for(auto& child: dir.directories) {
		keepRealPaths(*child.second, dirRealPath + child.second->getRealName() + PATH_SEPARATOR);
	}
}

void ShareManager::Directory::File::validateName(const string& sourcePath, bool keepRealPath) {
	// A path set earlier (by the scan that found the file) stays the authority.
	auto diskPath = realPath ? *realPath : sourcePath + name;
	bool renamed = false;
	if(parent->nameInUse(name)) {
		renamed = true;
		uint32_t num = 0;
		string base = name, ext, vname;
		auto dot = base.rfind('.');
		if(dot != string::npos) {
			ext = base.substr(dot);
			base.erase(dot);
		}
		do {
			++num;
			vname = base + " (" + Util::toString(num) + ")" + ext;
		} while(parent->nameInUse(vname));
		dcdebug("Renaming duplicate <%s> to <%s>\n", name.c_str(), vname.c_str());
		name = move(vname);
	}
	// Otherwise the path follows from the directories (Directory::getRealPath), which saves
	// memory for every file of a large share.
	if(renamed || keepRealPath) {
		realPath = move(diskPath);
	} else {
		realPath.reset();
	}
}

void ShareManager::removeDirectory(const string& realPath) {
	if(realPath.empty())
		return;

	HashManager::getInstance()->stopHashing(realPath);

	bool rescan = false;
	{
		WLock l(cs);

		auto i = shares.find(realPath);
		if(i == shares.end()) {
			return;
		}

		auto vName = i->second;
		directories.erase(vName);

		shares.erase(i);
		pendingDirectories.erase(std::remove(pendingDirectories.begin(), pendingDirectories.end(), realPath), pendingDirectories.end());

		// Other folders shared under the same virtual name were part of the removed tree. Rescan them
		// in the background rather than holding the caller for the whole scan.
		for(i = shares.begin(); i != shares.end(); ++i) {
			if(Util::stricmp(i->second, vName) == 0 &&
				std::find(pendingDirectories.begin(), pendingDirectories.end(), i->first) == pendingDirectories.end())
			{
				pendingDirectories.push_back(i->first);
				rescan = true;
			}
		}

		rebuildIndices();
		setDirty();
	}

	// Not under cs: starting the scan waits for the previous scan thread, which takes cs as it
	// finishes.
	if(rescan) {
		startPendingScan();
	}
}

void ShareManager::renameDirectory(const string& realPath, const string& virtualName) {
	removeDirectory(realPath);
	addDirectory(realPath, virtualName);
}

int64_t ShareManager::getShareSize(const string& realPath) const noexcept {
	RLock l(cs);
 	dcassert(realPath.size()>0);
	auto i = shares.find(realPath);

	if(i != shares.end()) {
		auto j = directories.find(i->second);
		if(j != directories.end()) {
			// Check whether this is a merged share
			int vNames = 0;
			for(auto& s: shares) {
				if(Util::stricmp(s.second, i->second) == 0) { 
					vNames++;
					if (vNames > 1) break;
				}
			}

			if(vNames == 1) {
				// Only one root dir has been found, go simple...
				return j->second->getSize();
			} else {
				// This path is part of multiple merged root dirs, count only what belongs to this
				// folder: the files whose real path is under it. No file system access under the lock.
				int64_t tmp = 0;
				std::function<void (const Directory&)> count = [&](const Directory& dir) {
					for(const auto& f: dir.files) {
						if(f.tth && f.realPath && Util::strnicmp(*f.realPath, realPath, realPath.size()) == 0) {
							tmp += f.getSize();
						}
					}
					for(const auto& d: dir.directories) {
						count(*d.second);
					}
				};
				count(*j->second);
				return tmp;
			}
		}
	}
	return -1;
}

int64_t ShareManager::getShareSize() const noexcept {
	RLock l(cs);
	return sharedBytes;
}

ShareManager::ShareTotals ShareManager::getTotals(const ShareAccess& access) const {
	if(access.unrestricted) {
		return { indexVersion, sharedBytes, tthIndex.size() };
	}
	// Hubs ask for their totals with every info update; a profile's totals are counted again only
	// after the share changed. Called under the shared share lock, so the cache has its own lock.
	FastLock l(totalsCs);
	auto& totals = profileTotals[getProfileKey(access)];
	if(totals.version != indexVersion) {
		totals = { indexVersion, 0, 0 };
		for(const auto& item: tthIndex) {
			if(isFileAllowed(*item.second, access)) {
				totals.size += item.second->getSize();
				++totals.files;
			}
		}
	}
	return totals;
}

int64_t ShareManager::getShareSizeForHub(const string& hubUrl) const {
	auto access = getShareAccess(hubUrl);
	RLock l(cs);
	return getTotals(access).size;
}

size_t ShareManager::getSharedFiles() const noexcept {
	RLock l(cs);
	return tthIndex.size();
}

size_t ShareManager::getSharedFiles(const string& hubUrl) const {
	auto access = getShareAccess(hubUrl);
	RLock l(cs);
	return getTotals(access).files;
}

ShareManager::Directory::Ptr ShareManager::buildTree(const string& realPath, optional<std::reference_wrapper<const string>> dirName,
	const Directory::Ptr& parent, time_t lastWrite)
{
	const pft::PrivatePathScanner privacy;
	return scanTree(privacy, realPath, dirName, parent, lastWrite, false);
}

ShareManager::Directory::Ptr ShareManager::scanTree(const pft::PrivatePathScanner& privacy, const string& realPath,
	optional<std::reference_wrapper<const string>> dirName, const Directory::Ptr& parent, time_t lastWrite, bool keepRealPaths)
{
	if(lastWrite == 0) {
		FileFindIter directoryInfo(realPath.substr(0, realPath.size() - 1));
		if(directoryInfo != FileFindIter()) {
			lastWrite = directoryInfo->getLastWriteTime();
		}
	}

	auto dir = Directory::create(dirName ? dirName->get() : Util::getLastDir(realPath), parent, lastWrite);

	auto lastFileIter = dir->files.begin();

#ifdef _WIN32
	for(FileFindIter i(realPath + "*"), end; i != end; ++i) {
#else
	//the fileiter just searches directorys for now, not sure if more
	//will be needed later
	for(FileFindIter i(realPath), end; i != end; ++i) {
#endif
		if(stopping) {
			break;
		}
		auto name = i->getFileName();

		if(name.empty()) {
			LogManager::getInstance()->message(str(F_("Invalid file name found while hashing folder %1%") % Util::addBrackets(realPath)),
				LogMessage::SEV_WARNING, _("Sharing"));
			continue;
		}

		if(name == "." || name == "..")
			continue;
		if(!SETTING(SHARE_HIDDEN) && i->isHidden())
			continue;
		if(!SETTING(FOLLOW_LINKS) && i->isLink())
 			continue;

		if(i->isDirectory()) {
			auto newRealPath = realPath + name + PATH_SEPARATOR;

			if(privacy.isPrivateDirectory(newRealPath, name, i->isReparsePoint()))
				continue;
			if(!checkInvalidPaths(newRealPath))
				continue;

			// don't share unfinished downloads
			if(newRealPath == SETTING(TEMP_DOWNLOAD_DIRECTORY)) { continue; }

			auto virtualName = name;
			if(dir->nameInUse(virtualName)) {
				uint32_t num = 0;
				do {
					++num;
					virtualName = name + " (" + Util::toString(num) + ")";
				} while(dir->nameInUse(virtualName));
			}

			dir->directories[virtualName] = scanTree(privacy, newRealPath, std::cref(virtualName), dir, i->getLastWriteTime(), keepRealPaths);

			if(virtualName != name) {
				dir->directories[virtualName]->setRealName(move(name));
			}

		} else {
			// Not a directory, assume it's a file...make sure we're not sharing the settings file...
			if(name == "DCPlusPlus.xml" || name == "Favorites.xml") { continue; }

			auto size = i->getSize();
			auto fileName = realPath + name;
			if(privacy.isPrivateFile(fileName, name, i->isReparsePoint()))
				continue;

			if(!checkInvalidFileName(name))
				continue;

			if(!checkInvalidFileSize(size))
				continue;

			// don't share the private key file
			if(fileName == SETTING(TLS_PRIVATE_KEY_FILE)) { continue; }

			const auto fileDate = i->getLastWriteTime();
			auto request = HashManager::getInstance()->requestTTH(fileName, size, fileDate);
			if(request.tth && !request.markerChecked) {
				// Hashed by an older version, before markers were checked at hashing time: check this
				// file once. Every file hashed since was checked when it was hashed.
				if(pft::hasPrivateMarker(fileName))
					continue;
				HashManager::getInstance()->setMarkerChecked(fileName);
			}
			Directory::File f(name, size, dir, request.tth, fileDate);
			f.validateName(realPath, keepRealPaths);
			lastFileIter = dir->files.insert(lastFileIter, move(f));
		}
	}

	return dir;
}

bool ShareManager::checkHidden(const string& realPath) const {
	FileFindIter ff = FileFindIter(realPath.substr(0, realPath.size() - 1));

	if (ff != FileFindIter()) {
		return (SETTING(SHARE_HIDDEN) || !ff->isHidden());
	}

	return true;
}

bool ShareManager::checkInvalidFileName(const string& name) const
{
	for(auto& f: cachedFilterSkiplistRegEx)
	{
		if(checkRegEx(f, name))
		{
			return false;
		}
	}

	for(auto& f: cachedFilterSkiplistFileExtensions)
	{
		if(checkRegEx(f, name))
		{
			dcdebug("Filtering away the file '%s' with the pattern matching '%s'\n", name.c_str(), f.pattern.c_str());

			return false;
		}
	}

	return true;
}

bool ShareManager::checkInvalidPaths(const string& name) const
{
	for(auto& f: cachedFilterSkiplistPaths)
	{
		if(checkRegEx(f, name))
		{
			return false;
		}
	}

	return true;
}

bool ShareManager::checkInvalidFileSize(uint64_t size) const
{
	uint64_t minimumSize = SETTING(SHARING_SKIPLIST_MINSIZE);
	if(minimumSize != 0)
	{
		if( size < minimumSize )
		{
			return false;
		}
	}

	uint64_t maximumSize = SETTING(SHARING_SKIPLIST_MAXSIZE);
	if(maximumSize != 0)
	{
		if( size > maximumSize )
		{
			return false;
		}
	}

	return true;
}

bool ShareManager::checkRegEx(const StringMatch& matcher, const string& match) const
{
	if(match.empty())
	{
		return false;
	}
	
	return matcher.match(match);
}

void ShareManager::updateFilterCache()
{
	updateFilterCache(SETTING(SHARING_SKIPLIST_REGEX), cachedFilterSkiplistRegEx);
	updateFilterCache(SETTING(SHARING_SKIPLIST_EXTENSIONS), "$", true, cachedFilterSkiplistFileExtensions);
	updateFilterCache(SETTING(SHARING_SKIPLIST_PATHS), cachedFilterSkiplistPaths);
}

void ShareManager::updateFilterCache(const std::string& strSetting, std::list<StringMatch>& lst)
{
	updateFilterCache(strSetting, "", false, lst);
}

void ShareManager::updateFilterCache(const std::string& strSetting, const std::string& strExtraPattern, bool escapeDot, std::list<StringMatch>& lst)
{
	lst.clear();

	auto tokens = StringTokenizer<string>(strSetting, ';').getTokens();
	for(auto& pattern: tokens)
	{
		if(pattern.empty())
		{
			continue;
		}

		if(escapeDot)
		{
			Util::replace(".", "\\.", pattern);
		}

		StringMatch matcher;
		matcher.pattern = pattern + strExtraPattern;
		matcher.setMethod(StringMatch::REGEX);
		matcher.prepare();

		lst.push_back(matcher);
	}
}

void ShareManager::updateIndices(Directory& dir) {
	bloom.add(Text::toLower(dir.getName()));

	for(auto& i: dir.directories) {
		updateIndices(*i.second);
	}

	dir.size = 0;

	for(auto i = dir.files.begin(); i != dir.files.end(); ) {
		updateIndices(dir, i++);
	}
}

void ShareManager::rebuildIndices(size_t expectedFiles) {
	tthIndex.clear();
	sharedBytes = 0;
	++indexVersion;
	if(expectedFiles > 0) {
		tthIndex.reserve(expectedFiles);
	}

	// A fixed-size name filter fills up with a large share and then rejects nothing, so every
	// search walks the whole share. About four bits per name fragment keeps it selective.
	size_t fragments = 0;
	std::function<void (const Directory&)> count = [&](const Directory& dir) {
		fragments += decltype(bloom)::positions(dir.getName());
		for(const auto& file: dir.files) {
			if(file.tth) fragments += decltype(bloom)::positions(file.getName());
		}
		for(const auto& child: dir.directories) count(*child.second);
	};
	for(const auto& i: directories) count(*i.second);
	size_t bits = 1 << 20;
	while(bits < fragments * 4 && bits < (static_cast<size_t>(1) << 28)) bits <<= 1;
	bloom.reset(bits);

	for(auto& i: directories) {
		updateIndices(*i.second);
	}
}

void ShareManager::updateIndices(Directory& dir, const decltype(std::declval<Directory>().files.begin())& i) {
	const Directory::File& f = *i;

	if(!f.tth) {
		return;
	}

	auto j = tthIndex.find(*f.tth);
	if(j == tthIndex.end()) {
		dir.size += f.getSize();
		sharedBytes += f.getSize();

	} else {
		if(deferDuplicates) {
			// Indexing one folder of a refresh: the old tree of another folder may hold the other
			// copy, so which copy stays is settled by the full rebuild at the end.
			return;
		}
		if(!SETTING(LIST_DUPES)) {
			try {
				LogManager::getInstance()->message(str(F_("Duplicate file will not be shared: %1% (Size: %2% B) Dupe matched against: %3%")
					% Util::addBrackets(f.getRealPath()) % Util::toString(f.getSize()) % Util::addBrackets(j->second->getRealPath())),
					LogMessage::SEV_WARNING, _("Sharing"));
				dir.files.erase(i);
			} catch (const ShareException&) {
			}
			return;
		}
	}

	tthIndex[*f.tth] = &f;
	bloom.add(Text::toLower(f.getName()));
}

string ShareManager::getShareCacheFile() const {
	return Util::getPath(Util::PATH_USER_CONFIG) + "ShareCache.sqlite3";
}

string ShareManager::getShareCacheFingerprint() const {
	uint64_t hash = 1469598103934665603ULL;
	fnvAppend(hash, string("ShareCache.v3"));

	for(const auto& share: shares) {
		fnvAppend(hash, share.first);
		fnvAppend(hash, share.second);
	}

	fnvAppend(hash, SETTING(SHARE_HIDDEN) ? 1 : 0);
	fnvAppend(hash, SETTING(FOLLOW_LINKS) ? 1 : 0);
	fnvAppend(hash, SETTING(LIST_DUPES) ? 1 : 0);
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_REGEX));
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_EXTENSIONS));
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_PATHS));
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_MINSIZE));
	fnvAppend(hash, SETTING(SHARING_SKIPLIST_MAXSIZE));
	fnvAppend(hash, SETTING(TEMP_DOWNLOAD_DIRECTORY));
	fnvAppend(hash, SETTING(TLS_PRIVATE_KEY_FILE));

	return std::to_string(hash);
}

void ShareManager::createShareCacheSchema(SQLiteDB& db) {
	int version = 0;
	{
		auto versionStmt = db.prepare("PRAGMA user_version");
		version = versionStmt.step() ? versionStmt.columnInt(0) : 0;
	}
	if(version != 0 && version != SHARE_CACHE_SCHEMA_VERSION) {
		db.execute("DROP TABLE IF EXISTS files;DROP TABLE IF EXISTS directories;DROP TABLE IF EXISTS metadata;");
	}

	db.execute(
		"CREATE TABLE IF NOT EXISTS metadata ("
		"key TEXT PRIMARY KEY NOT NULL,"
		"value TEXT NOT NULL"
		") WITHOUT ROWID;"
		"CREATE TABLE IF NOT EXISTS directories ("
		"id INTEGER PRIMARY KEY NOT NULL,"
		"parent_id INTEGER REFERENCES directories(id) ON DELETE CASCADE,"
		"name TEXT NOT NULL CHECK(length(name) > 0),"
		"real_name TEXT,"
		"last_write INTEGER NOT NULL CHECK(last_write >= 0)"
		");"
		"CREATE TABLE IF NOT EXISTS files ("
		"id INTEGER PRIMARY KEY NOT NULL,"
		"directory_id INTEGER NOT NULL REFERENCES directories(id) ON DELETE CASCADE,"
		"name TEXT NOT NULL CHECK(length(name) > 0),"
		"size INTEGER NOT NULL CHECK(size >= 0),"
		"tth TEXT CHECK(tth IS NULL OR length(tth) = 39),"
		"real_path TEXT NOT NULL,"
		"last_write INTEGER NOT NULL CHECK(last_write >= 0)"
		");"
		// No secondary indexes: the cache is only ever read whole, in id order.
		"PRAGMA user_version = 3;"
	);
}

bool ShareManager::loadShareCache() noexcept {
	if(!SETTING(SHARE_CACHE)) {
		return false;
	}
	map<string, string> roots;
	{
		RLock l(cs);
		roots = shares;
	}
	// The cache was written by a scan that checked every directory; only the roots are checked
	// again (once each), in case private storage moved under one since.
	{
		const pft::PrivatePathScanner privacy;
		for(const auto& root: roots) {
			if(privacy.isPrivateRoot(root.first)) {
				LogManager::getInstance()->message(str(F_("Share cache skipped because the shared folder %1% contains privately received files") %
					Util::addBrackets(root.first)), LogMessage::SEV_INFO, _("Share cache"));
				return false;
			}
		}
	}

	const auto cacheFile = getShareCacheFile();
	if(File::getSize(cacheFile) < 0) {
		LogManager::getInstance()->message(_("Share cache not found; running full file list refresh"),
			LogMessage::SEV_INFO, _("Share cache"));
		return false;
	}

	try {
		SQLiteDB db(cacheFile);
		auto versionStmt = db.prepare("PRAGMA user_version");
		const auto version = versionStmt.step() ? versionStmt.columnInt(0) : 0;
		if(version != SHARE_CACHE_SCHEMA_VERSION) {
			LogManager::getInstance()->message(_("Share cache schema version does not match; running full file list refresh"),
				LogMessage::SEV_INFO, _("Share cache"));
			return false;
		}

		string expectedFingerprint;
		std::set<string, noCaseStringLess> expectedRoots;
		{
			RLock l(cs);
			expectedFingerprint = getShareCacheFingerprint();
			for(const auto& share: shares) {
				expectedRoots.insert(share.second);
			}
		}

		if(getMetadata(db, "fingerprint") != expectedFingerprint) {
			LogManager::getInstance()->message(_("Share cache does not match current sharing settings; running full file list refresh"),
				LogMessage::SEV_INFO, _("Share cache"));
			return false;
		}

		const auto directoryRows = countRows(db, "SELECT COUNT(*) FROM directories");
		const auto fileRows = countRows(db, "SELECT COUNT(*) FROM files");
		unordered_map<int64_t, Directory::Ptr> byId;
		if(directoryRows > 0 && directoryRows <= static_cast<uint64_t>(byId.max_size())) {
			byId.reserve(static_cast<size_t>(directoryRows));
		}

		unordered_map<string, Directory::Ptr, noCaseStringHash, noCaseStringEq> newDirectories;
		if(!expectedRoots.empty()) {
			newDirectories.reserve(expectedRoots.size());
		}

		// A file's real path is kept only when it can't be derived from its directories (see
		// Directory::File::validateName): under a name shared from one folder, it is that folder's
		// path plus the directories' real names.
		std::map<string, string, noCaseStringLess> singleRoots;
		{
			std::map<string, size_t, noCaseStringLess> rootCounts;
			for(const auto& root: roots) {
				++rootCounts[root.second];
				singleRoots[root.second] = root.first;
			}
			for(const auto& count: rootCounts) {
				if(count.second > 1) {
					singleRoots.erase(count.first);
				}
			}
		}
		unordered_map<int64_t, string> derivedPaths;
		if(directoryRows > 0 && directoryRows <= static_cast<uint64_t>(derivedPaths.max_size())) {
			derivedPaths.reserve(static_cast<size_t>(directoryRows));
		}

		auto dirs = db.prepare("SELECT id, parent_id, name, real_name, last_write FROM directories ORDER BY id");
		while(dirs.step()) {
			const auto id = dirs.columnInt64(0);
			if(id <= 0 || byId.find(id) != byId.end()) {
				throw ShareException(_("Invalid directory record in share cache"));
			}

			auto name = dirs.columnText(2);
			if(!isValidCachedName(name)) {
				throw ShareException(_("Invalid directory name in share cache"));
			}

			const auto directoryDate = dirs.columnInt64(4);
			if(directoryDate < 0 || static_cast<uint64_t>(directoryDate) >
				static_cast<uint64_t>((std::numeric_limits<time_t>::max)()))
			{
				throw ShareException(_("Invalid directory timestamp in share cache"));
			}

			auto dir = Directory::create(name, Directory::Ptr(), static_cast<time_t>(directoryDate));
			if(!dirs.columnIsNull(3)) {
				auto realName = dirs.columnText(3);
				if(!isValidCachedName(realName)) {
					throw ShareException(_("Invalid real directory name in share cache"));
				}
				dir->setRealName(std::move(realName));
			}

			if(dirs.columnIsNull(1)) {
				if(expectedRoots.find(name) == expectedRoots.end()) {
					throw ShareException(_("Unexpected root directory in share cache"));
				}
				if(!newDirectories.emplace(name, dir).second) {
					throw ShareException(_("Duplicate root directory in share cache"));
				}
			} else {
				const auto parentId = dirs.columnInt64(1);
				auto parent = byId.find(parentId);
				if(parent == byId.end()) {
					throw ShareException(_("Directory parent missing from share cache"));
				}
				dir->setParent(parent->second.get());
				if(!parent->second->directories.emplace(name, dir).second) {
					throw ShareException(_("Duplicate child directory in share cache"));
				}
			}
			byId.emplace(id, dir);

			string derived;
			if(dirs.columnIsNull(1)) {
				auto single = singleRoots.find(name);
				if(single != singleRoots.end()) {
					derived = single->second;
				}
			} else {
				auto parentPath = derivedPaths.find(dirs.columnInt64(1));
				if(parentPath != derivedPaths.end() && !parentPath->second.empty()) {
					derived = parentPath->second + dir->getRealName() + PATH_SEPARATOR;
				}
			}
			derivedPaths.emplace(id, std::move(derived));
		}

		if(newDirectories.size() != expectedRoots.size()) {
			throw ShareException(_("Share cache is missing a configured root directory"));
		}

		// Rows were written directory by directory, so id order groups them without sorting a
		// million rows first.
		auto files = db.prepare("SELECT directory_id, name, size, tth, real_path, last_write FROM files ORDER BY id");
		const auto storage = pft::storageRoot();
		while(files.step()) {
			auto dir = byId.find(files.columnInt64(0));
			if(dir == byId.end()) {
				throw ShareException(_("File parent missing from share cache"));
			}

			auto name = files.columnText(1);
			const auto size = files.columnInt64(2);
			if(!isValidCachedName(name) || size < 0) {
				throw ShareException(_("Invalid file record in share cache"));
			}

			optional<TTHValue> root;
			if(!files.columnIsNull(3)) {
				auto tth = files.columnText(3);
				if(tth.size() != 39) {
					throw ShareException(_("Invalid TTH value in share cache"));
				}
				root = TTHValue(tth);
			}

			const auto fileDate = files.columnInt64(5);
			if(fileDate < 0 || static_cast<uint64_t>(fileDate) >
				static_cast<uint64_t>((std::numeric_limits<time_t>::max)()))
			{
				throw ShareException(_("Invalid file timestamp in share cache"));
			}

			Directory::File file(name, size, dir->second, root, static_cast<time_t>(fileDate));
			if(files.columnIsNull(4)) {
				throw ShareException(_("Missing real file path in share cache"));
			}
			auto realPath = files.columnText(4);
			if(!isValidCachedPath(realPath) || !isUnderSharedRoot(realPath, roots)) {
				throw ShareException(_("Invalid real file path in share cache"));
			}
			// By name only: a per-file file system check would cost several network round trips
			// per file on a NAS, which is what made starting with a large network share take an hour.
			if(pft::isPrivateLocation(realPath, storage))
				continue;
			const auto& derived = derivedPaths[dir->first];
			if(derived.empty() || derived.size() + name.size() != realPath.size() ||
				realPath.compare(0, derived.size(), derived) != 0 || realPath.compare(derived.size(), string::npos, name) != 0)
			{
				file.realPath = std::move(realPath);
			}

			dir->second->files.insert(std::move(file));
		}

		{
			WLock l(cs);
			directories.swap(newDirectories);
			lastFullUpdate = GET_TICK();
			++listForce;
			rebuildIndices(static_cast<size_t>(std::min<uint64_t>(fileRows, static_cast<uint64_t>(tthIndex.max_size()))));
		}

		LogManager::getInstance()->message(str(F_("Loaded cached share tree from %1% (%2% directories, %3% files)") %
			Util::addBrackets(cacheFile) % std::to_string(directoryRows) % std::to_string(fileRows)),
			LogMessage::SEV_INFO, _("Share cache"));
		return true;
	} catch(const Exception& e) {
		LogManager::getInstance()->message(str(F_("Error loading share cache %1%: %2%; running full file list refresh") %
			Util::addBrackets(cacheFile) % e.getError()), LogMessage::SEV_WARNING, _("Share cache"));
	} catch(const std::exception& e) {
		LogManager::getInstance()->message(str(F_("Error loading share cache %1%: %2%; running full file list refresh") %
			Util::addBrackets(cacheFile) % e.what()), LogMessage::SEV_WARNING, _("Share cache"));
	}

	return false;
}

void ShareManager::saveShareCacheDirectory(SQLiteStatement& dirStmt, SQLiteStatement& fileStmt, const Directory::Ptr& dir,
	optional<int64_t> parentId, int64_t& nextId, uint64_t& directoryCount, uint64_t& fileCount) const
{
	// One directory is copied under the share lock and written outside it, so saving a large
	// share doesn't stop uploads, searches and hash results for the whole write.
	struct FileRow {
		string name;
		int64_t size;
		optional<TTHValue> tth;
		string realPath;
		time_t lastWrite;
	};
	string name;
	optional<string> realName;
	time_t lastWrite;
	vector<FileRow> files;
	vector<Directory::Ptr> children;
	{
		RLock l(cs);
		name = dir->getName();
		realName = dir->getRealNameOverride();
		lastWrite = dir->getLastWrite();
		files.reserve(dir->files.size());
		for(const auto& file: dir->files) {
			files.push_back({ file.getName(), file.getSize(), file.tth, file.getRealPath(), file.getLastWrite() });
		}
		children.reserve(dir->directories.size());
		for(const auto& child: dir->directories) {
			children.push_back(child.second);
		}
	}

	const auto id = nextId++;
	dirStmt.bind(1, id);
	if(parentId) {
		dirStmt.bind(2, *parentId);
	} else {
		dirStmt.bindNull(2);
	}
	dirStmt.bind(3, name);
	if(realName) {
		dirStmt.bind(4, *realName);
	} else {
		dirStmt.bindNull(4);
	}
	dirStmt.bind(5, static_cast<int64_t>(lastWrite));
	dirStmt.stepDone();
	dirStmt.reset();
	dirStmt.clearBindings();
	directoryCount++;

	string base32;
	for(const auto& file: files) {
		fileStmt.bind(1, id);
		fileStmt.bind(2, file.name);
		fileStmt.bind(3, file.size);
		if(file.tth) {
			base32.clear();
			fileStmt.bind(4, file.tth->toBase32(base32));
		} else {
			fileStmt.bindNull(4);
		}
		fileStmt.bind(5, file.realPath);
		fileStmt.bind(6, static_cast<int64_t>(file.lastWrite));
		fileStmt.stepDone();
		fileStmt.reset();
		fileStmt.clearBindings();
		fileCount++;
	}

	for(const auto& child: children) {
		saveShareCacheDirectory(dirStmt, fileStmt, child, id, nextId, directoryCount, fileCount);
	}
}

void ShareManager::saveShareCache() noexcept {
	if(!SETTING(SHARE_CACHE)) {
		return;
	}

	const auto cacheFile = getShareCacheFile();
	try {
		writeShareCache(cacheFile);
	} catch(const Exception& e) {
		LogManager::getInstance()->message(str(F_("Error saving share cache %1%: %2%") % Util::addBrackets(cacheFile) % e.getError()),
			LogMessage::SEV_ERROR, _("Share cache"));
	} catch(const std::exception& e) {
		LogManager::getInstance()->message(str(F_("Error saving share cache %1%: %2%") % Util::addBrackets(cacheFile) % e.what()),
			LogMessage::SEV_ERROR, _("Share cache"));
	}
}

namespace {
bool removeIfExists(const string& path) noexcept {
	if(File::getSize(path) == -1) {
		return true;
	}
	File::deleteFile(path);
	return File::getSize(path) == -1;
}
}

void ShareManager::writeShareCache(const string& cacheFile) {
	// The snapshot is written to a new file and then replaces the old one: no million-row delete
	// first, and an interrupted save never leaves a partial cache behind. (A corrupt old file is
	// simply replaced.)
	const auto newFile = cacheFile + ".new";
	for(const char* suffix: { "", "-wal", "-shm", "-journal" }) {
		if(!removeIfExists(newFile + suffix)) {
			throw FileException(str(F_("Unable to remove %1%") % Util::addBrackets(newFile + suffix)));
		}
	}

	uint64_t directoryCount = 0;
	uint64_t fileCount = 0;
	try {
		SQLiteDB db(newFile);
		// A throwaway file until it is complete: no journal, and the rows are written parents
		// first, consistent by construction.
		db.execute("PRAGMA journal_mode=OFF;PRAGMA synchronous=OFF;PRAGMA foreign_keys=OFF;");
		createShareCacheSchema(db);
		SQLiteTransaction transaction(db);
		{
			auto metaStmt = db.prepare("INSERT INTO metadata(key, value) VALUES(?1, ?2)");
			auto putMeta = [&metaStmt](const string& key, const string& value) {
				metaStmt.bind(1, key);
				metaStmt.bind(2, value);
				metaStmt.stepDone();
				metaStmt.reset();
				metaStmt.clearBindings();
			};

			string fingerprint;
			vector<Directory::Ptr> roots;
			{
				RLock l(cs);
				fingerprint = getShareCacheFingerprint();
				for(const auto& dir: directories) {
					roots.push_back(dir.second);
				}
			}
			putMeta("schema", Util::toString(SHARE_CACHE_SCHEMA_VERSION));
			putMeta("fingerprint", fingerprint);
			putMeta("app", APPNAME " " VERSIONSTRING);

			auto dirStmt = db.prepare("INSERT INTO directories(id, parent_id, name, real_name, last_write) VALUES(?1, ?2, ?3, ?4, ?5)");
			auto fileStmt = db.prepare("INSERT INTO files(directory_id, name, size, tth, real_path, last_write) VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
			int64_t nextId = 1;
			for(const auto& root: roots) {
				if(stopping) {
					throw Exception(_("Shutting down"));
				}
				saveShareCacheDirectory(dirStmt, fileStmt, root, nullopt, nextId, directoryCount, fileCount);
			}

			putMeta("directories", std::to_string(directoryCount));
			putMeta("files", std::to_string(fileCount));
		}
		transaction.commit();
	} catch(...) {
		removeIfExists(newFile);
		throw;
	}

	// The old database's write-ahead log must never be replayed into the new file.
	for(const char* suffix: { "-wal", "-shm" }) {
		if(!removeIfExists(cacheFile + suffix)) {
			removeIfExists(newFile);
			throw FileException(str(F_("Unable to remove %1%") % Util::addBrackets(cacheFile + suffix)));
		}
	}
	// Atomic replacement (File::renameFile would copy over an existing file).
#ifdef _WIN32
	if(!::MoveFileExW(File::toNativePath(newFile).c_str(), File::toNativePath(cacheFile).c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		const auto error = Util::translateError(GetLastError());
		removeIfExists(newFile);
		throw FileException(error);
	}
#else
	File::renameFile(newFile, cacheFile);
#endif

	LogManager::getInstance()->message(str(F_("Saved share cache %1% (%2% directories, %3% files)") %
		Util::addBrackets(cacheFile) % std::to_string(directoryCount) % std::to_string(fileCount)),
		LogMessage::SEV_INFO, _("Share cache"));
}

void ShareManager::startupRefresh(function<void (float)> progressF) noexcept {
	if(loadShareCache()) {
		// Cached data is immediately usable for share size, search, file lists
		// and upload path resolution; the background refresh reconciles it with
		// the live filesystem and broadcasts updated hub info when it finishes.
		// Set before the refresh starts so its completion cannot race ahead of it.
		cacheUnverified = true;
		refresh(true, true, false);
		return;
	}

	refresh(true, false, true, progressF);
}

void ShareManager::refresh(bool dirs, bool aUpdate, bool block, function<void (float)> progressF) noexcept {
	if(refreshing.test_and_set()) {
		LogManager::getInstance()->message(_("File list refresh in progress, please wait for it to finish before trying to refresh again"),
			LogMessage::SEV_WARNING, _("Sharing"));
		return;
	}
	refreshActive = true;

	update = aUpdate;
	refreshDirs = dirs;

	join();

	if(block) {
		runRefresh(progressF);

	} else {
		try {
			start();
			setThreadPriority(Thread::LOW);
		} catch(const ThreadException& e) {
			LogManager::getInstance()->message(str(F_("File list refresh failed: %1%") % e.getError()),
				LogMessage::SEV_ERROR, _("Sharing"));
			refreshActive = false;
			refreshing.clear();
		}
	}
}

StringPairList ShareManager::getDirectories() const noexcept {
	RLock l(cs);
	StringPairList ret;
	for(auto& i: shares) {
		ret.emplace_back(i.second, i.first);
	}
	return ret;
}

int ShareManager::run() {
	runRefresh();
	// A folder added after the refresh scanned its pending list, but before it released the
	// refresh flag, found the flag taken and left its scan to this thread.
	while(!stopping && hasPendingDirectories() && !refreshing.test_and_set()) {
		refreshActive = true;
		update = true;
		refreshDirs = false;
		runRefresh();
	}
	return 0;
}

bool ShareManager::scanRoot(const pft::PrivatePathScanner& privacy, const string& realPath, const string& virtualName,
	Directory::Ptr& tree_)
{
	// A root that can't be reached (a NAS that is off or disconnected) keeps its previous contents:
	// scanning it would find nothing and remove the whole folder from the share.
	if(!File::isDirectory(realPath)) {
		LogManager::getInstance()->message(str(F_("The shared folder %1% is not available; its previous contents stay shared until it can be scanned") %
			Util::addBrackets(realPath)), LogMessage::SEV_WARNING, _("Sharing"));
		return false;
	}

	Directory::Ptr scanned;
	if(privacy.isPrivateRoot(realPath)) {
		LogManager::getInstance()->message(str(F_("The shared folder %1% is not shared because it contains privately received files") %
			Util::addBrackets(realPath)), LogMessage::SEV_WARNING, _("Sharing"));
	} else if(checkHidden(realPath)) {
		bool merged;
		{
			RLock l(cs);
			merged = getRealRoots(virtualName).size() > 1;
		}
		scanned = scanTree(privacy, realPath, nullopt, nullptr, 0, merged);
		if(stopping) {
			return false;
		}
		// Lost during the scan: what was found is incomplete.
		if(!File::isDirectory(realPath)) {
			LogManager::getInstance()->message(str(F_("The shared folder %1% became unavailable while it was scanned; its previous contents stay shared until it can be scanned") %
				Util::addBrackets(realPath)), LogMessage::SEV_WARNING, _("Sharing"));
			return false;
		}
	}

	if(!scanned) {
		scanned = Directory::create(virtualName);
	}
	scanned->setName(virtualName);
	if(!tree_) {
		tree_ = scanned;
	} else {
		tree_->merge(scanned, realPath);
	}
	return true;
}

StringList ShareManager::getRealRoots(const string& virtualName) const {
	StringList roots;
	for(const auto& share: shares) {
		if(Util::stricmp(share.second, virtualName) == 0) {
			roots.push_back(share.first);
		}
	}
	return roots;
}

void ShareManager::afterTreeChange() {
	rebuildIndices();
	replayScanHashes();
	replayMovedFiles();
	setDirty();
}

void ShareManager::unindexTree(const Directory& dir) {
	for(const auto& file: dir.files) {
		if(file.tth) {
			auto i = tthIndex.find(*file.tth);
			if(i != tthIndex.end() && i->second == &file) {
				tthIndex.erase(i);
				sharedBytes -= file.getSize();
			}
		}
	}
	for(const auto& child: dir.directories) {
		unindexTree(*child.second);
	}
}

void ShareManager::replaceTree(const string& virtualName, const Directory::Ptr& tree) {
	// Only this name's entries change, so a refresh doesn't rebuild the indices of the whole share
	// (over a second for a million files, under the share lock) after every folder. The rebuild at
	// the end of the refresh settles duplicates across folders and resizes the name filter.
	auto old = directories.find(virtualName);
	if(old != directories.end()) {
		unindexTree(*old->second);
		old->second = tree;
	} else {
		directories[virtualName] = tree;
	}
	++indexVersion;
	deferDuplicates = true;
	updateIndices(*tree);
	deferDuplicates = false;
	replayScanHashes();
	replayMovedFiles();
	setDirty();
}

void ShareManager::runRefresh(function<void (float)> progressF) {
	// Folders shared under the same virtual name are scanned and applied together.
	vector<pair<string, StringList>> groups;
	size_t rootCount = 0;
	{
		WLock l(cs);
		for(const auto& share: shares) {
			auto group = find_if(groups.begin(), groups.end(), [&share](const pair<string, StringList>& g) {
				return Util::stricmp(g.first, share.second) == 0;
			});
			if(group == groups.end()) {
				groups.emplace_back(share.second, StringList { share.first });
			} else {
				group->second.push_back(share.first);
			}
			++rootCount;
		}
		// A full scan builds every registered root, including any still waiting for their own scan.
		if(refreshDirs) {
			pendingDirectories.clear();
		}
		// Hashing isn't paused while folders are scanned; keep what finishes until the scan is live.
		recordScanHashes = true;
		scanHashes.clear();
		movedFiles.clear();
	}
	// A full refresh with nothing shared still leaves an accurate (empty) index.
	bool scannedAllRoots = refreshDirs;

	bool refreshedDirs = false;
	bool scannedRoots = false;

	if(refreshDirs) {
		LogManager::getInstance()->message(_("File list refresh initiated"), LogMessage::SEV_INFO, _("Sharing"));
		const auto started = GET_TICK();

		// Make sure that the cache is updated.
		updateFilterCache();
		const pft::PrivatePathScanner privacy;

		float progressCounter = 0;
		const float progressTotal = static_cast<float>(std::max<size_t>(rootCount, 1));

		for(const auto& group: groups) {
			if(stopping) {
				break;
			}

			Directory::Ptr tree;
			bool usable = true;
			for(const auto& root: group.second) {
				if(stopping || !scanRoot(privacy, root, group.first, tree)) {
					usable = false;
					break;
				}
				if(progressF) {
					progressF(++progressCounter / progressTotal);
				}
			}
			if(stopping) {
				break;
			}
			if(!usable) {
				// Kept as it was; the queue's in-share check waits for a complete scan.
				scannedAllRoots = false;
				continue;
			}

			// Each folder is shared as soon as it is scanned, not after every folder is done.
			WLock l(cs);
			// Removed, renamed or joined by another folder while it was scanned: that change has its own scan.
			if(getRealRoots(group.first) != group.second) {
				continue;
			}
			replaceTree(group.first, tree);
			refreshedDirs = true;
		}

		if(!stopping) {
			WLock l(cs);
			// Virtual names that are no longer shared at all.
			for(auto i = directories.begin(); i != directories.end();) {
				if(getRealRoots(i->first).empty()) {
					i = directories.erase(i);
					refreshedDirs = true;
				} else {
					++i;
				}
			}
			if(refreshedDirs) {
				afterTreeChange();
			}
		}

		refreshDirs = false;
		scannedRoots = !stopping;
		// Automatic refreshes are timed from the end of the previous one, and a long scan pushes the
		// next one back by as long as it took, so slow storage is never scanned without a break.
		lastFullUpdate = GET_TICK();
		lastRefreshDuration = lastFullUpdate - started;

		if(!stopping) {
			LogManager::getInstance()->message(_("File list refresh finished"), LogMessage::SEV_INFO, _("Sharing"));
		}
	}

	if(!stopping && scanPendingDirectories()) {
		refreshedDirs = true;
	}

	{
		WLock l(cs);
		recordScanHashes = false;
		vector<ScanHash>().swap(scanHashes);
		vector<MovedFile>().swap(movedFiles);
	}

	if(stopping) {
		refreshing.clear();
		refreshActive = false;
		return;
	}

	if(update) {
		/* Hold off hashing until all infos have been sent out
		   so we properly signal the update to possible bloom requesters. */
		ClientManager::getInstance()->infoUpdated(scannedRoots ? std::make_shared<HashManager::HashPauser>() : nullptr);
	}

	refreshing.clear();
	refreshActive = false;
	if(refreshedDirs) {
		saveShareCache();
	}

	// The queue skipped its "already in share" check while the share index was only the
	// startup cache, which can list files deleted since the last run; run it now.
	if(scannedAllRoots && cacheUnverified.exchange(false) && SETTING(DONT_DL_ALREADY_SHARED)) {
		QueueManager::getInstance()->removeSharedDownloads();
	}
}

void ShareManager::getBloom(ByteVector& v, size_t k, size_t m, size_t h) const {
	dcdebug("Creating bloom filter, k=%u, m=%u, h=%u\n", k, m, h);
	RLock l(cs);

	HashBloom bloom;
	bloom.reset(k, m, h);
	for(auto& i: tthIndex) {
		bloom.add(i.first);
	}
	bloom.copy_to(v);
}

void ShareManager::getBloom(ByteVector& v, size_t k, size_t m, size_t h, const string& hubUrl) const {
	dcdebug("Creating hub share bloom filter, k=%u, m=%u, h=%u\n", k, m, h);
	auto access = getShareAccess(hubUrl);
	RLock l(cs);

	HashBloom bloom;
	bloom.reset(k, m, h);
	for(const auto& item: tthIndex) {
		if(isFileAllowed(*item.second, access)) {
			bloom.add(item.first);
		}
	}
	bloom.copy_to(v);
}

namespace {
class XmlSizeCounter : public OutputStream {
public:
	explicit XmlSizeCounter(OutputStream& output) : output(output) { }
	size_t write(const void* data, size_t len) override {
		const auto written = output.write(data, len);
		count += static_cast<int64_t>(len);
		return written;
	}
	size_t flush() override { return output.flush(); }
	int64_t getCount() const { return count; }
private:
	OutputStream& output;
	int64_t count = 0;
};

bool removeFile(const string& path) noexcept {
	File::deleteFile(path);
	return File::getSize(path) == -1;
}

string fileListHeader(const string& base = "/") {
	string tmp;
	return "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() +
		"\" Base=\"" + SimpleXML::escape(base, tmp, true) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n";
}
}

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::writeDirectoryXml(OutputStream& os, const Directory::Ptr& dir, string& indent, string& tmp) const {
	// One directory at a time under the share lock: uploads, searches and hash results go on while
	// a large share's list is written and compressed.
	struct FileEntry {
		string name;
		int64_t size;
		TTHValue tth;
	};
	string name;
	time_t lastWrite;
	vector<Directory::Ptr> children;
	vector<FileEntry> files;
	{
		RLock l(cs);
		name = dir->getName();
		lastWrite = dir->getLastWrite();
		children.reserve(dir->directories.size());
		for(const auto& child: dir->directories) {
			children.push_back(child.second);
		}
		files.reserve(dir->files.size());
		for(const auto& file: dir->files) {
			if(file.tth) {
				files.push_back({ file.getName(), file.getSize(), *file.tth });
			}
		}
	}

	os.write(indent);
	os.write(LITERAL("<Directory Name=\""));
	os.write(SimpleXML::escape(name, tmp, true));
	if(lastWrite > 0) {
		os.write(LITERAL("\" Date=\""));
		os.write(Util::toString(static_cast<int64_t>(lastWrite)));
	}
	os.write(LITERAL("\">\r\n"));
	indent += '\t';
	for(const auto& child: children) {
		writeDirectoryXml(os, child, indent, tmp);
	}
	for(const auto& file: files) {
		os.write(indent);
		os.write(LITERAL("<File Name=\""));
		os.write(SimpleXML::escape(file.name, tmp, true));
		os.write(LITERAL("\" Size=\""));
		os.write(Util::toString(file.size));
		os.write(LITERAL("\" TTH=\""));
		tmp.clear();
		os.write(file.tth.toBase32(tmp));
		os.write(LITERAL("\"/>\r\n"));
	}
	indent.erase(indent.length() - 1);
	os.write(indent);
	os.write(LITERAL("</Directory>\r\n"));
}
#undef LITERAL

void ShareManager::writeListXml(OutputStream& os, const ShareAccess& access) const {
	vector<Directory::Ptr> roots;
	{
		RLock l(cs);
		for(const auto& item: directories) {
			// An explicit favorite-hub profile with no selected directories is not
			// unrestricted: it gets a valid root-only file list.
			if(isVirtualAllowed(item.first, access)) {
				roots.push_back(item.second);
			}
		}
	}
	os.write(SimpleXML::utf8Header);
	os.write(fileListHeader());
	string indent, tmp;
	for(const auto& root: roots) {
		writeDirectoryXml(os, root, indent, tmp);
	}
	os.write("</FileListing>");
}

string ShareManager::getProfileKey(const ShareAccess& access) {
	if(access.unrestricted) {
		return Util::emptyString;
	}
	// Never empty: an explicit empty profile is not the unrestricted list.
	string key = "profile";
	for(const auto& directory: access.directories) {
		key += '\n' + Text::toLower(directory);
	}
	return key;
}

ShareManager::FileList ShareManager::getList(const ShareAccess& access) {
	const auto key = getProfileKey(access);
	// Rebuilt when forced (a refresh, a finished download), and otherwise at most every 15 minutes
	// while the share changes (as files are hashed, for example), as AirDC++ does. Requires cs.
	auto isCurrent = [this](const FileList& list) {
		return !list.path.empty() && list.forceVersion == listForce &&
			(list.dirtyVersion == listVersion || (list.generated + 15 * 60 * 1000 >= GET_TICK() && list.generated >= lastFullUpdate));
	};
	{
		FastLock l(fileListsCs);
		const auto& list = fileLists[key];
		if(isCurrent(list)) {
			return list;
		}
	}

	Lock gl(listCs, std::try_to_lock);
	if(!gl.owns_lock()) {
		// A list is being built (that takes a while for a large share): whoever already has one
		// gets it meanwhile rather than waiting.
		{
			FastLock l(fileListsCs);
			const auto& list = fileLists[key];
			if(!list.path.empty()) {
				return list;
			}
		}
		gl.lock();
	}

	uint64_t version, force;
	{
		FastLock l(fileListsCs);
		const auto& list = fileLists[key];
		// Built by another request meanwhile.
		if(isCurrent(list)) {
			return list;
		}
		version = listVersion;
		force = listForce;
	}

	FileList list;
	try {
		string name = "files";
		if(!key.empty()) {
			TigerHash hash;
			hash.update(key.data(), key.size());
			name += "-" + TTHValue(hash.finalize()).toBase32().substr(0, 16);
		}
		auto path = Util::getPath(Util::PATH_USER_CONFIG) + name + "-" + Util::toString(++listN) + ".xml.bz2";
		{
			File f(path, File::WRITE, File::TRUNCATE | File::CREATE);
			// We don't care about the leaves...
			CalcOutputStream<TTFilter<1024*1024*1024>, false> bzTree(&f);
			FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
			XmlSizeCounter count(bzipper);
			CalcOutputStream<TTFilter<1024*1024*1024>, false> xml(&count);

			writeListXml(xml, access);
			xml.flush();

			list.xmlLen = count.getCount();
			xml.getFilter().getTree().finalize();
			bzTree.getFilter().getTree().finalize();
			list.xmlRoot = xml.getFilter().getTree().getRoot();
			list.bzRoot = bzTree.getFilter().getTree().getRoot();
		}
		if(key.empty()) {
			// The full list keeps its usual name when it isn't being uploaded.
			const auto stable = Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2";
			{
				FastLock l(fileListsCs);
				auto& old = fileLists[key];
				if(old.path == stable) {
					old.ref.reset();
				}
			}
			if(File::getSize(stable) == -1 || removeFile(stable)) {
				try {
					File::renameFile(path, stable);
					path = stable;
				} catch(const FileException&) {
					// Ignore, the numbered name works as well.
				}
			}
		}
		list.ref = std::make_shared<File>(path, File::READ, File::OPEN);
		list.bzLen = File::getSize(path);
		list.path = path;
		LogManager::getInstance()->message(str(F_("File list %1% generated") % Util::addBrackets(path)),
			LogMessage::SEV_INFO, _("Sharing"));
	} catch(const Exception&) {
		// Keep serving the previous list.
		FastLock l(fileListsCs);
		return fileLists[key];
	}

	list.dirtyVersion = version;
	list.forceVersion = force;
	list.generated = GET_TICK();

	string replaced;
	{
		FastLock l(fileListsCs);
		auto& current = fileLists[key];
		if(current.path != list.path) {
			replaced = current.path;
		}
		current = list;
	}
	// A list being uploaded is still open; it is removed later.
	if(!replaced.empty()) {
		staleListFiles.push_back(replaced);
	}
	removeStaleLists();
	return list;
}

void ShareManager::removeStaleLists() noexcept {
	staleListFiles.erase(std::remove_if(staleListFiles.begin(), staleListFiles.end(), [](const string& path) {
		return File::getSize(path) == -1 || removeFile(path);
	}), staleListFiles.end());
}

void ShareManager::generateXmlList() {
	getList(ShareAccess { true, { } });
}

string ShareManager::getListFile(const string& hubUrl) {
	return getList(getShareAccess(hubUrl)).path;
}

bool ShareManager::isOwnListFile(const string& path) const {
	FastLock l(fileListsCs);
	return std::any_of(fileLists.begin(), fileLists.end(), [&path](const pair<const string, FileList>& list) {
		return !list.second.path.empty() && Util::stricmp(list.second.path, path) == 0;
	});
}

namespace {
class XmlListInputStream : public InputStream {
public:
	explicit XmlListInputStream(File* file) : stream(file) { }
	size_t read(void* data, size_t& len) override {
		// Upload progress must count XML bytes, not the backing bzip2 file reads.
		const auto produced = stream.read(data, len);
		len = produced;
		return produced;
	}
private:
	FilteredInputStream<UnBZFilter, true> stream;
};
}

std::pair<std::unique_ptr<InputStream>, int64_t> ShareManager::openXmlList(const string& hubUrl) {
	// The uncompressed list is read back from the compressed one, so it is never built in memory.
	const auto list = getList(hubUrl.empty() ? ShareAccess { true, { } } : getShareAccess(hubUrl));
	if(list.path.empty()) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
	auto file = std::make_unique<File>(list.path, File::READ, File::OPEN);
	auto stream = std::make_unique<XmlListInputStream>(file.get());
	file.release();
	return { std::move(stream), list.xmlLen };
}

string ShareManager::generateFileListData(const string& hubUrl, bool compressed) {
	const auto list = getList(getShareAccess(hubUrl));
	if(list.path.empty()) {
		return Util::emptyString;
	}
	if(compressed) {
		return File(list.path, File::READ, File::OPEN).read();
	}
	string xml;
	StringRefOutputStream output(xml);
	auto opened = openXmlList(hubUrl);
	char buffer[64 * 1024];
	for(;;) {
		size_t n = sizeof(buffer);
		const auto produced = opened.first->read(buffer, n);
		if(!produced) break;
		output.write(buffer, produced);
	}
	return xml;
}

MemoryInputStream* ShareManager::generateFileList(const string& hubUrl, bool compressed) {
	return new MemoryInputStream(generateFileListData(hubUrl, compressed));
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse) const {
	if(dir.empty() || dir[0] != '/' || dir[dir.size()-1] != '/')
		return 0;

	time_t baseDate = 0;
	{
		RLock l(cs);
		if(dir == "/") {
			for(const auto& item: directories) {
				baseDate = (std::max)(baseDate, item.second->getLastWrite());
			}
		} else {
			Directory::Ptr root;
			string::size_type pos = 1;
			string::size_type next = 1;
			bool first = true;
			while((next = dir.find('/', pos)) != string::npos) {
				if(next == pos) {
					++pos;
					continue;
				}
				if(first) {
					first = false;
					auto item = directories.find(dir.substr(pos, next - pos));
					if(item == directories.end()) {
						return nullptr;
					}
					root = item->second;
				} else {
					auto item = root->directories.find(dir.substr(pos, next - pos));
					if(item == root->directories.end()) {
						return nullptr;
					}
					root = item->second;
				}
				pos = next + 1;
			}
			baseDate = root ? root->getLastWrite() : 0;
		}
	}

	string xml = SimpleXML::utf8Header;
	string tmp;
	xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() +
		"\" Base=\"" + SimpleXML::escape(dir, tmp, true) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\"";
	if(baseDate > 0) {
		xml += " BaseDate=\"" + Util::toString(static_cast<int64_t>(baseDate)) + "\"";
	}
	xml += ">\r\n";
	// Limit generated XML before compression to keep partial-list requests memory-bounded.
	const auto maxPartialListBytes = static_cast<uint64_t>(std::max(1024, SETTING(MAX_PARTIAL_LIST_BYTES)));
	if(xml.size() >= maxPartialListBytes) {
		return 0;
	}
	StringRefOutputStream stringOutput(xml);
	LimitedOutputStream<false> sos(&stringOutput, maxPartialListBytes - xml.size());
	string indent = "\t";

	RLock l(cs);
	if(dir == "/") {
		for(auto& i: directories) {
			tmp.clear();
			i.second->toXml(sos, indent, tmp, recurse ? -1 : 0, !recurse);
		}
	} else {
		string::size_type i = 1, j = 1;

		Directory::Ptr root;

		bool first = true;
		while( (i = dir.find('/', j)) != string::npos) {
			if(i == j) {
				j++;
				continue;
			}

			if(first) {
				first = false;
				auto it = directories.find(dir.substr(j, i-j));
				if(it == directories.end())
					return 0;
				root = it->second;

			} else {
				auto it2 = root->directories.find(dir.substr(j, i-j));
				if(it2 == root->directories.end()) {
					return 0;
				}
				root = it2->second;
			}
			j = i + 1;
		}

		if(!root)
			return 0;

		for(auto& it2: root->directories) {
			it2.second->toXml(sos, indent, tmp, recurse ? -1 : 0, !recurse);
		}
		root->filesToXml(sos, indent, tmp, !recurse);
	}

	xml += "</FileListing>";
	return new MemoryInputStream(xml);
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse, const string& hubUrl) const {
	if(dir.empty() || dir[0] != '/' || dir[dir.size() - 1] != '/') {
		return nullptr;
	}

	auto access = getShareAccess(hubUrl);
	time_t baseDate = 0;
	{
		RLock l(cs);
		if(dir == "/") {
			for(const auto& item: directories) {
				if(isVirtualAllowed(item.first, access)) {
					baseDate = (std::max)(baseDate, item.second->getLastWrite());
				}
			}
		} else {
			Directory::Ptr root;
			string::size_type pos = 1;
			string::size_type next = 1;
			bool first = true;
			while((next = dir.find('/', pos)) != string::npos) {
				if(next == pos) {
					++pos;
					continue;
				}
				if(first) {
					first = false;
					auto item = directories.find(dir.substr(pos, next - pos));
					if(item == directories.end() || !isVirtualAllowed(item->first, access)) {
						return nullptr;
					}
					root = item->second;
				} else {
					auto item = root->directories.find(dir.substr(pos, next - pos));
					if(item == root->directories.end()) {
						return nullptr;
					}
					root = item->second;
				}
				pos = next + 1;
			}
			baseDate = root ? root->getLastWrite() : 0;
		}
	}

	string xml = SimpleXML::utf8Header;
	string tmp;
	xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() +
		"\" Base=\"" + SimpleXML::escape(dir, tmp, true) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\"";
	if(baseDate > 0) {
		xml += " BaseDate=\"" + Util::toString(static_cast<int64_t>(baseDate)) + "\"";
	}
	xml += ">\r\n";
	// Apply the same uncompressed bound to TTH-based partial-list generation.
	const auto maxPartialListBytes = static_cast<uint64_t>(std::max(1024, SETTING(MAX_PARTIAL_LIST_BYTES)));
	if(xml.size() >= maxPartialListBytes) {
		return 0;
	}
	StringRefOutputStream stringOutput(xml);
	LimitedOutputStream<false> output(&stringOutput, maxPartialListBytes - xml.size());
	string indent = "\t";

	RLock l(cs);
	if(dir == "/") {
		for(const auto& item: directories) {
			if(isVirtualAllowed(item.first, access)) {
				tmp.clear();
				item.second->toXml(output, indent, tmp, recurse ? -1 : 0, !recurse);
			}
		}
	} else {
		string::size_type i = 1;
		string::size_type j = 1;
		Directory::Ptr root;
		bool first = true;

		while((i = dir.find('/', j)) != string::npos) {
			if(i == j) {
				++j;
				continue;
			}

			if(first) {
				first = false;
				auto item = directories.find(dir.substr(j, i - j));
				if(item == directories.end() || !isVirtualAllowed(item->first, access)) {
					return nullptr;
				}
				root = item->second;
			} else {
				auto item = root->directories.find(dir.substr(j, i - j));
				if(item == root->directories.end()) {
					return nullptr;
				}
				root = item->second;
			}
			j = i + 1;
		}

		if(!root) {
			return nullptr;
		}

		for(const auto& item: root->directories) {
			item.second->toXml(output, indent, tmp, recurse ? -1 : 0, !recurse);
		}
		root->filesToXml(output, indent, tmp, !recurse);
	}

	xml += "</FileListing>";
	return new MemoryInputStream(xml);
}

/* params for partial file lists - when any of these params is not satisfied, an incomplete dir is
returned rather than a full dir list. */
const int8_t maxLevel = 2;
const size_t maxItemsPerLevel[maxLevel] = { 16, 4 };

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::Directory::toXml(OutputStream& xmlFile, string& indent, string& tmp2, int8_t level, bool addFileDates) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));
	if(lastWrite > 0) {
		xmlFile.write(LITERAL("\" Date=\""));
		xmlFile.write(Util::toString(static_cast<int64_t>(lastWrite)));
	}

	if(level < 0 || (level < maxLevel && directories.size() + files.size() <= maxItemsPerLevel[level])) {
		xmlFile.write(LITERAL("\">\r\n"));

		indent += '\t';
		if(level >= 0)
			++level;

		for(auto& i: directories) {
			i.second->toXml(xmlFile, indent, tmp2, level, addFileDates);
		}
		filesToXml(xmlFile, indent, tmp2, addFileDates);

		if(level >= 0)
			--level;
		indent.erase(indent.length()-1);

		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));

	} else {
		if(directories.empty() && files.empty()) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			size_t directoryCount = 0;
			size_t fileCount = 0;
			int64_t visibleSize = 0;
			std::function<void(const Directory&)> countContent = [&](const Directory& directory) {
				directoryCount += directory.directories.size();
				for(const auto& file: directory.files) {
					if(file.tth) {
						++fileCount;
						visibleSize += file.getSize();
					}
				}
				for(const auto& child: directory.directories) {
					countContent(*child.second);
				}
			};
			countContent(*this);

			xmlFile.write(LITERAL("\" Size=\""));
			xmlFile.write(Util::toString(visibleSize));
			if(!directories.empty()) {
				xmlFile.write(LITERAL("\" Children=\"1"));
			}
			xmlFile.write(LITERAL("\" Directories=\""));
			xmlFile.write(std::to_string(directoryCount));
			xmlFile.write(LITERAL("\" Files=\""));
			xmlFile.write(std::to_string(fileCount));
			xmlFile.write(LITERAL("\" Incomplete=\"1\" />\r\n"));
		}
	}
}

void ShareManager::Directory::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDates) const {
	for(auto& f: files) {
		if(!f.tth) { continue; }
		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f.getName(), tmp2, true));
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(f.getSize()));
		xmlFile.write(LITERAL("\" TTH=\""));
		tmp2.clear();
		xmlFile.write(f.tth->toBase32(tmp2));
		if(addDates && f.getLastWrite() > 0) {
			xmlFile.write(LITERAL("\" Date=\""));
			xmlFile.write(Util::toString(static_cast<int64_t>(f.getLastWrite())));
		}
		xmlFile.write(LITERAL("\"/>\r\n"));
	}
}

ShareManager::SearchQuery::SearchQuery() :
	include(&includeInit),
	gt(0),
	lt(numeric_limits<int64_t>::max()),
	isDirectory(false)
{
}

namespace {
	inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}
ShareManager::SearchQuery::SearchQuery(const StringList& adcParams) :
	SearchQuery()
{
	for(auto& p: adcParams) {
		if(p.size() <= 2)
			continue;

		auto cmd = toCode(p[0], p[1]);
		if(toCode('T', 'R') == cmd) {
			root = TTHValue(p.substr(2));
			return;
		} else if(toCode('A', 'N') == cmd) {
			includeInit.emplace_back(p.substr(2));
		} else if(toCode('N', 'O') == cmd) {
			exclude.emplace_back(p.substr(2));
		} else if(toCode('E', 'X') == cmd) {
			ext.push_back(Text::toLower(p.substr(2)));
		} else if(toCode('G', 'R') == cmd) {
			auto exts = AdcHub::parseSearchExts(Util::toInt(p.substr(2)));
			ext.insert(ext.begin(), exts.begin(), exts.end());
		} else if(toCode('R', 'X') == cmd) {
			noExt.push_back(Text::toLower(p.substr(2)));
		} else if(toCode('G', 'E') == cmd) {
			gt = Util::toInt64(p.substr(2));
		} else if(toCode('L', 'E') == cmd) {
			lt = Util::toInt64(p.substr(2));
		} else if(toCode('E', 'Q') == cmd) {
			lt = gt = Util::toInt64(p.substr(2));
		} else if(toCode('T', 'Y') == cmd) {
			isDirectory = p[2] == '2';
		}
	}
}

ShareManager::SearchQuery::SearchQuery(const string& nmdcString, int searchType, int64_t size, int fileType) :
	SearchQuery()
{
	if(fileType == SearchManager::TYPE_TTH && nmdcString.compare(0, 4, "TTH:") == 0) {
		root = TTHValue(nmdcString.substr(4));

	} else {
		StringTokenizer<string> tok(Text::toLower(nmdcString), '$');
		for(auto& term: tok.getTokens()) {
			if(!term.empty()) {
				includeInit.emplace_back(term);
			}
		}

		if(searchType == SearchManager::SIZE_ATLEAST) {
			gt = size;
		} else if(searchType == SearchManager::SIZE_ATMOST) {
			lt = size;
		}

		switch(fileType) {
		case SearchManager::TYPE_AUDIO: ext = AdcHub::parseSearchExts(1 << 0); break;
		case SearchManager::TYPE_COMPRESSED: ext = AdcHub::parseSearchExts(1 << 1); break;
		case SearchManager::TYPE_DOCUMENT: ext = AdcHub::parseSearchExts(1 << 2); break;
		case SearchManager::TYPE_EXECUTABLE: ext = AdcHub::parseSearchExts(1 << 3); break;
		case SearchManager::TYPE_PICTURE: ext = AdcHub::parseSearchExts(1 << 4); break;
		case SearchManager::TYPE_VIDEO: ext = AdcHub::parseSearchExts(1 << 5); break;
		case SearchManager::TYPE_DIRECTORY: isDirectory = true; break;
		}
	}
}

bool ShareManager::SearchQuery::isExcluded(const string& str) {
	for(auto& i: exclude) {
		if(i.match(str))
			return true;
	}
	return false;
}

bool ShareManager::SearchQuery::hasExt(const string& name) {
	if(ext.empty())
		return true;
	if(!noExt.empty()) {
		ext = StringList(ext.begin(), set_difference(ext.begin(), ext.end(), noExt.begin(), noExt.end(), ext.begin()));
		noExt.clear();
	}
	auto fileExt = Util::getFileExt(name);
	return !fileExt.empty() && std::find(ext.cbegin(), ext.cend(), Text::toLower(fileExt.substr(1))) != ext.cend();
}

/**
 * Alright, the main point here is that when searching, a search string is most often found in
 * the filename, not directory name, so we want to make that case faster. Also, we want to
 * avoid changing StringLists unless we absolutely have to --> this should only be done if a string
 * has been matched in the directory name. This new stringlist should also be used in all descendants,
 * but not the parents...
 */
void ShareManager::Directory::search(SearchResultList& results, SearchQuery& query, size_t maxResults) const noexcept {
	if(query.isExcluded(name))
		return;

	// Find any matches in the directory name and removed matched terms from the query.
	unique_ptr<StringSearch::List> newTerms;

	for(auto& term: *query.include) {
		if(term.match(name)) {
			if(!newTerms) {
				newTerms.reset(new StringSearch::List(*query.include));
			}
			newTerms->erase(remove(newTerms->begin(), newTerms->end(), term), newTerms->end());
		}
	}

	auto const old = query.include;
	ScopedFunctor(([old, &query] { query.include = old; }));
	if(newTerms) {
		query.include = newTerms.get();
	}

	if(query.include->empty() && query.ext.empty() && query.gt == 0) {
		// We satisfied all the search words! Add the directory...
		/// @todo send the directory hash when we have one
		results.push_back(new SearchResult(SearchResult::TYPE_DIRECTORY, getSize(), getFullName(), TTHValue(string(39, 'A'))));
		ShareManager::getInstance()->addHits(1);
	}

	if(!query.isDirectory) {
		for(auto& i: files) {
			if(!i.tth) { continue; }

			// check the size
			if(!(i.getSize() >= query.gt)) {
				continue;
			} else if(!(i.getSize() <= query.lt)) {
				continue;
			}

			if(query.isExcluded(i.getName()))
				continue;

			// check if the name matches
			auto j = query.include->begin();
			for(; j != query.include->end() && j->match(i.getName()); ++j)
				;	// Empty
			if(j != query.include->end())
				continue;

			// check extensions
			if(!query.hasExt(i.getName()))
				continue;

			results.push_back(new SearchResult(SearchResult::TYPE_FILE, i.getSize(),
				getFullName() + i.getName(), *i.tth));
			ShareManager::getInstance()->addHits(1);

			if(results.size() >= maxResults) { return; }
		}
	}

	for(auto& dir: directories) {
		dir.second->search(results, query, maxResults);

		if(results.size() >= maxResults) { return; }
	}
}

SearchResultList ShareManager::search(SearchQuery&& query, size_t maxResults) noexcept {
	SearchResultList results;

	RLock l(cs);

	if(query.root) {
		auto i = tthIndex.find(*query.root);
		if(i != tthIndex.end()) {
			results.push_back(new SearchResult(SearchResult::TYPE_FILE, i->second->getSize(),
				i->second->getParent()->getFullName() + i->second->getName(), *i->second->tth));
			addHits(1);
		}
		return results;
	}

	for(auto& i: query.includeInit) {
		if(!bloom.match(i.getPattern()))
			return results;
	}

	for(auto& dir: directories) {
		dir.second->search(results, query, maxResults);

		if(results.size() >= maxResults) { return results; }
	}

	return results;
}

void ShareManager::appendTempSearchResults(SearchResultList& results, SearchQuery& query,
	size_t maxResults, const string& hubUrl) noexcept
{
	if(results.size() >= maxResults || query.isDirectory || hubUrl.empty()) {
		return;
	}

	vector<TempShareInfo> candidates;
	{
		RLock l(cs);
		for(const auto& item: tempShares) {
			// Private-chat attachments never answer searches: a search does not prove
			// who is asking, and the recipient downloads straight from the message.
			if(!tempShareExpired(item, GET_TICK()) && !item.recipient && Util::stricmp(item.hubUrl, hubUrl) == 0)
				candidates.push_back(item);
		}
	}

	for(const auto& item: candidates) {
		if(results.size() >= maxResults) break;
		if(item.exactOnly && !query.root) continue;
		if(!item.exactOnly && !SETTING(ENABLE_RTF_TEMP_SHARES)) continue;

		if(query.root) {
			if(item.tth != *query.root) continue;
		} else {
			const auto name = Util::getFileName(item.realPath);
			if(name.empty() || item.size < query.gt || item.size > query.lt || query.isExcluded(name)) continue;

			auto term = query.includeInit.begin();
			for(; term != query.includeInit.end() && term->match(name); ++term) { }
			if(term != query.includeInit.end() || !query.hasExt(name)) continue;
		}

		const auto duplicate = std::any_of(results.begin(), results.end(), [&](const SearchResultPtr& result) {
			return result->getType() == SearchResult::TYPE_FILE && result->getTTH() == item.tth;
		});
		bool valid = false;
		if(item.exactOnly) {
			try {
				File file(item.realPath, File::READ, File::OPEN | File::SHARED);
				valid = file.getSize() == item.size && file.getLastModified() == item.timestamp;
				file.close();
				valid = valid && HashManager::getInstance()->verifyFileTTH(item.realPath, item.size, item.tth);
			} catch(...) { valid = false; }
		} else {
			valid = validateChatAttachment(item.tth, item.size, hubUrl);
		}
		if(duplicate || !valid) continue;

		results.push_back(new SearchResult(SearchResult::TYPE_FILE, item.size,
			Util::getFileName(item.realPath), item.tth));
		addHits(1);
	}
}

SearchResultList ShareManager::search(SearchQuery&& query, size_t maxResults,
	const ShareAccess& access, const string& hubUrl) noexcept
{
	SearchResultList results;
	{
		RLock l(cs);

		if(query.root) {
			auto item = tthIndex.find(*query.root);
			if(item != tthIndex.end() && isFileAllowed(*item->second, access)) {
				results.push_back(new SearchResult(SearchResult::TYPE_FILE, item->second->getSize(),
					item->second->getParent()->getFullName() + item->second->getName(), *item->second->tth));
				addHits(1);
			}
		} else if(std::all_of(query.includeInit.begin(), query.includeInit.end(),
			[this](const StringSearch& term) { return bloom.match(term.getPattern()); }))
		{
			// The bloom filter holds every shared name, whatever the hub's profile, so a term it
			// rejects matches nothing; most searches never walk the share.
			for(const auto& dir: directories) {
				if(!isVirtualAllowed(dir.first, access)) continue;
				dir.second->search(results, query, maxResults);
				if(results.size() >= maxResults) break;
			}
		}
	}

	appendTempSearchResults(results, query, maxResults, hubUrl);
	return results;
}

SearchResultList ShareManager::search(const StringList& adcParams, size_t maxResults) noexcept {
#if DCPP_TIME_SEARCHES
	auto start = GET_TICK();
	ScopedFunctor(([start] {
		LogManager::getInstance()->message("The ADC search took " + Util::toString(GET_TICK() - start) + " ms",
			LogMessage::SEV_VERBOSE, _("Search"));
	}));
#endif

	return search(SearchQuery(adcParams), maxResults);
}

SearchResultList ShareManager::search(const StringList& adcParams, size_t maxResults, const string& hubUrl) noexcept {
	try {
		return search(SearchQuery(adcParams), maxResults, getShareAccess(hubUrl), hubUrl);
	} catch(...) {
		return SearchResultList();
	}
}

SearchResultList ShareManager::search(const string& nmdcString, int searchType, int64_t size, int fileType, size_t maxResults) noexcept {
#if DCPP_TIME_SEARCHES
	auto start = GET_TICK();
	ScopedFunctor(([start] {
		LogManager::getInstance()->message("The NMDC search took " + Util::toString(GET_TICK() - start) + " ms",
			LogMessage::SEV_VERBOSE, _("Search"));
	}));
#endif

	return search(SearchQuery(nmdcString, searchType, size, fileType), maxResults);
}

SearchResultList ShareManager::search(const string& nmdcString, int searchType, int64_t size, int fileType,
	size_t maxResults, const string& hubUrl) noexcept
{
	try {
		return search(SearchQuery(nmdcString, searchType, size, fileType), maxResults,
			getShareAccess(hubUrl), hubUrl);
	} catch(...) {
		return SearchResultList();
	}
}

ShareManager::Directory::Ptr ShareManager::getDirectory(const string& realPath) noexcept {
	for(auto& mi: shares) {
		if(Util::strnicmp(realPath, mi.first, mi.first.length()) == 0) {
			auto di = directories.find(mi.second);
			if(di == directories.end()) {
				return nullptr;
			}
			auto d = di->second;

			string::size_type i;
			string::size_type j = mi.first.length();
			while((i = realPath.find(PATH_SEPARATOR, j)) != string::npos) {
				auto dirName = realPath.substr(j, i - j);

				auto& subDirs = d->directories;
				d.reset();
				for(auto& subDir: subDirs) {
					if(subDir.second->getRealName() == dirName) {
						d = subDir.second;
						break;
					}
				}
				if(!d) {
					return nullptr;
				}

				j = i + 1;
			}
			return d;
		}
	}
	return nullptr;
}

optional<std::reference_wrapper<const ShareManager::Directory::File>> ShareManager::getFile(const string& realPath, Directory::Ptr d) noexcept {
	if(!d) {
		d = getDirectory(realPath);
		if(!d) {
			return nullopt;
		}
	}

	auto i = d->findFile(Util::getFileName(realPath));
	if(i == d->files.end()) {
		/* should never happen, but let's fail gracefully (maybe a synchro issue with a dir being
		removed during hashing...)... */
		dcdebug("ShareManager::getFile: the file <%s> could not be found, strange!\n", realPath.c_str());
		return nullopt;
	}

	if(i->realPath && i->realPath == realPath) {
		return std::cref(*i);
	}

	/* see if the files sorted right before this one have a real path we are looking for. this is
	the most common case for dupes: "x (1).ext" is sorted before "x.ext". */
	auto real = i;
	while(real != d->files.begin()) {
		--real;
		if(!real->realPath) {
			break;
		}
		if(real->realPath == realPath) {
			return std::cref(*real);
		}
	}

	/* couldn't find it before the base file? maybe it's sorted after; could happen with files with
	no ext: "x (1)" is sorted after "x". */
	real = i;
	++real;
	while(real != d->files.end() && real->realPath) {
		if(real->realPath == realPath) {
			return std::cref(*real);
		}
		++real;
	}

	/* most common case: no duplicate; just return the base file. */
	return std::cref(*i);
}

void ShareManager::on(QueueManagerListener::FileMoved, const string& realPath) noexcept {
	if(!SETTING(ADD_FINISHED_INSTANTLY)) {
		return;
	}
	try {
		if(pft::isPrivatePath(realPath))
			return;
	} catch(...) {
		return;
	}

	MovedFile moved { realPath, -1, 0, nullopt };
	try {
		File completedFile(realPath, File::READ, File::OPEN | File::SHARED);
		moved.size = completedFile.getSize();
		moved.lastWrite = completedFile.getLastModified();
	} catch(const FileException&) {
		return;
	}
	{
		RLock l(cs);
		// Check if the finished download dir is supposed to be shared
		if(!getDirectory(realPath)) {
			return;
		}
	}

	// File system work stays outside the share lock.
	moved.tth = HashManager::getInstance()->getTTH(realPath, moved.size, static_cast<uint32_t>(moved.lastWrite));
	const auto parentPath = Util::getFilePath(realPath);
	time_t parentWrite = 0;
	FileFindIter parentInfo(parentPath.substr(0, parentPath.size() - 1));
	if(parentInfo != FileFindIter()) {
		parentWrite = static_cast<time_t>(parentInfo->getLastWriteTime());
	}

	WLock l(cs);
	if(addMovedFile(moved)) {
		auto dir = getDirectory(realPath);
		if(dir) {
			dir->setLastWrite((std::max)(dir->getLastWrite(), parentWrite));
		}
		if(recordScanHashes) {
			movedFiles.push_back(moved);
		}
	}
}

bool ShareManager::addMovedFile(const MovedFile& moved) {
	auto dir = getDirectory(moved.realPath);
	if(!dir) {
		return false;
	}

	// A download that replaced a shared file: drop the old entry and its index.
	auto existing = getFile(moved.realPath, dir);
	string existingPath;
	if(existing) {
		try {
			existingPath = existing->get().getRealPath();
		} catch(const ShareException&) { }
	}
	if(existing && Util::stricmp(existingPath, moved.realPath) == 0) {
		const auto& old = existing->get();
		if(old.getSize() == moved.size && old.getLastWrite() == moved.lastWrite && old.tth == moved.tth) {
			return true;
		}
		if(old.tth) {
			auto i = tthIndex.find(*old.tth);
			if(i != tthIndex.end() && i->second == &old) {
				tthIndex.erase(i);
				sharedBytes -= old.getSize();
			}
		}
		dir->files.erase(dir->files.find(old.getName()));
		++indexVersion;
	}

	// A finished download shows in the file list at once.
	++listForce;
	Directory::File f(Util::getFileName(moved.realPath), moved.size, dir, moved.tth, moved.lastWrite);
	auto root = dir.get();
	while(root->getParent()) {
		root = root->getParent();
	}
	f.validateName(Util::getFilePath(moved.realPath), getRealRoots(root->getName()).size() > 1);
	const auto& file = *dir->files.insert(move(f)).first;
	if(file.tth) {
		++indexVersion;
		auto i = tthIndex.find(*file.tth);
		if(i == tthIndex.end()) {
			dir->size += file.getSize();
			sharedBytes += file.getSize();
			tthIndex[*file.tth] = &file;
		} else if(SETTING(LIST_DUPES)) {
			tthIndex[*file.tth] = &file;
		}
		bloom.add(Text::toLower(file.getName()));
	}
	setDirty();
	return true;
}

void ShareManager::replayMovedFiles() {
	for(const auto& moved: movedFiles) {
		addMovedFile(moved);
	}
}

void ShareManager::on(HashManagerListener::TTHDone, const string& realPath, const TTHValue& root,
	int64_t size, uint32_t timestamp, uint64_t hashJobId) noexcept
{
	// Files in private storage are never shared, but a pending chat attachment still gets its answer.
	if(!pft::isPrivateLocation(realPath)) {
		try {
			// A hash completion belongs to one exact filesystem snapshot. The hasher holds a
			// write-denying handle on the file until its listeners have run, so its results
			// (job id != 0) describe the file as it is. Other completions are checked again
			// against the file, so an older one cannot assign its TTH to a replacement.
			bool current = hashJobId != 0;
			if(!current) {
				File snapshot(realPath, File::READ, File::OPEN);
				current = snapshot.getSize() == size && snapshot.getLastModified() == timestamp;
			}
			if(current) {
				WLock l(cs);
				applyHash(realPath, root, size, timestamp, false);
				if(recordScanHashes) {
					scanHashes.push_back({ realPath, root, size, timestamp });
				}
			}
		} catch(...) { }
	}
	completeChatAttachments(realPath, root, size, timestamp, hashJobId);
}

void ShareManager::applyHash(const string& realPath, const TTHValue& root, int64_t size, uint32_t timestamp, bool sameTimestamp) {
	auto f = getFile(realPath);
	if(!f || f->get().getSize() != size) {
		return;
	}
	auto& file = const_cast<Directory::File&>(f->get());
	// getFile falls back to the file of that name; it must be this file on disk.
	if(file.realPath && Util::stricmp(*file.realPath, realPath) != 0) {
		return;
	}
	if(file.tth && *file.tth == root && (sameTimestamp || file.getLastWrite() == static_cast<time_t>(timestamp))) {
		return;
	}
	if(sameTimestamp && file.getLastWrite() != static_cast<time_t>(timestamp)) {
		return;
	}
	const bool wasHashed = static_cast<bool>(file.tth);
	++indexVersion;
	if(file.tth && root != *file.tth) {
		// Only this file's own entry: another file may share the old root.
		auto i = tthIndex.find(*file.tth);
		if(i != tthIndex.end() && i->second == &file) {
			tthIndex.erase(i);
			sharedBytes -= file.getSize();
		}
	}
	file.tth = root;
	file.setLastWrite(timestamp);
	auto i = tthIndex.find(root);
	if(i == tthIndex.end()) {
		tthIndex.emplace(root, &file);
		sharedBytes += file.getSize();
		if(!wasHashed) {
			file.getParent()->size += file.getSize();
		}
	} else if(SETTING(LIST_DUPES)) {
		i->second = &file;
	} else {
		// As a refresh does (updateIndices): a duplicate isn't shared.
		LogManager::getInstance()->message(str(F_("Duplicate file will not be shared: %1% (Size: %2% B) Dupe matched against: %3%")
			% Util::addBrackets(realPath) % Util::toString(file.getSize()) % Util::addBrackets(i->second->getRealPath())),
			LogMessage::SEV_WARNING, _("Sharing"));
		auto parent = file.getParent();
		parent->files.erase(parent->files.find(file.getName()));
		setDirty();
		return;
	}

	// A new hash makes the file list stale but doesn't force an immediate rebuild of it: while a
	// large share hashes, that would rebuild the whole list for nearly every list request.
	setDirty();

	bloom.add(Text::toLower(Util::getFileName(realPath)));
}

void ShareManager::replayScanHashes() {
	for(const auto& hash: scanHashes) {
		applyHash(hash.realPath, hash.root, hash.size, hash.timestamp, true);
	}
}

void ShareManager::on(HashManagerListener::TTHFailed, const string& realPath, int64_t size,
	uint32_t timestamp, uint64_t hashJobId, HashManagerListener::Failure, const string& detail) noexcept
{
	failChatAttachments(realPath, size, timestamp, hashJobId, detail);
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t tick) noexcept {
	{
		WLock l(cs);
		tempShares.erase(std::remove_if(tempShares.begin(), tempShares.end(),
			[tick](const TempShareInfo& item) { return tempShareExpired(item, tick); }), tempShares.end());
	}
	// Timed from the end of the last full refresh, and never sooner than that refresh took, so a
	// share that takes longer to scan than the interval isn't rescanned back to back. A refresh in
	// progress is left alone rather than reported every minute.
	if(SETTING(AUTO_REFRESH_TIME) > 0 && !refreshActive) {
		const auto interval = std::max<uint64_t>(static_cast<uint64_t>(SETTING(AUTO_REFRESH_TIME)) * 60 * 1000, lastRefreshDuration);
		if(lastFullUpdate + interval <= tick) {
			refresh(true, true);
		}
	}
}

} // namespace dcpp
