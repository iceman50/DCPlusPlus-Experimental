// Copyright (C) 2026 iceman50
#include "testbase.h"

#include <dcpp/util/text/Text.h>
#include <dcpp/util/Util.h>

using namespace dcpp;

TEST(testtext, test_tolower)
{
	Text::initialize();

	ASSERT_EQ("abcd1", Text::toLower("ABCd1"));

#ifdef _WIN32
	ASSERT_EQ(_T("abcd1"), Text::toLower(_T("ABCd1")));
#endif

	ASSERT_EQ('a', Text::toLower('A'));
}

TEST(testtext, test_trim_without_system_locale)
{
	string narrow = " \t\r\nvalue\f\v ";
	Util::trim(narrow);
	ASSERT_EQ("value", narrow);

	std::wstring wide = L" \t\r\nvalue\f\v ";
	Util::trim(wide);
	ASSERT_EQ(L"value", wide);
}

TEST(testtext, nmdc_text_uses_only_the_configured_charset)
{
	const string cp1251Nick("\xC2\xE0\xF1\xFF");               // "Vasya" in CP1251
	const string utf8Nick("\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F"); // the same name in UTF-8

	EXPECT_TRUE(Text::isUtf8Charset("utf-8"));
	EXPECT_TRUE(Text::isUtf8Charset("UTF-8"));
	EXPECT_FALSE(Text::isUtf8Charset("1251 (ANSI - Cyrillic)"));
	EXPECT_FALSE(Text::isUtf8Charset("English_United States.1252"));
#ifdef _WIN32
	EXPECT_TRUE(Text::isUtf8Charset("65001 (UTF-8)"));
#endif

	// Valid UTF-8 on a CP1251 hub is still CP1251 text, so it cannot pose as the CP1251 nick.
	EXPECT_EQ(utf8Nick, Text::nmdcToUtf8(cp1251Nick, "1251 (ANSI - Cyrillic)"));
	EXPECT_NE(utf8Nick, Text::nmdcToUtf8(utf8Nick, "1251 (ANSI - Cyrillic)"));

	// UTF-8 hubs pass valid text through and replace invalid bytes.
	EXPECT_EQ(utf8Nick, Text::nmdcToUtf8(utf8Nick, "utf-8"));
	EXPECT_EQ("a\xEF\xBF\xBD" "b", Text::nmdcToUtf8("a\xFF" "b", "utf-8"));
#ifdef _WIN32
	EXPECT_EQ(utf8Nick, Text::nmdcToUtf8(utf8Nick, "65001 (UTF-8)"));
	EXPECT_EQ("a\xEF\xBF\xBD" "b", Text::nmdcToUtf8("a\xFF" "b", "65001 (UTF-8)"));
#endif
}
