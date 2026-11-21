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
#include <dcpp/hash/HashManager.h>

#include <dcpp/core/io/File.h>
#include <dcpp/core/io/FileReader.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/core/classes/ScopedFunctor.h>
#include <dcpp/core/io/xml/SimpleXML.h>
#include <dcpp/core/io/SFVReader.h>
#include <dcpp/hash/TigerTreeHasher.h>
#include <dcpp/core/io/compress/ZUtils.h>
#include <dcpp/transfer/private/PrivateTransferStorage.h>

#include <unordered_set>

namespace dcpp {

using std::swap;

namespace {
void logHashStoreWarning(const string& message) {
	LogManager::getInstance()->message(message, LogMessage::SEV_WARNING, _("Hash database"));
}

uint64_t countRows(SQLiteDB& db, const char* sql) {
	auto stmt = db.prepare(sql);
	return stmt.step() ? static_cast<uint64_t>(stmt.columnInt64(0)) : 0;
}
}

/* Version history:
- Version 1: DC++ 0.307 to 0.68.
- Version 2: DC++ 0.670 to DC++ 0.802. Improved efficiency.
- Version 3: from DC++ 0.810 on. Changed the file registry to be case-sensitive. */
#define HASH_FILE_VERSION_STRING "3"
static const uint32_t HASH_FILE_VERSION = 3;
const int64_t HashManager::MIN_BLOCK_SIZE = 64 * 1024;

optional<TTHValue> HashManager::getTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) noexcept {
	return requestTTH(aFileName, aSize, aTimeStamp).tth;
}

HashManager::TTHRequest HashManager::requestTTH(
	const string& aFileName, int64_t aSize, uint32_t aTimeStamp) noexcept
{
	Lock l(cs);
	bool markerChecked = true;
	auto tth = store.getTTH(aFileName, aSize, aTimeStamp, &markerChecked);
	TTHRequest request { tth, tth ? 0 : queueHash(aFileName, aSize, aTimeStamp) };
	request.markerChecked = !tth || markerChecked;
	return request;
}

string HashManager::getVolume(const string& path) {
#ifdef _WIN32
	// \\?\C:\..., C:\..., \\?\UNC\server\share\..., \\server\share\...
	size_t start = 0;
	bool unc = false;
	if(path.compare(0, 8, "\\\\?\\UNC\\") == 0) {
		start = 8;
		unc = true;
	} else if(path.compare(0, 4, "\\\\?\\") == 0) {
		start = 4;
	} else if(path.compare(0, 2, "\\\\") == 0) {
		start = 2;
		unc = true;
	}

	if(unc) {
		const auto server = path.find_first_of("\\/", start);
		if(server == string::npos || server == start) {
			return Util::emptyString;
		}
		const auto share = path.find_first_of("\\/", server + 1);
		const auto end = share == string::npos ? path.size() : share;
		if(end == server + 1) {
			return Util::emptyString;
		}
		string volume = "\\\\" + path.substr(start, end - start);
		volume[2 + server - start] = '\\';
		return Text::toLower(volume);
	}

	if(path.size() >= start + 2 && path[start + 1] == ':') {
		return Text::toLower(path.substr(start, 2));
	}
#else
	(void)path;
#endif
	return Util::emptyString;
}

uint64_t HashManager::queueHash(const string& fileName, int64_t size, uint32_t timestamp) noexcept {
	const auto volume = getVolume(fileName);
	// Choosing the hasher and queueing with it are one step, so a volume never ends up with two.
	Lock l(hashersCs);
	return selectHasher(volume).hashFile(fileName, size, timestamp, volume);
}

HashManager::Hasher& HashManager::selectHasher(const string& volume) {
	// One reader per drive, as AirDC++ does: a hard disk or a NAS reading two files at once spends
	// its time seeking between them. Different drives are read in parallel.
	for(const auto& hasher: hashers) {
		if(hasher->hasVolume(volume)) {
			return *hasher;
		}
	}

	const auto limit = static_cast<size_t>(std::clamp(SETTING(MAX_HASH_VOLUMES),
		SettingsManager::MAX_HASH_VOLUMES_MIN, SettingsManager::MAX_HASH_VOLUMES_MAX));
	const auto usable = std::min(limit, hashers.size());
	for(size_t i = 0; i < usable; ++i) {
		if(hashers[i]->isIdle()) {
			return *hashers[i];
		}
	}

	if(hashers.size() < limit && !hashersStopped) {
		auto hasher = std::make_unique<Hasher>(nextJobId, hashersPaused);
		bool usableHasher = true;
		if(hashersStarted) {
			try {
				hasher->start();
				hasher->setThreadPriority(hasherPriority);
			} catch(const ThreadException& e) {
				LogManager::getInstance()->message(str(F_("Unable to start another hashing thread: %1%") % e.getError()),
					LogMessage::SEV_WARNING, _("Hashing"));
				usableHasher = false;
			}
		}
		if(usableHasher) {
			hashers.push_back(std::move(hasher));
			return *hashers.back();
		}
	}

	auto least = hashers.begin();
	for(auto i = hashers.begin() + 1; i < hashers.begin() + usable; ++i) {
		if((*i)->getLoad() < (*least)->getLoad()) {
			least = i;
		}
	}
	return **least;
}

void HashManager::startHashers() {
	Lock l(hashersCs);
	hashersStarted = true;
	for(const auto& hasher: hashers) {
		hasher->start();
		hasher->setThreadPriority(hasherPriority);
	}
}

void HashManager::shutdownHashers() {
	Lock l(hashersCs);
	hashersStopped = true;
	for(const auto& hasher: hashers) {
		hasher->shutdown();
	}
}

void HashManager::joinHashers() {
	// Not under hashersCs: a hasher's listeners may queue files (a chat attachment), which takes it.
	// No hasher is added once they are stopped.
	vector<Hasher*> all;
	{
		Lock l(hashersCs);
		for(const auto& hasher: hashers) {
			all.push_back(hasher.get());
		}
	}
	for(auto hasher: all) {
		hasher->join();
	}
}

void HashManager::stopHashing(const string& baseDir) {
	// Each hasher reports what it drops; listeners run without hashersCs held.
	vector<Hasher*> all;
	{
		Lock l(hashersCs);
		for(const auto& hasher: hashers) {
			all.push_back(hasher.get());
		}
	}
	for(auto hasher: all) {
		hasher->stopHashing(baseDir);
	}
}

void HashManager::setPriority(Thread::Priority p) {
	Lock l(hashersCs);
	hasherPriority = p;
	if(hashersStarted) {
		for(const auto& hasher: hashers) {
			hasher->setThreadPriority(p);
		}
	}
}

void HashManager::getStats(string& curFile, uint64_t& bytesLeft, size_t& filesLeft) const {
	curFile.clear();
	bytesLeft = 0;
	filesLeft = 0;
	Lock l(hashersCs);
	for(const auto& hasher: hashers) {
		string file;
		uint64_t bytes = 0;
		size_t files = 0;
		hasher->getStats(file, bytes, files);
		if(curFile.empty()) {
			curFile = std::move(file);
		}
		bytesLeft += bytes;
		filesLeft += files;
	}
}

optional<TTHValue> HashManager::findTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) noexcept {
	Lock l(cs);
	return store.getTTH(aFileName, aSize, aTimeStamp);
}

void HashManager::setMarkerChecked(const string& aFileName) noexcept {
	try {
		Lock l(cs);
		store.setMarkerChecked(aFileName);
	} catch(const Exception& e) {
		logHashStoreWarning(str(F_("Error saving hash data: %1%") % e.getError()));
	}
}

bool HashManager::addVerifiedFile(const string& aFileName, const TTHValue& aRoot) noexcept {
	try {
		File file(aFileName, File::READ, File::OPEN | File::SHARED);
		const auto size = file.getSize();
		const auto timestamp = file.getLastModified();
		file.close();

		if(timestamp == 0 || size < 0) {
			return false;
		}
		Lock l(cs);
		TigerTree tree;
		if(!store.hasTree(aRoot, size) || !store.getTree(aRoot, tree) || tree.getFileSize() != size) {
			// A file of one block (as the hasher would hash it) is described by its root alone.
			if(size > MIN_BLOCK_SIZE) {
				return false;
			}
			tree = TigerTree(size, MIN_BLOCK_SIZE, aRoot);
		}
		store.addFile(aFileName, timestamp, tree, true, true);
		return true;
	} catch(const Exception&) {
		return false;
	}
}

