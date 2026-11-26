/* Copyright (C) 2026 iceman50 */

#include "testbase.h"

#include <dcpp/core/io/compress/PackageCache.h>
#include <dcpp/core/io/File.h>
#include <dcpp/util/Util.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>
#include <zlib.h>

using namespace dcpp;

namespace {

namespace fs = std::filesystem;

/** Write a ZIP archive of stored (uncompressed) files. */
void writeZip(const string& path, const std::vector<std::pair<string, string>>& files) {
	string bytes, central;
	auto number = [](string& out, uint32_t value, unsigned width) {
		for(unsigned i = 0; i < width; ++i) out += static_cast<char>(value >> (8 * i));
	};
	for(const auto& file: files) {
		const auto offset = static_cast<uint32_t>(bytes.size());
		const auto crc = crc32(0, reinterpret_cast<const Bytef*>(file.second.data()), static_cast<uInt>(file.second.size()));
		number(bytes, 0x04034b50, 4); number(bytes, 20, 2);
		for(unsigned i = 0; i < 4; ++i) number(bytes, 0, 2); // flags, method, time, date
		number(bytes, crc, 4); number(bytes, file.second.size(), 4); number(bytes, file.second.size(), 4);
		number(bytes, file.first.size(), 2); number(bytes, 0, 2);
		bytes += file.first; bytes += file.second;
		number(central, 0x02014b50, 4); number(central, 0x0314, 2); number(central, 20, 2);
		for(unsigned i = 0; i < 4; ++i) number(central, 0, 2);
		number(central, crc, 4); number(central, file.second.size(), 4); number(central, file.second.size(), 4);
		number(central, file.first.size(), 2);
		for(unsigned i = 0; i < 4; ++i) number(central, 0, 2); // extra, comment, disk, attributes
		number(central, 0, 4); number(central, offset, 4); central += file.first;
	}
	const auto offset = bytes.size(); bytes += central;
	number(bytes, 0x06054b50, 4); number(bytes, 0, 2); number(bytes, 0, 2);
	number(bytes, files.size(), 2); number(bytes, files.size(), 2);
	number(bytes, central.size(), 4); number(bytes, offset, 4); number(bytes, 0, 2);
	File::ensureDirectory(path);
	File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(bytes);
}

string read(const string& path) {
	return File(path, File::READ, File::OPEN).read();
}

/** The extracted package's own folder, above its "contents". */
string packageFolder(const string& contents) {
	return contents.substr(0, contents.size() - std::strlen("contents" PATH_SEPARATOR_STR));
}

bool exists(const string& path) {
	std::error_code ignored;
	return fs::exists(fs::u8path(path), ignored);
}

const PackageCache::Limits roomy { 64, 1024 * 1024, 16 * 1024 * 1024 };
const string kind = "Test";

class testpackagecache : public testing::Test {
protected:
	Util::PathsMap originalPaths;
	string root;

	void SetUp() override {
		for(int index = 0; index < Util::PATH_LAST; ++index) {
			const auto path = static_cast<Util::Paths>(index);
			originalPaths[path] = Util::getPath(path);
		}
		root = Util::getTempPath() + "dcpp-test-package-cache-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + PATH_SEPARATOR_STR;
		std::error_code ignored;
		fs::remove_all(fs::u8path(root), ignored);
		auto paths = originalPaths;
		paths[Util::PATH_USER_LOCAL] = root + "local" PATH_SEPARATOR_STR;
		Util::initialize(paths);
	}

	void TearDown() override {
		Util::initialize(originalPaths);
		std::error_code ignored;
		fs::remove_all(fs::u8path(root), ignored);
	}
};

}

TEST_F(testpackagecache, extracts_a_package_once_and_reads_it_from_its_folder)
{
	const auto package = root + "packs" PATH_SEPARATOR_STR "Sample.zip";
	writeZip(package, { { "info.xml", "<manifest/>" }, { "icons/a.ico", "icon a" } });

	const auto contents = PackageCache::extract(package, kind, roomy);
	EXPECT_EQ(0u, contents.find(PackageCache::getDirectory(kind)));
	EXPECT_EQ(0u, contents.find(root + "local" PATH_SEPARATOR_STR "Extracted" PATH_SEPARATOR_STR "Test" PATH_SEPARATOR_STR "Sample-"));
	EXPECT_EQ("<manifest/>", read(contents + "info.xml"));
	EXPECT_EQ("icon a", read(contents + "icons" PATH_SEPARATOR_STR "a.ico"));

	// Not extracted again: what is in the folder stays.
	File(contents + "info.xml", File::WRITE, File::CREATE | File::TRUNCATE).write(string("left alone"));
	EXPECT_EQ(contents, PackageCache::extract(package, kind, roomy));
	EXPECT_EQ("left alone", read(contents + "info.xml"));
}

