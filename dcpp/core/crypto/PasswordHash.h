/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_PASSWORD_HASH_H
#define DCPLUSPLUS_DCPP_PASSWORD_HASH_H

#include <string>

namespace dcpp {

using std::string;

/** Salted, slow hashes for passwords that only ever need to be checked, never read back, such as
the client lock password. The stored form is "pbkdf2-sha256$<iterations>$<salt>$<hash>", with the
salt and the hash in base32, so the iteration count can be raised later without breaking hashes
that were stored before. */
namespace PasswordHash {

/** PBKDF2-HMAC-SHA256 rounds for new hashes, as OWASP recommends. Checking one takes a fraction
of a second, which is unnoticeable when unlocking but makes guessing slow. */
constexpr unsigned defaultIterations = 600000;

/** Iteration counts accepted from a stored hash. The upper bound keeps an edited settings file
from freezing the interface. */
constexpr unsigned minIterations = 1;
constexpr unsigned maxIterations = 10000000;

/** Hash a password with a new random salt. The password is UTF-8 and may be empty, although
callers should refuse empty passwords. */
string create(const string& password, unsigned iterations = defaultIterations);

/** Whether the password matches a hash made by create. Returns false for an empty or malformed
hash, so a damaged setting never unlocks anything. */
bool verify(const string& password, const string& stored) noexcept;

/** Whether the text is a hash create could have made. */
bool isValid(const string& stored) noexcept;

}

}

#endif
