// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"
#include <dcpp/net/HttpConnection.h>
#include <dcpp/net/HttpManager.h>
#include <dcpp/net/Socket.h>
#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/system/Streams.h>
#include <dcpp/system/TimerManager.h>
#include <chrono>
#include <future>

using namespace dcpp;

TEST(HttpUrl, RejectsCredentialsInjectionInvalidHostsAndPorts) {
	for(const string url : { "", "http:", "file:///x", "https:///", "https://user:secret@host/", "https://host:/", "https://host:0/",
		"https://host:65536/", "https://host:12x/", "https://host:80:90/", "https://[bad]/", "https://[::1]oops/",
		"https://host/\r\nInjected: x", "https://host\\other/", "https://ho st/", "https://%68ost/" })
		EXPECT_THROW(HttpConnection::parseUrl(url), Exception) << url;
	EXPECT_THROW(HttpConnection::parseUrl(string("https://host/\0bad", 17)), Exception);
	const auto parsed = HttpConnection::parseUrl("HTTPS://[::1]:0443?q=1#fragment");
	EXPECT_EQ("https://[::1]", parsed.origin());
	EXPECT_EQ("/?q=1", parsed.target);
	EXPECT_EQ("http://example.test:8080", HttpConnection::parseUrl("http://EXAMPLE.test:8080/").origin());
}
TEST(HttpUrl, ResolvesRelativeQueryNetworkAndAbsoluteRedirects) {
	const string base = "https://example.test:8443/a/b?old=1";
	EXPECT_EQ("https://example.test:8443/c", HttpConnection::resolveRedirect(base, "../c"));
	EXPECT_EQ("https://example.test:8443/a/b?new=2", HttpConnection::resolveRedirect(base, "?new=2"));
	EXPECT_EQ("https://other.test/x", HttpConnection::resolveRedirect(base, "//other.test/x"));
	EXPECT_EQ("https://example.test:8443/root", HttpConnection::resolveRedirect(base, "/root"));
	EXPECT_THROW(HttpConnection::resolveRedirect(base, ""), Exception);
	EXPECT_THROW(HttpConnection::resolveRedirect(base, "file:secret"), Exception);
}

namespace {
class HttpTransport : public testing::Test {
public:
	void SetUp() override {
#ifdef _WIN32
		WSADATA data {};
		ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &data));
#endif
	}
	void TearDown() override {
#ifdef _WIN32
		WSACleanup();
#endif
	}
	string fetch(const string& response, uint64_t limit = 1024) {
		Socket server(Socket::TYPE_TCP);
		server.setLocalIp4("127.0.0.1"); server.setV4only(true);
		const auto port = server.listen("0");
		auto peer = std::async(std::launch::async, [&] {
			try {
				if(!server.wait(3000, true, false).first) return;
				Socket client(Socket::TYPE_TCP);
				client.accept(server);
				string request;
				char buffer[1024];
				while(request.find("\r\n\r\n") == string::npos) {
					if(!client.wait(3000, true, false).first) return;
					const auto n = client.read(buffer, sizeof(buffer));
					if(n <= 0) return;
					request.append(buffer, n);
				}
				client.writeAll(response.data(), static_cast<int>(response.size()), 3000);
			} catch(const Exception&) { }
		});
		string result;
		HttpConnection connection;
		HttpConnection::Options options;
		options.maxBody = limit; options.timeoutMs = 3000;
		connection.downloadBlocking("http://127.0.0.1:" + port + "/", options,
			[&](const HttpConnection&, const uint8_t* data, size_t n) { result.append(reinterpret_cast<const char*>(data), n); });
		return result;
	}
};
}
TEST_F(HttpTransport, ReadsFixedChunkedEmptyAndCloseDelimitedBodies) {
	EXPECT_EQ("hello", fetch("HTTP/1.1 200 OK\r\nContent-Length:5\r\n\r\nhello"));
	EXPECT_EQ("hello", fetch("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;ext=x\r\nhe\r\n3\r\nllo\r\n0\r\nX-Checksum: ignored\r\n\r\n"));
	EXPECT_EQ("hello", fetch("HTTP/1.0 200 OK\r\n\r\nhello"));
	EXPECT_EQ("", fetch("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"));
	EXPECT_EQ("", fetch("HTTP/1.1 204 No Content\r\n\r\n"));
	EXPECT_EQ("hello", fetch("HTTP/1.1 103 Early Hints\r\nLink: </a>\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"));
}
TEST_F(HttpTransport, RejectsAmbiguousMalformedTruncatedAndOversizedResponses) {
	for(const string response : {
		"HTTP/1.1 404 200 in reason\r\nContent-Length: 0\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 4\r\nContent-Length: 4\r\n\r\ndata",
		"HTTP/1.1 200 OK\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 9223372036854775808\r\n\r\n",
		"HTTP/1.1 200 OK\r\nContent-Length: 5oops\r\n\r\nhello",
		"HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nhello",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nab",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n",
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",
		"HTTP/1.1 204 No Content\r\nContent-Length: 2\r\n\r\nhi",
		"HTTP/1.1 200 OK\r\nContent-Length: 1025\r\n\r\n" })
		EXPECT_THROW(fetch(response), Exception) << response;
	EXPECT_THROW(fetch("HTTP/1.1 200 OK\r\n\r\nhello", 4), Exception);
	EXPECT_THROW(fetch("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n", 4), Exception);
	EXPECT_THROW(fetch("HTTP/1.1 200 OK\r\nX-Large: " + string(9000, 'a') + "\r\n\r\n"), Exception);
}
TEST_F(HttpTransport, CancelsAndTimesOutWithoutWaitingForResponse) {
	Socket server(Socket::TYPE_TCP);
	server.setLocalIp4("127.0.0.1"); server.setV4only(true);
	const auto port = server.listen("0");
	HttpConnection connection;
	HttpConnection::Options options;
	options.timeoutMs = 200; options.idleTimeoutMs = 200;
	const auto began = std::chrono::steady_clock::now();
	EXPECT_THROW(connection.downloadBlocking("http://127.0.0.1:" + port + "/", options, {}), Exception);
	EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(2));
	options.cancelled = [] { return true; };
	EXPECT_THROW(connection.downloadBlocking("http://127.0.0.1:" + port + "/", options, {}), Exception);
}

