/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <chrono>
#include <filesystem>

// Exercise the real manager transitions without opening external connections.
// This follows the private-access pattern used by the share/cache tests.
#define private public
#include <dcpp/hub/ClientManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/hub/NmdcHub.h>
#include <dcpp/connection/UserConnection.h>
#include <dcpp/transfer/UploadManager.h>
#include <dcpp/plugins/PluginManager.h>
#undef private

#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/transfer/DownloadManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/net/HttpManager.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/user/UserMatchManager.h>

using namespace dcpp;

namespace {

class RecordingHub : public NmdcHub {
public:

	explicit RecordingHub(const string& url) : NmdcHub(url) { }
	void connect(const OnlineUser&, const string& token, ConnectionType) override {
		attempts.push_back(token);
	}
	StringList attempts;
};

class ConnectionManagerTest : public ::testing::Test, public ConnectionManagerListener {
public:
	void SetUp() override {
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		configPath = (std::filesystem::temp_directory_path() /
			("dcpp-connection-test-" + std::to_string(ticks))).string() + PATH_SEPARATOR;
		Util::PathsMap paths;
		for(auto path: { Util::PATH_USER_CONFIG, Util::PATH_USER_LOCAL, Util::PATH_GLOBAL_CONFIG,
			Util::PATH_RESOURCES, Util::PATH_LOCALE, Util::PATH_DOWNLOADS, Util::PATH_FILE_LISTS,
			Util::PATH_HUB_LISTS })
		{
			paths[path] = configPath;
		}
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);
		SettingsManager::newInstance();
		SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, false);
		LogManager::newInstance();
		TimerManager::newInstance();
		ConnectivityManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		QueueManager::newInstance();
		DownloadManager::newInstance();
		UploadManager::newInstance();
		ConnectionManager::newInstance();
		manager = ConnectionManager::getInstance();
		manager->addListener(this);
	}

	void TearDown() override {
		for(auto& peer: peers) {
			peer->fire(UserConnectionListener::Failed(), peer.get(), string("Test shutdown"));
		}
		peers.clear();
		manager->removeListener(this);
		ConnectionManager::deleteInstance();
		DownloadManager::deleteInstance();
		UploadManager::deleteInstance();
		QueueManager::deleteInstance();
		ClientManager::getInstance()->onlineUsers.clear();
		onlineUsers.clear();
		hubs.clear();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		ConnectivityManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

	UserPtr user(uint8_t id) {
		uint8_t bytes[CID::SIZE] = { 0 };
		bytes[0] = id;
		return UserPtr(new User(CID(bytes)));
	}

	UserConnection* peer(const UserPtr& user, const string& token, bool nmdc, bool upload, bool mcn = false) {
		auto uc = manager->getConnection(nmdc, false);
		peers.emplace_back(uc);
		uc->setUser(user);
		uc->setToken(token);
		uc->setHubUrl("dchub://one.example");
		uc->setFlag(upload ? UserConnection::FLAG_UPLOAD : UserConnection::FLAG_DOWNLOAD);
		uc->setFlag(UserConnection::FLAG_SUPPORTS_ADCGET);
		uc->setFlag(UserConnection::FLAG_SUPPORTS_TTHF);
		if(mcn) { uc->setFlag(UserConnection::FLAG_SUPPORTS_MCN1); }
		return uc;
	}

	OnlineUser& online(const UserPtr& user, const string& hub) {
		hubs.emplace_back(new RecordingHub(hub));
		onlineUsers.emplace_back(new OnlineUser(user, *hubs.back(), 1));
		ClientManager::getInstance()->onlineUsers.emplace(user->getCID(), onlineUsers.back().get());
		return *onlineUsers.back();
	}

	UserConnection* pmPeer(const UserPtr& user, const string& token, bool incoming = false) {
		auto uc = peer(user, token, false, false);
		uc->unsetFlag(UserConnection::FLAG_DOWNLOAD);
		uc->setFlag(UserConnection::FLAG_PM);
		if(incoming) uc->setFlag(UserConnection::FLAG_INCOMING);
		return uc;
	}

	void queueFile(const UserPtr& user, const string& hub) {
		// Queue while offline so the test can control connection creation and time.
		QueueManager::getInstance()->add(configPath + "file-" + user->getCID().toBase32(),
			1024 * 1024, TTHValue(), HintedUser(user, hub));
		user->setFlag(User::ONLINE);
	}

	void on(ConnectionManagerListener::Connected, ConnectionQueueItem* cqi, UserConnection* uc) noexcept override {
		EXPECT_EQ(cqi->getToken(), uc->getToken());
		EXPECT_EQ(cqi->getUser().user, uc->getUser());
		++connected;
	}
	void on(ConnectionManagerListener::Failed, ConnectionQueueItem*, const string& error) noexcept override {
		failures.push_back(error);
	}

	ConnectionManager* manager = nullptr;
	string configPath;
	vector<unique_ptr<UserConnection>> peers;
	vector<unique_ptr<RecordingHub>> hubs;
	vector<unique_ptr<OnlineUser>> onlineUsers;
	StringList failures;
	int connected = 0;
};

} // namespace

