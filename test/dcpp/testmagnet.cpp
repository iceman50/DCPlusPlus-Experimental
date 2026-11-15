// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"
#include <dcpp/core/classes/Magnet.h>
#include <dcpp/hash/value/MerkleTree.h>
#include <limits>

using namespace dcpp;
namespace {
const string hash = "LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ";
const string prefix = "magnet:?xt=urn:tree:tiger:" + hash;
}

TEST(MagnetMetadata, PreservesCompleteAndIncompleteInformation) {
	Magnet::Metadata data;
	ASSERT_TRUE(Magnet::parseUri(prefix + "&dn=a%20file%20%26%20name.txt&xl=123", data));
	EXPECT_EQ(hash, data.hash);
	EXPECT_EQ("a file & name.txt", data.name);
	EXPECT_EQ(123, data.size);
	EXPECT_TRUE(data.canDownload());
	ASSERT_TRUE(Magnet::parseUri(prefix, data));
	EXPECT_EQ(-1, data.size);
	EXPECT_FALSE(data.canDownload());
	ASSERT_TRUE(Magnet::parseUri(prefix + "&xl=0", data));
	EXPECT_EQ(0, data.size);
	EXPECT_TRUE(data.canDownload());
	ASSERT_TRUE(Magnet::parseUri("magnet:?kt=some+search+words", data));
	EXPECT_EQ("some search words", data.key);
	EXPECT_TRUE(data.hash.empty());
	EXPECT_FALSE(data.canDownload());
}

TEST(MagnetMetadata, SupportsLegacyTopicsAndHashPriority) {
	Magnet::Metadata data;
	for(const auto topic : { "urn:tree:tiger:", "urn:tree:tiger/:", "urn:tree:tiger/1024:" }) {
		ASSERT_TRUE(Magnet::parseUri("MAGNET:?XT=" + string(topic) + hash, data));
		EXPECT_EQ(hash, data.hash);
	}
	ASSERT_TRUE(Magnet::parseUri("magnet:?xt=urn:bitprint:" + string(32, 'A') + "." + hash, data));
	EXPECT_EQ(hash, data.hash);
	ASSERT_TRUE(Magnet::parseUri(prefix + "&xs=urn:tree:tiger:" + string(39, 'A'), data));
	EXPECT_EQ(hash, data.hash);
	ASSERT_TRUE(Magnet::parseUri("magnet:?xt=urn:btih:other-protocol&xs=urn:tree:tiger:" + hash, data));
	EXPECT_EQ(hash, data.hash);
	string lower = hash;
	for(auto& c : lower) if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
	ASSERT_TRUE(Magnet::parseUri("magnet:?xt=urn:tree:tiger:" + lower, data));
	EXPECT_EQ(hash, data.hash);
}

TEST(MagnetMetadata, RejectsMalformedAndConflictingFieldsWithoutStaleResults) {
	Magnet::Metadata data;
	for(const auto& uri : {
		prefix + "&xl=-1", prefix + "&xl=1.5", prefix + "&xl=",
		prefix + "&xl=9223372036854775808", prefix + "&xl=12&xl=13",
		prefix + "&dn=bad%", prefix + "&dn=bad%0G", prefix + "&dn=bad%00name",
		prefix + "&xt=urn:tree:tiger:" + string(39, 'A'),
		string("magnet:?xt=urn:tree:tiger:") + string(39, '0'),
		string("magnet:?xt=urn:tree:tiger:"), string("magnet:?kt=%20%20"),
		string("magnet:?dn=only-a-name"), string("https://example.invalid/") }) {
		ASSERT_TRUE(Magnet::parseUri(prefix + "&dn=previous&xl=1", data));
		EXPECT_FALSE(Magnet::parseUri(uri, data)) << uri;
		EXPECT_TRUE(data.hash.empty());
		EXPECT_TRUE(data.name.empty());
		EXPECT_EQ(-1, data.size);
	}
	ASSERT_TRUE(Magnet::parseUri(prefix + "&xl=9223372036854775807", data));
	EXPECT_EQ(std::numeric_limits<int64_t>::max(), data.size);
	string h = hash, name = "stale", key = "stale";
	EXPECT_FALSE(Magnet::parseUri("not a magnet", h, name, key));
	EXPECT_TRUE(h.empty() && name.empty() && key.empty());
}

TEST(MagnetMetadata, SuggestedDestinationIsOnlyAFilename) {
	Magnet::Metadata data;
	ASSERT_TRUE(Magnet::parseUri(prefix + "&dn=..%2F..%2Ftest.txt", data));
	EXPECT_EQ("test.txt", data.suggestedName());
	data.name = "C:\\outside\\test.txt:stream";
	EXPECT_EQ("test.txt_stream", data.suggestedName());
	data.name = "CON.txt";
	EXPECT_EQ("_CON.txt", data.suggestedName());
	data.name = ".. ";
	EXPECT_EQ(hash, data.suggestedName());
}
