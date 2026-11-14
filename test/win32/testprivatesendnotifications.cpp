/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "testbase.h"

#include <win32/frames/private_transfer/PrivateSendNotifications.h>

using namespace dcpp;

namespace {

DirectTransferManager::View offer(const string& id, const HintedUser& peer) {
	DirectTransferManager::View row {};
	row.id = id;
	row.contact = peer;
	row.incoming = true;
	row.state = DirectTransferManager::State::Offered;
	row.remainingSeconds = 60;
	return row;
}

}

TEST(PrivateSendNotificationsTest, retries_undelivered_notices_without_replaying_delivered_ones) {
	PrivateSendNotifications notifications;
	const HintedUser peer(UserPtr(new User(CID::generate())), "adcs://private.invalid");
	const auto row = offer("first", peer);
	notifications.update({ row });
	ASSERT_EQ(1u, notifications.notices().size());
	// A full PM window limit leaves the notice undelivered for the next tick.
	notifications.update({ row });
	ASSERT_EQ(1u, notifications.notices().size());
	notifications.delivered(row.id);
	// Closing and reopening a PM does not replace this application-owned state.
	notifications.update({ row });
	EXPECT_TRUE(notifications.notices().empty());
	EXPECT_TRUE(notifications.needsAttention());
}

TEST(PrivateSendNotificationsTest, acknowledgement_only_covers_current_offers_from_that_peer) {
	PrivateSendNotifications notifications;
	const HintedUser peer(UserPtr(new User(CID::generate())), "adcs://private.invalid");
	const HintedUser other(UserPtr(new User(CID::generate())), peer.hint);
	const auto first = offer("first", peer);
	const auto second = offer("second", other);
	notifications.update({ first, second });
	notifications.acknowledge(peer);
	EXPECT_TRUE(notifications.needsAttention());
	notifications.acknowledge(other);
	EXPECT_FALSE(notifications.needsAttention());
	EXPECT_EQ(2u, notifications.pending().size());
	const auto third = offer("third", peer);
	notifications.update({ first, second, third });
	EXPECT_TRUE(notifications.needsAttention());
}

TEST(PrivateSendNotificationsTest, resolved_expired_and_outgoing_transfers_do_not_flash) {
	PrivateSendNotifications notifications;
	const HintedUser peer(UserPtr(new User(CID::generate())), "adcs://private.invalid");
	auto row = offer("first", peer);
	for(const auto state : { DirectTransferManager::State::Connecting, DirectTransferManager::State::Receiving,
		DirectTransferManager::State::Completed, DirectTransferManager::State::Cancelled,
		DirectTransferManager::State::Declined, DirectTransferManager::State::Expired, DirectTransferManager::State::Failed })
	{
		row.state = state;
		notifications.update({ row });
		EXPECT_TRUE(notifications.pending().empty());
		EXPECT_FALSE(notifications.needsAttention());
	}
	row.state = DirectTransferManager::State::Offered;
	row.remainingSeconds = 0;
	notifications.update({ row });
	EXPECT_TRUE(notifications.pending().empty());
	row.remainingSeconds = 60;
	row.incoming = false;
	notifications.update({ row });
	EXPECT_TRUE(notifications.pending().empty());
	EXPECT_EQ(1u, notifications.notices().size());
	row.state = DirectTransferManager::State::Preparing;
	notifications.update({ row });
	EXPECT_TRUE(notifications.notices().empty());
}

TEST(PrivateSendNotificationsTest, removed_sessions_release_notification_state) {
	PrivateSendNotifications notifications;
	const HintedUser peer(UserPtr(new User(CID::generate())), "adcs://private.invalid");
	const auto row = offer("first", peer);
	notifications.update({ row });
	notifications.delivered(row.id);
	notifications.acknowledge(peer);
	notifications.update({});
	EXPECT_TRUE(notifications.pending().empty());
	EXPECT_TRUE(notifications.notices().empty());
	notifications.update({ row });
	EXPECT_EQ(1u, notifications.notices().size());
	EXPECT_TRUE(notifications.needsAttention());
}
