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
#include <dcpp/core/io/compress/Archive.h>

#include <dcpp/core/header/debug.h>
#include <dcpp/core/io/File.h>
#include <dcpp/util/Util.h>
#include <set>

#include <unzip.h>
#ifdef _WIN32
#include <iowin32.h>
#endif

namespace dcpp {

Archive::Archive(const string& path) {
#ifdef _WIN32
	zlib_filefunc64_def funcs;
	fill_win32_filefunc64(&funcs);
	file = unzOpen2_64(File::toNativePath(path).c_str(), &funcs);
#else
	file = unzOpen64(path.c_str());
#endif
	if(!file) {
		throw Exception(_("Invalid archive"));
	}
}

Archive::~Archive() {
	if(file) {
		unzClose(file);
	}
}

void Archive::extract(const string& path) {
	auto isDir = [](const string& path) { return *(path.end() - 1) == '/' || *(path.end() - 1) == '\\'; };
	auto isSafeEntry = [](string entry) {
		if(entry.empty() || entry.front() == '/' || entry.front() == '\\' || entry.find(':') != string::npos) return false;
		std::replace(entry.begin(), entry.end(), '\\', '/');
		size_t begin = 0;
		while(begin < entry.size()) {
			const auto end = entry.find('/', begin);
			const auto part = entry.substr(begin, end == string::npos ? string::npos : end - begin);
			if(part.empty() || part == "." || part == "..") return false;
			if(end == string::npos) break;
			begin = end + 1;
		}
		return true;
	};

	dcassert(!path.empty() && isDir(path));

	if(check(unzGoToFirstFile(file)) != UNZ_OK) { return; }

	do {
		unz_file_info64 info = { };
		if(check(unzGetCurrentFileInfo64(file, &info, nullptr, 0, nullptr, 0, nullptr, 0)) != UNZ_OK) { continue; }
		vector<char> pathBuf(static_cast<size_t>(info.size_filename) + 1, 0);
		if(check(unzGetCurrentFileInfo64(file, nullptr, pathBuf.data(),
			static_cast<uLong>(pathBuf.size()), nullptr, 0, nullptr, 0)) != UNZ_OK) { continue; }
		string path_out(pathBuf.data(), info.size_filename);
		if(path_out.empty() || isDir(path_out)) {
			continue;
		}
		if(!isSafeEntry(path_out)) throw Exception(_("Invalid archive path"));
		if(check(unzOpenCurrentFile(file)) != UNZ_OK) { continue; }
		path_out = path + path_out;

		File::ensureDirectory(path_out);
		File f_out(path_out, File::WRITE, File::CREATE | File::TRUNCATE);

		const size_t BUF_SIZE = 64 * 1024;
		ByteVector buf(BUF_SIZE);
		while(true) {
			auto read = unzReadCurrentFile(file, &buf[0], BUF_SIZE);
			if(read > 0) {
				f_out.write(&buf[0], read);
			} else if(read == UNZ_EOF) {
				break;
			} else {
				check(read);
			}
		}

		// this does the CRC check.
		check(unzCloseCurrentFile(file));

	} while(check(unzGoToNextFile(file)) == UNZ_OK);
}

bool Archive::isPackagePath(const string& path) {
	if(path.empty() || path.size() > 240 || path.front() == '/' || path.back() == '/') return false;
	for(unsigned char c: path) {
		if(c < 32 || c >= 127 || string("\\:<>\"|?*~").find(c) != string::npos) return false;
	}
	size_t begin = 0;
	while(begin < path.size()) {
		const auto end = path.find('/', begin);
		auto part = path.substr(begin, end == string::npos ? string::npos : end - begin);
		if(part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
		std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) { return static_cast<char>(toupper(c)); });
		part = part.substr(0, part.find('.'));
		while(!part.empty() && part.back() == ' ') part.pop_back();
		if(part == "CON" || part == "PRN" || part == "AUX" || part == "NUL" ||
			(part.size() == 4 && (part.substr(0, 3) == "COM" || part.substr(0, 3) == "LPT") && part[3] >= '0' && part[3] <= '9')) return false;
		if(end == string::npos) break;
		begin = end + 1;
	}
	return true;
}

std::vector<string> Archive::packageEntries(size_t maxEntries, uint64_t maxFileBytes, uint64_t maxTotalBytes) {
	std::vector<string> result;
	std::set<string> names, regularNames;
	uint64_t total = 0;
	size_t count = 0;
	if(check(unzGoToFirstFile(file)) != UNZ_OK) throw Exception("Empty plugin package");
	do {
		unz_file_info64 info {};
		check(unzGetCurrentFileInfo64(file, &info, nullptr, 0, nullptr, 0, nullptr, 0));
		if(++count > maxEntries || info.size_filename > 241 || info.uncompressed_size > maxFileBytes ||
			info.uncompressed_size > maxTotalBytes - total) throw Exception("Plugin package exceeds size limits");
		total += info.uncompressed_size;
		vector<char> name(info.size_filename + 1, 0);
		check(unzGetCurrentFileInfo64(file, nullptr, name.data(), static_cast<uLong>(name.size()), nullptr, 0, nullptr, 0));
		string entry(name.data(), info.size_filename);
		const bool directory = !entry.empty() && entry.back() == '/';
		if(directory) entry.pop_back();
		const auto mode = (info.external_fa >> 16) & 0170000;
		if(!isPackagePath(entry) || (info.flag & 1) || (mode && mode != (directory ? 0040000 : 0100000)))
			throw Exception("Invalid plugin package entry");
		auto folded = entry;
		std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
		if(!names.insert(folded).second) throw Exception("Duplicate plugin package path");
		if(!directory) { result.push_back(entry); regularNames.insert(folded); }
	} while(check(unzGoToNextFile(file)) == UNZ_OK);
	for(const auto& name: names) {
		for(auto slash = name.find('/'); slash != string::npos; slash = name.find('/', slash + 1)) {
			if(regularNames.count(name.substr(0, slash))) throw Exception("Conflicting plugin package paths");
		}
	}
	return result;
}

string Archive::readFile(const string& name, size_t maxBytes) {
	if(unzLocateFile(file, name.c_str(), 0) != UNZ_OK) throw Exception(_("Archive entry not found"));
	unz_file_info64 info = {};
	if(check(unzGetCurrentFileInfo64(file, &info, nullptr, 0, nullptr, 0, nullptr, 0)) != UNZ_OK)
		throw Exception(_("Invalid archive"));
	if(info.uncompressed_size > maxBytes) throw Exception(_("Archive entry is too large"));
	if(check(unzOpenCurrentFile(file)) != UNZ_OK) throw Exception(_("Invalid archive"));

	string result(static_cast<size_t>(info.uncompressed_size), '\0');
	size_t offset = 0;
	try {
		while(offset < result.size()) {
			const auto requested = static_cast<unsigned>(std::min<size_t>(result.size() - offset, 64 * 1024));
			const auto read = unzReadCurrentFile(file, &result[offset], requested);
			if(read <= 0) throw Exception(_("Invalid archive"));
			offset += static_cast<size_t>(read);
		}
		char extra;
		if(unzReadCurrentFile(file, &extra, 1) != 0) throw Exception("Invalid archive entry length");
	} catch(...) {
		unzCloseCurrentFile(file);
		throw;
	}
	check(unzCloseCurrentFile(file));
	return result;
}

int Archive::check(int ret) {
	if(ret != UNZ_OK && ret != UNZ_END_OF_LIST_OF_FILE && ret != UNZ_EOF) {
		throw Exception(_("Invalid archive"));
	}
	return ret;
}

} // namespace dcpp
