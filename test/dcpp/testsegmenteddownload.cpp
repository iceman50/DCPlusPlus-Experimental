// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"

#include <chrono>
#include <filesystem>

#include <dcpp/core/io/File.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/queue/QueueItem.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/transfer/download/Download.h>
#define private public
#include <dcpp/connection/UserConnection.h>
#undef private

using namespace dcpp;

namespace {

constexpr int64_t blockSize = 64 * 1024;

class SegmentedDownloadTest : public ::testing::Test {
public:
	void SetUp() override {
		const auto base = std::filesystem::temp_directory_path() /
			("dcpp-segments-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(base);
		configPath = base.string() + PATH_SEPARATOR;
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);
		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		HashManager::newInstance();

		data.resize(4 * blockSize);
		for(size_t i = 0; i < data.size(); ++i)
			data[i] = static_cast<char>((i * 7 + i / 977) & 0xff);
		TigerTree tree(blockSize);
		tree.update(data.data(), data.size());
		tree.finalize();
		ASSERT_TRUE(HashManager::getInstance()->addTree(tree));
		root = tree.getRoot();
	}

	void TearDown() override {
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

	/** Receive a download's whole segment, verified against the tree as a real transfer is. */
	void receive(Download& d) {
		const auto start = d.getStartPos();
		const auto size = d.getSize();
		d.open(size, false, false);
		ASSERT_EQ(static_cast<size_t>(size), d.getOutput()->write(data.data() + start, static_cast<size_t>(size)));
		d.getOutput()->flush();
		d.close();
	}

	string configPath;
	string data;
	TTHValue root;
};

}

TEST_F(SegmentedDownloadTest, a_later_segment_may_arrive_before_the_file_is_created) {
	const auto temp = configPath + "file.dctmp";
	QueueItem item(configPath + "file.bin", static_cast<int64_t>(data.size()), QueueItem::NORMAL, 0, 0, root);
	item.setTempTarget(temp);

	// Two connections to the same user start the same new file, as MCN does.
	UserConnection first(false), second(false);
	first.setFlag(UserConnection::FLAG_DOWNLOAD);
	second.setFlag(UserConnection::FLAG_DOWNLOAD);
	Download head(first, item);
	item.getDownloads().push_back(&head);
	Download tail(second, item);
	item.getDownloads().push_back(&tail);
	ASSERT_EQ(0, head.getStartPos());
	ASSERT_EQ(blockSize, tail.getStartPos());

	// The later segment's reply arrives first, before anything has created the file.
	ASSERT_EQ(-1, File::getSize(temp));
	receive(tail);
	receive(head);
	item.getDownloads().clear();

	EXPECT_EQ(static_cast<int64_t>(data.size()), File::getSize(temp));
	File file(temp, File::READ, File::OPEN);
	const auto written = file.read();
	EXPECT_EQ(data.substr(0, 2 * blockSize), written.substr(0, 2 * blockSize));
}

TEST_F(SegmentedDownloadTest, resuming_still_requires_the_existing_file) {
	const auto temp = configPath + "resumed.dctmp";
	QueueItem item(configPath + "resumed.bin", static_cast<int64_t>(data.size()), QueueItem::NORMAL, 0, 0, root);
	item.setTempTarget(temp);
	item.addSegment(Segment(0, blockSize));

	// The first block was downloaded earlier, but its file has since disappeared.
	UserConnection connection(false);
	connection.setFlag(UserConnection::FLAG_DOWNLOAD);
	Download resumed(connection, item);
	ASSERT_EQ(blockSize, resumed.getStartPos());
	EXPECT_THROW(resumed.open(resumed.getSize(), false, false), Exception);
	EXPECT_EQ(-1, File::getSize(temp));
}
