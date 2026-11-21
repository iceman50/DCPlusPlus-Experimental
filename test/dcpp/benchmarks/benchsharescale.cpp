// Copyright (C) 2026 iceman50
// Measures how sharing and hashing scale with very large shares and network (SMB) storage.
//   unc_primitives:       per-call cost of the file system operations a scan, cache load and hash do,
//                         on a local path and on the same files over loopback SMB (\\localhost\C$).
//   million_file_share:   search, hub INF totals, file list and share cache on an in-memory share.
//   million_file_hash_db: hash database load time and memory.
// Sizes: DCPP_BENCH_SHARE_FILES (default 1,000,000), DCPP_BENCH_UNC_DIRS / DCPP_BENCH_UNC_FILES.
#include "testbase.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <psapi.h>
#endif

#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/core/io/File.h>
#include <dcpp/core/io/SFVReader.h>
#include <dcpp/core/io/db/SQLiteDB.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#define private public
#include <dcpp/hash/HashManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/queue/QueueManager.h>
#undef private
#include <dcpp/hub/ClientManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/search/SearchResult.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/transfer/download/DownloadManager.h>
#include <dcpp/transfer/private/PrivateTransferStorage.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dcpp/util/Util.h>
#include <dcpp/util/text/Text.h>

using namespace dcpp;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

int envInt(const char* name, int fallback) {
	const auto value = std::getenv(name);
	return value ? std::atoi(value) : fallback;
}

