/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stdinc.h"

#include <dcpp/transfer/private/PrivateTransferProtocol.h>

#include <dcpp/protocol/AdcCommand.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/core/timer/TimerManager.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <limits>

namespace dcpp {
namespace pft {
namespace {

const char* names[] = { "OFR", "ACC", "DEC", "CAN", "PAU", "GRA", "RES", "AUT", "RDY", "EMP", "RCP", "ACK" };
const size_t counts[] = { 12, 3, 2, 2, 2, 9, 5, 4, 3, 3, 3, 3 };

void require(bool value) {
	if(!value)
		throw ProtocolException("Invalid private transfer record");
}

void validate(const Record& r) {
	const auto op = static_cast<size_t>(r.op);
	require(op < std::size(counts) && r.fields.size() == counts[op]);
	const auto& f = r.fields;
	require(isHex(f[0], 16) && isHex(f[1], 32));
	switch(r.op) {
		case Op::Offer:
			require(isBase32(f[2], 24) && isBase32(f[3], 24));
			require(isHex(f[4], 32) && isHex(f[5], 32) && safeName(f[6]));
			number(f[7], MAX_FILE_SIZE);
			require(isHex(f[8], 32) && isBase32(f[9], 24));
			require(number(f[10], 86400) > 0 && f[11] == DRAFT);
			break;

		case Op::Grant:
			require(isHex(f[2], 16) && isHex(f[3], 16) && isHex(f[4], 32));
			require(number(f[5], MAX_FILE_SIZE) <= number(f[6], MAX_FILE_SIZE));
			require(number(f[7], 60) > 0 && number(f[8], 604800) > 0);
			break;

		case Op::Resume:
			require(isHex(f[2], 16) && isHex(f[4], 32));
			number(f[3], MAX_FILE_SIZE);
			break;

		case Op::Auth:
			require(isHex(f[2], 16) && isHex(f[3], 32));
			break;

		case Op::Accept:
		case Op::Ready:
		case Op::Empty:
		case Op::Receipt:
		case Op::Ack:
			require(isHex(f[2], 16));
			break;

		default:
			break;
	}
}
}

std::string hex(const void* data, size_t size) {
	static const char alphabet[] = "0123456789abcdef";
	const auto* p = static_cast<const unsigned char*>(data);
	std::string out(size * 2, '0');
	for(size_t i = 0; i < size; ++i) {
		out[2 * i] = alphabet[p[i] >> 4];
		out[2 * i + 1] = alphabet[p[i] & 15];
	}

	return out;
}

std::string sha256(const void* bytes, size_t size) {
	unsigned char out[EVP_MAX_MD_SIZE];
	unsigned length = 0;
	if(EVP_Digest(bytes, size, out, &length, EVP_sha256(), nullptr) != 1 || length != 32)
		throw ProtocolException("Private transfer hashing failed");

	return hex(out, length);
}

std::string randomHex(size_t bytes) {
	if(bytes != 16 && bytes != 32)
		throw ProtocolException("Invalid random identifier length");

	std::array<unsigned char, 32> data {};
	if(RAND_bytes(data.data(), static_cast<int>(bytes)) != 1)
		throw ProtocolException("Private transfer random generator failed");

	auto result = hex(data.data(), bytes);
	OPENSSL_cleanse(data.data(), data.size());

	return result;
}

bool isHex(const std::string& value, size_t bytes) noexcept {
	return value.size() == bytes * 2 &&
		std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool isBase32(const std::string& value, size_t bytes) noexcept {
	if(value.size() != (bytes * 8 + 4) / 5 ||
		!std::all_of(value.begin(), value.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'); }))
		return false;

	const auto remainder = bytes * 8 % 5;
	const char c = value.back();
	const unsigned last = c >= 'A' && c <= 'Z' ? c - 'A' : c - '2' + 26;

	return !remainder || (last & ((1u << (5 - remainder)) - 1)) == 0;
}

uint64_t number(const std::string& value, uint64_t maximum) {
	require(!value.empty() && value.size() <= 20 && (value.size() == 1 || value[0] != '0'));
	uint64_t result = 0;
	for(char c : value) {
		require(c >= '0' && c <= '9');
		const auto digit = static_cast<uint64_t>(c - '0');
		require(digit <= maximum && result <= (maximum - digit) / 10);
		result = result * 10 + digit;
	}

	return result;
}

bool safeName(const std::string& name) {
	if(name.empty() || name.size() > 240 || !Text::validateUtf8(name) || name == "." || name == ".." || name.back() == '.' ||
		name.back() == ' ')
		return false;
#ifdef _WIN32
	const auto wide = Text::toT(name);
	if(!IsNormalizedString(NormalizationC, wide.data(), static_cast<int>(wide.size())))
		return false;
#else
	// This Windows-only revision cannot assert Unicode normalization elsewhere.
	for(unsigned char c : name)
		if(c >= 128)
			return false;
#endif
	for(unsigned char c : name)
		if(c < 32 || c == 127 || std::string("<>:\"/\\|?*").find(c) != std::string::npos)
			return false;

	// C1 controls, Unicode line separators and directional formatting can hide
	// extensions or forge offer cards. Reject them rather than silently rename.
	for(size_t i = 0; i + 1 < name.size(); ++i) {
		if(static_cast<unsigned char>(name[i]) == 0xc2 && static_cast<unsigned char>(name[i + 1]) <= 0x9f)
			return false;

		if(static_cast<unsigned char>(name[i]) == 0xd8 && static_cast<unsigned char>(name[i + 1]) == 0x9c)
			return false; // Arabic letter mark

		if(i + 2 < name.size() && static_cast<unsigned char>(name[i]) == 0xe2 &&
			((static_cast<unsigned char>(name[i + 1]) == 0x80 &&
				 ((static_cast<unsigned char>(name[i + 2]) >= 0xa8 && static_cast<unsigned char>(name[i + 2]) <= 0xae) ||
					 (static_cast<unsigned char>(name[i + 2]) >= 0x8e && static_cast<unsigned char>(name[i + 2]) <= 0x8f))) ||
				(static_cast<unsigned char>(name[i + 1]) == 0x81 && static_cast<unsigned char>(name[i + 2]) >= 0xa6 &&
					static_cast<unsigned char>(name[i + 2]) <= 0xaf)))
			return false;
	}

	auto stem = name.substr(0, name.find('.'));
	while(!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
		stem.pop_back();
	std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return c >= 'a' && c <= 'z' ? c - 32 : c; });
	if(stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CLOCK$" || stem == "CONIN$" || stem == "CONOUT$")
		return false;

	if(stem.size() >= 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT")) {
		const auto suffix = stem.substr(3);
		if((suffix.size() == 1 && suffix[0] >= '0' && suffix[0] <= '9') || suffix == "\xc2\xb9" || suffix == "\xc2\xb2" ||
			suffix == "\xc2\xb3")
			return false;
	}

	return true;
}

bool equalSecret(const std::string& a, const std::string& b) noexcept {
	return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

void wipe(std::string& value) noexcept {
	if(!value.empty())
		OPENSSL_cleanse(&value[0], value.size());
	value.clear();
}

uint64_t now() noexcept {
#ifdef _WIN32
	return GetTickCount64();
#else
	return GET_TICK();
#endif
}

std::string Record::encode() const {
	validate(*this);
	std::string out = std::string("CPFT ") + DRAFT + ' ' + names[static_cast<size_t>(op)];
	for(const auto& field : fields)
		out += ' ' + AdcCommand::escape(field, false);
	require(out.size() <= MAX_LINE);

	return out + '\n';
}

Record Record::decode(const std::string& line) {
	require(line.size() <= MAX_LINE && line.compare(0, 5, "CPFT ") == 0 && line.find_first_of("\r\n\0", 0, 3) == std::string::npos &&
		Text::validateUtf8(line));
	AdcCommand command(line);
	const auto& fields = command.getParameters();
	require(fields.size() >= 2 && fields[0] == DRAFT);
	size_t op = 0;
	while(op < std::size(names) && fields[1] != names[op])
		++op;
	require(op < std::size(names));
	Record result { static_cast<Op>(op), { fields.begin() + 2, fields.end() } };
	validate(result);

	// Only the canonical ADC escaping/spacing is accepted (no hidden trailing
	// fields or alternate encodings when constructing a metadata commitment).
	auto canonical = result.encode();
	canonical.pop_back();
	require(canonical == line);

	return result;
}

std::string Metadata::digest() const {
	// Each UTF-8/canonical ASCII field has a uint32 big-endian byte length.
	std::string bytes = "PFT1-metadata-v1";
	for(const auto& field : { id, sender, recipient, senderPin, recipientPin, name, std::to_string(size), sha256, tth,
			std::to_string(offerSeconds), std::string(DRAFT) }) {
		const auto n = static_cast<uint32_t>(field.size());
		for(int shift : { 24, 16, 8, 0 })
			bytes.push_back(static_cast<char>(n >> shift));
		bytes += field;
	}

	return pft::sha256(bytes.data(), bytes.size());
}

Record Metadata::record() const {
	Record r { Op::Offer,
		{ id, digest(), sender, recipient, senderPin, recipientPin, name, std::to_string(size), sha256, tth, std::to_string(offerSeconds),
			DRAFT } };
	validate(r);

	return r;
}

Metadata Metadata::fromRecord(const Record& r) {
	require(r.op == Op::Offer);
	validate(r);
	const auto& f = r.fields;
	Metadata m { f[0], f[2], f[3], f[4], f[5], f[6], f[8], f[9], number(f[7], MAX_FILE_SIZE), number(f[10], 86400) };
	require(equalSecret(m.digest(), f[1]));

	return m;
}

bool Authorization::Binding::operator==(const Binding& o) const noexcept {
	return peerCID == o.peerCID && peerPin == o.peerPin && offer == o.offer && digest == o.digest && attempt == o.attempt;
}

Authorization::~Authorization() {
	revoke();
}

void Authorization::issue(
	const Binding& b, const std::string& secret, uint64_t offset, uint64_t length, uint64_t size, uint64_t expiry, uint64_t stop) {
	require(isHex(secret, 32) && isBase32(b.peerCID, 24) && isHex(b.peerPin, 32) && isHex(b.offer, 16) && isHex(b.digest, 32) &&
		isHex(b.attempt, 16));
	require(offset <= size && length == size - offset && size <= MAX_FILE_SIZE && expiry <= stop);
	const auto hash = sha256(secret.data(), secret.size());
	std::lock_guard<std::mutex> lock(mutex);

	wipe(secretHash);
	binding = b;
	secretHash = hash;
	lease = { lease.generation + 1, offset, length };
	expires = expiry;
	hardStop = stop;
	used = false;
	revoked = false;
}

Authorization::Lease Authorization::consume(const Binding& b, const std::string& secret, uint64_t tick) {
	require(isHex(secret, 32));
	const auto hash = sha256(secret.data(), secret.size());
	std::lock_guard<std::mutex> lock(mutex);

	require(!revoked && !used && tick < expires && tick < hardStop && binding == b && equalSecret(hash, secretHash));
	used = true;
	wipe(secretHash);

	return lease;
}

bool Authorization::valid(const Lease& l, uint64_t tick) const noexcept {
	std::lock_guard<std::mutex> lock(mutex);

	return !revoked && used && tick < hardStop && l.generation == lease.generation && l.offset == lease.offset && l.length == lease.length;
}

void Authorization::revoke() noexcept {
	std::lock_guard<std::mutex> lock(mutex);

	revoked = true;
	++lease.generation;
	wipe(secretHash);
}

void Authorization::read(const Lease& l, const std::function<void()>& operation) {
	std::lock_guard<std::mutex> lock(mutex);

	require(
		!revoked && used && now() < hardStop && l.generation == lease.generation && l.offset == lease.offset && l.length == lease.length);
	operation();
}
}
}
