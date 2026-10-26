/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "testbase.h"

#include <dcpp/transfer/private/PrivateTransferStorage.h>
#include <dcpp/system/File.h>
#include <dcpp/text/Text.h>
#include <dcpp/settings/SettingsManager.h>
#include <filesystem>

using namespace dcpp;
#ifdef _WIN32
namespace {

class PrivateStorage :
	public ::testing::Test
{
protected:
	std::filesystem::path base;
	std::filesystem::path otherVolume;
	std::string source;
	pft::Metadata metadata;

	void SetUp() override {
		base = std::filesystem::current_path() / "build" / std::filesystem::u8path("dcpp-private-test-" + pft::randomHex(16));
		std::filesystem::create_directory(base);
		Util::PathsMap paths;
		for(auto key : { Util::PATH_USER_CONFIG, Util::PATH_USER_LOCAL, Util::PATH_GLOBAL_CONFIG, Util::PATH_RESOURCES, Util::PATH_LOCALE,
				Util::PATH_DOWNLOADS, Util::PATH_FILE_LISTS, Util::PATH_HUB_LISTS })
			paths[key] = base.u8string() + PATH_SEPARATOR;
		Util::initialize(paths);
		SettingsManager::newInstance();
		source = (base / "source.txt").u8string();
		File(source, File::WRITE, File::CREATE | File::TRUNCATE).write("private bytes");
		auto file = pft::PrivateFile::openSource(source);
		auto h = file->hashes(file->size(), [] { return false; });
		metadata.id = pft::randomHex(16);
		metadata.name = "received.txt";
		metadata.size = file->size();
		metadata.sha256 = h.sha256;
		metadata.tth = h.tth;
	}

	void TearDown() override {
		SettingsManager::deleteInstance();
		// This fixture created base itself with an unpredictable basename.
		ASSERT_EQ(std::filesystem::current_path() / "build", base.parent_path());
		ASSERT_EQ(0u, base.filename().string().find("dcpp-private-test-"));
		std::error_code ec;
		std::filesystem::remove_all(base, ec);
		EXPECT_FALSE(ec) << ec.message();
		if(!otherVolume.empty()) {
			ASSERT_TRUE(std::filesystem::equivalent(std::filesystem::temp_directory_path(), otherVolume.parent_path()));
			ASSERT_EQ(0u, otherVolume.filename().string().find("dcpp-private-test-"));
			std::filesystem::remove_all(otherVolume, ec);
			EXPECT_FALSE(ec) << ec.message();
		}
	}

	size_t stagedFiles() {
		size_t n = 0;
		auto root = base / "PrivateSend" / "Pending";
		if(std::filesystem::exists(root))
			for(const auto& entry : std::filesystem::recursive_directory_iterator(root))
				if(entry.is_regular_file())
					++n;
		return n;
	}
};
}

TEST_F(PrivateStorage, stable_source_denies_mutation_and_bounds_reads) {
	auto file = pft::PrivateFile::openSource(source);
	char bytes[32] {};
	EXPECT_EQ(13u, file->read(0, bytes, 13));
	EXPECT_EQ("private bytes", std::string(bytes, 13));
	EXPECT_THROW(file->read(1, bytes, 13), FileException);
	EXPECT_THROW(File(source, File::WRITE, File::OPEN), FileException);
	EXPECT_FALSE(DeleteFileW(Text::toT(source).c_str()));
	EXPECT_THROW(file->hashes(13, [] { return true; }), FileException);
	EXPECT_THROW(pft::PrivateFile::openSource(source + ":Zone.Identifier"), FileException);
}

TEST_F(PrivateStorage, destination_is_sequential_and_cannot_publish_unverified_data) {
	{
		auto file = pft::PrivateFile::createDestination(metadata);
		EXPECT_THROW(file->publish(), FileException);
		EXPECT_THROW(file->write(1, "x", 1), FileException);
		file->write(0, "private bytes", 13);
		EXPECT_THROW(file->write(0, "x", 1), FileException);
		EXPECT_THROW(file->write(13, "x", 1), FileException);
		EXPECT_THROW(file->publish(), FileException);
		EXPECT_EQ(1u, stagedFiles());
	}

	EXPECT_EQ(0u, stagedFiles());
	EXPECT_FALSE(std::filesystem::exists(base / "PrivateSend" / "Received" / metadata.id));
	EXPECT_EQ("private bytes", File(source, File::READ, File::OPEN).read());
}

