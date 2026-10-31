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

#include "stdafx.h"

#include <win32/frames/chat/ChatCommands.h>
#include <dcpp/text/format.h>
#include <dcpp/util/Util.h>
#include <win32/app/UiServices.h>
#include <win32/shell/ShellActions.h>
#include <win32/forward.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/transfer/ThrottleManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/version.h>
#include <win32/frames/search/SearchFrame.h>
#include <win32/frames/tools/ACFrame.h>
#include <win32/app/MainWindow.h>

#include <cstdlib>

namespace ChatCommands {

namespace {

#define LINE2 _T("-- https://dcplusplus.sourceforge.io <DC++ ") _T(VERSIONSTRING) _T(">")
const TCHAR
	*msgs[] = {
		_T("\r\n-- I'm a happy DC++ user. You could be happy too.\r\n") LINE2,
		_T("\r\n-- Neo-...what? Nope...never heard of it...\r\n") LINE2,
		_T("\r\n-- Evolution of species: Ape --> Man\r\n-- Evolution of science: \"The Earth is Flat\" --> \"The Earth is Round\"\r\n-- Evolution of DC: NMDC --> ADC\r\n") LINE2,
		_T("\r\n-- I share, therefore I am.\r\n") LINE2,
		_T("\r\n-- I came, I searched, I found...\r\n") LINE2,
		_T("\r\n-- I came, I shared, I sent...\r\n") LINE2,
		_T("\r\n-- I can fully configure the toolbar, can you?\r\n") LINE2,
		_T("\r\n-- I don't have to see any ads, do you?\r\n") LINE2,
		_T("\r\n-- My client supports passive-passive downloads, does yours?\r\n") LINE2,
		_T("\r\n-- I can refresh my opened file lists, can you?\r\n") LINE2,
		_T("\r\n-- I can get help for every part of GUI and settings in my client, can you?\r\n") LINE2,
		_T("\r\n-- My client automatically detects the connection, does yours?\r\n") LINE2,
		_T("\r\n-- My client keeps track of all the recent opened windows, does yours?\r\n") LINE2,
		_T("\r\n-- These addies are pretty annoying, aren't they? Get revenge by sending them yourself!\r\n") LINE2,
		_T("\r\n-- My client supports taskbar thumbnails and Aero Peek previews, does yours?\r\n") LINE2,
		_T("\r\n-- My client supports secure communication and transfers, does yours?\r\n") LINE2,
		_T("\r\n-- My client supports grouping favorite hubs, does yours?\r\n") LINE2,
		_T("\r\n-- My client supports segmented downloading, does yours?\r\n") LINE2,
		_T("\r\n-- My client supports browsing file lists, does yours?\r\n") LINE2 };

#define MSGS 19

static const map<tstring, tstring> cmdMap = {
	{_T("/refresh"),								  T_("Manually refreshes DC++'s share list by going through the shared directories and adding new folders and files. DC++ automatically refreshes once an hour by default, and also refreshes after the list of shared directories is changed.")},
	{_T("/me"),										  T_("Speak as a third person.")},
	{_T("/slots #"),								  T_("Sets the current number of upload slots to the number you specify. If this is less than the current number of slots, no uploads are cancelled.")},
	{_T("/dslots #"),								  T_("Sets the current number of download slots to the number you specify. If this is less than the current number of slots, no downloads are cancelled.")},
	{_T("/search <string>"),						  T_("Opens a new search window with the specified search string. It does not automatically send the search.")},
	{_T("/clear [lines to keep]"),					  T_("Clears the current window of all text. Optionally, you can specify how many of the latest (most recent) lines should be kept.")},
	{_T("/dc++"),									  T_("Sends a random DC++ advertising message to the chat, including a link to the DC++ homepage and the version number.")},
	{_T("/away [message]"),							  T_("Sets Away status. New private message windows will be responded to, once, with the message you specified, or the default away message configured in the Personal information settings page.")},
	{_T("/back"),									  T_("Un-sets Away status.")},
	{_T("/d <search string>"),						  T_("Launches your default web browser to the DuckDuckGo search engine with the specified search.")},
	{_T("/g <search string>"),						  T_("Launches your default web browser to the Google search engine with the specified search.")},
	{_T("/imdb <imdb query>"),						  T_("Launches your default web browser to the Internet Movie Database (imdb) with the specified query.")},
	{_T("/rebuild"),								  T_("Rebuilds the hash database (HashStore.sqlite3), removing entries for files that are no longer shared, or old hashes for files that have since changed. This runs in the main DC++ thread, so the interface will freeze until the rebuild is finished.")},
	{_T("/log <status, system, downloads, uploads>"), T_("If no parameter is specified, it launches the log for the hub or private chat with the associated application in Windows. If one of the parameters is specified it opens that log file. The status log is available only in the hub frame.")},
	{_T("/help"),									  T_("Displays available commands. (The ones listed on this page.) Optionally, you can specify \"brief\" to have a brief listing.")},
	{_T("/u <url>"),								  T_("Launches your default web browser with the given URL.")},
	{_T("/f <search string>"),						  T_("Highlights the last occourrence of the specified search string in the chat window.")},
	{_T("/download #"),								  T_("Set the download speed limit in KiBs. Zero or omitted value disables the limit.")},
	{_T("/upload #""/upload #"),					  T_("Set the upload speed limit in KiBs. Zero or omitted value disables the limit.")},
	{_T("/close"),									  T_("Closes the current window.")},
	{_T("/about:config, /a:c, /ac"),				  T_("Opens the internal settings list debugging tool window.")}
};

} // namespace

tstring getDescriptiveCommands() {
	tstring ret;

	for(const auto& commands : cmdMap) {
		ret += _T("\r\n") + commands.first + _T("\r\n\t") + commands.second;
	}

	return ret;
}

tstring
	commands =
		_T("/refresh, /me <msg>, /slots #, /dslots #, /search <string>, /clear [lines to keep], /dc++, /away <msg>, /back, /d <searchstring>, /g <searchstring>, /imdb <imdbquery>, /rebuild, /log [type], /help [brief], /u <url>, /f <string>, /download [#], /upload [#], /close, /a[bout][:]c[onfig]");

bool checkCommand(tstring& cmd, tstring& param, tstring& message, tstring& status, bool& thirdPerson) {
	string::size_type i = cmd.find(' ');
	if(i != string::npos) {
		param = cmd.substr(i + 1);
		cmd = cmd.substr(1, i - 1);
	} else {
		cmd = cmd.substr(1);
	}

	if(Util::stricmp(cmd.c_str(), _T("log")) == 0) {
		if(Util::stricmp(param.c_str(), _T("system")) == 0) {
			ShellActions::openFile(Text::toT(Util::validateFileName(LogManager::getInstance()->getPath(LogManager::SYSTEM))));
		} else if(Util::stricmp(param.c_str(), _T("downloads")) == 0) {
			ShellActions::openFile(Text::toT(
				Util::validateFileName(LogManager::getInstance()->getPath(LogManager::DOWNLOAD))));
		} else if(Util::stricmp(param.c_str(), _T("uploads")) == 0) {
			ShellActions::openFile(Text::toT(Util::validateFileName(LogManager::getInstance()->getPath(LogManager::UPLOAD))));
		} else {
			return false;
		}
	} else if(Util::stricmp(cmd.c_str(), _T("me")) == 0) {
		message = param;
		thirdPerson = true;
	} else if(Util::stricmp(cmd.c_str(), _T("refresh")) == 0) {
		try {
			ShareManager::getInstance()->setDirty();
			ShareManager::getInstance()->refresh(true);
		} catch (const ShareException& e) {
			status = Text::toT(e.getError());
		}
	} else if(Util::stricmp(cmd.c_str(), _T("slots")) == 0) {
		int j = Util::toInt(Text::fromT(param));
		if(j > 0) {
			ThrottleManager::setSetting(ThrottleManager::getCurSetting(SettingsManager::SLOTS), j);
			status = T_("Slots set");
		} else {
			status = T_("Invalid number of slots");
		}
	} else if(Util::stricmp(cmd.c_str(), _T("dslots")) == 0) {
		int j = Util::toInt(Text::fromT(param));
		if(j >= 0) {
			SettingsManager::getInstance()->set(SettingsManager::DOWNLOAD_SLOTS, j);
			status = T_("Download slots set");
		} else {
			status = T_("Invalid number of slots");
		}
	} else if(Util::stricmp(cmd.c_str(), _T("search")) == 0) {
		if(!param.empty()) {
			SearchFrame::openWindow(UiServices::mainWindow->getTabView(), param, SearchManager::TYPE_ANY);
		} else {
			status = T_("Specify a search string");
		}
	} else if(Util::stricmp(cmd.c_str(), _T("dc++")) == 0) {
		message = msgs[GET_TICK() % MSGS];
	} else if(Util::stricmp(cmd.c_str(), _T("away")) == 0) {
		if(Util::getAway() && param.empty()) {
			Util::setAway(false);
			status = T_("Away mode off");
		} else {
			Util::setAway(true);
			Util::setAwayMessage(Text::fromT(param));
			auto awayMessage = Util::getAwayMessage();
			status = str(TF_("Away mode on: %1%") % (awayMessage.empty() ? T_("No away message") : Text::toT(awayMessage)));
		}
	} else if(Util::stricmp(cmd.c_str(), _T("back")) == 0) {
		Util::setAway(false);
		status = T_("Away mode off");
	} else if(Util::stricmp(cmd.c_str(), _T("d")) == 0) {
		if(param.empty()) {
			status = T_("Specify a search string");
		} else {
			ShellActions::openLink(_T("https://www.duckduckgo.com/?q=") + Text::toT(Util::encodeURI(Text::fromT(param))));
		}
	} else if(Util::stricmp(cmd.c_str(), _T("g")) == 0) {
		if(param.empty()) {
			status = T_("Specify a search string");
		} else {
			ShellActions::openLink(_T("https://www.google.com/search?q=") + Text::toT(Util::encodeURI(Text::fromT(param))));
		}
	} else if(Util::stricmp(cmd.c_str(), _T("imdb")) == 0) {
		if(param.empty()) {
			status = T_("Specify a search string");
		} else {
			ShellActions::openLink(_T("https://www.imdb.com/find?q=") + Text::toT(Util::encodeURI(Text::fromT(param))));
		}
	} else if(Util::stricmp(cmd.c_str(), _T("u")) == 0) {
		if(param.empty()) {
			status = T_("Specify a URL");
		} else {
			ShellActions::openLink(Text::toT(Util::encodeURI(Text::fromT(param))));
		}
	} else if(Util::stricmp(cmd.c_str(), _T("rebuild")) == 0) {
		HashManager::getInstance()->rebuild();
	} else if(Util::stricmp(cmd.c_str(), _T("upload")) == 0) {
		auto value = std::strtoll(Text::fromT(param).c_str(), nullptr, 10);
		if(value < 0 || value > ThrottleManager::MAX_LIMIT) {
			status = T_("Invalid speed value");
		} else {
			ThrottleManager::setSetting(ThrottleManager::getCurSetting(SettingsManager::MAX_UPLOAD_SPEED_MAIN), static_cast<int>(value));
			status = value ? str(TF_("Upload limit set to %1% KiB/s") % value) : T_("Upload limit disabled");
		}
	} else if(Util::stricmp(cmd.c_str(), _T("download")) == 0) {
		auto value = std::strtoll(Text::fromT(param).c_str(), nullptr, 10);
		if(value < 0 || value > ThrottleManager::MAX_LIMIT) {
			status = T_("Invalid speed value");
		} else {
			ThrottleManager::setSetting(ThrottleManager::getCurSetting(SettingsManager::MAX_DOWNLOAD_SPEED_MAIN), static_cast<int>(value));
			status = value ? str(TF_("Download limit set to %1% KiB/s") % value) : T_("Download limit disabled");
		}
	} else if(Util::stricmp(cmd.c_str(), _T("about:config")) == 0 || Util::stricmp(cmd.c_str(), _T("ac")) == 0 || Util::stricmp(cmd.c_str(), _T("a:c")) == 0) {
		ACFrame::openWindow(UiServices::mainWindow->getTabView());
	} else {
		return false;
	}

	return true;
}

} // namespace ChatCommands
