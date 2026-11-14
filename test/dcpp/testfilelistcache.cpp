/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"
#include <filesystem>
#include <cstdlib>
#include <clocale>
#include <dcpp/filelist/DirectoryListing.h>
#include <dcpp/core/io/File.h>
#include <dcpp/core/io/stream/FilteredFile.h>
#include <dcpp/core/io/compress/ZstdUtils.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/user/User.h>
#include <sqlite3.h>

using namespace dcpp;
namespace {
class UserCollation {
public:
	UserCollation() : previous(std::setlocale(LC_COLLATE, nullptr)) { std::setlocale(LC_COLLATE, ""); }
	~UserCollation() { std::setlocale(LC_COLLATE, previous.c_str()); }
private:
	string previous;
};

class FilelistCacheTest : public ::testing::Test {
public:
	void SetUp() override {
		SettingsManager::newInstance();
		SettingsManager::getInstance()->set(SettingsManager::FILELIST_CACHE, true);
		folder = std::filesystem::temp_directory_path() / ("dcpp-flcache-" + CID::generate().toBase32());
		std::filesystem::create_directory(folder);
		path = (folder / "files.xml").string();
		user = HintedUser(UserPtr(new User(CID())), "adc://test.invalid");
	}
	void TearDown() override { std::filesystem::remove_all(folder); SettingsManager::deleteInstance(); }
	void write(const string& xml) { File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(xml); }
	string file(const string& name, int size = 1) {
		return "<File Name=\"" + name + "\" Size=\"" + std::to_string(size) + "\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Date=\"1700000000\"/>";
	}
	std::filesystem::path folder;
	string path;
	HintedUser user;
};
}

TEST_F(FilelistCacheTest, builds_multiple_chunks_reopens_and_loads_only_requested_files) {
	string xml = "<FileListing Base=\"/\"><Directory Name=\"Big\">";
	for(int i = 0; i < 12000; ++i) xml += file("file" + std::to_string(i));
	xml += "</Directory><Directory Name=\"Small\">" + file("small", 7) + "</Directory></FileListing>";
	write(xml);
	for(int pass = 0; pass < 2; ++pass) {
		DirectoryListing list(user);
		const auto originalRoot = list.getRoot();
		list.loadFile(path);
		EXPECT_EQ(originalRoot, list.getRoot());
		ASSERT_TRUE(list.usesCache());
		EXPECT_EQ(12001u, list.getTotalFileCount());
		EXPECT_EQ(12007, list.getTotalSize());
		auto big = *list.getRoot()->directories.begin();
		EXPECT_EQ("Big", big->getName());
		EXPECT_TRUE(big->files.empty());
		big->ensureFiles();
		EXPECT_EQ(12000u, big->files.size());
		EXPECT_EQ(1700000000, (*big->files.begin())->getRemoteDate());
		EXPECT_EQ(big, (*big->files.begin())->getParent());
		big->releaseFiles();
		EXPECT_TRUE(big->files.empty());
		EXPECT_EQ(12000u, big->getFileCount());
		big->ensureFiles();
		EXPECT_EQ(12000u, big->files.size());
	}
}

TEST_F(FilelistCacheTest, partial_updates_survive_releasing_and_saving) {
	write("<FileListing Base=\"/\"><Directory Name=\"Share\">" + file("first") + "</Directory></FileListing>");
	DirectoryListing list(user); list.loadFile(path);
	list.updateXML("<FileListing Base=\"/Share/\">" + file("first", 20) + file("second", 3) + "</FileListing>");
	auto dir = *list.getRoot()->directories.begin();
	dir->releaseFiles();
	EXPECT_EQ(2u, dir->files.size());
	EXPECT_EQ(23, list.getTotalSize());
	list.save((folder / "saved.xml").string());
	DirectoryListing saved(user); saved.loadFile((folder / "saved.xml").string());
	EXPECT_EQ(2u, saved.getTotalFileCount());
	EXPECT_EQ(23, saved.getTotalSize());
}

TEST_F(FilelistCacheTest, invalidates_stale_and_broken_indexes) {
	write("<FileListing Base=\"/\">" + file("old") + "</FileListing>");
	{ DirectoryListing list(user); list.loadFile(path); ASSERT_TRUE(list.usesCache()); }
	write("<FileListing Base=\"/\">" + file("replacement", 123) + "</FileListing>");
	{ DirectoryListing list(user); list.loadFile(path); EXPECT_EQ(123, list.getTotalSize()); }
	File(path + ".dcfl", File::WRITE, File::OPEN | File::TRUNCATE).write("broken index");
	{ DirectoryListing list(user); list.loadFile(path); EXPECT_EQ(123, list.getTotalSize()); }
}

TEST_F(FilelistCacheTest, rejects_duplicates_across_chunk_boundaries) {
	string xml = "<FileListing Base=\"/\">";
	for(int i = 0; i < 6000; ++i) xml += file("file" + std::to_string(i));
	write(xml + file("file0") + "</FileListing>");
	DirectoryListing list(user);
	EXPECT_THROW(list.loadFile(path), Exception);
}

