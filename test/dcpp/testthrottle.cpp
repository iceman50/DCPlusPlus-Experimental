// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"

#include <dcpp/hub/ClientManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/transfer/download/DownloadManager.h>
#include <dcpp/connection/ThrottleManager.h>
#include <dcpp/transfer/upload/UploadManager.h>

#include <chrono>
#include <future>
#include <thread>

using namespace dcpp;
using namespace std::chrono_literals;

namespace {

// Exercise the production limiter without allocating or copying GiB-sized buffers.
class CountingSocket : public Socket {
public:
	CountingSocket() : Socket(TYPE_TCP) { }
	int read(void*, int len) override { received += len; return len; }
	int write(const void*, int len) override { sent += len; return len; }
	std::atomic<int64_t> received { 0 }, sent { 0 };
};

class ThrottleManagerTest : public testing::Test {
protected:
	void SetUp() override {
		SettingsManager::newInstance();
		TimerManager::newInstance();
		ClientManager::newInstance();
		DownloadManager::newInstance();
		UploadManager::newInstance();
		// Deliver controlled second events only to the limiter, without starting a clock thread.
		TimerManager::getInstance()->removeListeners();
		ThrottleManager::newInstance();
	}

	void TearDown() override {
		ThrottleManager::deleteInstance();
		UploadManager::deleteInstance();
		DownloadManager::deleteInstance();
		ClientManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		SettingsManager::deleteInstance();
	}

	void setRate(int kib) {
		SettingsManager::getInstance()->set(SettingsManager::MAX_UPLOAD_SPEED_MAIN, kib);
		SettingsManager::getInstance()->set(SettingsManager::MAX_DOWNLOAD_SPEED_MAIN, kib);
	}
	void refill() { TimerManager::getInstance()->fire(TimerManagerListener::Second(), uint64_t(0)); }