TEST(testconnections, repeated_nmdc_expectations_remain_valid) {
	ExpectedMap expected;
	expected.add("Alice", "Me", "dchub://hub.example");
	expected.add("Alice", "Me", "DCHUB://HUB.example");
	expected.add("Bob", "Me", "dchub://hub.example");
	EXPECT_EQ("dchub://hub.example", expected.remove("Alice").second);
	EXPECT_EQ("dchub://hub.example", expected.remove("Bob").second);
	EXPECT_TRUE(expected.remove("Alice").second.empty());
}

TEST(testconnections, ambiguous_nmdc_hubs_are_still_rejected) {
	ExpectedMap expected;
	expected.add("Alice", "Me", "dchub://one.example");
	expected.add("Alice", "Me", "dchub://two.example");
	EXPECT_TRUE(expected.remove("Alice").second.empty());
	expected.add("Alice", "Me", "dchub://one.example");
	EXPECT_EQ("dchub://one.example", expected.remove("Alice").second);
}

TEST_F(ConnectionManagerTest, duplicate_named_user_rules_keep_independent_appearance_sources) {
	UserMatchManager::newInstance();
	struct Cleanup { ~Cleanup() { UserMatchManager::deleteInstance(); } } cleanup;
	auto matching = UserMatchManager::getInstance();
	UserMatch alice, bob, background;
	alice.name = bob.name = background.name = "Friends";
	auto nickRule = [](UserMatch& match, const string& nick) {
		UserMatch::Rule rule;
		rule.field = UserMatch::Rule::NICK;
		rule.pattern = nick;
		rule.setMethod(StringMatch::EXACT);
		match.addRule(std::move(rule));
	};
	nickRule(alice, "Alice");
	nickRule(bob, "Bob");
	alice.style.textColor = 0x0000ff;
	bob.style.textColor = 0xff0000;
	bob.style.font = "Segoe UI,-16,700,0,1";
	background.style.bgColor = 0xeeeeee;
	matching->setList({ alice, bob, background }, false);
	auto& onlineBob = online(user(1), "dchub://styles.example");
	onlineBob.getIdentity().setNick("Bob");
	matching->match(onlineBob);
	const auto style = onlineBob.getIdentity().getStyle();
	EXPECT_EQ(bob.style.textColor, style.textColor);
	EXPECT_EQ(bob.styleId, style.textSource);
	EXPECT_EQ(bob.styleId, style.fontSource);
	EXPECT_EQ(background.styleId, style.bgSource);
	EXPECT_EQ(bob.style.font, matching->getStyle(style.fontSource).font);
	EXPECT_EQ(background.style.bgColor, matching->getStyle(style.bgSource).bgColor);
	bob.name = "Renamed";
	bob.style.textColor = 0x00ff00;
	matching->setList({ background, bob, alice }, false);
	EXPECT_EQ(bob.style.textColor, matching->getStyle(style.textSource).textColor);
	matching->setList({ alice, alice }, false);
	const auto cloned = matching->getList();
	EXPECT_NE(cloned[0].styleId, cloned[1].styleId);
	EXPECT_EQ(-1, matching->getStyle(style.textSource).textColor);
}

