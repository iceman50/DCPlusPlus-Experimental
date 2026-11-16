/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include <dcpp/core/crypto/SecureRandom.h>

#include <climits>
#include <cstring>
#include <random>

#include <openssl/rand.h>

namespace dcpp {
namespace SecureRandom {

void fill(void* data, size_t size) noexcept {
	auto bytes = static_cast<unsigned char*>(data);
	while(size > 0) {
		const auto chunk = static_cast<int>(std::min<size_t>(size, INT_MAX));
		if(RAND_bytes(bytes, chunk) != 1) {
			// std::random_device reads the operating system's generator on supported platforms.
			std::random_device dev;
			for(int i = 0; i < chunk; i += static_cast<int>(sizeof(unsigned int))) {
				const auto value = dev();
				std::memcpy(bytes + i, &value, std::min<size_t>(sizeof(value), static_cast<size_t>(chunk - i)));
			}
		}
		bytes += chunk;
		size -= static_cast<size_t>(chunk);
	}
}

uint64_t next64() noexcept {
	uint64_t value = 0;
	fill(&value, sizeof(value));
	return value;
}

}
}
