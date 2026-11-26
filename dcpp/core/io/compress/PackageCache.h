/*
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

#ifndef DCPLUSPLUS_DCPP_PACKAGE_CACHE_H
#define DCPLUSPLUS_DCPP_PACKAGE_CACHE_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace dcpp {

using std::string;

/**
 * Package files (ZIP archives: icon and emoticon packages) extracted once, each into its own folder,
 * which is read instead of the archive from then on, as an installed plugin is.
 *
 * A package is extracted the first time it is asked for, and again once its file changes (a new
 * size or modification time gets a new folder). Each folder, under Extracted in the user's local
 * data, holds the package's files in "contents" and a note of the package file it came from;
 * that note is written last, so a folder whose extraction was cut short is extracted again.
 */
class PackageCache {
public:
	struct Limits {
		size_t maxEntries;
		uint64_t maxFileBytes;
		uint64_t maxTotalBytes;
	};

	/** The folder (ending in a path separator) holding the files of the package at `path`,
	    extracting them first if this version of the file hasn't been. `kind` names the folder that
	    packages of one sort are kept in. Throws if the package can't be read, or holds more
	    entries or data than `limits` allow, also when it was extracted before under other limits. */
	static string extract(const string& path, const string& kind, const Limits& limits);

	/** Remove the extracted packages of `kind` whose package file is gone or has changed since. */
	static void clean(const string& kind);

	/** Where packages of `kind` are extracted. */
	static string getDirectory(const string& kind);
};

} // namespace dcpp

#endif
