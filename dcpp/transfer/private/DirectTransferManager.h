/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DCPLUSPLUS_DIRECT_TRANSFER_MANAGER_H
#define DCPLUSPLUS_DIRECT_TRANSFER_MANAGER_H

#include <dcpp/connection/ConnectionManagerListener.h>
#include <dcpp/user/HintedUser.h>
#include <dcpp/transfer/private/PrivateTransferProtocol.h>
#include <dcpp/transfer/private/PrivateTransferStorage.h>
#include <dcpp/core/Singleton.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/connection/UserConnectionListener.h>

#include <future>
#include <map>
#include <mutex>

namespace dcpp {

/** Owns a deliberately separate, non-persistent private queue. Nothing in this
 * manager is registered with ShareManager, QueueManager, FinishedManager or
 * public search/source matching. All UI access uses copies, never live pointers.
 */
class DirectTransferManager :
	public Singleton<DirectTransferManager>,
	private ConnectionManagerListener,
	private UserConnectionListener,
	private TimerManagerListener
{
public:
	enum class State {
		Preparing,
		Offered,
		Connecting,
		Sending,
		Receiving,
		Verifying,
		Paused,
		AwaitingReceipt,
		Completed,
		Cancelled,
		Declined,
		Expired,
		Failed
	};

	struct View {
		std::string id;
		std::string peer;
		std::string name;
		std::string status;
		std::string savedPath;

		State state;
		bool incoming;

		uint64_t bytes;
		uint64_t size;
		uint64_t remainingSeconds;
		double bytesPerSecond;
		double percent;

		HintedUser contact;
		bool canResume;
		std::string destinationPath;
	};

	static bool available() noexcept { return instance != nullptr; }

	/** Called after settings load/write; disabling synchronously revokes access. */
	void configure();
	void shutdown();

	static const char* stateName(State state) noexcept;

	/** An empty peer returns the entire private queue for application notifications. */
	std::vector<View> snapshot(const HintedUser& peer = HintedUser());
	/** Dismiss terminal entries for this contact without deleting files or
	 * interrupting receipt acknowledgements and background cleanup. */
	size_t clearFinished(const HintedUser& peer);

	/** Returns the actual TLS peer fingerprint for independent comparison. */
	std::string peerFingerprint(const HintedUser& peer);
	std::string ownFingerprint() const;

	/** Checks the current channel and contact approval before choosing a file. */
	void checkSend(const HintedUser& peer);
	void approvePeer(const HintedUser& peer, const std::string& independentlyVerifiedPin);
	/** Match the live peer certificate to the identity advertised by this ADCS hub. */
	void approvePeerFromHub(const HintedUser& peer);
	bool isPeerApproved(const HintedUser& peer);
	void forgetPeer(const HintedUser& peer);

	std::string offer(const HintedUser& peer, const std::string& localPath);
	void accept(const std::string& id, const std::string& target = {});
	void decline(const std::string& id);
	void cancel(const std::string& id);
	void pause(const std::string& id);
	void resume(const std::string& id);

private:
	friend class Singleton<DirectTransferManager>;

	DirectTransferManager();
	~DirectTransferManager();

	struct Session;
	struct Connection;
	struct ReadStream;

	std::mutex mutex;
	std::map<std::string, std::shared_ptr<Session>> sessions;
	std::map<UserConnection*, std::unique_ptr<Connection>> connections;

	bool stopping = false;
	bool enabled = false;
	bool requireVerification = false;
	uint64_t offerSeconds = 900;
	uint64_t transferMilliseconds = 86400000;
	std::map<std::string, std::string> approvedPins;

	Connection& control(const HintedUser& peer, bool requirePin = true);
	bool contactAllowed(const std::string& cid, const std::string& pin) const;
	std::shared_ptr<Session> find(const std::string& id);

	bool hasJobCapacity() const;
	void limitJobs();
	void limitTransfers(const Session& candidate);

	void send(Session& session, const pft::Record& record);
	void terminate(Session& session, State state);
	void suspend(Session& session);
	void issue(Session& session, uint64_t offset);

	void process(Connection& connection, const pft::Record& record);
	void processDataCommand(Connection& connection, const std::string& line);

	void startVerify(Session& session);
	void poll();

	// ConnectionManagerListener
	void on(ConnectionManagerListener::Connected, ConnectionQueueItem*, UserConnection*) noexcept override;
	void on(ConnectionManagerListener::Removed, ConnectionQueueItem*) noexcept override;

	// UserConnectionListener
	void on(UserConnectionListener::PrivateTransfer, UserConnection*, const std::string&) noexcept override;
	void on(UserConnectionListener::Data, UserConnection*, const uint8_t*, size_t) noexcept override;
	void on(UserConnectionListener::BytesSent, UserConnection*, size_t, size_t) noexcept override;
	void on(UserConnectionListener::TransmitDone, UserConnection*) noexcept override;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t) noexcept override;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DIRECT_TRANSFER_MANAGER_H)
