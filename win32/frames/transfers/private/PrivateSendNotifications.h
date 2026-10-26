/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DCPLUSPLUS_WIN32_PRIVATE_SEND_NOTIFICATIONS_H
#define DCPLUSPLUS_WIN32_PRIVATE_SEND_NOTIFICATIONS_H

#include <dcpp/transfer/private/DirectTransferManager.h>

#include <set>

/** UI-thread state shared by all PM windows. Closing a PM must not replay notices. */
class PrivateSendNotifications {
public:
	using View = dcpp::DirectTransferManager::View;
	using State = dcpp::DirectTransferManager::State;

	void update(std::vector<View> values) {
		rows = std::move(values);
		std::set<std::string> live;
		for(const auto& row : rows)
			live.insert(row.id);

		for(auto i = notified.begin(); i != notified.end();) {
			if(!live.count(*i))
				i = notified.erase(i);
			else
				++i;
		}
		for(auto i = reviewed.begin(); i != reviewed.end();) {
			if(!live.count(*i))
				i = reviewed.erase(i);
			else
				++i;
		}
	}

	std::vector<View> notices() const {
		std::vector<View> result;
		for(const auto& row : rows)
			if(row.state != State::Preparing && !notified.count(row.id))
				result.push_back(row);
		return result;
	}

	std::vector<View> pending() const {
		std::vector<View> result;
		for(const auto& row : rows)
			if(row.incoming && row.state == State::Offered && row.remainingSeconds)
				result.push_back(row);
		return result;
	}

	bool needsAttention() const {
		for(const auto& row : pending())
			if(!reviewed.count(row.id))
				return true;
		return false;
	}

	void delivered(const std::string& id) { notified.insert(id); }

	void acknowledge(const dcpp::HintedUser& peer) {
		for(const auto& row : pending())
			if(row.contact.user == peer.user)
				reviewed.insert(row.id);
	}

private:
	std::vector<View> rows;
	std::set<std::string> notified;
	std::set<std::string> reviewed;
};

#endif // !defined(DCPLUSPLUS_WIN32_PRIVATE_SEND_NOTIFICATIONS_H)
