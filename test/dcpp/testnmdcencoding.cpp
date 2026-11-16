// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"

#include <chrono>
#include <filesystem>

// Drive the real NMDC hub, search and connection handlers without opening sockets. Outgoing
// hub and peer commands are captured through the plugin network hooks.
#define private public
#include <dcpp/hub/Client.h>
#include <dcpp/hub/NmdcHub.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/connection/UserConnection.h>
#include <dcpp/connection/socket/BufferedSocket.h>
#include <dcpp/favorites/FavoriteManager.h>
#undef private

#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/core/crypto/CryptoManager.h>
#include <dcpp/core/io/xml/SimpleXML.h>
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/message/ChatMessage.h>
#include <dcpp/plugins/PluginApiImpl.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/search/SearchResult.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/transfer/download/DownloadManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dcpp/user/UserMatchManager.h>
#include <dcpp/util/text/Text.h>

using namespace dcpp;

namespace {

const string testHubUrl = "dchub://encoding.example.invalid:411";
const string testHubIp = "192.0.2.10";
const string cp1251 = "1251 (ANSI - Cyrillic)";
const string utf8CodePage = "65001 (UTF-8)";

// "Вася" as CP1251 bytes (user A) and as UTF-8 bytes (user B). An NMDC hub compares nicks as
// bytes, so it lets both log in.
const string nickA = "\xC2\xE0\xF1\xFF";
const string nickB = "\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F";
// How each nick must read on a CP1251 hub: A as "Вася", B's bytes as CP1251 text "Р’Р°СЃСЏ".
const string textA = nickB;
const string textB = "\xD0\xA0\xE2\x80\x99\xD0\xA0\xC2\xB0\xD0\xA1\xD0\x83\xD0\xA1\xD0\x8F";

// "файл.txt" as CP1251 bytes and as UTF-8.
const string fileCp1251 = "\xF4\xE0\xE9\xEB.txt";
const string fileText = "\xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB.txt";

const string tth = string(39, 'A');

Bool DCAPI captureLine(dcptr_t, dcptr_t data, dcptr_t common, Bool*) {
	static_cast<StringList*>(common)->emplace_back(static_cast<const char*>(data));
	return True; // handled: nothing reaches a socket
}

class EncodingHub : public NmdcHub {
public:
	EncodingHub() : NmdcHub(testHubUrl) {
		// Plugin hooks read the socket's state; it is never connected or written to.
		sock = BufferedSocket::getSocket('|');
		sock->setSocket(unique_ptr<Socket>(new Socket(Socket::TYPE_TCP)));
	}
	~EncodingHub() { shutdown(); }

	void login() {
		reloadSettings(true);
		// What connecting and the hub's $Hello do for our own identity.
		setMyIdentity(Identity(ClientManager::getInstance()->getMe(), 0));
		getUser(get(Nick));
		state = STATE_NORMAL;
		ip = testHubIp;
		port = "411";
	}

	void reload() { reloadSettings(false); }
};

class Recorder : public ClientListener, public SearchManagerListener {
public:
	using ClientListener::on;

	void on(ClientListener::Message, Client*, const ChatMessage& message) noexcept override {
		messages.push_back(message);
	}
	void on(ClientListener::NmdcSearch, Client*, const string& seeker, int, int64_t, int, const string&) noexcept override {
		seekers.push_back(seeker);
	}
	void on(SearchManagerListener::SR, const SearchResultPtr& result) noexcept override {
		results.push_back(result);
	}

	vector<ChatMessage> messages;
	StringList seekers;
	vector<SearchResultPtr> results;
};

class NmdcEncodingTest : public ::testing::Test {
public:
	void SetUp() override {
#ifdef _WIN32
		WSADATA data;
		ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &data));
#endif
		Text::initialize();
		configPath = (std::filesystem::temp_directory_path() / ("dcpp-nmdc-encoding-" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string() + PATH_SEPARATOR;
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);
		SettingsManager::newInstance();
		SettingsManager::getInstance()->set(SettingsManager::NICK, "TestNick");
		SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, false);
		LogManager::newInstance();
		TimerManager::newInstance();
		ConnectivityManager::newInstance();
		HashManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
		DownloadManager::newInstance();
		UploadManager::newInstance();
		ConnectionManager::newInstance();
		CryptoManager::newInstance();
		PluginManager::newInstance();
		PluginApiImpl::init();
		UserMatchManager::newInstance();

