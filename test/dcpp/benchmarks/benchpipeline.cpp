// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"
#include <dcpp/system/File.h>
#include <dcpp/system/FileReader.h>
#include <dcpp/hash/TigerTreeHasher.h>
#include <dcpp/hash/HashManager.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace dcpp;
namespace {
using Clock = std::chrono::steady_clock;
struct Corpus {
	const std::filesystem::path parent = std::filesystem::weakly_canonical("build");
	const std::filesystem::path root = parent / ("pipeline-profile-" + std::to_string(Clock::now().time_since_epoch().count()));
	Corpus() {
		if(!std::filesystem::create_directory(root)) throw std::runtime_error("Benchmark directory already exists");
	}
	~Corpus() {
		std::error_code error;
		const auto resolved = std::filesystem::weakly_canonical(root, error);
		if(!error && resolved.parent_path() == parent && resolved.filename() == root.filename()) {
			std::filesystem::remove_all(root, error);
		}
	}
	string make(const char* name, size_t size) {
		const auto path = (root / name).string();
		ByteVector block(std::min<size_t>(size, 1024 * 1024));
		uint32_t state = 1234567;
		for(auto& byte: block) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; byte = static_cast<uint8_t>(state); }
		File file(path, File::WRITE, File::CREATE | File::TRUNCATE);
		for(size_t pos = 0; pos < size; pos += block.size()) file.write(block.data(), std::min(block.size(), size - pos));
		file.flush();
		return path;
	}
};
template<class Operation>
void measure(const char* area, size_t bytes, size_t parameter, Operation operation) {
	operation(); // Explicit warm-cache measurement; never report this as physical disk throughput.
	std::vector<double> times;
	for(int i = 0; i < 7; ++i) {
		auto start = Clock::now();
		operation();
		times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
	}
	std::sort(times.begin(), times.end());
	std::cout << area << ',' << bytes << ',' << parameter << ',' << times[3] << ',' << times.back() << '\n';
}
}

TEST(PipelineBenchmark, DISABLED_cached_reads_and_hashing) {
	Corpus corpus;
	std::cout << "area,bytes,parameter,median_ms,max_ms\n";
	for(size_t bytes: {size_t(4096), size_t(1024 * 1024), size_t(128 * 1024 * 1024)}) {
		const auto path = corpus.make("input.bin", bytes);
		const size_t repetitions = bytes == 4096 ? 1000 : 1;
		measure("cached_read_new_reader", bytes, repetitions, [&] {
			for(size_t i = 0; i < repetitions; ++i) {
				File file(path, File::READ, File::OPEN);
				size_t observed = 0;
				EXPECT_EQ(bytes, FileReader(false).read(file, [&](const void*, size_t count) { observed += count; return true; }));
				EXPECT_EQ(bytes, observed);
			}
		});
		TigerTree expected(std::max(TigerTree::calcBlockSize(bytes, 10), HashManager::MIN_BLOCK_SIZE));
		File reference(path, File::READ, File::OPEN);
		FileReader(false).read(reference, [&](const void* data, size_t count) { expected.update(data, count); return true; });
		expected.finalize();
		for(size_t workers: {size_t(1), size_t(4), size_t(8)}) {
			measure("hash", bytes, workers, [&] {
				File file(path, File::READ, File::OPEN);
				TigerTree actual;
				// Match HashManager's serial fallback and minimum TTH block size.
				if(TigerTreeHasher::getWorkerCount(workers, bytes, expected.getBlockSize()) == 1) {
					actual = TigerTree(expected.getBlockSize());
					FileReader(true).read(file, [&](const void* data, size_t count) { actual.update(data, count); return true; });
					actual.finalize();
				} else {
					ASSERT_TRUE(TigerTreeHasher::hash(file, bytes, expected.getBlockSize(), workers, actual, {}, {}));
				}
				EXPECT_EQ(expected.getRoot(), actual.getRoot());
				EXPECT_EQ(expected.getLeaves(), actual.getLeaves());
			});
		}
	}
}
