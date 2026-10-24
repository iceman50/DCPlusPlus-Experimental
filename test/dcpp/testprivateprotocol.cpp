/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "testbase.h"

#include <dcpp/PrivateTransferProtocol.h>
#include <thread>
#include <atomic>
#include <future>
#include <random>

using namespace dcpp;

namespace {

pft::Metadata metadata() {
	return { std::string(32, '1'), std::string(39, 'A'), std::string(38, 'B') + "A", std::string(64, '2'), std::string(64, '3'),
		"a file.txt", std::string(64, '4'), std::string(39, 'A'), 42, 900 };
}

pft::Authorization::Binding binding() {
	return { std::string(39, 'A'), std::string(64, '2'), std::string(32, '1'), std::string(64, '3'), std::string(32, '4') };
}
}

TEST(PrivateProtocol, offer_round_trip_and_metadata_commitment) {
	const auto m = metadata();
	auto line = m.record().encode();
	line.pop_back();

	// Independent Python hashlib/struct.pack big-endian reference vector.
	EXPECT_EQ("d582c02d69512034eb020287e043c6435495e39ed48b574b90c81784286449cb", m.digest());
	const auto r = pft::Record::decode(line);
	EXPECT_EQ(m.name, pft::Metadata::fromRecord(r).name);
	EXPECT_EQ(m.digest(), pft::Metadata::fromRecord(r).digest());
	auto altered = r;
	altered.fields[6] = "different.txt";
	EXPECT_THROW(pft::Metadata::fromRecord(altered), pft::ProtocolException);
	EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", pft::sha256("", 0));
}

TEST(PrivateProtocol, rejects_ambiguous_and_unbounded_records) {
	auto line = metadata().record().encode();
	line.pop_back();
	for(const auto& bad :
		{ line + " ", line + "\n", line + " extra", line + std::string(5000, 'x'), "B" + line.substr(1), std::string("CPFT 2 ACC x y z") })
		EXPECT_THROW(pft::Record::decode(bad), Exception);
	auto record = metadata().record();
	record.fields[7] = "042";
	EXPECT_THROW(record.encode(), Exception);
	record = metadata().record();
	record.fields[0][0] = 'A';
	EXPECT_THROW(record.encode(), Exception);
}

TEST(PrivateProtocol, numeric_bounds_are_checked_before_conversion) {
	for(const auto& bad : { "", "-1", "+1", "01", " 1", "1e3", "18446744073709551616" })
		EXPECT_THROW(pft::number(bad, UINT64_MAX), pft::ProtocolException);
	EXPECT_EQ(UINT64_MAX, pft::number("18446744073709551615", UINT64_MAX));
	EXPECT_THROW(pft::number("11", 10), pft::ProtocolException);
	EXPECT_EQ(0u, pft::number("0", 0));
}

TEST(PrivateProtocol, filenames_cannot_select_paths_or_hide_extensions) {
	for(const auto& name : { "../x", "x/y", "x\\y", "C:x", "x:stream", "CON.txt", "nul", "LPT1.a", "com\xc2\xb9.txt", ".", "..", "x.", "x ",
			"x\n.txt", "x\xe2\x80\xae.exe" })
		EXPECT_FALSE(pft::safeName(name)) << name;
	EXPECT_TRUE(pft::safeName("report 2026.txt"));
	EXPECT_TRUE(pft::safeName("caf\xc3\xa9.txt"));
	EXPECT_FALSE(pft::safeName("cafe\xcc\x81.txt")); // decomposed, not NFC
	EXPECT_FALSE(pft::safeName("x\xe2\x80\x8f.txt"));
	EXPECT_FALSE(pft::safeName("x\xd8\x9c.txt"));
	EXPECT_FALSE(pft::safeName(std::string(241, 'x')));
}

