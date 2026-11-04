/*
 * Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "stdinc.h"
#include <dcpp/chat/MessageOutbox.h>
#include <dcpp/text/Text.h>
#include <set>

namespace dcpp {

void MessageOutbox::configure(int entries, int messageBytes, int expiryDays) {
	std::lock_guard<std::mutex> l(mutex);
	entryLimit = std::clamp(entries, entryLimitMin, entryLimitMax);
	messageLimit = std::clamp(messageBytes, messageLimitMin, messageLimitMax);
	expiryInterval = int64_t(std::clamp(expiryDays, expiryDaysMin, expiryDaysMax)) * 24 * 60 * 60;
}

void MessageOutbox::open(const string& path) {
	std::lock_guard<std::mutex> l(mutex);
	db.open(path);
	sqlite3_busy_timeout(db.getHandle(), 1000);
	{
		auto version = db.prepare("PRAGMA user_version");
		version.step();
		if(version.columnInt(0) > 1) throw SQLiteException("This message outbox was created by a newer application version.");
	}
	// Unlike rebuildable caches, the pre-send marker must survive a power loss.
	db.execute("PRAGMA synchronous=FULL; PRAGMA secure_delete=ON;");
	db.execute("CREATE TABLE IF NOT EXISTS messages (id INTEGER PRIMARY KEY AUTOINCREMENT, "
		"cid TEXT NOT NULL, nick TEXT NOT NULL, hub TEXT NOT NULL, body TEXT NOT NULL, "
		"nmdc INTEGER NOT NULL, direct INTEGER NOT NULL, third_person INTEGER NOT NULL, "
		"created INTEGER NOT NULL, expires INTEGER NOT NULL, state INTEGER NOT NULL)");
	db.execute("PRAGMA user_version=1");
	// A crash may have happened after a send but before its completion was saved.
	db.execute("UPDATE messages SET state=3 WHERE state=1");
}

int64_t MessageOutbox::enqueue(Entry e, int64_t now) {
	std::lock_guard<std::mutex> l(mutex);
	if(e.cid.size() != 39 || e.cid.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567") != string::npos ||
		e.hub.empty() || e.hub.size() > 2048 || e.hub.find('\0') != string::npos ||
		e.nick.empty() || e.nick.size() > 1024 || e.nick.find('\0') != string::npos ||
		e.text.empty() || e.text.size() > messageLimit || e.text.find('\0') != string::npos ||
		!Text::validateUtf8(e.text) || (e.nmdc && e.direct))
	{
		throw SQLiteException("Invalid recipient, route or message. The configured UTF-8 message size limit is " +
			std::to_string(messageLimit) + " bytes.");
	}
	SQLiteTransaction tx(db);
	{
		auto count = db.prepare("SELECT count(*) FROM messages");
		count.step();
		if(count.columnInt64(0) >= static_cast<int64_t>(entryLimit))
			throw SQLiteException("The message outbox is full. Remove old entries before queuing more messages.");
	}
	auto q = db.prepare("INSERT INTO messages (cid,nick,hub,body,nmdc,direct,third_person,created,expires,state) "
		"VALUES (?,?,?,?,?,?,?,?,?,0)");
	q.bind(1, e.cid); q.bind(2, e.nick); q.bind(3, Text::toLower(e.hub)); q.bind(4, e.text);
	q.bind(5, int(e.nmdc)); q.bind(6, int(e.direct)); q.bind(7, int(e.thirdPerson));
	q.bind(8, now); q.bind(9, now + expiryInterval); q.stepDone();
	auto id = sqlite3_last_insert_rowid(db.getHandle());
	tx.commit();
	return id;
}

std::vector<MessageOutbox::Entry> MessageOutbox::listLocked() const {
	std::vector<Entry> result;
	auto q = db.prepare("SELECT id,cid,nick,hub,body,nmdc,direct,third_person,created,expires,state FROM messages ORDER BY id");
	while(q.step()) {
		Entry e;
		e.id = q.columnInt64(0); e.cid = q.columnText(1); e.nick = q.columnText(2);
		e.hub = q.columnText(3); e.text = q.columnText(4);
		e.nmdc = q.columnInt(5) != 0; e.direct = q.columnInt(6) != 0; e.thirdPerson = q.columnInt(7) != 0;
		e.created = q.columnInt64(8); e.expires = q.columnInt64(9); e.state = State(q.columnInt(10));
		result.push_back(std::move(e));
	}
	return result;
}

std::vector<MessageOutbox::Entry> MessageOutbox::list() const {
	std::lock_guard<std::mutex> l(mutex);
	return listLocked();
}

bool MessageOutbox::hasPending(const string& cid, const string& hub) const {
	std::lock_guard<std::mutex> l(mutex);
	auto q = db.prepare("SELECT 1 FROM messages WHERE cid=? AND hub=? AND state IN (0,1,3) LIMIT 1");
	q.bind(1, cid); q.bind(2, Text::toLower(hub));
	return q.step();
}

bool MessageOutbox::erase(int64_t id) {
	std::lock_guard<std::mutex> l(mutex);
	auto q = db.prepare("DELETE FROM messages WHERE id=? AND state<>1");
	q.bind(1, id); q.stepDone();
	return sqlite3_changes(db.getHandle()) != 0;
}

bool MessageOutbox::retry(int64_t id, int64_t now) {
	std::lock_guard<std::mutex> l(mutex);
	auto q = db.prepare("UPDATE messages SET state=0, expires=? WHERE id=? AND state IN (3,4)");
	q.bind(1, now + expiryInterval); q.bind(2, id); q.stepDone();
	return sqlite3_changes(db.getHandle()) != 0;
}

bool MessageOutbox::transition(int64_t id, State from, State to) {
	std::lock_guard<std::mutex> l(mutex);
	auto q = db.prepare("UPDATE messages SET state=? WHERE id=? AND state=?");
	q.bind(1, int(to)); q.bind(2, id); q.bind(3, int(from)); q.stepDone();
	return sqlite3_changes(db.getHandle()) != 0;
}

void MessageOutbox::process(int64_t now, const Ready& ready, const Send& send) {
	std::lock_guard<std::mutex> dispatch(dispatchMutex);
	std::vector<Entry> entries;
	{
		std::lock_guard<std::mutex> l(mutex);
		auto expire = db.prepare("UPDATE messages SET state=4 WHERE state=0 AND expires<=?");
		expire.bind(1, now); expire.stepDone();
		auto prune = db.prepare("DELETE FROM messages WHERE state IN (2,4) AND expires<?");
		prune.bind(1, now - lifetime); prune.stepDone();
		entries = listLocked();
	}
	std::set<std::pair<string, string>> blocked;
	for(const auto& e: entries) {
		if(e.state == SENT || e.state == EXPIRED) continue;
		if(!blocked.emplace(e.cid, e.hub).second || e.state != QUEUED) continue;
		if(!ready(e)) continue;
		// Cancel may have won while readiness was being checked.
		if(!transition(e.id, QUEUED, SENDING)) continue;
		bool accepted;
		try {
			accepted = send(e);
		} catch(...) {
			transition(e.id, SENDING, UNCERTAIN);
			return;
		}
		// If this write fails, leave SENDING durable and stop the dispatcher.
		transition(e.id, SENDING, accepted ? SENT : QUEUED);
		return;
	}
}
}
