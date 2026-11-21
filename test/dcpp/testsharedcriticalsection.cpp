// Copyright (C) 2026 iceman50
// SharedCriticalSection: the share lock. Readers run in parallel, writers exclude everyone, and a
// thread that holds it may take it again (shared under shared or exclusive, exclusive under
// exclusive) without deadlocking, even while another thread's writer is waiting.
#include "testbase.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <dcpp/core/thread/SharedCriticalSection.h>

using namespace dcpp;
using namespace std::chrono_literals;

namespace {

/** Waits for a future; a deadlock shows up as a timeout instead of a hung test run. */
template<typename T>
bool ready(std::future<T>& f, std::chrono::milliseconds timeout = 5s) {
	return f.wait_for(timeout) == std::future_status::ready;
}

} // namespace

TEST(SharedCriticalSectionTest, nested_shared_hold_does_not_wait_behind_a_waiting_writer) {
	SharedCriticalSection cs;
	std::promise<void> outerHeld, writerQueued;
	std::atomic<bool> writerIn { false };

	auto reader = std::async(std::launch::async, [&] {
		RLock outer(cs);
		outerHeld.set_value();
		writerQueued.get_future().wait();
		// A writer is now waiting for this thread to let go. Taking the lock shared again must not
		// queue behind it (that is the deadlock a plain std::shared_mutex has).
		RLock inner(cs);
		RLock innermost(cs);
		return !writerIn.load();
	});

	outerHeld.get_future().wait();
	auto writer = std::async(std::launch::async, [&] {
		WLock l(cs);
		writerIn = true;
	});
	std::this_thread::sleep_for(50ms);
	writerQueued.set_value();

	ASSERT_TRUE(ready(reader));
	EXPECT_TRUE(reader.get());
	ASSERT_TRUE(ready(writer));
	EXPECT_TRUE(writerIn.load());
}

TEST(SharedCriticalSectionTest, the_writer_may_take_it_again_shared_or_exclusive) {
	SharedCriticalSection cs;
	{
		WLock w(cs);
		EXPECT_TRUE(cs.ownedByThisThread());
		{
			RLock r(cs);
			RLock r2(cs);
			WLock w2(cs);
			EXPECT_TRUE(cs.ownedByThisThread());
		}
		EXPECT_TRUE(cs.ownedByThisThread());

		// Still exclusive: another thread can't read.
		auto other = std::async(std::launch::async, [&] {
			std::shared_lock<SharedCriticalSection> r(cs, std::defer_lock);
			return r.try_lock();
		});
		ASSERT_TRUE(ready(other));
		EXPECT_FALSE(other.get());
	}
	EXPECT_FALSE(cs.ownedByThisThread());

	// Released completely: another thread can write.
	auto other = std::async(std::launch::async, [&] { WLock l(cs); return true; });
	ASSERT_TRUE(ready(other));
	EXPECT_TRUE(other.get());
}

TEST(SharedCriticalSectionTest, readers_hold_it_at_the_same_time) {
	SharedCriticalSection cs;
	std::atomic<int> inside { 0 };
	std::atomic<int> maxInside { 0 };
	auto read = [&] {
		RLock l(cs);
		const auto n = ++inside;
		int seen = maxInside;
		while(n > seen && !maxInside.compare_exchange_weak(seen, n)) { }
		// Stay until another reader is inside too (or give up after a while).
		for(int i = 0; i < 500 && maxInside < 2; ++i) {
			std::this_thread::sleep_for(1ms);
		}
		--inside;
	};
	auto a = std::async(std::launch::async, read);
	auto b = std::async(std::launch::async, read);
	ASSERT_TRUE(ready(a));
	ASSERT_TRUE(ready(b));
	EXPECT_EQ(2, maxInside.load());
}

TEST(SharedCriticalSectionTest, writers_exclude_readers_and_writers) {
	SharedCriticalSection cs;
	int64_t a = 0, b = 0;
	std::atomic<bool> torn { false };
	std::atomic<bool> stop { false };

	std::vector<std::future<void>> threads;
	for(int t = 0; t < 4; ++t) {
		threads.push_back(std::async(std::launch::async, [&] {
			for(int i = 0; i < 20000; ++i) {
				WLock l(cs);
				++a;
				++b;
			}
		}));
	}
	for(int t = 0; t < 4; ++t) {
		threads.push_back(std::async(std::launch::async, [&] {
			while(!stop) {
				RLock l(cs);
				if(a != b) torn = true;
			}
		}));
	}
	for(int t = 0; t < 4; ++t) {
		ASSERT_TRUE(ready(threads[t], 60s));
	}
	stop = true;
	for(auto& t: threads) {
		ASSERT_TRUE(ready(t, 60s));
	}
	EXPECT_EQ(80000, a);
	EXPECT_EQ(80000, b);
	EXPECT_FALSE(torn.load());
}

TEST(SharedCriticalSectionTest, a_writer_gets_in_while_readers_keep_arriving) {
	// Searches arrive all the time on a busy hub; a refresh or a hash result must still get in.
	SharedCriticalSection cs;
	std::atomic<bool> stop { false };
	std::vector<std::future<void>> readers;
	for(int t = 0; t < 6; ++t) {
		readers.push_back(std::async(std::launch::async, [&] {
			while(!stop) {
				RLock l(cs);
				std::this_thread::sleep_for(std::chrono::microseconds(200));
			}
		}));
	}
	std::this_thread::sleep_for(20ms);

	for(int i = 0; i < 20; ++i) {
		auto writer = std::async(std::launch::async, [&] { WLock l(cs); });
		ASSERT_TRUE(ready(writer, 2s)) << "writer starved on attempt " << i;
	}
	stop = true;
	for(auto& r: readers) {
		ASSERT_TRUE(ready(r));
	}
}

TEST(SharedCriticalSectionTest, a_shared_hold_that_outlives_the_exclusive_one_keeps_readers_only) {
	SharedCriticalSection cs;
	WLock w(cs);
	RLock r(cs);
	w.unlock();
	EXPECT_FALSE(cs.ownedByThisThread());

	// Now held shared only: other readers get in, writers wait.
	auto reader = std::async(std::launch::async, [&] { RLock l(cs); return true; });
	ASSERT_TRUE(ready(reader));
	EXPECT_TRUE(reader.get());

	std::atomic<bool> writerIn { false };
	auto writer = std::async(std::launch::async, [&] { WLock l(cs); writerIn = true; });
	std::this_thread::sleep_for(50ms);
	EXPECT_FALSE(writerIn.load());
	r.unlock();
	ASSERT_TRUE(ready(writer));
	EXPECT_TRUE(writerIn.load());
}

TEST(SharedCriticalSectionTest, separate_locks_are_tracked_separately) {
	SharedCriticalSection a, b;
	RLock ra(a);
	// Holding a shared says nothing about b: b can be taken exclusively.
	WLock wb(b);
	EXPECT_TRUE(b.ownedByThisThread());
	EXPECT_FALSE(a.ownedByThisThread());
	wb.unlock();

	auto writer = std::async(std::launch::async, [&] { WLock l(b); return true; });
	ASSERT_TRUE(ready(writer));
	EXPECT_TRUE(writer.get());
}
