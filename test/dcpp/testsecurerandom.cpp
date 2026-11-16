// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"

#include <set>

#include <dcpp/core/crypto/SecureRandom.h>
#include <dcpp/user/CID.h>
#include <dcpp/util/Util.h>

using namespace dcpp;

TEST(testsecurerandom, private_ids_do_not_follow_from_the_util_rand_seed)
{
	// The old CID::generate drew from Util::mt, so the same 32-bit seed always gave the same
	// private ID, and the ID could be searched from its public CID.
	Util::mt.seed(1234);
	const auto first = CID::generate();
	Util::mt.seed(1234);
	const auto second = CID::generate();
	EXPECT_FALSE(first == second);
	EXPECT_TRUE(first);
	EXPECT_TRUE(second);
}

TEST(testsecurerandom, fills_whole_buffers_with_varying_values)
{
	std::set<uint64_t> values;
	for(int i = 0; i < 64; ++i)
		values.insert(SecureRandom::next64());
	EXPECT_EQ(64u, values.size());

	// Every byte position, including the last partial word, receives random data.
	unsigned char data[37];
	bool changed[sizeof(data)] = {};
	for(int round = 0; round < 32; ++round) {
		std::fill(std::begin(data), std::end(data), 0);
		SecureRandom::fill(data, sizeof(data));
		for(size_t i = 0; i < sizeof(data); ++i)
			changed[i] = changed[i] || data[i] != 0;
	}
	EXPECT_TRUE(std::all_of(std::begin(changed), std::end(changed), [](bool b) { return b; }));
}
