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

#ifndef DCPLUSPLUS_DCPP_STRING_SEARCH_H
#define DCPLUSPLUS_DCPP_STRING_SEARCH_H

#include <string.h>

#include <dcpp/util/text/Text.h>

namespace dcpp {

/**
 * A class that implements a fast substring search algo suited for matching
 * one pattern against many strings (currently Quick Search, a variant of
 * Boyer-Moore. Code based on "A very fast substring search algorithm" by
 * D. Sunday).
 * @todo Perhaps find an algo suitable for matching multiple substrings.
 */
class StringSearch {
public:
	typedef vector<StringSearch> List;

	StringSearch() { }
	StringSearch(const string& aPattern) noexcept : pattern(Text::toLower(aPattern)) {
		initDelta1();
	}
	StringSearch(const StringSearch& rhs) noexcept : pattern(rhs.pattern) {
		memcpy(delta1, rhs.delta1, sizeof(delta1));
	}
	StringSearch& operator=(const StringSearch& rhs) {
		memcpy(delta1, rhs.delta1, sizeof(delta1));
		pattern = rhs.pattern;
		return *this;
	}
	StringSearch& operator=(const string& rhs) {
		pattern = Text::toLower(rhs);
		initDelta1();
		return *this;
	}

	bool operator==(const StringSearch& rhs) { return pattern == rhs.pattern; }

	const string& getPattern() const { return pattern; }

	/** Match a text against the pattern */
	bool match(const string& aText) const noexcept {
		const string::size_type plen = pattern.length();
		if(aText.length() < plen) {
			return false;
		}

		// Text::toLower maps ASCII letters A-Z to a-z and leaves other ASCII as is (CharLowerW does
		// so in every locale), so an ASCII text - most file names - is compared in place. Searches
		// match every shared name against every term; lowercasing each into a new string made a
		// search of a large share mostly memory allocation.
		const auto* text = reinterpret_cast<const uint8_t*>(aText.c_str());
		bool ascii = true;
		for(string::size_type i = 0; i < aText.length(); ++i) {
			if(text[i] >= 0x80) {
				ascii = false;
				break;
			}
		}
		if(ascii) {
			return find(text, aText.length(), [](uint8_t c) { return static_cast<uint8_t>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c); });
		}

		// Lower-case representation of UTF-8 string, since we no longer have that 1 char = 1 byte...
		static thread_local string lower;
		lower.clear();
		Text::toLower(aText, lower);
		return find(reinterpret_cast<const uint8_t*>(lower.c_str()), lower.length(), [](uint8_t c) { return c; });
	}

private:
	/** Sunday's quick search of a null-terminated text, each text byte passed through lower(). */
	template<typename Lower>
	bool find(const uint8_t* tx, string::size_type length, Lower lower) const noexcept {
		const auto* px = reinterpret_cast<const uint8_t*>(pattern.c_str());
		const string::size_type plen = pattern.length();
		if(length < plen) {
			return false;
		}

		const uint8_t* end = tx + length - plen + 1;
		while(tx < end) {
			size_t i = 0;
			for(; px[i] && (px[i] == lower(tx[i])); ++i)
				;		// Empty!

			if(px[i] == 0)
				return true;

			// tx[plen] is at most the terminating null.
			tx += delta1[lower(tx[plen])];
		}

		return false;
	}

	enum { ASIZE = 256 };
	/**
	 * Delta1 shift, uint16_t because we expect all patterns to be shorter than 2^16
	 * chars.
	 */
	uint16_t delta1[ASIZE];
	string pattern;

	void initDelta1() {
		uint16_t x = (uint16_t)(pattern.length() + 1);
		uint16_t i;
		for(i = 0; i < ASIZE; ++i) {
			delta1[i] = x;
		}
		// x = pattern.length();
		x--;
		uint8_t* p = (uint8_t*)pattern.data();
		for(i = 0; i < x; ++i) {
			delta1[p[i]] = (uint16_t)(x - i);
		}
	}
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_STRING_SEARCH_H