bool HashManager::verifyFileTTH(const string& aFileName, int64_t aSize, const TTHValue& root) noexcept {
	if(aSize < 0) {
		return false;
	}

	try {
		// Refresh-time uploads may be resolved through a cached share entry. Check the
		// live file size and timestamp first so moved/changed files don't get served
		// under an old TTH without forcing a full hash when the cheap proof changed.
		File f(aFileName, File::READ, File::OPEN | File::SHARED);
		auto size = f.getSize();
		auto timestamp = f.getLastModified();
		f.close();

		if(size != aSize) {
			return false;
		}
		// Every route into the hash store refuses privately received files.
		if(pft::hasPrivateMarker(aFileName)) {
			return false;
		}

		{
			Lock l(cs);
			// The verifier can be hit repeatedly by the same queued remote file while
			// a refresh is still active. Reuse both success and failure results only
			// while path, size, timestamp and requested root all still match.
			auto i = std::find_if(verificationCache.begin(), verificationCache.end(), [&](const VerificationCacheEntry& entry) {
				return entry.size == size && entry.timeStamp == timestamp && entry.root == root &&
					Util::stricmp(entry.fileName, aFileName) == 0;
			});
			if(i != verificationCache.end()) {
				auto result = i->result;
				if(i + 1 != verificationCache.end()) {
					auto entry = std::move(*i);
					verificationCache.erase(i);
					verificationCache.push_back(std::move(entry));
				}
				return result;
			}
		}

		// A full read is the final authority before upload. This is intentionally
		// synchronous because the caller is about to expose these bytes to a peer.
		auto bs = max(TigerTree::calcBlockSize(size, 10), MIN_BLOCK_SIZE);
		TigerTree tt(bs);
		auto sizeLeft = size;
		FileReader(true).read(aFileName, [&](const void* buf, size_t n) {
			sizeLeft -= static_cast<int64_t>(n);
			return tt.update(buf, n), true;
		});

		if(sizeLeft != 0) {
			Lock l(cs);
			verificationCache.emplace_back(aFileName, root, size, timestamp, false);
			if(verificationCache.size() > 256) {
				verificationCache.pop_front();
			}
			return false;
		}

		tt.finalize();
		if(tt.getRoot() != root) {
			Lock l(cs);
			verificationCache.emplace_back(aFileName, root, size, timestamp, false);
			if(verificationCache.size() > 256) {
				verificationCache.pop_front();
			}
			return false;
		}

		{
			Lock l(cs);
			store.addFile(aFileName, timestamp, tt, true, true);
			verificationCache.emplace_back(aFileName, root, size, timestamp, true);
			if(verificationCache.size() > 256) {
				verificationCache.pop_front();
			}
		}

		fire(HashManagerListener::TTHDone(), aFileName, root, size, timestamp, 0);
		return true;
	} catch(const Exception&) {
		return false;
	} catch(...) {
		return false;
	}
}

bool HashManager::verifyFileTTH(File& file, int64_t aSize, const TTHValue& root) noexcept {
	if(aSize < 0) return false;

	try {
		const auto originalPos = file.getPos();
		ScopedFunctor(([&file, originalPos] { file.setPos(originalPos); }));
		if(file.getSize() != aSize) return false;
		file.setPos(0);

		TigerTree tree(max(TigerTree::calcBlockSize(aSize, 10), MIN_BLOCK_SIZE));
		ByteVector buffer(1024 * 1024);
		int64_t remaining = aSize;
		while(remaining > 0) {
			auto bytes = static_cast<size_t>(std::min<int64_t>(remaining, buffer.size()));
			if(file.read(buffer.data(), bytes) == 0 || bytes == 0) return false;
			tree.update(buffer.data(), bytes);
			remaining -= static_cast<int64_t>(bytes);
		}
		if(file.getSize() != aSize) return false;
		tree.finalize();
		return tree.getRoot() == root;
	} catch(...) {
		return false;
	}
}

bool HashManager::getTree(const TTHValue& root, TigerTree& tt) {
	Lock l(cs);
	return store.getTree(root, tt);
}

int64_t HashManager::getBlockSize(const TTHValue& root) {
	Lock l(cs);
	return store.getBlockSize(root);
}

void HashManager::hashDone(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth,
	int64_t speed, int64_t size, uint64_t jobId)
{
	try {
		Lock l(cs);
		store.addFile(aFileName, aTimeStamp, tth, true, true);
	} catch (const Exception& e) {
		LogManager::getInstance()->message(str(F_("Hashing failed: %1%") % e.getError()), LogMessage::SEV_ERROR, _("Hashing"));
		hashFailed(aFileName, size, aTimeStamp, jobId,
			HashManagerListener::Failure::HASH_STORE, e.getError());
		return;
	}

	fire(HashManagerListener::TTHDone(), aFileName, tth.getRoot(), size, aTimeStamp, jobId);

	// Off by default, as in AirDC++: the hasher reports one summary per folder instead.
	if(!SETTING(LOG_HASHED_FILES)) {
		return;
	}
	if(speed > 0) {
		LogManager::getInstance()->message(str(F_("Finished hashing: %1% (%2% at %3%/s)") % Util::addBrackets(aFileName) %
			Util::formatBytes(size) % Util::formatBytes(speed)), LogMessage::SEV_INFO, _("Hashing"));
	} else if(size >= 0) {
		LogManager::getInstance()->message(str(F_("Finished hashing: %1% (%2%)") % Util::addBrackets(aFileName) %
			Util::formatBytes(size)), LogMessage::SEV_INFO, _("Hashing"));
	} else {
		LogManager::getInstance()->message(str(F_("Finished hashing: %1%") % Util::addBrackets(aFileName)),
			LogMessage::SEV_INFO, _("Hashing"));
	}
}

void HashManager::hashFailed(const string& aFileName, int64_t size, uint32_t timestamp, uint64_t jobId,
	HashManagerListener::Failure reason, const string& detail) noexcept
{
	fire(HashManagerListener::TTHFailed(), aFileName, size, timestamp, jobId, reason, detail);
}

bool HashManager::verifyHashStore(bool fullCheck) {
	Lock l(cs);
	return store.verify(fullCheck);
}

void HashManager::optimizeHashStore() {
	Lock l(cs);
	store.optimize();
}

void HashManager::compactHashStore() {
	Lock l(cs);
	store.compact();
}

void HashManager::scheduleHashStoreMaintenance(function<bool ()> operation, HashStoreMaintenanceCallback callback) {
	hashStoreMaintenance.schedule([operation = move(operation), callback = move(callback)] {
		bool success = false;
		string error;
		try {
			success = operation();
		} catch(const Exception& e) {
			error = e.getError();
		} catch(const std::exception& e) {
			error = e.what();
		} catch(...) {
			error = _("Unknown hash database maintenance error");
		}
		if(callback) callback(success, error);
	});
}

void HashManager::verifyHashStoreAsync(bool fullCheck, HashStoreMaintenanceCallback callback) {
	scheduleHashStoreMaintenance([this, fullCheck] { return verifyHashStore(fullCheck); }, move(callback));
}

void HashManager::optimizeHashStoreAsync(HashStoreMaintenanceCallback callback) {
	scheduleHashStoreMaintenance([this] {
		optimizeHashStore();
		return true;
	}, move(callback));
}

void HashManager::compactHashStoreAsync(HashStoreMaintenanceCallback callback) {
	scheduleHashStoreMaintenance([this] {
		compactHashStore();
		return true;
	}, move(callback));
}

void HashManager::HashStoreMaintenanceWorker::schedule(function<void ()> task) {
	{
		Lock l(cs);
		if(stop) return;
		tasks.push_back(move(task));
	}
	s.signal();
}

void HashManager::HashStoreMaintenanceWorker::shutdown() {
	{
		Lock l(cs);
		stop = true;
		tasks.clear();
	}
	s.signal();
}

int HashManager::HashStoreMaintenanceWorker::run() {
	setThreadPriority(Thread::LOW);
	for(;;) {
		s.wait();

		function<void ()> task;
		{
			Lock l(cs);
			if(stop) break;
			if(tasks.empty()) continue;
			task = move(tasks.front());
			tasks.pop_front();
		}

		task();
	}
	return 0;
}

void HashManager::HashStore::addFile(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, bool aUsed, bool aMarkerChecked) {
	if(!addTree(tth)) {
		throw HashException(_("Unable to save hash data"));
	}

	auto fname = Util::getFileName(aFileName), fpath = Util::getFilePath(aFileName);

	fileIndex[fpath].insert_or_assign(std::move(fname), FileInfo(tth.getRoot(), aTimeStamp, aUsed, aMarkerChecked));
	saveFile(aFileName, aTimeStamp, tth, aMarkerChecked);
}

void HashManager::HashStore::setMarkerChecked(const string& aFileName) {
	auto i = fileIndex.find(Util::getFilePath(aFileName));
	if(i == fileIndex.end()) {
		return;
	}
	auto j = i->second.find(Util::getFileName(aFileName));
	if(j == i->second.end() || j->second.getMarkerChecked()) {
		return;
	}
	j->second.setMarkerChecked(true);

	beginWrite();
	if(!markCheckedStmt.isOpen()) {
		markCheckedStmt = db.prepare("UPDATE files SET checked=1 WHERE path=?1");
	}
	markCheckedStmt.bind(1, aFileName);
	markCheckedStmt.stepDone();
	markCheckedStmt.reset();
	markCheckedStmt.clearBindings();
	recordWrite();
}

bool HashManager::HashStore::hasTree(const TTHValue& root, int64_t size) const {
	auto i = treeIndex.find(root);
	return i != treeIndex.end() && i->second.getSize() == size;
}

bool HashManager::HashStore::addTree(const TigerTree& tt) noexcept {
	if (treeIndex.find(tt.getRoot()) == treeIndex.end()) {
		try {
			saveTree(tt);
		} catch (const Exception& e) {
			LogManager::getInstance()->message(str(F_("Error saving hash data: %1%") % e.getError()),
				LogMessage::SEV_ERROR, _("Hash database"));
			return false;
		}
		treeIndex.emplace(tt.getRoot(), TreeInfo(tt.getFileSize(), tt.getLeaves().size() == 1 ? SMALL_TREE : 0, tt.getBlockSize()));
	}

	return true;
}

