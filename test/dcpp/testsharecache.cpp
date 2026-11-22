/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <thread>

#define private public
#include <dcpp/bbs/BBSManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dcpp/queue/QueueManager.h>
#undef private

#include <dcpp/hub/ClientManager.h>
#include <dcpp/message/RichText.h>
#include <dcpp/user/CID.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/core/io/File.h>
#include <dcpp/core/io/SFVReader.h>
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/search/SearchResult.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/core/io/db/SQLiteDB.h>
#include <dcpp/core/io/stream/Streams.h>
#include <dcpp/core/io/xml/SimpleXML.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/transfer/Transfer.h>
#include <dcpp/transfer/upload/Upload.h>
#include <dcpp/util/Util.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/transfer/private/PrivateTransferStorage.h>

using namespace dcpp;

namespace {

class ShareCacheTest : public ::testing::Test {
public:
	void SetUp() override {
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto base = std::filesystem::temp_directory_path() /
			("dcpp-sharecache-test-" + std::to_string(ticks) + "-" + std::to_string(++counter));
		configPath = base.string() + PATH_SEPARATOR;
		sharePath = configPath + "Share" PATH_SEPARATOR_STR;
		secondSharePath = configPath + "SecondShare" PATH_SEPARATOR_STR;
		std::filesystem::remove_all(base);
		std::filesystem::create_directories(sharePath);
		std::filesystem::create_directories(secondSharePath);

		Util::PathsMap paths;
		paths[Util::PATH_USER_CONFIG] = configPath;
		paths[Util::PATH_USER_LOCAL] = configPath;
		paths[Util::PATH_GLOBAL_CONFIG] = configPath;
		paths[Util::PATH_RESOURCES] = configPath;
		paths[Util::PATH_LOCALE] = configPath;
		paths[Util::PATH_DOWNLOADS] = configPath;
		paths[Util::PATH_FILE_LISTS] = configPath;
		paths[Util::PATH_HUB_LISTS] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);

		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		HashManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		UploadManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
		BBSManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
	}

	void TearDown() override {
		BBSManager::deleteInstance();
		ShareManager::deleteInstance();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		QueueManager::deleteInstance();
		UploadManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

	void populateShare() {
		auto sm = ShareManager::getInstance();
		sm->shares[sharePath] = "Virtual";
		std::filesystem::create_directories(sharePath + "Child");
		std::ofstream(sharePath + "Child" PATH_SEPARATOR_STR "file.bin") << "data";

		auto root = ShareManager::Directory::create("Virtual", ShareManager::Directory::Ptr(), 1700000000);
		auto child = ShareManager::Directory::create("Child", root, 1700000100);
		root->directories.emplace("Child", child);

		ShareManager::Directory::File file("file.bin", 1234, child,
			TTHValue("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), 1700000200);
		child->files.insert(std::move(file));

		sm->directories["Virtual"] = root;
		sm->rebuildIndices(1);
	}

	void populateMergedShares() {
		auto sm = ShareManager::getInstance();
		sm->shares[sharePath] = "Virtual";
		sm->shares[secondSharePath] = "Virtual";
		std::filesystem::create_directories(sharePath + "Child");
		std::filesystem::create_directories(secondSharePath + "Other");
		std::ofstream(sharePath + "Child" PATH_SEPARATOR_STR "file.bin") << "data";
		std::ofstream(secondSharePath + "Other" PATH_SEPARATOR_STR "other.bin") << "more";

		auto root = ShareManager::Directory::create("Virtual");
		auto child = ShareManager::Directory::create("Child", root);
		auto other = ShareManager::Directory::create("Other", root);
		root->directories.emplace("Child", child);
		root->directories.emplace("Other", other);

		child->files.insert(ShareManager::Directory::File("file.bin", 1234, child,
			TTHValue("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")));
		other->files.insert(ShareManager::Directory::File("other.bin", 2345, other,
			TTHValue("BAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")));

		sm->directories["Virtual"] = root;
		sm->rebuildIndices(2);
	}

	void clearLoadedShare() {
		auto sm = ShareManager::getInstance();
		sm->directories.clear();
		sm->tthIndex.clear();
		sm->sharedBytes = 0;
		sm->bloom.clear();
	}

	string readStream(std::unique_ptr<MemoryInputStream> stream) {
		if(!stream) {
			return Util::emptyString;
		}
		string result(stream->getSize(), '\0');
		size_t size = result.size();
		stream->read(result.data(), size);
		result.resize(size);
		return result;
	}

	struct CachedFile {
		string path;
		int64_t size;
		uint32_t timestamp;
		TTHValue tth;
	};

	CachedFile cacheFile(const string& path, const string& contents) {
		{
			std::ofstream output(path, std::ios::binary);
			output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		}

		TigerTree tree(HashManager::MIN_BLOCK_SIZE);
		tree.update(contents.data(), contents.size());
		tree.finalize();

		File file(path, File::READ, File::OPEN | File::SHARED);
		const auto size = file.getSize();
		const auto timestamp = file.getLastModified();
		file.close();
		EXPECT_EQ(size, static_cast<int64_t>(contents.size()));
		EXPECT_TRUE(HashManager::getInstance()->verifyFileTTH(path, size, tree.getRoot()));
		return { path, size, timestamp, tree.getRoot() };
	}

	/** @param padding Bytes of ignored empty elements after the item; the queue loader only reports
	 * progress when it reads a new 64 KiB chunk, so padding guarantees a progress callback while
	 * loading continues after the item's add. (Plain whitespace would overflow the XML reader's
	 * 64 KiB content limit.) */
	void writeQueueFile(const string& target, int64_t size, const TTHValue& tth, size_t padding = 0) {
		string escaped;
		string base32;
		string filler;
		for(size_t i = 0; i < padding / 4; ++i) {
			filler += "<P/>";
		}
		std::ofstream(QueueManager::getInstance()->getQueueFile(), std::ios::binary | std::ios::trunc)
			<< SimpleXML::utf8Header << "<Downloads Version=\"1\">\r\n\t<Download Target=\""
			<< SimpleXML::escape(target, escaped, true) << "\" Size=\"" << size
			<< "\" Priority=\"3\" Added=\"1\" TTH=\"" << tth.toBase32(base32) << "\"/>\r\n" << filler << "</Downloads>\r\n";
	}

	string configPath;
	string sharePath;
	string secondSharePath;
	static int counter;
};

class BBSProbe : public BBSManagerListener {
public:
	void on(DocumentUpdated, const string& hubUrl, const string& board, const string& tth) noexcept override {
		updates.push_back(hubUrl + '\n' + board + '\n' + tth);
	}

	void on(Status, const string& hubUrl, const string& message) noexcept override {
		statuses.push_back(hubUrl + '\n' + message);
	}

	vector<string> updates;
	vector<string> statuses;
};

int ShareCacheTest::counter = 0;

} // namespace

TEST_F(ShareCacheTest, round_trips_share_tree_and_indices) {
	populateShare();
	ASSERT_EQ(ShareManager::getInstance()->getSharedFiles(), 1U);
	ASSERT_EQ(ShareManager::getInstance()->getShareSize(), 1234);

	ShareManager::getInstance()->saveShareCache();
	ASSERT_GE(File::getSize(configPath + "ShareCache.sqlite3"), 0);

	clearLoadedShare();
	ASSERT_TRUE(ShareManager::getInstance()->loadShareCache());

	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 1U);
	EXPECT_EQ(ShareManager::getInstance()->getShareSize(), 1234);
	EXPECT_TRUE(ShareManager::getInstance()->isTTHShared(TTHValue("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")));
	EXPECT_EQ(ShareManager::getInstance()->toReal("/Virtual/Child/file.bin"), sharePath + "Child" PATH_SEPARATOR_STR "file.bin");

	auto child = ShareManager::getInstance()->directories["Virtual"]->directories["Child"];
	EXPECT_EQ(1700000000, ShareManager::getInstance()->directories["Virtual"]->getLastWrite());
	EXPECT_EQ(1700000100, child->getLastWrite());
	auto file = child->findFile("file.bin");
	ASSERT_NE(file, child->files.cend());
	// Derived from the directories (one folder shares this name), so not stored per file.
	EXPECT_FALSE(file->realPath);
	EXPECT_EQ(file->getRealPath(), sharePath + "Child" PATH_SEPARATOR_STR "file.bin");
	EXPECT_EQ(1700000200, file->getLastWrite());
}

TEST_F(ShareCacheTest, replaces_unreadable_cache_file_on_save) {
	{
		const string garbage(8192, 'x');
		std::ofstream corrupt(configPath + "ShareCache.sqlite3", std::ios::binary | std::ios::trunc);
		corrupt.write(garbage.data(), static_cast<std::streamsize>(garbage.size()));
	}
	populateShare();
	EXPECT_FALSE(ShareManager::getInstance()->loadShareCache());

	// The cache is derived data: saving must recover instead of failing on every later save.
	ShareManager::getInstance()->saveShareCache();
	clearLoadedShare();
	ASSERT_TRUE(ShareManager::getInstance()->loadShareCache());
	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 1U);
}

#ifdef _WIN32

TEST_F(ShareCacheTest, preserves_long_unicode_file_paths) {
	auto sm = ShareManager::getInstance();
	sm->shares[sharePath] = "Virtual";

	auto root = ShareManager::Directory::create("Virtual");
	auto child = ShareManager::Directory::create("Child", root);
	root->directories.emplace("Child", child);

	string segment;
	for(size_t i = 0; i < 100; ++i) {
		segment += "\xE6\xB5\x8B";
	}
	string longPath = sharePath;
	while(longPath.size() <= 32768) {
		longPath += segment + PATH_SEPARATOR_STR;
	}
	longPath += "file.bin";
	ASSERT_LT(File::toNativePath(longPath).size(), 32767U);

	ShareManager::Directory::File file("file.bin", 1234, child,
		TTHValue("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
	file.realPath = longPath;
	child->files.insert(std::move(file));
	sm->directories["Virtual"] = root;
	sm->rebuildIndices(1);

	sm->saveShareCache();
	clearLoadedShare();
	ASSERT_TRUE(sm->loadShareCache());
	EXPECT_EQ(longPath, sm->toReal("/Virtual/Child/file.bin"));
}

#endif

TEST_F(ShareCacheTest, emits_dates_only_for_non_recursive_partial_file_items) {
	populateShare();

	const auto partial = readStream(std::unique_ptr<MemoryInputStream>(
		ShareManager::getInstance()->generatePartialList("/Virtual/Child/", false)));
	EXPECT_NE(string::npos, partial.find("BaseDate=\"1700000100\""));
	EXPECT_NE(string::npos, partial.find("<File Name=\"file.bin\" Size=\"1234\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Date=\"1700000200\"/>"));

	const auto recursive = readStream(std::unique_ptr<MemoryInputStream>(
		ShareManager::getInstance()->generatePartialList("/Virtual/Child/", true)));
	EXPECT_NE(string::npos, recursive.find("BaseDate=\"1700000100\""));
	EXPECT_NE(string::npos, recursive.find("<File Name=\"file.bin\" Size=\"1234\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"/>"));
	EXPECT_EQ(string::npos, recursive.find("TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Date="));

	string full;
	StringRefOutputStream fullOutput(full);
	string indent;
	string tmp;
	ShareManager::getInstance()->directories["Virtual"]->toXml(fullOutput, indent, tmp, -1, false);
	EXPECT_NE(string::npos, full.find("<Directory Name=\"Virtual\" Date=\"1700000000\""));
	EXPECT_EQ(string::npos, full.find("TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Date="));
}

TEST_F(ShareCacheTest, incomplete_partial_directories_include_size_and_content_metadata) {
	auto sm = ShareManager::getInstance();
	sm->shares[sharePath] = "Virtual";
	auto root = ShareManager::Directory::create("Virtual", ShareManager::Directory::Ptr(), 1700000000);
	auto large = ShareManager::Directory::create("Large", root, 1700000100);
	root->directories.emplace("Large", large);
	for(int i = 0; i < 5; ++i) {
		string tth(39, 'A');
		tth[0] = static_cast<char>('A' + i);
		large->files.insert(ShareManager::Directory::File("file" + std::to_string(i) + ".bin", 10, large,
			TTHValue(tth), 1700000200 + i));
	}
	sm->directories["Virtual"] = root;
	sm->rebuildIndices(5);

	const auto partial = readStream(std::unique_ptr<MemoryInputStream>(sm->generatePartialList("/", false)));
	EXPECT_NE(string::npos, partial.find("<Directory Name=\"Large\" Date=\"1700000100\" Size=\"50\" Directories=\"0\" Files=\"5\" Incomplete=\"1\" />"));
}

TEST_F(ShareCacheTest, preserves_merged_shares_search_and_protocol_lookups) {
	populateMergedShares();
	ASSERT_EQ(ShareManager::getInstance()->getSharedFiles(), 2U);
	ASSERT_EQ(ShareManager::getInstance()->getShareSize(), 3579);

	ShareManager::getInstance()->saveShareCache();
	clearLoadedShare();
	ASSERT_TRUE(ShareManager::getInstance()->loadShareCache());

	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 2U);
	EXPECT_EQ(ShareManager::getInstance()->toVirtual(TTHValue("BAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")),
		"/Virtual/Other/other.bin");
	EXPECT_EQ(ShareManager::getInstance()->toReal("/Virtual/Other/other.bin"),
		secondSharePath + "Other" PATH_SEPARATOR_STR "other.bin");

	auto results = ShareManager::getInstance()->search("other", SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10);
	EXPECT_TRUE(std::any_of(results.begin(), results.end(), [](const SearchResultPtr& result) {
		return result->getFile() == "Virtual\\Other\\other.bin";
	}));
}

TEST_F(ShareCacheTest, full_refresh_saves_cache_snapshot_for_refresh_command_path) {
	ShareManager::getInstance()->shares[sharePath] = "Virtual";
	std::filesystem::create_directories(sharePath + "Fresh");
	std::ofstream(sharePath + "Fresh" PATH_SEPARATOR_STR "new.bin") << "data";

	ShareManager::getInstance()->refresh(true, false, true);

	EXPECT_GE(File::getSize(configPath + "ShareCache.sqlite3"), 0);
	clearLoadedShare();
	EXPECT_TRUE(ShareManager::getInstance()->loadShareCache());
	EXPECT_TRUE(ShareManager::getInstance()->hasVirtual("Virtual"));
}

TEST_F(ShareCacheTest, replaces_old_share_cache_schema_without_self_lock) {
	{
		SQLiteDB db(configPath + "ShareCache.sqlite3");
		db.execute(
			"CREATE TABLE metadata (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) WITHOUT ROWID;"
			"CREATE TABLE directories (id INTEGER PRIMARY KEY NOT NULL, parent_id INTEGER, name TEXT NOT NULL, real_name TEXT);"
			"CREATE TABLE files (id INTEGER PRIMARY KEY NOT NULL, directory_id INTEGER NOT NULL, name TEXT NOT NULL, size INTEGER NOT NULL, tth TEXT, real_path TEXT);"
			"PRAGMA user_version = 1;"
		);
	}

	populateShare();
	ShareManager::getInstance()->saveShareCache();

	clearLoadedShare();
	ASSERT_TRUE(ShareManager::getInstance()->loadShareCache());
	EXPECT_EQ(ShareManager::getInstance()->toReal("/Virtual/Child/file.bin"), sharePath + "Child" PATH_SEPARATOR_STR "file.bin");
}

TEST_F(ShareCacheTest, extension_filter_leaves_matching_files_out_of_the_share) {
	auto sm = ShareManager::getInstance();
	sm->shares[sharePath] = "Virtual";
	cacheFile(sharePath + "keep.txt", "keep");
	cacheFile(sharePath + "skip.nfo", "skip");
	cacheFile(sharePath + "picture.png", "picture");
	cacheFile(sharePath + "SHOUT.NFO", "shout");
	cacheFile(sharePath + "readme.info", "info");
	auto findShared = [](const ShareManager::Directory& dir, const string& name) {
		return dir.findFile(name) != dir.files.end();
	};
	// Every way of writing an extension that people use, in any letter case.
	for(const auto& filter: { "nfo;png", ".nfo;.png", "*.nfo;*.png", " nfo ; png ", "NFO;PNG" }) {
		SettingsManager::getInstance()->set(SettingsManager::SHARING_SKIPLIST_EXTENSIONS, string(filter));
		sm->refresh(true, false, true);
		auto root = sm->directories["Virtual"];
		ASSERT_TRUE(root) << filter;
		EXPECT_TRUE(findShared(*root, "keep.txt")) << filter;
		EXPECT_FALSE(findShared(*root, "skip.nfo")) << filter;
		EXPECT_FALSE(findShared(*root, "picture.png")) << filter;
		EXPECT_FALSE(findShared(*root, "SHOUT.NFO")) << filter;
		// An extension, not any name ending in those letters.
		EXPECT_TRUE(findShared(*root, "readme.info")) << filter;
	}
}

TEST_F(ShareCacheTest, a_refresh_asked_for_during_another_runs_after_it) {
	auto sm = ShareManager::getInstance();
	auto hm = HashManager::getInstance();
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
	}
	cacheFile(sharePath + "keep.txt", "keep");
	cacheFile(sharePath + "skip.nfo", "skip");
	sm->refresh(true, false, true);
	ASSERT_TRUE(sm->directories["Virtual"]->findFile("skip.nfo") != sm->directories["Virtual"]->files.end());

	{
		// The scan reads the hash store, so it waits here while a filter is added.
		Lock l(hm->cs);
		sm->refresh(true);
		ASSERT_TRUE(sm->isRefreshing());
		SettingsManager::getInstance()->set(SettingsManager::SHARING_SKIPLIST_EXTENSIONS, string("nfo"));
		// Settings asks for a refresh while the first one runs: it is queued, not refused.
		sm->refresh(true);
	}
	sm->join();
	EXPECT_FALSE(sm->isRefreshing());
	auto root = sm->directories["Virtual"];
	EXPECT_TRUE(root->findFile("keep.txt") != root->files.end());
	EXPECT_TRUE(root->findFile("skip.nfo") == root->files.end()) << "The queued refresh did not run with the new filter";
}

TEST_F(ShareCacheTest, rejects_cache_when_share_settings_change) {
	populateShare();
	ShareManager::getInstance()->saveShareCache();

	SettingsManager::getInstance()->set(SettingsManager::SHARING_SKIPLIST_EXTENSIONS, ".bin");
	clearLoadedShare();

	EXPECT_FALSE(ShareManager::getInstance()->loadShareCache());
	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 0U);
}

TEST_F(ShareCacheTest, loads_cache_when_already_shared_downloads_are_blocked) {
	populateShare();
	ShareManager::getInstance()->saveShareCache();

	SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, true);
	clearLoadedShare();

	EXPECT_TRUE(ShareManager::getInstance()->loadShareCache());
	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 1U);
}