TEST_F(ConnectionManagerTest, user_styles_refresh_without_replacing_identity_snapshots) {
	UserMatchManager::newInstance();
	struct Cleanup { ~Cleanup() { UserMatchManager::deleteInstance(); } } cleanup;
	auto matching = UserMatchManager::getInstance();
	UserMatch first, fallback, unrelated;
	first.name = fallback.name = unrelated.name = "Duplicate name";
	first.setFlag(UserMatch::IGNORE_CHAT);
	first.style.textColor = 0x112233;
	fallback.style.textColor = 0x445566;
	fallback.style.bgColor = 0x778899;
	UserMatch::Rule rule;
	rule.field = UserMatch::Rule::NICK;
	rule.pattern = "Someone else";
	rule.setMethod(StringMatch::EXACT);
	unrelated.addRule(std::move(rule));
	unrelated.style.textColor = 0xffffff;
	matching->setList({ unrelated, first, fallback }, false);
	auto& bob = online(user(1), "dchub://styles.example");
	bob.getIdentity().setNick("Bob");
	matching->match(bob);
	const auto snapshot = bob.getIdentity().getStyle();
	ASSERT_EQ(2U, snapshot.matchingStyles.size());
	EXPECT_EQ(first.style.textColor, snapshot.textColor);
	EXPECT_TRUE(bob.getIdentity().noChat());

	// Remove the first override: the lower priority rule must supply the new color.
	// Add a previously absent font and background to that same first rule.
	first.style.textColor = -1;
	first.style.font = "Segoe UI,-16,700,0,1";
	first.style.bgColor = 0x123456;
	fallback.style.textColor = 0xaabbcc;
	matching->setList({ unrelated, first, fallback }, false);
	auto resolved = matching->resolveStyle(snapshot);
	EXPECT_EQ(fallback.style.textColor, resolved.textColor);
	EXPECT_EQ(first.style.bgColor, resolved.bgColor);
	EXPECT_EQ(first.style.font, resolved.font);
	EXPECT_EQ(fallback.styleId, resolved.textSource);
	EXPECT_EQ(first.styleId, resolved.bgSource);
	EXPECT_EQ(0x112233, bob.getIdentity().getStyle().textColor);
	EXPECT_TRUE(bob.getIdentity().noChat());

	// Pending old snapshots must also honor reordered and deleted definitions.
	matching->setList({ fallback, first, unrelated }, false);
	EXPECT_EQ(fallback.style.bgColor, matching->resolveStyle(snapshot).bgColor);
	matching->setList({ unrelated }, false);
	resolved = matching->resolveStyle(snapshot);
	EXPECT_EQ(-1, resolved.textColor);
	EXPECT_EQ(-1, resolved.bgColor);
	EXPECT_TRUE(resolved.font.empty());

	// Even a match with no original visual fields can acquire themed overrides.
	first.style = Style();
	matching->setList({ first }, false);
	matching->match(bob);
	const auto emptySnapshot = bob.getIdentity().getStyle();
	first.style.textColor = 0xabcdef;
	matching->setList({ first }, false);
	EXPECT_EQ(first.style.textColor, matching->resolveStyle(emptySnapshot).textColor);

	Style literal;
	literal.textColor = 0x010203;
	EXPECT_EQ(literal.textColor, matching->resolveStyle(literal).textColor);
}

