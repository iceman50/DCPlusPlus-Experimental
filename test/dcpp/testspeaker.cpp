// Copyright (C) 2026 iceman50
#include "testbase.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <dcpp/core/Speaker.h>

using namespace dcpp;
using namespace std::chrono_literals;

namespace {

class TestSpeakerListener {
public:
	virtual ~TestSpeakerListener() = default;
	template<int I> struct X { };
	using Event = X<0>;

	virtual void on(Event, bool removeSelf) noexcept = 0;
};

class TestSpeaker : public Speaker<TestSpeakerListener> {
public:
	bool contains(TestSpeakerListener* listener) {
		Lock l(listenerCS);
		return std::find(listeners.begin(), listeners.end(), listener) != listeners.end();
	}
};

class ReentrantListener : public TestSpeakerListener {
public:
	explicit ReentrantListener(TestSpeaker& speaker_) : speaker(speaker_) { }

	void on(Event, bool removeSelf) noexcept override {
		{
			std::unique_lock<std::mutex> l(mutex);
			++entered;
			cv.notify_all();
			cv.wait(l, [this] { return entered == 2; });
		}

		if(removeSelf) {
			speaker.removeListener(this);
			{
				std::lock_guard<std::mutex> l(mutex);
				removalCompleted = true;
			}
			cv.notify_all();
		} else {
			// Once the listener disappears, the other callback is inside
			// removeListener and waiting for our active count to decrement.
			while(speaker.contains(this)) {
				std::this_thread::yield();
			}
		}
	}

	TestSpeaker& speaker;
	std::mutex mutex;
	std::condition_variable cv;
	int entered = 0;
	bool removalCompleted = false;
};

class BlockingListener : public TestSpeakerListener {
public:
	void on(Event, bool) noexcept override {
		std::unique_lock<std::mutex> l(mutex);
		entered = true;
		cv.notify_all();
		cv.wait(l, [this] { return release; });
	}

	std::mutex mutex;
	std::condition_variable cv;
	bool entered = false;
	bool release = false;
	bool removalCompleted = false;
};

} // namespace

TEST(testspeaker, reentrant_removal_wakes_at_local_callback_depth) {
	auto speaker = new TestSpeaker;
	auto listener = new ReentrantListener(*speaker);
	speaker->addListener(listener);

	std::thread first([&] { speaker->fire(TestSpeakerListener::Event(), true); });
	std::thread second([&] { speaker->fire(TestSpeakerListener::Event(), false); });

	bool completed;
	{
		std::unique_lock<std::mutex> l(listener->mutex);
		completed = listener->cv.wait_for(l, 2s, [&] { return listener->removalCompleted; });
	}

	second.join();
	if(!completed) {
		// Keep the synchronization objects alive so a failing implementation
		// can be reported without hanging the entire test executable.
		first.detach();
		ADD_FAILURE() << "Re-entrant listener removal did not wake at its local callback depth";
		return;
	}

	first.join();
	delete listener;
	delete speaker;
}

TEST(testspeaker, external_removal_waits_for_active_callback) {
	auto speaker = new TestSpeaker;
	auto listener = new BlockingListener;
	speaker->addListener(listener);

	std::thread firing([&] { speaker->fire(TestSpeakerListener::Event(), false); });
	bool entered;
	{
		std::unique_lock<std::mutex> l(listener->mutex);
		entered = listener->cv.wait_for(l, 2s, [&] { return listener->entered; });
	}
	if(!entered) {
		firing.detach();
		ADD_FAILURE() << "Listener callback did not start";
		return;
	}

	std::thread removing([&] {
		speaker->removeListener(listener);
		{
			std::lock_guard<std::mutex> l(listener->mutex);
			listener->removalCompleted = true;
		}
		listener->cv.notify_all();
	});

	{
		std::unique_lock<std::mutex> l(listener->mutex);
		EXPECT_FALSE(listener->cv.wait_for(l, 100ms, [&] { return listener->removalCompleted; }));
		listener->release = true;
	}
	listener->cv.notify_all();

	bool completed;
	{
		std::unique_lock<std::mutex> l(listener->mutex);
		completed = listener->cv.wait_for(l, 2s, [&] { return listener->removalCompleted; });
	}
	firing.join();
	if(!completed) {
		removing.detach();
		ADD_FAILURE() << "External listener removal did not finish after its callback returned";
		return;
	}

	removing.join();
	delete listener;
	delete speaker;
}

namespace {

class CountingListener : public TestSpeakerListener {
public:
	void on(Event, bool) noexcept override { ++calls; }
	int calls = 0;
};

/** Reacts to an event by registering more listeners and firing again on the same thread, the
pattern that let upstream DC++ 0.884 replace the shared snapshot its outer fire() was iterating. */
class ReactingListener : public TestSpeakerListener {
public:
	explicit ReactingListener(TestSpeaker& speaker_) : speaker(speaker_) { }
	void on(Event, bool) noexcept override {
		if(depth++ > 0) return;
		for(auto& listener: added) speaker.addListener(&listener);
		speaker.fire(Event(), false);
		// With a shared snapshot, the outer fire() is now iterating freed memory. Reuse that memory
		// so such an implementation calls the wrong listeners instead of passing by luck.
		std::vector<std::vector<char>> churn;
		for(int i = 0; i < 64; ++i) churn.emplace_back(16, static_cast<char>(0xEE));
	}
	TestSpeaker& speaker;
	std::vector<CountingListener> added = std::vector<CountingListener>(64);
	int depth = 0;
};

class RemovingListener : public TestSpeakerListener {
public:
	RemovingListener(TestSpeaker& speaker_, CountingListener& victim_) : speaker(speaker_), victim(victim_) { }
	void on(Event, bool) noexcept override { speaker.removeListener(&victim); }
	TestSpeaker& speaker;
	CountingListener& victim;
};

} // namespace

TEST(testspeaker, reentrant_fire_calls_each_listener_of_each_event_once) {
	TestSpeaker speaker;
	ReactingListener reacting(speaker);
	CountingListener after;
	speaker.addListener(&reacting);
	speaker.addListener(&after);

	speaker.fire(TestSpeakerListener::Event(), false);

	// The outer event reaches the two original listeners; the inner one also reaches every
	// listener added before it. Listeners added during the outer event are not called by it.
	EXPECT_EQ(2, after.calls);
	EXPECT_EQ(2, reacting.depth);
	for(const auto& listener: reacting.added) {
		EXPECT_EQ(1, listener.calls);
	}
}

TEST(testspeaker, listener_removed_earlier_in_the_same_event_is_not_called) {
	TestSpeaker speaker;
	CountingListener victim;
	RemovingListener remover(speaker, victim);
	speaker.addListener(&remover);
	speaker.addListener(&victim);

	// Code normally deletes a listener right after removing it, so calling it later in the same
	// event would use a destroyed object.
	speaker.fire(TestSpeakerListener::Event(), false);
	EXPECT_EQ(0, victim.calls);
	EXPECT_FALSE(speaker.contains(&victim));
}
