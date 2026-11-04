/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "stdafx.h"
#include <dcpp/util/Util.h>

#include <win32/services/GeoIPUpdate.h>

#include <dcpp/user/CID.h>
#include <dcpp/net/HttpConnection.h>
#include <dcpp/net/HttpManager.h>
#include <dcpp/system/Thread.h>
#include <atomic>
#include <dcpp/system/File.h>
#include <dcpp/geo/GeoManager.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/system/TimerManager.h>
#include <wincrypt.h>

using namespace dcpp;

namespace {

std::string base64(const BYTE* data, DWORD size) {
	DWORD length = 0;
	if(!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &length))
		throw Exception("Could not encode GeoLite2 credentials");

	std::string out(length, '\0');
	if(!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &out[0], &length))
		throw Exception("Could not encode GeoLite2 credentials");

	out.resize(length);
	return out;
}

}

// HttpManager owns this transport and forwards its data and lifecycle events to
// TransferView. The original public URL remains the transfer identity throughout
// redirects, so signed storage URLs and credentials never become UI/log text.
class GeoIPTransfer final :
	public HttpConnection,
	private Thread,
	public OutputStream
{
public:
	GeoIPTransfer(std::string source_, bool maxmind_, std::string account_, std::string key_, std::string proxy_) :
		source(std::move(source_)),
		account(std::move(account_)),
		key(std::move(key_)),
		proxy(std::move(proxy_)),
		maxmind(maxmind_)
	{ }

	~GeoIPTransfer() override { abort(); }

	void download() override {
		running = true;

		try {
			Thread::start();
		} catch(const Exception& e) {
			error = e.getError();
			fire(HttpConnectionListener::Failed(), this, error);
			running = false;
		}
	}

	void abort() override {
		cancelled = true;
		if(GetCurrentThreadId() != threadId) join();
	}

	int64_t getSize() const override { return totalSize; }

	int64_t getDone() const override { return transferred; }

	double getSpeed() const override { return speed; }

	const std::string& getStatus() const override { return error; }

	size_t write(const void* data, size_t size) override {
		try {
			return output->write(data, size);
		} catch(const Exception& e) {
			writeError = e.getError();

			return 0;
		}
	}

	// The worker explicitly flushes before validation. This stream remains valid
	// for HttpManager's terminal event even after its staging file is closed.
	size_t flush() override { return 0; }

	std::atomic<bool> running { false }, cancelled { false };
	bool success = false;
	std::string error;

private:
	int run() override;
	void receive();
	std::string source, account, key, proxy, writeError;
	bool maxmind;
	std::unique_ptr<File> output;
	std::atomic<int64_t> totalSize { -1 }, transferred { 0 };
	std::atomic<double> speed { 0 };
};