bool HashManager::HashStore::loadLegacyTree(File& f, const TreeInfo& ti, const TTHValue& root, TigerTree& tt) {
	if (ti.getIndex() == SMALL_TREE) {
		tt = TigerTree(ti.getSize(), ti.getBlockSize(), root);
		return true;
	}
	try {
		f.setPos(ti.getIndex());
		size_t datalen = TigerTree::calcBlocks(ti.getSize(), ti.getBlockSize()) * TTHValue::BYTES;
		std::unique_ptr<uint8_t[]> buf(new uint8_t[datalen]);
		f.read(&buf[0], datalen);
		tt = TigerTree(ti.getSize(), ti.getBlockSize(), &buf[0]);
		if (!(tt.getRoot() == root))
			return false;
	} catch (const Exception&) {
		return false;
	}

	return true;
}

void HashManager::HashStore::openDb() {
	db.open(getDbFile());
	createSchema();
}

void HashManager::HashStore::createSchema() {
	// Keep table creation idempotent so brand-new databases and older SQLite databases can follow
	// the same open path before version-specific migrations are considered.
	db.execute(
		"CREATE TABLE IF NOT EXISTS trees ("
		"root BLOB PRIMARY KEY NOT NULL CHECK(length(root) = 24),"
		"size INTEGER NOT NULL CHECK(size >= 0),"
		"block_size INTEGER NOT NULL CHECK(block_size >= 1024),"
		"leaves BLOB"
		") WITHOUT ROWID;"
		"CREATE TABLE IF NOT EXISTS files ("
		"path TEXT PRIMARY KEY NOT NULL,"
		"size INTEGER NOT NULL CHECK(size >= 0),"
		"timestamp INTEGER NOT NULL CHECK(timestamp > 0),"
		"root BLOB NOT NULL CHECK(length(root) = 24),"
		"FOREIGN KEY(root) REFERENCES trees(root) ON UPDATE CASCADE ON DELETE CASCADE"
		") WITHOUT ROWID;"
		"CREATE INDEX IF NOT EXISTS idx_hash_files_root ON files(root);"
		"CREATE TABLE IF NOT EXISTS metadata ("
		"key TEXT PRIMARY KEY NOT NULL,"
		"value TEXT NOT NULL"
		") WITHOUT ROWID;"
	);

	migrateSchema(getSchemaVersion());
}

int HashManager::HashStore::getSchemaVersion() {
	auto stmt = db.prepare("PRAGMA user_version");
	return stmt.step() ? stmt.columnInt(0) : 0;
}

void HashManager::HashStore::migrateSchema(int version) {
	migrateSchemaV2(version);

	if(version < 3) {
		// Version 3 records whether a file was checked for the Private Send marker. Existing rows
		// start unchecked and are checked once by the next share scan.
		bool hasColumn = false;
		{
			auto columns = db.prepare("PRAGMA table_info(files)");
			while(columns.step()) {
				if(columns.columnText(1) == "checked") {
					hasColumn = true;
				}
			}
		}
		SQLiteTransaction transaction(db);
		if(!hasColumn) {
			db.execute("ALTER TABLE files ADD COLUMN checked INTEGER NOT NULL DEFAULT 0;");
		}
		db.execute("PRAGMA user_version = 3;");
		transaction.commit();
	}
}

void HashManager::HashStore::migrateSchemaV2(int version) {
	if(version < 2) {
		// Version 2 adds an enforced files.root -> trees.root relationship. Copying through a
		// replacement table lets old databases open while dropping orphaned or malformed rows.
		SQLiteTransaction transaction(db);
		db.execute(
			"CREATE TABLE IF NOT EXISTS files_v2 ("
			"path TEXT PRIMARY KEY NOT NULL,"
			"size INTEGER NOT NULL CHECK(size >= 0),"
			"timestamp INTEGER NOT NULL CHECK(timestamp > 0),"
			"root BLOB NOT NULL CHECK(length(root) = 24),"
			"FOREIGN KEY(root) REFERENCES trees(root) ON UPDATE CASCADE ON DELETE CASCADE"
			") WITHOUT ROWID;"
			"INSERT OR REPLACE INTO files_v2(path, size, timestamp, root) "
			"SELECT f.path, f.size, f.timestamp, f.root FROM files f "
			"WHERE f.path <> '' AND f.size >= 0 AND f.timestamp > 0 AND length(f.root) = 24 "
			"AND EXISTS(SELECT 1 FROM trees t WHERE t.root = f.root);"
			"DROP TABLE files;"
			"ALTER TABLE files_v2 RENAME TO files;"
			"CREATE INDEX IF NOT EXISTS idx_hash_files_root ON files(root);"
			"PRAGMA user_version = 2;"
		);
		transaction.commit();
	} else {
		// Older builds might have created the table but not the index, so keep this repair cheap
		// and safe for every startup.
		db.execute("CREATE INDEX IF NOT EXISTS idx_hash_files_root ON files(root);");
	}
}

bool HashManager::HashStore::hasDbData() {
	// The metadata table is intentionally ignored here: an empty database with only migration
	// metadata should not be treated as containing share hashes.
	auto stmt = db.prepare(
		"SELECT "
		"EXISTS(SELECT 1 FROM trees LIMIT 1) OR "
		"EXISTS(SELECT 1 FROM files LIMIT 1)"
	);
	return stmt.step() && stmt.columnInt(0) != 0;
}

string HashManager::HashStore::getMetadata(const string& key) {
	// Missing metadata returns an empty string so callers can use simple equality checks.
	auto stmt = db.prepare("SELECT value FROM metadata WHERE key=?1");
	stmt.bind(1, key);
	return stmt.step() ? stmt.columnText(0) : string();
}

void HashManager::HashStore::setMetadata(const string& key, const string& value) {
	// INSERT OR REPLACE keeps metadata writes atomic and avoids needing separate insert/update paths.
	auto stmt = db.prepare("INSERT OR REPLACE INTO metadata(key, value) VALUES(?1, ?2)");
	stmt.bind(1, key);
	stmt.bind(2, value);
	stmt.stepDone();
}

bool HashManager::HashStore::isLegacyMigrationComplete() {
	return getMetadata("legacy_migration_complete") == "1";
}

void HashManager::HashStore::markLegacyMigrationComplete(uint64_t migratedFiles, uint64_t migratedTrees) {
	// The marker is the durable decision that SQLite is authoritative. Counters are diagnostic only,
	// but they make user reports and future migration audits easier to interpret.
	setMetadata("legacy_migration_complete", "1");
	setMetadata("legacy_migration_time", std::to_string(GET_TIME()));
	setMetadata("legacy_migrated_files", std::to_string(migratedFiles));
	setMetadata("legacy_migrated_trees", std::to_string(migratedTrees));
	setMetadata("legacy_index_file", "HashIndex.xml");
	setMetadata("legacy_data_file", "HashData.dat");
}

void HashManager::HashStore::ensureDbOpen() {
	if(!db.isOpen()) {
		openDb();
	}
}

bool HashManager::HashStore::saveTree(const TigerTree& tt) {
	beginWrite();
	writeTreeRow(tt);
	recordWrite();
	return true;
}

void HashManager::HashStore::saveFile(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, bool aMarkerChecked) {
	beginWrite();
	writeFileRow(aFileName, aTimeStamp, tth, aMarkerChecked);
	recordWrite();
}

void HashManager::HashStore::writeTreeRow(const TigerTree& tt) {
	if(!saveTreeStmt.isOpen()) {
		saveTreeStmt = db.prepare(
			"INSERT INTO trees(root, size, block_size, leaves) VALUES(?1, ?2, ?3, ?4) "
			"ON CONFLICT(root) DO UPDATE SET "
			"size=excluded.size, block_size=excluded.block_size, leaves=excluded.leaves"
		);
	}
	saveTreeStmt.bind(1, tt.getRoot().data, TTHValue::BYTES);
	saveTreeStmt.bind(2, tt.getFileSize());
	saveTreeStmt.bind(3, tt.getBlockSize());
	if(tt.getLeaves().size() == 1) {
		saveTreeStmt.bindNull(4);
	} else {
		saveTreeStmt.bind(4, tt.getLeaves()[0].data, tt.getLeaves().size() * TTHValue::BYTES);
	}
	saveTreeStmt.stepDone();
	saveTreeStmt.reset();
	saveTreeStmt.clearBindings();
}

void HashManager::HashStore::writeFileRow(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, bool aMarkerChecked) {
	if(!saveFileStmt.isOpen()) {
		saveFileStmt = db.prepare(
			"INSERT INTO files(path, size, timestamp, root, checked) VALUES(?1, ?2, ?3, ?4, ?5) "
			"ON CONFLICT(path) DO UPDATE SET "
			"size=excluded.size, timestamp=excluded.timestamp, root=excluded.root, checked=excluded.checked"
		);
	}
	saveFileStmt.bind(1, aFileName);
	saveFileStmt.bind(2, tth.getFileSize());
	saveFileStmt.bind(3, static_cast<int64_t>(aTimeStamp));
	saveFileStmt.bind(4, tth.getRoot().data, TTHValue::BYTES);
	saveFileStmt.bind(5, static_cast<int64_t>(aMarkerChecked ? 1 : 0));
	saveFileStmt.stepDone();
	saveFileStmt.reset();
	saveFileStmt.clearBindings();
}

