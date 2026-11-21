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

#ifndef DCPLUSPLUS_DCPP_HASH_MANAGER_H
#define DCPLUSPLUS_DCPP_HASH_MANAGER_H

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <atomic>

#include <optional>

#include <dcpp/core/Singleton.h>
#include <dcpp/hash/value/MerkleTree.h>
#include <dcpp/core/thread/Thread.h>
#include <dcpp/core/thread/CriticalSection.h>
#include <dcpp/core/thread/SemaphoreDCpp.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/hash/HashManagerListener.h>
#include <dcpp/core/types/GetSet.h>
#include <dcpp/core/io/db/SQLiteDB.h>

namespace dcpp {

using std::function;
using std::map;

using std::optional;

STANDARD_EXCEPTION(HashException);

class HashLoader;
class File;

class HashManager : public Singleton<HashManager>, public Speaker<HashManagerListener>,
	private TimerManagerListener
{
public:

	/** We don't keep leaves for blocks smaller than this... */
	static const int64_t MIN_BLOCK_SIZE;

	HashManager() {
		hashers.push_back(std::make_unique<Hasher>(nextJobId, 0));
		primary = hashers.front().get();
		TimerManager::getInstance()->addListener(this);
	}
	virtual ~HashManager() {
		TimerManager::getInstance()->removeListener(this);
		hashStoreMaintenance.join();
		joinHashers();
	}

	/** Get the TTH root associated with the filename if its tree is current. */
	optional<TTHValue> getTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) noexcept;
	struct TTHRequest {
		optional<TTHValue> tth;
		uint64_t jobId;
		/** False for a stored root that predates the Private Send marker check (see setMarkerChecked). */
		bool markerChecked = true;
	};
	/** Return the current root or the exact hash-job cohort that will produce it. */
	TTHRequest requestTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) noexcept;
	/** The stored root for an unchanged file, without queueing a hash when there is none. */
	optional<TTHValue> findTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) noexcept;
	/** Record that a stored file was found without the Private Send marker. Files hashed by this
	 * version are checked when hashed; older records are checked once by the next share scan. */
	void setMarkerChecked(const string& aFileName) noexcept;
	/** Register a file whose content is already proven by a stored full tree (a finished,
	 * tree-verified download), so it isn't hashed again. @return false when the tree isn't stored
	 * or the file doesn't match it. */
	bool addVerifiedFile(const string& aFileName, const TTHValue& aRoot) noexcept;
	/**
	 * Hash a file synchronously and verify that it still matches the advertised root.
	 * Used for upload requests during share refreshes, where the cached virtual path
	 * may briefly point at stale or moved filesystem data.
	 */
	bool verifyFileTTH(const string& aFileName, int64_t aSize, const TTHValue& root) noexcept;
	/** Verify an already-open file handle and restore its original position. */
	bool verifyFileTTH(File& file, int64_t aSize, const TTHValue& root) noexcept;

	void stopHashing(const string& baseDir);
	void setPriority(Thread::Priority p);

	bool getTree(const TTHValue& root, TigerTree& tt);

	/** Return block size of the tree associated with root, or 0 if no such tree is in the store */
	int64_t getBlockSize(const TTHValue& root);

	bool addTree(const TigerTree& tree) { Lock l(cs); return store.addTree(tree); }

	/** Totals of every hasher; curFile is the file one of them is hashing. */
	void getStats(string& curFile, uint64_t& bytesLeft, size_t& filesLeft) const;

	/**
	 * Rebuild hash data file
	 */
	void rebuild() { primary->scheduleRebuild(); }
	using HashStoreMaintenanceCallback = function<void (bool, const string&)>;
	bool verifyHashStore(bool fullCheck = false);
	void optimizeHashStore();
	void compactHashStore();
	void verifyHashStoreAsync(bool fullCheck, HashStoreMaintenanceCallback callback);
	void optimizeHashStoreAsync(HashStoreMaintenanceCallback callback);
	void compactHashStoreAsync(HashStoreMaintenanceCallback callback);

	void startup(function<void (float)> progressF) {
		hashStoreMaintenance.start();
		startHashers();
		store.load(progressF);
	}

	void shutdown() {
		hashStoreMaintenance.shutdown();
		shutdownHashers();
		hashStoreMaintenance.join();
		joinHashers();
		Lock l(cs);
		store.save();
	}

	struct HashPauser {
		HashPauser();
		~HashPauser();
	};

	/** Hash pausing & resuming for GUI purposes. No more nr. of resume calls than pause */
	/// @return whether hashing was already paused
	bool pauseHashing() noexcept;
	void resumeHashing() noexcept;
	bool isHashingPaused() const noexcept;

