// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"
#include <dwt/widgets/TableTree.h>
#include <win32/SingleInstance.h>
#include <win32/util/UiServices.h>
#include <win32/util/IconManager.h>
#include <win32/util/AppAppearance.h>
#include <win32/util/UiFonts.h>
#include <win32/util/ImageLists.h>
#include <win32/components/TypedTable.h>
#include <win32/AspectUserCommand.h>
#include <win32/UserInfoBase.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/core/io/File.h>
#include <dcpp/transfer/download/DownloadManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Container.h>
#include <dwt/Application.h>
#include <dwmapi.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

// Keep test access in this translation unit, following the core test pattern.
#define private public
#include <win32/TransferView.h>
#undef private

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 181;

class TransferViewPerformanceTest {
	struct PaintProbe {
		std::function<bool()> updated;
		std::vector<double> milliseconds;
		bool complete = false;
	};
	static LRESULT CALLBACK watchPaint(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
		auto& probe = *reinterpret_cast<PaintProbe*>(data);
		if(message != WM_PAINT) return DefSubclassProc(window, message, wParam, lParam);
		const auto start = std::chrono::steady_clock::now();
		const auto result = DefSubclassProc(window, message, wParam, lParam);
		if(probe.updated && probe.updated()) {
			probe.milliseconds.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
			probe.complete = true;
		}
		return result;
	}
	static void check(bool value, const char* message) {
		if(!value) throw std::runtime_error(message);
	}
public:
	static void run(dwt::Window* window, size_t count, size_t perGroup, int sortColumn, bool visiblePaint = false) {
		using Clock = std::chrono::steady_clock;
		auto view = new TransferView(window, nullptr);
		view->resize(dwt::Rectangle(0, 0, 1000, 400));
		view->transfers->setSort(sortColumn, true);
		HintedUser peer(UserPtr(new User(CID::generate())), "adcs://benchmark.invalid");
		for(size_t i = 0; i < count; ++i) {
			TransferView::UpdateInfo update(peer, CONNECTION_TYPE_UPLOAD, std::to_string(i));
			update.setFile("benchmark-" + std::to_string(i / perGroup));
			update.setStatus(TransferView::STATUS_RUNNING);
			update.setTransferred(0, 0, 1024 * 1024 * 1024);
			view->addConn(update);
		}
		view->execTasks();
		PaintProbe probe;
		if(visiblePaint) {
			view->layout();
			check(SetWindowSubclass(view->transfers->handle(), watchPaint, 1, reinterpret_cast<DWORD_PTR>(&probe)) != FALSE, "Cannot observe table painting");
			view->setVisible(true);
			view->transfers->setVisible(true);
			UpdateWindow(view->transfers->handle());
		}
		std::vector<double> timings;
		for(int round = 0; round < 9; ++round) {
			std::vector<TransferView::UpdateInfo> updates;
			for(size_t i = 0; i < count; ++i) {
				auto update = std::make_unique<TransferView::UpdateInfo>(peer, CONNECTION_TYPE_UPLOAD, std::to_string(i));
				update->setTransferred((round + 1) * 4096, (round + 1) * 4096, 1024 * 1024 * 1024);
				update->setSpeed(1000000 + round);
				if(visiblePaint) {
					updates.push_back(std::move(*update));
				} else {
					view->resortMask |= update->updateMask;
					view->tasks.emplace_back([view](const TransferView::UpdateInfo& value) { view->updateConn(value); }, std::move(update));
				}
			}
			const auto start = Clock::now();
			if(visiblePaint) {
				probe.complete = false;
				probe.updated = [view, round, perGroup, count] {
					return view->transferItems.front().transferred == static_cast<int64_t>(std::min(perGroup, count)) * (round + 1) * 4096;
				};
				view->updatedConns(std::move(updates));
				// Exercise the normal async handoff and UI batch timer.
				while(!probe.complete && Clock::now() - start < std::chrono::seconds(5)) {
					if(!dwt::Application::instance().dispatch()) Sleep(1);
				}
				check(probe.complete, "Transfer update was not painted within five seconds");
				check(SUCCEEDED(DwmFlush()), "Desktop composition flush failed");
				if(round == 0) probe.milliseconds.clear();
			} else {
				view->execTasks();
			}
			const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
			if(round) timings.push_back(ms);
			for(const auto& transfer: view->transferItems) {
				check(transfer.speed == static_cast<int64_t>(transfer.conns.size()) * (1000000 + round), "Incorrect aggregate speed");
				check(transfer.transferred == static_cast<int64_t>(transfer.conns.size()) * (round + 1) * 4096, "Incorrect aggregate bytes");
				check(!transfer.columns[TransferView::COLUMN_SPEED].empty(), "Missing aggregate speed text");
			}
		}
		std::sort(timings.begin(), timings.end());
		std::printf("%s,%zu,%zu,%d,%.3f,%.3f\n", visiblePaint ? "event_to_paint_and_flush" : "ui", count, perGroup, sortColumn,
			(timings[3] + timings[4]) / 2, timings.back());
		if(visiblePaint) {
			RemoveWindowSubclass(view->transfers->handle(), watchPaint, 1);
			std::sort(probe.milliseconds.begin(), probe.milliseconds.end());
			check(!probe.milliseconds.empty(), "No paint samples");
			std::printf("wm_paint,%zu,%zu,%d,%.3f,%.3f\n", count, perGroup, sortColumn,
				probe.milliseconds[probe.milliseconds.size() / 2], probe.milliseconds.back());
		}
		// Exercise invalidation of cached size/time labels, including an idle
		// transition and a running connection whose remaining time rounds to zero.
		for(int phase = 0; phase < 3; ++phase) {
			TransferView::UpdateInfo update(peer, CONNECTION_TYPE_UPLOAD, "0");
			update.setStatus(phase == 0 ? TransferView::STATUS_WAITING : TransferView::STATUS_RUNNING);
			update.setSpeed(phase == 0 ? 0 : 100);
			update.setTransferred(0, 0, phase == 0 ? 0 : phase == 1 ? 1000 : 1);
			view->updateConn(update);
			view->execTasks();
			auto conn = view->findConn("0", CONNECTION_TYPE_UPLOAD);
			check(conn != nullptr, "Connection vanished during status update");
			check(conn->getText(TransferView::COLUMN_SIZE) == (phase == 0 ? tstring() : Text::toT(Util::formatBytes(update.size))), "Stale size label");
			check(conn->getText(TransferView::COLUMN_TIMELEFT) == (phase == 0 ? tstring() : Text::toT(Util::formatSeconds(phase == 1 ? 10 : 0))), "Stale remaining-time label");
		}
		// Updates followed by removal in the same batch must never leave dangling
		// parent work, and a surviving group must display its final child totals.
		for(size_t i = 0; i < count; ++i) {
			auto update = std::make_unique<TransferView::UpdateInfo>(peer, CONNECTION_TYPE_UPLOAD, std::to_string(i));
			update->setSpeed(7);
			view->tasks.emplace_back([view](const TransferView::UpdateInfo& value) {
				view->updateConn(value);
				view->removeConn(value);
			}, std::move(update));
			if(i + 1 == count / 2) {
				view->resortList = true;
				view->execTasks();
				for(const auto& transfer: view->transferItems) {
					int64_t speed = 0, bytes = 0;
					for(const auto& conn: transfer.conns) { speed += conn.speed; bytes += conn.transferred; }
					check(transfer.speed == speed && transfer.transferred == bytes, "Stale surviving group after removal");
				}
			}
		}
		view->resortList = true;
		view->execTasks();
		check(view->transferItems.empty() && view->connections[CONNECTION_TYPE_UPLOAD].empty(), "Removed transfers remain indexed");
		view->prepareClose();
		DestroyWindow(view->handle());
	}
};

