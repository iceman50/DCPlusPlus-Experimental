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
#include <dcpp/text/Text.h>
#include <dcpp/system/TimerManager.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <array>

using namespace dcpp;

namespace {

struct Internet {
	HINTERNET handle;

	explicit Internet(HINTERNET h) :
		handle(h)
	{
		if(!h)
			throw Exception("Could not open GeoLite2 HTTPS connection");
	}

	~Internet() { WinHttpCloseHandle(handle); }

	operator HINTERNET() const { return handle; }
};

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

void check(BOOL result) {
	if(!result)
		throw Exception("GeoLite2 HTTPS request failed (Windows error " + std::to_string(GetLastError()) + ")");
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
			start();
		} catch(const Exception& e) {
			error = e.getError();
			fire(HttpConnectionListener::Failed(), this, error);
			running = false;
		}
	}

	void abort() override {
		cancelled = true;
		join();
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
	if(value.empty() || value.size() > 8192 || value.find_first_of("\r\n\t ") != std::string::npos || value.find('\0') != std::string::npos)
		return false;

	const auto url = Text::toT(value);
	URL_COMPONENTS parts {};
	parts.dwStructSize = sizeof(parts);
	parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);

	return WinHttpCrackUrl(url.c_str(), 0, 0, &parts) && parts.nScheme == INTERNET_SCHEME_HTTPS && parts.dwHostNameLength &&
		!parts.dwUserNameLength && !parts.dwPasswordLength;
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
	if(!proxy.empty()) {
		std::string protocol, host, port, path, query, fragment;
		Util::decodeUrl(proxy, protocol, host, port, path, query, fragment);
		proxy = (host.find(':') == std::string::npos ? host : "[" + host + "]") + (port.empty() ? ":80" : ":" + port);
	}

	transfer = std::make_shared<GeoIPTransfer>(source, maxmind, std::move(account), std::move(key), std::move(proxy));
	pending = true;
	SettingsManager::getInstance()->set(SettingsManager::GEOIP_LAST_ATTEMPT, now);
	LogManager::getInstance()->message(_("Downloading GeoLite2 Country database..."), LogMessage::SEV_INFO, "GeoIP");

	// Queries may contain tokens on a custom host. Show only the public path.
	HttpManager::getInstance()->download(transfer, source.substr(0, source.find_first_of("?#")), transfer.get());
}

void GeoIPTransfer::receive() {
	const auto proxyName = Text::toT(proxy);
	Internet session(
		WinHttpOpen(L"DC++ GeoLite2 Updater", proxy.empty() ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY,
			proxy.empty() ? WINHTTP_NO_PROXY_NAME : proxyName.c_str(), WINHTTP_NO_PROXY_BYPASS, 0));
	check(WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000));
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
	check(WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)));
	std::wstring url = Text::toT(source);
	const auto started = GET_TICK();
	for(unsigned redirect = 0; redirect < 6 && !cancelled; ++redirect) {
		URL_COMPONENTS parts {};
		parts.dwStructSize = sizeof(parts);
		parts.dwUserNameLength = parts.dwPasswordLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
			static_cast<DWORD>(-1);
		check(WinHttpCrackUrl(url.c_str(), 0, 0, &parts));
		std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
		if(parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwUserNameLength || parts.dwPasswordLength ||
			(maxmind &&
				(parts.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
					(host != L"download.maxmind.com" &&
						host != L"mm-prod-geoip-databases.a2649acb697e2c09b632799562c076f2.r2.cloudflarestorage.com"))))
			throw Exception("Unexpected GeoLite2 download redirect");

		std::wstring resource(parts.lpszUrlPath, parts.dwUrlPathLength);
		if(parts.dwExtraInfoLength)
			resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
		Internet connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
		Internet request(WinHttpOpenRequest(
			connection, L"GET", resource.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
		DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
		check(WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)));

		// Credentials are headers, never URL parameters; never forward them to R2.
		if(maxmind && host == L"download.maxmind.com") {
			const auto credentials = account + ":" + key;
			const auto authorization = L"Authorization: Basic " +
				Text::toT(base64(reinterpret_cast<const BYTE*>(credentials.data()), static_cast<DWORD>(credentials.size())));
			check(WinHttpAddRequestHeaders(request, authorization.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD));
		}

		check(WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
		check(WinHttpReceiveResponse(request, nullptr));
		DWORD status = 0, length = sizeof(status);
		check(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
			&length, WINHTTP_NO_HEADER_INDEX));
		if(status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
			std::array<wchar_t, 8192> location {};
			length = static_cast<DWORD>(location.size() * sizeof(wchar_t));
			check(WinHttpQueryHeaders(
				request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location.data(), &length, WINHTTP_NO_HEADER_INDEX));

			// WinHTTP returns Location verbatim; handle same-origin relative paths too.
			std::wstring next = location.data();
			if(next.compare(0, 2, L"//") == 0)
				next = L"https:" + next;
			else if(!next.empty() && next[0] == L'/') {
				auto authorityEnd = url.find(L'/', 8);
				next = url.substr(0, authorityEnd) + next;
			}

			url = std::move(next);
			continue;
		}

		if(status == 401 && host == L"download.maxmind.com") {
			// Recognize only the documented response; never log arbitrary response
			// bodies, which a proxy could populate with request credentials.
			std::array<char, 1024> body {};
			DWORD count = 0;
			const bool missing = WinHttpReadData(request, body.data(), static_cast<DWORD>(body.size()), &count) &&
				std::string(body.data(), count).find("An account ID and license key are required") != std::string::npos;
			throw Exception(missing
					? _("GeoLite2 authentication failed (HTTP 401): MaxMind reports missing credentials. Check whether your HTTP proxy removes the Authorization header.")
					: _("GeoLite2 authentication failed (HTTP 401): check your numeric MaxMind Account Information ID and full active license key in Settings > Experimental > Interface. Save, then retry File > GeoIP database update."));
		}

		if(status != 200)
			throw Exception("GeoLite2 download failed at " + Text::fromT(host) + " (HTTP " + std::to_string(status) + ")");

		DWORD contentLength = 0;
		length = sizeof(contentLength);
		if(WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			   &contentLength, &length, WINHTTP_NO_HEADER_INDEX)) {
			if(contentLength > 32 * 1024 * 1024)
				throw Exception("GeoLite2 download is too large");

			totalSize = contentLength;
		}

		std::array<char, 65536> buffer;
		size_t total = 0;
		for(;;) {
			if(cancelled || GET_TICK() - started > 120000)
				throw Exception("GeoLite2 download cancelled or timed out");

			DWORD count = 0;
			check(WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &count));
			if(!count)
				break;

			total += count;
			if(total > 32 * 1024 * 1024)
				throw Exception("GeoLite2 download is too large");

			transferred = total;
			speed = static_cast<double>(total) * 1000 / std::max<uint64_t>(1, GET_TICK() - started);
			fire(HttpConnectionListener::Data(), this, reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(count));
			if(!writeError.empty())
				throw Exception(writeError);
		}

		if(totalSize >= 0 && total != static_cast<size_t>(totalSize))
			throw Exception("Incomplete GeoLite2 download");

		output->flush();
		return;
	}

	throw Exception("GeoLite2 download cancelled or too many redirects");
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