TEST_F(ShareCacheTest, keeps_queued_file_listed_only_by_stale_cache) {
	// The cache says AAAA... is shared, but the real file on disk was never hashed, so the
	// live scan will not share it: the queued download must survive both queue load and scan.
	auto sm = ShareManager::getInstance();
	auto qm = QueueManager::getInstance();
	populateShare();
	sm->saveShareCache();
	clearLoadedShare();
	SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, true);

	const TTHValue cachedTTH("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
	writeQueueFile(configPath + "stale.bin", 1234, cachedTTH);

	ASSERT_TRUE(sm->loadShareCache());
	sm->cacheUnverified = true; // what startupRefresh sets before its background scan
	ASSERT_TRUE(sm->isTTHShared(cachedTTH));

	qm->loadQueue([](float) { });
	EXPECT_EQ(1U, qm->getTargets(cachedTTH).size());

	sm->refresh(true, false, true);
	EXPECT_TRUE(sm->isShareVerified());
	EXPECT_FALSE(sm->isTTHShared(cachedTTH));
	EXPECT_EQ(1U, qm->getTargets(cachedTTH).size());
}

TEST_F(ShareCacheTest, removes_queued_file_once_live_scan_confirms_it_is_shared) {
	auto sm = ShareManager::getInstance();
	auto qm = QueueManager::getInstance();
	const auto shared = cacheFile(sharePath + "real.bin", "really shared contents");
	sm->shares[sharePath] = "Virtual";
	SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, true);
	writeQueueFile(configPath + "wanted.bin", shared.size, shared.tth);

	sm->cacheUnverified = true;
	qm->loadQueue([](float) { });
	EXPECT_EQ(1U, qm->getTargets(shared.tth).size());

	sm->refresh(true, false, true);
	EXPECT_TRUE(sm->isShareVerified());
	EXPECT_TRUE(sm->isTTHShared(shared.tth));
	EXPECT_TRUE(qm->getTargets(shared.tth).empty());
}

TEST_F(ShareCacheTest, queue_load_checks_deferred_items_when_scan_finished_first) {
	// The scan finished between an item's deferral and the end of queue load, so the share's
	// own pass ran too early to see it; loadQueue's closing check must catch it.
	auto sm = ShareManager::getInstance();
	auto qm = QueueManager::getInstance();
	const auto shared = cacheFile(sharePath + "real.bin", "shared while queue loads");
	sm->shares[sharePath] = "Virtual";
	SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, true);
	writeQueueFile(configPath + "late.bin", shared.size, shared.tth, 128 * 1024);

	sm->refresh(true, false, true);
	ASSERT_TRUE(sm->isTTHShared(shared.tth));

	// The item is deferred while the flag is set; the scan "finishes" right after it is added.
	sm->cacheUnverified = true;
	bool sawItem = false;
	qm->loadQueue([&](float) {
		if(!sawItem && !qm->getTargets(shared.tth).empty()) {
			sawItem = true;
			sm->cacheUnverified = false;
		}
	});
	ASSERT_TRUE(sawItem);
	EXPECT_TRUE(qm->getTargets(shared.tth).empty());
}

TEST_F(ShareCacheTest, temp_shares_are_searchable_and_downloadable_only_on_their_route) {
	auto sm = ShareManager::getInstance();
	const auto path = configPath + "dropped photo.jpg";
	const string route = "adc://example.invalid";
	const string contents = "temporary attachment";
	std::ofstream(path, std::ios::binary) << contents;
	TigerTree tree(HashManager::MIN_BLOCK_SIZE);
	tree.update(contents.data(), contents.size());
	tree.finalize();
	File file(path, File::READ, File::OPEN | File::SHARED);
	const auto timestamp = file.getLastModified();
	file.close();
	ASSERT_TRUE(HashManager::getInstance()->verifyFileTTH(path, contents.size(), tree.getRoot()));
	ASSERT_TRUE(sm->addTempShare(path, contents.size(), timestamp, tree.getRoot(), route));
	const string nmdcRoute = "dchub://legacy.example.invalid";
	ASSERT_TRUE(sm->addTempShare(path, contents.size(), timestamp, tree.getRoot(), nmdcRoute));

	EXPECT_EQ(sm->getSharedFiles(), 0U);
	EXPECT_EQ(sm->getShareSize(), 0);
	EXPECT_TRUE(sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10).empty());

	const string matchingRoute = "ADC://EXAMPLE.INVALID";
	const string otherRoute = "adc://other.invalid";
	const auto matchingResults = sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, matchingRoute);
	ASSERT_EQ(matchingResults.size(), 1U);
	EXPECT_EQ(matchingResults.front()->getType(), SearchResult::TYPE_FILE);
	EXPECT_EQ(matchingResults.front()->getFile(), "dropped photo.jpg");
	EXPECT_EQ(matchingResults.front()->getSize(), static_cast<int64_t>(contents.size()));
	EXPECT_EQ(matchingResults.front()->getTTH(), tree.getRoot());
	EXPECT_TRUE(sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, otherRoute).empty());
	const auto nmdcResults = sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, nmdcRoute);
	ASSERT_EQ(nmdcResults.size(), 1U);
	EXPECT_EQ(nmdcResults.front()->getTTH(), tree.getRoot());

	const auto adcNameResults = sm->search(StringList { "ANphoto", "EXjpg" }, 10, matchingRoute);
	ASSERT_EQ(adcNameResults.size(), 1U);
	EXPECT_EQ(adcNameResults.front()->getTTH(), tree.getRoot());
	const auto adcTthResults = sm->search(StringList { "TR" + tree.getRoot().toBase32() }, 10, matchingRoute);
	ASSERT_EQ(adcTthResults.size(), 1U);
	EXPECT_EQ(adcTthResults.front()->getTTH(), tree.getRoot());
	EXPECT_TRUE(sm->search(StringList { "TR" + tree.getRoot().toBase32() }, 10, otherRoute).empty());

	EXPECT_EQ(sm->toRealWithSize("TTH/" + tree.getRoot().toBase32(), matchingRoute),
		std::make_pair(path, static_cast<int64_t>(contents.size())));
	EXPECT_EQ(sm->toRealWithSize("TTH/" + tree.getRoot().toBase32(), nmdcRoute),
		std::make_pair(path, static_cast<int64_t>(contents.size())));
	EXPECT_THROW(sm->toRealWithSize("TTH/" + tree.getRoot().toBase32(), otherRoute), ShareException);
	const auto resolved = sm->resolveFile("TTH/" + tree.getRoot().toBase32(), matchingRoute);
	EXPECT_EQ(resolved.realPath, path);
	EXPECT_EQ(resolved.size, static_cast<int64_t>(contents.size()));
	EXPECT_TRUE(resolved.temporary);
	EXPECT_FALSE(resolved.exactOnly);

	ASSERT_NE(sm->findTempShare(tree.getRoot(), &matchingRoute), nullptr);
	EXPECT_EQ(sm->findTempShare(tree.getRoot(), &otherRoute), nullptr);
	EXPECT_TRUE(sm->isTempShare(tree.getRoot(), path, matchingRoute));
	EXPECT_FALSE(sm->isTempShare(tree.getRoot(), path, otherRoute));
	const auto tempShares = sm->getTempShares();
	ASSERT_EQ(tempShares.size(), 2U);
	EXPECT_EQ(tempShares.front().realPath, path);

	SettingsManager::getInstance()->set(SettingsManager::ENABLE_RTF_TEMP_SHARES, false);
	EXPECT_TRUE(sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, matchingRoute).empty());
	EXPECT_THROW(sm->toRealWithSize("TTH/" + tree.getRoot().toBase32(), matchingRoute), ShareException);
	EXPECT_FALSE(sm->removeTempShare(path, tree.getRoot(), otherRoute));
	EXPECT_TRUE(sm->removeTempShare(path, tree.getRoot(), matchingRoute));
	EXPECT_TRUE(sm->removeTempShare(path, tree.getRoot(), nmdcRoute));
	EXPECT_TRUE(sm->getTempShares().empty());
}