	void transfer(CountingSocket& socket, int64_t bytes) {
		auto limiter = ThrottleManager::getInstance();
		while(bytes > 0) {
			auto len = static_cast<size_t>(std::min<int64_t>(bytes, 16 * 1024 * 1024));
			EXPECT_EQ(static_cast<int>(len), limiter->read(&socket, nullptr, len, true));
			EXPECT_EQ(static_cast<int>(len), limiter->write(&socket, nullptr, len, true));
			bytes -= len;
		}
	}
};

TEST_F(ThrottleManagerTest, sustains_large_budgets_across_repeated_refills)
{
	CountingSocket socket;
	int64_t total = 0;
	for(int rate: { 2097152, 4194304, 16777216 }) {
		setRate(rate);
		const auto budget = TransferRate::fromKiB(rate);
		for(int second = 0; second < 20; ++second) {
			refill();
			transfer(socket, budget);
			total += budget;
			ASSERT_EQ(total, socket.sent.load());
			ASSERT_EQ(total, socket.received.load());
		}
	}
}

TEST_F(ThrottleManagerTest, concurrent_transfers_exhaust_one_shared_budget_and_resume_after_refill)
{
	setRate(16777216);
	refill();
	CountingSocket socket;
	std::vector<std::future<void>> workers;
	for(int i = 0; i < 4; ++i)
		workers.push_back(std::async(std::launch::async, [&] { transfer(socket, 4LL * 1024 * 1024 * 1024); }));
	for(auto& worker: workers) {
		const auto ready = worker.wait_for(5s);
		EXPECT_EQ(std::future_status::ready, ready);
		if(ready != std::future_status::ready)
			ThrottleManager::getInstance()->shutdown();
		worker.get();
	}
	EXPECT_EQ(17179869184LL, socket.sent.load());
	EXPECT_EQ(17179869184LL, socket.received.load());

	auto read = std::async(std::launch::async, [&] {
		return ThrottleManager::getInstance()->read(&socket, nullptr, 1024, true);
	});
	auto write = std::async(std::launch::async, [&] {
		size_t len = 1024;
		return ThrottleManager::getInstance()->write(&socket, nullptr, len, true);
	});
	EXPECT_EQ(std::future_status::timeout, read.wait_for(20ms));
	EXPECT_EQ(std::future_status::timeout, write.wait_for(20ms));
	refill();
	// Exhausted calls return the BufferedSocket retry indicators after waking.
	EXPECT_EQ(-1, read.get());
	EXPECT_EQ(0, write.get());
	transfer(socket, 17179869184LL);
	EXPECT_EQ(34359738368LL, socket.sent.load());
	EXPECT_EQ(34359738368LL, socket.received.load());
}

TEST_F(ThrottleManagerTest, maximum_rate_keeps_individual_socket_calls_within_int_range)
{
	setRate(16777216);
	refill();
	CountingSocket socket;
	const auto requested = std::numeric_limits<size_t>::max();
	EXPECT_EQ(std::numeric_limits<int>::max(), ThrottleManager::getInstance()->read(&socket, nullptr, requested, true));
	auto len = requested;
	EXPECT_EQ(std::numeric_limits<int>::max(), ThrottleManager::getInstance()->write(&socket, nullptr, len, true));
	EXPECT_EQ(static_cast<size_t>(std::numeric_limits<int>::max()), len);
}

TEST_F(ThrottleManagerTest, measures_sustained_loopback_at_the_maximum_configured_rate)
{
#ifdef _WIN32
	struct NetworkSession {
		WSADATA data;
		int result = WSAStartup(MAKEWORD(2, 2), &data);
		~NetworkSession() { if(result == 0) WSACleanup(); }
	} network;
	ASSERT_EQ(0, network.result);
#endif
	Socket listener(Socket::TYPE_TCP), sender(Socket::TYPE_TCP), receiver(Socket::TYPE_TCP);
	listener.setV4only(true);
	listener.setLocalIp4("127.0.0.1");
	const auto port = listener.listen("0");
	sender.setV4only(true);
	sender.connect("127.0.0.1", port);
	ASSERT_TRUE(sender.waitConnected(2000));
	ASSERT_TRUE(listener.wait(2000, true, false).first);
	receiver.accept(listener);
	setRate(16777216);
	refill();
	std::atomic<bool> stop { false }, writerDone { false }, cancel { false };
	std::atomic<int64_t> sent { 0 }, received { 0 };
	const auto start = std::chrono::steady_clock::now();
	auto writer = std::async(std::launch::async, [&] {
		std::vector<char> data(64 * 1024, 'x');
		while(!stop) {
			if(!sender.wait(10, false, true).second) continue;
			size_t len = data.size();
			const auto n = ThrottleManager::getInstance()->write(&sender, data.data(), len, true);
			if(n > 0) sent += n;
		}
	});
	auto reader = std::async(std::launch::async, [&] {
		std::vector<char> data(64 * 1024);
		while(!cancel && (!writerDone || received < sent)) {
			if(!receiver.wait(10, true, false).first) continue;
			const auto n = ThrottleManager::getInstance()->read(&receiver, data.data(), data.size(), true);
			if(n > 0) received += n;
		}
	});
	for(int second = 1; second <= 3; ++second) {
		std::this_thread::sleep_until(start + std::chrono::seconds(second));
		if(second < 3) refill();
	}
	stop = true;
	ThrottleManager::getInstance()->shutdown();
	// Unblock both workers before checking their results, including on failure.
	writer.wait();
	writerDone = true;
	const auto readerStatus = reader.wait_for(5s);
	cancel = true;
	reader.wait();
	writer.get();
	reader.get();
	EXPECT_EQ(std::future_status::ready, readerStatus);
	EXPECT_GT(sent.load(), 0);
	EXPECT_EQ(sent.load(), received.load());
	const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	std::printf("Loopback with 16 GiB/s limits: %lld bytes, %.3f s, %.3f GiB/s (machine-dependent)\n",
		static_cast<long long>(received.load()), seconds, received / seconds / (1024.0 * 1024 * 1024));
}

} // namespace
