// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "testbase.h"

#include <chrono>
#include <filesystem>

#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/net/HttpManager.h>
#include <dcpp/system/File.h>

using namespace dcpp;

namespace {
class FavoriteTabTextTest : public ::testing::Test {
protected:
	string configPath;

	void SetUp() override {
		configPath = (std::filesystem::temp_directory_path() / ("dcpp-favorite-tab-" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string() + PATH_SEPARATOR;
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);
		SettingsManager::newInstance();
		TimerManager::newInstance();
		ClientManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
	}

	void TearDown() override {
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		ClientManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

	void reload() {
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		FavoriteManager::newInstance();
		FavoriteManager::getInstance()->load();
	}
};

TEST_F(FavoriteTabTextTest, PersistsUnicodeAndXmlCharactersAcrossRestartAndClear) {
	FavoriteHubEntry hub;
	hub.setName("Original favorite name");
	hub.setServer("adcs://example.invalid:411");
	hub.setFailoverServers(StringList { "adcs://backup.example.invalid:411" });
	const string label = u8"Movies & <Friends> \"\u00e9\u2605\"";
	hub.setTabText(label);
	FavoriteManager::getInstance()->addFavorite(hub);
	reload();
	auto loaded = FavoriteManager::getInstance()->getFavoriteHubEntry(hub.getServer());
	ASSERT_NE(nullptr, loaded);
	EXPECT_EQ(label, loaded->getTabText());
	EXPECT_EQ(hub.getName(), loaded->getName());
	EXPECT_EQ(loaded, FavoriteManager::getInstance()->getFavoriteHubEntry(hub.getFailoverServers().front()));
	loaded->setTabText("");
	FavoriteManager::getInstance()->save();
	reload();
	loaded = FavoriteManager::getInstance()->getFavoriteHubEntry(hub.getServer());
	ASSERT_NE(nullptr, loaded);
	EXPECT_TRUE(loaded->getTabText().empty());
}

TEST_F(FavoriteTabTextTest, OldFavoritesWithoutTabTextKeepDefault) {
	File file(configPath + "Favorites.xml", File::WRITE, File::CREATE | File::TRUNCATE);
	file.write("<Favorites><Hubs><Hub Name=\"Legacy hub\" Server=\"dchub://legacy.example.invalid:411\"/></Hubs></Favorites>");
	file.close();
	reload();
	auto loaded = FavoriteManager::getInstance()->getFavoriteHubEntry("dchub://legacy.example.invalid:411");
	ASSERT_NE(nullptr, loaded);
	EXPECT_TRUE(loaded->getTabText().empty());
	EXPECT_EQ("Legacy hub", loaded->getName());
}
} // namespace