TEST_F(ShareCacheTest, private_chat_attachments_are_only_for_their_recipient) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://private.example.invalid";
	const auto alice = CID::generate();
	const auto bob = CID::generate();
	const auto file = cacheFile(configPath + "passport scan.pdf", "private chat attachment");
	const auto tth = "TTH/" + file.tth.toBase32();
	ASSERT_TRUE(sm->addTempShare(file.path, file.size, file.timestamp, file.tth, route, alice));

	// Nobody finds it by searching, not even by its exact TTH.
	EXPECT_TRUE(sm->search("passport", SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10, route).empty());
	EXPECT_TRUE(sm->search(StringList { "ANpassport" }, 10, route).empty());
	EXPECT_TRUE(sm->search(StringList { "TR" + file.tth.toBase32() }, 10, route).empty());

	// Only the recipient can resolve, inspect or download it.
	EXPECT_THROW(sm->resolveFile(tth, route), ShareException);
	EXPECT_THROW(sm->resolveFile(tth, route, bob), ShareException);
	const auto resolved = sm->resolveFile(tth, route, alice);
	EXPECT_EQ(resolved.realPath, file.path);
	EXPECT_TRUE(resolved.temporary);
	EXPECT_THROW(sm->getTTH(tth, route, bob), ShareException);
	EXPECT_EQ(sm->getTTH(tth, route, alice), file.tth);
	EXPECT_THROW(sm->getFileInfo(tth, route, bob), ShareException);
	EXPECT_NO_THROW(sm->getFileInfo(tth, route, alice));
	EXPECT_THROW(sm->toVirtual(file.tth, route, bob), ShareException);
	EXPECT_EQ(sm->toVirtual(file.tth, route, alice), tth);
	EXPECT_TRUE(sm->getRealPaths(file.tth, route, bob).empty());
	EXPECT_EQ(sm->getRealPaths(file.tth, route, alice), StringList { file.path });
	EXPECT_EQ(std::unique_ptr<MemoryInputStream>(sm->getTree(tth, route, bob)), nullptr);
	EXPECT_THROW(sm->toRealWithSize(tth), ShareException);
	EXPECT_THROW(sm->getTTH(tth), ShareException);

	// Messages may promise it only in the private chat it was shared in.
	EXPECT_FALSE(sm->validateChatAttachment(file.tth, file.size, route));
	EXPECT_FALSE(sm->validateChatAttachment(file.tth, file.size, route, bob));
	EXPECT_TRUE(sm->validateChatAttachment(file.tth, file.size, route, alice));
	const auto message = "**Attachment** " + RichText::makeAttachmentMarkdown("passport scan.pdf",
		file.tth.toBase32(), file.size, false);
	auto privateMessage = message;
	EXPECT_TRUE(RichText::prepareOutgoingMessage(privateMessage, true, route, alice));
	auto otherMessage = message;
	EXPECT_FALSE(RichText::prepareOutgoingMessage(otherMessage, true, route, bob));
	auto hubMessage = message;
	EXPECT_FALSE(RichText::prepareOutgoingMessage(hubMessage, true, route));

	const auto shares = sm->getTempShares();
	ASSERT_EQ(shares.size(), 1U);
	EXPECT_EQ(shares.front().recipient, alice);
	EXPECT_FALSE(sm->isTempShare(file.tth, file.path, route));
	EXPECT_TRUE(sm->isTempShare(file.tth, file.path, route, alice));

	// The same file posted in the hub chat is for everyone there, as before.
	ASSERT_TRUE(sm->addTempShare(file.path, file.size, file.timestamp, file.tth, route));
	EXPECT_EQ(sm->resolveFile(tth, route, bob).realPath, file.path);
	EXPECT_EQ(sm->search("passport", SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10, route).size(), 1U);
	EXPECT_EQ(sm->getTempShares().size(), 2U);

	EXPECT_FALSE(sm->removeTempShare(file.path, file.tth, route, bob));
	EXPECT_TRUE(sm->removeTempShare(file.path, file.tth, route, alice));
	EXPECT_TRUE(sm->removeTempShare(file.path, file.tth, route));
	EXPECT_TRUE(sm->getTempShares().empty());
}

TEST_F(ShareCacheTest, uploads_serve_a_private_chat_attachment_only_to_its_recipient) {
	auto sm = ShareManager::getInstance();
	auto um = UploadManager::getInstance();
	const string route = "adc://private.example.invalid";
	const auto alice = ClientManager::getInstance()->getUser(CID::generate());
	const auto bob = ClientManager::getInstance()->getUser(CID::generate());
	const auto file = cacheFile(configPath + "contract.pdf", "attachment for one recipient");
	ASSERT_TRUE(sm->addTempShare(file.path, file.size, file.timestamp, file.tth, route, alice->getCID()));

	// Returns the path the upload would read, or nothing when the request is refused.
	auto request = [&](const UserPtr& user, Transfer::Type type) -> optional<string> {
		UserConnection connection(false);
		connection.setHubUrl(route);
		if(user) connection.setUser(user);
		if(!um->prepareFile(connection, Transfer::names[type], "TTH/" + file.tth.toBase32(), 0, -1)) {
			return nullopt;
		}
		Upload* upload;
		{
			Lock l(um->cs);
			upload = um->uploads.back();
		}
		auto path = upload->getPath();
		um->removeUpload(upload);
		return path;
	};

	EXPECT_FALSE(request(bob, Transfer::TYPE_FILE));
	EXPECT_FALSE(request(nullptr, Transfer::TYPE_FILE));
	EXPECT_FALSE(request(bob, Transfer::TYPE_TREE));
	const auto served = request(alice, Transfer::TYPE_FILE);
	ASSERT_TRUE(served);
	EXPECT_EQ(*served, file.path);
}

TEST_F(ShareCacheTest, preparing_a_private_chat_attachment_shares_it_with_the_recipient_only) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://private.example.invalid";
	const auto alice = CID::generate();
	const auto cached = cacheFile(configPath + "holiday.png", "private attachment preparation");
	optional<ShareManager::ChatAttachmentResult> result;

	sm->prepareChatAttachment(cached.path, route, alice,
		[&result](ShareManager::ChatAttachmentResult value) { result = std::move(value); });

	ASSERT_TRUE(result);
	ASSERT_TRUE(result->attachment);
	EXPECT_TRUE(result->attachment->temporary);
	EXPECT_EQ(result->attachment->recipient, alice);
	EXPECT_TRUE(sm->validateChatAttachment(cached.tth, cached.size, route, alice));
	EXPECT_FALSE(sm->validateChatAttachment(cached.tth, cached.size, route, CID::generate()));
	EXPECT_TRUE(sm->search("holiday", SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10, route).empty());
}

TEST_F(ShareCacheTest, hub_url_keys_identify_a_hub_however_its_address_is_written) {
	EXPECT_EQ(Util::hubUrlKey("hub.example.invalid"), "nmdc://hub.example.invalid:411");
	EXPECT_EQ(Util::hubUrlKey("dchub://HUB.example.invalid"), "nmdc://hub.example.invalid:411");
	EXPECT_EQ(Util::hubUrlKey(" nmdc://hub.example.invalid:411/ "), "nmdc://hub.example.invalid:411");
	EXPECT_EQ(Util::hubUrlKey("DCHUB://hub.example.invalid:411"), "nmdc://hub.example.invalid:411");
	EXPECT_EQ(Util::hubUrlKey("nmdcs://hub.example.invalid"), "nmdcs://hub.example.invalid:411");
	EXPECT_EQ(Util::hubUrlKey("adcs://hub.example.invalid:1511/?kp=SHA256/ABC"), "adcs://hub.example.invalid:1511");
	EXPECT_EQ(Util::hubUrlKey("adc://[2001:DB8::1]:411"), "adc://[2001:db8::1]:411");
	EXPECT_NE(Util::hubUrlKey("adc://hub.example.invalid:1511"), Util::hubUrlKey("adcs://hub.example.invalid:1511"));
	EXPECT_NE(Util::hubUrlKey("hub.example.invalid:411"), Util::hubUrlKey("nmdcs://hub.example.invalid:411"));
	EXPECT_NE(Util::hubUrlKey("hub.example.invalid:411"), Util::hubUrlKey("hub.example.invalid:412"));
}

TEST_F(ShareCacheTest, share_profiles_apply_however_the_hub_address_is_written) {
	populateShare();
	auto sm = ShareManager::getInstance();
	auto fm = FavoriteManager::getInstance();
	FavoriteHubEntry hub;
	hub.setServer("hub.example.invalid:411");
	hub.setPassword("secret");
	hub.setShareDirectories({});
	fm->addFavorite(hub);

	for(const string url: { "hub.example.invalid:411", "dchub://HUB.example.invalid", "nmdc://hub.example.invalid:411/" }) {
		std::set<string> directories { "not cleared" };
		EXPECT_TRUE(fm->getHubShareDirectories(url, directories)) << url;
		EXPECT_TRUE(directories.empty()) << url;
		EXPECT_EQ(sm->getSharedFiles(url), 0U) << url;
		EXPECT_EQ(sm->getShareSizeForHub(url), 0) << url;
		EXPECT_TRUE(sm->search("file", SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10, url).empty()) << url;
		EXPECT_THROW(sm->resolveFile("/Virtual/Child/file.bin", url), ShareException) << url;
	}
	// Other hubs keep the full share, and settings such as the password still
	// follow only the exact address.
	for(const string url: { "nmdcs://hub.example.invalid:411", "hub.example.invalid:412", "other.example.invalid" }) {
		std::set<string> directories;
		EXPECT_FALSE(fm->getHubShareDirectories(url, directories)) << url;
		EXPECT_EQ(sm->getSharedFiles(url), 1U) << url;
	}
	EXPECT_NE(fm->getFavoriteHubEntry("hub.example.invalid:411"), nullptr);
	EXPECT_EQ(fm->getFavoriteHubEntry("dchub://hub.example.invalid"), nullptr);
}

TEST_F(ShareCacheTest, editing_a_favorite_address_keeps_its_share_profile_for_the_old_address) {
	auto fm = FavoriteManager::getInstance();
	FavoriteHubEntry added;
	added.setServer("adc://old.example.invalid:1511");
	added.setPassword("secret");
	added.setShareDirectories({ sharePath });
	fm->addFavorite(added);
	auto entry = fm->getFavoriteHubEntry("adc://old.example.invalid:1511");
	ASSERT_NE(entry, nullptr);

	fm->editFavorite(entry, [](FavoriteHubEntry& e) { e.setServer("adc://new.example.invalid:1511"); });
	std::set<string> directories;
	// A window still open on the old address keeps the profile...
	EXPECT_TRUE(fm->getHubShareDirectories("adc://old.example.invalid:1511", directories));
	EXPECT_EQ(directories, std::set<string> { sharePath });
	EXPECT_TRUE(fm->getHubShareDirectories("adc://new.example.invalid:1511", directories));
	// ...but the password does not follow the favorite to an address it no longer has.
	EXPECT_EQ(fm->getFavoriteHubEntry("adc://old.example.invalid:1511"), nullptr);
	EXPECT_EQ(fm->getFavoriteHubEntry("adc://new.example.invalid:1511"), entry);

	// Clearing the profile clears it for the old address as well.
	fm->editFavorite(entry, [](FavoriteHubEntry& e) { e.clearShareProfile(); });
	EXPECT_FALSE(fm->getHubShareDirectories("adc://old.example.invalid:1511", directories));

	fm->editFavorite(entry, [this](FavoriteHubEntry& e) { e.setShareDirectories({ sharePath }); });
	fm->removeFavorite(entry);
	EXPECT_FALSE(fm->getHubShareDirectories("adc://old.example.invalid:1511", directories));
	EXPECT_FALSE(fm->getHubShareDirectories("adc://new.example.invalid:1511", directories));

	// A favorite that is not stored yet (the Add dialog) is simply changed.
	FavoriteHubEntry pending;
	fm->editFavorite(&pending, [](FavoriteHubEntry& e) { e.setServer("adc://pending.example.invalid"); });
	EXPECT_EQ(pending.getServer(), "adc://pending.example.invalid");
	EXPECT_EQ(fm->getFavoriteHubEntry("adc://pending.example.invalid"), nullptr);
}

TEST_F(ShareCacheTest, favorite_list_changes_keep_share_profiles_consistent) {
	auto fm = FavoriteManager::getInstance();
	FavoriteHubEntry first, second;
	first.setServer("adc://first.example.invalid");
	second.setServer("adc://second.example.invalid");
	second.setShareDirectories({});
	fm->addFavorite(first);
	fm->addFavorite(second);
	auto hubs = fm->getFavoriteHubs();
	ASSERT_EQ(hubs.size(), 2U);

	// Only a reordering of the same favorites is accepted.
	fm->reorderFavorites(FavoriteHubEntryList { hubs[1] });
	EXPECT_EQ(fm->getFavoriteHubs(), hubs);
	fm->reorderFavorites(FavoriteHubEntryList { hubs[1], hubs[0] });
	EXPECT_EQ(fm->getFavoriteHubs(), (FavoriteHubEntryList { hubs[1], hubs[0] }));

	// Failovers the hub announces carry its share profile too.
	const StringList failovers { "adcs://failover.example.invalid:1511" };
	EXPECT_TRUE(fm->setFailoverServers("adc://second.example.invalid", failovers));
	EXPECT_FALSE(fm->setFailoverServers("adc://second.example.invalid", failovers));
	EXPECT_FALSE(fm->setFailoverServers("adc://unknown.example.invalid", failovers));
	std::set<string> directories;
	EXPECT_TRUE(fm->getHubShareDirectories("ADCS://Failover.example.invalid:1511/", directories));
	EXPECT_FALSE(fm->getHubShareDirectories("adc://first.example.invalid", directories));
}

TEST_F(ShareCacheTest, prepares_cached_unshared_attachment_with_exact_route_metadata) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto cached = cacheFile(configPath + "cached attachment.png", "cached temporary attachment");
	optional<ShareManager::ChatAttachmentResult> result;

	const auto requestId = sm->prepareChatAttachment(cached.path, route,
		[&result](ShareManager::ChatAttachmentResult value) { result = std::move(value); });

	ASSERT_TRUE(result);
	EXPECT_EQ(result->requestId, requestId);
	EXPECT_TRUE(result->error.empty());
	ASSERT_TRUE(result->attachment);
	EXPECT_EQ(result->attachment->realPath, cached.path);
	EXPECT_EQ(result->attachment->hubUrl, route);
	EXPECT_EQ(result->attachment->tth, cached.tth);
	EXPECT_EQ(result->attachment->size, cached.size);
	EXPECT_EQ(result->attachment->timestamp, cached.timestamp);
	EXPECT_TRUE(result->attachment->temporary);
	EXPECT_TRUE(sm->validateChatAttachment(cached.tth, cached.size, route));
	EXPECT_FALSE(sm->validateChatAttachment(cached.tth, cached.size, "adc://other.example.invalid"));

	{
		std::ofstream output(cached.path, std::ios::binary | std::ios::trunc);
		output << "changed temporary attachment with a different size";
	}
	EXPECT_FALSE(sm->validateChatAttachment(cached.tth, cached.size, route));
}

