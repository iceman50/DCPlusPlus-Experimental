// Copyright (C) 2026 iceman50
// The ASCII fast paths in Util::stricmp/strnicmp, noCaseStringHash and StringSearch::match must give
// exactly the results of the character-by-character Unicode algorithms they replaced.
#include "testbase.h"

#include <random>

#include <dcpp/util/Util.h>
#include <dcpp/util/text/StringSearch.h>
#include <dcpp/util/text/Text.h>

using namespace dcpp;

namespace {

int referenceStricmp(const char* a, const char* b) {
	wchar_t ca = 0, cb = 0;
	while(*a) {
		ca = cb = 0;
		int na = Text::utf8ToWc(a, ca);
		int nb = Text::utf8ToWc(b, cb);
		ca = Text::toLower(ca);
		cb = Text::toLower(cb);
		if(ca != cb) {
			return (int)ca - (int)cb;
		}
		a += abs(na);
		b += abs(nb);
	}
	ca = cb = 0;
	Text::utf8ToWc(a, ca);
	Text::utf8ToWc(b, cb);
	return (int)Text::toLower(ca) - (int)Text::toLower(cb);
}

int referenceStrnicmp(const char* a, const char* b, size_t n) {
	const char* end = a + n;
	wchar_t ca = 0, cb = 0;
	while(*a && a < end) {
		ca = cb = 0;
		int na = Text::utf8ToWc(a, ca);
		int nb = Text::utf8ToWc(b, cb);
		ca = Text::toLower(ca);
		cb = Text::toLower(cb);
		if(ca != cb) {
			return (int)ca - (int)cb;
		}
		a += abs(na);
		b += abs(nb);
	}
	ca = cb = 0;
	Text::utf8ToWc(a, ca);
	Text::utf8ToWc(b, cb);
	return (a >= end) ? 0 : ((int)Text::toLower(ca) - (int)Text::toLower(cb));
}

size_t referenceHash(const string& s) {
	size_t x = 0;
	auto end = s.data() + s.size();
	for(auto str = s.data(); str < end; ) {
		wchar_t c = 0;
		int n = Text::utf8ToWc(str, c);
		if(n < 0) {
			x = x * 32 - x + '_';
			str += abs(n);
		} else {
			x = x * 32 - x + static_cast<size_t>(Text::toLower(c));
			str += n;
		}
	}
	return x;
}

bool referenceMatch(const string& pattern, const string& text) {
	const auto lower = Text::toLower(text);
	return lower.find(pattern) != string::npos;
}

int sign(int value) {
	return (value > 0) - (value < 0);
}

// Names mixing ASCII (both cases), accented and Turkish letters, other scripts, and invalid UTF-8.
string randomName(std::mt19937& rng) {
	static const char* const pieces[] = {
		"a", "B", "z", "Z", "0", "9", " ", "-", ".", "_", "[", "@", "`", "{", "~",
		"I", "i", "\xC4\xB0" /* İ */, "\xC4\xB1" /* ı */, "\xC3\x89" /* É */, "\xC3\xA9" /* é */,
		"\xC3\x84" /* Ä */, "\xC3\xA4" /* ä */, "\xCE\xA3" /* Σ */, "\xCF\x83" /* σ */,
		"\xE6\x97\xA5" /* 日 */, "\xF0\x9F\x8E\xB5" /* 🎵 */, "\xFF" /* invalid */, "\xC3" /* truncated */,
	};
	std::uniform_int_distribution<size_t> length(0, 12), piece(0, sizeof(pieces) / sizeof(pieces[0]) - 1);
	string name;
	for(auto n = length(rng); n > 0; --n) {
		name += pieces[piece(rng)];
	}
	return name;
}

}

TEST(StringCompare, ascii_fast_paths_agree_with_the_unicode_algorithms) {
	std::mt19937 rng(12345);
	for(int i = 0; i < 20000; ++i) {
		auto a = randomName(rng);
		auto b = (i % 4 == 0) ? a : randomName(rng);
		if(i % 7 == 0) {
			// Same text in another case.
			b = Text::toLower(a);
		}
		ASSERT_EQ(sign(referenceStricmp(a.c_str(), b.c_str())), sign(Util::stricmp(a.c_str(), b.c_str()))) << a << " | " << b;
		const auto n = std::uniform_int_distribution<size_t>(0, a.size() + 2)(rng);
		ASSERT_EQ(sign(referenceStrnicmp(a.c_str(), b.c_str(), n)), sign(Util::strnicmp(a.c_str(), b.c_str(), n))) << a << " | " << b << " | " << n;
		ASSERT_EQ(referenceHash(a), noCaseStringHash()(a)) << a;
		ASSERT_EQ(referenceHash(b), noCaseStringHash()(b)) << b;
	}
}

TEST(StringCompare, string_search_agrees_with_lowercase_find) {
	std::mt19937 rng(54321);
	for(int i = 0; i < 20000; ++i) {
		const auto text = randomName(rng) + randomName(rng);
		string term = randomName(rng);
		if(i % 3 == 0 && text.size() > 2) {
			// A piece of the text itself, in another case.
			const auto start = std::uniform_int_distribution<size_t>(0, text.size() - 1)(rng);
			term = text.substr(start, std::uniform_int_distribution<size_t>(1, text.size() - start)(rng));
		}
		const StringSearch search(term);
		ASSERT_EQ(referenceMatch(search.getPattern(), text), search.match(text)) << term << " | " << text;
	}
	EXPECT_TRUE(StringSearch("matrix").match("The.MATRIX.1999.mkv"));
	EXPECT_TRUE(StringSearch("\xC3\xA9t\xC3\xA9").match("L'\xC3\x89T\xC3\x89 indien"));
	EXPECT_FALSE(StringSearch("summer").match("winter.flac"));
	EXPECT_TRUE(StringSearch("").match("anything"));
}
