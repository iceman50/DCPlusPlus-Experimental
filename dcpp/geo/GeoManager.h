/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_GEO_MANAGER_H
#define DCPLUSPLUS_DCPP_GEO_MANAGER_H

#include <memory>
#include <string>

#include <dcpp/forward.h>
#include <dcpp/geo/GeoIP.h>
#include <dcpp/base/Singleton.h>

namespace dcpp {

using std::string;
using std::unique_ptr;

/** Manages IP->country mappings. */
class GeoManager : public Singleton<GeoManager>
{
public:
	/** Open the shared IPv4/IPv6 Country database. */
	void init();
	/** Install a validated download while lookups are locked. */
	void install(const string& candidate);
	uint64_t buildEpoch() const;
	GeoCountry lookup(const string& ip) const;
	/** Unload the database. */
	void close();

	enum { V6 = 1 << 1, V4 = 1 << 2
	};

	/** Map an IP address to a country. The flags restrict the permitted address families. */
	string getCountry(const string& ip, int flags = V6 | V4);

	static string getDbPath();

private:
	friend class Singleton<GeoManager>;

	mutable CriticalSection cs;
	unique_ptr<GeoIP> geo;

	GeoManager() { }
	virtual ~GeoManager() { }
};

} // namespace dcpp

#endif