TEST_F(PrivateStorage, bad_hash_and_cancel_never_publish) {
	for(bool cancel : { false, true }) {
		auto file = pft::PrivateFile::createDestination(metadata);
		file->write(0, "private bytes", 13);
		auto m = metadata;
		if(!cancel)
			m.sha256 = std::string(64, '0');
		EXPECT_THROW(file->verifyAndScan(m, [cancel] { return cancel; }), FileException);
		EXPECT_THROW(file->publish(), FileException);
	}

	EXPECT_EQ(0u, stagedFiles());
}

TEST_F(PrivateStorage, real_attachment_policy_provenance_and_no_overwrite) {
	auto file = pft::PrivateFile::createDestination(metadata);
	file->write(0, "private bytes", 13);
	ASSERT_NO_THROW(file->verifyAndScan(metadata, [] { return false; }));
	EXPECT_THROW(file->write(13, "", 0), FileException);
	auto saved = file->publish();
	EXPECT_EQ((base / metadata.name).u8string(), saved);
	EXPECT_EQ("private bytes", File(saved, File::READ, File::OPEN).read());
	EXPECT_NE(std::string::npos, File(saved + ":Zone.Identifier", File::READ, File::OPEN).read().find("ZoneId=3"));
	EXPECT_TRUE(pft::isPrivatePath(saved));
	auto alias = (base / "hardlink.txt").u8string();
	ASSERT_TRUE(CreateHardLinkW(Text::toT(alias).c_str(), Text::toT(saved).c_str(), nullptr));
	EXPECT_TRUE(pft::isPrivatePath(alias));
	EXPECT_THROW(pft::PrivateFile::createDestination(metadata), FileException);
	EXPECT_EQ("private bytes", File(saved, File::READ, File::OPEN).read());
	EXPECT_EQ(0u, stagedFiles());
}

TEST_F(PrivateStorage, configured_download_directory_and_custom_filename) {
	const auto downloads = base / "downloads";
	const auto chosen = base / "chosen";
	std::filesystem::create_directory(downloads);
	std::filesystem::create_directory(chosen);
	// The setting need not end with a separator.
	SettingsManager::getInstance()->set(SettingsManager::DOWNLOAD_DIRECTORY, downloads.u8string());
	EXPECT_EQ((downloads / metadata.name).u8string(), pft::defaultDownloadPath(metadata.name));
	{
		auto file = pft::PrivateFile::createDestination(metadata);
		EXPECT_EQ((downloads / metadata.name).u8string(), file->targetPath());
	}
	EXPECT_TRUE(std::filesystem::is_directory(downloads));
	const auto target = (chosen / "renamed file.txt").u8string();
	auto file = pft::PrivateFile::createDestination(metadata, target);
	file->write(0, "private bytes", 13);
	file->verifyAndScan(metadata, [] { return false; });
	EXPECT_FALSE(std::filesystem::exists(target));
	EXPECT_EQ(target, file->publish());
	EXPECT_TRUE(pft::isPrivatePath(target));
	EXPECT_EQ("private bytes", File(target, File::READ, File::OPEN).read());
	EXPECT_FALSE(std::filesystem::exists(downloads / metadata.name));
}

TEST_F(PrivateStorage, destination_collision_after_accept_preserves_existing_file) {
	const auto chosen = base / "chosen";
	std::filesystem::create_directory(chosen);
	const auto target = (chosen / "existing.txt").u8string();
	{
		auto file = pft::PrivateFile::createDestination(metadata, target);
		file->write(0, "private bytes", 13);
		file->verifyAndScan(metadata, [] { return false; });
		File(target, File::WRITE, File::CREATE | File::TRUNCATE).write("keep me");
		EXPECT_THROW(file->publish(), FileException);
	}
	EXPECT_EQ("keep me", File(target, File::READ, File::OPEN).read());
	EXPECT_TRUE(std::filesystem::is_directory(chosen));
	EXPECT_EQ(0u, stagedFiles());
}

