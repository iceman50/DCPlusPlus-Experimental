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
#include "stdinc.h"
#include <dcpp/net/HttpConnection.h>
#include <dcpp/net/Socket.h>
#include <dcpp/net/SSLSocket.h>
#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/version.h>
#include <array>
#include <chrono>
#include <limits>

namespace dcpp {
namespace {
using Clock = std::chrono::steady_clock;
string lower(string value) {
	for(auto& c : value) if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
	return value;
}
string trim(string value) {
	const auto first = value.find_first_not_of(" \t");
	if(first == string::npos) return {};
	return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}
uint64_t number(const string& value, unsigned base) {
	if(value.empty()) throw Exception("Invalid HTTP length");
	uint64_t result = 0;
	for(unsigned char c : value) {
		const unsigned digit = c >= '0' && c <= '9' ? c - '0' :
			c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 255;
		if(digit >= base || result > (static_cast<uint64_t>(INT64_MAX) - digit) / base)
			throw Exception("Invalid HTTP length");
		result = result * base + digit;
	}
	return result;
}
bool safeValue(const string& value) {
	return std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 32 && c != 127; });
}
bool token(const string& value) {
	return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			string("!#$%&'*+-.^_`|~").find(c) != string::npos;
	});
}

// Bounded reads and short waits keep cancellation responsive, including while
// connecting, tunnelling, negotiating TLS and waiting for response headers.
class Wire {
public:
	Wire(Socket& socket_, const std::function<void()>& check_, bool raw_ = false) :
		socket(socket_), check(check_), raw(raw_) { }
	void send(const string& data) {
		size_t offset = 0;
		while(offset < data.size()) {
			check();
			const auto count = static_cast<int>(std::min<size_t>(data.size() - offset, 65536));
			const int n = raw ? socket.Socket::write(data.data() + offset, count) : socket.write(data.data() + offset, count);
			if(n > 0) { offset += n; progress(); }
			else wait(false);
		}
	}
	size_t read(uint8_t* out, size_t count) {
		check();
		if(begin == end) {
			for(;;) {
				check();
				int n = raw ? socket.Socket::read(buffer.data(), 1) : socket.read(buffer.data(), static_cast<int>(buffer.size()));
				if(n == 0) return 0;
				if(n > 0) { begin = 0; end = n; progress(); break; }
				wait(true);
			}
		}
		count = std::min(count, end - begin);
		std::copy_n(buffer.data() + begin, count, out);
		begin += count;
		return count;
	}
	string line(size_t& budget) {
		string result;
		uint8_t c;
		for(;;) {
			if(!budget || result.size() >= 8192) throw Exception("HTTP headers or chunk metadata are too large");
			--budget;
			if(!read(&c, 1)) throw Exception("Truncated HTTP response");
			if(c == '\n') {
				if(result.empty() || result.back() != '\r') throw Exception("Invalid HTTP line ending");
				result.pop_back();
				return result;
			}
			result += static_cast<char>(c);
		}
	}
	std::function<void()> progress;
private:
	void wait(bool reading) {
		if(raw) socket.Socket::wait(100, reading, !reading);
		else socket.wait(100, reading, !reading);
	}
	Socket& socket;
	std::function<void()> check;
	bool raw;
	std::array<uint8_t, 65536> buffer;
	size_t begin = 0, end = 0;
};
struct Response {
	int status = 0;
	int64_t length = -1;
	bool chunked = false;
	string location, type;
};
Response headers(Wire& wire) {
	size_t budget = 65536;
	for(unsigned interim = 0; interim < 9; ++interim) {
		Response result;
		const auto line = wire.line(budget);
		if(line.size() < 12 || (line.compare(0, 9, "HTTP/1.1 ") && line.compare(0, 9, "HTTP/1.0 ")) ||
			(line.size() > 12 && line[12] != ' ') || !safeValue(line)) throw Exception("Invalid HTTP status line");
		result.status = static_cast<int>(number(line.substr(9, 3), 10));
		if(result.status < 100 || result.status > 599) throw Exception("Invalid HTTP status code");
		bool transferEncoding = false, location = false;
		for(;;) {
			auto field = wire.line(budget);
			if(field.empty()) break;
			const auto colon = field.find(':');
			if(colon == string::npos || !token(field.substr(0, colon))) throw Exception("Invalid HTTP header");
			const auto name = lower(field.substr(0, colon));
			const auto value = trim(field.substr(colon + 1));
			if(!safeValue(value)) throw Exception("Invalid HTTP header value");
			if(name == "content-length") {
				if(result.length != -1) throw Exception("Duplicate HTTP Content-Length");
				result.length = number(value, 10);
			} else if(name == "transfer-encoding") {
				if(transferEncoding || lower(value) != "chunked") throw Exception("Unsupported HTTP Transfer-Encoding");
				transferEncoding = result.chunked = true;
			} else if(name == "location") {
				if(location) throw Exception("Duplicate HTTP Location");
				location = true;
				result.location = value;
			} else if(name == "content-type") result.type = value;
			else if(name == "content-encoding" && lower(value) == "x-bzip2") result.type = "application/x-bzip2";
		}
		if(result.chunked && result.length != -1) throw Exception("Ambiguous HTTP response framing");
		if(result.status >= 200) return result;
		if(result.status == 101 || result.chunked || result.length != -1) throw Exception("Unsupported HTTP interim response");
	}
	throw Exception("Too many HTTP interim responses");
}
}

