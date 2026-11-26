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

#include "stdinc.h"
#include <dcpp/core/io/compress/PackageCache.h>

#include <dcpp/core/header/debug.h>
#include <dcpp/core/io/compress/Archive.h>
#include <dcpp/core/io/File.h>
#include <dcpp/util/Util.h>
#include <dcpp/util/text/Text.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

namespace dcpp {

namespace fs = std::filesystem;

namespace {

constexpr const char* CONTENTS = "contents";
constexpr const char* NOTE = "source.txt";

/** One version of a package file. */
struct Source {
	/** Absolute; in lower case on Windows, whose file names ignore case. */
	string path;
	uint64_t size = 0;
	int64_t time = 0;

	bool operator==(const Source& other) const {
		return path == other.path && size == other.size && time == other.time;
	}
};

/** What an extracted folder holds: the package file it came from, and how much was extracted. */
struct Note {
	Source source;
	Archive::Totals totals;
};

std::mutex& cacheMutex() {
	static std::mutex value;
	return value;
}

bool identify(const string& path, Source& source) {
	std::error_code error;
	auto file = fs::absolute(fs::u8path(path), error);
	if(error) return false;
	file = file.lexically_normal();
	const auto size = fs::file_size(file, error);
	if(error) return false;
	const auto time = fs::last_write_time(file, error);
	if(error) return false;
	source.path = file.u8string();
#ifdef _WIN32
	source.path = Text::toLower(source.path);
#endif
	source.size = size;
	source.time = static_cast<int64_t>(time.time_since_epoch().count());
	return true;
}

/** The package's file name, readable, then a hash of the version, so each version has its own. */
string folderName(const string& path, const Source& source) {
	auto base = fs::u8path(path).stem().u8string();
	for(auto& c: base) {
		const auto u = static_cast<unsigned char>(c);
		if(!((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || c == '-' || c == '_')) c = '_';
	}
	if(base.size() > 48) base.resize(48);
	if(base.empty()) base = "package";

	// FNV-1a: the same on every run, unlike std::hash.
	uint64_t hash = 14695981039346656037ull;
	const auto version = source.path + '|' + std::to_string(source.size) + '|' + std::to_string(source.time);
	for(auto c: version) {
		hash ^= static_cast<unsigned char>(c);
		hash *= 1099511628211ull;
	}
	char hex[17];
	std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
	return base + '-' + hex;
}

string formatNote(const Note& note) {
	return "path=" + note.source.path +
		"\nsize=" + std::to_string(note.source.size) +
		"\ntime=" + std::to_string(note.source.time) +
		"\nentries=" + std::to_string(note.totals.entries) +
		"\nlargest=" + std::to_string(note.totals.largestFile) +
		"\ntotal=" + std::to_string(note.totals.totalBytes) + "\n";
}

bool readNote(const string& folder, Note& note) {
	string text;
	try {
		text = File(folder + NOTE, File::READ, File::OPEN).read();
	} catch(const FileException&) {
		return false;
	}
	std::map<string, string> values;
	size_t begin = 0;
	while(begin < text.size()) {
		auto end = text.find('\n', begin);
		if(end == string::npos) end = text.size();
		const auto line = text.substr(begin, end - begin);
		const auto equals = line.find('=');
		if(equals != string::npos) values[line.substr(0, equals)] = line.substr(equals + 1);
		begin = end + 1;
	}
	for(auto key: { "path", "size", "time", "entries", "largest", "total" }) {
		if(values.find(key) == values.end()) return false;
	}
	try {
		note.source.path = values["path"];
		note.source.size = std::stoull(values["size"]);
		note.source.time = std::stoll(values["time"]);
		note.totals.entries = static_cast<size_t>(std::stoull(values["entries"]));
		note.totals.largestFile = std::stoull(values["largest"]);
		note.totals.totalBytes = std::stoull(values["total"]);
	} catch(const std::exception&) {
		return false;
	}
	return true;
}

void removeFolder(const string& folder) {
	std::error_code ignored;
	fs::remove_all(fs::u8path(folder), ignored);
}

/** The folders (ending in a path separator) in a kind's directory. */
std::vector<string> listFolders(const string& directory) {
	std::vector<string> folders;
	std::error_code error;
	for(fs::directory_iterator i(fs::u8path(directory), error), end; !error && i != end; i.increment(error)) {
		folders.push_back(i->path().u8string() + PATH_SEPARATOR_STR);
	}
	return folders;
}

Note extractInto(const string& path, const Source& source, const string& folder, const PackageCache::Limits& limits) {
	// What an extraction cut short left behind.
	removeFolder(folder);
	try {
		const auto contents = folder + CONTENTS + PATH_SEPARATOR_STR;
		File::ensureDirectory(contents);
		Note note { source, Archive(path).extract(contents, limits.maxEntries, limits.maxFileBytes, limits.maxTotalBytes) };
		// Written last: a folder without it is extracted again.
		File(folder + NOTE, File::WRITE, File::CREATE | File::TRUNCATE).write(formatNote(note));
		return note;
	} catch(...) {
		removeFolder(folder);
		throw;
	}
}

}

string PackageCache::extract(const string& path, const string& kind, const Limits& limits) {
	dcassert(!kind.empty());
	std::lock_guard<std::mutex> lock(cacheMutex());
	Source source;
	if(!identify(path, source)) throw Exception(_("Invalid archive"));

	const auto directory = getDirectory(kind);
	const auto folder = directory + folderName(path, source) + PATH_SEPARATOR_STR;
	Note note;
	if(!readNote(folder, note) || !(note.source == source)) {
		note = extractInto(path, source, folder, limits);
		// This version replaces what was extracted of the file before.
		for(const auto& other: listFolders(directory)) {
			Note otherNote;
			if(other != folder && readNote(other, otherNote) && otherNote.source.path == source.path) removeFolder(other);
		}
	}

	if(note.totals.entries > limits.maxEntries) throw Exception(_("Archive has too many entries"));
	if(note.totals.largestFile > limits.maxFileBytes || note.totals.totalBytes > limits.maxTotalBytes)
		throw Exception(_("Archive contents are too large"));
	return folder + CONTENTS + PATH_SEPARATOR_STR;
}

void PackageCache::clean(const string& kind) {
	dcassert(!kind.empty());
	std::lock_guard<std::mutex> lock(cacheMutex());
	for(const auto& folder: listFolders(getDirectory(kind))) {
		Note note;
		Source current;
		if(!readNote(folder, note) || !identify(note.source.path, current) || !(current == note.source)) {
			removeFolder(folder);
		}
	}
}

string PackageCache::getDirectory(const string& kind) {
	const auto& local = Util::getPath(Util::PATH_USER_LOCAL);
	// Util sets the paths at startup; some tests run without them.
	const auto base = local.empty() ? Util::getTempPath() + "dcpp-" : local;
	return base + "Extracted" PATH_SEPARATOR_STR + kind + PATH_SEPARATOR_STR;
}

} // namespace dcpp
