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
#include <dcpp/core/version.h>

#include "version-rev-id.inc" // should #define DCPP_REVISION
#include "version-build.inc" // generated local build counter

#define xstrver(s) strver(s)
#define strver(s) #s

namespace dcpp {
const std::string buildLabel("[Build: " xstrver(DCPP_BUILD_NUMBER) "]");
const std::string hubVersionString(VERSIONSTRING "-" xstrver(DCPP_BUILD_NUMBER));
const std::string fullVersionString(APPNAME " v" VERSIONSTRING " (" xstrver(DCPP_REVISION) ")");
}
