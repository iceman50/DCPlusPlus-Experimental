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
#include <dcpp/core/io/SFVReader.h>

#include <dcpp/util/text/StringTokenizer.h>
#include <dcpp/core/io/File.h>

#ifndef _WIN32
#include <dirent.h>
#include <fnmatch.h>
#endif

namespace dcpp {

bool SFVReader::findCRC(const string& sfv, const string& fileName, uint32_t& crc_) noexcept {
	string::size_type i = 0;
	while( (i = Util::findSubString(sfv, fileName, i)) != string::npos) {
		// Either we're at the beginning of the file or the line...otherwise skip...
		if( (i == 0) || (sfv[i-1] == '\n') ) {
			string::size_type j = i + fileName.length() + 1;
			if(j < sfv.length() - 8) {
				// Check for at least one whitespace between filename and crc32 value...otherwise skip...
				if(!isspace(static_cast<unsigned char>(sfv[j-1]))) {
					i = j;
					continue;
				}
				uint32_t value = 0;
				sscanf(sfv.c_str() + j, "%x", &value);
				crc_ = value;
				return true;
			}
		}
		i += fileName.length();
	}
	return false;
}

void SFVReader::load(const string& fileName) noexcept {
	string path = Util::getFilePath(fileName);
	string fname = Util::getFileName(fileName);
	StringList files = File::findFiles(path, "*.sfv");

	for(auto& i: files) {
		try {
			if (findCRC(File(i, File::READ, File::OPEN).read(), fname, crc32)) {
				crcFound = true;
				return;
			}
		} catch(const FileException&) {
			// Ignore...
		}
	}
}

bool SFVDirectoryReader::getCRC(const string& fileName, uint32_t& crc_) {
	const auto path = Util::getFilePath(fileName);
	if(!loaded || path != directory) {
		directory = path;
		loaded = true;
		sfvContents.clear();
		for(const auto& sfvFile: File::findFiles(path, "*.sfv")) {
			try {
				sfvContents.push_back(File(sfvFile, File::READ, File::OPEN).read());
			} catch(const FileException&) {
				// Ignore, as SFVReader does.
			}
		}
	}
	if(sfvContents.empty()) {
		return false;
	}
	const auto name = Util::getFileName(fileName);
	for(const auto& sfv: sfvContents) {
		if(SFVReader::findCRC(sfv, name, crc_)) {
			return true;
		}
	}
	return false;
}

void SFVDirectoryReader::clear() noexcept {
	directory.clear();
	loaded = false;
	sfvContents.clear();
}

} // namespace dcpp