TEST_F(HttpTransport, OptInTLSProxyAndRedirectIntegration) {
	const auto base = std::getenv("DCPP_HTTP_TEST_HTTPS");
	const auto badName = std::getenv("DCPP_HTTP_TEST_BAD_NAME");
	const auto proxy = std::getenv("DCPP_HTTP_TEST_PROXY");
	if(!base || !badName || !proxy) GTEST_SKIP() << "Run test/support/run_http_tests.py";
	auto get = [&](const string& path, HttpConnection::Options options) {
		HttpConnection connection;
		string result;
		connection.downloadBlocking(string(base) + path, options, [&](const HttpConnection&, const uint8_t* data, size_t n) {
			result.append(reinterpret_cast<const char*>(data), n);
		});
		return result;
	};
	HttpConnection::Options options;
	options.httpsOnly = true;
	options.authorization = "Basic secret";
	options.authorizationOrigin = HttpConnection::parseUrl(base).origin();
	EXPECT_EQ("Basic secret", get("/auth", options));
	EXPECT_EQ("Basic secret", get("/relative", options));
	EXPECT_EQ("hello", get("/close", options));
	EXPECT_THROW(get("/truncated", options), Exception);
	EXPECT_THROW(get("/abrupt", options), Exception);
	EXPECT_EQ("none", get("/cross-origin", options));
	EXPECT_THROW(get("/downgrade", options), Exception);
	EXPECT_THROW(get("/loop", options), Exception);
	options.sameOrigin = true;
	EXPECT_THROW(get("/cross-origin", options), Exception);
	options.proxy = proxy;
	EXPECT_EQ("Basic secret", get("/auth", options));
	options.proxy.clear();
	HttpConnection wrongName;
	EXPECT_THROW(wrongName.downloadBlocking(badName, options, {}), Exception);
	SettingsManager::newInstance(); ConnectivityManager::newInstance(); TimerManager::newInstance(); HttpManager::newInstance();
	std::promise<Flags::MaskType> finished;
	auto future = finished.get_future();
	StringOutputStream body;
	HttpManager::getInstance()->download(string(base) + "/auth", &body,
		[&](const HttpConnection*, OutputStream*, Flags::MaskType flags) { finished.set_value(flags); });
	const auto ready = future.wait_for(std::chrono::seconds(5));
	EXPECT_EQ(std::future_status::ready, ready);
	if(ready == std::future_status::ready) {
		EXPECT_TRUE(future.get() & HttpManager::CONN_COMPLETE);
	}
	EXPECT_EQ("none", body.stringRef());
	std::promise<Flags::MaskType> posted;
	auto postResult = posted.get_future();
	StringOutputStream postBody;
	HttpManager::getInstance()->download(string(base) + "/post", StringMap { { "key", "value" } }, &postBody,
		[&](const HttpConnection*, OutputStream*, Flags::MaskType flags) { posted.set_value(flags); });
	const auto postReady = postResult.wait_for(std::chrono::seconds(5));
	EXPECT_EQ(std::future_status::ready, postReady);
	if(postReady == std::future_status::ready) {
		EXPECT_TRUE(postResult.get() & HttpManager::CONN_COMPLETE);
		EXPECT_EQ("key=value", postBody.stringRef());
	}
	std::promise<Flags::MaskType> cancelled;
	auto cancelResult = cancelled.get_future();
	const auto stallUrl = string(base) + "/stall";
	HttpManager::getInstance()->download(stallUrl,
		[&](const HttpConnection*, OutputStream*, Flags::MaskType flags) { cancelled.set_value(flags); });
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	const auto cancelStart = std::chrono::steady_clock::now();
	HttpManager::getInstance()->disconnect(stallUrl);
	EXPECT_LT(std::chrono::steady_clock::now() - cancelStart, std::chrono::seconds(2));
	const auto cancelReady = cancelResult.wait_for(std::chrono::seconds(2));
	EXPECT_EQ(std::future_status::ready, cancelReady);
	if(cancelReady == std::future_status::ready) {
		EXPECT_TRUE(cancelResult.get() & HttpManager::CONN_FAILED);
	}
	HttpManager::getInstance()->shutdown();
	HttpManager::deleteInstance(); TimerManager::getInstance()->shutdown(); TimerManager::deleteInstance();
	ConnectivityManager::deleteInstance(); SettingsManager::deleteInstance();
}
