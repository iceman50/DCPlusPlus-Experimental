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
#include <dcpp/DCPlusPlus.h>

#include <dcpp/modules/ADLSearch.h>
#include <dcpp/bbs/BBSManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/connectivity/ConnectivityManager.h>
#include <dcpp/core/crypto/CryptoManager.h>
#include <dcpp/transfer/download/DownloadManager.h>
#include <dcpp/transfer/private/DirectTransferManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/modules/FinishedManager.h>
#include <dcpp/core/geo/GeoManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/connectivity/MappingManager.h>
#include <dcpp/plugins/PluginApiImpl.h>
#include <dcpp/private_chat/PrivateChatManager.h>
#include <dcpp/private_chat/MessageOutboxManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/connection/ThrottleManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/user/UserMatchManager.h>
#include <dcpp/recents/WindowManager.h>
#include <cmath>

extern "C" int _nl_msg_cat_cntr;

namespace dcpp {

void startup() {
	// "Dedicated to the near-memory of Nev. Let's start remembering people while they're still alive."
	// Nev's great contribution to dc++
	while(1) break;


#ifdef _WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	bindtextdomain(PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(PACKAGE, "UTF-8");

	ResourceManager::newInstance();
	SettingsManager::newInstance();

	LogManager::newInstance();
	TimerManager::newInstance();
	HashManager::newInstance();
	CryptoManager::newInstance();
	SearchManager::newInstance();
	ClientManager::newInstance();
	ConnectionManager::newInstance();
	PrivateChatManager::newInstance();
	MessageOutboxManager::newInstance();
	DownloadManager::newInstance();
	UploadManager::newInstance();
	ThrottleManager::newInstance();
	QueueManager::newInstance();
	ShareManager::newInstance();
	DirectTransferManager::newInstance();
	BBSManager::newInstance();
	HttpManager::newInstance();
	FavoriteManager::newInstance();
	FinishedManager::newInstance();
	ADLSearchManager::newInstance();
	ConnectivityManager::newInstance();
	MappingManager::newInstance();
	GeoManager::newInstance();
	UserMatchManager::newInstance();
	WindowManager::newInstance();
	PluginManager::newInstance();
	PluginApiImpl::init();
}

void load(function<void (const string&)> stepF, function<void (float)> progressF) {
	SettingsManager::getInstance()->load();
	DirectTransferManager::getInstance()->configure();
	if(!pft::purgeUnfinished())
		LogManager::getInstance()->message(
			"Some expired Private Send partial files could not be removed", LogMessage::SEV_WARNING, "Private Send");

#ifdef _WIN32
	if(!SETTING(LANGUAGE).empty()) {
		string language = "LANGUAGE=" + SETTING(LANGUAGE);
		putenv(language.c_str());

		// Apparently this is supposted to make gettext reload the message catalog...
		_nl_msg_cat_cntr++;
	}
#endif

	auto announce = [&stepF](const string& str) {
		if(stepF) {
			stepF(str);
		}
	};
	// Give each loading stage a fixed part of the bar. This measures completed
	// work, not elapsed time; stages without estimates hold their position.
	const bool loadCountries = SETTING(GET_USER_COUNTRY);
	const float stageCount = loadCountries ? 7.0f : 6.0f;
	unsigned completedStages = 0;
	float reportedProgress = 0;
	auto reportProgress = [&](float value) {
		if(!std::isfinite(value)) return;
		reportedProgress = std::max(reportedProgress,
			(completedStages + std::clamp(value, 0.0f, 1.0f)) / stageCount);
		if(progressF) progressF(reportedProgress);
	};
	auto completeStage = [&] {
		reportProgress(1);
		++completedStages;
	};
	reportProgress(0);

	announce(_("Users"));
	ClientManager::getInstance()->loadUsers();
	FavoriteManager::getInstance()->load();
	completeStage();

	PluginManager::getInstance()->loadPlugins(stepF);
	completeStage();

	announce(_("Security certificates"));
	CryptoManager::getInstance()->loadCertificates();
	completeStage();

	announce(_("Hash database"));
	HashManager::getInstance()->startup(reportProgress);
	completeStage();

	announce(_("Shared Files"));
	ShareManager::getInstance()->startupRefresh(reportProgress);
	completeStage();

	announce(_("Download Queue"));
	QueueManager::getInstance()->loadQueue(reportProgress);
	completeStage();

	if(loadCountries) {
		announce(_("Country information"));
		GeoManager::getInstance()->init();
		completeStage();
	}
	MessageOutboxManager::getInstance()->start();
}

void shutdown() {
	MessageOutboxManager::deleteInstance();
	DirectTransferManager::getInstance()->shutdown();
	PluginManager::getInstance()->unloadPlugins();
	TimerManager::getInstance()->shutdown();
	HashManager::getInstance()->shutdown();
	ThrottleManager::getInstance()->shutdown();
	PrivateChatManager::deleteInstance();
	ConnectionManager::getInstance()->shutdown();
	HttpManager::getInstance()->shutdown();
	MappingManager::getInstance()->close();
	GeoManager::getInstance()->close();
	// UI teardown normally releases hubs. Drain any survivors before waiting for
	// their self-owned BufferedSocket workers, otherwise shutdown can wait forever.
	ClientManager::getInstance()->shutdown();
	BufferedSocket::waitShutdown();
	DirectTransferManager::deleteInstance();
	FavoriteManager::getInstance()->shutdown();

	WindowManager::getInstance()->prepareSave();
	QueueManager::getInstance()->saveQueue(true);
	ClientManager::getInstance()->saveUsers();
	SettingsManager::getInstance()->save();

	PluginApiImpl::shutdown();
	PluginManager::deleteInstance();
	WindowManager::deleteInstance();
	UserMatchManager::deleteInstance();
	GeoManager::deleteInstance();
	MappingManager::deleteInstance();
	ConnectivityManager::deleteInstance();
	ADLSearchManager::deleteInstance();
	FinishedManager::deleteInstance();
	HttpManager::deleteInstance();
	BBSManager::deleteInstance();
	ShareManager::deleteInstance();
	CryptoManager::deleteInstance();
	ThrottleManager::deleteInstance();
	DownloadManager::deleteInstance();
	UploadManager::deleteInstance();
	QueueManager::deleteInstance();
	ConnectionManager::deleteInstance();
	SearchManager::deleteInstance();
	FavoriteManager::deleteInstance();
	ClientManager::deleteInstance();
	HashManager::deleteInstance();
	LogManager::deleteInstance();
	SettingsManager::deleteInstance();
	TimerManager::deleteInstance();
	ResourceManager::deleteInstance();

#ifdef _WIN32
	::WSACleanup();
#endif
}

} // namespace dcpp
