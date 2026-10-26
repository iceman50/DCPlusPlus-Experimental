/* Copyright (C) 2026 iceman50 */

#include "testbase.h"

#include <dcpp/net/HttpConnection.h>
#include <dcpp/net/HttpManager.h>
#include <dcpp/system/Streams.h>
#include <dcpp/system/TimerManager.h>

using namespace dcpp;

namespace {

class TestTransport :
	public HttpConnection
{
public:
	bool abortEmitsFailure = false;
	bool aborted = false;

	void download() override {}

	void abort() override {
		aborted = true;
		if(abortEmitsFailure)
			fire(HttpConnectionListener::Failed(), this, string("Cancelled"));
	}

	void finish() {
		const uint8_t body[] = { 'd', 'a', 't', 'a' };
		fire(HttpConnectionListener::Data(), this, body, sizeof(body));
		fire(HttpConnectionListener::Complete(), this);
	}
};

class HttpManagerTest :
	public ::testing::Test,
	public HttpManagerListener
{
public:
	void SetUp() override {
		TimerManager::newInstance();
		HttpManager::newInstance();
		HttpManager::getInstance()->addListener(this);
	}

	void TearDown() override {
		HttpManager::getInstance()->removeListener(this);
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
	}

	void on(Added, HttpConnection*) noexcept override { ++added; }

	void on(Updated, HttpConnection*, const uint8_t*, size_t n) noexcept override { bytes += n; }

	void on(Complete, HttpConnection*, OutputStream*) noexcept override { ++completed; }

	void on(Failed, HttpConnection*, const string&) noexcept override { ++failed; }

	void on(Removed, HttpConnection*) noexcept override { ++removed; }

	int added = 0, completed = 0, failed = 0, removed = 0;
	size_t bytes = 0;
};
}

TEST_F(HttpManagerTest, CustomTransportWritesDataAndReportsCompletion) {
	auto connection = std::make_shared<TestTransport>();
	StringOutputStream output;
	int callbacks = 0;
	HttpManager::getInstance()->download(
		connection, "https://example.test/Country.mmdb", &output, [&](const HttpConnection*, OutputStream*, Flags::MaskType flags) {
			if(flags & HttpManager::CONN_COMPLETE)
				++callbacks;
		});
	connection->finish();
	EXPECT_EQ(1, added);
	EXPECT_EQ(4U, bytes);
	EXPECT_EQ("data", output.stringRef());
	EXPECT_EQ(1, completed);
	EXPECT_EQ(1, callbacks);
}

TEST_F(HttpManagerTest, CancelTargetsActiveRetryAndDeliversOneTerminalCallback) {
	auto completedConnection = std::make_shared<TestTransport>();
	HttpManager::getInstance()->download(completedConnection, "https://example.test/Country.mmdb", nullptr);
	completedConnection->finish();
	auto retry = std::make_shared<TestTransport>();
	retry->abortEmitsFailure = true;
	int callbacks = 0;
	HttpManager::getInstance()->download(
		retry, "https://example.test/Country.mmdb", nullptr, [&](const HttpConnection*, OutputStream*, Flags::MaskType flags) {
			if(flags & HttpManager::CONN_FAILED)
				++callbacks;
		});
	HttpManager::getInstance()->disconnect("https://example.test/Country.mmdb");
	EXPECT_FALSE(completedConnection->aborted);
	EXPECT_TRUE(retry->aborted);
	EXPECT_EQ(1, failed);
	EXPECT_EQ(1, callbacks);
}

TEST_F(HttpManagerTest, CancelLegacyTransportAlsoDeliversTerminalCallback) {
	auto connection = std::make_shared<TestTransport>();
	int callbacks = 0;
	HttpManager::getInstance()->download(
		connection, "https://example.test/Country.mmdb", nullptr, [&](const HttpConnection*, OutputStream*, Flags::MaskType flags) {
			if(flags & HttpManager::CONN_FAILED)
				++callbacks;
		});
	HttpManager::getInstance()->disconnect(connection->getUrl());
	EXPECT_EQ(1, failed);
	EXPECT_EQ(1, callbacks);
}

TEST_F(HttpManagerTest, ExpiringOldAttemptDoesNotRemoveActiveTransferRow) {
	auto old = std::make_shared<TestTransport>();
	HttpManager::getInstance()->download(old, "https://example.test/Country.mmdb", nullptr);
	old->finish();
	auto current = std::make_shared<TestTransport>();
	HttpManager::getInstance()->download(current, old->getUrl(), nullptr);
	TimerManager::getInstance()->fire(TimerManagerListener::Minute(), GET_TICK() + 61000);
	EXPECT_EQ(0, removed);
	EXPECT_FALSE(current->aborted);
}

TEST_F(HttpManagerTest, CompletionCallbackCanDisconnectWithoutASecondTerminalEvent) {
	auto connection = std::make_shared<TestTransport>();
	int callbacks = 0;
	HttpManager::getInstance()->download(connection, "https://example.test/Country.mmdb", nullptr,
		[&](const HttpConnection* c, OutputStream*, Flags::MaskType) {
			if(++callbacks == 1)
				HttpManager::getInstance()->disconnect(c->getUrl());
		});
	connection->finish();
	EXPECT_EQ(1, callbacks);
	EXPECT_EQ(1, completed);
	EXPECT_EQ(0, failed);
	EXPECT_FALSE(connection->aborted);
}

TEST_F(HttpManagerTest, LateTransportEventsCannotChangeCompletedOutput) {
	auto connection = std::make_shared<TestTransport>();
	StringOutputStream output;
	HttpManager::getInstance()->download(connection, "https://example.test/Country.mmdb", &output);
	connection->finish();
	connection->finish();
	connection->abortEmitsFailure = true;
	connection->abort();
	EXPECT_EQ("data", output.stringRef());
	EXPECT_EQ(1, completed);
	EXPECT_EQ(0, failed);
}
