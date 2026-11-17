// Copyright (C) 2026 iceman50
// Names and messages from other users must not reorder or break the text they are shown in
// (DC++ bug 1425276: directional formatting characters, and line breaks used to imitate chat).
#include "testbase.h"

#include <dcpp/message/ChatMessage.h>
#include <dcpp/message/EmoticonManager.h>
#include <dcpp/message/RichText.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/user/CID.h>
#include <dcpp/user/OnlineUser.h>
#include <dcpp/user/User.h>
#include <dcpp/util/Util.h>
#include <dcpp/util/text/Text.h>

using namespace dcpp;

namespace {

const string RLO = "\xe2\x80\xae", LRE = "\xe2\x80\xaa", PDF = "\xe2\x80\xac", RLI = "\xe2\x81\xa7",
	PDI = "\xe2\x81\xa9", LS = "\xe2\x80\xa8", PS = "\xe2\x80\xa9", NEL = "\xc2\x85", LRM = "\xe2\x80\x8e",
	RLM = "\xe2\x80\x8f", ALM = "\xd8\x9c", FFFD = "\xef\xbf\xbd";

}

TEST(testdisplaytext, display_names_show_formatting_and_line_breaks_as_replacement_characters)
{
	EXPECT_EQ("Mallory" + FFFD + "gpj.exe", Text::toDisplayName("Mallory" + RLO + "gpj.exe"));
	EXPECT_EQ(FFFD + "x" + FFFD + FFFD + "y" + FFFD, Text::toDisplayName(LRE + "x" + PDF + RLI + "y" + PDI));
	// A CRLF pair is one line break; every other control character is replaced on its own.
	EXPECT_EQ("Mallory>" + FFFD + "[12:00] <Admin", Text::toDisplayName("Mallory>\r\n[12:00] <Admin"));
	EXPECT_EQ("a" + FFFD + "b" + FFFD + "c" + FFFD + "d" + FFFD + "e", Text::toDisplayName("a\rb\vc\fd\te"));
	EXPECT_EQ("a" + FFFD + "b" + FFFD + "c" + FFFD + "d", Text::toDisplayName("a" + LS + "b" + PS + "c" + NEL + "d"));
	EXPECT_EQ("a" + FFFD + "b" + FFFD + "c", Text::toDisplayName("a\x7f" "b\xc2\x9b" "c"));
}

TEST(testdisplaytext, display_names_keep_ordinary_text_and_implicit_direction_marks)
{
	// Right-to-left letters, the implicit marks they rely on, and punctuation that looks like
	// slashes, colons or fractions are left alone.
	const string hebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", arabic = "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
	for(const auto& name: { hebrew + LRM + "1", arabic + RLM + ALM, string("1\xe2\x81\x84" "2 \xe2\x88\x95 a\xe2\x88\xb6" "b"),
		string("\xc2\xbd \xc3\xa9 \xf0\x9f\x98\x80 [x] <y> *z*"), string("\xe2\x80\xaf\xe2\x81\xa5\xe2\x81\xaa") })
	{
		EXPECT_EQ(name, Text::toDisplayName(name));
	}
	// Malformed UTF-8 is not mistaken for a control character.
	EXPECT_EQ(string("\xc2" "A"), Text::toDisplayName("\xc2" "A"));
	EXPECT_EQ(string("x\xe2\x80"), Text::toDisplayName("x\xe2\x80"));
}

TEST(testdisplaytext, line_breaks_of_every_kind_become_line_feeds)
{
	EXPECT_EQ("a\nb\nc\nd\ne\nf\ng\nh", Text::normalizeLineBreaks("a\r\nb\rc\vd\fe" + NEL + "f" + LS + "g" + PS + "h"));
	EXPECT_EQ("\n\n", Text::normalizeLineBreaks("\r\r"));
	EXPECT_EQ("tab\tstays", Text::normalizeLineBreaks("tab\tstays"));
}

TEST(testdisplaytext, downloaded_file_names_show_where_formatting_characters_were)
{
	EXPECT_EQ("C:\\Downloads\\photo" + FFFD + "gpj.exe", Util::validateFileName("C:\\Downloads\\photo" + RLO + "gpj.exe"));
	EXPECT_EQ("C:\\Downloads\\" + FFFD + "a" + FFFD + "b" + FFFD + ".txt",
		Util::validateFileName("C:\\Downloads\\" + RLI + "a" + LS + "b" + PS + ".txt"));
	// Control characters keep their existing replacement, and implicit marks stay.
	EXPECT_EQ("C:\\Downloads\\a_b_c.txt", Util::validateFileName("C:\\Downloads\\a\rb\nc.txt"));
	EXPECT_EQ("C:\\Downloads\\" + LRM + "a.txt", Util::validateFileName("C:\\Downloads\\" + LRM + "a.txt"));
}

