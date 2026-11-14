/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DCPLUSPLUS_PRIVATE_TRANSFER_PROTOCOL_H
#define DCPLUSPLUS_PRIVATE_TRANSFER_PROTOCOL_H

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <dcpp/core/classes/Exception.h>

namespace dcpp {
namespace pft {

STANDARD_EXCEPTION(ProtocolException);

/** Experimental, recipient-bound Private File Transfer, revision 1.
 * This identifier is proposed, not registered with the ADC maintainers.
 * Do not change the wire grammar without changing DRAFT as well.
 */
inline constexpr char FEATURE[] = "PFT1";
inline constexpr char DRAFT[] = "1";
inline constexpr size_t MAX_LINE = 4096;
inline constexpr uint64_t MAX_FILE_SIZE = uint64_t(50) * 1024 * 1024 * 1024;

enum class Op {
	Offer,
	Accept,
	Decline,
	Cancel,
	Pause,
	Grant,
	Resume,
	Auth,
	Ready,
	Empty,
	Receipt,
	Ack
};

/** Fixed positional schemas deliberately have no optional or duplicate keys.
 * fields[0] is always the offer ID, fields[1] its immutable metadata digest.
 * This structure may contain a capability: never pass it to generic loggers.
 */
struct Record {
	Op op;
	std::vector<std::string> fields;

	std::string encode() const;
	static Record decode(const std::string& line);
};

struct Metadata {
	std::string id, sender, recipient, senderPin, recipientPin, name, sha256, tth;
	uint64_t size = 0;
	uint64_t offerSeconds = 900;

	std::string digest() const;
	Record record() const;
	static Metadata fromRecord(const Record& record);
};

std::string randomHex(size_t bytes);
std::string sha256(const void* bytes, size_t size);
std::string hex(const void* bytes, size_t size);
bool isHex(const std::string& value, size_t bytes) noexcept;
bool isBase32(const std::string& value, size_t bytes) noexcept;
bool safeName(const std::string& name);
uint64_t number(const std::string& value, uint64_t maximum);
bool equalSecret(const std::string& a, const std::string& b) noexcept;
void wipe(std::string& value) noexcept;

/** Includes Windows suspend time. Never use wall time for access decisions. */
uint64_t now() noexcept;

/** Thread-safe, one-use capability and revocable read lease.
 * The caller still owns protocol state, consent, file selection, and peer TLS
 * authentication. Exact identity and content bindings are rechecked here so
 * a public connection token or guessed hash can never become authorization.
 */
class Authorization {
public:
	struct Binding {
		std::string peerCID, peerPin, offer, digest, attempt;

		bool operator==(const Binding& other) const noexcept;
	};

	struct Lease {
		uint64_t generation = 0, offset = 0, length = 0;
	};

	Authorization() = default;
	~Authorization();

	/** Replaces old permission; expiry/hardStop are exclusive monotonic ticks. */
	void issue(const Binding& binding, const std::string& secret, uint64_t offset, uint64_t length, uint64_t size, uint64_t expires,
		uint64_t hardStop);
	Lease consume(const Binding& binding, const std::string& secret, uint64_t tick);
	bool valid(const Lease& lease, uint64_t tick) const noexcept;

	/** Serializes one bounded disk read with revoke(). The callback must not
	 * reenter this authorization object. Bytes already buffered cannot be recalled. */
	void read(const Lease& lease, const std::function<void()>& operation);
	void revoke() noexcept;

private:
	mutable std::mutex mutex;
	Binding binding;
	std::string secretHash;
	Lease lease;
	uint64_t expires = 0, hardStop = 0;
	bool used = false, revoked = true;
};

}
} // namespace dcpp::pft

#endif
