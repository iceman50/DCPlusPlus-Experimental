/*
 * Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "testbase.h"
#include <dcpp/chat/MessageOutbox.h>
#include <chrono>
#include <filesystem>
#include <future>

using namespace dcpp;
namespace {
MessageOutbox::Entry message(const string& text = "Hello", const string& hub = "adcs://original.example") {
	MessageOutbox::Entry e;
	e.cid = string(39, 'A'); e.nick = "Recipient"; e.hub = hub; e.text = text;
	return e;
}
bool available(const MessageOutbox::Entry&) { return true; }
bool accepted(const MessageOutbox::Entry&) { return true; }
}

TEST(MessageOutbox, WaitsForOriginalRouteAndPreservesOrder) {
	MessageOutbox box;
	box.open(":memory:");
	auto first = box.enqueue(message("first"), 100);
	box.enqueue(message("second"), 101);
	auto elsewhere = box.enqueue(message("other", "adcs://elsewhere.example"), 102);
	std::vector<int64_t> sent;
	auto send = [&](const auto& e) { sent.push_back(e.id); return true; };
	box.process(103, [](const auto& e) { return e.hub == "adcs://elsewhere.example"; }, send);
	ASSERT_EQ(sent, std::vector<int64_t>{elsewhere});
	box.process(104, available, send);
	ASSERT_EQ(sent.back(), first);
	box.process(105, available, send);
	ASSERT_EQ(sent.size(), 3u);
	EXPECT_EQ(box.list()[1].state, MessageOutbox::SENT);
	box.process(106, available, send);
	EXPECT_EQ(sent.size(), 3u);
}

TEST(MessageOutbox, DefiniteFailureRemainsQueuedButAmbiguousFailureBlocksSuccessors) {
	MessageOutbox box;
	box.open(":memory:");
	auto first = box.enqueue(message(), 100);
	box.enqueue(message("second"), 100);
	box.process(101, available, [](const auto&) { return false; });
	EXPECT_EQ(box.list()[0].state, MessageOutbox::QUEUED);
	box.process(102, available, [](const auto&) -> bool { throw std::runtime_error("interrupted"); });
	EXPECT_EQ(box.list()[0].state, MessageOutbox::UNCERTAIN);
	bool sent = false;
	box.process(103, available, [&](const auto&) { sent = true; return true; });
	EXPECT_FALSE(sent);
	EXPECT_TRUE(box.hasPending(message().cid, message().hub));
	EXPECT_TRUE(box.retry(first, 104));
	box.process(105, available, accepted);
	EXPECT_EQ(box.list()[0].state, MessageOutbox::SENT);
	EXPECT_FALSE(box.retry(first, 106));
}

TEST(MessageOutbox, CancellationWinsBeforeSendButNotDuringSend) {
	MessageOutbox box;
	box.open(":memory:");
	auto first = box.enqueue(message(), 100);
	box.process(101, [&](const auto&) { EXPECT_TRUE(box.erase(first)); return true; },
		[](const auto&) { ADD_FAILURE() << "Cancelled message was sent"; return true; });
	EXPECT_TRUE(box.list().empty());
	auto next = box.enqueue(message(), 102);
	box.process(103, available, [&](const auto&) {
		EXPECT_EQ(box.list()[0].state, MessageOutbox::SENDING);
		EXPECT_FALSE(box.erase(next));
		EXPECT_FALSE(box.retry(next, 104));
		return true;
	});
	EXPECT_EQ(box.list()[0].state, MessageOutbox::SENT);
	EXPECT_TRUE(box.erase(next));
}

TEST(MessageOutbox, ExpiresWithoutSendingAndRequiresExplicitRetry) {
	MessageOutbox box;
	box.open(":memory:");
	auto id = box.enqueue(message(), 100);
	box.process(100 + MessageOutbox::lifetime, available, [](const auto&) {
		ADD_FAILURE() << "Expired message was sent"; return true;
	});
	EXPECT_EQ(box.list()[0].state, MessageOutbox::EXPIRED);
	EXPECT_FALSE(box.hasPending(message().cid, message().hub));
	EXPECT_TRUE(box.retry(id, 100 + MessageOutbox::lifetime));
	box.process(101 + MessageOutbox::lifetime, available, accepted);
	EXPECT_EQ(box.list()[0].state, MessageOutbox::SENT);
}

TEST(MessageOutbox, ValidatesSizeIdentityAndRouteAndBoundsStorage) {
	MessageOutbox box;
	box.open(":memory:");
	EXPECT_THROW(box.enqueue(message(""), 100), SQLiteException);
	EXPECT_THROW(box.enqueue(message(string(MessageOutbox::maxMessageBytes + 1, 'x')), 100), SQLiteException);
	EXPECT_THROW(box.enqueue(message("hello", ""), 100), SQLiteException);
	EXPECT_THROW(box.enqueue(message(string("a\0b", 3)), 100), SQLiteException);
	auto e = message(); e.cid = "not-a-cid";
	EXPECT_THROW(box.enqueue(e, 100), SQLiteException);
	e = message(); e.nmdc = true; e.direct = true;
	EXPECT_THROW(box.enqueue(e, 100), SQLiteException);
	for(size_t i = 0; i < MessageOutbox::maxEntries; ++i) box.enqueue(message(), 100);
	EXPECT_THROW(box.enqueue(message(), 100), SQLiteException);
}

TEST(MessageOutbox, RestoresMessagesAndRecoversInterruptedSendWithoutResending) {
	const auto path = std::filesystem::temp_directory_path() /
		("dcpp-outbox-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
	{
		MessageOutbox box;
		box.open(path.string());
		auto e = message("saved text"); e.direct = true; e.thirdPerson = true;
		box.enqueue(e, 100);
		box.enqueue(message("later"), 101);
	}
	{
		SQLiteDB db(path.string());
		// Simulate a crash after the durable sending marker was committed.
		db.execute("UPDATE messages SET state=1 WHERE id=1");
	}
	{
		MessageOutbox restored;
		restored.open(path.string());
		auto entries = restored.list();
		ASSERT_EQ(entries.size(), 2u);
		EXPECT_EQ(entries[0].state, MessageOutbox::UNCERTAIN);
		EXPECT_EQ(entries[0].text, "saved text");
		EXPECT_TRUE(entries[0].direct);
		EXPECT_TRUE(entries[0].thirdPerson);
		restored.process(102, available, [](const auto&) { ADD_FAILURE() << "Ambiguous send retried automatically"; return true; });
		EXPECT_EQ(restored.list()[1].state, MessageOutbox::QUEUED);
		EXPECT_TRUE(restored.erase(entries[0].id));
		restored.process(103, available, accepted);
		EXPECT_EQ(restored.list()[0].state, MessageOutbox::SENT);
	}
	std::filesystem::remove(path);
}

TEST(MessageOutbox, ConcurrentDispatchDoesNotSendSameMessageTwice) {
	MessageOutbox box;
	box.open(":memory:");
	auto id = box.enqueue(message(), 100);
	std::promise<void> entered, release;
	auto resumed = release.get_future();
	int sends = 0;
	auto first = std::async(std::launch::async, [&] {
		box.process(101, available, [&](const auto&) {
			++sends; entered.set_value(); resumed.wait(); return true;
		});
	});
	entered.get_future().wait();
	EXPECT_FALSE(box.erase(id));
	auto second = std::async(std::launch::async, [&] {
		box.process(102, available, [&](const auto&) { ++sends; return true; });
	});
	release.set_value();
	first.get(); second.get();
	EXPECT_EQ(sends, 1);
}

TEST(MessageOutbox, ConfiguredLimitsApplyWithoutChangingSavedDeadlines) {
	MessageOutbox box;
	box.open(":memory:");
	box.enqueue(message("original"), 100);
	box.configure(2, 2048, 3);
	EXPECT_THROW(box.enqueue(message(string(2049, 'x')), 101), SQLiteException);
	auto id = box.enqueue(message(string(2048, 'x')), 101);
	EXPECT_THROW(box.enqueue(message(), 102), SQLiteException);
	auto entries = box.list();
	ASSERT_EQ(entries.size(), 2u);
	EXPECT_EQ(entries[0].expires, 100 + MessageOutbox::lifetime);
	EXPECT_EQ(entries[1].expires, 101 + 3 * 86400);
	box.configure(1, 1024, 2);
	EXPECT_EQ(box.list().size(), 2u); // Lower limits never discard existing messages.
	box.process(101 + 3 * 86400, [](const auto&) { return false; }, accepted);
	EXPECT_TRUE(box.retry(id, 101 + 3 * 86400));
	EXPECT_EQ(box.list()[1].expires, 101 + 5 * 86400);
	box.configure(10, 32 * 1024, 7);
	EXPECT_NO_THROW(box.enqueue(message(string(32 * 1024, 'x')), 102));
}

TEST(MessageOutbox, ConfiguredLimitsAreBoundedEvenOutsideTheSettingsUI) {
	MessageOutbox box;
	box.open(":memory:");
	box.configure(-1, -1, -1);
	EXPECT_THROW(box.enqueue(message(string(1025, 'x')), 100), SQLiteException);
	box.enqueue(message(string(1024, 'x')), 100);
	EXPECT_THROW(box.enqueue(message(), 100), SQLiteException);
	EXPECT_EQ(box.list()[0].expires, 100 + 86400);
	box.configure(INT_MAX, INT_MAX, INT_MAX);
	EXPECT_THROW(box.enqueue(message(string(MessageOutbox::messageLimitMax + 1, 'x')), 101), SQLiteException);
	box.enqueue(message(), 101);
	EXPECT_EQ(box.list()[1].expires, 101 + int64_t(MessageOutbox::expiryDaysMax) * 86400);
}

TEST(MessageOutbox, PersistenceFailurePreventsUntrackedSendingAndAutomaticDuplicates) {
	const auto path = std::filesystem::temp_directory_path() /
		("dcpp-outbox-lock-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
	{
		MessageOutbox box;
		box.open(path.string());
		box.enqueue(message(), 100);
		SQLiteDB blocker(path.string());
		int sends = 0;
		blocker.beginImmediate();
		EXPECT_THROW(box.process(101, available, [&](const auto&) { ++sends; return true; }), SQLiteException);
		EXPECT_EQ(sends, 0);
		blocker.rollback();
		EXPECT_EQ(box.list()[0].state, MessageOutbox::QUEUED);
		EXPECT_THROW(box.process(102, available, [&](const auto&) {
			++sends;
			// The send succeeded but another writer prevents saving completion.
			blocker.beginImmediate();
			return true;
		}), SQLiteException);
		blocker.rollback();
		EXPECT_EQ(sends, 1);
		EXPECT_EQ(box.list()[0].state, MessageOutbox::SENDING);
		box.process(103, available, [&](const auto&) { ++sends; return true; });
		EXPECT_EQ(sends, 1);
	}
	{
		MessageOutbox box;
		box.open(path.string());
		ASSERT_EQ(box.list().size(), 1u);
		EXPECT_EQ(box.list()[0].state, MessageOutbox::UNCERTAIN);
	}
	std::filesystem::remove(path);
}