TEST_F(PrivateStorage, selected_paths_reject_streams_reserved_names_and_staging) {
	for(const auto& name : { "NUL.txt", "file:stream", "trailing.", "trailing " })
		EXPECT_THROW(pft::PrivateFile::createDestination(metadata, (base / name).u8string()), FileException);
	EXPECT_THROW(pft::PrivateFile::createDestination(metadata, source), FileException);
	const auto staging = base / (".DCPlusPlus-PrivateSend-" + pft::randomHex(16));
	std::filesystem::create_directory(staging);
	EXPECT_TRUE(pft::isPrivatePath(staging.u8string()));
	EXPECT_TRUE(pft::isPrivatePath((staging / "unfinished.txt").u8string()));
	EXPECT_THROW(pft::PrivateFile::createDestination(metadata, (staging / "bad.txt").u8string()), FileException);
}

TEST_F(PrivateStorage, save_to_another_volume_preserves_provenance_and_cleans_staging) {
	const auto temporary = std::filesystem::temp_directory_path();
	if(Util::stricmp(temporary.root_name().u8string(), base.root_name().u8string()) == 0)
		GTEST_SKIP() << "A second local volume is not available through the temporary directory";
	otherVolume = temporary / ("dcpp-private-test-" + pft::randomHex(16));
	ASSERT_TRUE(std::filesystem::create_directory(otherVolume));
	{
		auto file = pft::PrivateFile::createDestination(metadata, (otherVolume / "interrupted.txt").u8string());
		file->write(0, "private bytes", 13);
		EXPECT_THROW(file->verifyAndScan(metadata, [&] {
			for(const auto& entry : std::filesystem::directory_iterator(otherVolume))
				if(entry.is_directory()) return true; // Cancel in CopyFileEx's progress callback.
			return false;
		}), FileException);
		EXPECT_THROW(file->publish(), FileException);
	}
	EXPECT_TRUE(std::filesystem::is_empty(otherVolume));
	EXPECT_EQ(0u, stagedFiles());
	for(bool cancel : { true, false }) {
		const auto target = (otherVolume / (cancel ? "cancelled.txt" : "saved.txt")).u8string();
		{
			std::shared_ptr<pft::PrivateFile> file;
			ASSERT_NO_THROW(file = pft::PrivateFile::createDestination(metadata, target));
			file->write(0, "private bytes", 13);
			// Complete preparation on the other drive, then exercise cancellation
			// cleanup as well as the final rename.
			ASSERT_NO_THROW(file->verifyAndScan(metadata, [] { return false; }));
			EXPECT_FALSE(std::filesystem::exists(target));
			if(!cancel) {
				EXPECT_EQ(target, file->publish());
				EXPECT_EQ("private bytes", File(target, File::READ, File::OPEN).read());
				EXPECT_TRUE(pft::isPrivatePath(target));
				EXPECT_NE(std::string::npos, File(target + ":Zone.Identifier", File::READ, File::OPEN).read().find("ZoneId=3"));
			}
		}
		EXPECT_EQ(!cancel, std::filesystem::exists(target));
		EXPECT_EQ(0u, stagedFiles());
		for(const auto& entry : std::filesystem::directory_iterator(otherVolume))
			EXPECT_FALSE(entry.is_directory());
	}
}

TEST_F(PrivateStorage, private_tree_aliases_and_names_fail_closed) {
	EXPECT_TRUE(pft::isPrivatePath((base / "PrivateSend" / "Received" / "x").u8string()));
	EXPECT_TRUE(pft::isPrivatePath((base / "other" / ".." / "PrivateSend" / "x").u8string()));
	EXPECT_FALSE(pft::isPrivatePath(source));
	for(const auto& name : { "../escape", "NUL.txt", "file:stream" }) {
		auto m = metadata;
		m.name = name;
		EXPECT_THROW(pft::PrivateFile::createDestination(m), FileException);
	}
}

TEST_F(PrivateStorage, startup_removes_only_partial_files) {
	auto old = base / "PrivateSend" / "Pending" / pft::randomHex(16);
	auto saved = base / "PrivateSend" / "Received";
	std::filesystem::create_directories(old);
	std::filesystem::create_directories(saved);
	File((old / "old.txt").u8string(), File::WRITE, File::CREATE | File::TRUNCATE).write("partial");
	File((saved / "keep.txt").u8string(), File::WRITE, File::CREATE | File::TRUNCATE).write("complete");
	EXPECT_TRUE(pft::purgeUnfinished());
	EXPECT_FALSE(std::filesystem::exists(old));
	EXPECT_EQ("complete", File((saved / "keep.txt").u8string(), File::READ, File::OPEN).read());
}
#endif
