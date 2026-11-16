/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_SECURE_RANDOM_H
#define DCPLUSPLUS_DCPP_SECURE_RANDOM_H

#include <cstddef>
#include <cstdint>

namespace dcpp {

/** Unpredictable random bytes for secrets and identifiers that others must not guess, such as
the private ID and connection tokens. Util::rand is a seeded std::mt19937 and must not be used
for these: its output follows from a single 32-bit seed. */
namespace SecureRandom {

/** Fill the buffer from OpenSSL's generator, or from the operating system if that fails. */
void fill(void* data, size_t size) noexcept;

uint64_t next64() noexcept;

}

}

#endif