TEST_F(testpackagecache, a_changed_package_is_extracted_again_and_replaces_its_old_copy)
{
	const auto package = root + "packs" PATH_SEPARATOR_STR "Sample.zip";
	writeZip(package, { { "info.xml", "first" } });
	const auto first = PackageCache::extract(package, kind, roomy);

	// Another size.
	writeZip(package, { { "info.xml", "second, longer" } });
	const auto second = PackageCache::extract(package, kind, roomy);
	EXPECT_NE(first, second);
	EXPECT_EQ("second, longer", read(second + "info.xml"));
	EXPECT_FALSE(exists(packageFolder(first)));

	// The same size, written later.
	const auto written = fs::last_write_time(fs::u8path(package));
	writeZip(package, { { "info.xml", "third, longer!" } });
	fs::last_write_time(fs::u8path(package), written + std::chrono::seconds(2));
	const auto third = PackageCache::extract(package, kind, roomy);
	EXPECT_NE(second, third);
	EXPECT_EQ("third, longer!", read(third + "info.xml"));
	EXPECT_FALSE(exists(packageFolder(second)));
}

TEST_F(testpackagecache, a_package_named_by_relative_and_absolute_paths_is_extracted_once)
{
	const string relative = "test/dcpp/data/out/package-cache-relative.zip";
	writeZip(relative, { { "info.xml", "relative" } });
	const auto absolute = fs::absolute(fs::u8path(relative)).u8string();

	const auto contents = PackageCache::extract(relative, kind, roomy);
	EXPECT_EQ(contents, PackageCache::extract(absolute, kind, roomy));
	File::deleteFile(relative);
}

TEST_F(testpackagecache, limits_also_apply_to_a_package_extracted_before)
{
	const auto package = root + "packs" PATH_SEPARATOR_STR "Sample.zip";
	writeZip(package, { { "info.xml", string(100, 'x') }, { "b", string(50, 'y') } });
	const auto contents = PackageCache::extract(package, kind, roomy);

	EXPECT_THROW(PackageCache::extract(package, kind, { 1, 1024, 1024 }), Exception);
	EXPECT_THROW(PackageCache::extract(package, kind, { 64, 99, 1024 }), Exception);
	EXPECT_THROW(PackageCache::extract(package, kind, { 64, 1024, 149 }), Exception);
	EXPECT_EQ(contents, PackageCache::extract(package, kind, { 2, 100, 150 }));

	// One that doesn't fit isn't left extracted.
	const auto large = root + "packs" PATH_SEPARATOR_STR "Large.zip";
	writeZip(large, { { "info.xml", string(100, 'x') } });
	EXPECT_THROW(PackageCache::extract(large, kind, { 64, 10, 1024 }), Exception);
	for(const auto& entry: fs::directory_iterator(fs::u8path(PackageCache::getDirectory(kind)))) {
		EXPECT_EQ(string::npos, entry.path().filename().u8string().find("Large")) << entry.path().u8string();
	}
}

TEST_F(testpackagecache, an_extraction_cut_short_is_done_again)
{
	const auto package = root + "packs" PATH_SEPARATOR_STR "Sample.zip";
	writeZip(package, { { "info.xml", "complete" } });
	const auto contents = PackageCache::extract(package, kind, roomy);

	// As if DC++ stopped before the note of the package's source was written.
	File::deleteFile(packageFolder(contents) + "source.txt");
	File(contents + "info.xml", File::WRITE, File::CREATE | File::TRUNCATE).write(string("partial"));
	EXPECT_EQ(contents, PackageCache::extract(package, kind, roomy));
	EXPECT_EQ("complete", read(contents + "info.xml"));
}

TEST_F(testpackagecache, what_is_not_a_package_is_refused_and_not_kept)
{
	const auto package = root + "packs" PATH_SEPARATOR_STR "Broken.zip";
	File::ensureDirectory(package);
	File(package, File::WRITE, File::CREATE | File::TRUNCATE).write(string("not a zip"));
	EXPECT_THROW(PackageCache::extract(package, kind, roomy), Exception);
	EXPECT_THROW(PackageCache::extract(root + "packs" PATH_SEPARATOR_STR "Missing.zip", kind, roomy), Exception);
	std::error_code ignored;
	EXPECT_TRUE(fs::is_empty(fs::u8path(PackageCache::getDirectory(kind)), ignored) || !exists(PackageCache::getDirectory(kind)));
}

TEST_F(testpackagecache, cleaning_removes_copies_of_packages_that_are_gone_or_changed)
{
	const auto kept = root + "packs" PATH_SEPARATOR_STR "Kept.zip";
	const auto removed = root + "packs" PATH_SEPARATOR_STR "Removed.zip";
	const auto changed = root + "packs" PATH_SEPARATOR_STR "Changed.zip";
	writeZip(kept, { { "info.xml", "kept" } });
	writeZip(removed, { { "info.xml", "removed" } });
	writeZip(changed, { { "info.xml", "changed" } });
	const auto keptCopy = PackageCache::extract(kept, kind, roomy);
	const auto removedCopy = PackageCache::extract(removed, kind, roomy);
	const auto changedCopy = PackageCache::extract(changed, kind, roomy);
	const auto stray = PackageCache::getDirectory(kind) + "stray" PATH_SEPARATOR_STR;
	File::ensureDirectory(stray);

	File::deleteFile(removed);
	writeZip(changed, { { "info.xml", "changed since" } });
	PackageCache::clean(kind);

	EXPECT_TRUE(exists(keptCopy + "info.xml"));
	EXPECT_FALSE(exists(packageFolder(removedCopy)));
	EXPECT_FALSE(exists(packageFolder(changedCopy)));
	EXPECT_FALSE(exists(stray));
	EXPECT_EQ(keptCopy, PackageCache::extract(kept, kind, roomy));
}