void HashManager::HashStore::removeFile(const string& aFileName) noexcept {
	try {
		beginWrite();
		if(!removeFileStmt.isOpen()) {
			removeFileStmt = db.prepare("DELETE FROM files WHERE path=?1");
		}
		removeFileStmt.bind(1, aFileName);
		removeFileStmt.stepDone();
		removeFileStmt.reset();
		removeFileStmt.clearBindings();
		recordWrite();
	} catch (const SQLiteException& e) {
		logHashStoreWarning(str(F_("Error removing stale hash database entry for %1%: %2%") % Util::addBrackets(aFileName) % e.getError()));
	}
}

bool HashManager::HashStore::loadTree(const TTHValue& root, TigerTree& tt) {
	try {
		ensureDbOpen();
		if(!loadTreeStmt.isOpen()) {
			loadTreeStmt = db.prepare("SELECT size, block_size, leaves FROM trees WHERE root=?1");
		}
		loadTreeStmt.bind(1, root.data, TTHValue::BYTES);
		if(!loadTreeStmt.step()) {
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return false;
		}

		const auto size = loadTreeStmt.columnInt64(0);
		const auto blockSize = loadTreeStmt.columnInt64(1);
		if(size < 0 || blockSize < 1024) {
			logHashStoreWarning(str(F_("Invalid hash tree metadata for %1% in hash database") % root.toBase32()));
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return false;
		}

		const auto expectedLeaves = TigerTree::calcBlocks(size, blockSize);
		const auto expectedBytes = expectedLeaves * TTHValue::BYTES;
		if(loadTreeStmt.columnIsNull(2)) {
			if(expectedLeaves != 1) {
				logHashStoreWarning(str(F_("Missing hash tree leaf data for %1% in hash database") % root.toBase32()));
				loadTreeStmt.reset();
				loadTreeStmt.clearBindings();
				return false;
			}
			tt = TigerTree(size, blockSize, root);
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return true;
		}

		const auto bytes = loadTreeStmt.columnBytes(2);
		const auto blob = static_cast<const uint8_t*>(loadTreeStmt.columnBlob(2));
		if(!blob || bytes != expectedBytes || bytes == 0 || (bytes % TTHValue::BYTES) != 0) {
			logHashStoreWarning(str(F_("Invalid hash tree leaf data for %1% in hash database") % root.toBase32()));
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return false;
		}

		std::unique_ptr<uint8_t[]> buf(new uint8_t[bytes]);
		memcpy(&buf[0], blob, bytes);
		tt = TigerTree(size, blockSize, &buf[0]);
		if(tt.getRoot() != root) {
			logHashStoreWarning(str(F_("Hash tree root mismatch for %1% in hash database") % root.toBase32()));
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return false;
		}
		loadTreeStmt.reset();
		loadTreeStmt.clearBindings();
		return true;
	} catch (const Exception& e) {
		if(loadTreeStmt.isOpen()) {
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
		}
		logHashStoreWarning(str(F_("Error loading hash tree %1%: %2%") % root.toBase32() % e.getError()));
		return false;
	}
}

bool HashManager::HashStore::getTree(const TTHValue& root, TigerTree& tt) {
	auto i = treeIndex.find(root);
	if (i == treeIndex.end())
		return false;
	return loadTree(root, tt);
}

int64_t HashManager::HashStore::getBlockSize(const TTHValue& root) const {
	auto i = treeIndex.find(root);
	return i == treeIndex.end() ? 0 : i->second.getBlockSize();
}

optional<TTHValue> HashManager::HashStore::getTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp, bool* markerChecked_) noexcept {
	auto fname = Util::getFileName(aFileName), fpath = Util::getFilePath(aFileName);

	auto i = fileIndex.find(fpath);
	if (i != fileIndex.end()) {
		auto j = i->second.find(fname);
		if (j != i->second.end()) {
			FileInfo& fi = j->second;
			const auto& root = fi.getRoot();
			auto ti = treeIndex.find(root);
			if(ti != treeIndex.end() && ti->second.getSize() == aSize && fi.getTimeStamp() == aTimeStamp) {
				fi.setUsed(true);
				if(markerChecked_) {
					*markerChecked_ = fi.getMarkerChecked();
				}
				return root;
			}

			// the file size or the timestamp has changed
			removeFile(aFileName);
			i->second.erase(j);
		}
	}
	return std::nullopt;
}

void HashManager::HashStore::rebuild() {
	// Drops the records of files no share scan has looked up since startup, and the trees no file
	// uses any more. Done in the database and in the indexes directly: loading every tree into
	// memory first takes gigabytes for a large share. (A damaged tree is found whenever it is
	// loaded: its leaves are checked against its root.)
	try {
		flushWrites();
		vector<string> unused;
		std::unordered_set<TTHValue> referenced;
		for(const auto& [path, files]: fileIndex) {
			for(const auto& [name, fi]: files) {
				if(fi.getUsed()) {
					referenced.insert(fi.getRoot());
				} else {
					unused.push_back(path + name);
				}
			}
		}

		uint64_t removedTrees = 0;
		{
			SQLiteTransaction transaction(db);
			{
				auto remove = db.prepare("DELETE FROM files WHERE path=?1");
				for(const auto& path: unused) {
					remove.bind(1, path);
					remove.stepDone();
					remove.reset();
					remove.clearBindings();
				}
			}
			db.execute("DELETE FROM trees WHERE NOT EXISTS (SELECT 1 FROM files WHERE files.root = trees.root)");
			removedTrees = static_cast<uint64_t>(db.changes());
			transaction.commit();
		}

		for(auto i = fileIndex.begin(); i != fileIndex.end();) {
			auto& files = i->second;
			for(auto j = files.begin(); j != files.end();) {
				j = j->second.getUsed() ? std::next(j) : files.erase(j);
			}
			i = files.empty() ? fileIndex.erase(i) : std::next(i);
		}
		for(auto i = treeIndex.begin(); i != treeIndex.end();) {
			i = referenced.count(i->first) ? std::next(i) : treeIndex.erase(i);
		}

		LogManager::getInstance()->message(str(F_("Hash database rebuilt: removed %1% unused file records and %2% unused trees") %
			unused.size() % removedTrees), LogMessage::SEV_INFO, _("Hash database"));
		if(SETTING(HASH_DB_COMPACT_ON_REBUILD)) {
			compact();
		}
	} catch (const Exception& e) {
		LogManager::getInstance()->message(str(F_("Hash data rebuilding failed: %1%") % e.getError()),
			LogMessage::SEV_ERROR, _("Hash database"));
	}
}

void HashManager::HashStore::save() {
	if (db.isOpen()) {
		try {
			flushWrites();
			db.execute("PRAGMA optimize");
			db.execute("PRAGMA wal_checkpoint(PASSIVE)");
		} catch (const SQLiteException& e) {
			LogManager::getInstance()->message(str(F_("Error saving hash data: %1%") % e.getError()),
				LogMessage::SEV_ERROR, _("Hash database"));
		}
	}
}

bool HashManager::HashStore::verify(bool fullCheck) {
	ensureDbOpen();
	flushWrites();

	const char* sql = fullCheck ? "PRAGMA integrity_check" : "PRAGMA quick_check";
	auto stmt = db.prepare(sql);
	bool ok = true;
	StringList errors;
	while(stmt.step()) {
		const auto result = stmt.columnText(0);
		if(result != "ok") {
			ok = false;
			errors.push_back(result);
		}
	}

	if(ok) {
		LogManager::getInstance()->message(fullCheck ? _("Hash database integrity check passed") : _("Hash database quick check passed"),
			LogMessage::SEV_INFO, _("Hash database"));
	} else {
		LogManager::getInstance()->message(str(F_("Hash database integrity check failed: %1%") % Util::toString(errors)),
			LogMessage::SEV_ERROR, _("Hash database"));
	}
	return ok;
}

void HashManager::HashStore::optimize() {
	ensureDbOpen();
	flushWrites();
	db.execute("PRAGMA optimize");
	db.execute("PRAGMA wal_checkpoint(PASSIVE)");
	LogManager::getInstance()->message(_("Hash database optimized"), LogMessage::SEV_INFO, _("Hash database"));
}

void HashManager::HashStore::compact() {
	ensureDbOpen();
	flushWrites();
	resetStatements();
	db.execute("PRAGMA wal_checkpoint(TRUNCATE)");
	const auto oldAttachedLimit = db.setLimit(SQLITE_LIMIT_ATTACHED, 1);
	ScopedFunctor restoreAttachedLimit([this, oldAttachedLimit] {
		db.setLimit(SQLITE_LIMIT_ATTACHED, oldAttachedLimit);
	});
	db.execute("VACUUM");
	db.execute("PRAGMA optimize");
	LogManager::getInstance()->message(_("Hash database compacted"), LogMessage::SEV_INFO, _("Hash database"));
}

void HashManager::HashStore::beginWrite() {
	ensureDbOpen();
	if(!writeTransaction) {
		writeTransaction.reset(new SQLiteTransaction(db));
		writeTransactionStatements = 0;
	}
}

void HashManager::HashStore::recordWrite() {
	dirty = true;
	writeTransactionStatements++;
	const auto batchSize = max(1, SETTING(HASH_DB_WRITE_BATCH_SIZE));
	if(static_cast<int>(writeTransactionStatements) >= batchSize) {
		flushWrites();
	}
}

