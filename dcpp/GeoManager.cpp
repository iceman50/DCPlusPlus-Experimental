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

#include "GeoManager.h"

#include "SettingsManager.h"
#include "Util.h"

namespace dcpp {

void GeoManager::init() {
	Lock lock(cs);
	if(!geo)
		geo = std::make_unique<GeoIP>(getDbPath());
}

void GeoManager::install(const string& candidate) {
	Lock lock(cs);
	if(!geo)
		geo = std::make_unique<GeoIP>(getDbPath());
	geo->install(candidate);
}

void GeoManager::close() {
	Lock lock(cs);
	geo.reset();
}

uint64_t GeoManager::buildEpoch() const {
	Lock lock(cs);

	return geo ? geo->buildEpoch() : 0;
}

GeoCountry GeoManager::lookup(const string& ip) const {
	Lock lock(cs);

	return geo && SETTING(GET_USER_COUNTRY) ? geo->lookup(ip) : GeoCountry();
}

string GeoManager::getCountry(const string& ip, int flags) {
	if(!(flags & (Util::isIpV4(ip) ? V4 : V6)))
		return {};

	return lookup(ip).format(SETTING(COUNTRY_FORMAT));
}

string GeoManager::getDbPath() {
	return Util::getPath(Util::PATH_USER_LOCAL) + "GeoLite2-Country.mmdb";
}
}
