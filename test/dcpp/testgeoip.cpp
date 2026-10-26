/* Copyright (C) 2026 iceman50 */

#include "testbase.h"

#include <dcpp/geo/GeoIP.h>
#include <dcpp/user/CID.h>
#include <dcpp/system/File.h>
#include <dcpp/text/Text.h>
#include <filesystem>
#include <fstream>
#include <zlib.h>

namespace {

const char* fixture = "test/dcpp/data/geoip/GeoIP2-Country-Test.mmdb";

struct Files {
	std::filesystem::path dir = std::filesystem::temp_directory_path() / ("dcpp-geoip-" + dcpp::CID::generate().toBase32());

	Files() { std::filesystem::create_directories(dir); }

	~Files() {
		std::error_code e;
		std::filesystem::remove_all(dir, e);
	}

	std::string path(const wchar_t* name) {
#ifdef _WIN32
		return dcpp::Text::fromT((dir / name).native());
#else
		return (dir / name).string();
#endif
	}
};

std::string read(const std::string& path) {
	return dcpp::File(path, dcpp::File::READ, dcpp::File::OPEN).read();
}

void write(const std::string& path, const std::string& value) {
	dcpp::File file(path, dcpp::File::WRITE, dcpp::File::CREATE | dcpp::File::TRUNCATE);
	file.write(value);
}

std::string entry(const std::string& name, const std::string& value, char type = '0') {
	std::string header(512, '\0');
	std::copy(name.begin(), name.end(), header.begin());
	snprintf(&header[124], 12, "%011llo", static_cast<unsigned long long>(value.size()));
	header[156] = type;
	std::fill(header.begin() + 148, header.begin() + 156, ' ');
	unsigned checksum = 0;
	for(unsigned char c : header)
		checksum += c;
	snprintf(&header[148], 7, "%06o", checksum);

	return header + value + std::string((512 - value.size() % 512) % 512, '\0');
}

void gzip(const std::string& path, const std::string& data) {
	z_stream stream {};
	ASSERT_EQ(Z_OK, deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY));
	std::string compressed(deflateBound(&stream, static_cast<uLong>(data.size())), '\0');
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
	stream.avail_in = static_cast<uInt>(data.size());
	stream.next_out = reinterpret_cast<Bytef*>(&compressed[0]);
	stream.avail_out = static_cast<uInt>(compressed.size());
	ASSERT_EQ(Z_STREAM_END, deflate(&stream, Z_FINISH));
	compressed.resize(stream.total_out);
	deflateEnd(&stream);
	write(path, compressed);
}
}

TEST(GeoIP, country_codes_ipv4_ipv6_and_unicode_paths) {
	Files files;
	auto path = files.path(L"countries-\u00e9\u4e16.mmdb");
	write(path, read(fixture));
	dcpp::GeoIP database(path);
	ASSERT_NE(0u, database.buildEpoch());
	auto gb = database.lookup("81.2.69.160");
	EXPECT_EQ("GB", gb.code);
	EXPECT_EQ("United Kingdom", gb.englishName);
	EXPECT_EQ("EU", gb.continent);
	EXPECT_EQ("GB United Kingdom EU", gb.format("%[2code] %[engname] %[continent]"));
	EXPECT_EQ("JP", database.lookup("2001:218::").code);
	EXPECT_EQ("GB", database.lookup("::ffff:81.2.69.160").code);
	for(const auto* ip : { "", "not-an-ip", "localhost", "127.0.0.1", "10.0.0.1", "::1", "fe80::1%1", "1.2.3.999" })
		EXPECT_TRUE(database.lookup(ip).code.empty()) << ip;
	EXPECT_TRUE(database.lookup(std::string("81.2.69.160\0junk", 15)).code.empty());
}

TEST(GeoIP, invalid_updates_preserve_database_and_owned_results) {
	Files files;
	auto live = files.path(L"live.mmdb"), staging = files.path(L"staging.mmdb");
	write(live, read(fixture));
	dcpp::GeoIP database(live);
	const auto result = database.lookup("81.2.69.160");
	write(staging, "<html>download denied</html>");
	EXPECT_THROW(database.install(staging), dcpp::Exception);
	EXPECT_EQ(read(fixture), read(live));
	EXPECT_EQ("GB", database.lookup("81.2.69.160").code);
	write(staging, read(fixture));
	EXPECT_NO_THROW(database.install(staging));
	EXPECT_EQ("GB", result.code);
	EXPECT_EQ("JP", database.lookup("2001:218::").code);
	EXPECT_THROW(database.install(files.path(L"missing.mmdb")), dcpp::Exception);
	EXPECT_EQ("GB", database.lookup("81.2.69.160").code);
	auto wrongType = read(fixture);
	const auto type = wrongType.find("GeoIP2-Country");
	ASSERT_NE(std::string::npos, type);
	wrongType.replace(type, 14, "GeoIP2-Unknown");
	write(staging, wrongType);
	EXPECT_THROW(database.install(staging), dcpp::Exception);
	EXPECT_EQ("GB", database.lookup("81.2.69.160").code);
#ifdef _WIN32
	write(staging, read(fixture));
	HANDLE held = CreateFileW(
		dcpp::File::toNativePath(live).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(INVALID_HANDLE_VALUE, held);
	EXPECT_THROW(database.install(staging), dcpp::Exception);
	CloseHandle(held);
	EXPECT_EQ(read(fixture), read(live));
	EXPECT_EQ("GB", database.lookup("81.2.69.160").code);
#endif
}

TEST(GeoIP, extracts_only_country_database_and_rejects_bad_archives) {
	Files files;
	auto archive = files.path(L"download.gz"), target = files.path(L"candidate.mmdb");
	const auto database = read(fixture);
	const auto valid = entry("GeoLite2-Country_20261010/GeoLite2-Country.mmdb", database) + std::string(1024, '\0');
	gzip(archive, entry("../../unwanted.txt", "not extracted") + valid);
	ASSERT_NO_THROW(dcpp::GeoIP::extractCountry(archive, target));
	EXPECT_EQ(database, read(target));
	for(const auto& tar :
		{ entry("wrong.mmdb", database) + std::string(1024, '\0'), entry("GeoLite2-Country.mmdb", database, '2') + std::string(1024, '\0'),
			entry("GeoLite2-Country.mmdb", database) + valid, valid.substr(0, 600) }) {
		gzip(archive, tar);
		EXPECT_THROW(dcpp::GeoIP::extractCountry(archive, target), dcpp::Exception);
		EXPECT_EQ(database, read(target));
	}

	gzip(archive, valid);
	auto corrupt = read(archive);
	corrupt.resize(corrupt.size() - 5);
	write(archive, corrupt);
	EXPECT_THROW(dcpp::GeoIP::extractCountry(archive, target), dcpp::Exception);
	EXPECT_EQ(database, read(target));
}
