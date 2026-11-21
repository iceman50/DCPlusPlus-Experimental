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

#ifndef DCPLUSPLUS_DCPP_SHARE_MANAGER_H
#define DCPLUSPLUS_DCPP_SHARE_MANAGER_H

#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/hash/HashManagerListener.h>
#include <dcpp/queue/QueueManagerListener.h>

#include <dcpp/core/classes/Exception.h>
#include <dcpp/core/thread/CriticalSection.h>
#include <dcpp/core/thread/SharedCriticalSection.h>
#include <dcpp/util/text/StringSearch.h>
#include <dcpp/core/Singleton.h>
#include <dcpp/core/classes/BloomFilter.h>
#include <dcpp/core/classes/FastAlloc.h>
#include <dcpp/hash/value/MerkleTree.h>
#include <dcpp/core/classes/Pointer.h>
#include <dcpp/util/text/StringMatch.h>
#include <dcpp/user/CID.h>

namespace dcpp {

using std::function;
using std::map;
using std::nullopt;
using std::optional;
using std::set;
using std::unique_ptr;
using std::unordered_map;

STANDARD_EXCEPTION(ShareException);

class SimpleXML;
class Client;
class File;
class OutputStream;
class MemoryInputStream;
class SQLiteDB;
class SQLiteStatement;

struct ShareLoader;
namespace pft { class PrivatePathScanner; }
class ShareManager : public Singleton<ShareManager>, private SettingsManagerListener, private Thread, private TimerManagerListener,
	private HashManagerListener, private QueueManagerListener
{
public:
	struct TempShareInfo {
		string realPath;
		string hubUrl;
		TTHValue tth;
		int64_t size;
		uint32_t timestamp;
		/** Exact-only entries are protocol documents and never match name/size searches. */
		bool exactOnly;
		/** Monotonic time of the most recent addition to a chat draft. */
		uint64_t added = 0;
		/** The one user a private-chat attachment is for; zero for everyone on the hub.
		 * Such entries never answer searches and upload only to that user. */
		CID recipient = CID();
	};
	struct ChatAttachmentInfo {
		string realPath;
		string hubUrl;
		TTHValue tth;
		int64_t size;
		uint32_t timestamp;
		bool temporary;
		CID recipient = CID();
	};
	struct ChatAttachmentResult {
		uint64_t requestId;
		optional<ChatAttachmentInfo> attachment;
		string error;
	};
	using ChatAttachmentCallback = function<void (ChatAttachmentResult)>;
	struct ResolvedFileInfo {
		string realPath;
		int64_t size;
		optional<TTHValue> tth;
		bool temporary;
		uint32_t timestamp;
		/** Distinguishes route-scoped protocol documents from temporary chat attachments. */
		bool exactOnly;
	};

	/**
	 * @param aDirectory Physical directory location
	 * @param aName Virtual name
	 */
	void addDirectory(const string& realPath, const string &virtualName);
	void removeDirectory(const string& realPath);
	void renameDirectory(const string& realPath, const string& virtualName);

	string toVirtual(const TTHValue& tth) const;
	/* The functions below that take a hub and a user answer that user on that hub.
	 * A temporary share for one private-chat recipient is visible only when the user
	 * is that recipient; the default (zero) user sees only shares for the whole hub. */
	string toVirtual(const TTHValue& tth, const string& hubUrl, const CID& user = CID()) const;
	optional<TTHValue> getTTHFromReal(const string& realPath) noexcept;
	/** Register a transient, TTH-only file for one chat route. Temporary files
	 * are searchable and uploadable only on that route, but never appear in file lists. */
	bool addTempShare(const string& realPath, int64_t size, uint32_t timestamp,
		const TTHValue& tth, const string& hubUrl, const CID& recipient = CID()) noexcept;
	/** Register a verified protocol document that is visible only to exact-TTH searches/transfers. */
	bool addTTHOnlyShare(const string& realPath, int64_t size, uint32_t timestamp,
		const TTHValue& tth, const string& hubUrl) noexcept;
	bool removeTTHOnlyShare(const string& realPath, const TTHValue& tth,
		const string& hubUrl) noexcept;
	bool isTempShare(const TTHValue& tth, const string& realPath, const string& hubUrl,
		const CID& recipient = CID()) const noexcept;
	vector<TempShareInfo> getTempShares() const;
	bool removeTempShare(const string& realPath, const TTHValue& tth, const string& hubUrl,
		const CID& recipient = CID()) noexcept;
	size_t clearTempShares() noexcept;
	/** Prepare a local file as an exact-TTH attachment for one chat route. The
	 * callback may run immediately or on the hashing thread and is called exactly
	 * once unless the request is cancelled. */
	uint64_t prepareChatAttachment(const string& realPath, const string& hubUrl,
		ChatAttachmentCallback callback) noexcept;
	/** As above, for a private chat: a temporary share is only for the recipient. */
	uint64_t prepareChatAttachment(const string& realPath, const string& hubUrl,
		const CID& recipient, ChatAttachmentCallback callback) noexcept;
	void cancelChatAttachment(uint64_t requestId) noexcept;
	/** Verify that an attachment is currently visible and, for temporary entries,
	 * still matches its saved file snapshot and TTH. */
	bool validateChatAttachment(const TTHValue& tth, int64_t size, const string& hubUrl,
		const CID& user = CID()) const noexcept;
	string toReal(const string& virtualFile);
	string toReal(const string& virtualFile, const string& hubUrl, const CID& user = CID());
	/** @return Actual file path & size. Returns 0 for file lists. */
	pair<string, int64_t> toRealWithSize(const string& virtualFile);
	pair<string, int64_t> toRealWithSize(const string& virtualFile, const string& hubUrl, const CID& user = CID());
	/** Resolve an upload candidate and atomically retain whether it came from the
	 * route-scoped temporary-share table. */
	ResolvedFileInfo resolveFile(const string& virtualFile, const string& hubUrl, const CID& user = CID());
	StringList getRealPaths(const string& virtualPath);
	/** Return all currently shared real paths for a TTH that are visible from the requesting hub. */
	StringList getRealPaths(const TTHValue& tth, const string& hubUrl, const CID& user = CID()) const;
	optional<TTHValue> getTTH(const string& virtualFile) const;
	optional<TTHValue> getTTH(const string& virtualFile, const string& hubUrl, const CID& user = CID()) const;

	void refresh(bool dirs = false, bool aUpdate = true, bool block = false, function<void (float)> progressF = nullptr) noexcept;
	/** Load a validated cached share tree when possible, then refresh the real filesystem in the background. */
	void startupRefresh(function<void (float)> progressF = nullptr) noexcept;
	/** True while a share refresh is building or applying a live filesystem view. */
	bool isRefreshing() const noexcept { return refreshActive; }
	/** False while the share index is the startup cache and the full scan that reconciles it
	 * with the filesystem has not finished; the index may still list deleted files. */
	bool isShareVerified() const noexcept { return !cacheUnverified; }
	/** True while added share folders are still waiting for, or in, their background scan. */
	bool hasPendingDirectories() const noexcept;
	/** True while this share root is waiting for, or in, its background scan. */
	bool isDirectoryPending(const string& realPath) const noexcept;
	/** The share changed: file lists are rebuilt at most every 15 minutes (or when forced). */
	void setDirty() { ++listVersion; }

	SearchResultList search(const StringList& adcParams, size_t maxResults) noexcept;
	SearchResultList search(const StringList& adcParams, size_t maxResults, const string& hubUrl) noexcept;
	SearchResultList search(const string& nmdcString, int searchType, int64_t size, int fileType, size_t maxResults) noexcept;
	SearchResultList search(const string& nmdcString, int searchType, int64_t size, int fileType, size_t maxResults, const string& hubUrl) noexcept;

	StringPairList getDirectories() const noexcept;

	MemoryInputStream* generatePartialList(const string& dir, bool recurse) const;
	MemoryInputStream* generatePartialList(const string& dir, bool recurse, const string& hubUrl) const;
	MemoryInputStream* getTree(const string& virtualFile) const;
	MemoryInputStream* getTree(const string& virtualFile, const string& hubUrl, const CID& user = CID()) const;
	MemoryInputStream* generateFileList(const string& hubUrl, bool compressed);
	std::pair<std::unique_ptr<InputStream>, int64_t> openXmlList(const string& hubUrl);
	bool hasCustomShare(const string& hubUrl) const;

	AdcCommand getFileInfo(const string& aFile);
	AdcCommand getFileInfo(const string& aFile, const string& hubUrl, const CID& user = CID());

	int64_t getShareSize() const noexcept;
	int64_t getShareSizeForHub(const string& hubUrl) const;
	int64_t getShareSize(const string& realPath) const noexcept;

	size_t getSharedFiles() const noexcept;
	size_t getSharedFiles(const string& hubUrl) const;

	string getShareSizeString() const { return std::to_string(getShareSize()); }
	string getShareSizeString(const string& aDir) const { return std::to_string(getShareSize(aDir)); }

	void getBloom(ByteVector& v, size_t k, size_t m, size_t h) const;
	void getBloom(ByteVector& v, size_t k, size_t m, size_t h, const string& hubUrl) const;

	SearchManager::TypeModes getType(const string& fileName) const noexcept;

	string validateVirtual(const string& /*aVirt*/) const noexcept;
	bool hasVirtual(const string& name) const noexcept;

	/** Searches run in parallel (under the shared lock), so the counter is atomic. */
	void addHits(uint32_t aHits) {
		hits += aHits;
	}
	uint32_t getHits() const noexcept { return hits; }
	void setHits(uint32_t aHits) noexcept { hits = aHits; }

	string getOwnListFile() {
		return getList(ShareAccess { true, { } }).path;
	}
	/** The compressed file list a hub's users get (the full list, or the hub's share profile). */
	string getListFile(const string& hubUrl);
	/** Whether the path is one of the file lists generated for this share. */
	bool isOwnListFile(const string& path) const;

	bool isTTHShared(const TTHValue& tth){
		RLock l(cs);
		return tthIndex.find(tth) != tthIndex.end();
	}

	void updateFilterCache();

private:
	std::atomic<uint32_t> hits;

	struct SearchQuery;

	struct ShareAccess {
		bool unrestricted = false;
		std::set<string> directories;
	};

	class Directory : public FastAlloc<Directory>, public intrusive_ptr_base<Directory> {
	public:
		typedef intrusive_ptr<Directory> Ptr;

		Directory(const Directory&) = delete;
		Directory& operator=(const Directory&) = delete;

		struct File {
			File() : size(0), lastWrite(0), parent(0) { }
			File(const string& aName, int64_t aSize, const Directory::Ptr& aParent, const optional<TTHValue>& aRoot,
				time_t aLastWrite = 0) :
				name(aName), tth(aRoot), size(aSize), lastWrite(aLastWrite), parent(aParent.get()) { }

			bool operator==(const File& rhs) const {
				return getParent() == rhs.getParent() && (Util::stricmp(getName(), rhs.getName()) == 0);
			}

			/** Transparent, so a name looks a file up in O(log n) instead of a linear scan; the
			match is the same case-insensitive equality the set already orders by. */
			struct FileLess {
				using is_transparent = void;
				bool operator()(const File& a, const File& b) const { return (Util::stricmp(a.getName(), b.getName()) < 0); }
				bool operator()(const File& a, const string& b) const { return (Util::stricmp(a.getName(), b) < 0); }
				bool operator()(const string& a, const File& b) const { return (Util::stricmp(a, b.getName()) < 0); }
			};

			/** Ensure this file's virtual name doesn't clash with the names of the parent directory's
			sub-directories or files; rename to "file (N).ext" otherwise. The real path is stored when
			it can't be derived from the directories: for a renamed file, or with keepRealPath.
			@param sourcePath Real path (on the disk) of the directory this file came from. */
			void validateName(const string& sourcePath, bool keepRealPath = false);

			string getADCPath() const { return parent->getADCPath() + name; }
			string getFullName() const { return parent->getFullName() + name; }
			string getRealPath() const { return realPath ? *realPath : parent->getRealPath(name); }

			GETSET(string, name, Name);
			optional<string> realPath; // Exact path on disk, when getRealPath() can't derive it (renamed files, merged names).
			optional<TTHValue> tth;
			GETSET(int64_t, size, Size);
			GETSET(time_t, lastWrite, LastWrite);
			GETSET(Directory*, parent, Parent);
		};

		int64_t size;
		unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> directories;
		set<File, File::FileLess> files;

		static Ptr create(const string& aName, const Ptr& aParent = Ptr(), time_t aLastWrite = 0) {
			return Ptr(new Directory(aName, aParent, aLastWrite));
		}

		const string& getRealName() const noexcept;
		const optional<string>& getRealNameOverride() const noexcept { return realName; }
		template<typename SetT> void setRealName(SetT&& realName) noexcept { this->realName = std::forward<SetT>(realName); }

		string getADCPath() const noexcept;
		string getFullName() const noexcept;
		string getRealPath(const std::string& path) const;

		/** Check whether the given name would clash with this directory's sub-directories or
		files. */
		bool nameInUse(const string& name) const;

		int64_t getSize() const noexcept;

		void search(SearchResultList& results, SearchQuery& query, size_t maxResults) const noexcept;

		/// @param level -1 to include all levels, or the current level.
		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, int8_t level, bool addFileDates) const;
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDates) const;