string HttpConnection::Url::authority() const {
	return (host.find(':') == string::npos ? host : "[" + host + "]") +
		((scheme == "https" && port == "443") || (scheme == "http" && port == "80") ? "" : ":" + port);
}
string HttpConnection::Url::origin() const { return scheme + "://" + authority(); }

HttpConnection::Url HttpConnection::parseUrl(const string& value) {
	if(value.empty() || value.size() > 8192 || !safeValue(value) || value.find_first_of(" \\") != string::npos)
		throw Exception("Invalid HTTP URL");
	const auto sep = value.find("://");
	if(sep == string::npos) throw Exception("HTTP URL requires a scheme");
	Url result;
	result.scheme = lower(value.substr(0, sep));
	if(result.scheme != "http" && result.scheme != "https") throw Exception("Unsupported HTTP URL scheme");
	const auto end = value.find_first_of("/?#", sep + 3);
	const auto authority = value.substr(sep + 3, end - (sep + 3));
	if(authority.empty() || authority.find('@') != string::npos) throw Exception("HTTP URL must not contain credentials");
	string port;
	if(authority.front() == '[') {
		const auto bracket = authority.find(']');
		if(bracket == string::npos) throw Exception("Invalid HTTP IPv6 address");
		result.host = authority.substr(1, bracket - 1);
		in6_addr address {};
		if(inet_pton(AF_INET6, result.host.c_str(), &address) != 1) throw Exception("Invalid HTTP IPv6 address");
		if(bracket + 1 < authority.size()) {
			if(authority[bracket + 1] != ':') throw Exception("Invalid HTTP authority");
			port = authority.substr(bracket + 2);
			if(port.empty()) throw Exception("Invalid HTTP port");
		}
	} else {
		const auto colon = authority.find(':');
		result.host = authority.substr(0, colon);
		if(result.host.empty() || !std::all_of(result.host.begin(), result.host.end(), [](unsigned char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
		})) throw Exception("Invalid HTTP host");
		if(colon != string::npos) {
			port = authority.substr(colon + 1);
			if(port.empty()) throw Exception("Invalid HTTP port");
		}
	}
	result.host = lower(result.host);
	if(port.empty()) result.port = result.scheme == "https" ? "443" : "80";
	else {
		const auto n = number(port, 10);
		if(n == 0 || n > 65535) throw Exception("Invalid HTTP port");
		result.port = std::to_string(n);
	}
	result.target = end == string::npos ? "/" : value.substr(end, value.find('#', end) - end);
	if(result.target.empty() || result.target.front() == '?') result.target.insert(0, "/");
	return result;
}

string HttpConnection::resolveRedirect(const string& source, const string& location) {
	if(location.empty()) throw Exception("Missing HTTP redirect location");
	auto base = parseUrl(source);
	string result;
	if(location.compare(0, 2, "//") == 0) result = base.scheme + ":" + location;
	else if(location.find("://") != string::npos) result = location;
	else if(location.front() == '/') result = base.origin() + location;
	else if(location.front() == '?') result = base.origin() + base.target.substr(0, base.target.find('?')) + location;
	else if(location.front() == '#') result = base.origin() + base.target + location;
	else {
		const auto path = base.target.substr(0, base.target.find('?'));
		// A colon in the first segment denotes an unsupported URI scheme.
		if(location.substr(0, location.find('/')).find(':') != string::npos) throw Exception("Invalid HTTP redirect scheme");
		result = base.origin() + path.substr(0, path.rfind('/') + 1) + location;
	}
	auto parsed = parseUrl(result);
	const auto query = parsed.target.find('?');
	const auto path = parsed.target.substr(0, query);
	string normalized;
	for(size_t pos = 1; pos <= path.size();) {
		auto end = path.find('/', pos);
		if(end == string::npos) end = path.size();
		const auto part = path.substr(pos, end - pos);
		if(part == "..") normalized.erase(normalized.rfind('/') == string::npos ? 0 : normalized.rfind('/'));
		else if(part != ".") normalized += '/' + part;
		if(end == path.size()) { if(part == "." || part == "..") normalized += '/'; break; }
		pos = end + 1;
	}
	return parsed.origin() + (normalized.empty() ? "/" : normalized) + (query == string::npos ? "" : parsed.target.substr(query));
}

