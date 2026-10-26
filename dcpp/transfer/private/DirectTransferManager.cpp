/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stdinc.h"

#include <dcpp/transfer/private/DirectTransferManager.h>

#include <dcpp/protocol/AdcCommand.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/net/CryptoManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/system/Streams.h>
#include <dcpp/connection/UserConnection.h>

#include <deque>
#include <sstream>

namespace dcpp {

namespace {

void need(bool condition) {
	if(!condition)
		throw pft::ProtocolException("Private Send is unavailable or its authorization has expired");
}

bool isTerminal(DirectTransferManager::State state) {
	return state >= DirectTransferManager::State::Completed;
}

std::map<string, string> loadPins() {
	std::map<string, string> result;
	std::istringstream stream(SETTING(PFT_CONTACT_PINS));
	string cid;
	string pin;

	while(stream >> cid >> pin) {
		if(result.size() >= 256 || !pft::isBase32(cid, 24) || !pft::isHex(pin, 32))
			break;

		result[cid] = pin;
	}

	return result;
}

void savePins(const std::map<string, string>& values) {
	string text;

	for(const auto& entry : values)
		text += entry.first + " " + entry.second + "\n";

	SettingsManager::getInstance()->set(SettingsManager::PFT_CONTACT_PINS, text);
}

string fingerprint(UserConnection* connection) {
	const auto keyprint = connection->getKeyprint();

	return pft::hex(keyprint.data(), keyprint.size());
}

}

struct DirectTransferManager::Session {
	enum Job {
		None,
		Prepare,
		PrefixLocal,
		PrefixRemote,
		Verify
	};

	struct Result {
		std::shared_ptr<pft::PrivateFile> file;
		pft::PrivateFile::Hashes hashes;
		bool success = false;
	};

	HintedUser peer;
	pft::Metadata metadata;
	string digest;
	string nonce;
	string attempt;
	string secret;
	string receipt;
	string pendingReceipt;
	string path;

	State state = State::Preparing;
	bool incoming = false;
	bool awaitingGrant = false;
	uint64_t deadline = 0;
	uint64_t hardStop = 0;
	uint64_t resumeBy = 0;
	uint64_t grantBy = 0;
	uint64_t bytes = 0;
	uint64_t lastProgress = 0;

	// Monotonic time and payload position, sampled by the manager's timer.
	std::deque<std::pair<uint64_t, uint64_t>> speedSamples;

	unsigned resumes = 0;
	unsigned receiptTries = 0;
	uint64_t receiptNext = 0;
	uint64_t generation = 0;
	uint64_t jobGeneration = 0;
	bool receiptAcknowledged = false;

	std::shared_ptr<pft::PrivateFile> file;
	std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
	pft::Authorization authorization;
	pft::Authorization::Lease lease;
	UserConnection* data = nullptr;

	Job jobKind = None;
	std::future<Result> job;
	string prefix;
	uint64_t resumeOffset = 0;

	~Session() {
		pft::wipe(secret);
	}
};

struct DirectTransferManager::ReadStream :
	InputStream
{
	ReadStream(std::shared_ptr<Session> session_) :
		session(session_),
		lease(session_->lease),
		file(session_->file),
		offset(lease.offset),
		remaining(lease.length)
	{
	}

	size_t read(void* buffer, size_t& length) override {
		length = static_cast<size_t>(std::min<uint64_t>({ length, remaining, 256 * 1024 }));
		if(!length)
			return 0;

		session->authorization.read(lease, [&] {
			need(!session->cancelled->load());
			file->read(offset, buffer, length);
		});
		offset += length;
		remaining -= length;

		return length;
	}

	std::shared_ptr<Session> session;
	pft::Authorization::Lease lease;
	std::shared_ptr<pft::PrivateFile> file;
	uint64_t offset;
	uint64_t remaining;
};

struct DirectTransferManager::Connection {
	UserConnection* userConnection = nullptr;
	HintedUser peer;
	string token;
	string pin;
	bool data = false;
	uint64_t created = pft::now();

