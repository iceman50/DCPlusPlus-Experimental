// Copyright (C) 2026 iceman50
// The client lock stores only a salted PBKDF2 hash of its password. These tests check that the
// right password matches, that nothing else does, and that a damaged setting never unlocks.
#include "testbase.h"

#include <chrono>
#include <iostream>

#include <dcpp/core/crypto/PasswordHash.h>

using namespace dcpp;

namespace {
// Keep most tests fast; one test below covers the real iteration count.
constexpr unsigned fast = 1000;
}

TEST(testpasswordhash, the_right_password_matches_and_others_do_not)
{
	const auto stored = PasswordHash::create("correct horse", fast);
	ASSERT_FALSE(stored.empty());
	EXPECT_TRUE(PasswordHash::isValid(stored));
	EXPECT_TRUE(PasswordHash::verify("correct horse", stored));
	EXPECT_FALSE(PasswordHash::verify("correct hors", stored));
	EXPECT_FALSE(PasswordHash::verify("correct horse ", stored));
	EXPECT_FALSE(PasswordHash::verify("Correct horse", stored));
	EXPECT_FALSE(PasswordHash::verify("", stored));
}

TEST(testpasswordhash, the_stored_form_names_the_scheme_and_never_contains_the_password)
{
	const auto stored = PasswordHash::create("hunter2", fast);
	EXPECT_EQ(0u, stored.find("pbkdf2-sha256$1000$"));
	EXPECT_EQ(string::npos, stored.find("hunter2"));
	// scheme, iterations, a 16-byte salt and a 32-byte hash, in base32
	EXPECT_EQ(string("pbkdf2-sha256$1000$").size() + 26 + 1 + 52, stored.size());
}

TEST(testpasswordhash, each_hash_gets_its_own_salt)
{
	const auto a = PasswordHash::create("same password", fast);
	const auto b = PasswordHash::create("same password", fast);
	EXPECT_NE(a, b);
	EXPECT_TRUE(PasswordHash::verify("same password", a));
	EXPECT_TRUE(PasswordHash::verify("same password", b));
}

TEST(testpasswordhash, non_ascii_passwords_are_hashed_as_utf8)
{
	const string password = "p\xC3\xA4ssw\xC3\xB6rd \xE5\xAF\x86\xE7\xA0\x81"; // "pässwörd 密码"
	const auto stored = PasswordHash::create(password, fast);
	EXPECT_TRUE(PasswordHash::verify(password, stored));
	EXPECT_FALSE(PasswordHash::verify("passw\xC3\xB6rd \xE5\xAF\x86\xE7\xA0\x81", stored));
}

TEST(testpasswordhash, damaged_or_foreign_hashes_never_match)
{
	const auto good = PasswordHash::create("secret", fast);
	ASSERT_TRUE(PasswordHash::verify("secret", good));

	const auto first = good.find('$');
	const auto second = good.find('$', first + 1);
	const auto third = good.find('$', second + 1);
	const auto salt = good.substr(second + 1, third - second - 1);
	const auto hash = good.substr(third + 1);

	const string damaged[] = {
		"",
		"secret",
		"pbkdf2-sha256",
		"pbkdf2-sha256$1000$" + salt,
		"pbkdf2-sha1$1000$" + salt + "$" + hash,
		"pbkdf2-sha256$$" + salt + "$" + hash,
		"pbkdf2-sha256$0$" + salt + "$" + hash,
		"pbkdf2-sha256$-5$" + salt + "$" + hash,
		"pbkdf2-sha256$ 1000$" + salt + "$" + hash,
		"pbkdf2-sha256$1000x$" + salt + "$" + hash,
		"pbkdf2-sha256$99999999999$" + salt + "$" + hash,
		"pbkdf2-sha256$10000001$" + salt + "$" + hash,
		"pbkdf2-sha256$1000$" + salt.substr(1) + "$" + hash,
		"pbkdf2-sha256$1000$" + salt + "A$" + hash,
		"pbkdf2-sha256$1000$" + salt + "$" + hash.substr(1),
		"pbkdf2-sha256$1000$" + salt + "$" + hash + "A",
		"pbkdf2-sha256$1000$" + salt + "$" + hash + "$",
		"pbkdf2-sha256$1000$" + salt + "$" + hash.substr(0, hash.size() - 1) + "1",
		"pbkdf2-sha256$1000$" + salt + "$" + hash.substr(0, hash.size() - 1) + "a",
		"PBKDF2-SHA256$1000$" + salt + "$" + hash,
		good + " ",
	};
	for(const auto& stored: damaged) {
		EXPECT_FALSE(PasswordHash::isValid(stored)) << stored;
		EXPECT_FALSE(PasswordHash::verify("secret", stored)) << stored;
	}
}

TEST(testpasswordhash, changing_any_part_of_the_hash_breaks_the_match)
{
	const auto good = PasswordHash::create("secret", fast);
	const auto saltStart = good.find('$', good.find('$') + 1) + 1;
	for(auto i = saltStart; i < good.size(); ++i) {
		if(good[i] == '$') continue;
		auto changed = good;
		changed[i] = changed[i] == 'A' ? 'B' : 'A';
		EXPECT_FALSE(PasswordHash::verify("secret", changed)) << "position " << i;
	}
}

TEST(testpasswordhash, the_iteration_count_is_read_from_the_stored_hash)
{
	// A hash made with another count still verifies, so the default can be raised later.
	const auto older = PasswordHash::create("secret", 2000);
	EXPECT_EQ(0u, older.find("pbkdf2-sha256$2000$"));
	EXPECT_TRUE(PasswordHash::verify("secret", older));

	// The same salt and hash under a different count don't match.
	auto relabelled = older;
	relabelled.replace(string("pbkdf2-sha256$").size(), 4, "2001");
	EXPECT_TRUE(PasswordHash::isValid(relabelled));
	EXPECT_FALSE(PasswordHash::verify("secret", relabelled));
}

TEST(testpasswordhash, out_of_range_iteration_counts_fall_back_to_the_default)
{
	EXPECT_EQ(0u, PasswordHash::create("secret", 0).find("pbkdf2-sha256$600000$"));
}

TEST(testpasswordhash, the_default_hash_is_slow_enough_to_matter_and_fast_enough_to_unlock)
{
	const auto start = std::chrono::steady_clock::now();
	const auto stored = PasswordHash::create("secret");
	const auto created = std::chrono::steady_clock::now();
	EXPECT_TRUE(PasswordHash::verify("secret", stored));
	const auto verified = std::chrono::steady_clock::now();

	using std::chrono::duration_cast;
	using std::chrono::milliseconds;
	const auto createMs = duration_cast<milliseconds>(created - start).count();
	const auto verifyMs = duration_cast<milliseconds>(verified - created).count();
	std::cout << "PBKDF2-SHA256 x" << PasswordHash::defaultIterations << ": create " << createMs
		<< " ms, verify " << verifyMs << " ms" << std::endl;
	EXPECT_EQ(0u, stored.find("pbkdf2-sha256$600000$"));
	// Unlocking waits for one verify on the interface thread.
	EXPECT_LT(verifyMs, 2000);
}