		auto findFile(const string& aFile) const -> decltype(files.cbegin()) { return files.find(aFile); }

		void merge(const Ptr& source, const string& realPath);

		GETSET(string, name, Name);
		GETSET(Directory*, parent, Parent);
		GETSET(time_t, lastWrite, LastWrite);

	private:
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);

		Directory(const string& aName, const Ptr& aParent, time_t aLastWrite);
		~Directory() { }

		optional<string> realName; // only defined if this directory had to be renamed to avoid duplication.
	};

	friend class Directory;
	friend struct ShareLoader;

	friend class Singleton<ShareManager>;
	ShareManager();

	virtual ~ShareManager();

	struct SearchQuery {
		SearchQuery();
		SearchQuery(const StringList& adcParams);
		SearchQuery(const string& nmdcString, int searchType, int64_t size, int fileType);

		bool isExcluded(const string& str);
		bool hasExt(const string& name);

		StringSearch::List* include;
		StringSearch::List includeInit;
		StringSearch::List exclude;
		StringList ext;
		StringList noExt;

		int64_t gt;
		int64_t lt;

		optional<TTHValue> root;

		bool isDirectory;
	};

	/** A generated file list: the full list (key "") or one per share profile. */
	struct FileList {
		string path; ///< bzip2-compressed
		std::shared_ptr<File> ref; ///< Keeps the current list open
		int64_t xmlLen = 0;
		int64_t bzLen = 0;
		optional<TTHValue> xmlRoot;
		optional<TTHValue> bzRoot;
		uint64_t dirtyVersion = 0; ///< listVersion it was built from
		uint64_t forceVersion = 0; ///< listForce it was built from
		uint64_t generated = 0;
	};
	/** By profile key; guarded by fileListsCs. Generation is serialized by listCs, taken before cs. */
	unordered_map<string, FileList> fileLists;
	/** Guards fileLists only; never held while taking another lock. */
	mutable FastCriticalSection fileListsCs;
	mutable CriticalSection listCs;
	/** List files replaced while still open (being uploaded); guarded by listCs. */
	StringList staleListFiles;
	/** Changed without the share lock by setDirty() (settings pages). */
	std::atomic<uint64_t> listVersion { 1 };
	std::atomic<uint64_t> listForce { 1 };
	/** The list for a hub's view, rebuilt first when needed. Never call with cs held. */
	FileList getList(const ShareAccess& access);
	AdcCommand getListInfo(const string& aFile, const ShareAccess& access);
	static string getProfileKey(const ShareAccess& access);
	void removeStaleLists() noexcept;
	void writeListXml(OutputStream& os, const ShareAccess& access) const;
	void writeDirectoryXml(OutputStream& os, const Directory::Ptr& dir, string& indent, string& tmp) const;
	bool refreshDirs;
	bool update;

	int listN;

	static std::atomic_flag refreshing;
	static std::atomic<bool> refreshActive;
	std::atomic<bool> cacheUnverified { false };

	std::atomic<uint64_t> lastFullUpdate;

	/** The share lock. Searches, uploads, file lists and size queries hold it shared, so they run
	 * in parallel; changes to the share (hash results, refresh swaps, finished downloads, temporary
	 * shares, share settings) hold it exclusively. A thread holding it shared must never ask for it
	 * exclusively (see SharedCriticalSection). */
	mutable SharedCriticalSection cs;

	// List of root directory items
	unordered_map<string, Directory::Ptr, noCaseStringHash, noCaseStringEq> directories;

	/** Map real name to virtual name - multiple real names may be mapped to a single virtual one.
	The map is sorted to make sure conflicts are always resolved in the same order when merging. */
	map<string, string> shares;
	/** Share roots registered but not yet scanned. They are scanned by the refresh thread, so
	 * adding a large folder doesn't block the caller (the settings dialog). Guarded by cs. */
	StringList pendingDirectories;
	string scanningDirectory;
	bool scanPendingDirectories();
	void startPendingScan() noexcept;

	/** A hash that finished while a refresh was scanning. Hashing keeps running during scans, so
	 * a scan can read a file before its hash is done and then replace the tree the hash updated. */
	struct ScanHash {
		string realPath;
		TTHValue root;
		int64_t size;
		uint32_t timestamp;
	};
	/** Recorded while recordScanHashes is set; replayed onto each tree a refresh swaps in. Guarded by cs. */
	vector<ScanHash> scanHashes;
	bool recordScanHashes = false;
	/** Give the shared file at realPath its hash. With sameTimestamp, skip a file whose
	 * timestamp differs, as the file then changed after it was hashed. Requires cs. */
	void applyHash(const string& realPath, const TTHValue& root, int64_t size, uint32_t timestamp, bool sameTimestamp);
	void replayScanHashes();

	/** A finished download added to the share while a refresh was scanning (ADD_FINISHED_INSTANTLY);
	 * the scanned tree may predate it, so it is added again after each swap. Guarded by cs. */
	struct MovedFile {
		string realPath;
		int64_t size;
		time_t lastWrite;
		optional<TTHValue> tth;
	};
	vector<MovedFile> movedFiles;
	/** Add a file to the live tree if its directory is shared and it isn't there yet. Requires cs. */
	bool addMovedFile(const MovedFile& file);
	void replayMovedFiles();

	/** Rebuild the indices after a tree swap and re-apply what changed during the scan. Requires cs. */
	void afterTreeChange();
	/** Swap in a virtual name's new tree, updating the indices for that name only. Requires cs. */
	void replaceTree(const string& virtualName, const Directory::Ptr& tree);
	/** Remove a tree's files from the TTH index. Requires cs. */
	void unindexTree(const Directory& dir);
	/** Set by replaceTree: duplicates are left unindexed rather than removed. Guarded by cs. */
	bool deferDuplicates = false;
	/** Real folders shared under a virtual name, in share order. Requires cs. */
	StringList getRealRoots(const string& virtualName) const;
	/** Scan one share root into tree_ (merged when tree_ already holds another root of the same
	 * virtual name). @return false when the root couldn't be scanned completely (unavailable, or
	 * stopping) and its previous contents should be kept. */
	bool scanRoot(const pft::PrivatePathScanner& privacy, const string& realPath, const string& virtualName,
		Directory::Ptr& tree_);

	/** Set when shutting down: scans stop at the next directory entry and their results are dropped. */
	std::atomic<bool> stopping { false };
	/** Length of the last full refresh; the next automatic one waits at least this long. */
	std::atomic<uint64_t> lastRefreshDuration { 0 };

	unordered_map<TTHValue, const Directory::File*> tthIndex;
	/** Sum of the sizes in tthIndex, kept as it changes. Guarded by cs. */
	int64_t sharedBytes = 0;
	/** Changes whenever tthIndex does. Guarded by cs. */
	uint64_t indexVersion = 1;
	struct ShareTotals {
		uint64_t version = 0;
		int64_t size = 0;
		size_t files = 0;
	};
	/** By profile key, valid while version == indexVersion. Guarded by totalsCs (filled under the
	 * shared share lock, so several readers may update it at once). */
	mutable unordered_map<string, ShareTotals> profileTotals;
	mutable FastCriticalSection totalsCs;
	ShareTotals getTotals(const ShareAccess& access) const;

	vector<TempShareInfo> tempShares;
	static bool tempShareExpired(const TempShareInfo& item, uint64_t tick) noexcept;
	struct PendingChatAttachment {
		uint64_t requestId;
		string realPath;
		string hubUrl;
		int64_t size;
		uint32_t timestamp;
		uint64_t generation;
		uint64_t hashJobId;
		ChatAttachmentCallback callback;
		CID recipient = CID();
	};
	enum class ChatAttachmentRequestState { READY, CANCELLED, CLEARED };
	mutable CriticalSection chatAttachmentCs;
	unordered_map<uint64_t, PendingChatAttachment> pendingChatAttachments;
	std::unordered_set<uint64_t> activeChatAttachments;
	std::unordered_set<uint64_t> cancelledChatAttachments;
	uint64_t chatAttachmentGeneration = 0;
	std::atomic<uint64_t> nextChatAttachmentRequest { 1 };

	BloomFilter<5> bloom;

	std::list<StringMatch> cachedFilterSkiplistRegEx;
	std::list<StringMatch> cachedFilterSkiplistFileExtensions;
	std::list<StringMatch> cachedFilterSkiplistPaths;

	const Directory::File& findFile(const string& virtualFile) const;
	const TempShareInfo* findTempShare(const TTHValue& tth, const string* hubUrl = nullptr,
		const bool* exactOnly = nullptr, const CID& user = CID()) const noexcept;
	/** Whether a temporary share may be served to this user (see TempShareInfo::recipient). */
	static bool tempShareVisibleTo(const TempShareInfo& item, const CID& user) noexcept;
	bool addValidatedTempShare(const string& realPath, int64_t size, uint32_t timestamp,
		const TTHValue& tth, const string& hubUrl, bool exactOnly = false,
		const CID& recipient = CID()) noexcept;
	void finishChatAttachment(PendingChatAttachment request, const TTHValue& tth) noexcept;
	void failChatAttachments(const string& realPath, int64_t size, uint32_t timestamp,
		uint64_t hashJobId, const string& error) noexcept;
	void completeChatAttachments(const string& realPath, const TTHValue& tth,
		int64_t size, uint32_t timestamp, uint64_t hashJobId) noexcept;
	static void invokeChatAttachmentCallback(PendingChatAttachment&& request,
		optional<ChatAttachmentInfo> attachment, string error) noexcept;
	void deliverChatAttachmentCallback(PendingChatAttachment&& request,
		optional<ChatAttachmentInfo> attachment, string error) noexcept;
	ChatAttachmentRequestState getChatAttachmentRequestState(uint64_t requestId,
		uint64_t generation) const noexcept;
	ChatAttachmentRequestState beginChatAttachmentCallback(uint64_t requestId,
		uint64_t generation) noexcept;
	ResolvedFileInfo resolveFileLocked(const string& virtualFile, const string& hubUrl, const CID& user);
	/** @throw ShareException when the file carries the Private Send marker. */
	static void checkNotMarked(const string& realPath);
	ShareAccess getShareAccess(const string& hubUrl) const;
	bool isVirtualAllowed(const string& virtualName, const ShareAccess& access) const;
	bool isFileAllowed(const Directory::File& file, const ShareAccess& access) const;
	string generateFileListData(const string& hubUrl, bool compressed);

	Directory::Ptr buildTree(const string& realPath, optional<std::reference_wrapper<const string>> dirName = nullopt,
		const Directory::Ptr& parent = nullptr, time_t lastWrite = 0);
	/** @param keepRealPaths Store each file's real path: the virtual name is shared from several folders. */
	Directory::Ptr scanTree(const pft::PrivatePathScanner& privacy, const string& realPath,
		optional<std::reference_wrapper<const string>> dirName, const Directory::Ptr& parent, time_t lastWrite, bool keepRealPaths);
	/** Store the real path of every file under dir that doesn't have one. */
	static void keepRealPaths(Directory& dir, const string& dirRealPath);
	bool checkHidden(const string& realPath) const;
	bool checkInvalidFileName(const string& realPath) const;
	bool checkInvalidPaths(const string& realPath) const;
	bool checkInvalidFileSize(uint64_t size) const;
	bool checkRegEx(const StringMatch& matcher, const string& match) const;

	void updateFilterCache(const std::string& strSetting, std::list<StringMatch>& lst);
	void updateFilterCache(const std::string& strSetting, const std::string& strExtraPattern, bool escapeDot, std::list<StringMatch>& lst);

	/** Rebuild TTH and bloom indexes after replacing the share tree; expectedFiles pre-sizes large TTH maps. */
	void rebuildIndices(size_t expectedFiles = 0);

	void updateIndices(Directory& aDirectory);
	void updateIndices(Directory& dir, const decltype(std::declval<Directory>().files.begin())& i);

	void merge(const Directory::Ptr& directory, const string& realPath);

	void generateXmlList();
	pair<Directory::Ptr, string> splitVirtual(const string& virtualPath) const;
	string findRealRoot(const string& virtualRoot, const string& virtualLeaf) const;

	SearchResultList search(SearchQuery&& query, size_t maxResults) noexcept;
	SearchResultList search(SearchQuery&& query, size_t maxResults,
		const ShareAccess& access, const string& hubUrl) noexcept;
	void appendTempSearchResults(SearchResultList& results, SearchQuery& query,
		size_t maxResults, const string& hubUrl) noexcept;

	/** Get the directory pointer corresponding to a given real path (on disk). Note that only
	directories are considered here but not the file's base name. */
	Directory::Ptr getDirectory(const string& realPath) noexcept;
	/** Get the file corresponding to a given real path (on disk). */
	optional<std::reference_wrapper<const ShareManager::Directory::File>> getFile(const string& realPath, Directory::Ptr d = nullptr) noexcept;

	virtual int run();
	void runRefresh(function<void (float)> progressF = nullptr);

	/** Return the per-profile SQLite snapshot path for cached share metadata. */
	string getShareCacheFile() const;
	/** Hash the configured shares and share-affecting settings used to validate a snapshot. */
	string getShareCacheFingerprint() const;
	/** Try to load a complete, validated share snapshot without changing state on failure. */
	bool loadShareCache() noexcept;
	/** Persist the current in-memory share tree as one transaction for the next startup. */
	void saveShareCache() noexcept;
	/** Write the share-cache snapshot to cacheFile; throws on failure. */
	void writeShareCache(const string& cacheFile);
	/** Create or upgrade the share-cache schema; destructive migrations should use a new schema version. */
	void createShareCacheSchema(SQLiteDB& db);
	/** Recursively write one directory subtree using stable parent ids for fast parent-ordered loading. */
	void saveShareCacheDirectory(SQLiteStatement& dirStmt, SQLiteStatement& fileStmt, const Directory::Ptr& dir,
		optional<int64_t> parentId, int64_t& nextId, uint64_t& directoryCount, uint64_t& fileCount) const;

	// QueueManagerListener
	virtual void on(QueueManagerListener::FileMoved, const string& realPath) noexcept;

	// HashManagerListener
	virtual void on(HashManagerListener::TTHDone, const string& realPath, const TTHValue& root,
		int64_t size, uint32_t timestamp, uint64_t hashJobId) noexcept;
	virtual void on(HashManagerListener::TTHFailed, const string& realPath, int64_t size,
		uint32_t timestamp, uint64_t hashJobId, HashManagerListener::Failure reason,
		const string& detail) noexcept;

	// SettingsManagerListener
	virtual void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
		save(xml);
	}
	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
		load(xml);
	}

	// TimerManagerListener
	virtual void on(TimerManagerListener::Minute, uint64_t tick) noexcept;
	void load(SimpleXML& aXml);
	void save(SimpleXML& aXml);

};

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