	std::shared_ptr<Session> session;
	std::unique_ptr<ReadStream> stream;
};

DirectTransferManager::DirectTransferManager() {
	configure();
	ConnectionManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
}

DirectTransferManager::~DirectTransferManager() {
	shutdown();
	ConnectionManager::getInstance()->removeListener(this);
}

void DirectTransferManager::configure() {
	std::lock_guard<std::mutex> lock(mutex);

	enabled = SETTING(ENABLE_PRIVATE_SEND);
	requireVerification = SETTING(PFT_REQUIRE_VERIFICATION);
	approvedPins = loadPins();
	offerSeconds = std::clamp(SETTING(PFT_OFFER_MINUTES), 1, 1440) * 60ull;
	transferMilliseconds = std::clamp(SETTING(PFT_TRANSFER_MINUTES), 1, 10080) * 60000ull;

	for(auto& entry : sessions) {
		auto& session = *entry.second;

		if(!enabled || !contactAllowed(session.peer.user->getCID().toBase32(),
			session.incoming ? session.metadata.senderPin : session.metadata.recipientPin))
			terminate(session, State::Cancelled);
	}
}

void DirectTransferManager::shutdown() {
	TimerManager::getInstance()->removeListener(this);

	{
		std::lock_guard<std::mutex> lock(mutex);

		if(stopping)
			return;

		stopping = true;
		for(auto& entry : sessions)
			if(!isTerminal(entry.second->state))
				terminate(*entry.second, State::Cancelled);

		for(auto& entry : connections)
			entry.first->disconnect(true);
	}

	// ConnectionManager remains live until every socket has delivered Removed.
	// Call this before its shutdown, destroy this manager after socket draining.
}

const char* DirectTransferManager::stateName(State state) noexcept {
	static const char* names[] = {
		"Preparing file",
		"Waiting for acceptance",
		"Connecting",
		"Sending",
		"Receiving",
		"Verifying and checking policy",
		"Paused - resume before expiry",
		"Bytes sent; awaiting receipt",
		"Saved and verified",
		"Cancelled",
		"Declined",
		"Expired",
		"Failed securely"
	};

	return names[static_cast<unsigned>(state)];
}

std::vector<DirectTransferManager::View> DirectTransferManager::snapshot(const HintedUser& peer) {
	std::lock_guard<std::mutex> lock(mutex);

	std::vector<View> result;

	for(const auto& entry : sessions) {
		const auto& session = *entry.second;

		if(peer.user && session.peer.user != peer.user)
			continue;

		const auto stop = session.state == State::Paused ? session.resumeBy : session.hardStop ? session.hardStop : session.deadline;
		const auto tick = pft::now();
		const auto label = !session.incoming && session.state == State::Completed ?
			"Recipient confirmed saved" : stateName(session.state);

		double speed = 0;
		if((session.state == State::Sending || session.state == State::Receiving) && !session.speedSamples.empty()) {
			const auto& sample = session.speedSamples.front();
			speed = static_cast<double>(session.bytes - sample.second) * 1000 / std::max<uint64_t>(1, tick - sample.first);
		}

		const auto percent = session.metadata.size ? std::min(100.0, static_cast<double>(session.bytes) * 100 / session.metadata.size) :
			(session.state == State::Verifying || session.state == State::AwaitingReceipt ||
				session.state == State::Completed ? 100.0 : 0.0);

		result.push_back({
			entry.first,
			session.peer.user->getCID().toBase32(),
			session.metadata.name,
			label,
			session.path,
			session.state,
			session.incoming,
			session.bytes,
			session.metadata.size,
			stop > tick ? (stop - tick) / 1000 : 0,
			speed,
			percent,
			session.peer,
			session.incoming && session.state == State::Paused && !session.job.valid() &&
				!session.data && session.file && tick < session.resumeBy && session.resumes < 3,
			session.incoming && session.file ? session.file->targetPath() : session.path
		});
	}

	return result;
}

string DirectTransferManager::peerFingerprint(const HintedUser& peer) {
	std::lock_guard<std::mutex> lock(mutex);

	return control(peer, false).pin;
}

string DirectTransferManager::ownFingerprint() const {
	const auto& keyprint = CryptoManager::getInstance()->getKeyprint();

	return pft::hex(keyprint.data(), keyprint.size());
}

void DirectTransferManager::checkSend(const HintedUser& peer) {
	std::lock_guard<std::mutex> lock(mutex);

	control(peer);
}

void DirectTransferManager::approvePeer(const HintedUser& peer, const string& pin) {
	std::lock_guard<std::mutex> lock(mutex);

	auto& connection = control(peer, false);
	need(pft::isHex(pin, 32) && pft::equalSecret(connection.pin, pin));
	need(approvedPins.size() < 256 || approvedPins.count(peer.user->getCID().toBase32()));

	approvedPins[peer.user->getCID().toBase32()] = pin;
	savePins(approvedPins);
}

void DirectTransferManager::forgetPeer(const HintedUser& peer) {
	std::lock_guard<std::mutex> lock(mutex);

	approvedPins.erase(peer.user->getCID().toBase32());
	savePins(approvedPins);

	for(auto& entry : sessions)
		if(entry.second->peer.user == peer.user && !isTerminal(entry.second->state))
			terminate(*entry.second, State::Cancelled);
}

string DirectTransferManager::offer(const HintedUser& peer, const string& path) {
	std::lock_guard<std::mutex> lock(mutex);

	auto& connection = control(peer);
	limitJobs();

	size_t active = 0;
	size_t perPeer = 0;

	for(auto& entry : sessions)
		if(!isTerminal(entry.second->state)) {
			++active;
			if(entry.second->peer.user == peer.user)
				++perPeer;
		}

	need(active < 20 && perPeer < 3 && sessions.size() < 128);

	auto session = std::make_shared<Session>();
	session->peer = peer;
	session->metadata.name = Util::getFileName(path);
	session->metadata.id = pft::randomHex(16);
	session->metadata.sender = ClientManager::getInstance()->getMyCID().toBase32();
	session->metadata.recipient = peer.user->getCID().toBase32();
	session->metadata.senderPin = ownFingerprint();
	session->metadata.recipientPin = connection.pin;
	session->metadata.offerSeconds = offerSeconds;

	session->deadline = pft::now() + 86400000;
	session->jobKind = Session::Prepare;

	auto token = session->cancelled;
	session->job = std::async(std::launch::async, [path, token] {
		Session::Result result;

		try {
			result.file = pft::PrivateFile::openSource(path);
			result.hashes = result.file->hashes(result.file->size(), [token] { return token->load(); });
			result.success = true;
		} catch(...) {
		}

		return result;
	});

	sessions.emplace(session->metadata.id, session);

	return session->metadata.id;
}

void DirectTransferManager::accept(const string& id, const string& target) {
	std::lock_guard<std::mutex> lock(mutex);

	auto session = find(id);

	need(session->incoming && session->state == State::Offered && pft::now() < session->deadline);
	control(session->peer);
	limitTransfers(*session);

	session->file = pft::PrivateFile::createDestination(session->metadata, target);
	session->nonce = pft::randomHex(16);
	session->hardStop = pft::now() + transferMilliseconds;
	session->state = State::Connecting;
	session->lastProgress = pft::now();
	session->awaitingGrant = true;

	try {
		send(*session, { pft::Op::Accept, { id, session->digest, session->nonce } });
	} catch(...) {
		terminate(*session, State::Failed);
		throw;
	}
}

void DirectTransferManager::decline(const string& id) {
	std::lock_guard<std::mutex> lock(mutex);

	auto session = find(id);

	need(session->incoming && session->state == State::Offered);

	try {
		send(*session, { pft::Op::Decline, { id, session->digest } });
	} catch(...) {
	}

	terminate(*session, State::Declined);
}

void DirectTransferManager::cancel(const string& id) {
	std::lock_guard<std::mutex> lock(mutex);

	auto session = find(id);
	terminate(*session, State::Cancelled);

	try {
		if(!session->digest.empty())
			send(*session, { pft::Op::Cancel, { id, session->digest } });
	} catch(...) {
	}
}

void DirectTransferManager::pause(const string& id) {
	std::lock_guard<std::mutex> lock(mutex);

	auto session = find(id);

	need(session->state == State::Sending || session->state == State::Receiving || session->state == State::Connecting);
	suspend(*session);

	try {
		send(*session, { pft::Op::Pause, { id, session->digest } });
	} catch(...) {
	}
}

void DirectTransferManager::resume(const string& id) {
	std::lock_guard<std::mutex> lock(mutex);

	auto session = find(id);
	control(session->peer);
	limitJobs();
	need(session->incoming && session->state == State::Paused && !session->job.valid() && !session->data && session->file &&
		pft::now() < session->resumeBy &&
		++session->resumes <= 3);

	session->nonce = pft::randomHex(16);
	session->resumeOffset = session->bytes;
	session->jobKind = Session::PrefixLocal;
	session->jobGeneration = session->generation;

	auto file = session->file;
	auto token = session->cancelled;
	auto bytes = session->bytes;
	session->job = std::async(std::launch::async, [file, token, bytes] {
		Session::Result result;

		try {
			result.hashes = file->hashes(bytes, [token] { return token->load(); });
			result.success = true;
		} catch(...) {
		}

		return result;
	});
}

DirectTransferManager::Connection& DirectTransferManager::control(const HintedUser& peer, bool requirePin) {
	if(stopping || !enabled)
		throw pft::ProtocolException("Private Send is disabled. Enable it in Settings > Experimental > Private Send.");
	if(peer.hint.compare(0, 7, "adcs://") != 0)
		throw pft::ProtocolException("Private Send requires an ADCS hub. Open this contact's chat through an ADCS hub.");

	for(auto& entry : connections) {
		auto& connection = *entry.second;
		if(!connection.data && connection.peer.user == peer.user && hubHintsEqual(connection.peer.hint, peer.hint) &&
			connection.userConnection->getState() == UserConnection::STATE_CMD &&
			connection.userConnection->isPrivateTransferSecure() && connection.userConnection->isSet(UserConnection::FLAG_SUPPORTS_PFT1)) {
			if(requirePin && !contactAllowed(peer.user->getCID().toBase32(), connection.pin))
				throw pft::ProtocolException("Contact is not verified. Use Verify contact... to confirm their CID and SHA-256 through an independent channel.");

			return connection;
		}
	}

	throw pft::ProtocolException("Start a TLS 1.2+ direct chat using authenticated encryption with a Private Send compatible client first");
}

bool DirectTransferManager::contactAllowed(const string& cid, const string& pin) const {
	// Testing mode skips saved contact approval, never the per-transfer identity,
	// TLS certificate, capability, consent, or integrity checks.
	if(!requireVerification)
		return true;

	auto i = approvedPins.find(cid);

	return i != approvedPins.end() && pft::equalSecret(i->second, pin);
}

std::shared_ptr<DirectTransferManager::Session> DirectTransferManager::find(const string& id) {
	auto i = sessions.find(id);
	need(i != sessions.end());

	return i->second;
}

void DirectTransferManager::limitJobs() {
	size_t count = 0;
	for(auto& entry : sessions)
		if(entry.second->job.valid())
			++count;

	need(count < 4);
}

void DirectTransferManager::limitTransfers(const Session& candidate) {
	size_t active = 0;
	for(const auto& entry : sessions) {
		const auto& session = *entry.second;

		if(&session == &candidate || isTerminal(session.state) || session.state == State::Offered || session.state == State::Preparing)
			continue;

		need(session.peer.user != candidate.peer.user);
		++active;
	}

	need(active < 4);
}

void DirectTransferManager::send(Session& session, const pft::Record& record) {
	auto& connection = control(session.peer);
	need(connection.pin == (session.incoming ? session.metadata.senderPin : session.metadata.recipientPin));
	connection.userConnection->sendPrivate(record.encode());
}

void DirectTransferManager::terminate(Session& session, State state) {
	if(isTerminal(session.state))
		return;

	session.state = state;
	session.cancelled->store(true);
	session.authorization.revoke();
	pft::wipe(session.secret);
	if(session.data)
		session.data->disconnect(true);
	session.file.reset();
	session.deadline = pft::now() + 300000;
}

void DirectTransferManager::suspend(Session& session) {
	if(isTerminal(session.state) || session.state == State::Verifying)
		return;

	++session.generation;
	session.authorization.revoke();
	pft::wipe(session.secret);
	if(session.data)
		session.data->disconnect(true);
	if(!session.resumeBy)
		session.resumeBy = std::min(session.hardStop, pft::now() + 1800000);
	session.state = State::Paused;
}

void DirectTransferManager::issue(Session& session, uint64_t offset) {
	need(!session.incoming && session.file && pft::now() < session.hardStop && offset <= session.metadata.size && !session.data);

	session.attempt = pft::randomHex(16);
	auto secret = pft::randomHex(32);
	const auto tick = pft::now();
	session.grantBy = std::min(session.hardStop, tick + 60000);
	if(session.resumeBy)
		session.grantBy = std::min(session.grantBy, session.resumeBy);
	need(session.grantBy > tick);

	session.authorization.issue({ session.metadata.recipient, session.metadata.recipientPin, session.metadata.id, session.digest,
		session.attempt }, secret, offset,
		session.metadata.size - offset, session.metadata.size, session.grantBy, session.hardStop);

	session.state = State::Connecting;
	session.bytes = offset;
	session.lastProgress = pft::now();

	auto record = pft::Record { pft::Op::Grant,
		{ session.metadata.id, session.digest, session.nonce, session.attempt, secret, std::to_string(offset),
			std::to_string(session.metadata.size),
			std::to_string(std::max<uint64_t>(1, (session.grantBy - tick) / 1000)),
			std::to_string(std::max<uint64_t>(1, (session.hardStop - tick) / 1000)) } };
	try {
		send(session, record);
	} catch(...) {
		pft::wipe(secret);
		pft::wipe(record.fields[4]);
		session.authorization.revoke();
		throw;
	}

	pft::wipe(secret);
	pft::wipe(record.fields[4]);
}

void DirectTransferManager::process(Connection& connection, const pft::Record& record) {
	const auto& fields = record.fields;
	need(enabled && !stopping && connection.userConnection->isPrivateTransferSecure() &&
		contactAllowed(connection.peer.user->getCID().toBase32(), connection.pin));

	if(record.op == pft::Op::Offer) {
		need(!connection.data);

		const auto metadata = pft::Metadata::fromRecord(record);
		need(metadata.sender == connection.peer.user->getCID().toBase32() && metadata.senderPin == connection.pin &&
			metadata.recipient == ClientManager::getInstance()->getMyCID().toBase32() && metadata.recipientPin == ownFingerprint());
		auto existing = sessions.find(metadata.id);
		if(existing != sessions.end()) {
			need(existing->second->digest == fields[1] && existing->second->peer.user == connection.peer.user);
			return;
		}

		size_t active = 0;
		size_t perPeer = 0;

		for(auto& entry : sessions)
			if(!isTerminal(entry.second->state)) {
				++active;
				if(entry.second->peer.user == connection.peer.user)
					++perPeer;
			}

		need(active < 20 && perPeer < 3 && sessions.size() < 128);

		auto session = std::make_shared<Session>();
		session->peer = connection.peer;
		session->metadata = metadata;
		session->digest = fields[1];
		session->incoming = true;
		session->state = State::Offered;
		session->deadline = pft::now() + metadata.offerSeconds * 1000;
		sessions.emplace(metadata.id, session);
		return;
	}

	auto session = find(fields[0]);

	need(session->digest == fields[1] && connection.peer.user == session->peer.user && hubHintsEqual(connection.peer.hint,
		session->peer.hint) &&
		connection.pin == (session->incoming ? session->metadata.senderPin : session->metadata.recipientPin) &&
		ownFingerprint() == (session->incoming ? session->metadata.recipientPin : session->metadata.senderPin));

	if(isTerminal(session->state)) {
		if(!connection.data && !session->incoming && session->state == State::Completed && record.op == pft::Op::Receipt &&
			fields[2] == session->receipt)
			send(*session, { pft::Op::Ack, { fields[0], fields[1], fields[2] } });
		if(!connection.data && session->incoming && session->state == State::Completed && record.op == pft::Op::Ack &&
			fields[2] == session->receipt)
			session->receiptAcknowledged = true;
		return;
	}

	if(record.op == pft::Op::Auth) {
		need(connection.data && !connection.session && !session->incoming && session->state == State::Connecting && !session->data &&
			connection.userConnection->getProtocolToken() == session->attempt);
		control(session->peer);
		session->lease = session->authorization.consume({ session->metadata.recipient, connection.pin, fields[0], fields[1],
			fields[2] }, fields[3], pft::now());
		connection.session = session;
		session->data = connection.userConnection;
		connection.userConnection->sendPrivate(pft::Record { pft::Op::Ready, { fields[0], fields[1], session->attempt } }.encode());
		if(!session->lease.length) {
			session->state = State::AwaitingReceipt;
			session->authorization.revoke();
		}

		return;
	}

	if(record.op == pft::Op::Ready) {
		need(connection.data && connection.session == session && session->incoming && session->state == State::Connecting &&
			fields[2] == session->attempt && pft::now() < session->grantBy);
		if(session->bytes == session->metadata.size) {
			startVerify(*session);
			return;
		}

		const auto command = AdcCommand(AdcCommand::CMD_GET)
			.addParam("file")
			.addParam("private/" + fields[0])
			.addParam(std::to_string(session->bytes))
			.addParam(std::to_string(session->metadata.size - session->bytes));
		connection.userConnection->sendPrivate(command.toString(0));
		return;
	}

	need(!connection.data);

	switch(record.op) {
	case pft::Op::Accept:
		need(!session->incoming);
		if(session->state != State::Offered) {
			need(session->nonce == fields[2]);
			return;
		}

		need(pft::now() < session->deadline);
		limitTransfers(*session);
		session->nonce = fields[2];
		session->hardStop = pft::now() + transferMilliseconds;
		issue(*session, 0);
		break;

	case pft::Op::Grant:
		if(session->incoming && !session->awaitingGrant && fields[2] == session->nonce && fields[3] == session->attempt)
			return;

		need(session->incoming && session->awaitingGrant && session->state == State::Connecting && !session->data &&
			fields[2] == session->nonce &&
			pft::number(fields[5], pft::MAX_FILE_SIZE) == session->bytes && pft::number(fields[6],
				pft::MAX_FILE_SIZE) == session->metadata.size);
		session->awaitingGrant = false;
		session->attempt = fields[3];
		session->secret = fields[4];
		session->grantBy = pft::now() + pft::number(fields[7], 60) * 1000;
		session->hardStop = std::min(session->hardStop, pft::now() + pft::number(fields[8], 604800) * 1000);
		session->state = State::Connecting;
		session->lastProgress = pft::now();
		ClientManager::getInstance()->connect(session->peer, session->attempt, CONNECTION_TYPE_PRIVATE);
		break;

	case pft::Op::Decline:
		need(!session->incoming && session->state == State::Offered);
		terminate(*session, State::Declined);
		break;

	case pft::Op::Cancel:
		terminate(*session, State::Cancelled);
		break;

	case pft::Op::Pause:
		need(session->hardStop);
		suspend(*session);
		break;

	case pft::Op::Resume: {
		need(!session->incoming && (session->state == State::Paused || session->state == State::AwaitingReceipt ||
			session->state == State::Connecting) &&
			!session->job.valid() && !session->data && pft::now() < session->hardStop && (!session->resumeBy ||
				pft::now() < session->resumeBy) && ++session->resumes <= 3);
		limitJobs();
		session->authorization.revoke();
		session->nonce = fields[2];
		session->resumeOffset = pft::number(fields[3], session->metadata.size);
		session->prefix = fields[4];
		auto file = session->file;
		auto token = session->cancelled;
		auto bytes = session->resumeOffset;
		session->jobKind = Session::PrefixRemote;
		session->jobGeneration = session->generation;
		session->job = std::async(std::launch::async, [file, token, bytes] {
			Session::Result result;

			try {
				result.hashes = file->hashes(bytes, [token] { return token->load(); });
				result.success = true;
			} catch(...) {
			}

			return result;
		});
		break;
	}

	case pft::Op::Receipt:
		// The independent control socket can deliver a receipt before the data
		// socket dispatches its final TransmitDone callback. Defer confirmation
		// until that callback instead of treating valid cross-socket ordering as
		// a protocol failure or reporting an unfinished local send as complete.
		if(!session->incoming && session->state == State::Sending) {
			need(session->pendingReceipt.empty() || session->pendingReceipt == fields[2]);
			session->pendingReceipt = fields[2];
			break;
		}

		need(!session->incoming && (session->state == State::AwaitingReceipt || (session->state == State::Paused &&
			session->bytes == session->metadata.size)));
		session->receipt = fields[2];
		terminate(*session, State::Completed);

		try {
			send(*session, { pft::Op::Ack, { fields[0], fields[1], fields[2] } });
		} catch(...) {
		}

		break;

	case pft::Op::Ack:
		need(session->incoming && fields[2] == session->receipt);
		break;

	default:
		need(false);
	}
}

void DirectTransferManager::processDataCommand(Connection& connection, const string& line) {
	need(connection.data && connection.session);
	auto session = connection.session;

	need(!isTerminal(session->state) && session->data == connection.userConnection && pft::now() < session->hardStop);
	control(session->peer);

	AdcCommand command(line);
	const auto& fields = command.getParameters();
	need(command.getType() == AdcCommand::TYPE_CLIENT && fields.size() == 4 && fields[0] == "file" &&
		fields[1] == "private/" + session->metadata.id);

	const auto offset = pft::number(fields[2], session->metadata.size);
	const auto length = pft::number(fields[3], session->metadata.size);
	need(length > 0 && offset == session->bytes && length == session->metadata.size - offset);

	if(command.getCommand() == AdcCommand::CMD_GET) {
		need(!session->incoming && session->state == State::Connecting && !connection.stream &&
			session->authorization.valid(session->lease, pft::now()) &&
			offset == session->lease.offset && length == session->lease.length);
		session->state = State::Sending;
		session->lastProgress = pft::now();
		session->speedSamples.assign(1, { session->lastProgress, session->bytes });
		session->resumeBy = 0;
		connection.stream = std::make_unique<ReadStream>(session);
		connection.userConnection->sendPrivate(AdcCommand(AdcCommand::CMD_SND)
			.addParam("file")
			.addParam(fields[1])
			.addParam(fields[2])
			.addParam(fields[3])
			.toString(0));
		connection.userConnection->transmitFile(connection.stream.get());
	} else {
		need(command.getCommand() == AdcCommand::CMD_SND && session->incoming && session->state == State::Connecting);
		session->state = State::Receiving;
		session->lastProgress = pft::now();
		session->speedSamples.assign(1, { session->lastProgress, session->bytes });
		session->resumeBy = 0;
		connection.userConnection->setDataMode(static_cast<int64_t>(length));
	}
}

void DirectTransferManager::startVerify(Session& session) {
	limitJobs();
	need(session.file && session.bytes == session.metadata.size && !session.job.valid());

	session.state = State::Verifying;
	session.jobKind = Session::Verify;

	auto file = session.file;
	auto token = session.cancelled;
	auto metadata = session.metadata;
	session.job = std::async(std::launch::async, [file, token, metadata] {
		Session::Result result;

		try {
			file->verifyAndScan(metadata, [token] { return token->load(); });
			result.success = true;
		} catch(...) {
		}

		return result;
	});
}

void DirectTransferManager::poll() {
	const auto tick = pft::now();

	for(auto& entry : connections)
		if(entry.second->data && !entry.second->session && tick - entry.second->created >= 10000)
			entry.first->disconnect(true);

	for(auto& entry : sessions) {
		auto& session = *entry.second;

		try {
			if(session.state == State::Sending || session.state == State::Receiving) {
				if(session.speedSamples.empty() || tick - session.speedSamples.back().first >= 1000)
					session.speedSamples.emplace_back(tick, session.bytes);
				while(session.speedSamples.size() > 1 && tick - session.speedSamples[1].first >= 5000)
					session.speedSamples.pop_front();
			} else {
				session.speedSamples.clear();
			}

			if(!isTerminal(session.state) &&
				(!enabled || !contactAllowed(session.peer.user->getCID().toBase32(),
					session.incoming ? session.metadata.senderPin : session.metadata.recipientPin) ||
					ownFingerprint() != (session.incoming ? session.metadata.recipientPin : session.metadata.senderPin)))
				terminate(session, State::Cancelled);

			if(!isTerminal(session.state) &&
				((session.hardStop && tick >= session.hardStop) || (!session.hardStop && tick >= session.deadline) ||
					(session.state == State::Paused && tick >= session.resumeBy)))
				terminate(session, State::Expired);

			if(!isTerminal(session.state) && (session.state == State::Connecting || session.state == State::Sending ||
				session.state == State::Receiving) &&
				tick - session.lastProgress >= (session.state == State::Connecting ? 30000 : 120000))
				suspend(session);

			if(session.job.valid() && session.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				auto result = session.job.get();
				const auto kind = session.jobKind;
				session.jobKind = Session::None;
				if(isTerminal(session.state))
					continue;

				if((kind == Session::PrefixLocal || kind == Session::PrefixRemote) && session.jobGeneration != session.generation)
					continue;

				need(result.success);

				if(kind == Session::Prepare) {
					session.file = result.file;
					session.metadata.name = result.file->name();
					session.metadata.size = result.file->size();
					session.metadata.sha256 = result.hashes.sha256;
					session.metadata.tth = result.hashes.tth;
					session.digest = session.metadata.digest();
					session.deadline = tick + session.metadata.offerSeconds * 1000;
					session.state = State::Offered;
					send(session, session.metadata.record());
				} else if(kind == Session::PrefixLocal) {
					session.awaitingGrant = true;
					send(session, { pft::Op::Resume,
						{ session.metadata.id, session.digest, session.nonce, std::to_string(session.resumeOffset),
							result.hashes.sha256 } });
					session.state = State::Connecting;
					session.lastProgress = tick;
				} else if(kind == Session::PrefixRemote) {
					need(session.prefix == result.hashes.sha256);
					issue(session, session.resumeOffset);
				} else if(kind == Session::Verify) {
					control(session.peer);
					need(tick < session.hardStop);
					session.path = session.file->publish();
					session.receipt = pft::randomHex(16);
					terminate(session, State::Completed);
				}
			}

			if(session.incoming && session.state == State::Completed && !session.receiptAcknowledged && session.receiptTries < 12 &&
				tick >= session.receiptNext) {
				++session.receiptTries;
				session.receiptNext = tick + 10000;

				try {
					send(session, { pft::Op::Receipt, { session.metadata.id, session.digest, session.receipt } });
				} catch(...) {
				}
			}
		} catch(...) {
			terminate(session, State::Failed);
		}
	}

	for(auto i = sessions.begin(); i != sessions.end();) {
		if(isTerminal(i->second->state) && tick >= i->second->deadline && !i->second->job.valid() && !i->second->data)
			i = sessions.erase(i);
		else
			++i;
	}
}

void DirectTransferManager::on(ConnectionManagerListener::Connected, ConnectionQueueItem* queueItem, UserConnection* userConnection) noexcept {
	if(queueItem->getType() != CONNECTION_TYPE_PM && queueItem->getType() != CONNECTION_TYPE_PRIVATE)
		return;

	try {
		std::lock_guard<std::mutex> lock(mutex);

		if(stopping) {
			userConnection->disconnect(true);
			return;
		}

		auto connection = std::make_unique<Connection>();
		connection->userConnection = userConnection;
		connection->peer = queueItem->getUser();
		connection->token = queueItem->getToken();
		connection->pin = fingerprint(userConnection);
		connection->data = queueItem->getType() == CONNECTION_TYPE_PRIVATE;

		std::shared_ptr<Session> receiving;
		if(connection->data) {
			need(enabled && userConnection->isSet(UserConnection::FLAG_SUPPORTS_PFT1) && userConnection->isPrivateTransferSecure() &&
				contactAllowed(connection->peer.user->getCID().toBase32(), connection->pin));
			userConnection->configurePrivateTransferSocket();

			bool expected = false;
			for(auto& entry : sessions) {
				auto session = entry.second;

				if(session->state == State::Connecting && session->peer.user == connection->peer.user &&
					hubHintsEqual(session->peer.hint, connection->peer.hint) &&
					session->attempt == userConnection->getProtocolToken()) {
					need(!session->data &&
						connection->pin == (session->incoming ? session->metadata.senderPin : session->metadata.recipientPin) &&
						pft::now() < session->grantBy);
					control(session->peer);
					expected = true;
					if(session->incoming)
						receiving = session;
					break;
				}
			}

			need(expected);
		}

		auto registered = connection.get();
		connections.emplace(userConnection, std::move(connection));
		userConnection->addListener(this);

		if(receiving) {
			registered->session = receiving;
			receiving->data = userConnection;
			userConnection->sendPrivate(
				pft::Record { pft::Op::Auth, { receiving->metadata.id, receiving->digest, receiving->attempt, receiving->secret } }
					.encode());
			pft::wipe(receiving->secret);
		}
	} catch(...) {
		userConnection->disconnect(true);
	}
}

void DirectTransferManager::on(ConnectionManagerListener::Removed, ConnectionQueueItem* queueItem) noexcept {
	std::unique_ptr<Connection> removed;

	{
		std::lock_guard<std::mutex> lock(mutex);

		for(auto i = connections.begin(); i != connections.end(); ++i)
			if(i->second->token == queueItem->getToken() && i->second->peer.user == queueItem->getUser().user) {
				removed = std::move(i->second);
				connections.erase(i);
				break;
			}

		if(removed) {
			if(removed->session && removed->session->data == removed->userConnection) {
				auto& session = *removed->session;
				session.data = nullptr;
				if(session.state != State::AwaitingReceipt && session.state != State::Verifying)
					suspend(session);
			}

			if(!removed->data)
				for(auto& entry : sessions)
					if(entry.second->peer.user == removed->peer.user && !isTerminal(entry.second->state)) {
						if(entry.second->hardStop)
							suspend(*entry.second);
						else
							terminate(*entry.second, State::Cancelled);
					}
		}
	}

	// No manager lock while draining callbacks; Removed pins the UC until return.
	if(removed)
		removed->userConnection->removeListener(this);
}

void DirectTransferManager::on(UserConnectionListener::PrivateTransfer, UserConnection* userConnection, const string& line) noexcept {
	std::lock_guard<std::mutex> lock(mutex);

	auto i = connections.find(userConnection);
	if(i == connections.end()) {
		userConnection->disconnect(true);
		return;
	}

	try {
		if(line.compare(0, 5, "CPFT ") == 0) {
			auto record = pft::Record::decode(line);

			try {
				process(*i->second, record);
			} catch(...) {
				for(auto& field : record.fields)
					pft::wipe(field);
				throw;
			}

			for(auto& field : record.fields)
				pft::wipe(field);
		} else
			processDataCommand(*i->second, line);
	} catch(...) {
		if(i->second->session)
			terminate(*i->second->session, State::Failed);
		userConnection->disconnect(true);
	}
}

void DirectTransferManager::on(UserConnectionListener::Data, UserConnection* userConnection, const uint8_t* data, size_t length) noexcept {
	std::lock_guard<std::mutex> lock(mutex);

	auto i = connections.find(userConnection);
	if(i == connections.end())
		return;

	auto session = i->second->session;

	try {
		need(session && session->incoming && session->state == State::Receiving && pft::now() < session->hardStop &&
			length <= session->metadata.size - session->bytes);
		control(session->peer);

		while(length) {
			const auto chunk = std::min<size_t>(length, 256 * 1024);
			session->file->write(session->bytes, data, chunk);
			session->bytes += chunk;
			data += chunk;
			length -= chunk;
		}

		session->lastProgress = pft::now();
		if(session->bytes == session->metadata.size)
			startVerify(*session);
	} catch(...) {
		if(session)
			terminate(*session, State::Failed);
		userConnection->disconnect(true);
	}
}

void DirectTransferManager::on(UserConnectionListener::BytesSent, UserConnection* userConnection, size_t, size_t actual) noexcept {
	std::lock_guard<std::mutex> lock(mutex);

	auto i = connections.find(userConnection);
	if(i == connections.end() || !i->second->session)
		return;

	auto& session = *i->second->session;

	if(session.state == State::Sending && actual) {
		session.bytes += std::min<uint64_t>(actual, session.metadata.size - session.bytes);
		session.lastProgress = pft::now();
	}
}

void DirectTransferManager::on(UserConnectionListener::TransmitDone, UserConnection* userConnection) noexcept {
	std::lock_guard<std::mutex> lock(mutex);

	auto i = connections.find(userConnection);
	if(i == connections.end() || !i->second->session)
		return;

	auto& session = *i->second->session;

	if(session.state == State::Sending) {
		session.state = State::AwaitingReceipt;
		session.authorization.revoke();
		userConnection->disconnect();
		if(!session.pendingReceipt.empty()) {
			session.receipt = std::move(session.pendingReceipt);
			terminate(session, State::Completed);

			try {
				send(session, { pft::Op::Ack, { session.metadata.id, session.digest, session.receipt } });
			} catch(...) {
			}
		}
	}
}

void DirectTransferManager::on(TimerManagerListener::Second, uint64_t) noexcept {
	std::lock_guard<std::mutex> lock(mutex);

	try {
		poll();
	} catch(...) {
	}
}

} // namespace dcpp