		auto plugins = PluginManager::getInstance();
		ASSERT_NE(nullptr, plugins->bindHook(HOOK_NETWORK_HUB_OUT, captureLine, &hubOut));
		ASSERT_NE(nullptr, plugins->bindHook(HOOK_NETWORK_CONN_OUT, captureLine, &peerOut));
		SearchManager::getInstance()->addListener(&recorder);
	}

	void TearDown() override {
		for(auto& peer: peers) {
			peer->fire(UserConnectionListener::Failed(), peer.get(), string("Test shutdown"));
		}
		peers.clear();
		closeHub();
		SearchManager::getInstance()->removeListener(&recorder);
		BufferedSocket::waitShutdown();

		UserMatchManager::deleteInstance();
		PluginApiImpl::shutdown();
		PluginManager::deleteInstance();
		CryptoManager::deleteInstance();
		ConnectionManager::deleteInstance();
		UploadManager::deleteInstance();
		DownloadManager::deleteInstance();
		ShareManager::deleteInstance();
		QueueManager::deleteInstance();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		ConnectivityManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
#ifdef _WIN32
		WSACleanup();
#endif
	}

	/** Log in to a hub registered with ClientManager, as search and connection lookups require. */
	EncodingHub& openHub() {
		hub.reset(new EncodingHub());
		{
			auto cm = ClientManager::getInstance();
			Lock l(cm->cs);
			cm->clients.insert(hub.get());
		}
		hub->addListener(&recorder);
		hub->login();
		return *hub;
	}

	void closeHub() {
		if(!hub)
			return;
		hub->removeListener(&recorder);
		{
			auto cm = ClientManager::getInstance();
			Lock l(cm->cs);
			cm->clients.erase(hub.get());
		}
		hub.reset();
	}

	static string myInfo(const string& nick, const string& description, const string& share) {
		return "$MyINFO $ALL " + nick + " " + description + "$ $LAN(T3)" + string(1, '\x01') + "$$" + share + "$";
	}

	/** An NMDC file search result, as sent by UDP or relayed by the hub. */
	static string searchResult(const string& nick, const string& file) {
		return "$SR " + nick + " " + file + "\x05" "1024 1/2\x05" "TTH:" + tth + " (" + testHubIp + ":411)";
	}

	UserConnection* incoming(const string& encoding = Util::emptyString) {
		auto uc = ConnectionManager::getInstance()->getConnection(true, false);
		peers.emplace_back(uc);
		// The handshake sends $MyNick and $Lock; the hook captures them before the socket is used.
		uc->socket = BufferedSocket::getSocket('|');
		uc->socket->setSocket(unique_ptr<Socket>(new Socket(Socket::TYPE_TCP)));
		uc->setState(UserConnection::STATE_SUPNICK);
		if(encoding.empty()) {
			uc->setFlag(UserConnection::FLAG_INCOMING);
		} else {
			// An outgoing connection already knows its hub and encoding.
			uc->setHubUrl(testHubUrl);
			uc->setEncoding(encoding);
		}
		return uc;
	}

	bool sentLine(const StringList& lines, const string& expected) const {
		return std::find(lines.begin(), lines.end(), expected) != lines.end();
	}

	string configPath;
	unique_ptr<EncodingHub> hub;
	vector<unique_ptr<UserConnection>> peers;
	Recorder recorder;
	StringList hubOut;
	StringList peerOut;
};

