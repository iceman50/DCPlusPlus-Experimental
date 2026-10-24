/*
 * Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "testbase.h"

#include <dcpp/Download.h>
#include <dcpp/File.h>
#include <dcpp/QueueItem.h>
#define private public
#include <dcpp/UserConnection.h>
#undef private

#include <chrono>

using namespace dcpp;

#ifdef _WIN32
namespace {
class DownloadAllocationTest : public ::testing::Test {
protected:
	void SetUp() override {
		SettingsManager::newInstance();
		path = Util::getTempPath() + "dcpp-allocation-" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dctmp";
	}
	void TearDown() override {
		File::deleteFile(path);
		SettingsManager::deleteInstance();
	}
	string path;
};
}

TEST_F(DownloadAllocationTest, allocationErrorIdentifiesUnfinishedFileAndPreservesData) {
	File(path, File::WRITE, File::CREATE | File::TRUNCATE).write("keep this data");
	UserConnection connection(false);
	connection.setFlag(UserConnection::FLAG_DOWNLOAD);
	// Unknown size bypasses tree lookup; inject an invalid allocation size to
	// exercise the real failure path without filling the test machine's disk.
	QueueItem item(path + ".finished", -1, QueueItem::NORMAL, 0, 0, TTHValue());
	item.setTempTarget(path);
	Download download(connection, item);
	download.getTigerTree().setFileSize(-1);
	try {
		download.open(0, false);
		FAIL() << "Invalid allocation should fail";
	} catch(const FileException& e) {
		const auto& error = e.getError();
		EXPECT_NE(string::npos, error.find("Could not allocate download file \"" + path + "\""));
		EXPECT_EQ(string::npos, error.find(path + ".finished"));
		EXPECT_NE(string::npos, error.find("size: -1 bytes"));
		EXPECT_NE(string::npos, error.find("previous size: 14 bytes"));
		EXPECT_NE(string::npos, error.find("Windows error 131"));
		EXPECT_NE(string::npos, error.find("Available space:"));
	}
	EXPECT_FALSE(download.getOutput());
	EXPECT_EQ("keep this data", File(path, File::READ, File::OPEN).read());
}
#endif
