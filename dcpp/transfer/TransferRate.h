/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_TRANSFER_RATE_H
#define DCPLUSPLUS_DCPP_TRANSFER_RATE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dcpp { namespace TransferRate {

// Settings remain in KiB/s. Raising this policy limit is a separate change.
constexpr int MAX_LIMIT_KIB = 1024 * 1024; // 1 GiB/s

constexpr int64_t fromKiB(int rate) noexcept {
	return static_cast<int64_t>(rate) * 1024;
}

// Preserve the existing binary megabit conversion used for ADC upload speeds.
inline int64_t fromMibit(double rate) noexcept {
	if(!std::isfinite(rate) || rate <= 0)
		return 0;

	constexpr auto maxRate = std::numeric_limits<int64_t>::max();
	constexpr double scale = 1024.0 * 1024.0 / 8;
	if(rate >= static_cast<double>(maxRate) / scale)
		return maxRate;
	return static_cast<int64_t>(rate * scale);
}

// Socket and SSL operations take a signed int, even on 64-bit builds.
inline int socketChunkSize(uint64_t bytes) noexcept {
	return static_cast<int>(std::min<uint64_t>(bytes, std::numeric_limits<int>::max()));
}

} } // namespace dcpp::TransferRate

#endif