TEST_F(ShareCacheTest, protocol_documents_are_exposed_only_by_exact_tth_on_their_route) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://bbs.example.invalid";
	const string otherRoute = "adc://other.example.invalid";
	const auto cached = cacheFile(configPath + "protocol document.bbs", "IBB0 IDdocument body");

	ASSERT_TRUE(sm->addTTHOnlyShare(cached.path, cached.size, cached.timestamp, cached.tth, route));
	EXPECT_TRUE(sm->getTempShares().empty());
	EXPECT_TRUE(sm->search("protocol", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, route).empty());
	EXPECT_TRUE(sm->search(StringList { "ANdocument", "EXbbs" }, 10, route).empty());

	const auto exact = sm->search(StringList { "TR" + cached.tth.toBase32() }, 10, route);
	ASSERT_EQ(exact.size(), 1U);
	EXPECT_EQ(exact.front()->getTTH(), cached.tth);
	EXPECT_TRUE(sm->search(StringList { "TR" + cached.tth.toBase32() }, 10, otherRoute).empty());

	SettingsManager::getInstance()->set(SettingsManager::ENABLE_RTF_TEMP_SHARES, false);
	EXPECT_EQ(sm->toRealWithSize("TTH/" + cached.tth.toBase32(), route),
		std::make_pair(cached.path, cached.size));
	EXPECT_THROW(sm->toRealWithSize("TTH/" + cached.tth.toBase32(), otherRoute), ShareException);
	const auto resolved = sm->resolveFile("TTH/" + cached.tth.toBase32(), route);
	EXPECT_TRUE(resolved.temporary);
	EXPECT_TRUE(resolved.exactOnly);
	EXPECT_EQ(sm->clearTempShares(), 0U);
	EXPECT_EQ(sm->toRealWithSize("TTH/" + cached.tth.toBase32(), route),
		std::make_pair(cached.path, cached.size));

	EXPECT_TRUE(sm->removeTTHOnlyShare(cached.path, cached.tth, route));
	EXPECT_THROW(sm->toRealWithSize("TTH/" + cached.tth.toBase32(), route), ShareException);
}

TEST_F(ShareCacheTest, search_dispatch_reports_global_throttling) {
	auto search = SearchManager::getInstance();
	StringList routes { "adc://bbs.example.invalid" };
	search->lastSearch.store(GET_TICK(), std::memory_order_relaxed);
	EXPECT_FALSE(search->search(routes, string(39, 'A'), 0, SearchManager::TYPE_TTH,
		SearchManager::SIZE_DONTCARE, "bbs-throttled", StringList()));

	search->lastSearch.store(GET_TICK() - 6000, std::memory_order_relaxed);
	EXPECT_TRUE(search->search(routes, string(39, 'A'), 0, SearchManager::TYPE_TTH,
		SearchManager::SIZE_DONTCARE, "bbs-dispatched", StringList()));
}

TEST_F(ShareCacheTest, unanswered_bbs_requests_and_queued_downloads_expire) {
	auto bbs = BBSManager::getInstance();
	const string route = "adc://bbs.example.invalid";
	const string boardName = "general";
	const string tth(39, 'A');
	BBSBoard board;
	board.name = boardName;
	bbs->updateBoard(route, board);
	BBSEntry entry;
	entry.tth = tth;
	entry.size = 3;
	entry.board = boardName;
	entry.timestamp = 1;
	ASSERT_TRUE(bbs->updateEntry(route, entry));

	BBSProbe probe;
	bbs->addListener(&probe);
	string error;
	SearchManager::getInstance()->lastSearch.store(GET_TICK(), std::memory_order_relaxed);
	ASSERT_TRUE(bbs->requestDocument(route, boardName, tth, error)) << error;
	ASSERT_TRUE(bbs->isDocumentPending(route, boardName, tth));
	ASSERT_EQ(bbs->pending[tth].size(), 1U);
	ASSERT_EQ(bbs->pending[tth].front().lastSearch, 0U);
	SearchManager::getInstance()->lastSearch.store(GET_TICK() - 6000, std::memory_order_relaxed);
	bbs->on(TimerManagerListener::Second(), bbs->pending[tth].front().started + 1000);
	ASSERT_NE(bbs->pending[tth].front().lastSearch, 0U);
	const auto searchDeadline = bbs->pending[tth].front().started + BBSManager::DOCUMENT_SEARCH_TIMEOUT_MS;
	probe.updates.clear();
	probe.statuses.clear();
	bbs->on(TimerManagerListener::Second(), searchDeadline);
	EXPECT_FALSE(bbs->isDocumentPending(route, boardName, tth));
	ASSERT_EQ(probe.updates.size(), 1U);
	ASSERT_EQ(probe.statuses.size(), 1U);
	EXPECT_NE(probe.statuses.front().find("No online peer responded"), string::npos);

	const auto now = GET_TICK();
	bbs->pending[tth].push_back({ route, boardName, entry.size, now, now, now });
	probe.updates.clear();
	probe.statuses.clear();
	bbs->on(TimerManagerListener::Second(), now + BBSManager::DOCUMENT_QUEUE_TIMEOUT_MS);
	EXPECT_FALSE(bbs->isDocumentPending(route, boardName, tth));
	ASSERT_EQ(probe.updates.size(), 1U);
	ASSERT_EQ(probe.statuses.size(), 1U);
	EXPECT_NE(probe.statuses.front().find("download timed out"), string::npos);
	bbs->removeListener(&probe);
}

TEST_F(ShareCacheTest, removing_a_bbs_queue_item_clears_pending_state) {
	auto bbs = BBSManager::getInstance();
	const string route = "adc://bbs.example.invalid";
	const string boardName = "general";
	const string tth(39, 'A');
	const auto target = bbs->getCachePath(tth);
	std::filesystem::create_directories(std::filesystem::path(target).parent_path());
	uint8_t cidData[CID::SIZE] = { 1 };
	UserPtr user(new User(CID(cidData)));
	QueueManager::getInstance()->add(target, 3, TTHValue(tth), HintedUser(user, route),
		QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_TEXT);
	const auto now = GET_TICK();
	bbs->pending[tth].push_back({ route, boardName, 3, now, now, now });
	BBSProbe probe;
	bbs->addListener(&probe);
	QueueManager::getInstance()->remove(target);
	bbs->removeListener(&probe);

	EXPECT_FALSE(bbs->isDocumentPending(route, boardName, tth));
	ASSERT_EQ(probe.updates.size(), 1U);
	ASSERT_EQ(probe.statuses.size(), 1U);
	EXPECT_NE(probe.statuses.front().find("ended before it could be verified"), string::npos);
}

TEST_F(ShareCacheTest, failed_bbs_verification_clears_pending_state) {
	auto bbs = BBSManager::getInstance();
	const string route = "adc://bbs.example.invalid";
	const string boardName = "general";
	const string tth(39, 'A');
	const string invalid = "bad";
	BBSBoard board;
	board.name = boardName;
	bbs->updateBoard(route, board);
	BBSEntry entry;
	entry.tth = tth;
	entry.size = static_cast<int64_t>(invalid.size());
	entry.board = boardName;
	entry.timestamp = 1;
	ASSERT_TRUE(bbs->updateEntry(route, entry));

	const auto path = bbs->getCachePath(tth);
	std::filesystem::create_directories(std::filesystem::path(path).parent_path());
	std::ofstream(path, std::ios::binary) << invalid;
	const auto now = GET_TICK();
	bbs->pending[tth].push_back({ route, boardName, entry.size, now, now, now });
	BBSProbe probe;
	bbs->addListener(&probe);
	bbs->completeDocument(tth, path);
	bbs->removeListener(&probe);

	EXPECT_FALSE(bbs->isDocumentPending(route, boardName, tth));
	EXPECT_EQ(File::getSize(path), -1);
	ASSERT_EQ(probe.updates.size(), 1U);
	ASSERT_EQ(probe.statuses.size(), 1U);
	EXPECT_NE(probe.statuses.front().find("does not match its TTH"), string::npos);
}

TEST_F(ShareCacheTest, opened_file_verification_ignores_stale_same_metadata_cache_entries) {
	const auto cached = cacheFile(configPath + "same metadata attachment.bin", "AAAAAAAAAAAAAAAA");
	const string replacement = "BBBBBBBBBBBBBBBB";
	{
		std::ofstream output(cached.path, std::ios::binary | std::ios::trunc);
		output.write(replacement.data(), static_cast<std::streamsize>(replacement.size()));
	}

	TigerTree replacementTree(HashManager::MIN_BLOCK_SIZE);
	replacementTree.update(replacement.data(), replacement.size());
	replacementTree.finalize();

	File file(cached.path, File::READ, File::OPEN);
	file.setPos(3);
	EXPECT_FALSE(HashManager::getInstance()->verifyFileTTH(file, cached.size, cached.tth));
	EXPECT_EQ(file.getPos(), 3);
	EXPECT_TRUE(HashManager::getInstance()->verifyFileTTH(
		file, static_cast<int64_t>(replacement.size()), replacementTree.getRoot()));
	EXPECT_EQ(file.getPos(), 3);
}

TEST_F(ShareCacheTest, preparing_existing_temp_share_moves_it_to_newest_position) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto first = cacheFile(configPath + "first attachment.bin", "first attachment");
	const auto second = cacheFile(configPath + "second attachment.bin", "second attachment");
	int callbackCount = 0;
	auto prepare = [&](const CachedFile& file) {
		sm->prepareChatAttachment(file.path, route, [&](ShareManager::ChatAttachmentResult result) {
			++callbackCount;
			EXPECT_TRUE(result.error.empty());
			ASSERT_TRUE(result.attachment);
			EXPECT_TRUE(result.attachment->temporary);
		});
	};

	prepare(first);
	prepare(second);
	ASSERT_EQ(callbackCount, 2);
	auto shares = sm->getTempShares();
	ASSERT_EQ(shares.size(), 2U);
	EXPECT_EQ(shares[0].tth, first.tth);
	EXPECT_EQ(shares[1].tth, second.tth);

	prepare(first);
	ASSERT_EQ(callbackCount, 3);
	shares = sm->getTempShares();
	ASSERT_EQ(shares.size(), 2U);
	EXPECT_EQ(shares[0].tth, second.tth);
	EXPECT_EQ(shares[1].tth, first.tth);
}

TEST_F(ShareCacheTest, uncached_chat_attachment_completes_on_hash_worker) {
	const auto path = configPath + "dropped attachment.bin";
	{
		std::ofstream output(path, std::ios::binary);
		output << "newly dropped attachment";
	}
	HashManager::getInstance()->startup([](float) {});
	auto promise = std::make_shared<std::promise<ShareManager::ChatAttachmentResult>>();
	auto result = promise->get_future();
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto request = sm->prepareChatAttachment(
		path, route, [promise](ShareManager::ChatAttachmentResult value) { promise->set_value(std::move(value)); });
	const auto ready = result.wait_for(std::chrono::seconds(10));
	if(ready != std::future_status::ready)
		sm->cancelChatAttachment(request);
	ASSERT_EQ(ready, std::future_status::ready);
	const auto completed = result.get();
	ASSERT_TRUE(completed.attachment) << completed.error;
	EXPECT_EQ(completed.requestId, request);
	EXPECT_TRUE(sm->validateChatAttachment(completed.attachment->tth, completed.attachment->size, route));
}

TEST_F(ShareCacheTest, temporary_share_expiration_removes_only_chat_shares_at_deadline) {
	auto sm = ShareManager::getInstance();
	SettingsManager::getInstance()->set(SettingsManager::RTF_TEMP_SHARE_EXPIRATION, 2);
	const string route = "adc://attachments.example.invalid";
	const auto file = cacheFile(configPath + "expiring attachment.bin", "expiring attachment");
	ASSERT_TRUE(sm->addTempShare(file.path, file.size, file.timestamp, file.tth, route));
	ASSERT_TRUE(sm->addTTHOnlyShare(file.path, file.size, file.timestamp, file.tth, route));
	const auto added = sm->tempShares.front().added;
	sm->on(TimerManagerListener::Minute(), added + 119999);
	EXPECT_EQ(sm->tempShares.size(), 2U);
	sm->on(TimerManagerListener::Minute(), added + 120000);
	ASSERT_EQ(sm->tempShares.size(), 1U);
	EXPECT_TRUE(sm->tempShares.front().exactOnly);
	EXPECT_TRUE(sm->getTempShares().empty());
	EXPECT_FALSE(sm->validateChatAttachment(file.tth, file.size, route));
	EXPECT_EQ(File::getSize(file.path), file.size);
}

TEST_F(ShareCacheTest, expiration_is_optional_and_readding_renews_the_lifetime) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto file = cacheFile(configPath + "renewed attachment.bin", "renewed attachment");
	ASSERT_TRUE(sm->addTempShare(file.path, file.size, file.timestamp, file.tth, route));
	sm->tempShares.front().added = 1;
	EXPECT_FALSE(sm->tempShareExpired(sm->tempShares.front(), 120001));
	SettingsManager::getInstance()->set(SettingsManager::RTF_TEMP_SHARE_EXPIRATION, 2);
	EXPECT_TRUE(sm->tempShareExpired(sm->tempShares.front(), 120001));
	ASSERT_TRUE(sm->addTempShare(file.path, file.size, file.timestamp, file.tth, route));
	ASSERT_EQ(sm->tempShares.size(), 1U);
	const auto added = sm->tempShares.front().added;
	EXPECT_FALSE(sm->tempShareExpired(sm->tempShares.front(), added + 119999));
	EXPECT_TRUE(sm->tempShareExpired(sm->tempShares.front(), added + 120000));
	SettingsManager::getInstance()->set(SettingsManager::RTF_TEMP_SHARE_EXPIRATION, 0);
	sm->on(TimerManagerListener::Minute(), added + 120000);
	EXPECT_EQ(sm->getTempShares().size(), 1U);
}

