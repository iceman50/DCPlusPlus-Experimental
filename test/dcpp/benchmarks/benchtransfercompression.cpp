/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "testbase.h"
#include <dcpp/system/FilteredFile.h>
#include <dcpp/storage/ZUtils.h>
#include <dcpp/storage/ZstdUtils.h>
#include <dcpp/storage/BZUtils.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>

namespace {
using namespace dcpp;
using Clock = std::chrono::steady_clock;
constexpr size_t chunkSize = 64 * 1024;

// The production ZstdFilter is used for level 6. These comparison filters
// retain its streaming calls, window limit and checksum, changing only level.
template<int level> class ZstdLevel {
public:
	ZstdLevel() : ctx(ZSTD_createCCtx(), ZSTD_freeCCtx) {
		if(!ctx) throw std::bad_alloc();
		check(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_compressionLevel, level));
		check(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_windowLog, 23));
		check(ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_checksumFlag, 1));
	}
	bool operator()(const void* in, size_t& insize, void* out, size_t& outsize) {
		if(ended) { insize = outsize = 0; return false; }
		ZSTD_inBuffer input { in, insize, 0 };
		ZSTD_outBuffer output { out, outsize, 0 };
		const bool finish = insize == 0;
		const auto result = ZSTD_compressStream2(ctx.get(), &output, &input,
			finish ? ZSTD_e_end : ZSTD_e_continue);
		check(result);
		insize = input.pos;
		outsize = output.pos;
		ended = finish && result == 0;
		return !ended;
	}
private:
	static void check(size_t result) {
		if(ZSTD_isError(result)) throw Exception(ZSTD_getErrorName(result));
	}
	std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> ctx;
	bool ended = false;
};

class InputView : public InputStream {
public:
	explicit InputView(const string& value) : data(value) { }
	size_t read(void* out, size_t& size) override {
		size = std::min(size, data.size() - pos);
		memcpy(out, data.data() + pos, size);
		pos += size;
		return size;
	}
private:
	const string& data;
	size_t pos = 0;
};

class OutputBuffer : public OutputStream {
public:
	explicit OutputBuffer(string& value) : data(value) { }
	size_t write(const void* in, size_t size) override {
		if(size > data.size() - pos) throw Exception("Benchmark output overflow");
		memcpy(&data[pos], in, size);
		pos += size;
		return size;
	}
	size_t flush() override { return 0; }
	size_t pos = 0;
private:
	string& data;
};

template<class Filter> size_t compress(const string& data, string& result) {
	InputView input(data);
	FilteredInputStream<Filter, false> stream(&input);
	OutputBuffer output(result);
	char buffer[chunkSize];
	for(;;) {
		size_t size = sizeof(buffer);
		const auto produced = stream.read(buffer, size);
		if(!produced) break;
		output.write(buffer, produced);
	}
	return output.pos;
}

template<class Filter> size_t decompress(const string& data, string& result) {
	OutputBuffer output(result);
	FilteredOutputStream<Filter, false> stream(&output);
	for(size_t pos = 0; pos < data.size(); pos += chunkSize)
		stream.write(data.data() + pos, std::min(chunkSize, data.size() - pos));
	stream.flush();
	return output.pos;
}

using CodecFunction = size_t (*)(const string&, string&);
struct Codec {
	const char* name;
	CodecFunction encode;
	CodecFunction decode;
};
const Codec codecs[] = {
	{ "zlib-3-adaptive", compress<ZFilter>, decompress<UnZFilter> },
	{ "zstd-1", compress<ZstdLevel<1>>, decompress<UnZstdFilter> },
	{ "zstd-2", compress<ZstdLevel<2>>, decompress<UnZstdFilter> },
	{ "zstd-3", compress<ZstdLevel<3>>, decompress<UnZstdFilter> },
	{ "zstd-6", compress<ZstdFilter>, decompress<UnZstdFilter> },
	// File lists are read through an input filter in DirectoryListing::loadFile.
	{ "bzip2-9", compress<BZFilter>, compress<UnBZFilter> }
};

template<class Function> double timed(Function fn) {
	const auto start = Clock::now();
	size_t iterations = 0;
	double seconds;
	do {
		fn();
		++iterations;
		seconds = std::chrono::duration<double>(Clock::now() - start).count();
	} while(seconds < 0.1);
	return seconds / iterations;
}
}

TEST(TransferCompressionBenchmark, DISABLED_corpus) {
	const auto directory = std::getenv("TRANSFER_BENCH_DIR");
	ASSERT_TRUE(directory && *directory) << "Set TRANSFER_BENCH_DIR to the prepared corpus directory";
	std::ifstream manifest(std::filesystem::path(directory) / "manifest.txt");
	ASSERT_TRUE(manifest.good());
	std::ofstream csv(std::filesystem::path(directory) / "results.csv");
	ASSERT_TRUE(csv.good());
	csv << "dataset,codec,round,input_bytes,compressed_bytes,compress_seconds,decompress_seconds\n";
	csv << std::setprecision(12);
	std::cout << "zlib " << zlibVersion() << ", zstd " << ZSTD_versionString()
		<< ", bzip2 " << BZ2_bzlibVersion() << ", chunk " << chunkSize << std::endl;
	string name;
	while(std::getline(manifest, name)) {
		if(name.empty()) continue;
		SCOPED_TRACE(name);
		std::ifstream file(std::filesystem::path(directory) / name, std::ios::binary);
		ASSERT_TRUE(file.good()) << name;
		const string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		ASSERT_FALSE(data.empty());
		const size_t count = name == "filelist.xml" ? 6 : 5;
		std::vector<string> frames(count);
		string encoded(data.size() + data.size() / 100 + 1024 * 1024, '\0');
		string decoded(data.size(), '\0');
		// Warm each codec and validate full frames before timing.
		for(size_t i = 0; i < count; ++i) {
			SCOPED_TRACE(codecs[i].name);
			frames[i] = encoded.substr(0, codecs[i].encode(data, encoded));
			ASSERT_EQ(data.size(), codecs[i].decode(frames[i], decoded));
			ASSERT_TRUE(data == decoded) << name << ": " << codecs[i].name;
		}
		for(size_t round = 0; round < 7; ++round) {
			// Rotate execution order to reduce systematic warmup/thermal bias.
			for(size_t offset = 0; offset < count; ++offset) {
				const size_t i = (offset + round) % count;
				size_t encodedSize = 0, decodedSize = 0;
				const auto cs = timed([&] { encodedSize = codecs[i].encode(data, encoded); });
				ASSERT_EQ(frames[i].size(), encodedSize);
				ASSERT_EQ(0, encoded.compare(0, encodedSize, frames[i]));
				const auto ds = timed([&] { decodedSize = codecs[i].decode(frames[i], decoded); });
				ASSERT_EQ(data.size(), decodedSize);
				ASSERT_TRUE(data == decoded);
				csv << name << ',' << codecs[i].name << ',' << round << ',' << data.size()
					<< ',' << encodedSize << ',' << cs << ',' << ds << '\n';
			}
		}
		csv.flush();
		std::cout << "Completed " << name << " (" << data.size() << " bytes)" << std::endl;
	}
}