TEST_F(NmdcEncodingTest, HubKeepsUsersWithDifferentNickBytesApart) {
	SettingsManager::getInstance()->set(SettingsManager::NMDC_ENCODING, cp1251);
	auto& hub = openHub();
	ASSERT_EQ(cp1251, hub.getEncoding());

	hub.emulateCommand(myInfo(nickA, "first", "1000"));
	hub.emulateCommand(myInfo(nickB, "second", "2000"));

	auto a = hub.findUser(textA);
	auto b = hub.findUser(textB);
	ASSERT_TRUE(a);
	ASSERT_TRUE(b);
	ASSERT_NE(a, b);
	ASSERT_NE(a->getUser(), b->getUser());
	// B's $MyINFO must not overwrite A's.
	EXPECT_EQ("first", a->getIdentity().getDescription());
	EXPECT_EQ(1000, a->getIdentity().getBytesShared());
	EXPECT_EQ("second", b->getIdentity().getDescription());

	// Main chat from B is attributed to B.
	hub.emulateCommand("<" + nickB + "> hello");
	ASSERT_EQ(1u, recorder.messages.size());
	EXPECT_EQ(b->getUser(), recorder.messages.back().from);

	// A private message from B opens B's chat, not A's.
	hub.emulateCommand("$To: TestNick From: " + nickB + " $<" + nickB + "> secret");
	ASSERT_EQ(2u, recorder.messages.size());
	EXPECT_EQ(b->getUser(), recorder.messages.back().from);
	EXPECT_EQ(b->getUser(), recorder.messages.back().replyTo);

	// Replies go back to B's own bytes.
	hub.privateMessage(*b, "reply", false, false);
	EXPECT_TRUE(sentLine(hubOut, "$To: " + nickB + " From: TestNick $<TestNick> reply|")) << ::testing::PrintToString(hubOut);
	hub.privateMessage(*a, "reply", false, false);
	EXPECT_TRUE(sentLine(hubOut, "$To: " + nickA + " From: TestNick $<TestNick> reply|")) << ::testing::PrintToString(hubOut);

	// B leaving removes only B.
	hub.emulateCommand("$Quit " + nickB);
	EXPECT_TRUE(hub.findUser(textA));
	EXPECT_FALSE(hub.findUser(textB));
	EXPECT_TRUE(ClientManager::getInstance()->isOnline(a->getUser()));
}

TEST_F(NmdcEncodingTest, SearchResultsAndPassiveSearchesMatchTheHubUsers) {
	SettingsManager::getInstance()->set(SettingsManager::NMDC_ENCODING, cp1251);
	auto& hub = openHub();
	hub.emulateCommand(myInfo(nickA, "first", "1000"));
	hub.emulateCommand(myInfo(nickB, "second", "2000"));
	auto a = hub.findUser(textA);
	auto b = hub.findUser(textB);
	ASSERT_TRUE(a && b);

	// Results relayed by the hub (passive) and sent by UDP (active) both name the right user.
	SearchManager::getInstance()->onData(searchResult(nickB, fileCp1251));
	SearchManager::getInstance()->onData(searchResult(nickA, fileCp1251), "198.51.100.7");
	ASSERT_EQ(2u, recorder.results.size());
	EXPECT_EQ(b->getUser(), recorder.results[0]->getUser().user);
	EXPECT_EQ(testHubUrl, recorder.results[0]->getUser().hint);
	EXPECT_EQ(fileText, recorder.results[0]->getFile());
	EXPECT_EQ(a->getUser(), recorder.results[1]->getUser().user);

	// A passive search from B marks B passive, and the reply address is B's own bytes.
	hub.emulateCommand("$Search Hub:" + nickB + " F?T?0?1?test");
	ASSERT_EQ(1u, recorder.seekers.size());
	EXPECT_EQ(nickB, Text::fromUtf8(recorder.seekers[0].substr(4), hub.getEncoding()));
	EXPECT_TRUE(b->getUser()->isSet(User::PASSIVE));
	EXPECT_FALSE(a->getUser()->isSet(User::PASSIVE));
}

TEST_F(NmdcEncodingTest, ConnectionsMatchTheHubUsers) {
	SettingsManager::getInstance()->set(SettingsManager::NMDC_ENCODING, cp1251);
	auto& hub = openHub();
	hub.emulateCommand(myInfo(nickA, "first", "1000"));
	hub.emulateCommand(myInfo(nickB, "second", "2000"));
	auto a = hub.findUser(textA);
	auto b = hub.findUser(textB);
	ASSERT_TRUE(a && b);
	auto cm = ConnectionManager::getInstance();

	// Asking B to connect uses B's bytes, and B's incoming connection is matched to B.
	hub.connectToMe(*b);
	ASSERT_FALSE(hubOut.empty());
	EXPECT_EQ(0u, hubOut.back().find("$ConnectToMe " + nickB + " "));
	auto fromB = incoming();
	cm->on(UserConnectionListener::MyNick(), fromB, nickB);
	EXPECT_EQ(b->getUser(), fromB->getUser());
	EXPECT_EQ(testHubUrl, fromB->getHubUrl());
	EXPECT_EQ(cp1251, fromB->getEncoding());
	EXPECT_TRUE(sentLine(peerOut, "$MyNick TestNick|")) << ::testing::PrintToString(peerOut);

	// A queued download from A is matched by A's bytes on an outgoing connection, not by B's.
	cm->getCQI(HintedUser(a->getUser(), testHubUrl), CONNECTION_TYPE_DOWNLOAD);
	auto toB = incoming(cp1251);
	cm->on(UserConnectionListener::MyNick(), toB, nickB);
	EXPECT_EQ(b->getUser(), toB->getUser());
	EXPECT_FALSE(toB->isSet(UserConnection::FLAG_DOWNLOAD));
	auto toA = incoming(cp1251);
	cm->on(UserConnectionListener::MyNick(), toA, nickA);
	EXPECT_EQ(a->getUser(), toA->getUser());
	EXPECT_TRUE(toA->isSet(UserConnection::FLAG_DOWNLOAD));
}

