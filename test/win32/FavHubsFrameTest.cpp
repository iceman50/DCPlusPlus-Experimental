// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// The favorite hubs list must keep its scroll position and selection when hubs connect or
// disconnect, and when a refresh is needed (DC++ Experimental bug 2173949).
#include "stdafx.h"
#include <win32/SingleInstance.h>
#include <win32/util/UiServices.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/IconManager.h>
#define private public
#define protected public
#include <dcpp/hub/ClientManager.h>
#include <dcpp/hub/NmdcHub.h>
#include <win32/frames/favorites/FavHubsFrame.h>
#undef protected
#undef private
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/core/io/File.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dwt/Application.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Window.h>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 183;

namespace {

void check(bool value, const char* message) {
	if(!value) throw std::runtime_error(message);
}

string server(int i) {
	return "adc://favorite-" + Util::toString(i) + ".invalid:411";
}

struct ListState {
	int scroll;
	std::vector<LPARAM> selected;
	bool operator==(const ListState& o) const { return scroll == o.scroll && selected == o.selected; }
};

ListState state(TablePtr hubs) {
	ListState s { hubs->getScrollInfo(SB_VERT, SIF_POS).nPos, {} };
	for(auto i: hubs->getSelection())
		s.selected.push_back(hubs->getData(i));
	return s;
}

void pump(dwt::Application& app) {
	for(unsigned i = 0; i < 200; ++i)
		app.dispatch();
}

void verify(dwt::Window* window, dwt::Application& app) {
	auto tabs = window->addChild(dwt::TabView::Seed());
	tabs->resize(dwt::Rectangle(0, 0, 620, 300));
	FavHubsFrame::openWindow(tabs);
	auto frame = FavHubsFrame::frame;
	check(frame, "Favorite hubs window did not open");
	auto hubs = frame->hubs;
	pump(app);
	check(hubs->size() == 60, "Favorite hubs were not listed");

	auto entry = [](int i) { return reinterpret_cast<LPARAM>(FavoriteManager::getInstance()->getFavoriteHubEntry(server(i))); };
	auto row45 = hubs->findData(entry(45));
	hubs->select(row45);
	hubs->ensureVisible(row45);
	const auto before = state(hubs);
	check(before.scroll > 0, "The list did not scroll to the selected hub");
	check(before.selected == std::vector<LPARAM> { entry(45) }, "The hub was not selected");

	// A favorite hub opens; its status shows without moving the list.
	auto hub = new NmdcHub(server(50));
	{
		auto cm = ClientManager::getInstance();
		Lock l(cm->cs);
		cm->clients.insert(hub);
	}
	ClientManager::getInstance()->fire(ClientManagerListener::ClientConnected(), static_cast<Client*>(hub));
	pump(app);
	check(state(hubs) == before, "A hub connecting moved the list or lost the selection");
	check(hubs->getText(hubs->findData(entry(50)), FavHubsFrame::COLUMN_STATUS) == T_("Disconnected"),
		"The opened hub's status was not shown");
	check(hubs->getText(hubs->findData(entry(49)), FavHubsFrame::COLUMN_STATUS).empty(),
		"A hub that is not open shows a status");

	// It closes, or a reconnect attempt fails.
	{
		auto cm = ClientManager::getInstance();
		Lock l(cm->cs);
		cm->clients.erase(hub);
	}
	ClientManager::getInstance()->fire(ClientManagerListener::ClientDisconnected(), static_cast<Client*>(hub));
	pump(app);
	check(state(hubs) == before, "A hub disconnecting moved the list or lost the selection");
	check(hubs->getText(hubs->findData(entry(50)), FavHubsFrame::COLUMN_STATUS).empty(),
		"The closed hub's status was not cleared");
	delete hub;

	// Edited properties show in place.
	FavoriteManager::getInstance()->getFavoriteHubEntry(server(45))->setName("Renamed hub");
	frame->updateRow(hubs->findData(entry(45)));
	check(hubs->getText(hubs->findData(entry(45)), FavHubsFrame::COLUMN_NAME) == _T("Renamed hub"),
		"The edited hub was not updated");
	check(state(hubs) == before, "Updating a hub moved the list or lost the selection");

	// Refreshes that rebuild the list keep their place too.
	{
		FavHubsFrame::StateKeeper keeper(hubs, false);
		frame->refresh();
	}
	const auto after = state(hubs);
	check(after.selected == before.selected, "Rebuilding the list lost the selection");
	check(after.scroll > 0 && std::abs(after.scroll - before.scroll) < 32, "Rebuilding the list lost the scroll position");

	frame->close(true);
	pump(app);
	check(!FavHubsFrame::frame, "Favorite hubs window did not close");
}

}

int dwtMain(dwt::Application& app) {
	try {
		wchar_t cwd[32768];
		check(GetCurrentDirectoryW(std::size(cwd), cwd) != 0, "Cannot find test workspace");
		const auto directory = Text::fromT(cwd) + "\\build\\favhubs-frame-test\\";
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = directory;
		Util::initialize(paths);
		File::ensureDirectory(directory);
		File::deleteFile(directory + "Favorites.xml");
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		PluginManager::newInstance();
		ClientManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		IconManager::initialize();
		UiServices::initFont();
		ControlSeeds::init();

		FavHubGroups groups;
		groups["Group B"] = HubSettings();
		groups["Group C"] = HubSettings();
		FavoriteManager::getInstance()->setFavHubGroups(groups);
		for(int i = 0; i < 60; ++i) {
			FavoriteHubEntry e;
			e.setServer(server(i));
			e.setName("Hub " + Util::toString(i));
			e.setGroup(i < 20 ? "" : i < 40 ? "Group B" : "Group C");
			FavoriteManager::getInstance()->addFavorite(e);
		}

		auto window = new dwt::Window();
		dwt::Window::Seed seed;
		seed.caption = L"Favorite hubs test";
		seed.location = dwt::Rectangle(-32000, -32000, 620, 300);
		window->create(seed);
		verify(window, app);
		DestroyWindow(window->handle());

		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		ClientManager::deleteInstance();
		PluginManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("Favorite hubs keep their scroll position and selection across connects, disconnects, edits and refreshes.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
