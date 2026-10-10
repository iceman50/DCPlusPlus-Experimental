/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <dcpp/Archive.h>
#include <dcpp/File.h>
#include <dcpp/FileReader.h>
#include <dcpp/SQLiteDB.h>
#include <dcpp/Text.h>
#include <dcpp/Util.h>

#include <chrono>

using namespace dcpp;

#ifdef _WIN32

#include <winioctl.h>

namespace {

struct LongPathFixture {
	LongPathFixture() {
		auto current = Util::getTempPath() + "dcpp-long-path-" +
			std::to_string(::GetCurrentProcessId()) + '-' + std::to_string(::GetTickCount64());
		directories.push_back(current);

		while(current.size() < MAX_PATH + 32) {
			current += PATH_SEPARATOR_STR "segment-\xE6\xB5\x8B\xE8\xAF\x95-xxxxxxxxxxxxxxxxxxxxxxxx";
			directories.push_back(current);
		}

		directory = current;
		file = current + PATH_SEPARATOR_STR "hash-me.txt";
	}

	~LongPathFixture() {
		File::deleteFile(file);
		for(auto i = directories.rbegin(); i != directories.rend(); ++i) {
			::RemoveDirectory(File::toNativePath(*i).c_str());
		}
	}

	string file;
	string directory;
	StringList directories;
};

}

TEST(FileTest, nativePathUsesExtendedSyntaxForLongAbsolutePaths) {
	const string tail(MAX_PATH, 'x');
	const auto drivePath = "C:\\" + tail;
	const auto uncPath = "\\\\server\\share\\" + tail;

	EXPECT_EQ(L"\\\\?\\" + Text::toT(drivePath), File::toNativePath(drivePath));
	EXPECT_EQ(L"\\\\?\\UNC\\server\\share\\" + Text::toT(tail), File::toNativePath(uncPath));
	EXPECT_EQ(Text::toT("relative.txt"), File::toNativePath("relative.txt"));
}

TEST(FileTest, readsAndEnumeratesLongPaths) {
	LongPathFixture fixture;
	const string expected = "long-path hashing data";

	File::ensureDirectory(fixture.file);
	File(fixture.file, File::WRITE, File::CREATE | File::TRUNCATE).write(expected);

	EXPECT_TRUE(File::isDirectory(fixture.directory));
	EXPECT_EQ(static_cast<int64_t>(expected.size()), File::getSize(fixture.file));
	FileFindIter found(fixture.file);
	ASSERT_TRUE(found != FileFindIter());
	EXPECT_EQ("hash-me.txt", found->getFileName());

	string actual;
	const auto bytes = FileReader(true).read(fixture.file, [&](const void* data, size_t size) {
		actual.append(static_cast<const char*>(data), size);
		return true;
	});

	EXPECT_EQ(expected.size(), bytes);
	EXPECT_EQ(expected, actual);
}

TEST(FileTest, opensLongPathsInIntegratedFileConsumers) {
	LongPathFixture fixture;
	File::ensureDirectory(fixture.file);

	const auto database = fixture.directory + PATH_SEPARATOR_STR "LongPath.sqlite3";
	{
		SQLiteDB db(database);
		db.execute("CREATE TABLE test(value INTEGER); INSERT INTO test VALUES(1);");
		auto statement = db.prepare("SELECT value FROM test");
		ASSERT_TRUE(statement.step());
		EXPECT_EQ(1, statement.columnInt(0));
	}
	File::deleteFile(database);
	File::deleteFile(database + "-shm");
	File::deleteFile(database + "-wal");

	const auto archive = fixture.directory + PATH_SEPARATOR_STR "test.zip";
	const auto outputDirectory = fixture.directory + PATH_SEPARATOR_STR "archive-out" PATH_SEPARATOR_STR;
	const auto extracted = outputDirectory + "gtest.h";
	File::copyFile("test/data/gtest_h.zip", archive);
	Archive(archive).extract(outputDirectory);
	EXPECT_GT(File::getSize(extracted), 0);

	File::deleteFile(extracted);
	File::deleteFile(archive);
	::RemoveDirectory(File::toNativePath(outputDirectory.substr(0, outputDirectory.size() - 1)).c_str());
}

TEST(FileTest, resizeRejectsNegativeSizeWithoutTruncatingData) {
	LongPathFixture fixture;
	File::ensureDirectory(fixture.file);
	File file(fixture.file, File::RW, File::CREATE | File::TRUNCATE);
	file.write("keep this data");
	file.setPos(4);
	EXPECT_THROW(file.setSize(-1), FileException);
	EXPECT_EQ(4, file.getPos());
	EXPECT_EQ("keep this data", file.read());
}

TEST(FileTest, failedResizePreservesPositionAndReportsWindowsError) {
	LongPathFixture fixture;
	File::ensureDirectory(fixture.file);
	File(fixture.file, File::WRITE, File::CREATE | File::TRUNCATE).write("keep this data");
	File file(fixture.file, File::READ, File::OPEN);
	file.setPos(4);
	try {
		file.setSize(100);
		FAIL() << "Resizing a read-only handle should fail";
	} catch(const FileException& e) {
		EXPECT_NE(string::npos, e.getError().find("SetEndOfFile"));
		EXPECT_NE(string::npos, e.getError().find("Windows error 5"));
	}
	EXPECT_EQ(4, file.getPos());
	EXPECT_EQ("keep this data", file.read());
}