TEST_F(ConnectionManagerTest, nmdc_transfers_to_multiple_users_have_independent_ids) {
	StringSet downloadTokens, uploadTokens;
	for(uint8_t id = 1; id <= 4; ++id) {
		auto u = user(id);
		auto token = manager->getCQI(HintedUser(u, "dchub://one.example"), CONNECTION_TYPE_DOWNLOAD).getToken();
		auto down = peer(u, "MyNick", true, false);
		manager->addDownloadConnection(down);
		ASSERT_TRUE(down->isSet(UserConnection::FLAG_ASSOCIATED));
		EXPECT_EQ(token, down->getToken());
		downloadTokens.insert(down->getToken());
		manager->onDownloadStarted(*down);
		EXPECT_TRUE(manager->downloads.back().getRunning());
		auto up = peer(u, "MyNick", true, true);
		manager->addNewConnection(up, CONNECTION_TYPE_UPLOAD);
		ASSERT_TRUE(up->isSet(UserConnection::FLAG_ASSOCIATED));
		uploadTokens.insert(up->getToken());
	}
	EXPECT_EQ(8, connected);
	EXPECT_EQ(4U, downloadTokens.size());
	EXPECT_EQ(4U, uploadTokens.size());
	EXPECT_EQ(0U, downloadTokens.count("MyNick"));
	EXPECT_EQ(0U, uploadTokens.count("MyNick"));

	peers.front()->fire(UserConnectionListener::Failed(), peers.front().get(), string("Connection lost"));
	EXPECT_EQ(ConnectionQueueItem::WAITING, manager->downloads.front().getState());
	for(size_t i = 1; i < manager->downloads.size(); ++i) {
		EXPECT_EQ(ConnectionQueueItem::ACTIVE, manager->downloads[i].getState());
	}
}