void HashManager::HashStore::flushWrites() {
	if(writeTransaction) {
		writeTransaction->commit();
		writeTransaction.reset();
		writeTransactionStatements = 0;
		dirty = false;
	}
}

void HashManager::HashStore::flushWritesNoexcept() noexcept {
	try {
		flushWrites();
	} catch (const Exception& e) {
		LogManager::getInstance()->message(str(F_("Error saving hash data: %1%") % e.getError()),
			LogMessage::SEV_ERROR, _("Hash database"));
	}
}

void HashManager::HashStore::resetStatements() noexcept {
	saveTreeStmt = SQLiteStatement();
	saveFileStmt = SQLiteStatement();
	removeFileStmt = SQLiteStatement();
	loadTreeStmt = SQLiteStatement();
	markCheckedStmt = SQLiteStatement();
}

string HashManager::HashStore::getIndexFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashIndex.xml"; }
string HashManager::HashStore::getDataFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashData.dat"; }
string HashManager::HashStore::getDbFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashStore.sqlite3"; }

class HashLoader: public SimpleXMLReader::CallBack {
public:
	HashLoader(HashManager::HashStore& s, const CountedInputStream<false>& countedStream, uint64_t fileSize, function<void (float)> progressF) :
		store(s),
		countedStream(countedStream),
		streamPos(0),
		fileSize(fileSize),
		progressF(progressF),
		version(HASH_FILE_VERSION),
		inTrees(false),
		inFiles(false),
		inHashStore(false)
	{ }
	void startTag(const string& name, StringPairList& attribs, bool simple);

private:
	HashManager::HashStore& store;

	const CountedInputStream<false>& countedStream;
	uint64_t streamPos;
	uint64_t fileSize;
	function<void (float)> progressF;

	int version;
	string file;

	bool inTrees;
	bool inFiles;
	bool inHashStore;
};

void HashManager::HashStore::loadDb(function<void (float)> progressF) {
	// Reserve the tree index when the row count is reasonable; this avoids repeated rehashing for
	// large hash databases without trusting a corrupt count large enough to exceed container limits.
	const auto treeRows = countRows(db, "SELECT COUNT(*) FROM trees");
	if(treeRows > 0 && treeRows <= static_cast<uint64_t>(treeIndex.max_size())) {
		treeIndex.reserve(static_cast<size_t>(treeRows));
	}

	uint64_t invalidTrees = 0;
	auto trees = db.prepare("SELECT root, size, block_size, leaves IS NULL FROM trees");
	while(trees.step()) {
		// SQLite CHECK constraints protect new writes, but startup still validates rows so older or
		// manually edited databases cannot poison the in-memory hash index.
		if(trees.columnBytes(0) != TTHValue::BYTES) {
			invalidTrees++;
			continue;
		}
		const auto root = TTHValue(static_cast<const uint8_t*>(trees.columnBlob(0)));
		const auto size = trees.columnInt64(1);
		const auto blockSize = trees.columnInt64(2);
		const auto index = trees.columnInt(3) ? SMALL_TREE : 0;
		if(size >= 0 && blockSize >= 1024) {
			treeIndex.emplace(root, TreeInfo(size, index, blockSize));
		} else {
			invalidTrees++;
		}
	}

	uint64_t invalidFiles = 0;
	uint64_t orphanFiles = 0;
	auto files = db.prepare("SELECT path, timestamp, root, checked FROM files ORDER BY path");
	string lastPath;
	FileList* currentFileList = nullptr;
	uint64_t loaded = 0;
	while(files.step()) {
		auto file = files.columnText(0);
		auto timeStamp = static_cast<uint32_t>(files.columnInt64(1));
		// File rows are skipped unless the path, timestamp and root are all usable by the existing
		// in-memory lookup contract.
		if(file.empty() || timeStamp == 0 || files.columnBytes(2) != TTHValue::BYTES) {
			invalidFiles++;
			continue;
		}

		const auto root = TTHValue(static_cast<const uint8_t*>(files.columnBlob(2)));
		// A file without a valid tree cannot answer TTH or tree requests correctly, so leave it to
		// normal rehashing instead of exposing a partial record.
		if(treeIndex.find(root) == treeIndex.end()) {
			orphanFiles++;
			continue;
		}

		auto fname = Util::getFileName(file), fpath = Util::getFilePath(file);
		if(!currentFileList || fpath != lastPath) {
			auto result = fileIndex.emplace(fpath, FileList());
			lastPath = std::move(fpath);
			currentFileList = &result.first->second;
		}
		currentFileList->try_emplace(std::move(fname), root, timeStamp, false, files.columnInt(3) != 0);
		if((++loaded % 1024) == 0) {
			progressF(0);
		}
	}

	if(invalidTrees > 0 || invalidFiles > 0 || orphanFiles > 0) {
		logHashStoreWarning(str(F_("Hash database loaded with %1% invalid tree records, %2% invalid file records and %3% file records without matching trees") %
			invalidTrees % invalidFiles % orphanFiles));
	}

	progressF(1);
}

HashManager::HashStore::LegacyLoadResult HashManager::HashStore::loadLegacy(function<void (float)> progressF) {
	if(File::getSize(getIndexFile()) == -1) {
		// Missing legacy files are not an error; this is the normal path for fresh SQLite installs.
		return LegacyLoadResult::Missing;
	}

	try {
		File f(getIndexFile(), File::READ, File::OPEN);
		CountedInputStream<false> countedStream(&f);
		HashLoader l(*this, countedStream, f.getSize(), progressF);
		SimpleXMLReader(&l).parse(countedStream);
		return LegacyLoadResult::Loaded;
	} catch (const Exception& e) {
		// A corrupt XML index should not mark migration complete. Leaving the old files untouched
		// gives the user a chance to inspect or restore them.
		logHashStoreWarning(str(F_("Error loading legacy hash database %1%: %2%") % getIndexFile() % e.getError()));
		return LegacyLoadResult::Failed;
	}
}

bool HashManager::HashStore::migrateLegacy(function<void (float)> progressF) {
	if(treeIndex.empty() && fileIndex.empty()) {
		try {
			// A valid but empty legacy store, or a fresh install with no legacy files, still needs the
			// completion marker so startup will not retry migration forever.
			SQLiteTransaction transaction(db);
			markLegacyMigrationComplete(0, 0);
			transaction.commit();
			return true;
		} catch (const Exception& e) {
			LogManager::getInstance()->message(str(F_("Hash database migration failed: %1%") % e.getError()),
				LogMessage::SEV_ERROR, _("Hash database"));
			return false;
		}
	}

	try {
		flushWrites();
		unordered_map<TTHValue, TigerTree> validTrees;
		uint64_t invalidTrees = 0;

		{
			// Read the trees in file order. Hash-table order is random within HashData.dat, which
			// costs a seek per tree when it is on a hard disk.
			vector<pair<int64_t, TTHValue>> order;
			order.reserve(treeIndex.size());
			for(const auto& i: treeIndex) {
				order.emplace_back(i.second.getIndex(), i.first);
			}
			std::sort(order.begin(), order.end(), [](const pair<int64_t, TTHValue>& a, const pair<int64_t, TTHValue>& b) {
				return a.first < b.first;
			});

			File dataFile(getDataFile(), File::READ, File::OPEN);
			size_t done = 0;
			for(const auto& item: order) {
				auto i = treeIndex.find(item.second);
				TigerTree tree;
				if(loadLegacyTree(dataFile, i->second, i->first, tree)) {
					// Recompute the SMALL_TREE marker from the verified tree rather than trusting
					// stale XML metadata.
					i->second.setIndex(tree.getLeaves().size() == 1 ? SMALL_TREE : 0);
					validTrees.emplace(i->first, tree);
				} else {
					invalidTrees++;
					treeIndex.erase(i);
				}
				if((++done & 1023) == 0) {
					progressF(0.5f * static_cast<float>(done) / static_cast<float>(order.size()));
				}
			}
		}

		uint64_t orphanFiles = 0;
		for(auto i = fileIndex.begin(); i != fileIndex.end();) {
			auto& fileList = i->second;
			for(auto j = fileList.begin(); j != fileList.end();) {
				// Drop file records whose tree failed validation so SQLite never receives rows that
				// cannot satisfy later tree requests.
				if(validTrees.find(j->second.getRoot()) == validTrees.end()) {
					orphanFiles++;
					j = fileList.erase(j);
				} else {
					++j;
				}
			}

			if(fileList.empty()) {
				i = fileIndex.erase(i);
			} else {
				++i;
			}
		}

		uint64_t migratedFiles = 0;
		size_t rows = validTrees.size(), written = 0;
		for(const auto& i: fileIndex) {
			rows += i.second.size();
		}
		auto rowWritten = [&] {
			if((++written & 1023) == 0) {
				progressF(0.5f + 0.5f * static_cast<float>(written) / static_cast<float>(rows));
			}
		};
		SQLiteTransaction transaction(db);
		// Replace the SQLite contents atomically so a crash cannot leave a half-migrated hash store
		// marked as complete.
		db.execute("DELETE FROM files");
		db.execute("DELETE FROM trees");
		for(auto& i: validTrees) {
			writeTreeRow(i.second);
			rowWritten();
		}
		for(auto& i: fileIndex) {
			const string& dir = i.first;
			for(auto& [name, fi]: i.second) {
				auto tree = validTrees.find(fi.getRoot());
				if(tree != validTrees.end()) {
					writeFileRow(dir + name, fi.getTimeStamp(), tree->second, fi.getMarkerChecked());
					migratedFiles++;
				}
				rowWritten();
			}
		}
		markLegacyMigrationComplete(migratedFiles, validTrees.size());
		transaction.commit();
		if(invalidTrees > 0 || orphanFiles > 0) {
			logHashStoreWarning(str(F_("Legacy hash database migration skipped %1% invalid trees and %2% file records without matching trees") %
				invalidTrees % orphanFiles));
		}
		return true;
	} catch (const Exception& e) {
		// On failure, keep the legacy files untouched and clear partial memory state; files can be
		// rehashed normally on the next sharing refresh.
		LogManager::getInstance()->message(str(F_("Hash database migration failed: %1%") % e.getError()),
			LogMessage::SEV_ERROR, _("Hash database"));
		fileIndex.clear();
		treeIndex.clear();
		return false;
	}
}