TEST_F(ShareCacheTest, preparing_permanently_visible_file_does_not_create_temp_share) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto cached = cacheFile(sharePath + "visible attachment.jpg", "permanently shared attachment");

	auto root = ShareManager::Directory::create("Visible");
	root->files.insert(ShareManager::Directory::File("visible attachment.jpg", cached.size, root, cached.tth));
	sm->shares[sharePath] = "Visible";
	sm->directories["Visible"] = root;
	sm->rebuildIndices(1);

	optional<ShareManager::ChatAttachmentResult> result;
	sm->prepareChatAttachment(cached.path, route,
		[&result](ShareManager::ChatAttachmentResult value) { result = std::move(value); });

	ASSERT_TRUE(result);
	EXPECT_TRUE(result->error.empty());
	ASSERT_TRUE(result->attachment);
	EXPECT_FALSE(result->attachment->temporary);
	EXPECT_EQ(result->attachment->realPath, cached.path);
	EXPECT_EQ(result->attachment->tth, cached.tth);
	EXPECT_EQ(result->attachment->size, cached.size);
	EXPECT_TRUE(sm->getTempShares().empty());
	EXPECT_TRUE(sm->validateChatAttachment(cached.tth, cached.size, route));
}

TEST_F(ShareCacheTest, cancelling_pending_chat_attachment_removes_request_without_callback) {
	auto sm = ShareManager::getInstance();
	const auto path = configPath + "uncached attachment.bin";
	{
		std::ofstream output(path, std::ios::binary);
		output << "not present in the hash store";
	}
	bool callbackCalled = false;
	const auto requestId = sm->prepareChatAttachment(path, "adc://attachments.example.invalid",
		[&callbackCalled](ShareManager::ChatAttachmentResult) { callbackCalled = true; });

	EXPECT_FALSE(callbackCalled);
	{
		Lock l(sm->chatAttachmentCs);
		auto pending = sm->pendingChatAttachments.find(requestId);
		ASSERT_NE(pending, sm->pendingChatAttachments.end());
		EXPECT_NE(pending->second.hashJobId, 0U);
	}

	sm->cancelChatAttachment(requestId);
	{
		Lock l(sm->chatAttachmentCs);
		EXPECT_EQ(sm->pendingChatAttachments.find(requestId), sm->pendingChatAttachments.end());
	}
	EXPECT_FALSE(callbackCalled);
	EXPECT_TRUE(sm->getTempShares().empty());
}

TEST_F(ShareCacheTest, terminal_hash_event_only_completes_its_own_job_cohort) {
	auto sm = ShareManager::getInstance();
	const string path = configPath + "same snapshot retry.bin";
	const string route = "adc://attachments.example.invalid";
	constexpr int64_t size = 123;
	constexpr uint32_t timestamp = 456;
	constexpr uint64_t oldJob = 7001;
	constexpr uint64_t retryJob = 7002;
	optional<ShareManager::ChatAttachmentResult> oldResult;
	optional<ShareManager::ChatAttachmentResult> retryResult;
	uint64_t generation;
	{
		Lock l(sm->chatAttachmentCs);
		generation = sm->chatAttachmentGeneration;
		const auto oldId = sm->nextChatAttachmentRequest.fetch_add(1);
		const auto retryId = sm->nextChatAttachmentRequest.fetch_add(1);
		sm->pendingChatAttachments.emplace(oldId, ShareManager::PendingChatAttachment {
			oldId, path, route, size, timestamp, generation, oldJob,
			[&oldResult](ShareManager::ChatAttachmentResult value) { oldResult = std::move(value); } });
		sm->pendingChatAttachments.emplace(retryId, ShareManager::PendingChatAttachment {
			retryId, path, route, size, timestamp, generation, retryJob,
			[&retryResult](ShareManager::ChatAttachmentResult value) { retryResult = std::move(value); } });
	}

	sm->failChatAttachments(path, size, timestamp, oldJob, "old job failed");
	ASSERT_TRUE(oldResult);
	EXPECT_FALSE(oldResult->attachment);
	EXPECT_EQ(oldResult->error, "old job failed");
	EXPECT_FALSE(retryResult);
	{
		Lock l(sm->chatAttachmentCs);
		ASSERT_EQ(sm->pendingChatAttachments.size(), 1U);
		EXPECT_EQ(sm->pendingChatAttachments.begin()->second.hashJobId, retryJob);
	}

	sm->failChatAttachments(path, size, timestamp, retryJob, "retry failed");
	ASSERT_TRUE(retryResult);
	EXPECT_EQ(retryResult->error, "retry failed");
	{
		Lock l(sm->chatAttachmentCs);
		EXPECT_TRUE(sm->pendingChatAttachments.empty());
		EXPECT_TRUE(sm->activeChatAttachments.empty());
	}
}

TEST_F(ShareCacheTest, cancelling_extracted_chat_attachment_suppresses_callback_and_cleans_state) {
	auto sm = ShareManager::getInstance();
	const auto cached = cacheFile(configPath + "extracted cancellation.bin", "cancel extracted attachment");
	bool callbackCalled = false;
	uint64_t generation;
	{
		Lock l(sm->chatAttachmentCs);
		generation = sm->chatAttachmentGeneration;
	}
	const auto requestId = sm->nextChatAttachmentRequest.fetch_add(1);
	ShareManager::PendingChatAttachment request { requestId, cached.path,
		"adc://attachments.example.invalid", cached.size, cached.timestamp, generation, 0,
		[&callbackCalled](ShareManager::ChatAttachmentResult) { callbackCalled = true; } };
	{
		Lock l(sm->chatAttachmentCs);
		sm->activeChatAttachments.insert(requestId);
	}

	sm->cancelChatAttachment(requestId);
	sm->finishChatAttachment(std::move(request), cached.tth);

	EXPECT_FALSE(callbackCalled);
	EXPECT_TRUE(sm->getTempShares().empty());
	{
		Lock l(sm->chatAttachmentCs);
		EXPECT_EQ(sm->activeChatAttachments.count(requestId), 0U);
		EXPECT_EQ(sm->cancelledChatAttachments.count(requestId), 0U);
	}
}

TEST_F(ShareCacheTest, clearing_temp_shares_completes_pending_attachment_with_cancellation) {
	auto sm = ShareManager::getInstance();
	const auto path = configPath + "pending clear attachment.bin";
	{
		std::ofstream output(path, std::ios::binary);
		output << "uncached attachment cancelled by clear";
	}
	optional<ShareManager::ChatAttachmentResult> result;
	const auto requestId = sm->prepareChatAttachment(path, "adc://attachments.example.invalid",
		[&result](ShareManager::ChatAttachmentResult value) { result = std::move(value); });
	EXPECT_FALSE(result);

	EXPECT_EQ(sm->clearTempShares(), 0U);
	ASSERT_TRUE(result);
	EXPECT_EQ(result->requestId, requestId);
	EXPECT_FALSE(result->attachment);
	EXPECT_FALSE(result->error.empty());
	{
		Lock l(sm->chatAttachmentCs);
		EXPECT_EQ(sm->pendingChatAttachments.find(requestId), sm->pendingChatAttachments.end());
		EXPECT_EQ(sm->activeChatAttachments.count(requestId), 0U);
	}
	EXPECT_TRUE(sm->getTempShares().empty());
}

TEST_F(ShareCacheTest, stale_attachment_completion_reports_clear_and_cannot_readd) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto cached = cacheFile(configPath + "clear race attachment.bin", "clear race attachment");
	uint64_t staleGeneration;
	{
		Lock l(sm->chatAttachmentCs);
		staleGeneration = sm->chatAttachmentGeneration;
	}

	EXPECT_EQ(sm->clearTempShares(), 0U);
	optional<ShareManager::ChatAttachmentResult> staleResult;
	const auto staleRequestId = sm->nextChatAttachmentRequest.fetch_add(1);
	ShareManager::PendingChatAttachment staleRequest { staleRequestId, cached.path, route,
		cached.size, cached.timestamp, staleGeneration, 0,
		[&staleResult](ShareManager::ChatAttachmentResult value) { staleResult = std::move(value); } };
	{
		Lock l(sm->chatAttachmentCs);
		sm->activeChatAttachments.insert(staleRequestId);
	}
	sm->finishChatAttachment(std::move(staleRequest), cached.tth);

	ASSERT_TRUE(staleResult);
	EXPECT_FALSE(staleResult->attachment);
	EXPECT_FALSE(staleResult->error.empty());
	EXPECT_TRUE(sm->getTempShares().empty());
	optional<ShareManager::ChatAttachmentResult> freshResult;
	sm->prepareChatAttachment(cached.path, route,
		[&freshResult](ShareManager::ChatAttachmentResult value) { freshResult = std::move(value); });
	ASSERT_TRUE(freshResult);
	ASSERT_TRUE(freshResult->attachment);
	EXPECT_TRUE(freshResult->attachment->temporary);
	ASSERT_EQ(sm->getTempShares().size(), 1U);
}

TEST_F(ShareCacheTest, streams_full_xml_with_correct_uncompressed_size) {
	populateShare();
	auto sm = ShareManager::getInstance();
	++sm->listForce;
	auto opened = sm->openXmlList("adc://stream.example");
	string xml;
	char buffer[37];
	for(;;) {
		size_t n = sizeof(buffer);
		const auto produced = opened.first->read(buffer, n);
		EXPECT_EQ(produced, n);
		if(!produced) break;
		xml.append(buffer, produced);
	}
	EXPECT_EQ(static_cast<int64_t>(xml.size()), opened.second);
	EXPECT_NE(string::npos, xml.find("file.bin"));
	EXPECT_NE(string::npos, xml.find("</FileListing>"));
}

TEST_F(ShareCacheTest, streamed_xml_respects_an_explicit_empty_hub_profile) {
	populateShare();
	FavoriteHubEntry hub;
	hub.setServer("adc://empty-stream.example");
	hub.setShareDirectories({});
	FavoriteManager::getInstance()->addFavorite(hub);
	auto opened = ShareManager::getInstance()->openXmlList(hub.getServer());
	string xml;
	char buffer[101];
	for(;;) {
		size_t n = sizeof(buffer);
		const auto produced = opened.first->read(buffer, n);
		EXPECT_EQ(produced, n);
		if(!produced) break;
		xml.append(buffer, produced);
	}
	EXPECT_EQ(static_cast<int64_t>(xml.size()), opened.second);
	EXPECT_EQ(string::npos, xml.find("file.bin"));
	EXPECT_EQ(string::npos, xml.find("Virtual"));
	EXPECT_NE(string::npos, xml.find("</FileListing>"));
}

#ifdef _WIN32
TEST_F(ShareCacheTest, private_storage_cannot_be_added_as_public_or_temporary_share) {
	auto sm = ShareManager::getInstance();
	const auto root = pft::storageRoot();
	std::filesystem::create_directories(root);
	const auto file = cacheFile(root + "private.txt", "private");
	SettingsManager::getInstance()->set(SettingsManager::ENABLE_RTF_TEMP_SHARES, true);
	EXPECT_THROW(sm->addDirectory(root, "Private"), ShareException);
	EXPECT_TRUE(pft::PrivatePathScanner().isPrivateRoot(root));
	EXPECT_FALSE(sm->addValidatedTempShare(file.path, file.size, file.timestamp, file.tth, "adcs://test.invalid", false));
	EXPECT_FALSE(sm->addValidatedTempShare(file.path, file.size, file.timestamp, file.tth, "adcs://test.invalid", true));
	EXPECT_TRUE(sm->getTempShares().empty());
}

