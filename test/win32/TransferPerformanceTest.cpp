// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"
#include <dwt/widgets/TableTree.h>
#include <win32/app/SingleInstance.h>
#include <win32/app/UiServices.h>
#include <win32/appearance/IconManager.h>
#include <win32/appearance/ImageLists.h>
#include <win32/controls/TypedTable.h>
#include <win32/frames/users/AspectUserCommand.h>
#include <win32/frames/users/UserInfoBase.h>
#include <dcpp/app/ResourceManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/net/HttpManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/system/File.h>
#include <dcpp/transfer/DownloadManager.h>
#include <dcpp/transfer/UploadManager.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Container.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

// Keep test access in this translation unit, following the core test pattern.
#define private public
#include <win32/frames/transfers/TransferView.h>
#undef private

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 181;

class TransferViewPerformanceTest {
	static void check(bool value, const char* message) {
		if(!value) throw std::runtime_error(message);
	}
public:
	static void run(dwt::Window* window, size_t count, size_t perGroup, int sortColumn) {
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
		std::vector<double> timings;
		for(int round = 0; round < 9; ++round) {
			for(size_t i = 0; i < count; ++i) {
				auto update = std::make_unique<TransferView::UpdateInfo>(peer, CONNECTION_TYPE_UPLOAD, std::to_string(i));
				update->setTransferred((round + 1) * 4096, (round + 1) * 4096, 1024 * 1024 * 1024);
				update->setSpeed(1000000 + round);
				view->resortMask |= update->updateMask;
				view->tasks.emplace_back([view](const TransferView::UpdateInfo& value) { view->updateConn(value); }, std::move(update));
			}
			const auto start = Clock::now();
			view->execTasks();
			const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
			if(round) timings.push_back(ms);
			for(const auto& transfer: view->transferItems) {
				check(transfer.speed == static_cast<int64_t>(transfer.conns.size()) * (1000000 + round), "Incorrect aggregate speed");
				check(transfer.transferred == static_cast<int64_t>(transfer.conns.size()) * (round + 1) * 4096, "Incorrect aggregate bytes");
				check(!transfer.columns[TransferView::COLUMN_SPEED].empty(), "Missing aggregate speed text");
			}
		}
		std::sort(timings.begin(), timings.end());
		std::printf("ui,%zu,%zu,%d,%.3f,%.3f\n", count, perGroup, sortColumn,
			(timings[3] + timings[4]) / 2, timings.back());
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
		UiServices::initFont();
		ImageLists::fileImages = dwt::ImageListPtr(new dwt::ImageList(dwt::Point(16, 16)));
		auto window = new dwt::Window;
		dwt::Window::Seed seed;
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		std::puts("area,connections,connections_per_group,sort_column,median_ms,max_ms");
		const bool benchmark = std::getenv("DCPP_PROFILE") != nullptr;
		for(size_t count: benchmark ? std::vector<size_t>{100, 500, 1000} : std::vector<size_t>{16}) {
			for(size_t group: {size_t(1), size_t(16), count}) {
				for(int sort: {0, 6}) TransferViewPerformanceTest::run(window, count, group, sort);
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