void HashManager::HashStore::load(function<void (float)> progressF) {
	for(bool retried = false;; retried = true) {
		try {
			loadStore(progressF);
			return;
		} catch (const SQLiteException& e) {
			fileIndex.clear();
			treeIndex.clear();
			// Only SQLite's own corruption verdicts justify replacing the file. Busy, locked or
			// I/O failures (for example another running instance) must leave it in place.
			if(!retried && e.isCorruption() && setAsideUnreadableDb(e.getError())) {
				continue;
			}
			LogManager::getInstance()->message(str(F_("Error loading hash data: %1%") % e.getError()),
				LogMessage::SEV_ERROR, _("Hash database"));
			return;
		} catch (const Exception& e) {
			LogManager::getInstance()->message(str(F_("Error loading hash data: %1%") % e.getError()),
				LogMessage::SEV_ERROR, _("Hash database"));
			fileIndex.clear();
			treeIndex.clear();
			return;
		}
	}
}

bool HashManager::HashStore::setAsideUnreadableDb(const string& reason) {
	resetStatements();
	writeTransaction.reset();
	db.close();

	const auto dbFile = getDbFile();
	const auto stamp = Util::formatTime("%Y%m%d-%H%M%S", time(nullptr));
	string suffix = ".corrupt-" + stamp;
	for(int n = 2; File::getSize(dbFile + suffix) != -1; ++n) {
		suffix = ".corrupt-" + stamp + "-" + std::to_string(n);
	}

	try {
		File::renameFile(dbFile, dbFile + suffix);
	} catch (const FileException& e) {
		LogManager::getInstance()->message(str(F_("The hash database %1% is unreadable (%2%) and could not be renamed: %3%") %
			dbFile % reason % e.getError()), LogMessage::SEV_ERROR, _("Hash database"));
		return false;
	}
	// The write-ahead log belongs to the unreadable database; never replay it into a new one.
	for(const char* sidecar: { "-wal", "-shm" }) {
		if(File::getSize(dbFile + sidecar) != -1) {
			try {
				File::renameFile(dbFile + sidecar, dbFile + sidecar + suffix);
			} catch (const FileException&) {
				try { File::deleteFile(dbFile + sidecar); } catch (const FileException&) { }
			}
		}
	}

	LogManager::getInstance()->message(str(F_("The hash database was unreadable (%1%). It was renamed to %2% and a new one was created; files will be rehashed as needed.") %
		reason % (dbFile + suffix)), LogMessage::SEV_WARNING, _("Hash database"));
	return true;
}

void HashManager::HashStore::loadStore(function<void (float)> progressF) {
	{
		openDb();
		if(SETTING(HASH_DB_VERIFY_STARTUP) && !verify(false)) {
			throw SQLiteException(_("startup verification failed"), SQLITE_CORRUPT);
		}
		if(hasDbData()) {
			// Existing SQLite rows always win over legacy files. The metadata marker is backfilled for
			// databases created by earlier SQLite builds that did not have the metadata table yet.
			loadDb(progressF);
			if(!isLegacyMigrationComplete()) {
				SQLiteTransaction transaction(db);
				markLegacyMigrationComplete(0, 0);
				transaction.commit();
			}
		} else if(isLegacyMigrationComplete()) {
			// An empty SQLite store with a completion marker is intentional. Do not import legacy files
			// that may appear later over the completed SQLite state.
			progressF(1);
		} else {
			// Only databases without rows and without a completion marker are eligible for one-time
			// legacy XML/DAT import.
			// Reading the legacy index is the first 30% of startup progress; migrating it is the rest,
			// so a long migration no longer looks stuck at 100%.
			const auto legacyResult = loadLegacy([&](float progress) { progressF(0.3f * progress); });
			if(legacyResult == LegacyLoadResult::Failed) {
				fileIndex.clear();
				treeIndex.clear();
			} else if(migrateLegacy([&](float progress) { progressF(0.3f + 0.7f * progress); })) {
				if(legacyResult == LegacyLoadResult::Missing) {
					LogManager::getInstance()->message(_("No legacy hash database found; SQLite hash database marked as migration complete"),
						LogMessage::SEV_INFO, _("Hash database"));
				}
				progressF(1);
			}
		}
	}
}

namespace {
/* version 2 files were stored in lower-case; carry the file registration over only if the file can
be found, and if it has no case-insensitive duplicate. */

#ifdef _WIN32

bool upgradeFromV2(string& file) {
	WIN32_FIND_DATA data;
	// FindFirstFile does a case-insensitive search by default
	auto handle = ::FindFirstFile(File::toNativePath(file).c_str(), &data);
	if(handle == INVALID_HANDLE_VALUE) {
		// file not found
		return false;
	}
	if(::FindNextFile(handle, &data)) {
		// found a dupe
		::FindClose(handle);
		return false;
	}
	::FindClose(handle);

	// don't use dcpp::File as that would be case-sensitive
	handle = ::CreateFile(File::toNativePath(Util::getFilePath(file) + Text::fromT(data.cFileName)).c_str(),
		GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if(handle == INVALID_HANDLE_VALUE) {
		return false;
	}

	// use GetFinalPathNameByHandle to retrieve a properly cased path from the
	// lower-case one that the version 2 file registry has provided us with.
	wstring buf((std::max<size_t>)(file.size() * 2, MAX_PATH), 0);
	while(true) {
		const auto length = ::GetFinalPathNameByHandle(handle, &buf[0], static_cast<DWORD>(buf.size()), 0);
		if(!length) {
			buf.clear();
			break;
		}
		if(length < buf.size()) {
			buf.resize(length);
			break;
		}
		buf.resize(static_cast<size_t>(length) + 1, 0);
	}

	::CloseHandle(handle);

	if(buf.empty()) {
		return false;
	}
	// GetFinalPathNameByHandle prepends "\\?\"; normalize it back to regular paths.
	if(buf.size() >= 8 && buf.substr(0, 8) == L"\\\\?\\UNC\\") {
		buf.replace(0, 8, L"\\\\");
	} else if(buf.size() >= 4 && buf.substr(0, 4) == L"\\\\?\\") {
		buf.erase(0, 4);
	}

	auto buf8 = Text::fromT(buf);
	if(Text::toLower(buf8) == file) {
		file = move(buf8);
		return true;
	}

	return false;
}

#else

bool upgradeFromV2(string& file) {
	/// @todo implement this on Linux; by default, force re-hashing.
	return false;
}

#endif

}

static const string sHashStore = "HashStore";
static const string sversion = "version"; // Oops, v1 was like this
static const string sVersion = "Version";
static const string sTrees = "Trees";
static const string sFiles = "Files";
static const string sFile = "File";
static const string sName = "Name";
static const string sSize = "Size";
static const string sHash = "Hash";
static const string sType = "Type";
static const string sTTH = "TTH";
static const string sIndex = "Index";
static const string sBlockSize = "BlockSize";
static const string sTimeStamp = "TimeStamp";
static const string sRoot = "Root";

void HashLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	ScopedFunctor([this] {
		auto readBytes = countedStream.getReadBytes();
		if(readBytes != streamPos) {
			streamPos = readBytes;
			progressF(static_cast<float>(readBytes) / static_cast<float>(fileSize));
		}
	});

	if (!inHashStore && name == sHashStore) {
		version = Util::toInt(getAttrib(attribs, sVersion, 0));
		if (version == 0) {
			version = Util::toInt(getAttrib(attribs, sversion, 0));
		}
		inHashStore = !simple;
	} else if (inHashStore && (version == 2 || version == 3)) {
		if (inTrees && name == sHash) {
			const string& type = getAttrib(attribs, sType, 0);
			int64_t index = Util::toInt64(getAttrib(attribs, sIndex, 1));
			int64_t blockSize = Util::toInt64(getAttrib(attribs, sBlockSize, 2));
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 3));
			const string& root = getAttrib(attribs, sRoot, 4);
			if (!root.empty() && type == sTTH && (index >= 8 || index == HashManager::SMALL_TREE) && blockSize >= 1024) {
				store.treeIndex[TTHValue(root)] = HashManager::HashStore::TreeInfo(size, index, blockSize);
			}
		} else if (inFiles && name == sFile) {
			file = getAttrib(attribs, sName, 0);
			auto timeStamp = Util::toUInt32(getAttrib(attribs, sTimeStamp, 1));
			const auto& root = getAttrib(attribs, sRoot, 2);
			if(!file.empty() && timeStamp > 0 && !root.empty() && (version != 2 || upgradeFromV2(file))) {
				auto fname = Util::getFileName(file), fpath = Util::getFilePath(file);
				store.fileIndex[fpath].try_emplace(fname, TTHValue(root), timeStamp, false, false);
			}
		} else if (name == sTrees) {
			inTrees = !simple;
		} else if (name == sFiles) {
			inFiles = !simple;
		}
	}
}

