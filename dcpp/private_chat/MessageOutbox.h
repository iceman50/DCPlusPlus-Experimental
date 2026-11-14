/*
 * Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef DCPLUSPLUS_MESSAGE_OUTBOX_H
#define DCPLUSPLUS_MESSAGE_OUTBOX_H

#include <dcpp/core/io/db/SQLiteDB.h>
#include <functional>
#include <mutex>
#include <vector>

namespace dcpp {

// Durable state machine, independent of windows, presence callbacks and sockets.
class MessageOutbox {
public:
	enum State { QUEUED, SENDING, SENT, UNCERTAIN, EXPIRED };
	struct Entry {
		int64_t id = 0;
		string cid, nick, hub, text;
		bool nmdc = false, direct = false, thirdPerson = false;
		int64_t created = 0, expires = 0;
		State state = QUEUED;
	};
	static constexpr int64_t lifetime = 7 * 24 * 60 * 60;
	static constexpr size_t maxEntries = 500, maxMessageBytes = 16 * 1024;
	static constexpr int entryLimitMin = 1, entryLimitMax = 10000;
	static constexpr int messageLimitMin = 1024, messageLimitMax = 64 * 1024;
	static constexpr int expiryDaysMin = 1, expiryDaysMax = 365;
	// Applies to new entries and explicit retries; saved deadlines stay intact.
	void configure(int entries, int messageBytes, int expiryDays);
	using Ready = std::function<bool(const Entry&)>;
	// False is permitted only when nothing was handed to the transport.
	using Send = std::function<bool(const Entry&)>;

	void open(const string& path);
	int64_t enqueue(Entry entry, int64_t now);
	std::vector<Entry> list() const;
	bool erase(int64_t id); // Sending messages cannot be cancelled.
	bool retry(int64_t id, int64_t now); // Explicit acknowledgement of duplicate risk.
	bool hasPending(const string& cid, const string& hub) const;
	// One serialized dispatcher; never call external code holding the database mutex.
	// Sends at most one message per invocation, preserving order within a route.
	void process(int64_t now, const Ready& ready, const Send& send);

private:
	mutable std::mutex mutex;
	std::mutex dispatchMutex;
	mutable SQLiteDB db;
	size_t entryLimit = maxEntries, messageLimit = maxMessageBytes;
	int64_t expiryInterval = lifetime;
	std::vector<Entry> listLocked() const;
	bool transition(int64_t id, State from, State to);
};
}
#endif