namespace {
class AllocationTestFile : public File {
public:
	explicit AllocationTestFile(const string& path) : File(path, File::RW, File::CREATE | File::SHARED) { }
	bool makeSparse() {
		DWORD returned = 0;
		return ::DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != 0;
	}
};

void checkLargeFileSizes(File& file, const string& path) {
	// Exact search-result sizes, plus DWORD boundaries and a file above 20 GiB.
	const int64_t sizes[] = { 0xffffffffLL, 0x100000000LL, 13294717994LL, 12416007017LL,
		13208887396LL, 12840916094LL, 14645412583LL, 13455705289LL, 11587322376LL,
		12838835076LL, 15245217193LL, 24LL * 1024 * 1024 * 1024 + 123 };
	file.write("keep this data");
	for(auto size: sizes) {
		SCOPED_TRACE(size);
		file.setPos(4);
		file.setSize(size);
		EXPECT_EQ(4, file.getPos());
		EXPECT_EQ(size, file.getSize());
		EXPECT_EQ(size, File::getSize(path));
		// Segments use separate shared handles to the same preallocated file.
		File segment(path, File::WRITE, File::OPEN | File::SHARED);
		segment.setPos(size - 1);
		EXPECT_EQ(size - 1, segment.getPos());
	}
	file.setPos(0);
	EXPECT_EQ("keep this data", file.read(14));
}
}

TEST(FileTest, resizesLargeSparseFilesWithLongDownloadNames) {
	LongPathFixture fixture;
	fixture.file = fixture.directory + PATH_SEPARATOR_STR
		"Les.Soprano-The.Sopranos.S01E01.MULTi.TRUEFRENCH.1080p.BluRay.REMUX.AVC.DTS-TheTruth.mkv."
		"X25BREV6HMZXRKYZQXYSBK3J7GK3PQ5JZ5OMITQ.dctmp";
	File::ensureDirectory(fixture.file);
	AllocationTestFile file(fixture.file);
	if(!file.makeSparse()) GTEST_SKIP() << "Test filesystem does not support sparse files";
	checkLargeFileSizes(file, fixture.file);
	file.setPos(file.getSize() - 1);
	file.write("x");
	file.setEndPos(-1);
	EXPECT_EQ("x", file.read(1));
}

// Explicit opt-in: reserves up to 24 GiB temporarily. Normal test runs use sparse files.
TEST(FileTest, DISABLED_reservesLargeDownloadFilesWithoutSparseAllocation) {
	LongPathFixture fixture;
	File::ensureDirectory(fixture.file);
	ULARGE_INTEGER available;
	ASSERT_TRUE(::GetDiskFreeSpaceEx(File::toNativePath(fixture.directory).c_str(), &available, nullptr, nullptr));
	if(available.QuadPart < 26ULL * 1024 * 1024 * 1024) GTEST_SKIP() << "Requires 26 GiB free";
	AllocationTestFile file(fixture.file);
	checkLargeFileSizes(file, fixture.file);
}

#endif

namespace {

class ShortReadFile : public File {
public:
	ShortReadFile(const string& path, size_t maximumRead) : File(path, File::READ, File::OPEN | File::SHARED), maximumRead(maximumRead) { }

	size_t read(void* buffer, size_t& bytes) override {
		bytes = std::min(bytes, maximumRead);
		return File::read(buffer, bytes);
	}

private:
	size_t maximumRead;
};

string readerTestPath(const string& name) {
	return Util::getTempPath() + "dcpp-filereader-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + '-' + name;
}

}

TEST(FileReaderTest, fills_requested_blocks_across_short_operating_system_reads) {
	const auto path = readerTestPath("short-reads.bin");
	const size_t blockSize = 1024 * 1024;
	string expected(blockSize + 37, '\0');
	for(size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<char>((i * 31U + 7U) & 0xffU);
	File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(expected);

	ShortReadFile input(path, 4093);
	vector<size_t> chunks;
	string actual;
	const auto bytes = FileReader(false, blockSize).read(input, [&](const void* data, size_t size) {
		chunks.push_back(size);
		actual.append(static_cast<const char*>(data), size);
		return true;
	});

	EXPECT_EQ(expected.size(), bytes);
	EXPECT_EQ(expected, actual);
	ASSERT_EQ(2U, chunks.size());
	EXPECT_EQ(blockSize, chunks[0]);
	EXPECT_EQ(37U, chunks[1]);
	File::deleteFile(path);
}

TEST(FileReaderTest, stops_callbacks_immediately_after_cancellation) {
	const auto path = readerTestPath("cancel.bin");
	const string data(3 * 1024 * 1024, 'x');
	File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(data);

	size_t callbacks = 0;
	FileReader(true).read(path, [&](const void*, size_t) {
		++callbacks;
		return false;
	});

	EXPECT_EQ(1U, callbacks);
	File::deleteFile(path);
}