private:
	class HashStoreMaintenanceWorker : public Thread {
	public:
		HashStoreMaintenanceWorker() : stop(false) { }

		void schedule(function<void ()> task);
		void shutdown();

	private:
		int run() override;

		std::deque<function<void ()>> tasks;
		CriticalSection cs;
		Semaphore s;
		bool stop;
	};

	/** Reads and hashes queued files one at a time. There is one per drive (or network share) being
	 * hashed, up to MAX_HASH_VOLUMES, so drives are read in parallel and each one sequentially. */
	class Hasher : public Thread {
	public:
		/** @param jobIds Shared by all hashers, so job ids are unique across them.
		 * @param initialPaused Pause count to start with (hashing is paused when it's created). */
		Hasher(std::atomic<uint64_t>& jobIds, unsigned initialPaused) : jobIds(jobIds), stop(false), running(false),
			idle(true), paused(initialPaused), rebuild(false), currentSize(0), currentFileSize(0), currentTimestamp(0),
			currentJobId(0), currentTerminal(false), queuedBytes(0), queuedFiles(0) { }

		/** @param volume getVolume(fileName); counted until the file has been hashed or dropped. */
		uint64_t hashFile(const string& fileName, int64_t size, uint32_t timestamp, const string& volume) noexcept;
		/** Whether a file on this volume is queued here or being hashed here. */
		bool hasVolume(const string& volume) const noexcept;
		/** Nothing queued and nothing being hashed. */
		bool isIdle() const noexcept;
		/** Bytes queued or still to read of the current file. */
		uint64_t getLoad() const noexcept;

		/// @return whether hashing was already paused
		bool pause() noexcept;
		void resume() noexcept;
		bool isPaused() const noexcept;

		void stopHashing(const string& baseDir);
		virtual int run();
		/**
		 * Hash one validated file snapshot with bounded parallel TTH leaf workers.
		 *
		 * File bytes are read and observed in order on this Hasher thread so speed
		 * limiting, pause/cancellation checks, and optional SFV CRC32 processing
		 * remain serialized. TigerTreeHasher owns and joins every worker before
		 * returning; the output tree is replaced only after the expected byte count
		 * and all ordered leaves have been verified.
		 *
		 * @param file Open file positioned at the first byte to hash. The caller
		 *             keeps the handle alive through result publication.
		 * @param tth Receives the completed tree on success and supplies its block size.
		 * @param size Exact number of bytes expected from the file snapshot.
		 * @param xcrc32 Optional ordered SFV CRC32 accumulator; may be null.
		 * @param workerCount Already-bounded number of TTH worker threads.
		 * @param lastRead Running speed-limit timestamp, updated as bytes are read.
		 * @return true only when all expected bytes and leaves were processed;
		 *         false for orderly cancellation or a size mismatch.
		 * @throw FileException for read failures. Worker and callback exceptions are
		 *        propagated only after all workers have joined.
		 */
		bool fastHash(File& file, TigerTree& tth, int64_t size, CRC32Filter* xcrc32, size_t workerCount, uint64_t& lastRead);
		void getStats(string& curFile, uint64_t& bytesLeft, size_t& filesLeft) const;
		void shutdown() {
			bool wakePaused;
			{
				Lock l(cs);
				stop = true;
				wakePaused = paused != 0;
			}
			if(wakePaused) s.signal();
			s.signal();
		}
		void scheduleRebuild();

	private:
		struct HashRequest {
			int64_t size;
			uint32_t timestamp;
			uint64_t jobId;
		};

		// Case-sensitive (faster), it is rather unlikely that case changes, and if it does it's harmless.
		// map because it's sorted (to avoid random hash order that would create quite strange shares while hashing)
		map<string, HashRequest> w;
		/** Further snapshots of a path already in w, oldest first. Rare: a file that changed while
		 * waiting, requested again with its new size or time. */
		std::multimap<string, HashRequest> waiting;
		mutable CriticalSection cs;
		Semaphore s;
		std::atomic<uint64_t>& jobIds;
		/** Files queued or being hashed, by volume. */
		unordered_map<string, size_t> volumes;
		string currentVolume;

		std::atomic<bool> stop;
		/** A queue item's currently being processed */ 
		std::atomic<bool> running;
		/** The queue is empty */ 
		bool idle;
		unsigned paused;
		std::atomic<bool> rebuild;
		string currentFile;
		int64_t currentSize;
		int64_t currentFileSize;
		uint32_t currentTimestamp;
		uint64_t currentJobId;
		bool currentTerminal;
		/** Totals of w and waiting, so progress polling doesn't walk the queue. */
		uint64_t queuedBytes;
		size_t queuedFiles;

		void instantPause();
		uint64_t newJobId() noexcept;
		/** A request left the queue: hashed, or dropped (with its volume, unless it is being hashed). */
		void dequeued(const HashRequest& request) noexcept;
		void volumeDone(const string& volume) noexcept;
		/** Continue hashing the file being hashed as the snapshot it actually is, under the job of a
		 * queued request for that snapshot when there is one. */
		void adoptSnapshot(const string& fileName, int64_t size, uint32_t timestamp,
			int64_t& requestedSize_, uint32_t& requestedTimestamp_, uint64_t& requestedJobId_) noexcept;

		/** Files at least this large are read unbuffered; smaller ones through the snapshot handle. */
		static const int64_t DIRECT_READ_MIN_SIZE = 16 * 1024 * 1024;
	};

	friend class Hasher;

	class HashStore {
	public:
		HashStore();
		~HashStore();
		/** @param aMarkerChecked The file was confirmed not to carry the Private Send marker. */
		void addFile(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, bool aUsed, bool aMarkerChecked = true);
		void setMarkerChecked(const string& aFileName);
		/** Whether the full tree for this root, of this file size, is stored. */
		bool hasTree(const TTHValue& root, int64_t size) const;

		void load(function<void (float)> progressF);
		void save();

		void rebuild();
		bool verify(bool fullCheck);
		void optimize();
		void compact();

		/** @param markerChecked_ Receives whether the stored file was checked for the Private Send marker. */
		optional<TTHValue> getTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp, bool* markerChecked_ = nullptr) noexcept;

		bool addTree(const TigerTree& tt) noexcept;
		bool getTree(const TTHValue& root, TigerTree& tth);
		int64_t getBlockSize(const TTHValue& root) const;
		bool isDirty() { return dirty; }
	private:
		enum class LegacyLoadResult {
			Missing,
			Loaded,
			Failed
		};

		/** Root -> tree mapping info, we assume there's only one tree for each root (a collision would mean we've broken tiger...) */
		struct TreeInfo {
			TreeInfo() : size(0), index(0), blockSize(0) { }
			TreeInfo(int64_t aSize, int64_t aIndex, int64_t aBlockSize) : size(aSize), index(aIndex), blockSize(aBlockSize) { }

			GETSET(int64_t, size, Size);
			GETSET(int64_t, index, Index);
			GETSET(int64_t, blockSize, BlockSize);
		};

		/** File -> root mapping info; the file name is its key in the directory's FileList. */
		struct FileInfo {
		public:
			FileInfo(const TTHValue& aRoot, uint32_t aTimeStamp, bool aUsed, bool aMarkerChecked) :
				root(aRoot), timeStamp(aTimeStamp), used(aUsed), markerChecked(aMarkerChecked) { }

			GETSET(TTHValue, root, Root);
			GETSET(uint32_t, timeStamp, TimeStamp);
			GETSET(bool, used, Used);
			/** Confirmed not to carry the Private Send marker (database column "checked"). */
			GETSET(bool, markerChecked, MarkerChecked);
		};

		friend class HashLoader;

		/** Keyed by file name, so a share scan's per-file lookup stays O(1) in folders holding
		 * hundreds of thousands of files (a linear search made scans quadratic per folder). */
		typedef unordered_map<string, FileInfo> FileList;
		unordered_map<string, FileList> fileIndex;
		unordered_map<TTHValue, TreeInfo> treeIndex;

		SQLiteDB db;
		bool dirty;
		std::unique_ptr<SQLiteTransaction> writeTransaction;
		uint32_t writeTransactionStatements;

		SQLiteStatement saveTreeStmt;
		SQLiteStatement saveFileStmt;
		SQLiteStatement removeFileStmt;
		SQLiteStatement loadTreeStmt;
		SQLiteStatement markCheckedStmt;

		/** Open the SQLite hash database and create or migrate the schema before use. */
		void openDb();
		/** Load the database, importing legacy XML/DAT files once when it has no data yet. */
		void loadStore(function<void (float)> progressF);
		/** Rename an unreadable database (and its WAL/SHM files) aside so a new one can be created. */
		bool setAsideUnreadableDb(const string& reason);
		/** Create all current SQLite hash-store tables and then apply versioned migrations. */
		void createSchema();
		/** Return SQLite PRAGMA user_version for deciding which compatibility migrations are needed. */
		int getSchemaVersion();
		/** Upgrade older SQLite hash-store schemas while preserving only rows that remain valid. */
		void migrateSchema(int version);
		void migrateSchemaV2(int version);
		/** Return true when SQLite already contains durable hash rows and should be treated as authoritative. */
		bool hasDbData();
		/** Read one metadata value used for durable database state such as legacy migration completion. */
		string getMetadata(const string& key);
		/** Store one metadata value inside the same SQLite database as the hash rows. */
		void setMetadata(const string& key, const string& value);
		/** Return true when legacy XML/DAT import has completed or has been intentionally skipped. */
		bool isLegacyMigrationComplete();
		/** Persist the completion marker and migration counters so stale legacy files are not re-imported. */
		void markLegacyMigrationComplete(uint64_t migratedFiles, uint64_t migratedTrees);
		/** Lazily open the SQLite database for code paths that may be called before startup loading. */
		void ensureDbOpen();

		/** Load SQLite tree/file rows into the in-memory indexes used by sharing, search and TTH lookups. */
		void loadDb(function<void (float)> progressF);
		/** Read the legacy HashIndex.xml into memory and report whether it was missing, loaded or corrupt. */
		LegacyLoadResult loadLegacy(function<void (float)> progressF);
		/** Validate legacy HashData.dat tree data, write valid records into SQLite and mark migration complete. */
		bool migrateLegacy(function<void (float)> progressF);

		void saveFile(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, bool aMarkerChecked);
		void removeFile(const string& aFileName) noexcept;
		bool saveTree(const TigerTree& tt);
		void writeFileRow(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, bool aMarkerChecked);
		void writeTreeRow(const TigerTree& tt);
		bool loadTree(const TTHValue& root, TigerTree& tt);

		bool loadLegacyTree(File& dataFile, const TreeInfo& ti, const TTHValue& root, TigerTree& tt);
		void beginWrite();
		void recordWrite();
		void flushWrites();
		void flushWritesNoexcept() noexcept;
		void resetStatements() noexcept;

		/** Return the legacy XML hash-index path in the user configuration directory. */
		static string getIndexFile();
		/** Return the legacy DAT hash-tree path in the user configuration directory. */
		static string getDataFile();
		/** Return the SQLite hash database path in the user configuration directory. */
		static string getDbFile();
	};

	friend class HashLoader;

	void scheduleHashStoreMaintenance(function<bool ()> operation, HashStoreMaintenanceCallback callback);

	HashStoreMaintenanceWorker hashStoreMaintenance;
	/** hashers[0] (primary) always exists and runs database rebuilds; others are added as drives are
	 * hashed in parallel and kept until shutdown. Guarded by hashersCs, taken after cs. */
	vector<std::unique_ptr<Hasher>> hashers;
	Hasher* primary;
	mutable CriticalSection hashersCs;
	bool hashersStarted = false;
	bool hashersStopped = false;
	unsigned hashersPaused = 0;
	Thread::Priority hasherPriority = Thread::IDLE;
	std::atomic<uint64_t> nextJobId { 1 };
	HashStore store;

	/** Queue a file with the hasher for its volume. @return the job id that will report it. */
	uint64_t queueHash(const string& fileName, int64_t size, uint32_t timestamp) noexcept;
	/** The hasher already reading this volume, else an idle one, else a new one (up to
	 * MAX_HASH_VOLUMES), else the least loaded. Requires hashersCs. */
	Hasher& selectHasher(const string& volume);
	/** The drive or network share a path is on, from the path alone (no disk access): "c:",
	 * "\\server\share". Empty when unknown, so such files share one hasher. */
	static string getVolume(const string& path);
	void startHashers();
	void shutdownHashers();
	void joinHashers();

	struct VerificationCacheEntry {
		VerificationCacheEntry(string aFileName, const TTHValue& aRoot, int64_t aSize, uint32_t aTimeStamp, bool aResult) :
			fileName(std::move(aFileName)), root(aRoot), size(aSize), timeStamp(aTimeStamp), result(aResult) { }

		string fileName;
		TTHValue root;
		int64_t size;
		uint32_t timeStamp;
		bool result;
	};
	// Small proof cache for refresh-time upload checks. Entries are keyed by path, size,
	// timestamp and requested root, so changed files must be hashed again before upload.
	// This is intentionally separate from the durable hash store: it is only a short-lived
	// guard against re-reading the same requested file while a live share refresh catches up.
	std::deque<VerificationCacheEntry> verificationCache;

	mutable CriticalSection cs;

	/** Single node tree where node = root, no storage in HashData.dat */
	static const int64_t SMALL_TREE = -1;

	void hashDone(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, int64_t speed,
		int64_t size, uint64_t jobId);
	void hashFailed(const string& aFileName, int64_t size, uint32_t timestamp, uint64_t jobId,
		HashManagerListener::Failure reason, const string& detail) noexcept;

	void doRebuild() {
		Lock l(cs);
		store.rebuild();
	}

	virtual void on(TimerManagerListener::Minute, uint64_t) noexcept {
		Lock l(cs);
		store.save();
	}
};

} // namespace dcpp

#endif // !defined(HASH_MANAGER_H)
