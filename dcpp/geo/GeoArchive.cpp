/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "stdinc.h"

#include <dcpp/geo/GeoIP.h>
#include <dcpp/system/File.h>

#include <zlib.h>
#include <array>

namespace dcpp {

void GeoIP::extractCountry(const std::string& archive, const std::string& target) {
	constexpr size_t MAX_DATABASE_SIZE = 32 * 1024 * 1024;
	constexpr size_t MAX_TAR_SIZE = 64 * 1024 * 1024;
	File input(archive, File::READ, File::OPEN);
	if(input.getSize() <= 0 || static_cast<uint64_t>(input.getSize()) > MAX_DATABASE_SIZE)
		throw Exception("Invalid GeoLite2 download size");

	std::string compressed = input.read();
	z_stream stream {};
	if(inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
		throw Exception("Could not open GeoLite2 download");

	struct End {
		z_stream& s;

		~End() { inflateEnd(&s); }
	} end { stream };

	stream.next_in = reinterpret_cast<Bytef*>(compressed.data());
	stream.avail_in = static_cast<uInt>(compressed.size());
	std::string tar;
	std::array<char, 65536> buffer;
	int status;
	do {
		stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
		stream.avail_out = static_cast<uInt>(buffer.size());
		status = inflate(&stream, Z_NO_FLUSH);
		if(status != Z_OK && status != Z_STREAM_END)
			throw Exception("Corrupt GeoLite2 gzip archive");

		tar.append(buffer.data(), buffer.size() - stream.avail_out);
		if(tar.size() > MAX_TAR_SIZE)
			throw Exception("GeoLite2 archive is too large");
	} while(status != Z_STREAM_END);
	if(stream.avail_in)
		throw Exception("Unexpected data after GeoLite2 gzip archive");

	size_t selected = 0, selectedSize = 0;
	bool terminated = false;
	for(size_t offset = 0; offset + 512 <= tar.size();) {
		const auto* header = tar.data() + offset;
		if(std::all_of(header, header + 512, [](char c) { return c == 0; })) {
			terminated = true;
			break;
		}

		auto octal = [](const char* p, size_t n) {
			size_t value = 0;
			for(size_t i = 0; i < n; ++i) {
				if(p[i] == 0 || p[i] == ' ')
					continue;

				if(p[i] < '0' || p[i] > '7' || value > MAX_TAR_SIZE)
					throw Exception("Invalid GeoLite2 tar header");

				value = value * 8 + (p[i] - '0');
			}

			return value;
		};
		size_t checksum = 0;
		for(size_t i = 0; i < 512; ++i)
			checksum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(header[i]);
		if(checksum != octal(header + 148, 8))
			throw Exception("Invalid GeoLite2 tar checksum");

		const auto size = octal(header + 124, 12);
		if(size > MAX_TAR_SIZE || size > tar.size() - offset - 512)
			throw Exception("Truncated GeoLite2 tar archive");

		std::string name(header, std::find(header, header + 100, '\0'));
		const auto slash = name.find_last_of('/');
		if(name.substr(slash == std::string::npos ? 0 : slash + 1) == "GeoLite2-Country.mmdb" && (header[156] == '0' || header[156] == 0)) {
			if(selectedSize || !size || size > MAX_DATABASE_SIZE)
				throw Exception("Invalid GeoLite2 Country archive entry");

			selected = offset + 512;
			selectedSize = size;
		}

		offset += 512 + ((size + 511) / 512) * 512;
	}

	if(!terminated || !selectedSize)
		throw Exception("GeoLite2 Country database missing or archive truncated");

	// Always write to the caller's staging filename, never to a tar-supplied path.
	File output(target, File::WRITE, File::CREATE | File::TRUNCATE);
	output.write(tar.data() + selected, selectedSize);
	output.flush();
}
}