TEST(PrivateAuthorization, binds_peer_content_attempt_and_lifetime) {
	pft::Authorization a;
	const auto b = binding();
	const std::string secret(64, '5');
	a.issue(b, secret, 12, 30, 42, 100, 200);
	auto wrong = b;
	wrong.peerPin[0] = '6';
	EXPECT_THROW(a.consume(wrong, secret, 99), Exception);
	wrong = b;
	wrong.peerCID[0] = 'B';
	EXPECT_THROW(a.consume(wrong, secret, 99), Exception);
	wrong = b;
	wrong.offer[0] = '6';
	EXPECT_THROW(a.consume(wrong, secret, 99), Exception);
	wrong = b;
	wrong.digest[0] = '6';
	EXPECT_THROW(a.consume(wrong, secret, 99), Exception);
	wrong = b;
	wrong.attempt[0] = '6';
	EXPECT_THROW(a.consume(wrong, secret, 99), Exception);
	EXPECT_THROW(a.consume(b, std::string(64, '6'), 99), Exception);
	auto lease = a.consume(b, secret, 99);
	EXPECT_THROW(a.consume(b, secret, 99), Exception);
	EXPECT_TRUE(a.valid(lease, 199));
	EXPECT_FALSE(a.valid(lease, 200));
	a.revoke();
	EXPECT_FALSE(a.valid(lease, 100));
	a.issue(b, secret, 0, 42, 42, 100, 200);
	EXPECT_THROW(a.consume(b, secret, 100), Exception);
	EXPECT_FALSE(a.valid(lease, 99));
}

TEST(PrivateAuthorization, simultaneous_replay_has_exactly_one_winner) {
	pft::Authorization a;
	const auto b = binding();
	const std::string secret(64, '5');
	a.issue(b, secret, 0, 42, 42, 100, 200);
	std::atomic<int> winners { 0 };
	std::vector<std::thread> workers;
	for(int i = 0; i < 16; ++i)
		workers.emplace_back([&] {
			try {
				a.consume(b, secret, 1);
				++winners;
			} catch(const Exception&) {
			}
		});
	for(auto& worker : workers)
		worker.join();
	EXPECT_EQ(1, winners.load());
}

TEST(PrivateAuthorization, invalid_ranges_and_rotated_grants_fail) {
	pft::Authorization a;
	const auto b = binding();
	const std::string secret(64, '5');
	EXPECT_THROW(a.issue(b, secret, 43, 0, 42, 100, 200), Exception);
	EXPECT_THROW(a.issue(b, secret, 0, 41, 42, 100, 200), Exception);
	a.issue(b, secret, 0, 0, 0, 100, 200);
	auto old = a.consume(b, secret, 1);
	a.issue(b, std::string(64, '6'), 0, 0, 0, 100, 200);
	EXPECT_FALSE(a.valid(old, 1));
	EXPECT_THROW(a.consume(b, secret, 1), Exception);
	EXPECT_TRUE(a.valid(a.consume(b, std::string(64, '6'), 1), 1));
}

TEST(PrivateAuthorization, revoke_serializes_with_disk_reads) {
	pft::Authorization a;
	const auto b = binding();
	const std::string secret(64, '5');
	a.issue(b, secret, 0, 42, 42, pft::now() + 10000, pft::now() + 20000);
	auto lease = a.consume(b, secret, pft::now());
	std::promise<void> entered, release;
	auto released = release.get_future();
	auto reading = std::async(std::launch::async, [&] {
		a.read(lease, [&] {
			entered.set_value();
			released.wait();
		});
	});
	entered.get_future().wait();
	auto revoking = std::async(std::launch::async, [&] { a.revoke(); });
	EXPECT_EQ(std::future_status::timeout, revoking.wait_for(std::chrono::milliseconds(20)));
	release.set_value();
	reading.get();
	revoking.get();
	bool read = false;
	EXPECT_THROW(a.read(lease, [&] { read = true; }), Exception);
	EXPECT_FALSE(read);
}

TEST(PrivateProtocol, deterministic_wire_mutations_are_bounded_and_canonical) {
	auto source = metadata().record().encode();
	source.pop_back();
	std::mt19937 random(0x50465431);
	for(unsigned i = 0; i < 1000; ++i) {
		auto wire = source;
		const auto at = random() % wire.size();
		switch(i % 3) {
			case 0:
				wire[at] = static_cast<char>(random() % 256);
				break;

			case 1:
				wire.erase(at, 1);
				break;

			case 2:
				wire.insert(at, 1, static_cast<char>(random() % 256));
				break;
		}

		try {
			auto parsed = pft::Record::decode(wire);
			EXPECT_EQ(wire + '\n', parsed.encode());
		} catch(const Exception&) {
		}
	}
}
