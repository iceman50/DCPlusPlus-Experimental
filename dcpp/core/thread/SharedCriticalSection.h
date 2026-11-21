/*
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

#ifndef DCPLUSPLUS_DCPP_SHARED_CRITICAL_SECTION_H
#define DCPLUSPLUS_DCPP_SHARED_CRITICAL_SECTION_H

#include <atomic>
#include <exception>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace dcpp {

/**
 * A reader/writer lock that, like CriticalSection, may be taken again by a thread that holds it:
 * - shared while holding it shared or exclusively;
 * - exclusively while holding it exclusively.
 *
 * Only the outermost hold of a thread touches the underlying lock, so a nested shared hold never
 * waits behind a writer that is itself waiting for this thread (the deadlock a plain
 * std::shared_mutex has with re-entrant readers).
 *
 * Asking for it exclusively while holding it only shared can never succeed (the writer would wait
 * for its own reader); that ends the program at once rather than hanging it.
 */
class SharedCriticalSection {
public:
	SharedCriticalSection() = default;
	SharedCriticalSection(const SharedCriticalSection&) = delete;
	SharedCriticalSection& operator=(const SharedCriticalSection&) = delete;

	void lock() {
		const auto self = std::this_thread::get_id();
		if(owner.load(std::memory_order_relaxed) == self) {
			++writeDepth;
			return;
		}
		if(const auto held = find(); held && held->depth > 0) {
			std::terminate();
		}
		mutex.lock();
		owner.store(self, std::memory_order_relaxed);
		writeDepth = 1;
	}

	void unlock() {
		if(--writeDepth > 0) {
			return;
		}
		owner.store(std::thread::id(), std::memory_order_relaxed);
		const auto held = find();
		mutex.unlock();
		if(held && held->depth > 0) {
			// A shared hold taken inside the exclusive one outlives it: it becomes a real one.
			// (Scoped locks release in reverse order, so this needs an explicit early unlock.)
			mutex.lock_shared();
			held->real = true;
		}
	}

	void lock_shared() {
		auto& held = get();
		if(held.depth > 0 || owner.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
			++held.depth;
			return;
		}
		mutex.lock_shared();
		held.depth = 1;
		held.real = true;
	}

	void unlock_shared() {
		auto& held = get();
		if(--held.depth > 0) {
			return;
		}
		const bool real = held.real;
		release();
		if(real) {
			mutex.unlock_shared();
		}
	}

	bool try_lock() {
		const auto self = std::this_thread::get_id();
		if(owner.load(std::memory_order_relaxed) == self) {
			++writeDepth;
			return true;
		}
		if(const auto held = find(); held && held->depth > 0) {
			return false;
		}
		if(!mutex.try_lock()) {
			return false;
		}
		owner.store(self, std::memory_order_relaxed);
		writeDepth = 1;
		return true;
	}

	bool try_lock_shared() {
		auto& held = get();
		if(held.depth > 0 || owner.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
			++held.depth;
			return true;
		}
		if(!mutex.try_lock_shared()) {
			release();
			return false;
		}
		held.depth = 1;
		held.real = true;
		return true;
	}

	/** Whether the calling thread holds it exclusively. */
	bool ownedByThisThread() const noexcept {
		return owner.load(std::memory_order_relaxed) == std::this_thread::get_id();
	}

private:
	struct Held {
		const SharedCriticalSection* lock;
		unsigned depth;
		bool real;
	};

	/** The shared holds of the calling thread, one entry per lock it holds shared. */
	static std::vector<Held>& heldByThread() {
		static thread_local std::vector<Held> held;
		return held;
	}

	Held* find() const {
		for(auto& held: heldByThread()) {
			if(held.lock == this) {
				return &held;
			}
		}
		return nullptr;
	}

	Held& get() {
		if(const auto held = find()) {
			return *held;
		}
		auto& list = heldByThread();
		list.push_back({ this, 0, false });
		return list.back();
	}

	void release() {
		auto& list = heldByThread();
		for(auto i = list.begin(); i != list.end(); ++i) {
			if(i->lock == this) {
				list.erase(i);
				return;
			}
		}
	}

	std::shared_mutex mutex;
	std::atomic<std::thread::id> owner { std::thread::id() };
	/** Only touched by the owner. */
	unsigned writeDepth = 0;
};

typedef std::shared_lock<SharedCriticalSection> RLock;
typedef std::unique_lock<SharedCriticalSection> WLock;

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_SHARED_CRITICAL_SECTION_H