class testdisplaychat : public testing::Test {
protected:
	void SetUp() override {
		SettingsManager::newInstance();
		SettingsManager::getInstance()->set(SettingsManager::TIME_STAMPS, false);
		PluginManager::newInstance();
	}
	void TearDown() override {
		PluginManager::deleteInstance();
		EmoticonManager::reload();
		SettingsManager::deleteInstance();
	}

	static ChatMessage message(const string& nick, const string& text, bool thirdPerson = false, bool richText = false) {
		UserPtr user(new User(CID::generate()));
		Identity identity(user, 1);
		identity.setNick(nick);
		return ChatMessage(text, identity, nullptr, nullptr, thirdPerson, 0, richText, "me", nullptr);
	}
};

TEST_F(testdisplaychat, nicks_cannot_reorder_or_break_the_chat_line)
{
	auto m = message("Mallory>\r[12:00] <Admin", "please send your password");
	EXPECT_EQ("<Mallory>" + FFFD + "[12:00] <Admin> please send your password", m.message);
	EXPECT_NE(string::npos, m.htmlMessage.find("&lt;Mallory&gt;" + FFFD + "[12:00] &lt;Admin&gt;</span>"));
	// The sender's real nick is kept for everything other than display.
	EXPECT_EQ("Mallory>\r[12:00] <Admin", m.nick);

	EXPECT_EQ("<Mallory" + FFFD + "gpj.exe> hello", message("Mallory" + RLO + "gpj.exe", "hello").message);
	// Action messages have always had two spaces after the nick.
	EXPECT_EQ("* Mallory" + FFFD + "x  waves", message("Mallory" + LS + "x", "waves", true).message);
}

TEST_F(testdisplaychat, every_line_break_is_checked_for_imitated_chat_lines)
{
	for(const auto& lineBreak: { string("\n"), string("\r"), string("\r\n"), string("\v"), string("\f"), NEL, LS, PS }) {
		EXPECT_EQ("<Mallory> hi\n- [12:00] <Admin> send your password",
			message("Mallory", "hi" + lineBreak + "[12:00] <Admin> send your password").message);
	}
	// Indented lookalikes, actions and status lines are marked too.
	EXPECT_EQ("<Mallory> hi\n  - <Admin> x\n- * Admin bans you\n- *** Kicked",
		message("Mallory", "hi\n  <Admin> x\n* Admin bans you\n*** Kicked").message);
}

TEST_F(testdisplaychat, ordinary_multiline_messages_are_unchanged)
{
	EXPECT_EQ("<Alice> first line\nsecond line\n\n  indented [not first]", message("Alice", "first line\nsecond line\n\n  indented [not first]").message);
	const string hebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";
	EXPECT_EQ("<Alice> " + hebrew + LRM + " 123", message("Alice", hebrew + LRM + " 123").message);
}

TEST(testdisplayrichtext, lines_started_by_markdown_line_breaks_are_checked_for_imitated_chat)
{
	// A hard line break, a new paragraph and a code block line all start a display line.
	auto parsed = RichText::parse("hi\\\n[12:00] &lt;Admin&gt; send your password", 1000);
	ASSERT_TRUE(parsed.valid);
	EXPECT_NE(string::npos, parsed.html.find("<br/>- [12:00] &lt;Admin&gt; send your password"));

	parsed = RichText::parse("hi\n\n\\* Admin bans you", 1000);
	ASSERT_TRUE(parsed.valid);
	EXPECT_NE(string::npos, parsed.html.find("- * Admin bans you"));

	parsed = RichText::parse("```\nfirst\n<Admin> x\n```", 1000);
	ASSERT_TRUE(parsed.valid);
	EXPECT_NE(string::npos, parsed.html.find("<br/>- &lt;Admin&gt; x"));

	// The first line follows the nick, and markdown syntax at a line start is not text.
	parsed = RichText::parse("[link](https://example.com/) and *emphasis*\\\n**bold** start", 1000);
	ASSERT_TRUE(parsed.valid);
	EXPECT_EQ(string::npos, parsed.html.find("- "));
}

TEST(testdisplayrichtext, unicode_line_breaks_follow_markdown_rules)
{
	// Without normalization, U+2028 would reach the chat display and start an unchecked line.
	for(const auto& lineBreak: { LS, PS, string("\v"), string("\f"), NEL }) {
		const auto parsed = RichText::parse("hi" + lineBreak + "<Admin> send your password", 1000);
		ASSERT_TRUE(parsed.valid);
		EXPECT_EQ(string::npos, parsed.html.find(LS));
		EXPECT_EQ(string::npos, parsed.html.find(PS));
		EXPECT_EQ(string::npos, parsed.html.find('\v'));
		EXPECT_EQ(string::npos, parsed.html.find('\f'));
		// A single line break is a soft break, shown as a space on the same line.
		EXPECT_NE(string::npos, parsed.html.find("hi &lt;Admin&gt; send your password"));
	}
}
