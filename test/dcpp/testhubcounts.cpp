// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"

#include <chrono>
#include <filesystem>

#include <dcpp/hub/Client.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/net/HttpManager.h>
#include <dcpp/settings/SettingsManager.h>

using namespace dcpp;

namespace {

// Exercise the shared ADC/NMDC lifecycle without creating sockets or contacting hubs.
class CountedHub : public Client {
public:
	CountedHub() : Client("dchub://counts.example.invalid:411", '|', false) { }
	using Client::connect;
	using Client::reloadSettings;
	using Client::state;
	using Client::updateCounts;
	using Client::STATE_CONNECTING;
	using Client::STATE_PROTOCOL;
	using Client::STATE_IDENTIFY;
	using Client::STATE_VERIFY;
	using Client::STATE_DISCONNECTED;

	void login(bool registered = false, bool op = false) {
		auto identity = getMyIdentity();
		identity.set("RG", registered ? "1" : "");
		identity.setOp(op);
		setMyIdentity(identity);
		state = STATE_NORMAL;
		updateCounts(false);
	}

	void fail() { Client::on(BufferedSocketListener::Failed(), "Test disconnect"); }

	mutable StringList countsAtSocketSetup;
	mutable StringList urlsAtSocketSetup;

	void connect(const OnlineUser&, const string&, ConnectionType) override { }
	bool hubMessage(const string&, bool, bool) override { return false; }
	bool privateMessage(const OnlineUser&, const string&, bool, bool, bool) override { return false; }
	void sendUserCmd(const UserCommand&, const ParamMap&) override { }
	void search(int, int64_t, int, const string&, const string&, const StringList&, const string&) override { }
	void password(const string&) override { }
	size_t getUserCount() const override { return 0; }
	int64_t getAvailable() const override { return 0; }
	void emulateCommand(const string&) override { }
	void send(const AdcCommand&) override { }

private:
	void checkNick(string&) override { }
	bool v4only() const override {
		countsAtSocketSetup.push_back(getCounts());
		urlsAtSocketSetup.push_back(getConnectionUrl());
		throw Exception("Simulated socket setup failure");
	}
	OnlineUserList getUsers() const override { return {}; }
	void infoImpl() override { }
};

class FailureObserver : public ClientListener {
public:
	StringList observedCounts;
	void on(Failed, Client*, const string&) noexcept override {
		observedCounts.push_back(Client::getCounts());
	}
};

class HubCountsTest : public ::testing::Test {
public:
	void SetUp() override {
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		configPath = (std::filesystem::temp_directory_path() /
			("dcpp-hubcounts-test-" + std::to_string(ticks))).string() + PATH_SEPARATOR;
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path) {
			paths[static_cast<Util::Paths>(path)] = configPath;
		}
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);
		SettingsManager::newInstance();
		TimerManager::newInstance();
		ConnectivityManager::newInstance();
		ClientManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		ASSERT_EQ("0/0/0", Client::getCounts());
	}

	void TearDown() override {
		EXPECT_EQ("0/0/0", Client::getCounts());
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		ClientManager::deleteInstance();
		ConnectivityManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

private:
	string configPath;
};

TEST_F(HubCountsTest, DisconnectRemovesEachCategoryBeforeNotifyingListeners) {
	FailureObserver observer;
	CountedHub normal, registered, op;
	normal.login();
	registered.login(true);
	op.login(true, true);
	ASSERT_EQ("1/1/1", Client::getCounts());
	normal.addListener(&observer);
	normal.fail();
	EXPECT_EQ("0/1/1", Client::getCounts());
	ASSERT_EQ(1u, observer.observedCounts.size());
	EXPECT_EQ("0/1/1", observer.observedCounts.front());
	registered.fail();
	EXPECT_EQ("0/0/1", Client::getCounts());
	op.fail();
	EXPECT_EQ("0/0/0", Client::getCounts());
}

TEST_F(HubCountsTest, OfflineRefreshAndRepeatedFailureDoNotRestoreOrDoubleRemoveCount) {
	{
		CountedHub hub;
		hub.login();
		hub.fail();
		hub.updateCounts(false);
		EXPECT_EQ("0/0/0", Client::getCounts());
		hub.fail();
		EXPECT_EQ("0/0/0", Client::getCounts());
	}
	EXPECT_EQ("0/0/0", Client::getCounts());
}

TEST_F(HubCountsTest, OnlyFullyLoggedInSessionsCanContribute) {
	CountedHub hub;
	for(auto phase : { CountedHub::STATE_CONNECTING, CountedHub::STATE_PROTOCOL,
		CountedHub::STATE_IDENTIFY, CountedHub::STATE_VERIFY, CountedHub::STATE_DISCONNECTED })
	{
		hub.state = phase;
		hub.updateCounts(false);
		EXPECT_EQ("0/0/0", Client::getCounts()) << "State: " << phase;
	}
}

TEST_F(HubCountsTest, ReconnectRemovesOldSessionBeforeSocketSetupAndStaysUncountedOnFailure) {
	FailureObserver observer;
	CountedHub hub;
	hub.addListener(&observer);
	hub.login(true, true);
	hub.reconnect();
	ASSERT_EQ(1u, hub.countsAtSocketSetup.size());
	EXPECT_EQ("0/0/0", hub.countsAtSocketSetup.front());
	EXPECT_EQ("0/0/0", Client::getCounts());
	EXPECT_FALSE(hub.isConnected());
	ASSERT_EQ(1u, observer.observedCounts.size());
	EXPECT_EQ("0/0/0", observer.observedCounts.front());
	hub.reconnect();
	EXPECT_EQ("0/0/0", Client::getCounts());
	hub.login();
	hub.updateCounts(false);
	EXPECT_EQ("1/0/0", Client::getCounts());
}

TEST_F(HubCountsTest, FailedFailoverDoesNotRetainThePreviousHubCount) {
	CountedHub hub;
	auto favorite = new FavoriteHubEntry();
	favorite->setServer(hub.getHubUrl());
	favorite->setFailoverServers(StringList { "dchub://backup.example.invalid:411" });
	// FavoriteManager owns this entry; no user configuration is loaded or saved.
	FavoriteManager::getInstance()->getFavoriteHubs().push_back(favorite);
	hub.reloadSettings(true);
	hub.setAutoReconnect(true);
	hub.login(true);
	hub.fail();
	ASSERT_EQ(1u, hub.countsAtSocketSetup.size());
	EXPECT_EQ("dchub://backup.example.invalid:411", hub.urlsAtSocketSetup.front());
	EXPECT_EQ("0/0/0", hub.countsAtSocketSetup.front());
	EXPECT_EQ("0/0/0", Client::getCounts());
	EXPECT_FALSE(hub.isConnected());
	hub.login();
	EXPECT_EQ("1/0/0", Client::getCounts());
}

TEST_F(HubCountsTest, CategoryChangesAndClosingOfflineTabPreserveOtherHubs) {
	CountedHub other;
	other.login();
	{
		CountedHub hub;
		hub.login();
		EXPECT_EQ("2/0/0", Client::getCounts());
		hub.login(true);
		EXPECT_EQ("1/1/0", Client::getCounts());
		hub.login(true, true);
		EXPECT_EQ("1/0/1", Client::getCounts());
		hub.login(true);
		EXPECT_EQ("1/1/0", Client::getCounts());
		hub.fail();
		EXPECT_EQ("1/0/0", Client::getCounts());
	}
	EXPECT_EQ("1/0/0", Client::getCounts());
}

} // namespace
