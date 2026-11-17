// Copyright (C) 2026 iceman50
// Times loading and saving a download queue the size users with very large queues report
// (DC++ bug 2122791: 80,000-150,000 queued files). Override the size with DCPP_BENCH_QUEUE_ITEMS.
#include "testbase.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/core/io/File.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/hash/value/Encoder.h>
#include <dcpp/hub/ClientManager.h>
#define private public
#include <dcpp/queue/QueueManager.h>
#undef private
#include <dcpp/search/SearchManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/transfer/download/DownloadManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dcpp/user/CID.h>
#include <dcpp/util/Util.h>

using namespace dcpp;

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
	return std::chrono::duration<double>(Clock::now() - start).count();
}

string base32(uint64_t seed, size_t bytes) {
	std::vector<uint8_t> data(bytes);
	for(size_t i = 0; i < bytes; ++i) {
		seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
		data[i] = static_cast<uint8_t>(seed >> 56);
	}
	return Encoder::toBase32(data.data(), data.size());
}

}

TEST(benchqueueload, large_queue_load_and_save) {
	const auto items = std::getenv("DCPP_BENCH_QUEUE_ITEMS") ? std::atoi(std::getenv("DCPP_BENCH_QUEUE_ITEMS")) : 150000;
	const auto base = std::filesystem::temp_directory_path() /
		("dcpp-queue-bench-" + std::to_string(Clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(base);
	const auto configPath = base.string() + PATH_SEPARATOR;
	Util::PathsMap paths;
	for(int path = 0; path < Util::PATH_LAST; ++path)
		paths[static_cast<Util::Paths>(path)] = configPath;
	paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
	Util::initialize(paths);

	// 150,000 files in 1,500 folders, each with two of 2,000 sources, a third partly downloaded.
	{
		File out(configPath + "Queue.xml", File::WRITE, File::CREATE | File::TRUNCATE);
		string xml = "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\r\n<Downloads Version=\"0.884\">\r\n";
		for(int i = 0; i < items; ++i) {
			const auto target = configPath + "Downloads" PATH_SEPARATOR_STR "Folder " + std::to_string(i / 100) +
				PATH_SEPARATOR_STR "File " + std::to_string(i) + ".bin";
			xml += "\t<Download Target=\"" + target + "\" Size=\"" + std::to_string(100000000 + i) +
				"\" Priority=\"3\" Added=\"1700000000\" TTH=\"" + base32(i + 1, 24) + "\" TempTarget=\"\">\r\n";
			if(i % 3 == 0)
				xml += "\t\t<Segment Start=\"0\" Size=\"1048576\"/>\r\n";
			for(int s = 0; s < 2; ++s)
				xml += "\t\t<Source CID=\"" + base32(1000000 + (i * 7 + s * 13) % 2000, 24) + "\" Hub=\"adc://hub.example:411\"/>\r\n";
			xml += "\t</Download>\r\n";
			if(xml.size() > (1 << 20)) { out.write(xml); xml.clear(); }
		}
		xml += "</Downloads>\r\n";
		out.write(xml);
	}

	SettingsManager::newInstance();
	SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, false);
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

	auto start = Clock::now();
	QueueManager::getInstance()->loadQueue([](float) { });
	const auto loadSeconds = secondsSince(start);
	const auto loaded = QueueManager::getInstance()->fileQueue.getSize();

	start = Clock::now();
	QueueManager::getInstance()->saveQueue(true);
	const auto saveSeconds = secondsSince(start);

	std::cout << items << " queued files: loaded " << loaded << " in " << loadSeconds << " s, saved in " << saveSeconds << " s\n";
	EXPECT_EQ(static_cast<size_t>(items), loaded);

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
	std::filesystem::remove_all(base);
}