TEST_F(ConnectionManagerTest, ccpm_crossing_connections_converge_regardless_of_arrival_order) {
	SettingsManager::getInstance()->set(SettingsManager::ENABLE_CCPM, true);
	auto alice = user(1), bob = user(2);
	// Opposite endpoints observe opposite handshake completion orders.
	auto aFirst = pmPeer(alice, "200", true);
	auto bFirst = pmPeer(bob, "100", true);
	manager->addNewConnection(aFirst, CONNECTION_TYPE_PM);
	manager->addNewConnection(bFirst, CONNECTION_TYPE_PM);
	auto aSecond = pmPeer(alice, "100");
	auto bSecond = pmPeer(bob, "200");
	manager->addNewConnection(aSecond, CONNECTION_TYPE_PM);
	manager->addNewConnection(bSecond, CONNECTION_TYPE_PM);
	EXPECT_TRUE(aSecond->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_FALSE(bSecond->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(3, connected);
	// A superseded channel's late failure must not remove the winner.
	aFirst->fire(UserConnectionListener::Failed(), aFirst, string("Duplicate channel closed"));
	EXPECT_TRUE(manager->isPMConnectionActive(alice, aSecond->getToken()));
	EXPECT_TRUE(manager->isPMConnectionActive(bob, bFirst->getToken()));
}

TEST_F(ConnectionManagerTest, ccpm_hub_rendezvous_expires_once_after_thirty_seconds) {
	auto alice = ClientManager::getInstance()->getUser(user(91)->getCID());
	auto& presence = online(alice, "adcs://one.example");
	manager->addToken("pending-pm", presence, CONNECTION_TYPE_PM);
	manager->addToken("pending-download", presence, CONNECTION_TYPE_DOWNLOAD);
	manager->addToken("pending-private-file", presence, CONNECTION_TYPE_PRIVATE);
	const auto started = manager->findToken("pending-pm", alice->getCID())->second.created;
	manager->on(TimerManagerListener::Second(), started + ConnectionManager::PM_CONNECT_TIMEOUT - 1);
	EXPECT_TRUE(failures.empty());
	EXPECT_EQ(3U, manager->tokens.size());
	manager->on(TimerManagerListener::Second(), started + ConnectionManager::PM_CONNECT_TIMEOUT);
	ASSERT_EQ(1U, failures.size());
	EXPECT_NE(string::npos, failures.front().find("timed out"));
	EXPECT_EQ(2U, manager->tokens.size());
	manager->on(TimerManagerListener::Second(), started + ConnectionManager::PM_CONNECT_TIMEOUT + 1000);
	EXPECT_EQ(1U, failures.size());
}

TEST_F(ConnectionManagerTest, cancelling_ccpm_preserves_other_peers_hubs_and_transfer_tokens) {
	auto alice = user(91), bob = user(92);
	auto& a = online(alice, "adcs://one.example");
	auto& b = online(bob, "adcs://one.example");
	manager->addToken("shared", a, CONNECTION_TYPE_PM);
	manager->addToken("shared", b, CONNECTION_TYPE_PM);
	manager->addToken("download", a, CONNECTION_TYPE_DOWNLOAD);
	manager->cancelPMConnect(HintedUser(alice, "adcs://other.example"), "shared");
	EXPECT_EQ(3U, manager->tokens.size());
	manager->cancelPMConnect(HintedUser(alice, a.getClient().getHubUrl()), "shared");
	EXPECT_EQ(2U, manager->tokens.size());
	EXPECT_EQ(manager->tokens.end(), manager->findToken("shared", alice->getCID()));
	EXPECT_NE(manager->tokens.end(), manager->findToken("shared", bob->getCID()));
	manager->cancelPMConnect(HintedUser(alice, a.getClient().getHubUrl()), "download");
	EXPECT_EQ(2U, manager->tokens.size());
}

TEST_F(ConnectionManagerTest, ccpm_duplicate_token_is_not_admitted_twice) {
	SettingsManager::getInstance()->set(SettingsManager::ENABLE_CCPM, true);
	auto alice = user(1);
	auto first = pmPeer(alice, "100");
	auto duplicate = pmPeer(alice, "100");
	manager->addNewConnection(first, CONNECTION_TYPE_PM);
	manager->addNewConnection(duplicate, CONNECTION_TYPE_PM);
	EXPECT_EQ(1, connected);
	EXPECT_FALSE(duplicate->isSet(UserConnection::FLAG_ASSOCIATED));
}

TEST_F(ConnectionManagerTest, ccpm_equal_tokens_use_the_tcp_initiator_as_tiebreaker) {
	SettingsManager::getInstance()->set(SettingsManager::ENABLE_CCPM, true);
	auto alice = user(1);
	const bool preferIncoming = alice->getCID().toBase32() < ClientManager::getInstance()->getMyCID().toBase32();
	auto first = pmPeer(alice, "100", !preferIncoming);
	auto preferred = pmPeer(alice, "100", preferIncoming);
	manager->addNewConnection(first, CONNECTION_TYPE_PM);
	manager->addNewConnection(preferred, CONNECTION_TYPE_PM);
	EXPECT_EQ(2, connected);
	EXPECT_TRUE(manager->isPMConnectionActive(alice, preferred->getToken()));
	EXPECT_FALSE(manager->isPMConnectionActive(alice, first->getToken()));
}

TEST_F(ConnectionManagerTest, ccpm_other_hub_cannot_supersede_the_selected_route) {
	SettingsManager::getInstance()->set(SettingsManager::ENABLE_CCPM, true);
	auto alice = user(1);
	auto first = pmPeer(alice, "200");
	auto otherHub = pmPeer(alice, "100");
	otherHub->setHubUrl("adcs://other.example");
	manager->addNewConnection(first, CONNECTION_TYPE_PM);
	manager->addNewConnection(otherHub, CONNECTION_TYPE_PM);
	EXPECT_EQ(1, connected);
	EXPECT_TRUE(manager->isPMConnectionActive(alice, first->getToken()));
	EXPECT_FALSE(otherHub->isSet(UserConnection::FLAG_ASSOCIATED));
}

TEST_F(ConnectionManagerTest, adc_handshake_rejects_an_unexpected_peer_identity) {
	auto uc = pmPeer(user(1), "100");
	uc->setState(UserConnection::STATE_INF);
	manager->on(AdcCommand::INF(), uc,
		AdcCommand(AdcCommand::CMD_INF).addParam("ID", user(2)->getCID().toBase32()).addParam("PM", "1"));
	EXPECT_EQ(0U, manager->userConnections.size());
}

TEST_F(ConnectionManagerTest, adc_upload_tokens_are_scoped_to_the_peer) {
	auto alice = user(1), bob = user(2);
	auto first = peer(alice, "same-token", false, true, true);
	auto second = peer(bob, "same-token", false, true, true);
	manager->addNewConnection(first, CONNECTION_TYPE_UPLOAD);
	manager->addNewConnection(second, CONNECTION_TYPE_UPLOAD);
	EXPECT_EQ(2, connected);
	EXPECT_NE(first->getToken(), second->getToken());
	auto duplicate = peer(alice, "same-token", false, true, true);
	manager->addNewConnection(duplicate, CONNECTION_TYPE_UPLOAD);
	EXPECT_FALSE(duplicate->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(2, connected);
	auto extra = peer(alice, "another-token", false, true, true);
	manager->addNewConnection(extra, CONNECTION_TYPE_UPLOAD);
	EXPECT_EQ(3, connected);
	first->fire(UserConnectionListener::Failed(), first, string("Connection lost"));
	EXPECT_EQ(2U, manager->cqis[CONNECTION_TYPE_UPLOAD].size());
	EXPECT_TRUE(second->isSet(UserConnection::FLAG_ASSOCIATED));
}

TEST_F(ConnectionManagerTest, adc_download_matching_requires_both_user_and_token) {
	auto alice = user(1), bob = user(2);
	auto token = manager->getCQI(HintedUser(alice, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD).getToken();
	manager->getCQI(HintedUser(bob, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD);
	auto wrong = peer(bob, token, false, false);
	EXPECT_FALSE(manager->checkDownload(wrong));
	manager->addDownloadConnection(wrong);
	EXPECT_FALSE(wrong->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(0, connected);
	auto right = peer(alice, token, false, false);
	EXPECT_TRUE(manager->checkDownload(right));
	manager->addDownloadConnection(right);
	EXPECT_TRUE(right->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(1, connected);
}

TEST_F(ConnectionManagerTest, mcn_download_connections_keep_distinct_queue_ids) {
	auto u = user(1);
	StringSet tokens;
	for(int n = 0; n < 2; ++n) {
		auto token = manager->getCQI(HintedUser(u, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD).getToken();
		auto down = peer(u, token, false, false, true);
		manager->addDownloadConnection(down);
		EXPECT_TRUE(down->isSet(UserConnection::FLAG_ASSOCIATED));
		EXPECT_TRUE(down->isMCNNormal());
		EXPECT_EQ(token, down->getToken());
		tokens.insert(down->getToken());
	}
	EXPECT_EQ(2, connected);
	EXPECT_EQ(2U, tokens.size());
	EXPECT_EQ(2U, manager->downloads.size());
}

TEST_F(ConnectionManagerTest, nmdc_handshake_does_not_reset_other_users_retry_errors) {
	const string hub = "dchub://one.example";
	auto alice = UserPtr(new User(ClientManager::getInstance()->makeCid("Alice", hub)));
	auto bob = user(2);
	manager->getCQI(HintedUser(alice, hub), CONNECTION_TYPE_DOWNLOAD).setErrors(3);
	manager->getCQI(HintedUser(bob, hub), CONNECTION_TYPE_DOWNLOAD).setErrors(5);
	auto uc = manager->getConnection(true, false);
	peers.emplace_back(uc);
	uc->setHubUrl(hub);
	uc->setEncoding(Text::utf8);
	uc->setState(UserConnection::STATE_SUPNICK);
	manager->on(UserConnectionListener::MyNick(), uc, "Alice");
	EXPECT_EQ(alice, uc->getUser());
	EXPECT_EQ(0, manager->downloads[0].getErrors());
	EXPECT_EQ(5, manager->downloads[1].getErrors());
}

TEST_F(ConnectionManagerTest, incoming_adc_expectations_with_the_same_token_do_not_overwrite_peers) {
	auto alice = user(1), bob = user(2);
	auto& a = online(alice, "adc://one.example");
	auto& b = online(bob, "adc://two.example");
	manager->addToken("same-token", a, CONNECTION_TYPE_DOWNLOAD);
	manager->addToken("same-token", b, CONNECTION_TYPE_UPLOAD);
	manager->addToken("same-token", a, CONNECTION_TYPE_DOWNLOAD);
	ASSERT_EQ(2U, manager->tokens.size());
	auto first = peer(alice, "same-token", false, false);
	auto second = peer(bob, "same-token", false, true);
	auto firstCheck = manager->checkToken(first);
	EXPECT_TRUE(firstCheck.first);
	EXPECT_EQ(CONNECTION_TYPE_DOWNLOAD, firstCheck.second);
	EXPECT_EQ("adc://one.example", first->getHubUrl());
	auto secondCheck = manager->checkToken(second);
	EXPECT_TRUE(secondCheck.first);
	EXPECT_EQ(CONNECTION_TYPE_UPLOAD, secondCheck.second);
	EXPECT_EQ("adc://two.example", second->getHubUrl());
	EXPECT_TRUE(manager->tokens.empty());
}

TEST_F(ConnectionManagerTest, upload_slot_notifications_keep_the_downloaders_protocol_token) {
	auto u = user(1);
	online(u, "dchub://one.example");
	u->setFlag(User::ONLINE);
	auto up = peer(u, "downloaders-token", false, true);
	manager->addNewConnection(up, CONNECTION_TYPE_UPLOAD);
	ASSERT_TRUE(up->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_NE("downloaders-token", up->getToken());
	auto uploads = UploadManager::getInstance();
	uploads->addFailedUpload(*up, "file.bin");
	uploads->notifyQueuedUsers();
	ASSERT_EQ(1U, hubs.front()->attempts.size());
	EXPECT_EQ("downloaders-token", hubs.front()->attempts.front());
}

TEST_F(ConnectionManagerTest, removing_a_download_preserves_another_peers_identical_token) {
	auto alice = user(1), bob = user(2);
	auto& a = online(alice, "adc://one.example");
	auto& b = online(bob, "adc://two.example");
	auto& cqi = manager->getCQI(HintedUser(alice, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD);
	auto token = cqi.getToken();
	manager->addToken(token, a, CONNECTION_TYPE_DOWNLOAD);
	manager->addToken(token, b, CONNECTION_TYPE_UPLOAD);
	manager->putCQI(cqi);
	EXPECT_TRUE(manager->wasRemovedDownload(token, alice->getCID()));
	EXPECT_FALSE(manager->wasRemovedDownload(token, bob->getCID()));
	auto up = peer(bob, token, false, true);
	EXPECT_TRUE(manager->checkToken(up).first);
	EXPECT_TRUE(manager->tokens.empty());
}

TEST_F(ConnectionManagerTest, mcn_connection_limits_apply_per_user) {
	SettingsManager::getInstance()->set(SettingsManager::MAX_MCN_UPLOADS, 2);
	auto alice = user(1), bob = user(2);
	// Two regular connections plus the reserved priority connection.
	for(int n = 0; n < 3; ++n) {
		auto up = peer(alice, std::to_string(n), false, true, true);
		manager->addNewConnection(up, CONNECTION_TYPE_UPLOAD);
		EXPECT_TRUE(up->isSet(UserConnection::FLAG_ASSOCIATED));
	}
	auto excess = peer(alice, "extra", false, true, true);
	manager->addNewConnection(excess, CONNECTION_TYPE_UPLOAD);
	EXPECT_FALSE(excess->isSet(UserConnection::FLAG_ASSOCIATED));
	auto other = peer(bob, "0", false, true, true);
	manager->addNewConnection(other, CONNECTION_TYPE_UPLOAD);
	EXPECT_TRUE(other->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(4, connected);
}

TEST_F(ConnectionManagerTest, pending_failover_attempt_times_out_without_resetting_its_clock) {
	auto u = user(1);
	online(u, "adc://one.example");
	online(u, "adc://two.example");
	queueFile(u, "adc://one.example");
	auto& cqi = manager->getCQI(HintedUser(u, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD);
	cqi.setState(ConnectionQueueItem::CONNECTING);
	cqi.setLastAttempt(1000);
	cqi.setErrors(1);
	for(uint64_t tick = 2000; tick <= 51000; tick += 1000) {
		manager->on(TimerManagerListener::Second(), tick);
		EXPECT_EQ(1000U, cqi.getLastAttempt());
		EXPECT_EQ(ConnectionQueueItem::CONNECTING, cqi.getState());
	}
	manager->on(TimerManagerListener::Second(), 52000);
	EXPECT_EQ(ConnectionQueueItem::WAITING, cqi.getState());
	EXPECT_EQ(2, cqi.getErrors());
	EXPECT_EQ(1U, failures.size());
	manager->on(TimerManagerListener::Second(), 62000);
	EXPECT_EQ(ConnectionQueueItem::CONNECTING, cqi.getState());
	EXPECT_EQ(1U, hubs[0]->attempts.size());
}

TEST_F(ConnectionManagerTest, retries_use_the_next_eligible_hub_and_lists_keep_their_route) {
	auto u = user(1);
	online(u, "adc://one.example");
	online(u, "adc://two.example");
	queueFile(u, "adc://one.example");
	auto& cqi = manager->getCQI(HintedUser(u, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD);
	cqi.setLastAttempt(1000);
	cqi.setErrors(1);
	manager->on(TimerManagerListener::Second(), 3000);
	EXPECT_TRUE(hubs[0]->attempts.empty());
	EXPECT_EQ(1U, hubs[1]->attempts.size());

	auto listUser = user(2);
	online(listUser, "adc://one.example");
	online(listUser, "adc://two.example");
	QueueManager::getInstance()->addList(HintedUser(listUser, "ADC://ONE.example"), QueueItem::FLAG_CLIENT_VIEW);
	auto routes = manager->getDownloadRoutes(HintedUser(listUser, "adc://one.example"), MCNDownloadType::ANY);
	ASSERT_EQ(1U, routes.size());
	EXPECT_EQ("adc://one.example", routes.front());
	const string wrongHub = "adc://two.example";
	EXPECT_EQ(QueueItem::PAUSED, QueueManager::getInstance()->hasDownload(listUser, MCNDownloadType::ANY, &wrongHub));
}

TEST_F(ConnectionManagerTest, negotiates_zstd_independently_for_adc_and_nmdc) {
#ifdef _WIN32
	WSADATA wsa;
	ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa));
#endif
	// The ADC handler sends INF after SUP. Supply an idle transport and bypass
	// plugins, so that the real handler runs without external connections.
	PluginManager::newInstance();
	PluginManager::getInstance()->shutdown = true;
	auto adc = peer(user(1), "adc-zstd", false, false);
	adc->socket = BufferedSocket::getSocket('\n');
	adc->socket->sock = std::make_unique<Socket>(Socket::TYPE_TCP);
	adc->setState(UserConnection::STATE_SUPNICK);
	manager->on(AdcCommand::SUP(), adc, AdcCommand("CSUP ADBASE ADTIGR ADZLIG ADZST1"));
	EXPECT_TRUE(adc->isSet(UserConnection::FLAG_SUPPORTS_ZSTD_GET));
	EXPECT_TRUE(adc->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET));
	auto nmdc = peer(user(2), "nmdc-zstd", true, false);
	manager->on(UserConnectionListener::Supports(), nmdc, StringList { "ADCGet", "ZLIG", "ZST1" });
	EXPECT_TRUE(nmdc->isSet(UserConnection::FLAG_SUPPORTS_ZSTD_GET));
	EXPECT_TRUE(nmdc->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET));
	auto legacy = peer(user(3), "nmdc-old", true, false);
	manager->on(UserConnectionListener::Supports(), legacy, StringList { "ADCGet", "ZLIG" });
	EXPECT_FALSE(legacy->isSet(UserConnection::FLAG_SUPPORTS_ZSTD_GET));
	EXPECT_TRUE(legacy->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET));
	BufferedSocket::putSocket(adc->socket);
	adc->socket = nullptr;
	BufferedSocket::waitShutdown();
	PluginManager::deleteInstance();
#ifdef _WIN32
	WSACleanup();
#endif
}
