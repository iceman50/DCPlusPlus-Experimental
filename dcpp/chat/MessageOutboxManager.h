/*
 * Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef DCPLUSPLUS_MESSAGE_OUTBOX_MANAGER_H
#define DCPLUSPLUS_MESSAGE_OUTBOX_MANAGER_H
#include <dcpp/base/Singleton.h>
#include <dcpp/chat/MessageOutbox.h>
#include <dcpp/user/HintedUser.h>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <map>

namespace dcpp {
class MessageOutboxManager : public Singleton<MessageOutboxManager> {
public:
	static bool available() { return instance != nullptr; }
	void configure(); // Called on startup and when interface settings are applied.
	void start() noexcept;
	void shutdown();
	~MessageOutboxManager() { shutdown(); }
	MessageOutbox& getStore() { return store; }
	bool isEnabled() const { return enabled.load(); }
	void setEnabled(bool value); // UI thread; also saves the setting.
	string getError() const;
	int64_t enqueue(const HintedUser& user, const string& text, bool thirdPerson, bool direct);

private:
	friend class Singleton<MessageOutboxManager>;
	MessageOutboxManager() = default;
	MessageOutbox store;
	std::atomic_bool enabled { false };
	mutable std::mutex mutex;
	std::condition_variable wake;
	std::thread worker;
	bool stopping = false;
	string error;
	std::map<std::pair<string, string>, int64_t> nextConnect;
	bool connectedThisPass = false;
	void run() noexcept;
	void fail(const string& message) noexcept;
	HintedUser recipient(const MessageOutbox::Entry& e) const;
	bool ready(const MessageOutbox::Entry& e);
	bool send(const MessageOutbox::Entry& e);
};
}
#endif