HashManager::HashStore::HashStore() :
	dirty(false),
	writeTransactionStatements(0) {
}

HashManager::HashStore::~HashStore() {
	flushWritesNoexcept();
}

uint64_t HashManager::Hasher::newJobId() noexcept {
	auto jobId = jobIds++;
	if(jobId == 0) jobId = jobIds++;
	return jobId;
}

void HashManager::Hasher::volumeDone(const string& volume) noexcept {
	auto i = volumes.find(volume);
	if(i != volumes.end() && --i->second == 0) {
		volumes.erase(i);
	}
}

bool HashManager::Hasher::hasVolume(const string& volume) const noexcept {
	Lock l(cs);
	return volumes.find(volume) != volumes.end();
}

bool HashManager::Hasher::isIdle() const noexcept {
	Lock l(cs);
	return volumes.empty();
}

uint64_t HashManager::Hasher::getLoad() const noexcept {
	Lock l(cs);
	return queuedBytes + static_cast<uint64_t>(std::max<int64_t>(currentSize, 0));
}

uint64_t HashManager::Hasher::hashFile(const string& fileName, int64_t size, uint32_t timestamp, const string& volume) noexcept {
	Lock l(cs);
	if(running && !currentTerminal && currentFile == fileName && currentFileSize == size &&
		currentTimestamp == timestamp)
	{
		return currentJobId;
	}
	auto first = w.find(fileName);
	if(first != w.end()) {
		if(first->second.size == size && first->second.timestamp == timestamp) return first->second.jobId;
		for(auto i = waiting.lower_bound(fileName); i != waiting.end() && i->first == fileName; ++i) {
			if(i->second.size == size && i->second.timestamp == timestamp) return i->second.jobId;
		}
	}

	const auto jobId = newJobId();
	const HashRequest request { size, timestamp, jobId };
	if(first == w.end()) {
		w.emplace(fileName, request);
	} else {
		waiting.emplace_hint(waiting.upper_bound(fileName), fileName, request);
	}
	++volumes[volume];
	++queuedFiles;
	queuedBytes += static_cast<uint64_t>(std::max<int64_t>(size, 0));
	if(idle) s.signal();
	return jobId;
}

void HashManager::Hasher::adoptSnapshot(const string& fileName, int64_t size, uint32_t timestamp,
	int64_t& requestedSize_, uint32_t& requestedTimestamp_, uint64_t& requestedJobId_) noexcept
{
	Lock l(cs);
	uint64_t jobId = 0;
	auto first = w.find(fileName);
	if(first != w.end() && first->second.size == size && first->second.timestamp == timestamp) {
		jobId = first->second.jobId;
		dequeued(first->second);
		w.erase(first);
		auto next = waiting.find(fileName);
		if(next != waiting.end()) {
			w.emplace(fileName, next->second);
			waiting.erase(next);
		}
	} else {
		for(auto i = waiting.lower_bound(fileName); i != waiting.end() && i->first == fileName; ++i) {
			if(i->second.size == size && i->second.timestamp == timestamp) {
				jobId = i->second.jobId;
				dequeued(i->second);
				waiting.erase(i);
				break;
			}
		}
	}
	if(jobId != 0) {
		// That request is answered by the file being hashed, whose volume is already counted.
		volumeDone(currentVolume);
	} else {
		jobId = newJobId();
	}
	currentFileSize = requestedSize_ = size;
	currentTimestamp = requestedTimestamp_ = timestamp;
	currentJobId = requestedJobId_ = jobId;
	currentTerminal = false;
	currentSize = size;
}

void HashManager::Hasher::dequeued(const HashRequest& request) noexcept {
	if(queuedFiles > 0) --queuedFiles;
	const auto bytes = static_cast<uint64_t>(std::max<int64_t>(request.size, 0));
	queuedBytes = bytes >= queuedBytes ? 0 : queuedBytes - bytes;
}

bool HashManager::Hasher::pause() noexcept {
	Lock l(cs);
	return paused++;
}

void HashManager::Hasher::resume() noexcept {
	Lock l(cs);
	dcassert(paused > 0);
	if(--paused == 0)
		s.signal();
}

bool HashManager::Hasher::isPaused() const noexcept {
	Lock l(cs);
	return paused > 0;
}

void HashManager::Hasher::stopHashing(const string& baseDir) {
	vector<pair<string, HashRequest>> cancelled;
	{
		Lock l(cs);
		// Both queues are sorted by path, so the files under baseDir form one range.
		auto inBase = [&baseDir](const string& path) { return path.compare(0, baseDir.size(), baseDir) == 0; };
		for(auto i = w.lower_bound(baseDir); i != w.end() && inBase(i->first);) {
			cancelled.emplace_back(i->first, i->second);
			dequeued(i->second);
			volumeDone(HashManager::getVolume(i->first));
			i = w.erase(i);
		}
		for(auto i = waiting.lower_bound(baseDir); i != waiting.end() && inBase(i->first);) {
			cancelled.emplace_back(i->first, i->second);
			dequeued(i->second);
			volumeDone(HashManager::getVolume(i->first));
			i = waiting.erase(i);
		}
	}
	for(const auto& item: cancelled) {
		HashManager::getInstance()->hashFailed(item.first, item.second.size, item.second.timestamp,
			item.second.jobId,
			HashManagerListener::Failure::CANCELLED, _("Hashing was cancelled"));
	}
}

void HashManager::Hasher::getStats(string& curFile, uint64_t& bytesLeft, size_t& filesLeft) const {
	Lock l(cs);
	curFile = currentFile;
	filesLeft = queuedFiles + (running ? 1 : 0);
	bytesLeft = queuedBytes + static_cast<uint64_t>(std::max<int64_t>(currentSize, 0));
}

void HashManager::Hasher::instantPause() {
	bool wait = false;
	{
		Lock l(cs);
		if(paused > 0 && !stop) {
			wait = true;
		}
	}
	if(wait)
		s.wait();
}

void HashManager::Hasher::scheduleRebuild() {
	rebuild = true; 
	{
		Lock l(cs);
		if(idle) {
			s.signal();
			return;
		}
	}

	LogManager::getInstance()->message(_("Hash database rebuild has been scheduled"), LogMessage::SEV_INFO, _("Hash database"));
}

bool HashManager::Hasher::fastHash(File& file, TigerTree& tth, int64_t size, CRC32Filter* xcrc32, size_t workerCount, uint64_t& lastRead) {
	// The coordinator invokes this observer in file order before dispatching each
	// buffer, keeping rate limiting, CRC32 state, and cancellation single-threaded.
	auto observe = [this, xcrc32, &lastRead](const void* data, size_t bytes) {
		const auto maxSpeed = SETTING(MAX_HASH_SPEED);
		if(maxSpeed > 0) {
			const auto now = GET_TICK();
			const auto minTime = bytes * 1000ULL / (static_cast<uint64_t>(maxSpeed) * 1024ULL * 1024ULL);
			if(lastRead + minTime > now) Thread::sleep(static_cast<uint32_t>(lastRead + minTime - now));
			lastRead += minTime;
		} else {
			lastRead = GET_TICK();
		}

		if(xcrc32) (*xcrc32)(data, bytes);
		instantPause();
		return !stop;
	};
	// Leaf workers may finish out of order; progress reports completed bytes while
	// TigerTreeHasher retains responsibility for publishing leaves in file order.
	auto progress = [this](size_t bytes) {
		Lock l(cs);
		currentSize = bytes >= static_cast<uint64_t>(currentSize) ? 0 : currentSize - static_cast<int64_t>(bytes);
	};
	return TigerTreeHasher::hash(file, size, tth.getBlockSize(), workerCount, tth, observe, progress);
}

