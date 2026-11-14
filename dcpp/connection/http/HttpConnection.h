/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef DCPLUSPLUS_DCPP_HTTP_CONNECTION_H
#define DCPLUSPLUS_DCPP_HTTP_CONNECTION_H

#include <atomic>
#include <functional>
#include <thread>
#include <dcpp/connection/http/HttpConnectionListener.h>
#include <dcpp/core/types/GetSet.h>
#include <dcpp/core/Speaker.h>
#include <dcpp/util/Util.h>

namespace dcpp {
class HttpConnection : public Speaker<HttpConnectionListener> {
public:
	struct Url {
		string scheme, host, port, target;
		string authority() const;
		string origin() const;
	};
	struct Options {
		uint64_t maxBody = 256ULL * 1024 * 1024;
		uint64_t timeoutMs = 120000;
		uint64_t idleTimeoutMs = 10000;
		unsigned maxRedirects = 5;
		bool httpsOnly = false;
		bool sameOrigin = false;
		bool checkRevocation = false;
		string proxy;
		string bindAddress4, bindAddress6;
		// Credentials are scoped to an exact origin, never a proxy or other host.
		string authorization, authorizationOrigin;
		std::function<bool(const Url&)> allowedUrl;
		std::function<bool()> cancelled;
	};
	using DataCallback = std::function<void(const HttpConnection&, const uint8_t*, size_t)>;

	HttpConnection(const string& aUserAgent = Util::emptyString);
	virtual ~HttpConnection();
	HttpConnection(const HttpConnection&) = delete;
	HttpConnection& operator=(const HttpConnection&) = delete;
	virtual void download();
	void download(const StringMap& postData);
	virtual void abort();
	// Existing background jobs use the same transport/parser as HttpManager.
	// The sink may throw to stop the transfer. No asynchronous events are fired.
	void downloadBlocking(const string& source, const Options& options, const DataCallback& sink);
	static Url parseUrl(const string& value);
	static string resolveRedirect(const string& source, const string& location);

	const string& getMimeType() const { return mimeType; }
	virtual const string& getStatus() const { return statusLine; }
	int getStatusCode() const { return statusCode; }
	virtual int64_t getSize() const { return size; }
	virtual int64_t getDone() const { return done; }
	virtual double getSpeed() const { return speed; }
	GETSET(string, url, Url);

private:
	string userAgent, mimeType, statusLine;
	std::atomic<int> statusCode { 0 };
	std::atomic<int64_t> size { -1 }, done { 0 };
	std::atomic<double> speed { 0 };
	std::atomic<bool> stopped { false };
	std::thread worker;
	void start(string body, bool post);
	void perform(string source, const Options& options, const DataCallback& sink, string body, bool post, bool reportRedirects);
};
} // namespace dcpp
#endif