HttpConnection::HttpConnection(const string& agent) : userAgent(agent.empty() ? fullVersionString : agent) { }
HttpConnection::~HttpConnection() { abort(); }
void HttpConnection::abort() {
	stopped = true;
	if(worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
}
void HttpConnection::download() { start({}, false); }
void HttpConnection::download(const StringMap& postData) {
	string body;
	for(const auto& item : postData) {
		if(!body.empty()) body += '&';
		body += Util::encodeURI(item.first) + '=' + Util::encodeURI(item.second);
	}
	start(std::move(body), true);
}
void HttpConnection::start(string body, bool post) {
	abort();
	stopped = false;
	Options options;
	options.proxy = SETTING(HTTP_PROXY);
	options.bindAddress4 = CONNSETTING(BIND_ADDRESS);
	options.bindAddress6 = CONNSETTING(BIND_ADDRESS6);
	const auto source = url;
	try {
		worker = std::thread([this, source, options, body, post] {
			try {
				perform(source, options, [this](const HttpConnection&, const uint8_t* data, size_t len) {
					fire(HttpConnectionListener::Data(), this, data, len);
				}, body, post, true);
				if(stopped) throw Exception("HTTP request cancelled");
				fire(HttpConnectionListener::Complete(), this);
			} catch(const Exception& e) {
				statusLine = e.getError();
				fire(HttpConnectionListener::Failed(), this, statusLine);
			} catch(...) {
				statusLine = "HTTP request failed";
				fire(HttpConnectionListener::Failed(), this, statusLine);
			}
		});
	} catch(...) {
		statusLine = "Could not start HTTP request";
		fire(HttpConnectionListener::Failed(), this, statusLine);
	}
}
void HttpConnection::downloadBlocking(const string& source, const Options& options, const DataCallback& sink) {
	abort();
	stopped = false;
	url = source;
	perform(source, options, sink, {}, false, false);
}

void HttpConnection::perform(string source, const Options& options, const DataCallback& sink, string body, bool post, bool reportRedirects) {
	const auto began = Clock::now();
	auto lastProgress = began;
	auto progress = [&] { lastProgress = Clock::now(); };
	auto check = [&] {
		if(stopped || (options.cancelled && options.cancelled())) throw Exception("HTTP request cancelled");
		const auto now = Clock::now();
		if(std::chrono::duration_cast<std::chrono::milliseconds>(now - began).count() >= static_cast<int64_t>(options.timeoutMs) ||
			std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgress).count() >= static_cast<int64_t>(options.idleTimeoutMs))
			throw Exception("HTTP request timed out");
	};
	const auto original = parseUrl(source);
	auto previous = original;
	if(!safeValue(userAgent) || !safeValue(options.authorization)) throw Exception("Invalid HTTP request header");
	for(unsigned redirects = 0;; ++redirects) {
		check();
		auto target = parseUrl(source);
		if((options.httpsOnly && target.scheme != "https") || (previous.scheme == "https" && target.scheme != "https") ||
			(options.sameOrigin && original.origin() != target.origin()) || (options.allowedUrl && !options.allowedUrl(target)))
			throw Exception("HTTP redirect or URL rejected by policy");
		previous = target;
		size = -1; done = 0; speed = 0; statusCode = 0; mimeType.clear();
		const bool secure = target.scheme == "https";
		std::unique_ptr<Socket> socket(secure ? new SSLSocket(CryptoManager::SSL_HTTP, target.host) : new Socket(Socket::TYPE_TCP));
		socket->setLocalIp4(options.bindAddress4);
		socket->setLocalIp6(options.bindAddress6);
		auto endpoint = target;
		if(!options.proxy.empty()) {
			endpoint = parseUrl(options.proxy.find("://") == string::npos ? "http://" + options.proxy : options.proxy);
			if(endpoint.scheme != "http" || endpoint.target != "/") throw Exception("HTTP proxy must be an HTTP host and port");
		}
		socket->Socket::connect(endpoint.host, endpoint.port);
		while(!socket->Socket::waitConnected(100)) check();
		check(); progress();
		if(secure && !options.proxy.empty()) {
			Wire tunnel(*socket, check, true);
			tunnel.progress = progress;
			const auto authority = (target.host.find(':') == string::npos ? target.host : "[" + target.host + "]") + ':' + target.port;
			tunnel.send("CONNECT " + authority + " HTTP/1.1\r\nHost: " + authority + "\r\n\r\n");
			if(headers(tunnel).status != 200) throw Exception("HTTP proxy CONNECT failed");
		}
		if(secure) {
			while(!socket->waitConnected(100)) check();
			if(options.checkRevocation) static_cast<SSLSocket*>(socket.get())->verifyHttpRevocation();
			check(); progress();
		}
		Wire wire(*socket, check);
		wire.progress = progress;
		string request = (post ? "POST " : "GET ") + ((!secure && !options.proxy.empty()) ? target.origin() + target.target : target.target) +
			" HTTP/1.1\r\nHost: " + target.authority() + "\r\nUser-Agent: " + userAgent +
			"\r\nAccept-Encoding: identity\r\nCache-Control: no-cache\r\nConnection: close\r\n";
		if(!options.authorization.empty() && target.origin() == options.authorizationOrigin && secure)
			request += "Authorization: " + options.authorization + "\r\n";
		if(post) request += "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
		wire.send(request + "\r\n" + (post ? body : ""));
		const auto response = headers(wire);
		statusCode = response.status;
		statusLine = "HTTP " + std::to_string(response.status);
		if(response.status == 301 || response.status == 302 || response.status == 303 || response.status == 307 || response.status == 308) {
			if(redirects >= options.maxRedirects) throw Exception("Too many HTTP redirects");
			auto next = resolveRedirect(source, response.location);
			const auto destination = parseUrl(next);
			if((secure && destination.scheme != "https") || (options.httpsOnly && destination.scheme != "https") ||
				(options.sameOrigin && destination.origin() != original.origin()) || (options.allowedUrl && !options.allowedUrl(destination)))
				throw Exception("HTTP redirect rejected by policy");
			// Never replay a POST body to another origin, even on a 307/308.
			if(post && destination.origin() != target.origin()) throw Exception("Cross-origin HTTP POST redirect rejected");
			if(response.status == 303 || response.status == 301 || response.status == 302) { post = false; body.clear(); }
			source = std::move(next);
			if(reportRedirects) {
				fire(HttpConnectionListener::Redirected(), this, source);
				url = source;
			}
			continue;
		}
		if(response.status != 200 && response.status != 204) throw Exception(statusLine);
		if(response.status == 204 && (response.chunked || response.length > 0)) throw Exception("Invalid HTTP 204 framing");
		size = response.status == 204 ? 0 : response.length;
		mimeType = response.type;
		const auto path = lower(target.target.substr(0, target.target.find('?')));
		if(path.size() >= 4 && path.compare(path.size() - 4, 4, ".bz2") == 0) mimeType = "application/x-bzip2";
		if(size >= 0 && static_cast<uint64_t>(size.load()) > options.maxBody) throw Exception("HTTP response is too large");
		std::array<uint8_t, 65536> buffer;
		auto deliver = [&](size_t n) {
			if(n > options.maxBody - static_cast<uint64_t>(done.load())) throw Exception("HTTP response is too large");
			done += n;
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began).count();
			speed = static_cast<double>(done.load()) * 1000 / std::max<int64_t>(1, elapsed);
			if(sink) sink(*this, buffer.data(), n);
			check();
		};
		auto exact = [&](uint64_t count) {
			if(count > options.maxBody - static_cast<uint64_t>(done.load())) throw Exception("HTTP response is too large");
			while(count) {
				const auto n = wire.read(buffer.data(), static_cast<size_t>(std::min<uint64_t>(count, buffer.size())));
				if(!n) throw Exception("Truncated HTTP response body");
				deliver(n); count -= n;
			}
		};
		if(response.chunked) {
			for(;;) {
				size_t budget = 8192;
				auto line = wire.line(budget);
				const auto count = number(line.substr(0, line.find(';')), 16);
				if(!count) {
					budget = 65536;
					while(!(line = wire.line(budget)).empty()) {
						const auto colon = line.find(':');
						const auto name = lower(line.substr(0, colon));
						if(colon == string::npos || !token(name) || !safeValue(trim(line.substr(colon + 1))) ||
							name == "content-length" || name == "transfer-encoding") throw Exception("Invalid HTTP trailer");
					}
					break;
				}
				exact(count);
				budget = 2;
				if(!wire.line(budget).empty()) throw Exception("Invalid HTTP chunk terminator");
			}
		} else if(size >= 0) exact(static_cast<uint64_t>(size.load()));
		else {
			for(;;) {
				const auto n = wire.read(buffer.data(), buffer.size());
				if(!n) break;
				deliver(n);
			}
		}
		check();
		return;
	}
}
} // namespace dcpp