int64_t privateBytes() {
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX counters = { };
	counters.cb = sizeof(counters);
	if(K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
		return static_cast<int64_t>(counters.PrivateUsage);
#endif
	return 0;
}

TTHValue fakeRoot(uint64_t seed) {
	uint8_t data[TTHValue::BYTES];
	for(auto& b: data) {
		seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
		b = static_cast<uint8_t>(seed >> 56);
	}
	return TTHValue(data);
}

struct Managers {
	explicit Managers(const string& configPath) : path(configPath) {
		Util::PathsMap paths;
		for(int p = 0; p < Util::PATH_LAST; ++p)
			paths[static_cast<Util::Paths>(p)] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);

		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		HashManager::newInstance();
		ConnectivityManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
		DownloadManager::newInstance();
		UploadManager::newInstance();
		ConnectionManager::newInstance();
	}
	~Managers() {
		ConnectionManager::deleteInstance();
		UploadManager::deleteInstance();
		DownloadManager::deleteInstance();
		ShareManager::deleteInstance();
		QueueManager::deleteInstance();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		ConnectivityManager::deleteInstance();
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
	string path;
};

string makeConfigPath(const char* tag) {
	const auto base = std::filesystem::temp_directory_path() /
		(string("dcpp-") + tag + "-" + std::to_string(Clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(base);
	return base.string() + PATH_SEPARATOR;
}

template<typename F> void report(const char* what, size_t count, F&& f) {
	const auto start = Clock::now();
	f();
	const auto ms = msSince(start);
	std::cout << "  " << what << ": " << ms << " ms total, " << (count ? ms * 1000.0 / count : 0) << " us each (" << count << ")\n";
}

}

#ifdef _WIN32
TEST(benchsharescale, unc_primitives) {
	const auto configPath = makeConfigPath("share-unc");
	Managers managers(configPath);

	// dirs folders two levels down, each holding files small files.
	const auto dirs = envInt("DCPP_BENCH_UNC_DIRS", 200);
	const auto filesPerDir = envInt("DCPP_BENCH_UNC_FILES", 50);
	const auto localRoot = configPath + "Share" PATH_SEPARATOR_STR;
	StringList localDirs, localFiles;
	for(int d = 0; d < dirs; ++d) {
		const auto dir = localRoot + "Group " + std::to_string(d / 20) + PATH_SEPARATOR_STR + "Folder " + std::to_string(d) + PATH_SEPARATOR_STR;
		std::filesystem::create_directories(dir);
		localDirs.push_back(dir);
		for(int f = 0; f < filesPerDir; ++f) {
			const auto file = dir + "File " + std::to_string(f) + ".bin";
			std::ofstream(file, std::ios::binary) << file;
			localFiles.push_back(file);
		}
	}

	// \\localhost\C$\... reaches the same files through the SMB client.
	ASSERT_GT(localRoot.size(), 3u);
	ASSERT_EQ(':', localRoot[1]);
	const auto toUnc = [](const string& p) { return string("\\\\localhost\\") + p[0] + "$" + p.substr(2); };
	if(!(FileFindIter(toUnc(localRoot).substr(0, toUnc(localRoot).size() - 1)) != FileFindIter()))
		GTEST_SKIP() << "The administrative share " << toUnc(localRoot) << " is not reachable";

	for(int pass = 0; pass < 2; ++pass) {
		const bool unc = pass == 1;
		const auto path = [&](const string& p) { return unc ? toUnc(p) : p; };
		std::cout << (unc ? "SMB (loopback)" : "Local disk") << ", " << localDirs.size() << " folders, " << localFiles.size() << " files:\n";
		{
			size_t privateDirs = 0, privateFiles = 0;
			for(const auto& d: localDirs) privateDirs += pft::isPrivatePath(path(d)) ? 1 : 0;
			for(size_t i = 0; i < localFiles.size(); i += 100) privateFiles += pft::isPrivatePath(path(localFiles[i])) ? 1 : 0;
			std::cout << "  isPrivatePath true for " << privateDirs << " of " << localDirs.size() << " folders and " << privateFiles << " of "
				<< (localFiles.size() + 99) / 100 << " sampled files\n";
		}

		report("pft::isPrivatePath per file (single paths only now)", localFiles.size(), [&] {
			for(const auto& f: localFiles) pft::isPrivatePath(path(f));
		});
		report("pft::isPrivateLocation per file (share cache load, uploads)", localFiles.size(), [&] {
			for(const auto& f: localFiles) pft::isPrivateLocation(path(f));
		});
		report("pft::hasPrivateMarker per file (hashing, upload requests)", localFiles.size(), [&] {
			for(const auto& f: localFiles) pft::hasPrivateMarker(path(f));
		});
		report("PrivatePathScanner::isPrivateDirectory per folder (scan)", localDirs.size(), [&] {
			const pft::PrivatePathScanner scanner;
			for(const auto& d: localDirs) scanner.isPrivateDirectory(path(d), Util::getLastDir(d), false);
		});
		report("folder listing (FileFindIter) per folder", localDirs.size(), [&] {
			for(const auto& d: localDirs)
				for(FileFindIter i(path(d) + "*"), end; i != end; ++i) { }
		});
		report("File open + size + time per file (hasher, TTHDone snapshot)", localFiles.size(), [&] {
			for(const auto& f: localFiles) {
				File file(path(f), File::READ, File::OPEN);
				file.getSize();
				file.getLastModified();
			}
		});
		report("SFVReader per file (hasher)", localFiles.size(), [&] {
			for(const auto& f: localFiles) SFVReader sfv(path(f));
		});
		report("GetDiskFreeSpace per file (FileReader direct read)", localFiles.size(), [&] {
			for(const auto& f: localFiles) {
				DWORD a, b, c, d;
				::GetDiskFreeSpaceW(File::toNativePath(Util::getFilePath(path(f))).c_str(), &a, &b, &c, &d);
			}
		});

		auto sm = ShareManager::getInstance();
		{
			WLock l(sm->cs);
			sm->shares.clear();
			sm->shares[path(localRoot)] = "Share";
		}
		sm->updateFilterCache();
		report("ShareManager::buildTree, per file", localFiles.size(), [&] {
			sm->buildTree(path(localRoot));
		});
	}
}
#endif

#ifdef _WIN32
TEST(benchsharescale, hash_small_files) {
	// End to end through the real hasher thread and ShareManager's TTHDone handler.
	const auto configPath = makeConfigPath("hash-small");
	Managers managers(configPath);
	HashManager::getInstance()->startup([](float) { });
	const auto count = envInt("DCPP_BENCH_HASH_FILES", 500);
	const auto toUnc = [](const string& p) { return string("\\\\localhost\\") + p[0] + "$" + p.substr(2); };
	for(int pass = 0; pass < 2; ++pass) {
		const auto dir = configPath + "Small" + std::to_string(pass) + PATH_SEPARATOR_STR;
		std::filesystem::create_directories(dir);
		const string data(4096, 'x');
		StringList files;
		for(int i = 0; i < count; ++i) {
			files.push_back(dir + "File " + std::to_string(i) + ".bin");
			std::ofstream(files.back(), std::ios::binary) << data << i;
		}
		const auto start = Clock::now();
		for(const auto& f: files) {
			const auto path = pass ? toUnc(f) : f;
			File file(path, File::READ, File::OPEN | File::SHARED);
			const auto size = file.getSize();
			const auto time = file.getLastModified();
			file.close();
			HashManager::getInstance()->requestTTH(path, size, time);
		}
		for(;;) {
			string current; uint64_t bytes; size_t left;
			HashManager::getInstance()->getStats(current, bytes, left);
			if(left == 0) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		const auto ms = msSince(start);
		std::cout << "  " << (pass ? "SMB (loopback)" : "Local disk") << ": hashed " << count << " 4 KiB files in " << ms << " ms, "
			<< ms / count << " ms per file (" << count * 1000.0 / ms << " files/s)\n";
	}
}

TEST(benchsharescale, hash_two_volumes) {
	// Two network shares (here two server names for the same disk, so two volumes): with one hasher
	// per volume they are read at the same time, which is what a NAS's per-file latency needs.
	const auto configPath = makeConfigPath("hash-volumes");
	Managers managers(configPath);
	HashManager::getInstance()->startup([](float) { });
	const auto count = envInt("DCPP_BENCH_HASH_FILES", 500);
	const auto toUnc = [](const string& server, const string& p) { return "\\\\" + server + "\\" + p[0] + "$" + p.substr(2); };
	for(int volumes = 1; volumes <= 2; ++volumes) {
		SettingsManager::getInstance()->set(SettingsManager::MAX_HASH_VOLUMES, volumes);
		const auto dir = configPath + "Volumes" + std::to_string(volumes) + PATH_SEPARATOR_STR;
		std::filesystem::create_directories(dir);
		const string data(4096, 'x');
		StringList files;
		for(int i = 0; i < count; ++i) {
			const auto local = dir + "File " + std::to_string(i) + ".bin";
			std::ofstream(local, std::ios::binary) << data << i << volumes;
			files.push_back(toUnc(i % 2 ? "127.0.0.1" : "localhost", local));
		}
		// Queued while paused (as a share scan queues a folder), so only the hashing is timed.
		HashManager::getInstance()->pauseHashing();
		for(const auto& path: files) {
			File file(path, File::READ, File::OPEN | File::SHARED);
			const auto size = file.getSize();
			const auto time = file.getLastModified();
			file.close();
			HashManager::getInstance()->requestTTH(path, size, time);
		}
		const auto start = Clock::now();
		HashManager::getInstance()->resumeHashing();
		for(;;) {
			string current; uint64_t bytes; size_t left;
			HashManager::getInstance()->getStats(current, bytes, left);
			if(left == 0) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		const auto ms = msSince(start);
		std::cout << "  " << volumes << " drive(s) at a time, two SMB shares: hashed " << count << " 4 KiB files in " << ms << " ms, "
			<< ms / count << " ms per file (" << count * 1000.0 / ms << " files/s)\n";
	}
}
#endif

TEST(benchsharescale, million_file_share) {
	const auto configPath = makeConfigPath("share-million");
	Managers managers(configPath);
	auto sm = ShareManager::getInstance();

	const auto fileCount = envInt("DCPP_BENCH_SHARE_FILES", 1000000);
	const int filesPerDir = 100;
	const auto realRoot = configPath + "Media\\";
	std::filesystem::create_directories(realRoot);
	const auto before = privateBytes();
	const auto buildStart = Clock::now();
	{
		WLock l(sm->cs);
		sm->shares[realRoot] = "Media";
		auto root = ShareManager::Directory::create("Media");
		sm->directories["Media"] = root;
		ShareManager::Directory::Ptr group, dir;
		for(int i = 0; i < fileCount; ++i) {
			if(i % (filesPerDir * 100) == 0) {
				const auto name = "Collection " + std::to_string(i / (filesPerDir * 100));
				group = ShareManager::Directory::create(name, root, 1700000000);
				root->directories.emplace(name, group);
			}
			if(i % filesPerDir == 0) {
				const auto name = "Album " + std::to_string(i / filesPerDir) + " - Some Artist Name";
				dir = ShareManager::Directory::create(name, group, 1700000000);
				group->directories.emplace(name, dir);
			}
			const auto name = std::to_string(i % filesPerDir) + " - A Typical Track Title " + std::to_string(i) + ".flac";
			// As a scan builds it: one folder shares this name, so paths follow from the directories.
			ShareManager::Directory::File file(name, 30000000 + i, dir, fakeRoot(i + 1), 1700000000);
			dir->files.insert(std::move(file));
		}
	}
	const auto buildMs = msSince(buildStart);
	{
		WLock l(sm->cs);
		report("rebuildIndices", fileCount, [&] { sm->rebuildIndices(fileCount); });
	}
	const auto after = privateBytes();
	std::cout << fileCount << " files: tree built in " << buildMs << " ms, memory " << (after - before) / 1048576 << " MiB ("
		<< (after - before) / std::max(1, fileCount) << " bytes per file, tree + TTH index + bloom)\n";

	const string hub = "adcs://hub.example:1511";
	report("search, hub overload, no match (full walk)", 1, [&] {
		sm->search(string("zzqqxx"), SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10, hub);
	});
	report("search, hub overload, two terms, few matches", 1, [&] {
		sm->search(string("typical$999999"), SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10, hub);
	});
	report("search, ADC, no match (full walk)", 1, [&] {
		sm->search(StringList { "ANzzqqxx" }, 10, hub);
	});
	report("search by TTH", 1, [&] {
		sm->search(StringList { "TR" + fakeRoot(500000).toBase32() }, 10, hub);
	});
	report("getShareSize()", 1, [&] { sm->getShareSize(); });
	report("getShareSizeForHub + getSharedFiles(hub) (each ADC INF, per hub)", 1, [&] {
		sm->getShareSizeForHub(hub);
		sm->getSharedFiles(hub);
	});
	{
		WLock l(sm->cs);
		int rejected = 0;
		for(int i = 0; i < 1000; ++i)
			if(!sm->bloom.match("zq" + std::to_string(i * 7919) + "xk")) ++rejected;
		std::cout << "  bloom: " << sm->bloom.size() << " bits; rejects " << rejected << " of 1000 random non-matching terms\n";
	}

	// How long another thread (an upload, a search, a hash result) waits for the share lock meanwhile.
	auto longestWait = [&](const std::function<void ()>& work, bool exclusive = false) {
		std::atomic<bool> done { false };
		double longest = 0;
		std::thread probe([&] {
			while(!done) {
				const auto start = Clock::now();
				if(exclusive) {
					// As a hash result or a finished download takes it.
					WLock l(sm->cs);
				} else {
					sm->isTTHShared(fakeRoot(1));
				}
				longest = std::max(longest, msSince(start));
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});
		work();
		done = true;
		probe.join();
		return longest;
	};
	auto searchWalks = [&] {
		for(int i = 0; i < 5; ++i) {
			sm->search(string("typical$999999"), SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10, hub);
		}
	};
	double wait = 0;
	report("generateXmlList (full list)", fileCount, [&] {
		wait = longestWait([&] { ++sm->listForce; sm->generateXmlList(); });
	});
	std::cout << "    longest wait for the share lock meanwhile: " << wait << " ms\n";
	report("saveShareCache", fileCount, [&] {
		wait = longestWait([&] { sm->saveShareCache(); });
	});
	std::cout << "    longest wait for the share lock meanwhile: " << wait << " ms\n";
	report("5 searches walking the whole share (two terms)", 5, [&] {
		wait = longestWait(searchWalks);
	});
	std::cout << "    longest wait of an upload lookup meanwhile: " << wait << " ms\n";
	wait = longestWait(searchWalks, true);
	std::cout << "    longest wait of a hash result meanwhile: " << wait << " ms\n";
	{
		WLock l(sm->cs);
		sm->directories.clear();
		sm->tthIndex.clear();
	}
	report("  of which reading the cache's rows from SQLite", fileCount, [&] {
		SQLiteDB db(configPath + "ShareCache.sqlite3");
		auto files = db.prepare("SELECT directory_id, name, size, tth, real_path, last_write FROM files ORDER BY id");
		size_t bytes = 0;
		while(files.step()) {
			bytes += files.columnText(1).size() + files.columnText(4).size() + (files.columnIsNull(3) ? 0 : files.columnText(3).size());
		}
		EXPECT_GT(bytes, 0u);
	});
	report("  of which rebuildIndices", fileCount, [&] {
		WLock l(sm->cs);
		sm->rebuildIndices(fileCount);
	});
	{
		WLock l(sm->cs);
		sm->directories.clear();
		sm->tthIndex.clear();
	}
	report("loadShareCache", fileCount, [&] {
		EXPECT_TRUE(sm->loadShareCache());
	});
	{
		WLock l(sm->cs);
		std::cout << "  files loaded from the cache: " << sm->tthIndex.size() << "\n";
	}
}

TEST(benchsharescale, million_file_hash_db) {
	const auto configPath = makeConfigPath("hash-million");
	Managers managers(configPath);
	const auto fileCount = envInt("DCPP_BENCH_SHARE_FILES", 1000000);

	{
		SQLiteDB db(configPath + "HashStore.sqlite3");
		HashManager::getInstance()->store.db.open(configPath + "HashStore.sqlite3");
		HashManager::getInstance()->store.createSchema();
		HashManager::getInstance()->store.db.close();
		SQLiteTransaction transaction(db);
		auto tree = db.prepare("INSERT INTO trees(root, size, block_size, leaves) VALUES(?1, ?2, 65536, NULL)");
		auto file = db.prepare("INSERT INTO files(path, size, timestamp, root) VALUES(?1, ?2, 1700000000, ?3)");
		for(int i = 0; i < fileCount; ++i) {
			const auto root = fakeRoot(i + 1);
			tree.bind(1, root.data, TTHValue::BYTES);
			tree.bind(2, static_cast<int64_t>(30000 + i));
			tree.stepDone(); tree.reset(); tree.clearBindings();
			const auto path = "Z:\\Media\\Collection " + std::to_string(i / 10000) + "\\Album " + std::to_string(i / 100) +
				" - Some Artist Name\\" + std::to_string(i % 100) + " - A Typical Track Title " + std::to_string(i) + ".flac";
			file.bind(1, path);
			file.bind(2, static_cast<int64_t>(30000 + i));
			file.bind(3, root.data, TTHValue::BYTES);
			file.stepDone(); file.reset(); file.clearBindings();
		}
		transaction.commit();
	}

	const auto before = privateBytes();
	report("HashManager startup (loads every record into memory)", fileCount, [&] {
		HashManager::getInstance()->store.load([](float) { });
	});
	const auto after = privateBytes();
	std::cout << "  hash index memory: " << (after - before) / 1048576 << " MiB (" << (after - before) / std::max(1, fileCount) << " bytes per file)\n";
	{
		// The hasher thread is not started here, so queued requests stay queued.
		const auto queueBefore = privateBytes();
		report("HashManager::queueHash (queue a file for hashing)", fileCount, [&] {
			for(int i = 0; i < fileCount; ++i)
				HashManager::getInstance()->queueHash("Z:\\New\\Folder " + std::to_string(i / 100) + "\\Unhashed file " + std::to_string(i) + ".bin", 1000 + i, 1700000000);
		});
		const auto queueAfter = privateBytes();
		std::cout << "  hash queue memory: " << (queueAfter - queueBefore) / 1048576 << " MiB (" << (queueAfter - queueBefore) / std::max(1, fileCount) << " bytes per queued file)\n";
		report("Hasher::getStats (hash progress, polled by the UI)", 1, [&] {
			string file; uint64_t bytes; size_t files;
			HashManager::getInstance()->getStats(file, bytes, files);
		});
	}
	report("getTTH lookups", 100000, [&] {
		for(int i = 0; i < 100000; ++i) {
			const auto path = "Z:\\Media\\Collection " + std::to_string(i / 10000) + "\\Album " + std::to_string(i / 100) +
				" - Some Artist Name\\" + std::to_string(i % 100) + " - A Typical Track Title " + std::to_string(i) + ".flac";
			HashManager::getInstance()->store.getTTH(path, 30000 + i, 1700000000);
		}
	});
}
