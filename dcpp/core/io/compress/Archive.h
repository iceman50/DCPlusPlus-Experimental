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

#ifndef DCPLUSPLUS_DCPP_ARCHIVE_H
#define DCPLUSPLUS_DCPP_ARCHIVE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

typedef void* unzFile;

namespace dcpp {

using std::string;

/** Extract a ZIP archive. */
class Archive {
public:
	Archive(const string& path);
	~Archive();

	/** What extract() extracted. */
	struct Totals {
		size_t entries = 0;
		uint64_t largestFile = 0;
		uint64_t totalBytes = 0;
	};

	/** Extract all the files in the archive to the specified directory. Throws on errors, including
	    archives with more entries or more decompressed data than the limits allow. */
	Totals extract(const string& path, size_t maxEntries = 4096, uint64_t maxFileBytes = 16 * 1024 * 1024,
		uint64_t maxTotalBytes = 128 * 1024 * 1024);
	/** Read one archive entry without extracting its siblings. */
	string readFile(const string& name, size_t maxBytes);
	/** Validate a bounded package before reading any payloads. Returns regular file names. */
	std::vector<string> packageEntries(size_t maxEntries, uint64_t maxFileBytes, uint64_t maxTotalBytes);
	static bool isPackagePath(const string& path);

private:
	inline int check(int ret);

	::unzFile file;
};

} // namespace dcpp

#endif
