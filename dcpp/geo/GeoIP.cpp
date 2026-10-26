/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdinc.h"

#include <dcpp/geo/GeoIP.h>

#include <dcpp/system/File.h>
#include <dcpp/text/Text.h>
#include <dcpp/util/Util.h>

#include <maxminddb.h>
#include <array>
#include <cstring>
#ifndef _WIN32
#include <arpa/inet.h>
#endif

namespace dcpp {
namespace {

constexpr size_t MAX_DATABASE_SIZE = 32 * 1024 * 1024;

std::string field(MMDB_entry_s& entry, const char* group, const char* name, const char* locale = nullptr) {
	MMDB_entry_data_s value {};
	int result =
		locale ? MMDB_get_value(&entry, &value, group, name, locale, nullptr) : MMDB_get_value(&entry, &value, group, name, nullptr);
	return result == MMDB_SUCCESS && value.has_data && value.type == MMDB_DATA_TYPE_UTF8_STRING
		? std::string(value.utf8_string, value.data_size)
		: std::string();
}

bool countryCode(const std::string& code) {
	return code.size() == 2 && code[0] >= 'A' && code[0] <= 'Z' && code[1] >= 'A' && code[1] <= 'Z';
}
#ifdef _WIN32
std::unordered_map<std::string, GEOID>& geoIds() {
	static std::unordered_map<std::string, GEOID> ids;

	return ids;
}

BOOL CALLBACK collectGeoId(GEOID id) {
	wchar_t code[4] {};
	if(GetGeoInfoW(id, GEO_ISO2, code, 4, 0))
		geoIds().emplace(Text::fromT(code), id);
	return TRUE;
}

std::string windowsName(const std::string& code, GEOTYPE type) {
	static const bool initialized = EnumSystemGeoID(GEOCLASS_NATION, 0, collectGeoId) != FALSE;
	(void)initialized;
	auto i = geoIds().find(code);
	if(i == geoIds().end())
		return {};

	wchar_t value[256] {};
	return GetGeoInfoW(i->second, type, value, 256, 0) ? Text::fromT(value) : std::string();
}
#endif
}

struct GeoIP::Database {
	MMDB_s mmdb {};

	explicit Database(const std::string& path) {
		const auto size = File::getSize(path);
		if(size <= 0 || static_cast<uint64_t>(size) > MAX_DATABASE_SIZE)
			throw Exception("Invalid GeoLite2 Country database size");

		auto status = MMDB_open(path.c_str(), MMDB_MODE_MMAP, &mmdb);
		if(status != MMDB_SUCCESS)
			throw Exception("Invalid GeoLite2 Country database");

		const std::string type = mmdb.metadata.database_type ? mmdb.metadata.database_type : "";
		if((type != "GeoLite2-Country" && type != "GeoIP2-Country") || mmdb.metadata.ip_version != 6 ||
			mmdb.metadata.binary_format_major_version != 2 || !mmdb.metadata.build_epoch) {
			MMDB_close(&mmdb);
			throw Exception("Expected an IPv4/IPv6 GeoLite2 Country database");
		}
	}

	~Database() { MMDB_close(&mmdb); }
};

GeoIP::GeoIP(std::string path) :
	path(std::move(path))
{
	reload();
}

GeoIP::~GeoIP() = default;

bool GeoIP::reload() {
	Lock lock(cs);

	try {
		database = std::make_unique<Database>(path);

		return true;
	} catch(const Exception&) {
		return false;
	}
}

uint64_t GeoIP::buildEpoch() const {
	Lock lock(cs);

	return database ? database->mmdb.metadata.build_epoch : 0;
}

GeoCountry GeoIP::lookup(const std::string& ip) const {
	GeoCountry country;
	if(ip.empty() || ip.find('\0') != std::string::npos || ip.find('%') != std::string::npos)
		return country;

	sockaddr_storage address {};
	auto* v4 = reinterpret_cast<sockaddr_in*>(&address);
	auto* v6 = reinterpret_cast<sockaddr_in6*>(&address);
#ifdef _WIN32
	const auto parse = InetPtonA;
#else
	const auto parse = inet_pton;
#endif
	if(parse(AF_INET, ip.c_str(), &v4->sin_addr) == 1)
		v4->sin_family = AF_INET;
	else if(parse(AF_INET6, ip.c_str(), &v6->sin6_addr) == 1)
		v6->sin6_family = AF_INET6;
	else
		return country;

	Lock lock(cs);
	if(!database)
		return country;

	int error = 0;
	auto result = MMDB_lookup_sockaddr(&database->mmdb, reinterpret_cast<const sockaddr*>(&address), &error);
	if(error || !result.found_entry)
		return country;

	// Registration country is not a substitute for the IP's location.
	country.code = field(result.entry, "country", "iso_code");
	if(!countryCode(country.code))
		return {};

	country.englishName = field(result.entry, "country", "names", "en");
	country.continent = field(result.entry, "continent", "code");

	return country;
}

std::string GeoCountry::format(const std::string& pattern) const {
	if(code.empty())
		return {};

	std::string name = englishName, official = englishName, alpha3;
#ifdef _WIN32
	const auto localized = windowsName(code, GEO_FRIENDLYNAME);
	if(!localized.empty())
		name = localized;
	const auto full = windowsName(code, GEO_OFFICIALNAME);
	if(!full.empty())
		official = full;
	alpha3 = windowsName(code, GEO_ISO3);
#endif
	ParamMap params;
	params["2code"] = code;
	params["3code"] = alpha3;
	params["continent"] = continent;
	params["engname"] = englishName;
	params["name"] = name;
	params["officialname"] = official;

	return Util::formatParams(pattern, params);
}

void GeoIP::install(const std::string& candidate) {
	{
		Database validated(candidate);
	}

	Lock lock(cs);

	// Windows mappings must close before replacing the file. The rename keeps
	// the old file intact if replacement fails; reopen it under the same lock.
	database.reset();
#ifdef _WIN32
	const bool replaced = MoveFileExW(File::toNativePath(candidate).c_str(), File::toNativePath(path).c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
	const bool replaced = ::rename(candidate.c_str(), path.c_str()) == 0;
#endif
	try {
		database = std::make_unique<Database>(path);
	} catch(const Exception&) {
	}

	if(!replaced || !database)
		throw Exception("Could not install GeoLite2 Country database");
}
}