TEST_F(ShareCacheTest, private_markers_block_stale_resolution_refresh_and_cache_restoration) {
	populateShare();
	auto sm = ShareManager::getInstance();
	const auto path = sharePath + "Child" PATH_SEPARATOR_STR "file.bin";
	File(path + ":DCPlusPlus.PrivateSend", File::WRITE, File::CREATE | File::TRUNCATE).write("PFT1");
	// Never served, by TTH or by path, although the shared tree still lists it.
	EXPECT_THROW(sm->toRealWithSize("TTH/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), ShareException);
	EXPECT_THROW(sm->resolveFile("TTH/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "adcs://test.invalid"), ShareException);
	EXPECT_THROW(sm->resolveFile("/Virtual/Child/file.bin", "adcs://test.invalid"), ShareException);
	// A scan doesn't open every file; the marked file gets no hash (it isn't in the hash store,
	// and the hasher refuses it), so it isn't listed, searchable or indexed.
	const auto refreshed = sm->buildTree(sharePath);
	ASSERT_EQ(1u, refreshed->directories.size());
	for(const auto& file: refreshed->directories.begin()->second->files) {
		EXPECT_FALSE(file.tth) << file.getName();
	}
	// A cache written before the marker appeared still can't serve it.
	sm->saveShareCache();
	clearLoadedShare();
	ASSERT_TRUE(sm->loadShareCache());
	EXPECT_THROW(sm->resolveFile("TTH/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "adcs://test.invalid"), ShareException);
	EXPECT_THROW(sm->toRealWithSize("TTH/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), ShareException);
}

TEST_F(ShareCacheTest, scan_excludes_file_symlinks_into_private_storage) {
	// The scan checks regular files per directory; a file symlink must still be resolved to its
	// target, so it may not expose private storage.
	auto sm = ShareManager::getInstance();
	const auto root = pft::storageRoot();
	std::filesystem::create_directories(root + "Received");
	std::ofstream(root + "Received" PATH_SEPARATOR_STR "private.txt") << "private";
	std::ofstream(sharePath + "public.txt") << "public";

	const auto link = sharePath + "link.txt";
	if(!CreateSymbolicLinkW(Text::toT(link).c_str(), Text::toT(root + "Received" PATH_SEPARATOR_STR "private.txt").c_str(),
		SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		GTEST_SKIP() << "Creating a symlink needs Developer Mode or elevation";

	FileFindIter it(link);
	ASSERT_NE(it, FileFindIter());
	EXPECT_TRUE(it->isReparsePoint());

	const auto tree = sm->buildTree(sharePath);
	EXPECT_NE(tree->findFile("public.txt"), tree->files.end());
	EXPECT_EQ(tree->findFile("link.txt"), tree->files.end());
	std::filesystem::remove(link);
}
#endif

namespace {

bool waitForShareScans() {
	auto sm = ShareManager::getInstance();
	for(int i = 0; i < 1000; ++i) {
		if(!sm->hasPendingDirectories() && !sm->isRefreshing()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return false;
}

}

TEST_F(ShareCacheTest, added_folders_are_scanned_in_the_background) {
	auto sm = ShareManager::getInstance();
	const auto file = cacheFile(sharePath + "added.bin", "contents of an added share folder");
	{
		// The scan needs the hash store; holding it keeps the scan from finishing.
		Lock blockScan(HashManager::getInstance()->cs);
		sm->addDirectory(sharePath, "Added");
		// The folder is registered at once, before its contents are known.
		EXPECT_EQ(1u, sm->getDirectories().size());
		EXPECT_TRUE(sm->isDirectoryPending(sharePath));
		EXPECT_EQ(0, sm->getShareSize());
	}
	ASSERT_TRUE(waitForShareScans());
	EXPECT_FALSE(sm->isDirectoryPending(sharePath));
	EXPECT_EQ(file.size, sm->getShareSize(sharePath));
	EXPECT_EQ(file.size, sm->getShareSize());
}

TEST_F(ShareCacheTest, removing_a_merged_folder_rescans_the_others_in_the_background) {
	auto sm = ShareManager::getInstance();
	const auto first = cacheFile(sharePath + "first.bin", "first merged folder");
	const auto second = cacheFile(secondSharePath + "second.bin", "second merged folder contents");
	sm->addDirectory(sharePath, "Merged");
	sm->addDirectory(secondSharePath, "Merged");
	ASSERT_TRUE(waitForShareScans());
	EXPECT_EQ(first.size + second.size, sm->getShareSize());

	sm->removeDirectory(sharePath);
	ASSERT_TRUE(waitForShareScans());
	EXPECT_EQ(1u, sm->getDirectories().size());
	EXPECT_EQ(second.size, sm->getShareSize());
	EXPECT_EQ(second.size, sm->getShareSize(secondSharePath));
}

TEST_F(ShareCacheTest, removing_folders_while_scans_finish_does_not_deadlock) {
	// removeDirectory used to start the rescan while holding the share lock; starting a scan waits
	// for the previous scan thread, which takes that lock as it finishes.
	auto sm = ShareManager::getInstance();
	cacheFile(sharePath + "first.bin", "first merged folder");
	const auto second = cacheFile(secondSharePath + "second.bin", "second merged folder contents");
	sm->addDirectory(secondSharePath, "Merged");
	// Remove the moment a scan reports it has finished, while its thread is still saving the
	// share cache and checking for more work.
	for(int i = 0; i < 20; ++i) {
		sm->addDirectory(sharePath, "Merged");
		while(sm->isRefreshing()) { }
		sm->removeDirectory(sharePath);
		while(sm->isRefreshing()) { }
	}
	ASSERT_TRUE(waitForShareScans());
	EXPECT_EQ(1u, sm->getDirectories().size());
	EXPECT_EQ(second.size, sm->getShareSize());
}

TEST_F(ShareCacheTest, folders_added_during_a_refresh_are_not_lost) {
	auto sm = ShareManager::getInstance();
	const auto first = cacheFile(sharePath + "first.bin", "already shared");
	const auto second = cacheFile(secondSharePath + "second.bin", "added while refreshing");
	sm->addDirectory(sharePath, "First");
	ASSERT_TRUE(waitForShareScans());
	{
		Lock blockScan(HashManager::getInstance()->cs);
		sm->refresh(true, false, false);
		// The full refresh has its snapshot of shared folders; this one isn't in it.
		sm->addDirectory(secondSharePath, "Second");
	}
	ASSERT_TRUE(waitForShareScans());
	EXPECT_EQ(2u, sm->getDirectories().size());
	EXPECT_EQ(first.size + second.size, sm->getShareSize());
}

TEST_F(ShareCacheTest, hash_finished_during_a_scan_reaches_the_scanned_tree) {
	// Scans no longer pause hashing. A file the scan reads before its hash is done can finish
	// hashing before the scanned tree replaces the live one; that hash must not be lost.
	auto sm = ShareManager::getInstance();
	const string contents = "hashed while the scan ran";
	const auto path = sharePath + "late.bin";
	std::ofstream(path, std::ios::binary) << contents;
	TigerTree tree(HashManager::MIN_BLOCK_SIZE);
	tree.update(contents.data(), contents.size());
	tree.finalize();
	File file(path, File::READ, File::OPEN | File::SHARED);
	const auto size = file.getSize();
	const auto timestamp = static_cast<uint32_t>(file.getLastModified());
	file.close();

	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
		sm->recordScanHashes = true;
	}
	auto scanned = sm->buildTree(sharePath);
	scanned->setName("Virtual");
	ASSERT_NE(scanned->findFile("late.bin"), scanned->files.end());
	EXPECT_FALSE(scanned->findFile("late.bin")->tth);

	// The hash finishes while the old (empty) tree is still live.
	sm->on(HashManagerListener::TTHDone(), path, tree.getRoot(), size, timestamp, 0);
	EXPECT_FALSE(sm->isTTHShared(tree.getRoot()));

	{
		WLock l(sm->cs);
		sm->merge(scanned, sharePath);
		sm->rebuildIndices();
		sm->replayScanHashes();
	}
	EXPECT_TRUE(sm->isTTHShared(tree.getRoot()));
	EXPECT_EQ(size, sm->getShareSize());
}

TEST_F(ShareCacheTest, scan_hash_replay_skips_a_file_changed_after_hashing) {
	auto sm = ShareManager::getInstance();
	const auto path = sharePath + "changed.bin";
	std::ofstream(path, std::ios::binary) << "new contents";
	const TTHValue oldRoot("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");

	auto scanned = sm->buildTree(sharePath);
	scanned->setName("Virtual");
	const auto lastWrite = scanned->findFile("changed.bin")->getLastWrite();
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
		sm->merge(scanned, sharePath);
		// A hash of an earlier version of the file, with an older timestamp.
		sm->scanHashes.push_back({ path, oldRoot, 12, static_cast<uint32_t>(lastWrite - 60) });
		sm->rebuildIndices();
		sm->replayScanHashes();
	}
	EXPECT_FALSE(sm->isTTHShared(oldRoot));
}

TEST_F(ShareCacheTest, refresh_does_not_pause_hashing) {
	auto sm = ShareManager::getInstance();
	std::ofstream(sharePath + "file.bin") << "data";
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
	}
	std::atomic<bool> pausedDuringScan { false };
	std::atomic<bool> scanning { true };
	std::thread probe([&] {
		while(scanning) {
			if(HashManager::getInstance()->isHashingPaused()) pausedDuringScan = true;
			std::this_thread::yield();
		}
	});
	sm->refresh(true, false, true);
	scanning = false;
	probe.join();
	EXPECT_FALSE(pausedDuringScan);
	EXPECT_FALSE(HashManager::getInstance()->isHashingPaused());
	WLock l(sm->cs);
	EXPECT_FALSE(sm->recordScanHashes);
	EXPECT_TRUE(sm->scanHashes.empty());
}

namespace {

struct HashProbe : public HashManagerListener {
	struct Event {
		bool done;
		string path;
		int64_t size;
		uint32_t timestamp;
		uint64_t jobId;
		HashManagerListener::Failure reason;
	};
	std::mutex mutex;
	std::condition_variable changed;
	vector<Event> events;

	void on(TTHDone, const string& path, const TTHValue&, int64_t size, uint32_t timestamp, uint64_t jobId) noexcept override {
		std::lock_guard<std::mutex> l(mutex);
		events.push_back({ true, path, size, timestamp, jobId, HashManagerListener::Failure::FILE_IO });
		changed.notify_all();
	}
	void on(TTHFailed, const string& path, int64_t size, uint32_t timestamp, uint64_t jobId,
		HashManagerListener::Failure reason, const string&) noexcept override
	{
		std::lock_guard<std::mutex> l(mutex);
		events.push_back({ false, path, size, timestamp, jobId, reason });
		changed.notify_all();
	}
	bool waitFor(size_t count) {
		std::unique_lock<std::mutex> l(mutex);
		return changed.wait_for(l, std::chrono::seconds(20), [&] { return events.size() >= count; });
	}
};

struct FileSnapshot {
	int64_t size;
	uint32_t timestamp;
};

FileSnapshot snapshotOf(const string& path) {
	File file(path, File::READ, File::OPEN | File::SHARED);
	return { file.getSize(), file.getLastModified() };
}

TigerTree treeOf(const string& contents) {
	TigerTree tree(HashManager::MIN_BLOCK_SIZE);
	tree.update(contents.data(), contents.size());
	tree.finalize();
	return tree;
}

void writeFile(const string& path, const string& contents) {
	std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
}

void markPrivate(const string& path) {
	File(path + ":DCPlusPlus.PrivateSend", File::WRITE, File::CREATE | File::TRUNCATE).write("PFT1");
}

const ShareManager::Directory::File* findShared(const ShareManager::Directory& dir, const string& name) {
	auto i = dir.findFile(name);
	return i == dir.files.end() ? nullptr : &*i;
}

#ifdef _WIN32
string toAdminShare(const string& localPath) {
	return string("\\\\localhost\\") + localPath[0] + "$" + localPath.substr(2);
}
#endif

}

TEST_F(ShareCacheTest, scan_checks_markers_once_for_files_hashed_before_marker_checks) {
	auto sm = ShareManager::getInstance();
	auto hm = HashManager::getInstance();
	const auto plain = sharePath + "old.bin";
	const auto marked = sharePath + "old-private.bin";
	writeFile(plain, "hashed by an older version");
	writeFile(marked, "privately received, hashed by an older version");
	markPrivate(marked);
	for(const auto& path: { plain, marked }) {
		const auto snapshot = snapshotOf(path);
		Lock l(hm->cs);
		hm->store.addFile(path, snapshot.timestamp, treeOf(path == plain ? "hashed by an older version" :
			"privately received, hashed by an older version"), false, false);
	}

	const auto tree = sm->buildTree(sharePath);
	ASSERT_TRUE(findShared(*tree, "old.bin"));
	EXPECT_TRUE(findShared(*tree, "old.bin")->tth);
	EXPECT_FALSE(findShared(*tree, "old-private.bin"));

	bool checked = false;
	const auto plainSnapshot = snapshotOf(plain);
	const auto markedSnapshot = snapshotOf(marked);
	Lock l(hm->cs);
	EXPECT_TRUE(hm->store.getTTH(plain, plainSnapshot.size, plainSnapshot.timestamp, &checked));
	EXPECT_TRUE(checked);
	checked = true;
	EXPECT_TRUE(hm->store.getTTH(marked, markedSnapshot.size, markedSnapshot.timestamp, &checked));
	EXPECT_FALSE(checked);
}

TEST_F(ShareCacheTest, hasher_refuses_privately_received_files_and_hashes_changed_files_as_they_are) {
	auto hm = HashManager::getInstance();
	hm->startup([](float) {});
	HashProbe probe;
	hm->addListener(&probe);

	const auto privateFile = configPath + "received.bin";
	writeFile(privateFile, "private bytes");
	markPrivate(privateFile);
	const auto privateSnapshot = snapshotOf(privateFile);
	EXPECT_FALSE(hm->requestTTH(privateFile, privateSnapshot.size, privateSnapshot.timestamp).tth);
	ASSERT_TRUE(probe.waitFor(1));

	// Requested with a stale time, as a network listing can report it.
	const auto changed = configPath + "changed.bin";
	writeFile(changed, "current content");
	const auto current = snapshotOf(changed);
	const auto request = hm->requestTTH(changed, current.size, current.timestamp - 10);
	ASSERT_TRUE(probe.waitFor(3));
	hm->removeListener(&probe);

	EXPECT_FALSE(probe.events[0].done);
	EXPECT_EQ(HashManagerListener::Failure::PRIVATE_FILE, probe.events[0].reason);
	EXPECT_FALSE(hm->findTTH(privateFile, privateSnapshot.size, privateSnapshot.timestamp));

	// The requested snapshot gets a definite answer, then the file is hashed as it is.
	EXPECT_FALSE(probe.events[1].done);
	EXPECT_EQ(HashManagerListener::Failure::FILE_CHANGED, probe.events[1].reason);
	EXPECT_EQ(request.jobId, probe.events[1].jobId);
	EXPECT_TRUE(probe.events[2].done);
	EXPECT_EQ(current.timestamp, probe.events[2].timestamp);
	EXPECT_EQ(treeOf("current content").getRoot(), *hm->findTTH(changed, current.size, current.timestamp));
}

TEST_F(ShareCacheTest, hash_queue_keeps_totals_and_cancels_one_folder) {
	auto hm = HashManager::getInstance();
	const auto a = configPath + "One" PATH_SEPARATOR_STR "a.bin";
	const auto b = configPath + "One" PATH_SEPARATOR_STR "b.bin";
	const auto c = configPath + "Two" PATH_SEPARATOR_STR "c.bin";
	const auto first = hm->queueHash(a, 10, 1);
	EXPECT_EQ(first, hm->queueHash(a, 10, 1));
	const auto second = hm->queueHash(a, 20, 2);
	EXPECT_NE(first, second);
	EXPECT_EQ(second, hm->queueHash(a, 20, 2));
	hm->queueHash(b, 30, 3);
	hm->queueHash(c, 40, 4);

	string current;
	uint64_t bytes = 0;
	size_t files = 0;
	hm->getStats(current, bytes, files);
	EXPECT_EQ(4u, files);
	EXPECT_EQ(100u, bytes);

	HashProbe probe;
	hm->addListener(&probe);
	hm->stopHashing(configPath + "One" PATH_SEPARATOR_STR);
	hm->removeListener(&probe);
	EXPECT_EQ(3u, probe.events.size());
	hm->getStats(current, bytes, files);
	EXPECT_EQ(1u, files);
	EXPECT_EQ(40u, bytes);
}

TEST_F(ShareCacheTest, hash_volumes_come_from_the_path_alone) {
#ifdef _WIN32
	EXPECT_EQ("c:", HashManager::getVolume("C:\\Media\\a.bin"));
	EXPECT_EQ("c:", HashManager::getVolume("\\\\?\\C:\\Media\\a.bin"));
	EXPECT_EQ("d:", HashManager::getVolume("d:\\a.bin"));
	EXPECT_EQ("\\\\nas\\media", HashManager::getVolume("\\\\NAS\\Media\\Album\\a.flac"));
	EXPECT_EQ("\\\\nas\\media", HashManager::getVolume("\\\\?\\UNC\\nas\\media\\a.flac"));
	EXPECT_EQ("\\\\nas\\media", HashManager::getVolume("\\\\nas\\media"));
	EXPECT_EQ("\\\\nas\\other", HashManager::getVolume("\\\\nas\\other\\a.flac"));
	EXPECT_EQ("", HashManager::getVolume("\\\\nas"));
	EXPECT_EQ("", HashManager::getVolume("\\\\nas\\"));
	EXPECT_EQ("", HashManager::getVolume("relative\\a.bin"));
#endif
}

TEST_F(ShareCacheTest, hash_queue_uses_one_hasher_per_volume_up_to_the_setting) {
	auto hm = HashManager::getInstance();
	SettingsManager::getInstance()->set(SettingsManager::MAX_HASH_VOLUMES, 2);
	// The hasher threads aren't started here, so everything stays queued.
	const string a = "\\\\nas\\a\\", b = "\\\\nas\\b\\", c = "\\\\nas\\c\\";
	const auto a1 = hm->queueHash(a + "1.bin", 1000, 1);
	ASSERT_EQ(1u, hm->hashers.size());
	const auto b1 = hm->queueHash(b + "1.bin", 10, 1);
	// A second volume gets a hasher of its own.
	ASSERT_EQ(2u, hm->hashers.size());
	EXPECT_TRUE(hm->hashers[0]->hasVolume(HashManager::getVolume(a)));
	EXPECT_TRUE(hm->hashers[1]->hasVolume(HashManager::getVolume(b)));
	EXPECT_FALSE(hm->hashers[0]->hasVolume(HashManager::getVolume(b)));

	// Further files of a volume go to its hasher; the same file is still one job.
	const auto a2 = hm->queueHash(a + "2.bin", 1000, 1);
	EXPECT_EQ(a1, hm->queueHash(a + "1.bin", 1000, 1));
	EXPECT_EQ(2u, hm->hashers[0]->queuedFiles);
	EXPECT_EQ(1u, hm->hashers[1]->queuedFiles);

	// At the limit, a third volume shares the least loaded hasher.
	const auto c1 = hm->queueHash(c + "1.bin", 5, 1);
	ASSERT_EQ(2u, hm->hashers.size());
	EXPECT_TRUE(hm->hashers[1]->hasVolume(HashManager::getVolume(c)));
	EXPECT_EQ(2u, hm->hashers[1]->queuedFiles);
	// Job ids are unique across hashers.
	EXPECT_EQ(4u, (std::set<uint64_t> { a1, a2, b1, c1 }).size());

	string current;
	uint64_t bytes = 0;
	size_t files = 0;
	hm->getStats(current, bytes, files);
	EXPECT_EQ(4u, files);
	EXPECT_EQ(2015u, bytes);

	// Cancelling a folder frees its volume.
	HashProbe probe;
	hm->addListener(&probe);
	hm->stopHashing(b);
	hm->removeListener(&probe);
	ASSERT_EQ(1u, probe.events.size());
	EXPECT_EQ(b1, probe.events[0].jobId);
	EXPECT_FALSE(hm->hashers[1]->hasVolume(HashManager::getVolume(b)));
	EXPECT_TRUE(hm->hashers[1]->hasVolume(HashManager::getVolume(c)));
	hm->stopHashing(c);
	EXPECT_TRUE(hm->hashers[1]->isIdle());
	EXPECT_FALSE(hm->hashers[0]->isIdle());

	// An idle hasher takes the next new volume; no hasher is added.
	hm->queueHash(b + "2.bin", 10, 1);
	EXPECT_TRUE(hm->hashers[1]->hasVolume(HashManager::getVolume(b)));
	EXPECT_EQ(2u, hm->hashers.size());
}

TEST_F(ShareCacheTest, one_hasher_reads_every_volume_when_the_setting_is_one) {
	auto hm = HashManager::getInstance();
	SettingsManager::getInstance()->set(SettingsManager::MAX_HASH_VOLUMES, 1);
	hm->queueHash("\\\\nas\\a\\1.bin", 10, 1);
	hm->queueHash("\\\\nas\\b\\1.bin", 10, 1);
	hm->queueHash("C:\\Media\\1.bin", 10, 1);
	EXPECT_EQ(1u, hm->hashers.size());
	EXPECT_EQ(3u, hm->hashers[0]->queuedFiles);
}

TEST_F(ShareCacheTest, pausing_hashing_pauses_every_hasher_including_new_ones) {
	auto hm = HashManager::getInstance();
	SettingsManager::getInstance()->set(SettingsManager::MAX_HASH_VOLUMES, 3);
	hm->queueHash("\\\\nas\\a\\1.bin", 10, 1);
	hm->queueHash("\\\\nas\\b\\1.bin", 10, 1);
	EXPECT_FALSE(hm->pauseHashing());
	EXPECT_TRUE(hm->isHashingPaused());
	// Added while paused: starts paused.
	hm->queueHash("\\\\nas\\c\\1.bin", 10, 1);
	ASSERT_EQ(3u, hm->hashers.size());
	for(const auto& hasher: hm->hashers) {
		EXPECT_TRUE(hasher->isPaused());
	}
	EXPECT_TRUE(hm->pauseHashing());
	hm->resumeHashing();
	for(const auto& hasher: hm->hashers) {
		EXPECT_TRUE(hasher->isPaused());
	}
	hm->resumeHashing();
	EXPECT_FALSE(hm->isHashingPaused());
	for(const auto& hasher: hm->hashers) {
		EXPECT_FALSE(hasher->isPaused());
	}
}

#ifdef _WIN32
TEST_F(ShareCacheTest, two_volumes_are_hashed_by_two_hashers) {
	// The administrative share is another volume path to the same disk.
	const auto unc = toAdminShare(sharePath);
	if(!File::isDirectory(unc)) {
		GTEST_SKIP() << "The administrative share " << unc << " is not reachable";
	}
	auto hm = HashManager::getInstance();
	SettingsManager::getInstance()->set(SettingsManager::MAX_HASH_VOLUMES, 2);
	// Queued while paused, so each hasher still has its volume when the queue is checked.
	hm->pauseHashing();
	hm->startup([](float) {});
	HashProbe probe;
	hm->addListener(&probe);

	vector<pair<string, string>> files;
	for(int i = 0; i < 8; ++i) {
		const auto name = "volume file " + std::to_string(i) + ".bin";
		const auto contents = "contents " + std::to_string(i) + string(100000 + i * 1000, static_cast<char>('a' + i));
		writeFile(sharePath + name, contents);
		files.emplace_back((i % 2 ? unc : sharePath) + name, contents);
	}
	for(const auto& file: files) {
		const auto snapshot = snapshotOf(file.first);
		EXPECT_FALSE(hm->requestTTH(file.first, snapshot.size, snapshot.timestamp).tth);
	}
	ASSERT_EQ(2u, hm->hashers.size());
	EXPECT_TRUE(hm->hashers[0]->hasVolume(HashManager::getVolume(sharePath)));
	EXPECT_TRUE(hm->hashers[1]->hasVolume(HashManager::getVolume(unc)));

	hm->resumeHashing();
	ASSERT_TRUE(probe.waitFor(files.size()));
	hm->removeListener(&probe);
	for(const auto& event: probe.events) {
		EXPECT_TRUE(event.done) << event.path;
	}
	for(const auto& file: files) {
		const auto snapshot = snapshotOf(file.first);
		const auto tth = hm->findTTH(file.first, snapshot.size, snapshot.timestamp);
		ASSERT_TRUE(tth) << file.first;
		EXPECT_EQ(treeOf(file.second).getRoot(), *tth) << file.first;
	}
	for(int i = 0; i < 100 && !(hm->hashers[0]->isIdle() && hm->hashers[1]->isIdle()); ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	EXPECT_TRUE(hm->hashers[0]->isIdle());
	EXPECT_TRUE(hm->hashers[1]->isIdle());
}
#endif

TEST_F(ShareCacheTest, unavailable_share_root_keeps_its_previous_contents) {
	auto sm = ShareManager::getInstance();
	const auto root = configPath + "Removable" PATH_SEPARATOR_STR;
	std::filesystem::create_directories(root + "Album");
	writeFile(root + "Album" PATH_SEPARATOR_STR "track.flac", "audio");
	{
		WLock l(sm->cs);
		sm->shares[root] = "Removable";
	}
	sm->refresh(true, false, true);
	auto shared = [&] {
		WLock l(sm->cs);
		auto dir = sm->directories.find("Removable");
		if(dir == sm->directories.end()) return false;
		auto album = dir->second->directories.find("Album");
		return album != dir->second->directories.end() && findShared(*album->second, "track.flac") != nullptr;
	};
	ASSERT_TRUE(shared());

	// The drive or server goes away: a refresh must not empty the share.
	std::filesystem::rename(root, configPath + "Unplugged");
	sm->refresh(true, false, true);
	EXPECT_TRUE(shared());

	// Back again, and changed: the next refresh picks it up.
	std::filesystem::rename(configPath + "Unplugged", root);
	std::filesystem::remove(root + "Album" PATH_SEPARATOR_STR "track.flac");
	sm->refresh(true, false, true);
	EXPECT_FALSE(shared());
}

TEST_F(ShareCacheTest, automatic_refresh_waits_at_least_as_long_as_the_last_refresh_took) {
	auto sm = ShareManager::getInstance();
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
	}
	sm->refresh(true, false, true);
	const uint64_t finished = sm->lastFullUpdate;
	const uint64_t interval = static_cast<uint64_t>(SETTING(AUTO_REFRESH_TIME)) * 60 * 1000;
	ASSERT_GT(interval, 0u);

	// A scan that took two intervals pushes the next one back by two intervals.
	sm->lastRefreshDuration = 2 * interval;
	sm->on(TimerManagerListener::Minute(), finished + interval + 1);
	EXPECT_FALSE(sm->isRefreshing());
	EXPECT_EQ(finished, sm->lastFullUpdate.load());

	sm->on(TimerManagerListener::Minute(), finished + 2 * interval);
	for(int i = 0; i < 1000 && (sm->isRefreshing() || sm->lastFullUpdate == finished); ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	EXPECT_NE(finished, sm->lastFullUpdate.load());
	sm->join();
}

TEST_F(ShareCacheTest, stopping_abandons_a_scan_and_keeps_the_share) {
	auto sm = ShareManager::getInstance();
	for(int i = 0; i < 50; ++i)
		writeFile(sharePath + "file" + std::to_string(i) + ".bin", "x");
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
	}
	sm->refresh(true, false, true);
	{
		WLock l(sm->cs);
		ASSERT_EQ(50u, sm->directories["Virtual"]->files.size());
	}
	sm->stopping = true;
	pft::PrivatePathScanner privacy;
	ShareManager::Directory::Ptr tree;
	EXPECT_FALSE(sm->scanRoot(privacy, sharePath, "Virtual", tree));
	sm->refresh(true, false, true);
	sm->stopping = false;
	WLock l(sm->cs);
	EXPECT_EQ(50u, sm->directories["Virtual"]->files.size());
}

TEST_F(ShareCacheTest, finished_download_replaces_the_shared_entry_and_is_indexed) {
	auto sm = ShareManager::getInstance();
	SettingsManager::getInstance()->set(SettingsManager::ADD_FINISHED_INSTANTLY, true);
	const auto path = sharePath + "download.bin";
	writeFile(path, "first version");
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
	}
	const auto oldFile = cacheFile(path, "first version");
	sm->refresh(true, false, true);
	ASSERT_TRUE(sm->isTTHShared(oldFile.tth));

	// Downloaded again over the shared file.
	const auto newFile = cacheFile(path, "second, longer version");
	sm->on(QueueManagerListener::FileMoved(), path);

	EXPECT_TRUE(sm->isTTHShared(newFile.tth));
	EXPECT_FALSE(sm->isTTHShared(oldFile.tth));
	WLock l(sm->cs);
	const auto& files = sm->directories["Virtual"]->files;
	ASSERT_EQ(1u, files.size());
	EXPECT_EQ(newFile.size, files.begin()->getSize());
	EXPECT_EQ(newFile.tth, *files.begin()->tth);
	EXPECT_EQ(files.begin()->getSize(), sm->getShareSize());
}

TEST_F(ShareCacheTest, uploads_during_a_refresh_use_the_stored_hash_instead_of_rehashing) {
	auto sm = ShareManager::getInstance();
	auto um = UploadManager::getInstance();
	auto hm = HashManager::getInstance();
	const auto path = sharePath + "upload.bin";
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
	}
	const auto file = cacheFile(path, "requested while the share refreshes");
	sm->refresh(true, false, true);
	ASSERT_TRUE(sm->isTTHShared(file.tth));
	{
		Lock l(hm->cs);
		hm->verificationCache.clear();
	}

	ShareManager::refreshActive = true;
	UserConnection connection(false);
	connection.setHubUrl("adc://hub.example.invalid");
	connection.setUser(ClientManager::getInstance()->getUser(CID::generate()));
	const bool prepared = um->prepareFile(connection, Transfer::names[Transfer::TYPE_FILE], "TTH/" + file.tth.toBase32(), 0, -1);
	ShareManager::refreshActive = false;
	ASSERT_TRUE(prepared);
	{
		Lock l(um->cs);
		EXPECT_EQ(path, um->uploads.back()->getPath());
		um->removeUpload(um->uploads.back());
	}
	// Served on the stored record's word; the file wasn't read again.
	Lock l(hm->cs);
	EXPECT_TRUE(hm->verificationCache.empty());
}

#ifdef _WIN32
TEST_F(ShareCacheTest, unc_shares_can_be_added_and_are_scanned) {
	const auto unc = toAdminShare(sharePath);
	if(!File::isDirectory(unc)) {
		GTEST_SKIP() << "The administrative share " << unc << " is not reachable";
	}
	auto sm = ShareManager::getInstance();
	std::filesystem::create_directories(sharePath + "Sub");
	writeFile(sharePath + "Sub" PATH_SEPARATOR_STR "network.bin", "over SMB");

	// The old private-storage check treated every UNC path as private.
	EXPECT_FALSE(pft::isPrivatePath(unc));
	EXPECT_FALSE(pft::PrivatePathScanner().isPrivateRoot(unc));
	ASSERT_NO_THROW(sm->addDirectory(unc, "Network"));
	ASSERT_TRUE(waitForShareScans());
	{
		WLock l(sm->cs);
		auto dir = sm->directories.find("Network");
		ASSERT_NE(sm->directories.end(), dir);
		auto sub = dir->second->directories.find("Sub");
		ASSERT_NE(dir->second->directories.end(), sub);
		EXPECT_TRUE(findShared(*sub->second, "network.bin"));
	}

	// Private storage reached through the administrative share is still private.
	const auto storage = pft::storageRoot();
	std::filesystem::create_directories(storage + "Received");
	writeFile(storage + "Received" PATH_SEPARATOR_STR "inside.txt", "private");
	EXPECT_TRUE(pft::PrivatePathScanner().isPrivateRoot(toAdminShare(storage)));
	EXPECT_TRUE(pft::PrivatePathScanner().isPrivateRoot(toAdminShare(storage + "Received" PATH_SEPARATOR_STR)));
	EXPECT_TRUE(pft::isPrivatePath(toAdminShare(storage + "Received" PATH_SEPARATOR_STR "inside.txt")));
	EXPECT_THROW(sm->addDirectory(toAdminShare(storage), "Private"), ShareException);
}

TEST_F(ShareCacheTest, cache_loads_for_unc_share_roots) {
	const auto unc = toAdminShare(sharePath);
	if(!File::isDirectory(unc)) {
		GTEST_SKIP() << "The administrative share " << unc << " is not reachable";
	}
	auto sm = ShareManager::getInstance();
	const auto file = cacheFile(sharePath + "cached.bin", "from the share cache");
	{
		WLock l(sm->cs);
		sm->shares[unc] = "Network";
	}
	const auto uncFile = unc + "cached.bin";
	ASSERT_TRUE(HashManager::getInstance()->verifyFileTTH(uncFile, file.size, file.tth));
	sm->refresh(true, false, true);
	ASSERT_TRUE(sm->isTTHShared(file.tth));
	clearLoadedShare();
	ASSERT_TRUE(sm->loadShareCache());
	EXPECT_TRUE(sm->isTTHShared(file.tth));
}
#endif

TEST(SFVDirectoryReaderTest, reads_each_directory_once_and_agrees_with_sfv_reader) {
	const auto base = std::filesystem::temp_directory_path() /
		("dcpp-sfv-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(base / "one");
	std::filesystem::create_directories(base / "two");
	const auto one = (base / "one").u8string() + PATH_SEPARATOR_STR;
	const auto two = (base / "two").u8string() + PATH_SEPARATOR_STR;
	writeFile(one + "a.sfv", "; comment\r\nfirst.bin 1234abcd\r\nsecond.bin\tdeadbeef\r\n");
	writeFile(one + "b.sfv", "third.bin 00000042\r\n");
	writeFile(two + "c.sfv", "first.bin cafef00d\r\n");

	SFVDirectoryReader reader;
	for(const auto& path: { one + "first.bin", one + "second.bin", one + "third.bin", one + "missing.bin",
		two + "first.bin", two + "second.bin" })
	{
		SFVReader single(path);
		uint32_t crc = 0;
		EXPECT_EQ(single.hasCRC(), reader.getCRC(path, crc)) << path;
		if(single.hasCRC()) {
			EXPECT_EQ(single.getCRC(), crc) << path;
		}
	}
	uint32_t crc = 0;
	EXPECT_TRUE(reader.getCRC(one + "third.bin", crc));
	EXPECT_EQ(0x42u, crc);
	std::error_code ec;
	std::filesystem::remove_all(base, ec);
}

TEST_F(ShareCacheTest, finished_downloads_are_registered_as_hashed_when_their_tree_is_known) {
	auto sm = ShareManager::getInstance();
	auto hm = HashManager::getInstance();
	SettingsManager::getInstance()->set(SettingsManager::ADD_FINISHED_INSTANTLY, true);
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
	}
	sm->refresh(true, false, true);

	// Larger than one block, with its full tree stored as a download does before the data.
	string large(3 * HashManager::MIN_BLOCK_SIZE + 123, 'L');
	TigerTree largeTree(HashManager::MIN_BLOCK_SIZE);
	largeTree.update(large.data(), large.size());
	largeTree.finalize();
	ASSERT_TRUE(hm->addTree(largeTree));
	const auto largeTemp = configPath + "large.dctmp";
	writeFile(largeTemp, large);
	QueueManager::moveFile_(largeTemp, sharePath + "large.bin", largeTree.getRoot());
	const auto largeSnapshot = snapshotOf(sharePath + "large.bin");
	EXPECT_EQ(largeTree.getRoot(), hm->findTTH(sharePath + "large.bin", largeSnapshot.size, largeSnapshot.timestamp));
	EXPECT_TRUE(sm->isTTHShared(largeTree.getRoot()));

	// One block: the root alone describes it.
	const auto small = treeOf("small download");
	writeFile(configPath + "small.dctmp", "small download");
	QueueManager::moveFile_(configPath + "small.dctmp", sharePath + "small.bin", small.getRoot());
	EXPECT_TRUE(sm->isTTHShared(small.getRoot()));

	// Larger than one block without its tree: hashed as usual, never assumed.
	string unknown(2 * HashManager::MIN_BLOCK_SIZE, 'U');
	TigerTree unknownTree(HashManager::MIN_BLOCK_SIZE);
	unknownTree.update(unknown.data(), unknown.size());
	unknownTree.finalize();
	writeFile(configPath + "unknown.dctmp", unknown);
	QueueManager::moveFile_(configPath + "unknown.dctmp", sharePath + "unknown.bin", unknownTree.getRoot());
	const auto unknownSnapshot = snapshotOf(sharePath + "unknown.bin");
	EXPECT_FALSE(hm->findTTH(sharePath + "unknown.bin", unknownSnapshot.size, unknownSnapshot.timestamp));
	EXPECT_FALSE(sm->isTTHShared(unknownTree.getRoot()));
}

TEST_F(ShareCacheTest, merged_names_resolve_every_folders_files_after_refresh_and_cache_load) {
	auto sm = ShareManager::getInstance();
	const auto sep = string(PATH_SEPARATOR_STR);
	std::filesystem::create_directories(sharePath + "Common");
	std::filesystem::create_directories(secondSharePath + "Common");
	writeFile(sharePath + "only1.bin", "first only");
	writeFile(sharePath + "Common" + sep + "a.bin", "first a");
	writeFile(secondSharePath + "only2.bin", "second only");
	writeFile(secondSharePath + "Common" + sep + "a.bin", "second a");
	writeFile(secondSharePath + "Common" + sep + "b.bin", "second b");

	// One folder first, whose files derive their paths; then a second joins the name.
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Media";
	}
	sm->refresh(true, false, true);
	ASSERT_NO_THROW(sm->addDirectory(secondSharePath, "Media"));
	ASSERT_TRUE(waitForShareScans());

	auto check = [&](const char* when) {
		SCOPED_TRACE(when);
		EXPECT_EQ(sharePath + "only1.bin", sm->toReal("/Media/only1.bin"));
		EXPECT_EQ(secondSharePath + "only2.bin", sm->toReal("/Media/only2.bin"));
		EXPECT_EQ(secondSharePath + "Common" + sep + "b.bin", sm->toReal("/Media/Common/b.bin"));
		// The two a.bin files: one keeps its name, the other is renamed; each resolves to its own.
		std::set<string> paths { sm->toReal("/Media/Common/a.bin"), sm->toReal("/Media/Common/a (1).bin") };
		EXPECT_EQ((std::set<string> { sharePath + "Common" + sep + "a.bin", secondSharePath + "Common" + sep + "a.bin" }), paths);
	};
	check("after adding the second folder");
	sm->refresh(true, false, true);
	check("after a full refresh");
	sm->saveShareCache();
	clearLoadedShare();
	ASSERT_TRUE(sm->loadShareCache());
	check("after loading the cache");
}

TEST_F(ShareCacheTest, searches_uploads_lists_hashes_and_refreshes_run_together) {
	// The share lock is a reader/writer lock: searches, uploads, file lists and totals read in
	// parallel while hash results and refreshes change the share. Run them all at once and check
	// that nothing deadlocks, every reader sees a consistent share, and the indices add up.
	auto sm = ShareManager::getInstance();
	const string sep = PATH_SEPARATOR_STR;
	const string hub = "adc://stress.example.invalid";
	struct Stable { string path; int64_t size; TTHValue tth; };
	vector<Stable> stable;
	for(int d = 0; d < 3; ++d) {
		std::filesystem::create_directories(sharePath + "Folder " + std::to_string(d));
		for(int i = 0; i < 40; ++i) {
			const auto path = sharePath + "Folder " + std::to_string(d) + sep + "stress file " + std::to_string(d * 100 + i) + ".bin";
			const auto file = cacheFile(path, "stable contents " + std::to_string(d * 100 + i));
			stable.push_back({ file.path, file.size, file.tth });
		}
	}
	std::filesystem::create_directories(sharePath + "Changing");
	vector<string> changing;
	for(int i = 0; i < 8; ++i) {
		changing.push_back(cacheFile(sharePath + "Changing" + sep + "changing " + std::to_string(i) + ".bin", "first " + std::to_string(i)).path);
	}
	{
		WLock l(sm->cs);
		sm->shares[sharePath] = "Virtual";
	}
	sm->refresh(true, false, true);
	for(const auto& f: stable) {
		ASSERT_TRUE(sm->isTTHShared(f.tth));
	}

	std::atomic<bool> stop { false };
	std::atomic<int> failures { 0 };
	std::atomic<int> searches { 0 }, uploads { 0 }, lists { 0 }, hashes { 0 }, refreshes { 0 };
	auto fail = [&](const string& what) {
		if(failures++ < 5) ADD_FAILURE() << what;
	};

	vector<std::future<void>> workers;
	for(int t = 0; t < 3; ++t) {
		workers.push_back(std::async(std::launch::async, [&, t] {
			while(!stop) {
				auto results = t == 0 ? sm->search(string("stress$file"), SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 500, hub) :
					t == 1 ? sm->search(StringList { "ANstress", "ANfile" }, 500, hub) :
					sm->search(StringList { "ANstress" }, 500);
				if(results.size() < stable.size()) fail("a search missed shared files: " + std::to_string(results.size()));
				++searches;
			}
		}));
	}
	for(int t = 0; t < 2; ++t) {
		workers.push_back(std::async(std::launch::async, [&, t] {
			size_t i = t;
			while(!stop) {
				const auto& f = stable[i++ % stable.size()];
				try {
					auto real = t == 0 ? sm->toRealWithSize("TTH/" + f.tth.toBase32()) :
						sm->toRealWithSize("TTH/" + f.tth.toBase32(), hub, CID::generate());
					if(real.first != f.path || real.second != f.size) fail("an upload resolved to " + real.first);
					auto paths = sm->getRealPaths(f.tth, hub, CID());
					if(std::find(paths.begin(), paths.end(), f.path) == paths.end()) fail("getRealPaths lost " + f.path);
					if(sm->toVirtual(f.tth).empty()) fail("toVirtual failed");
				} catch(const Exception& e) {
					fail("an upload failed: " + e.getError());
				}
				++uploads;
			}
		}));
	}
	workers.push_back(std::async(std::launch::async, [&] {
		while(!stop) {
			try {
				++sm->listForce;
				if(sm->getListFile(hub).empty()) fail("no file list");
				std::unique_ptr<MemoryInputStream> partial(sm->generatePartialList("/Virtual/", true, hub));
				if(!partial) fail("no partial list");
				if(sm->getShareSize() <= 0 || sm->getSharedFiles() < stable.size()) fail("share totals dropped");
				if(sm->getShareSizeForHub(hub) <= 0 || sm->getSharedFiles(hub) < stable.size()) fail("hub totals dropped");
				ByteVector bloom;
				sm->getBloom(bloom, 8, 4096, 24, hub);
			} catch(const Exception& e) {
				fail("a file list failed: " + e.getError());
			}
			++lists;
		}
	}));
	workers.push_back(std::async(std::launch::async, [&] {
		// Hash results that change the share: each changing file gets new contents and a new root.
		int round = 0;
		while(!stop) {
			for(const auto& path: changing) {
				const auto contents = "round " + std::to_string(++round) + " of " + path;
				{
					std::ofstream output(path, std::ios::binary | std::ios::trunc);
					output << contents;
				}
				TigerTree tree(HashManager::MIN_BLOCK_SIZE);
				tree.update(contents.data(), contents.size());
				tree.finalize();
				if(!HashManager::getInstance()->verifyFileTTH(path, static_cast<int64_t>(contents.size()), tree.getRoot()))
					fail("hashing " + path + " failed");
				++hashes;
			}
		}
	}));
	workers.push_back(std::async(std::launch::async, [&] {
		while(!stop) {
			sm->refresh(true, false, true);
			++refreshes;
		}
	}));

	std::this_thread::sleep_for(std::chrono::milliseconds(2000));
	stop = true;
	for(auto& w: workers) {
		ASSERT_EQ(std::future_status::ready, w.wait_for(std::chrono::seconds(60))) << "a worker is stuck (deadlock)";
	}
	EXPECT_EQ(0, failures.load());
	EXPECT_GT(searches.load(), 0);
	EXPECT_GT(uploads.load(), 0);
	EXPECT_GT(lists.load(), 0);
	EXPECT_GT(hashes.load(), 0);
	EXPECT_GT(refreshes.load(), 0);
	std::cout << "    " << searches << " searches, " << uploads << " upload lookups, " << lists << " file lists, "
		<< hashes << " hash results, " << refreshes << " refreshes" << std::endl;

	// The incremental totals match a recount.
	WLock l(sm->cs);
	int64_t bytes = 0;
	for(const auto& item: sm->tthIndex) {
		bytes += item.second->getSize();
		EXPECT_EQ(item.first, *item.second->tth);
	}
	EXPECT_EQ(bytes, sm->sharedBytes);
	for(const auto& f: stable) {
		EXPECT_TRUE(sm->tthIndex.count(f.tth)) << f.path;
	}
}
