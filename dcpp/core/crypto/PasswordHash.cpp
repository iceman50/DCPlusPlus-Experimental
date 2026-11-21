/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include <dcpp/core/crypto/PasswordHash.h>

#include <dcpp/core/crypto/SecureRandom.h>
#include <dcpp/hash/value/Encoder.h>

#include <array>
#include <cstdint>
#include <cstdlib>

#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace dcpp {
namespace PasswordHash {

namespace {

const string scheme = "pbkdf2-sha256";
constexpr size_t saltBytes = 16;
constexpr size_t hashBytes = 32;

typedef std::array<uint8_t, saltBytes> Salt;
typedef std::array<uint8_t, hashBytes> Hash;

bool derive(const string& password, const Salt& salt, unsigned iterations, Hash& out) noexcept {
	return PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
		salt.data(), static_cast<int>(salt.size()), static_cast<int>(iterations), EVP_sha256(),
		static_cast<int>(out.size()), out.data()) == 1;
}

bool decodeExact(const string& text, uint8_t* out, size_t bytes) {
	// Base32 of n bytes is ceil(8n / 5) characters; anything else was not written by create.
	if(text.size() != (bytes * 8 + 4) / 5 || !Encoder::isBase32(text)) {
		return false;
	}
	Encoder::fromBase32(text.c_str(), out, bytes);
	// The last character carries unused bits; accept only the form create writes, so each
	// stored hash has exactly one spelling.
	return Encoder::toBase32(out, bytes) == text;
}

struct Parsed {
	unsigned iterations = 0;
	Salt salt { };
	Hash hash { };
};

bool parse(const string& stored, Parsed& parsed) noexcept {
	try {
		// scheme$iterations$salt$hash
		const auto a = stored.find('$');
		if(a == string::npos || stored.compare(0, a, scheme) != 0) return false;
		const auto b = stored.find('$', a + 1);
		if(b == string::npos) return false;
		const auto c = stored.find('$', b + 1);
		if(c == string::npos || stored.find('$', c + 1) != string::npos) return false;

		const auto count = stored.substr(a + 1, b - a - 1);
		if(count.empty() || count.size() > 9 || count.find_first_not_of("0123456789") != string::npos) return false;
		const auto iterations = std::strtoul(count.c_str(), nullptr, 10);
		if(iterations < minIterations || iterations > maxIterations) return false;
		parsed.iterations = static_cast<unsigned>(iterations);

		return decodeExact(stored.substr(b + 1, c - b - 1), parsed.salt.data(), parsed.salt.size()) &&
			decodeExact(stored.substr(c + 1), parsed.hash.data(), parsed.hash.size());
	} catch(...) {
		return false;
	}
}

}

string create(const string& password, unsigned iterations) {
	if(iterations < minIterations || iterations > maxIterations) {
		iterations = defaultIterations;
	}

	Salt salt;
	SecureRandom::fill(salt.data(), salt.size());
	Hash hash;
	if(!derive(password, salt, iterations, hash)) {
		return string();
	}

	auto stored = scheme + '$' + std::to_string(iterations) + '$' +
		Encoder::toBase32(salt.data(), salt.size()) + '$' + Encoder::toBase32(hash.data(), hash.size());
	OPENSSL_cleanse(hash.data(), hash.size());
	return stored;
}

bool verify(const string& password, const string& stored) noexcept {
	Parsed parsed;
	if(!parse(stored, parsed)) {
		return false;
	}

	Hash candidate;
	if(!derive(password, parsed.salt, parsed.iterations, candidate)) {
		return false;
	}
	// Compare in constant time so the comparison doesn't reveal how much of the hash matched.
	const bool match = CRYPTO_memcmp(candidate.data(), parsed.hash.data(), candidate.size()) == 0;
	OPENSSL_cleanse(candidate.data(), candidate.size());
	return match;
}

bool isValid(const string& stored) noexcept {
	Parsed parsed;
	return parse(stored, parsed);
}

}
}
