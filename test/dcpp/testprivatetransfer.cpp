/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "testbase.h"

#include <filesystem>
#include <future>
#include <thread>
#define private public
#include <dcpp/UserConnection.h>
#include <dcpp/ConnectionManager.h>
#include <dcpp/SSLSocket.h>
#undef private
#include <dcpp/DirectTransferManager.h>
#include <dcpp/CryptoManager.h>
#include <dcpp/DownloadManager.h>
#include <dcpp/UploadManager.h>
#include <dcpp/ThrottleManager.h>
#include <dcpp/ClientManager.h>
#include <dcpp/LogManager.h>
#include <dcpp/File.h>
#include <dcpp/SSLSocket.h>
#include <dcpp/PluginManager.h>
// TLS, SUP/INF, the manager and storage are real. Only the token registration
// normally done by hub rendezvous is injected; no external hub is contacted.

using namespace dcpp;
#ifdef _WIN32
namespace {

std::atomic<unsigned> privateHookLeaks { 0 };

Bool DCAPI inspectRawHook(dcptr_t, dcptr_t data, dcptr_t, Bool*) {
	const auto line = static_cast<const char*>(data);
	if(line && (strncmp(line, "CPFT", 4) == 0 || strstr(line, " file private/") != nullptr))
		++privateHookLeaks;
	return False;
}

struct TestTLS :
	SSLSocket
{
	using SSLSocket::SSLSocket;
	using Socket::getSock;
};

// Drive both ends of a real nonblocking handshake without helper threads.
bool handshake(TestTLS& client, TestTLS& server, SSL_SESSION* resume = nullptr) {
	Socket listener(Socket::TYPE_TCP);
	listener.setV4only(true);
	listener.setLocalIp4("127.0.0.1");
	const auto port = listener.listen("0");
	client.setV4only(true);
	client.Socket::connect("127.0.0.1", port, "");
	if(!listener.wait(5000, true, false).first)
		throw std::runtime_error("TLS test accept timeout");

	if(!client.Socket::waitConnected(5000))
		throw std::runtime_error("TLS test connect timeout");

	if(resume) {
		client.ssl.reset(SSL_new(client.ctx));
		if(!client.ssl || SSL_set_fd(client.ssl, client.getSock()) != 1 || SSL_set_session(client.ssl, resume) != 1)
			throw std::runtime_error("TLS session setup failed");

		SSL_set_ex_data(client.ssl, CryptoManager::idxVerifyData, client.verifyData.get());
	}

	server.accept(listener);
	bool a = false, b = false;
	const auto deadline = pft::now() + 10000;

	try {
		while(pft::now() < deadline) {
			if(!a)
				a = client.waitConnected(0);
			if(!b)
				b = server.waitAccepted(0);
			if(a && b)
				return true;

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	} catch(const SocketException&) {
		return false;
	}

	return false;
}

class PrivateTransferTest :
	public ::testing::TestWithParam<std::pair<int, bool>>
{
protected:
	struct Endpoint :
		UserConnectionListener
	{
		// UserConnection deletes itself after Failed; ConnectionManager owns its
		// live registry. Never give these sockets independent unique_ptr ownership.
		UserConnection* local = nullptr;
		std::unique_ptr<SSLSocket> remote;
		std::atomic<unsigned> processed { 0 };
		std::atomic<bool> failed { false };

		void on(UserConnectionListener::PrivateTransfer, UserConnection*, const std::string&) noexcept override { ++processed; }

		void on(UserConnectionListener::Failed, UserConnection*, const std::string&) noexcept override { failed = true; }
	};

	std::filesystem::path base;
	HintedUser peer;
	DirectTransferManager* manager = nullptr;
	std::vector<std::unique_ptr<Endpoint>> endpoints;
	std::string peerPin, source;

	void SetUp() override {
		WSADATA data {};
		ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &data));
		base = std::filesystem::current_path() / "build" / ("dcpp-pft-network-" + pft::randomHex(16));
		std::filesystem::create_directory(base);
		Util::PathsMap paths;
		for(auto key : { Util::PATH_USER_CONFIG, Util::PATH_USER_LOCAL, Util::PATH_GLOBAL_CONFIG, Util::PATH_RESOURCES, Util::PATH_LOCALE,
				Util::PATH_DOWNLOADS, Util::PATH_FILE_LISTS, Util::PATH_HUB_LISTS })
			paths[key] = base.u8string() + PATH_SEPARATOR;
		Util::initialize(paths);
		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		ClientManager::newInstance();
		CryptoManager::newInstance();
		// Exercise every transfer and identity check with an exact TLS version.
		for(auto role : { CryptoManager::SSL_CLIENT, CryptoManager::SSL_SERVER }) {
			auto context = CryptoManager::getInstance()->getSSLContext(role);
			ASSERT_EQ(1, SSL_CTX_set_min_proto_version(context, GetParam().first));
			ASSERT_EQ(1, SSL_CTX_set_max_proto_version(context, GetParam().first));
		}
		DownloadManager::newInstance();
		UploadManager::newInstance();
		ThrottleManager::newInstance();
		ConnectionManager::newInstance();
		PluginManager::newInstance();
		privateHookLeaks = 0;
		PluginManager::getInstance()->createHook(HOOK_NETWORK_CONN_IN, inspectRawHook);
		PluginManager::getInstance()->createHook(HOOK_NETWORK_CONN_OUT, inspectRawHook);
		auto settings = SettingsManager::getInstance();
		settings->set(SettingsManager::ENABLE_PRIVATE_SEND, true);
		settings->set(SettingsManager::PFT_REQUIRE_VERIFICATION, GetParam().second);
		settings->set(SettingsManager::ENABLE_CCPM, true);
		settings->set(SettingsManager::ALLOW_UNTRUSTED_CLIENTS, true);
		settings->set(SettingsManager::TLS_CERTIFICATE_FILE, (base / "local.crt").u8string());
		settings->set(SettingsManager::TLS_PRIVATE_KEY_FILE, (base / "local.key").u8string());
		CryptoManager::getInstance()->loadCertificates();
		ASSERT_TRUE(CryptoManager::getInstance()->TLSOk());

		// Give the synthetic peer a different real key pair. The application is
		// the TLS server; only the remote client's context gets this certificate.
		settings->set(SettingsManager::TLS_CERTIFICATE_FILE, (base / "peer.crt").u8string());
		settings->set(SettingsManager::TLS_PRIVATE_KEY_FILE, (base / "peer.key").u8string());
		CryptoManager::getInstance()->generateCertificate();
		auto context = CryptoManager::getInstance()->getSSLContext(CryptoManager::SSL_CLIENT);
		ASSERT_EQ(1, SSL_CTX_use_certificate_file(context, (base / "peer.crt").string().c_str(), SSL_FILETYPE_PEM));
		ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(context, (base / "peer.key").string().c_str(), SSL_FILETYPE_PEM));
		DirectTransferManager::newInstance();
		manager = DirectTransferManager::getInstance();
		uint8_t cid[CID::SIZE] {};
		cid[0] = 123;
		peer = HintedUser(ClientManager::getInstance()->getUser(CID(cid)), "adcs://private-test.invalid:411");
		source = (base / "source.txt").u8string();
		File(source, File::WRITE, File::CREATE | File::TRUNCATE).write("private transfer integration bytes");
	}

	void barrier(UserConnection& local) {
		auto done = std::make_shared<std::promise<void>>();
		auto future = done->get_future();
		local.socket->callAsync([done] { done->set_value(); });
		if(future.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
			throw std::runtime_error("socket barrier timeout");
	}

	void TearDown() override {
		if(!SETTING(PFT_REQUIRE_VERIFICATION)) {
			EXPECT_TRUE(SETTING(PFT_CONTACT_PINS).empty());
		}
		if(manager)
			manager->shutdown();
		ConnectionManager::getInstance()->shutdown();
		endpoints.clear();
		BufferedSocket::waitShutdown();
		EXPECT_EQ(0u, privateHookLeaks.load());
		if(manager)
			DirectTransferManager::deleteInstance();
		PluginManager::deleteInstance();
		ConnectionManager::deleteInstance();
		ThrottleManager::deleteInstance();
		UploadManager::deleteInstance();
		DownloadManager::deleteInstance();
		CryptoManager::deleteInstance();
		ClientManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		WSACleanup();
		ASSERT_EQ(std::filesystem::current_path() / "build", base.parent_path());
		ASSERT_EQ(0u, base.filename().string().find("dcpp-pft-network-"));
		std::error_code ec;
		std::filesystem::remove_all(base, ec);
		EXPECT_FALSE(ec) << ec.message();
	}

	Endpoint& connect(bool privateData = false, const std::string& attempt = "") {
		auto e = std::make_unique<Endpoint>();
		e->local = ConnectionManager::getInstance()->getConnection(false, true);
		e->local->setState(UserConnection::STATE_SUPNICK);
		e->local->setFlag(UserConnection::FLAG_INCOMING);
		e->local->addListener(e.get());
		Socket listener(Socket::TYPE_TCP);
		listener.setV4only(true);
		listener.setLocalIp4("127.0.0.1");
		auto port = listener.listen("0");
		e->remote = std::make_unique<SSLSocket>(CryptoManager::SSL_CLIENT, true, "", "");
		e->remote->setV4only(true);
		e->remote->connect("127.0.0.1", port);
		if(!listener.wait(5000, true, false).first)
			throw std::runtime_error("accept timeout");

		e->local->accept(listener);
		const auto deadline = pft::now() + 10000;
		while(!e->remote->waitConnected(25))
			if(pft::now() >= deadline)
				throw std::runtime_error("TLS timeout");

		EXPECT_EQ(GetParam().first, SSL_version(e->remote->ssl));
		barrier(*e->local);
		endpoints.push_back(std::move(e));
		auto& connected = *endpoints.back();
		write(connected, "CSUP ADBASE ADTIGR ADPFT1\n");
		const auto support = read(connected);
		if(support.compare(0, 5, "CSUP ") != 0)
			throw std::runtime_error("missing SUP");

		const auto token = privateData ? attempt : pft::randomHex(16);
		{
			auto cm = ConnectionManager::getInstance();
			Lock lock(cm->cs);
			cm->tokens.emplace(token,
				ConnectionManager::TokenInfo {
					peer.user->getCID(), privateData ? CONNECTION_TYPE_PRIVATE : CONNECTION_TYPE_PM, peer.hint });
		}

		write(connected, "CINF ID" + peer.user->getCID().toBase32() + " TO" + token + (privateData ? " PF1\n" : " PM1\n"));
		const auto identity = read(connected);
		if(identity.compare(0, 5, "CINF ") != 0 || identity.find(privateData ? " PF1" : " PM1") == std::string::npos)
			throw std::runtime_error("missing private INF role");

		return connected;
	}

	void trust(Endpoint& control) {
		ASSERT_TRUE(control.local->isPrivateTransferSecure());
		ASSERT_TRUE(control.remote->isPrivateTransferSecure());
		peerPin = manager->peerFingerprint(peer);
		ASSERT_NE(peerPin, manager->ownFingerprint());
		EXPECT_THROW(manager->approvePeer(peer, std::string(64, '0')), Exception);
		if(SETTING(PFT_REQUIRE_VERIFICATION))
			manager->approvePeer(peer, peerPin);
	}

	void write(Endpoint& e, const std::string& text) {
		size_t pos = 0;
		const auto deadline = pft::now() + 10000;
		while(pos < text.size()) {
			const auto n = e.remote->write(text.data() + pos, static_cast<int>(text.size() - pos));
			if(n > 0)
				pos += n;
			else
				e.remote->wait(25, false, true);
			if(pft::now() >= deadline)
				throw std::runtime_error("TLS write timeout");
		}
	}

	void command(Endpoint& e, const std::string& text) {
		const auto previous = e.processed.load();
		write(e, text);
		const auto deadline = pft::now() + 10000;
		while(e.processed.load() == previous) {
			if(pft::now() >= deadline)
				throw std::runtime_error("record dispatch timeout");

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	void waitDisconnected(Endpoint& e) {
		const auto deadline = pft::now() + 10000;
		while(!e.failed.load()) {
			if(pft::now() >= deadline)
				throw std::runtime_error("disconnect timeout");

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	std::string read(Endpoint& e, size_t size = 0) {
		std::string result;
		const auto deadline = pft::now() + 10000;
		while(!size || result.size() < size) {
			char buffer[64 * 1024];
			const auto wanted = size ? std::min<size_t>(sizeof(buffer), size - result.size()) : 1;
			const auto n = e.remote->read(buffer, static_cast<int>(wanted));
			if(n > 0) {
				if(!size && buffer[0] == '\n')
					return result;

				result.append(buffer, n);
			} else if(n == 0)
				throw std::runtime_error("unexpected TLS EOF");
			else
				e.remote->wait(10, true, false);
			if(pft::now() >= deadline)
				throw std::runtime_error("TLS read timeout");
		}

		return result;
	}

	void tick() { TimerManager::getInstance()->fire(TimerManagerListener::Second(), GET_TICK()); }

	DirectTransferManager::View state(const std::string& id) {
		for(const auto& row : manager->snapshot(peer))
			if(row.id == id)
				return row;

		throw std::runtime_error("missing private offer");
	}

	void waitState(const std::string& id, DirectTransferManager::State expected) {
		const auto deadline = pft::now() + 10000;
		while(pft::now() < deadline) {
			tick();
			for(const auto& row : manager->snapshot(peer))
				if(row.id == id && row.state == expected)
					return;

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		throw std::runtime_error("state timeout: " + state(id).status);
	}

	pft::Metadata incoming(const std::string& body) {
		File(source, File::WRITE, File::CREATE | File::TRUNCATE).write(body);
		auto f = pft::PrivateFile::openSource(source);
		auto hashes = f->hashes(f->size(), [] { return false; });

		return { pft::randomHex(16), peer.user->getCID().toBase32(), ClientManager::getInstance()->getMyCID().toBase32(), peerPin,
			manager->ownFingerprint(), "received.txt", hashes.sha256, hashes.tth, body.size(), 900 };
	}
};
}

TEST_P(PrivateTransferTest, sender_requires_approval_and_transfers_over_dedicated_tls) {
	auto& control = connect();
	if(SETTING(PFT_REQUIRE_VERIFICATION)) {
		try {
			manager->checkSend(peer);
			FAIL() << "Unverified contact passed the send preflight";
		} catch(const Exception& e) {
			EXPECT_NE(std::string::npos, e.getError().find("Verify contact"));
		}
		EXPECT_THROW(manager->offer(peer, source), Exception);
	} else {
		EXPECT_NO_THROW(manager->checkSend(peer));
	}
	EXPECT_TRUE(manager->snapshot(peer).empty());
	trust(control);
	EXPECT_NO_THROW(manager->checkSend(peer));
	const auto id = manager->offer(peer, source);
	const auto preparing = manager->snapshot(peer);
	ASSERT_EQ(1u, preparing.size());
	EXPECT_EQ(id, preparing.front().id);
	EXPECT_EQ(Util::getFileName(source), preparing.front().name);
	EXPECT_DOUBLE_EQ(0.0, preparing.front().percent);
	EXPECT_DOUBLE_EQ(0.0, preparing.front().bytesPerSecond);
	waitState(id, DirectTransferManager::State::Offered);
	const auto offer = pft::Record::decode(read(control));
	auto m = pft::Metadata::fromRecord(offer);
	write(control, pft::Record { pft::Op::Accept, { id, m.digest(), pft::randomHex(16) } }.encode());
	const auto grant = pft::Record::decode(read(control));
	ASSERT_EQ(pft::Op::Grant, grant.op);
	auto& data = connect(true, grant.fields[3]);
	write(data, pft::Record { pft::Op::Auth, { id, m.digest(), grant.fields[3], grant.fields[4] } }.encode());
	EXPECT_EQ(pft::Op::Ready, pft::Record::decode(read(data)).op);
	write(data, "CGET file private/" + id + " 0 " + std::to_string(m.size) + "\n");
	EXPECT_EQ("CSND file private/" + id + " 0 " + std::to_string(m.size), read(data));
	EXPECT_EQ("private transfer integration bytes", read(data, m.size));
	waitState(id, DirectTransferManager::State::AwaitingReceipt);
	EXPECT_DOUBLE_EQ(100.0, state(id).percent);
	EXPECT_DOUBLE_EQ(0.0, state(id).bytesPerSecond);
	const auto receipt = pft::Record { pft::Op::Receipt, { id, m.digest(), pft::randomHex(16) } };
	write(control, receipt.encode());
	EXPECT_EQ(pft::Op::Ack, pft::Record::decode(read(control)).op);
	waitState(id, DirectTransferManager::State::Completed);
	write(control, receipt.encode());
	EXPECT_EQ(pft::Op::Ack, pft::Record::decode(read(control)).op);
	EXPECT_EQ("private transfer integration bytes", File(source, File::READ, File::OPEN).read());
}

TEST_P(PrivateTransferTest, receiver_requires_consent_and_publishes_only_verified_bytes) {
	auto& control = connect();
	trust(control);
	const std::string body = "received over real TLS";
	auto m = incoming(body);
	write(control, m.record().encode());
	waitState(m.id, DirectTransferManager::State::Offered);
	const auto all = manager->snapshot();
	ASSERT_EQ(1u, all.size());
	EXPECT_EQ(m.id, all.front().id);
	EXPECT_EQ(peer.user, all.front().contact.user);
	EXPECT_EQ(peer.hint, all.front().contact.hint);
	const HintedUser unrelated(UserPtr(new User(CID::generate())), peer.hint);
	EXPECT_TRUE(manager->snapshot(unrelated).empty());
	EXPECT_FALSE(std::filesystem::exists(base / "PrivateSend"));
	const auto downloads = base / "downloads";
	std::filesystem::create_directory(downloads);
	SettingsManager::getInstance()->set(SettingsManager::DOWNLOAD_DIRECTORY, downloads.u8string());
	const auto target = GetParam().second ? (base / "chosen name.txt").u8string() : (downloads / m.name).u8string();
	manager->accept(m.id, GetParam().second ? target : std::string());
	EXPECT_EQ(target, state(m.id).destinationPath);
	EXPECT_TRUE(state(m.id).savedPath.empty());
	EXPECT_FALSE(std::filesystem::exists(target));
	auto accepted = pft::Record::decode(read(control));
	ASSERT_EQ(pft::Op::Accept, accepted.op);
	const auto attempt = pft::randomHex(16), secret = pft::randomHex(32);
	command(control,
		pft::Record { pft::Op::Grant, { m.id, m.digest(), accepted.fields[2], attempt, secret, "0", std::to_string(m.size), "60", "3600" } }
			.encode());
	auto& data = connect(true, attempt);
	auto auth = pft::Record::decode(read(data));
	ASSERT_EQ(pft::Op::Auth, auth.op);
	EXPECT_EQ(secret, auth.fields[3]);
	write(data, pft::Record { pft::Op::Ready, { m.id, m.digest(), attempt } }.encode());
	EXPECT_EQ("CGET file private/" + m.id + " 0 " + std::to_string(m.size), read(data));
	write(data, "CSND file private/" + m.id + " 0 " + std::to_string(m.size) + "\n" + body);
	waitState(m.id, DirectTransferManager::State::Completed);
	auto receipt = pft::Record::decode(read(control));
	EXPECT_EQ(pft::Op::Receipt, receipt.op);
	write(control, pft::Record { pft::Op::Ack, receipt.fields }.encode());
	EXPECT_EQ(target, state(m.id).savedPath);
	EXPECT_EQ(body, File(state(m.id).savedPath, File::READ, File::OPEN).read());
	EXPECT_TRUE(pft::isPrivatePath(state(m.id).savedPath));
}

TEST_P(PrivateTransferTest, disable_and_contact_revocation_cancel_pending_offers) {
	SettingsManager::getInstance()->set(SettingsManager::PFT_REQUIRE_VERIFICATION, true);
	manager->configure();
	auto& control = connect();
	trust(control);
	auto id = manager->offer(peer, source);
	waitState(id, DirectTransferManager::State::Offered);
	read(control);
	manager->forgetPeer(peer);
	EXPECT_EQ(DirectTransferManager::State::Cancelled, state(id).state);
	EXPECT_THROW(manager->offer(peer, source), Exception);
	manager->approvePeer(peer, peerPin);
	id = manager->offer(peer, source);
	SettingsManager::getInstance()->set(SettingsManager::ENABLE_PRIVATE_SEND, false);
	manager->configure();
	EXPECT_EQ(DirectTransferManager::State::Cancelled, state(id).state);
	EXPECT_THROW(manager->offer(peer, source), Exception);
}

TEST_P(PrivateTransferTest, testing_mode_can_be_revoked_without_saving_contact_approval) {
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::PFT_REQUIRE_VERIFICATION, false);
	manager->configure();
	EXPECT_THROW(manager->checkSend(peer), Exception);
	auto& control = connect();
	EXPECT_THROW(manager->checkSend(HintedUser(peer.user, "adc://private-test.invalid:411")), Exception);
	EXPECT_NO_THROW(manager->checkSend(peer));
	auto id = manager->offer(peer, source);
	waitState(id, DirectTransferManager::State::Offered);
	read(control);
	manager->forgetPeer(peer);
	EXPECT_EQ(DirectTransferManager::State::Cancelled, state(id).state);
	id = manager->offer(peer, source);
	waitState(id, DirectTransferManager::State::Offered);
	read(control);
	EXPECT_TRUE(settings->get(SettingsManager::PFT_CONTACT_PINS).empty());
	settings->set(SettingsManager::PFT_REQUIRE_VERIFICATION, true);
	manager->configure();
	EXPECT_EQ(DirectTransferManager::State::Cancelled, state(id).state);
	EXPECT_THROW(manager->checkSend(peer), Exception);
	EXPECT_THROW(manager->offer(peer, source), Exception);
}

TEST_P(PrivateTransferTest, legacy_tls_is_rejected) {
	auto context = CryptoManager::getInstance()->getSSLContext(CryptoManager::SSL_CLIENT);
	ASSERT_EQ(1, SSL_CTX_set_min_proto_version(context, TLS1_VERSION));
	ASSERT_EQ(1, SSL_CTX_set_max_proto_version(context, TLS1_1_VERSION));
	TestTLS client(CryptoManager::SSL_CLIENT, true, "", ""), server(CryptoManager::SSL_SERVER, true, "", "");
	EXPECT_FALSE(handshake(client, server));
	EXPECT_FALSE(client.isPrivateTransferSecure());
	EXPECT_FALSE(server.isPrivateTransferSecure());
}

TEST_P(PrivateTransferTest, tls12_cbc_and_static_rsa_are_rejected) {
	for(const auto cipher : { "ECDHE-RSA-AES128-SHA256", "AES128-GCM-SHA256" }) {
		SCOPED_TRACE(cipher);
		for(auto role : { CryptoManager::SSL_CLIENT, CryptoManager::SSL_SERVER }) {
			auto context = CryptoManager::getInstance()->getSSLContext(role);
			ASSERT_EQ(1, SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION));
			ASSERT_EQ(1, SSL_CTX_set_max_proto_version(context, TLS1_2_VERSION));
			ASSERT_EQ(1, SSL_CTX_set_cipher_list(context, cipher));
		}

		TestTLS client(CryptoManager::SSL_CLIENT, true, "", ""), server(CryptoManager::SSL_SERVER, true, "", "");
		ASSERT_TRUE(handshake(client, server));
		ASSERT_EQ(TLS1_2_VERSION, SSL_version(client.ssl));
		EXPECT_FALSE(client.isPrivateTransferSecure());
		EXPECT_FALSE(server.isPrivateTransferSecure());
	}
}

TEST_P(PrivateTransferTest, tls13_is_preferred_when_both_versions_are_enabled) {
	for(auto role : { CryptoManager::SSL_CLIENT, CryptoManager::SSL_SERVER }) {
		auto context = CryptoManager::getInstance()->getSSLContext(role);
		ASSERT_EQ(1, SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION));
		ASSERT_EQ(1, SSL_CTX_set_max_proto_version(context, 0));
	}

	TestTLS client(CryptoManager::SSL_CLIENT, true, "", ""), server(CryptoManager::SSL_SERVER, true, "", "");
	ASSERT_TRUE(handshake(client, server));
	EXPECT_EQ(TLS1_3_VERSION, SSL_version(client.ssl));
	EXPECT_TRUE(client.isPrivateTransferSecure());
	EXPECT_TRUE(server.isPrivateTransferSecure());
}

TEST_P(PrivateTransferTest, empty_file_has_no_zero_length_data_mode) {
	auto& control = connect();
	trust(control);
	auto m = incoming("");
	write(control, m.record().encode());
	waitState(m.id, DirectTransferManager::State::Offered);
	EXPECT_DOUBLE_EQ(0.0, state(m.id).percent);
	manager->accept(m.id);
	auto accepted = pft::Record::decode(read(control));
	const auto attempt = pft::randomHex(16), secret = pft::randomHex(32);
	command(control,
		pft::Record { pft::Op::Grant, { m.id, m.digest(), accepted.fields[2], attempt, secret, "0", "0", "60", "3600" } }.encode());
	auto& data = connect(true, attempt);
	ASSERT_EQ(pft::Op::Auth, pft::Record::decode(read(data)).op);
	write(data, pft::Record { pft::Op::Ready, { m.id, m.digest(), attempt } }.encode());
	waitState(m.id, DirectTransferManager::State::Completed);
	EXPECT_EQ(0, File::getSize(state(m.id).savedPath));
	EXPECT_DOUBLE_EQ(100.0, state(m.id).percent);
	EXPECT_DOUBLE_EQ(0.0, state(m.id).bytesPerSecond);
	EXPECT_EQ(pft::Op::Receipt, pft::Record::decode(read(control)).op);
}

TEST_P(PrivateTransferTest, damaged_payload_is_removed_without_receipt) {
	auto& control = connect();
	trust(control);
	auto m = incoming("good");
	write(control, m.record().encode());
	waitState(m.id, DirectTransferManager::State::Offered);
	manager->accept(m.id);
	auto accepted = pft::Record::decode(read(control));
	const auto attempt = pft::randomHex(16), secret = pft::randomHex(32);
	command(control,
		pft::Record { pft::Op::Grant, { m.id, m.digest(), accepted.fields[2], attempt, secret, "0", "4", "60", "3600" } }.encode());
	auto& data = connect(true, attempt);
	read(data);
	write(data, pft::Record { pft::Op::Ready, { m.id, m.digest(), attempt } }.encode());
	read(data);
	write(data, "CSND file private/" + m.id + " 0 4\nBAD!");
	waitState(m.id, DirectTransferManager::State::Failed);
	EXPECT_TRUE(state(m.id).savedPath.empty());
	for(const auto& entry : std::filesystem::recursive_directory_iterator(base / "PrivateSend"))
		EXPECT_FALSE(entry.is_regular_file());
}

TEST_P(PrivateTransferTest, resumed_receiver_uses_new_attempt_and_verified_prefix) {
	auto& control = connect();
	trust(control);
	auto m = incoming("prefixsuffix");
	write(control, m.record().encode());
	waitState(m.id, DirectTransferManager::State::Offered);
	manager->accept(m.id);
	auto accepted = pft::Record::decode(read(control));
	auto attempt = pft::randomHex(16), secret = pft::randomHex(32);
	command(control,
		pft::Record { pft::Op::Grant, { m.id, m.digest(), accepted.fields[2], attempt, secret, "0", "12", "60", "3600" } }.encode());
	auto& data = connect(true, attempt);
	read(data);
	write(data, pft::Record { pft::Op::Ready, { m.id, m.digest(), attempt } }.encode());
	read(data);
	write(data, "CSND file private/" + m.id + " 0 12\nprefix");
	const auto deadline = pft::now() + 10000;
	while(state(m.id).bytes != 6 && pft::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	ASSERT_EQ(6u, state(m.id).bytes);
	EXPECT_DOUBLE_EQ(50.0, state(m.id).percent);
	EXPECT_GT(state(m.id).bytesPerSecond, 0.0);
	manager->pause(m.id);
	EXPECT_DOUBLE_EQ(50.0, state(m.id).percent);
	EXPECT_DOUBLE_EQ(0.0, state(m.id).bytesPerSecond);
	EXPECT_EQ(pft::Op::Pause, pft::Record::decode(read(control)).op);
	waitDisconnected(data);
	manager->resume(m.id);
	waitState(m.id, DirectTransferManager::State::Connecting);
	auto resume = pft::Record::decode(read(control));
	ASSERT_EQ(pft::Op::Resume, resume.op);
	EXPECT_EQ("6", resume.fields[3]);
	EXPECT_EQ(pft::sha256("prefix", 6), resume.fields[4]);
	attempt = pft::randomHex(16);
	secret = pft::randomHex(32);
	command(
		control, pft::Record { pft::Op::Grant, { m.id, m.digest(), resume.fields[2], attempt, secret, "6", "12", "60", "3600" } }.encode());
	auto& resumed = connect(true, attempt);
	auto auth = pft::Record::decode(read(resumed));
	EXPECT_EQ(attempt, auth.fields[2]);
	EXPECT_EQ(secret, auth.fields[3]);
	write(resumed, pft::Record { pft::Op::Ready, { m.id, m.digest(), attempt } }.encode());
	EXPECT_EQ("CGET file private/" + m.id + " 6 6", read(resumed));
	write(resumed, "CSND file private/" + m.id + " 6 6\n");
	waitState(m.id, DirectTransferManager::State::Receiving);
	EXPECT_DOUBLE_EQ(50.0, state(m.id).percent);
	EXPECT_DOUBLE_EQ(0.0, state(m.id).bytesPerSecond);
	write(resumed, "suffix");
	waitState(m.id, DirectTransferManager::State::Completed);
	EXPECT_DOUBLE_EQ(100.0, state(m.id).percent);
	EXPECT_DOUBLE_EQ(0.0, state(m.id).bytesPerSecond);
	EXPECT_EQ("prefixsuffix", File(state(m.id).savedPath, File::READ, File::OPEN).read());
}

TEST_P(PrivateTransferTest, wrong_capability_cannot_read_any_source_bytes) {
	auto& control = connect();
	trust(control);
	const auto id = manager->offer(peer, source);
	waitState(id, DirectTransferManager::State::Offered);
	const auto m = pft::Metadata::fromRecord(pft::Record::decode(read(control)));
	write(control, pft::Record { pft::Op::Accept, { id, m.digest(), pft::randomHex(16) } }.encode());
	const auto grant = pft::Record::decode(read(control));
	auto& data = connect(true, grant.fields[3]);
	write(data, pft::Record { pft::Op::Auth, { id, m.digest(), grant.fields[3], std::string(64, '0') } }.encode());
	waitDisconnected(data);
	EXPECT_ANY_THROW(read(data));
	EXPECT_EQ(0u, state(id).bytes);
	manager->cancel(id);
	EXPECT_EQ(DirectTransferManager::State::Cancelled, state(id).state);
}

TEST_P(PrivateTransferTest, declined_and_expired_offers_never_create_receive_files) {
	auto& control = connect();
	trust(control);
	auto m = incoming("private");
	write(control, m.record().encode());
	waitState(m.id, DirectTransferManager::State::Offered);
	manager->decline(m.id);
	EXPECT_EQ(pft::Op::Decline, pft::Record::decode(read(control)).op);
	EXPECT_THROW(manager->accept(m.id), Exception);
	m.id = pft::randomHex(16);
	m.offerSeconds = 1;
	write(control, m.record().encode());
	waitState(m.id, DirectTransferManager::State::Offered);
	waitState(m.id, DirectTransferManager::State::Expired);
	EXPECT_THROW(manager->accept(m.id), Exception);
	EXPECT_FALSE(std::filesystem::exists(base / "PrivateSend"));
}

TEST_P(PrivateTransferTest, absent_peer_certificate_fails_tls_admission) {
	std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> empty(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
	ASSERT_TRUE(empty);
	ASSERT_EQ(1, SSL_CTX_set_min_proto_version(empty.get(), GetParam().first));
	ASSERT_EQ(1, SSL_CTX_set_max_proto_version(empty.get(), GetParam().first));
	TestTLS client(CryptoManager::SSL_CLIENT, true, "", ""), server(CryptoManager::SSL_SERVER, true, "", "");
	client.ctx = empty.get();
	EXPECT_FALSE(handshake(client, server));
	EXPECT_FALSE(server.isPrivateTransferSecure());
}

TEST_P(PrivateTransferTest, resumed_tls_is_rejected_and_early_data_is_disabled) {
	auto crypto = CryptoManager::getInstance();

	// Production does not configure a session-id context, so OpenSSL already
	// refuses mutual-auth resumption. Enable it only here to exercise our own
	// admission guard even if shared context policy changes in the future.
	const unsigned char contextID[] = "PFT1-negative-test";
	ASSERT_EQ(1, SSL_CTX_set_session_id_context(crypto->getSSLContext(CryptoManager::SSL_SERVER), contextID, sizeof(contextID) - 1));
	EXPECT_EQ(0u, SSL_CTX_get_max_early_data(crypto->getSSLContext(CryptoManager::SSL_CLIENT)));
	EXPECT_EQ(0u, SSL_CTX_get_max_early_data(crypto->getSSLContext(CryptoManager::SSL_SERVER)));
	TestTLS first(CryptoManager::SSL_CLIENT, true, "", ""), server(CryptoManager::SSL_SERVER, true, "", "");
	ASSERT_TRUE(handshake(first, server));
	ASSERT_EQ(GetParam().first, SSL_version(first.ssl));
	ASSERT_TRUE(first.isPrivateTransferSecure());
	ASSERT_EQ(1, server.write("x", 1));
	char byte = 0;
	const auto deadline = pft::now() + 10000;
	while(first.read(&byte, 1) != 1 && pft::now() < deadline)
		first.wait(10, true, false);
	ASSERT_EQ('x', byte);
	std::unique_ptr<SSL_SESSION, decltype(&SSL_SESSION_free)> session(SSL_get1_session(first.ssl), SSL_SESSION_free);
	ASSERT_TRUE(session);
	ASSERT_TRUE(SSL_SESSION_is_resumable(session.get()));
	TestTLS resumed(CryptoManager::SSL_CLIENT, true, "", ""), resumedServer(CryptoManager::SSL_SERVER, true, "", "");
	ASSERT_TRUE(handshake(resumed, resumedServer, session.get()));
	ASSERT_TRUE(SSL_session_reused(resumed.ssl));
	ASSERT_TRUE(SSL_session_reused(resumedServer.ssl));
	EXPECT_FALSE(resumed.isPrivateTransferSecure());
	EXPECT_FALSE(resumedServer.isPrivateTransferSecure());
}

TEST_P(PrivateTransferTest, local_certificate_rotation_revokes_existing_offers) {
	auto& control = connect();
	trust(control);
	const auto id = manager->offer(peer, source);
	waitState(id, DirectTransferManager::State::Offered);
	read(control);
	const auto original = manager->ownFingerprint();
	CryptoManager::getInstance()->loadCertificates();
	ASSERT_NE(original, manager->ownFingerprint());
	waitState(id, DirectTransferManager::State::Cancelled);
}
#endif

#ifdef _WIN32
TEST_P(PrivateTransferTest, early_receipt_waits_for_local_transmit_completion) {
	auto& control = connect();
	trust(control);
	const std::string payload(2 * 1024 * 1024, 'x');
	File(source, File::WRITE, File::CREATE | File::TRUNCATE).write(payload);
	const auto id = manager->offer(peer, source);
	waitState(id, DirectTransferManager::State::Offered);
	const auto m = pft::Metadata::fromRecord(pft::Record::decode(read(control)));
	write(control, pft::Record { pft::Op::Accept, { id, m.digest(), pft::randomHex(16) } }.encode());
	const auto grant = pft::Record::decode(read(control));
	auto& data = connect(true, grant.fields[3]);
	write(data, pft::Record { pft::Op::Auth, { id, m.digest(), grant.fields[3], grant.fields[4] } }.encode());
	read(data);
	command(data, "CGET file private/" + id + " 0 " + std::to_string(m.size) + "\n");
	command(control, pft::Record { pft::Op::Receipt, { id, m.digest(), pft::randomHex(16) } }.encode());
	EXPECT_NE(DirectTransferManager::State::Failed, state(id).state);
	EXPECT_EQ("CSND file private/" + id + " 0 " + std::to_string(m.size), read(data));
	EXPECT_EQ(payload, read(data, payload.size()));
	waitState(id, DirectTransferManager::State::Completed);
	EXPECT_EQ(pft::Op::Ack, pft::Record::decode(read(control)).op);
}
INSTANTIATE_TEST_SUITE_P(PrivateSendTLS, PrivateTransferTest,
	::testing::Values(std::make_pair(TLS1_2_VERSION, true), std::make_pair(TLS1_3_VERSION, true),
		std::make_pair(TLS1_2_VERSION, false), std::make_pair(TLS1_3_VERSION, false)),
	[](const ::testing::TestParamInfo<std::pair<int, bool>>& info) {
		return std::string(info.param.first == TLS1_2_VERSION ? "TLS12" : "TLS13") + (info.param.second ? "Verified" : "Testing");
	});
#endif
