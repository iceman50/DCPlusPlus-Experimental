/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdinc.h"
#include <dcpp/user/CID.h>

#include <dcpp/core/crypto/SecureRandom.h>

namespace dcpp {

CID CID::generate() {
	// The private ID is generated with this, and anyone who can predict it can sign in to ADC hubs
	// as its owner. Util::rand would make it a function of one 32-bit seed, which can be searched
	// from the public CID in under an hour.
	uint8_t data[CID::SIZE];
	SecureRandom::fill(data, sizeof(data));
	return CID(data);
}


}