std::string GeoIPUpdate::protectKey(const std::string& key) {
	if(key.empty())
		return {};

	DATA_BLOB in { static_cast<DWORD>(key.size()), reinterpret_cast<BYTE*>(const_cast<char*>(key.data())) }, out {};
	if(!CryptProtectData(&in, L"DC++ MaxMind license key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		throw Exception("Could not protect MaxMind license key");

	std::string stored;

	try {
		stored = base64(out.pbData, out.cbData);
	} catch(...) {
		LocalFree(out.pbData);
		throw;
	}

	LocalFree(out.pbData);
	return stored;
}

std::string GeoIPUpdate::unprotectKey(const std::string& stored) {
	if(stored.empty() || stored.size() > 16384)
		return {};

	DWORD length = 0;
	if(!CryptStringToBinaryA(stored.c_str(), static_cast<DWORD>(stored.size()), CRYPT_STRING_BASE64, nullptr, &length, nullptr, nullptr))
		return {};

	if(!length)
		return {};

	std::string encrypted(length, '\0');
	if(!CryptStringToBinaryA(stored.c_str(), static_cast<DWORD>(stored.size()), CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(&encrypted[0]),
		   &length, nullptr, nullptr))
		return {};

	DATA_BLOB in { length, reinterpret_cast<BYTE*>(&encrypted[0]) }, out {};
	if(!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return {};

	std::string key(reinterpret_cast<char*>(out.pbData), out.cbData);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);

	return key;
}

GeoIPUpdate::~GeoIPUpdate() {
	stop();
}

void GeoIPUpdate::stop() {
	if(transfer)
		transfer->abort();
	transfer.reset();
	pending = false;
}

bool GeoIPUpdate::validHostedUrl(const std::string& value) {
	try { return HttpConnection::parseUrl(value).scheme == "https"; }
	catch(...) { return false; }
}

bool GeoIPUpdate::poll() {
	if(!pending || transfer->running)
		return false;

	pending = false;
	if(transfer->success) {
		SettingsManager::getInstance()->set(SettingsManager::GEOIP_LAST_SUCCESS, static_cast<int64_t>(GET_TIME()));
		LogManager::getInstance()->message(_("GeoLite2 Country database updated successfully"), LogMessage::SEV_INFO, "GeoIP");
	} else if(!transfer->cancelled) {
		LogManager::getInstance()->message(transfer->error, LogMessage::SEV_WARNING, "GeoIP");
	}

	return transfer->success;
}

void GeoIPUpdate::startUpdate(bool force) {
	if(pending || !SETTING(GET_USER_COUNTRY))
		return;

	const auto now = static_cast<int64_t>(GET_TIME());
	if(!force &&
		((GeoManager::getInstance()->buildEpoch() && now - SETTING(GEOIP_LAST_SUCCESS) < 24 * 3600) ||
			now - SETTING(GEOIP_LAST_ATTEMPT) < 6 * 3600))
		return;

	const bool maxmind = SETTING(GEOIP_USE_MAXMIND);
	auto source = SETTING(GEOIP_DOWNLOAD_URL);
	Util::trim(source);
	std::string account, key;
	if(maxmind) {
		account = SETTING(MAXMIND_ACCOUNT_ID);
		Util::trim(account);
		key = unprotectKey(SETTING(MAXMIND_LICENSE_KEY));
		if(account.empty() || key.empty() || !std::all_of(account.begin(), account.end(), [](char c) { return c >= '0' && c <= '9'; })) {
			if(force)
				LogManager::getInstance()->message(
					_("Enter your MaxMind account ID and license key in Settings > Experimental > Interface."), LogMessage::SEV_WARNING,
					"GeoIP");
			return;
		}

		source = "https://download.maxmind.com/geoip/databases/GeoLite2-Country/download?suffix=tar.gz";
	} else if(!validHostedUrl(source)) {
		if(force)
			LogManager::getInstance()->message(
				_("Enter an HTTPS country database URL in Settings > Appearance."), LogMessage::SEV_WARNING, "GeoIP");
		return;
	}

	auto proxy = SETTING(HTTP_PROXY);
	transfer = std::make_shared<GeoIPTransfer>(source, maxmind, std::move(account), std::move(key), std::move(proxy));
	pending = true;
	SettingsManager::getInstance()->set(SettingsManager::GEOIP_LAST_ATTEMPT, now);
	LogManager::getInstance()->message(_("Downloading GeoLite2 Country database..."), LogMessage::SEV_INFO, "GeoIP");

	// Queries may contain tokens on a custom host. Show only the public path.
	HttpManager::getInstance()->download(transfer, source.substr(0, source.find_first_of("?#")), transfer.get());
}

void GeoIPTransfer::receive() {
	HttpConnection connection("DC++ GeoLite2 Updater");
	HttpConnection::Options options;
	options.maxBody = 32 * 1024 * 1024;
	options.httpsOnly = true;
	options.proxy = proxy;
	options.cancelled = [this] { return cancelled.load(); };
	if(maxmind) {
		options.allowedUrl = [](const HttpConnection::Url& url) {
			return url.port == "443" && (url.host == "download.maxmind.com" ||
				url.host == "mm-prod-geoip-databases.a2649acb697e2c09b632799562c076f2.r2.cloudflarestorage.com");
		};
		const auto credentials = account + ":" + key;
		options.authorization = "Basic " + base64(reinterpret_cast<const BYTE*>(credentials.data()), static_cast<DWORD>(credentials.size()));
		options.authorizationOrigin = "https://download.maxmind.com";
	}
	try {
		connection.downloadBlocking(source, options, [&](const HttpConnection& http, const uint8_t* data, size_t count) {
			totalSize = http.getSize();
			transferred = http.getDone();
			speed = http.getSpeed();
			fire(HttpConnectionListener::Data(), this, data, count);
			if(!writeError.empty()) throw Exception(writeError);
		});
	} catch(const Exception&) {
		if(maxmind && connection.getStatusCode() == 401)
			throw Exception(_("GeoLite2 authentication failed (HTTP 401): check your numeric MaxMind account ID, active license key and HTTP proxy settings."));
		throw;
	}
	output->flush();
}

int GeoIPTransfer::run() {
	const auto staging = GeoManager::getDbPath() + "." + CID::generate().toBase32();

	try {
		output = std::make_unique<File>(staging + ".download", File::WRITE, File::CREATE | File::TRUNCATE);
		receive();
		output.reset();
		if(cancelled)
			throw Exception("GeoLite2 download cancelled");

		const auto magic = File(staging + ".download", File::READ, File::OPEN).read(2);
		const bool archive =
			magic.size() == 2 && static_cast<unsigned char>(magic[0]) == 0x1f && static_cast<unsigned char>(magic[1]) == 0x8b;
		if(archive)
			GeoIP::extractCountry(staging + ".download", staging);
		if(cancelled)
			throw Exception("GeoLite2 download cancelled");

		GeoManager::getInstance()->install(archive ? staging : staging + ".download");
		success = true;
	} catch(const Exception& e) {
		error = e.getError();
	} catch(...) {
		error = "GeoLite2 Country update failed";
	}

	output.reset();
	File::deleteFile(staging + ".download");
	File::deleteFile(staging);
	std::fill(key.begin(), key.end(), '\0');
	key.clear();
	if(success)
		fire(HttpConnectionListener::Complete(), this);
	else
		fire(HttpConnectionListener::Failed(), this, error);
	running = false;

	return 0;
}
