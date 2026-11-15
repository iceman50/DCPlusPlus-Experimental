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
#include <dcpp/core/classes/Magnet.h>

#include <dcpp/util/text/StringTokenizer.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/util/Util.h>
#include <dcpp/hash/value/MerkleTree.h>
#include <limits>
#include <cstring>

namespace dcpp {

namespace {
bool decode(const string& input, string& output) {
	const auto hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
	for(size_t i = 0; i < input.size(); ++i) {
		if(input[i] == '%') {
			if(i + 2 >= input.size() || !hex(input[i + 1]) || !hex(input[i + 2])) return false;
			i += 2;
		}
	}
	output = Util::encodeURI(input, true);
	return std::none_of(output.begin(), output.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}
}

string Magnet::Metadata::suggestedName() const {
	auto result = name;
	const auto slash = result.find_last_of("/\\");
	if(slash != string::npos) result.erase(0, slash + 1);
	for(auto& c : result) {
		if(static_cast<unsigned char>(c) < 0x20 || string("<>:\"/\\|?*").find(c) != string::npos) c = '_';
	}
	while(!result.empty() && (result.back() == '.' || result.back() == ' ')) result.pop_back();
	if(result.empty()) result = hash.empty() ? "download" : hash;
	const auto base = Text::toLower(result.substr(0, result.find('.')));
	if(base == "con" || base == "prn" || base == "aux" || base == "nul" ||
		(base.size() == 4 && (base.substr(0, 3) == "com" || base.substr(0, 3) == "lpt") && base[3] >= '1' && base[3] <= '9'))
		result.insert(0, "_");
	return result;
}

bool Magnet::parseUri(const string& uri, Metadata& result) {
	result = Metadata();
	if(uri.size() > 65536 || Util::strnicmp(uri.c_str(), "magnet:?", 8)) return false;
	Metadata parsed;
	std::map<string, string> hashes;
	bool hasSize = false;
	StringTokenizer<string> parameters(uri.substr(8), '&');
	for(const auto& pair : parameters.getTokens()) {
		const auto equals = pair.find('=');
		if(equals == string::npos) return false;
		string type, value;
		if(!decode(pair.substr(0, equals), type) || !decode(pair.substr(equals + 1), value)) return false;
		type = Text::toLower(type);
		if(type == "xt" || type == "xs" || type == "as") {
			const auto lower = Text::toLower(value);
			string hash;
			if(lower.compare(0, 13, "urn:bitprint:") == 0) {
				if(value.size() != 85 || value[45] != '.') return false;
				hash = value.substr(46);
			} else {
				for(const auto prefix : { "urn:tree:tiger:", "urn:tree:tiger/:", "urn:tree:tiger/1024:" }) {
					const auto length = std::strlen(prefix);
					if(lower.compare(0, length, prefix) == 0) {
						hash = value.substr(length);
						if(hash.empty()) return false;
						break;
					}
				}
			}
			if(!hash.empty()) {
				for(auto& c : hash) if(c >= 'a' && c <= 'z') c -= 'a' - 'A';
				if(hash.size() != 39 || !Encoder::isBase32(hash)) return false;
				// Compare the decoded identity, including non-significant Base32 padding bits.
				hash = TTHValue(hash).toBase32();
				auto existing = hashes.find(type);
				if(existing != hashes.end() && existing->second != hash) return false;
				hashes[type] = hash;
			}
		} else if(type == "xl") {
			if(value.empty()) return false;
			int64_t size = 0;
			for(const auto c : value) {
				if(c < '0' || c > '9' || size > (std::numeric_limits<int64_t>::max() - (c - '0')) / 10) return false;
				size = size * 10 + c - '0';
			}
			if(hasSize && parsed.size != size) return false;
			parsed.size = size;
			hasSize = true;
		} else if(type == "dn") {
			if(parsed.name.empty()) parsed.name = value;
		} else if(type == "kt") {
			if(parsed.key.empty()) parsed.key = value;
		}
	}
	for(const auto type : { "xt", "xs", "as" }) {
		const auto found = hashes.find(type);
		if(found != hashes.end()) { parsed.hash = found->second; break; }
	}
	Util::trim(parsed.key);
	if(parsed.hash.empty() && parsed.key.empty()) return false;
	result = std::move(parsed);
	return true;
}

bool Magnet::parseUri(const string& uri, string& hash, string& name, string& key) {
	Metadata result;
	const bool valid = parseUri(uri, result);
	hash = std::move(result.hash);
	name = std::move(result.name);
	key = std::move(result.key);
	return valid;
}

} // namespace dcpp