int HashManager::Hasher::run() {
#ifndef _WIN32
	// Elsewhere this applies to the calling thread only. On Windows whoever starts the hasher sets
	// it (HashManager::hasherPriority), so it can't overwrite a priority set meanwhile.
	setThreadPriority(Thread::IDLE);
#endif

	struct SnapshotChanged { };
	SFVDirectoryReader sfvDirectory;
	// One summary line per folder hashed (unless each file is reported).
	struct FolderStats {
		string path;
		size_t files = 0;
		int64_t bytes = 0;
		uint64_t time = 0;
	} folder;
	auto reportFolder = [&folder] {
		if(folder.files > 0 && !SETTING(LOG_HASHED_FILES)) {
			const auto speed = folder.time > 0 ? folder.bytes * 1000 / static_cast<int64_t>(folder.time) : 0;
			LogManager::getInstance()->message(str(F_("Finished hashing %1% (%2% files, %3% at %4%/s)") %
				Util::addBrackets(folder.path) % folder.files % Util::formatBytes(folder.bytes) % Util::formatBytes(speed)),
				LogMessage::SEV_INFO, _("Hashing"));
		}
		folder = FolderStats();
	};
	string fname;
	int64_t requestedSize = 0;
	uint32_t requestedTimestamp = 0;
	uint64_t requestedJobId = 0;

	for(;;) {
		s.wait();
		if(stop)
			break;
		if(rebuild) {
			HashManager::getInstance()->doRebuild();
			rebuild = false;
			LogManager::getInstance()->message(_("Hash database rebuilt"), LogMessage::SEV_INFO, _("Hash database"));
			s.signal();
			continue;
		}
		{
			Lock l(cs);
			if(!w.empty()) {
				auto item = w.begin();
				currentFile = fname = item->first;
				// Stays counted while it is hashed, so more files of its volume come here.
				currentVolume = HashManager::getVolume(fname);
				const auto request = item->second;
				w.erase(item);
				dequeued(request);
				// The next snapshot of the same path, if any, takes its place in path order.
				auto next = waiting.find(fname);
				if(next != waiting.end()) {
					w.emplace(fname, next->second);
					waiting.erase(next);
				}
				currentFileSize = requestedSize = request.size;
				currentTimestamp = requestedTimestamp = request.timestamp;
				currentJobId = requestedJobId = request.jobId;
				currentTerminal = false;
				currentSize = requestedSize;
				running = true;
				idle = false;
			} else {
				fname.clear();
				requestedSize = 0;
				requestedTimestamp = 0;
				requestedJobId = 0;
				idle = true;
				// .sfv files added while idle are read the next time their directory is hashed.
				sfvDirectory.clear();
				reportFolder();
				continue;
			}
		}

		if(!fname.empty()) {
			bool terminalPublished = false;
			auto beginTerminal = [this, &terminalPublished] {
				{
					Lock l(cs);
					currentTerminal = true;
				}
				terminalPublished = true;
			};
			try {
				auto start = GET_TICK();

				File f(fname, File::READ, File::OPEN);
				auto size = f.getSize();
				auto timestamp = f.getLastModified();
				if(pft::hasPrivateMarker(fname)) {
					// Never shared, so not reported as an error either.
					f.close();
					beginTerminal();
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp,
						requestedJobId, HashManagerListener::Failure::PRIVATE_FILE, _("Privately received files are not shared"));
					throw SnapshotChanged();
				}
				if(size != requestedSize || (requestedTimestamp != 0 && timestamp != requestedTimestamp)) {
					// The file changed after it was queued (or a network listing reported a stale
					// time). Whoever asked about the old snapshot gets a definite answer; the
					// current content is hashed anyway, so the next refresh finds it hashed.
					beginTerminal();
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp,
						requestedJobId, HashManagerListener::Failure::FILE_CHANGED, _("The file changed before hashing started"));
					adoptSnapshot(fname, size, timestamp, requestedSize, requestedTimestamp, requestedJobId);
					terminalPublished = false;
				}

				auto sizeLeft = size;
				auto bs = max(TigerTree::calcBlockSize(size, 10), MIN_BLOCK_SIZE);

				TigerTree tt(bs);

				CRC32Filter crc32;
				uint32_t expectedCrc = 0;
				CRC32Filter* xcrc32 = sfvDirectory.getCRC(fname, expectedCrc) ? &crc32 : nullptr;

				auto lastRead = GET_TICK();

				const auto configuredWorkers = std::clamp(SETTING(HASHING_THREADS), SettingsManager::HASHING_THREADS_MIN, SettingsManager::HASHING_THREADS_MAX);
				const auto workerCount = TigerTreeHasher::getWorkerCount(static_cast<size_t>(configuredWorkers), size, bs);
				if(workerCount > 1) {
					if(fastHash(f, tt, size, xcrc32, workerCount, lastRead)) sizeLeft = 0;
				} else {
					FileReader fr(true);
					auto process = [&](const void* buf, size_t n) {
						const auto maxSpeed = SETTING(MAX_HASH_SPEED);
						if(maxSpeed > 0) {
							const auto now = GET_TICK();
							const auto minTime = n * 1000ULL / (static_cast<uint64_t>(maxSpeed) * 1024ULL * 1024ULL);
							if(lastRead + minTime > now) Thread::sleep(static_cast<uint32_t>(lastRead + minTime - now));
							lastRead += minTime;
						} else {
							lastRead = GET_TICK();
						}

						tt.update(buf, n);
						if(xcrc32) (*xcrc32)(buf, n);
						{
							Lock l(cs);
							currentSize = n >= static_cast<uint64_t>(currentSize) ? 0 : currentSize - static_cast<int64_t>(n);
						}
						sizeLeft -= static_cast<int64_t>(n);
						instantPause();
						return !stop;
					};
#ifdef _WIN32
					// Unbuffered overlapped reads keep large files out of the system cache; smaller
					// ones are read through the snapshot handle, saving a second open (a network
					// round trip on a NAS) per file.
					if(size >= DIRECT_READ_MIN_SIZE) {
						fr.read(fname, process);
					} else {
						fr.read(f, process);
					}
#else
					fr.read(f, process);
#endif
					tt.finalize();
				}

				// Keep the write-denying snapshot handle alive through hashDone and
				// listener publication. On Windows this prevents the path from being
				// replaced between the final read and snapshot attribution.
				if(sizeLeft == 0 && (f.getSize() != size || f.getLastModified() != timestamp)) {
					const auto detail = _("The file changed while it was being hashed");
					beginTerminal();
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp, requestedJobId, HashManagerListener::Failure::FILE_CHANGED, detail);
					LogManager::getInstance()->message(str(F_("%1% not shared; the file changed while it was being hashed.") % Util::addBrackets(fname)), LogMessage::SEV_WARNING, _("Hashing"));
					throw SnapshotChanged();
				}
				uint64_t end = GET_TICK();
				int64_t speed = 0;
				if(end > start) {
					speed = size * 1000 / (end - start);
				}

				if(sizeLeft != 0) {
					beginTerminal();
					LogManager::getInstance()->message(str(F_("%1% not shared; hashing did not complete.") % Util::addBrackets(fname)),
						LogMessage::SEV_WARNING, _("Hashing"));
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp,
						requestedJobId,
						stop ? HashManagerListener::Failure::CANCELLED : HashManagerListener::Failure::INCOMPLETE,
						stop ? _("Hashing was cancelled") : _("Hashing did not read the complete file"));
				} else if(xcrc32 && xcrc32->getValue() != expectedCrc) {
					beginTerminal();
					LogManager::getInstance()->message(str(F_("%1% not shared; calculated CRC32 does not match the one found in SFV file.") % Util::addBrackets(fname)),
						LogMessage::SEV_WARNING, _("Hashing"));
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp,
						requestedJobId,
						HashManagerListener::Failure::CRC_MISMATCH, _("The file failed its SFV CRC32 check"));
				} else {
					beginTerminal();
					HashManager::getInstance()->hashDone(fname, timestamp, tt, speed, size, requestedJobId);
					const auto path = Util::getFilePath(fname);
					if(path != folder.path) {
						reportFolder();
						folder.path = path;
					}
					folder.files++;
					folder.bytes += size;
					folder.time += end > start ? end - start : 0;
				}
			} catch(const SnapshotChanged&) {
				// The exact failure was already reported above.
			} catch(const FileException& e) {
				if(!terminalPublished) {
					beginTerminal();
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp,
						requestedJobId,
						HashManagerListener::Failure::FILE_IO, e.getError());
					LogManager::getInstance()->message(str(F_("Error hashing %1%: %2%") % Util::addBrackets(fname) % e.getError()),
						LogMessage::SEV_ERROR, _("Hashing"));
				}
			} catch(const Exception& e) {
				if(!terminalPublished) {
					beginTerminal();
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp,
						requestedJobId,
						HashManagerListener::Failure::FILE_IO, e.getError());
					LogManager::getInstance()->message(str(F_("Error hashing %1%: %2%") % Util::addBrackets(fname) % e.getError()),
						LogMessage::SEV_ERROR, _("Hashing"));
				}
			} catch(const std::exception& e) {
				if(!terminalPublished) {
					beginTerminal();
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp,
						requestedJobId,
						HashManagerListener::Failure::FILE_IO, e.what());
				}
			} catch(...) {
				if(!terminalPublished) {
					beginTerminal();
					HashManager::getInstance()->hashFailed(fname, requestedSize, requestedTimestamp,
						requestedJobId,
						HashManagerListener::Failure::FILE_IO, _("Unknown hashing error"));
				}
			}
		}
		{
			Lock l(cs);
			volumeDone(currentVolume);
			currentVolume.clear();
			currentFile.clear();
			currentSize = 0;
			currentFileSize = 0;
			currentTimestamp = 0;
			currentJobId = 0;
			currentTerminal = false;
			running = false;
		}
		s.signal();
	}
	return 0;
}

HashManager::HashPauser::HashPauser() {
	HashManager::getInstance()->pauseHashing();
}

HashManager::HashPauser::~HashPauser() {
	HashManager::getInstance()->resumeHashing();
}

bool HashManager::pauseHashing() noexcept {
	// Every hasher, and any added while paused (they start with this count).
	Lock l(hashersCs);
	for(const auto& hasher: hashers) {
		hasher->pause();
	}
	return hashersPaused++ > 0;
}

void HashManager::resumeHashing() noexcept {
	Lock l(hashersCs);
	dcassert(hashersPaused > 0);
	if(hashersPaused == 0) {
		return;
	}
	--hashersPaused;
	for(const auto& hasher: hashers) {
		hasher->resume();
	}
}

bool HashManager::isHashingPaused() const noexcept {
	Lock l(hashersCs);
	return hashersPaused > 0;
}

} // namespace dcpp
