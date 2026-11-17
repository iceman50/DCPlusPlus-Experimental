// Copyright (C) 2026 iceman50
// Times the first-start migration of a DC++ 0.884 hash database (HashIndex.xml + HashData.dat)
// into HashStore.sqlite3 for a large share. Set DCPP_BENCH_MIGRATION_FILES to change the size.
#include "testbase.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <psapi.h>
#endif

#include <dcpp/core/io/File.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/events/LogManager.h>
#define private public
#include <dcpp/hash/HashManager.h>
#undef private
#include <dcpp/hash/value/MerkleTree.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/util/Util.h>

using namespace dcpp;

namespace {

using Clock = std::chrono::steady_clock;

uint64_t next(uint64_t& state) {
	state = state * 6364136223846793005ULL + 1442695040888963407ULL;
	return state >> 11;
}

double peakMemoryMiB() {
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS counters = { sizeof(counters) };
	if(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
		return counters.PeakWorkingSetSize / (1024.0 * 1024.0);
#endif
	return 0;
}

}

TEST(benchhashmigration, legacy_hash_database_migration) {
	const auto files = std::getenv("DCPP_BENCH_MIGRATION_FILES") ? std::atoi(std::getenv("DCPP_BENCH_MIGRATION_FILES")) : 100000;
	const auto base = std::filesystem::temp_directory_path() /
		("dcpp-migration-bench-" + std::to_string(Clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(base);
	const auto configPath = base.string() + PATH_SEPARATOR;
	Util::PathsMap paths;
	for(int path = 0; path < Util::PATH_LAST; ++path)
		paths[static_cast<Util::Paths>(path)] = configPath;
	paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
	Util::initialize(paths);

	// About 30% small files (no stored leaves), the rest 1-32 MiB with upstream's block size.
	auto start = Clock::now();
	int64_t dataBytes = 0, sharedBytes = 0;
	{
		File index(configPath + "HashIndex.xml", File::WRITE, File::CREATE | File::TRUNCATE);
		File data(configPath + "HashData.dat", File::WRITE, File::CREATE | File::TRUNCATE);
		data.write(string(8, '\0'));
		int64_t offset = 8;
		string trees = "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\r\n<HashStore Version=\"3\">\r\n\t<Trees>\r\n";
		string fileEntries;
		uint64_t state = 12345;
		std::vector<uint8_t> leaves;
		for(int i = 0; i < files; ++i) {
			const bool small = next(state) % 10 < 3;
			const int64_t size = small ? 1 + static_cast<int64_t>(next(state) % (60 * 1024)) :
				(1 << 20) + static_cast<int64_t>(next(state) % (31LL << 20));
			const auto blockSize = std::max(TigerTree::calcBlockSize(size, 10), HashManager::MIN_BLOCK_SIZE);
			const auto blocks = TigerTree::calcBlocks(size, blockSize);
			leaves.resize(static_cast<size_t>(blocks) * TigerTree::BYTES);
			for(auto& b: leaves) b = static_cast<uint8_t>(next(state));
			TigerTree tree(size, blockSize, leaves.data());
			const auto root = tree.getRoot().toBase32();
			if(blocks == 1) {
				trees += "\t\t<Hash Type=\"TTH\" Index=\"-1\" BlockSize=\"" + std::to_string(blockSize) + "\" Size=\"" +
					std::to_string(size) + "\" Root=\"" + root + "\"/>\r\n";
			} else {
				data.write(leaves.data(), leaves.size());
				trees += "\t\t<Hash Type=\"TTH\" Index=\"" + std::to_string(offset) + "\" BlockSize=\"" + std::to_string(blockSize) +
					"\" Size=\"" + std::to_string(size) + "\" Root=\"" + root + "\"/>\r\n";
				offset += static_cast<int64_t>(leaves.size());
			}
			fileEntries += "\t\t<File Name=\"D:\\Share\\Folder " + std::to_string(i / 200) + "\\File " + std::to_string(i) +
				".bin\" TimeStamp=\"" + std::to_string(1700000000 + i) + "\" Root=\"" + root + "\"/>\r\n";
			sharedBytes += size;
			if(trees.size() > (1 << 20)) { index.write(trees); trees.clear(); }
		}
		dataBytes = offset;
		trees += "\t</Trees>\r\n\t<Files>\r\n";
		index.write(trees);
		index.write(fileEntries);
		index.write(string("\t</Files>\r\n</HashStore>\r\n"));
	}
	std::cout << "generated " << files << " files (" << sharedBytes / (1LL << 30) << " GiB shared), HashData.dat " <<
		dataBytes / (1 << 20) << " MiB, HashIndex.xml " << File::getSize(configPath + "HashIndex.xml") / (1 << 20) <<
		" MiB in " << std::chrono::duration<double>(Clock::now() - start).count() << " s\n";

	SettingsManager::newInstance();
	LogManager::newInstance();
	TimerManager::newInstance();
	HashManager::newInstance();

	const auto memoryBefore = peakMemoryMiB();
	start = Clock::now();
	std::vector<float> progress;
	HashManager::getInstance()->startup([&](float p) { progress.push_back(p); });
	const auto total = std::chrono::duration<double>(Clock::now() - start).count();
	// Startup progress must keep moving through the migration, not stop once the index is read.
	EXPECT_TRUE(std::is_sorted(progress.begin(), progress.end()));
	const auto duringMigration = std::count_if(progress.begin(), progress.end(), [](float p) { return p > 0.3f && p < 1.0f; });
	std::cout << progress.size() << " progress updates, " << duringMigration << " of them during migration\n";
	if(files >= 10000) {
		EXPECT_GT(duringMigration, 0);
	}
	std::cout << "migration: " << total << " s, peak memory " <<
		peakMemoryMiB() << " MiB (" << memoryBefore << " MiB before), HashStore.sqlite3 " <<
		File::getSize(configPath + "HashStore.sqlite3") / (1 << 20) << " MiB, -wal " <<
		std::max<int64_t>(0, File::getSize(configPath + "HashStore.sqlite3-wal")) / (1 << 20) << " MiB\n";

	size_t migrated = 0;
	for(const auto& directory: HashManager::getInstance()->store.fileIndex)
		migrated += directory.second.size();
	EXPECT_EQ(static_cast<size_t>(files), migrated);

	HashManager::getInstance()->shutdown();
	HashManager::deleteInstance();
	TimerManager::getInstance()->shutdown();
	TimerManager::deleteInstance();
	LogManager::deleteInstance();
	SettingsManager::deleteInstance();
	std::filesystem::remove_all(base);
}
