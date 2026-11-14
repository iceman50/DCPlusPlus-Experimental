// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"

#include <chrono>
#include <filesystem>

// Drive the real login handlers and capture their outgoing commands without a hub.
#define private public
#include <dcpp/hub/AdcHub.h>
#include <dcpp/connection/socket/BufferedSocket.h>
#undef private

#include <dcpp/bbs/BBSManager.h>
#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/core/crypto/CryptoManager.h>
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dcpp/user/UserMatchManager.h>

using namespace dcpp;

namespace {

class LoginHub : public AdcHub {
public:

	LoginHub() : AdcHub("adc://login.example.invalid:411", false) {
		sock = BufferedSocket::getSocket('\n');
		sock->setSocket(unique_ptr<Socket>(new Socket(Socket::TYPE_TCP)));
	}
	~LoginHub() { shutdown(); }

	void begin() {
		setMyIdentity(Identity(ClientManager::getInstance()->getMe(), 0));
		reloadSettings(true);
		AdcHub::on(BufferedSocketListener::Connected());
		emulateCommand("ISUP ADBASE ADTIGR");
		emulateCommand("ISID ABCD");
	}

	void acknowledge() {
		emulateCommand("BINF ABCD ID" + ClientManager::getInstance()->getMyCID().toBase32() + " NITestNick");
	}

	void send(const AdcCommand& command) override {
		AdcCommand::ProtocolState protocolState;
		ASSERT_TRUE(getProtocolState(protocolState));
		ASSERT_TRUE(command.isValidFor(protocolState, AdcCommand::CONTEXT_TO_HUB));
		outgoing.push_back(command.toString(sid));
	}

	StringList outgoing;
};

class AdcLoginTest : public ::testing::Test {
public:
	void SetUp() override {
#ifdef _WIN32
		WSADATA data;
		ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &data));
#endif
		configPath = (std::filesystem::temp_directory_path() / ("dcpp-adclogin-" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string() + PATH_SEPARATOR;
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);
		SettingsManager::newInstance();
		SettingsManager::getInstance()->set(SettingsManager::NICK, "TestNick");
		LogManager::newInstance();
		TimerManager::newInstance();
		ConnectivityManager::newInstance();
		HashManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		UploadManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
		BBSManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		CryptoManager::newInstance();
		PluginManager::newInstance();
		UserMatchManager::newInstance();
	}

	void TearDown() override {
		BufferedSocket::waitShutdown();
		UserMatchManager::deleteInstance();
		PluginManager::deleteInstance();
		CryptoManager::deleteInstance();
		BBSManager::deleteInstance();
		ShareManager::deleteInstance();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		QueueManager::deleteInstance();
		UploadManager::deleteInstance();
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

	void changeDescription(LoginHub& hub, const string& description) {
		SettingsManager::getInstance()->set(SettingsManager::DESCRIPTION, description);
		hub.infoImpl();
	}

	void expectParam(const string& line, const char* name, const string& expected) {
		AdcCommand command(line.substr(0, line.size() - 1));
		string value;
		ASSERT_TRUE(command.getParam(name, 0, value)) << line;
		EXPECT_EQ(expected, value);
	}

	string configPath;
};

TEST_F(AdcLoginTest, DelayedPasswordChallengeDoesNotReceivePartialLoginUpdates) {
	LoginHub hub;
	hub.begin();
	ASSERT_EQ(2u, hub.outgoing.size()); // SUP followed by the complete initial INF.
	expectParam(hub.outgoing.back(), "NI", "TestNick");
	expectParam(hub.outgoing.back(), "ID", ClientManager::getInstance()->getMyCID().toBase32());

	// Some hubs announce extra capabilities after SID. The protocol parser
	// accepts SUP in NORMAL, but that does not acknowledge our login identity.
	hub.emulateCommand("ISUP ADHBRI");
	EXPECT_EQ(2u, hub.outgoing.size());
	changeDescription(hub, "before-challenge");
	changeDescription(hub, "latest-before-challenge");
	EXPECT_EQ(2u, hub.outgoing.size());
	hub.emulateCommand("IGPA AAAAAAAAAAAAAAAA");
	changeDescription(hub, "during-verification");
	EXPECT_EQ(2u, hub.outgoing.size());
	hub.password("test-password");
	ASSERT_EQ(3u, hub.outgoing.size());
	EXPECT_EQ(0u, hub.outgoing.back().find("HPAS "));
	hub.emulateCommand("IINF CT32 NIHub");
	changeDescription(hub, "after-hub-info");
	EXPECT_EQ(3u, hub.outgoing.size());
	hub.acknowledge();
	ASSERT_EQ(4u, hub.outgoing.size());
	expectParam(hub.outgoing.back(), "DE", "after-hub-info");
	changeDescription(hub, "after-login");
	ASSERT_EQ(5u, hub.outgoing.size());
	expectParam(hub.outgoing.back(), "DE", "after-login");
}

TEST_F(AdcLoginTest, UnregisteredLoginDefersUpdatesAndReconnectSendsCompleteIdentity) {
	LoginHub hub;
	for(int attempt = 0; attempt < 2; ++attempt) {
		hub.outgoing.clear();
		hub.begin();
		ASSERT_EQ(2u, hub.outgoing.size());
		expectParam(hub.outgoing.back(), "NI", "TestNick");
		expectParam(hub.outgoing.back(), "ID", ClientManager::getInstance()->getMyCID().toBase32());
		const auto description = "pending-" + std::to_string(attempt);
		changeDescription(hub, description);
		EXPECT_EQ(2u, hub.outgoing.size());
		hub.acknowledge();
		ASSERT_EQ(3u, hub.outgoing.size());
		expectParam(hub.outgoing.back(), "DE", description);
		hub.infoImpl();
		EXPECT_EQ(3u, hub.outgoing.size());
	}
}

} // namespace
