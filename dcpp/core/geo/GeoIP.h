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
#ifndef DCPLUSPLUS_DCPP_GEOIP_H
#define DCPLUSPLUS_DCPP_GEOIP_H
#include <dcpp/core/thread/CriticalSection.h>
#include <memory>
#include <string>
#include <cstdint>
namespace dcpp {
struct GeoCountry {
	std::string code, englishName, continent;
	std::string format(const std::string& pattern) const;
};

/** Local IPv4/IPv6 Country MMDB reader. Results own their strings so an update
 * cannot invalidate values in use by UI or network threads. */
class GeoIP {
public:
	explicit GeoIP(std::string path);
	~GeoIP();
	GeoIP(const GeoIP&) = delete;
	GeoIP& operator=(const GeoIP&) = delete;
	GeoCountry lookup(const std::string& ip) const;
	uint64_t buildEpoch() const;
	bool reload();
	/** Validate before atomically replacing the database. Throws on failure. */
	void install(const std::string& candidate);
	/** Extract only GeoLite2-Country.mmdb from a bounded tar.gz download. */
	static void extractCountry(const std::string& archive, const std::string& target);

private:
	struct Database;
	const std::string path;
	mutable CriticalSection cs;
	std::unique_ptr<Database> database;
};
}
#endif