int dwtMain(dwt::Application&) {
	try {
		Util::PathsMap paths;
		for(int i = 0; i < Util::PATH_LAST; ++i) paths[static_cast<Util::Paths>(i)] = "build/transfer-ui-profile/";
		Util::initialize(paths);
		File::ensureDirectory("build/transfer-ui-profile/");
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		HashManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		ConnectionManager::newInstance();
		UploadManager::newInstance();
		DownloadManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
		HttpManager::newInstance();
		IconManager::initialize();
		SettingsManager::getInstance()->set(SettingsManager::ICON_PACK, IconManager::EMBEDDED_PACK);
		AppAppearance::init();
		UiFonts::initDefault();
		UiServices::initFont();
		ImageLists::fileImages = dwt::ImageListPtr(new dwt::ImageList(dwt::Point(16, 16)));
		auto window = new dwt::Window;
		const bool visiblePaint = std::getenv("DCPP_PROFILE_PAINT") != nullptr;
		dwt::Window::Seed seed;
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		if(visiblePaint) {
			window->setText(L"DC++ transfer painting profile (synthetic updates)");
			SetWindowPos(window->handle(), HWND_TOP, 80, 80, 1040, 480, SWP_NOACTIVATE);
			ShowWindow(window->handle(), SW_SHOWNOACTIVATE);
		}
		std::puts("area,connections,connections_per_group,sort_column,median_ms,max_ms");
		const bool benchmark = std::getenv("DCPP_PROFILE") != nullptr;
		for(size_t count: visiblePaint ? std::vector<size_t>{100, 1000} : benchmark ? std::vector<size_t>{100, 500, 1000} : std::vector<size_t>{16}) {
			for(size_t group: visiblePaint ? std::vector<size_t>{16} : std::vector<size_t>{1, 16, count}) {
				for(int sort: {0, 6}) TransferViewPerformanceTest::run(window, count, group, sort, visiblePaint);
			}
		}
		DestroyWindow(window->handle());
		ConnectionManager::deleteInstance();
		ShareManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		QueueManager::deleteInstance();
		UploadManager::deleteInstance();
		DownloadManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
