/*
 * Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "stdinc.h"
#include <dcpp/chat/MessageOutboxManager.h>
#include <dcpp/chat/PrivateChatManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/hub/AdcHub.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <chrono>
#include <set>

namespace dcpp {
void MessageOutboxManager::start() noexcept {
	try {
		store.open(Util::getPath(Util::PATH_USER_LOCAL) + "MessageOutbox.sqlite");
		enabled = SETTING(ENABLE_MESSAGE_OUTBOX);
		worker = std::thread([this] { run(); });
	} catch(const std::exception& e) { fail(e.what()); }
}

void MessageOutboxManager::shutdown() {
	{
		std::lock_guard<std::mutex> l(mutex);
		stopping = true;
	}
	wake.notify_all();
	if(worker.joinable()) worker.join();
}

void MessageOutboxManager::setEnabled(bool value) {
	enabled = value;
	SettingsManager::getInstance()->set(SettingsManager::ENABLE_MESSAGE_OUTBOX, value);
	SettingsManager::getInstance()->save();
	wake.notify_all();
}

string MessageOutboxManager::getError() const {
	std::lock_guard<std::mutex> l(mutex);
	return error;
}

void MessageOutboxManager::fail(const string& message) noexcept {
	{
		std::lock_guard<std::mutex> l(mutex);
		error = message;
	}
	LogManager::getInstance()->message("Message outbox stopped: " + message, LogMessage::SEV_ERROR, "Messages");
}

int64_t MessageOutboxManager::enqueue(const HintedUser& user, const string& text, bool thirdPerson, bool direct) {
	if(!isEnabled()) throw SQLiteException("Enable offline messages in View > Message outbox first.");
	auto problem = getError();
	if(!problem.empty()) throw SQLiteException(problem);
	MessageOutbox::Entry e;
	e.cid = user.user->getCID().toBase32();
	e.nick = ClientManager::getInstance()->getNick(user);
	e.hub = user.hint; e.text = text; e.nmdc = user.user->isNMDC();
	e.thirdPerson = thirdPerson; e.direct = direct;
	return store.enqueue(std::move(e), time(nullptr));
}

HintedUser MessageOutboxManager::recipient(const MessageOutbox::Entry& e) const {
	auto cm = ClientManager::getInstance();
	auto user = e.nmdc ? cm->getUser(e.nick, e.hub) : cm->getUser(CID(e.cid));
	// NMDC identity is nickname + original hub, never a nickname on another hub.
	if(user->getCID().toBase32() != e.cid) throw SQLiteException("Outbox recipient identity does not match its saved route.");
	return HintedUser(user, e.hub);
}

bool MessageOutboxManager::ready(const MessageOutbox::Entry& e) {
	if(!enabled.load()) return false;
	auto user = recipient(e);
	if(e.direct && PrivateChatManager::getInstance()->outboxDirectMessage(user, {}, false, true)) return true;
	{
		auto cm = ClientManager::getInstance();
		auto lock = cm->lock();
		auto ou = cm->findOnlineUserHint(user);
		if(!ou || !ou->getClient().isConnected()) return false;
		if(!e.direct) return true;
		if(!SETTING(ENABLE_CCPM) || e.nmdc || !user.user->isSet(User::TLS) ||
			!ou->getIdentity().supports(AdcHub::CCPM_FEATURE)) return false;
	}
	auto& next = nextConnect[{e.cid, e.hub}];
	const auto now = time(nullptr);
	if(now >= next && !connectedThisPass) {
		connectedThisPass = true;
		next = now + 60;
		ClientManager::getInstance()->connect(user, ConnectionManager::getInstance()->makeToken(), CONNECTION_TYPE_PM);
	}
	return false;
}

bool MessageOutboxManager::send(const MessageOutbox::Entry& e) {
	if(!enabled.load()) return false;
	auto user = recipient(e);
	if(e.direct) return PrivateChatManager::getInstance()->outboxDirectMessage(user, e.text, e.thirdPerson, false);
	return ClientManager::getInstance()->privateMessage(user, e.text, e.thirdPerson, e.nmdc);
}

void MessageOutboxManager::run() noexcept {
	try {
		for(;;) {
			{
				std::unique_lock<std::mutex> l(mutex);
				// A global two-second spacing also limits reconnect bursts.
				wake.wait_for(l, std::chrono::seconds(2), [this] { return stopping; });
				if(stopping) break;
			}
			vector<HintedUser> wanted;
			std::set<std::pair<string, string>> pendingRoutes;
			if(enabled.load()) {
				for(const auto& e: store.list()) {
					if(e.direct && e.state == MessageOutbox::QUEUED && pendingRoutes.emplace(e.cid, e.hub).second)
						wanted.push_back(recipient(e));
				}
			}
			PrivateChatManager::getInstance()->setOutboxRecipients(wanted);
			for(auto i = nextConnect.begin(); i != nextConnect.end();) {
				if(!pendingRoutes.count(i->first)) i = nextConnect.erase(i); else ++i;
			}
			connectedThisPass = false;
			store.process(time(nullptr), [this](const auto& e) { return ready(e); },
				[this](const auto& e) { return send(e); });
		}
	} catch(const std::exception& e) { fail(e.what()); }
	PrivateChatManager::getInstance()->setOutboxRecipients({});
}
}