TEST_F(FilelistCacheTest, preserves_distinct_names_with_equal_locale_collation) {
	UserCollation locale;
#ifdef _WIN32
	ASSERT_NE(nullptr, std::setlocale(LC_COLLATE, "English_United States.1252"));
#endif
	const string dotted = "Name.";
	const string unicodeDotted = "Name\xe2\x80\xa4"; // U+2024 ONE DOT LEADER
#ifdef _WIN32
	ASSERT_EQ(0, compare(dotted, unicodeDotted));
#endif
	write("<FileListing Base=\"/\"><Directory Name=\"" + dotted + "\">" +
		file(dotted, 3) + file(unicodeDotted, 7) + "</Directory><Directory Name=\"" +
		unicodeDotted + "\">" + file("other", 11) + "</Directory></FileListing>");
	for(bool cached: {false, true}) {
		SettingsManager::getInstance()->set(SettingsManager::FILELIST_CACHE, cached);
		for(int pass = 0; pass < 2; ++pass) {
			DirectoryListing list(user);
			ASSERT_NO_THROW(list.loadFile(path));
			EXPECT_EQ(cached, list.usesCache());
			ASSERT_EQ(2u, list.getRoot()->directories.size());
			EXPECT_EQ(3u, list.getTotalFileCount());
			EXPECT_EQ(21, list.getTotalSize());
			for(auto dir: list.getRoot()->directories) {
				dir->ensureFiles();
				if(dir->getName() == dotted) {
					ASSERT_EQ(2u, dir->files.size());
					std::set<string> names;
					for(auto entry: dir->files) names.insert(entry->getName());
					EXPECT_EQ((std::set<string>{dotted, unicodeDotted}), names);
				} else {
					EXPECT_EQ(unicodeDotted, dir->getName());
					ASSERT_EQ(1u, dir->files.size());
					EXPECT_EQ("other", (*dir->files.begin())->getName());
				}
			}
		}
	}
}

TEST_F(FilelistCacheTest, still_rejects_identical_file_and_directory_names) {
	for(bool cached: {false, true}) {
		SettingsManager::getInstance()->set(SettingsManager::FILELIST_CACHE, cached);
		for(const auto& entries: {file("same") + file("same"),
			string("<Directory Name=\"same\"/><Directory Name=\"same\"/>")})
		{
			write("<FileListing Base=\"/\">" + entries + "</FileListing>");
			DirectoryListing list(user);
			EXPECT_THROW(list.loadFile(path), Exception);
		}
	}
}

TEST_F(FilelistCacheTest, opens_zstd_lists_and_preserves_source) {
	const string xml = "<FileListing Base=\"/\">" + file("zstd", 30) + "</FileListing>";
	{
		File output(path + ".zst", File::WRITE, File::CREATE | File::TRUNCATE);
		FilteredOutputStream<ZstdFilter, false> compressor(&output);
		compressor.write(xml); compressor.flush();
	}
	DirectoryListing list(user); list.loadFile(path);
	EXPECT_TRUE(list.usesCache());
	EXPECT_EQ(30, list.getTotalSize());
	EXPECT_NE(-1, File::getSize(path + ".zst"));
	list.getRoot()->ensureFiles();
	EXPECT_EQ("zstd", (*list.getRoot()->files.begin())->getName());
}

TEST_F(FilelistCacheTest, corrupt_chunk_does_not_expose_partial_directory) {
	write("<FileListing Base=\"/\">" + file("first") + "</FileListing>");
	{ DirectoryListing list(user); list.loadFile(path); ASSERT_TRUE(list.usesCache()); }
	sqlite3* db = nullptr;
	ASSERT_EQ(SQLITE_OK, sqlite3_open((path + ".dcfl").c_str(), &db));
	EXPECT_EQ(SQLITE_OK, sqlite3_exec(db, "UPDATE chunks SET data=x'00010203'", nullptr, nullptr, nullptr));
	sqlite3_close(db);
	DirectoryListing list(user); list.loadFile(path);
	EXPECT_THROW(list.getRoot()->ensureFiles(), Exception);
	EXPECT_TRUE(list.getRoot()->files.empty());
	EXPECT_FALSE(list.getRoot()->areFilesLoaded());
}

TEST_F(FilelistCacheTest, honors_changed_xml_size_limit_on_cache_reuse) {
	string xml = "<FileListing Base=\"/\">";
	for(int i = 0; i < 12000; ++i) xml += file("file" + std::to_string(i));
	write(xml + "</FileListing>");
	{ DirectoryListing list(user); list.loadFile(path); ASSERT_TRUE(list.usesCache()); }
	SettingsManager::getInstance()->set(SettingsManager::MAX_FILELIST_SIZE, 1);
	DirectoryListing list(user);
	EXPECT_THROW(list.loadFile(path), Exception);
}

// Optional integration check for downloaded lists; all generated files stay in
// the fixture directory so the original download is never changed.
TEST_F(FilelistCacheTest, external_download_round_trip) {
	const auto source = std::getenv("DCPP_TEST_FILELIST");
	if(!source) GTEST_SKIP() << "Set DCPP_TEST_FILELIST to a downloaded file list";
	UserCollation locale;
	const auto input = std::filesystem::u8path(source);
	const auto copy = folder / input.filename();
	std::filesystem::copy_file(input, copy);
	for(int pass = 0; pass < 2; ++pass) {
		DirectoryListing list(user);
		ASSERT_NO_THROW(list.loadFile(copy.u8string()));
		ASSERT_TRUE(list.usesCache());
		std::function<void(DirectoryListing::Directory*)> check = [&](DirectoryListing::Directory* dir) {
			dir->ensureFiles();
			dir->releaseFiles();
			for(auto child: dir->directories) check(child);
		};
		EXPECT_NO_THROW(check(list.getRoot()));
	}
}