TEST_F(NmdcEncodingTest, Utf8HubDecodesUtf8AndReplacesInvalidBytes) {
	FavoriteHubEntry entry;
	entry.setServer(testHubUrl);
	entry.setEncoding(utf8CodePage);
	FavoriteManager::getInstance()->addFavorite(entry);
	auto& hub = openHub();
	ASSERT_EQ(utf8CodePage, hub.getEncoding());

	hub.emulateCommand(myInfo(nickB, "utf8", "1"));
	hub.emulateCommand(myInfo("Bad\xFFNick", "invalid", "1"));
	auto b = hub.findUser(nickB);
	ASSERT_TRUE(b);
	EXPECT_TRUE(hub.findUser("Bad\xEF\xBF\xBDNick"));
	EXPECT_FALSE(hub.findUser("Bad\xFFNick"));

	SearchManager::getInstance()->onData(searchResult(nickB, fileText));
	ASSERT_EQ(1u, recorder.results.size());
	EXPECT_EQ(b->getUser(), recorder.results[0]->getUser().user);
	EXPECT_EQ(fileText, recorder.results[0]->getFile());

	auto fromB = incoming(utf8CodePage);
	ConnectionManager::getInstance()->on(UserConnectionListener::MyNick(), fromB, nickB);
	EXPECT_EQ(b->getUser(), fromB->getUser());
}

TEST_F(NmdcEncodingTest, EncodingFollowsTheDefaultUnlessTheFavoriteSetsOne) {
	auto& hub = openHub();
	EXPECT_EQ(Text::systemCharset, hub.getEncoding());

	SettingsManager::getInstance()->set(SettingsManager::NMDC_ENCODING, cp1251);
	hub.reload();
	EXPECT_EQ(cp1251, hub.getEncoding());

	FavoriteHubEntry entry;
	EXPECT_TRUE(entry.getEncoding().empty());
	entry.setServer(testHubUrl);
	FavoriteManager::getInstance()->addFavorite(entry);
	hub.reload();
	EXPECT_EQ(cp1251, hub.getEncoding());

	auto favorite = FavoriteManager::getInstance()->getFavoriteHubEntry(testHubUrl);
	ASSERT_TRUE(favorite);
	favorite->setEncoding(utf8CodePage);
	hub.reload();
	EXPECT_EQ(utf8CodePage, hub.getEncoding());

	// Clearing the favorite's encoding returns to the default without reconnecting.
	favorite->setEncoding(Util::emptyString);
	hub.reload();
	EXPECT_EQ(cp1251, hub.getEncoding());
}

TEST_F(NmdcEncodingTest, SavedSystemLocaleEncodingLoadsAsDefault) {
	ASSERT_FALSE(Text::systemCharset.empty());
	SimpleXML xml;
	xml.addTag("Favorites");
	xml.stepIn();
	xml.addTag("Hubs");
	xml.stepIn();
	xml.addTag("Hub");
	xml.addChildAttrib("Server", string("dchub://old.example.invalid"));
	xml.addChildAttrib("Encoding", Text::systemCharset);
	xml.addTag("Hub");
	xml.addChildAttrib("Server", string("dchub://chosen.example.invalid"));
	xml.addChildAttrib("Encoding", cp1251);
	xml.stepOut();
	xml.stepOut();

	xml.resetCurrentChild();
	ASSERT_TRUE(xml.findChild("Favorites"));
	xml.stepIn();
	FavoriteManager::getInstance()->load(xml);

	auto old = FavoriteManager::getInstance()->getFavoriteHubEntry("dchub://old.example.invalid");
	auto chosen = FavoriteManager::getInstance()->getFavoriteHubEntry("dchub://chosen.example.invalid");
	ASSERT_TRUE(old && chosen);
	EXPECT_TRUE(old->getEncoding().empty());
	EXPECT_EQ(cp1251, chosen->getEncoding());
}

} // namespace
