// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"

#include <win32/app/SingleInstance.h>
#include <win32/appearance/ColorUtil.h>
#include <win32/controls/TableLayout.h>
#include <win32/frames/chat/ChatCommands.h>
#include <win32/frames/chat/ChatText.h>
#include <win32/services/LinkActions.h>
#include <dcpp/app/ResourceManager.h>
#include <dcpp/search/Magnet.h>
#include <dcpp/settings/SettingsManager.h>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 180;

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void tableSettings() {
	const std::vector<int> columns { 3, 0, 2, 1 };
	check(TableLayout::toString(columns) == "3,0,2,1", "Column persistence format changed");
	std::vector<int> restored(4);
	TableLayout::toInts(TableLayout::toString(columns), restored);
	check(restored == columns, "Column order did not round trip");
	TableLayout::toInts("1,2", restored);
	check(restored == std::vector<int>({ 1, 2, 2, 1 }), "Partial column settings lost defaults");
	TableLayout::toInts("4,3,2,1,0", restored);
	check(restored == std::vector<int>({ 4, 3, 2, 1 }), "Extra column settings were not ignored");
	check(TableLayout::toString({}).empty(), "Empty columns did not serialize to empty text");

	auto settings = SettingsManager::getInstance();
	for(int column = -1; column < 4; ++column) {
		for(bool ascending : { false, true }) {
			settings->set(SettingsManager::HUBFRAME_SORT, TableLayout::tableSortSetting(column, ascending));
			const auto sort = TableLayout::tableSortSetting(4, SettingsManager::HUBFRAME_SORT, 0, true);
			check(sort.first == column && sort.second == (column == -1 || ascending), "Saved sort order changed");
		}
	}
	for(int stale : { 8, -10 }) {
		settings->set(SettingsManager::HUBFRAME_SORT, stale);
		check(TableLayout::tableSortSetting(4, SettingsManager::HUBFRAME_SORT, 1, false) ==
			std::make_pair(1, false), "Stale sort column did not fall back to default");
	}
}

void chatAndLinks() {
	for(const auto* limitCommand: { L"/upload ", L"/download " }) {
		for(const auto* invalidLimit: { L"-1", L"2097152", L"4294967296", L"999999999999999999999999999999" }) {
			dwt::tstring command = dwt::tstring(limitCommand) + invalidLimit;
			dwt::tstring parameter, message, status;
			bool thirdPerson = false;
			check(ChatCommands::checkCommand(command, parameter, message, status, thirdPerson), "Limit command was not recognized");
			check(status == T_("Invalid speed value"), "Out-of-range limit command was not rejected");
		}
	}
	dwt::tstring command = L"/me waves hello", parameter, message, status;
	bool thirdPerson = false;
	check(ChatCommands::checkCommand(command, parameter, message, status, thirdPerson), "Action command was not recognized");
	check(command == L"me" && parameter == L"waves hello" && message == parameter && thirdPerson,
		"Action command parsing changed");
	check(ChatCommands::commands.find(L"/help [brief]") != dwt::tstring::npos &&
		ChatCommands::getDescriptiveCommands().find(L"/help") != dwt::tstring::npos, "Command help list changed");
	std::string path = "Downloaded <C:\\share\\folder\\file.txt>";
	ChatText::reducePaths(path);
	check(path == "Downloaded ...\\folder\\file.txt", "Chat path shortening changed");

	const std::string hash(39, 'A');
	const auto magnet = LinkActions::makeMagnet(TTHValue(hash), "a file & name.txt", 123);
	std::string parsedHash, parsedName, key;
	check(Magnet::parseUri(magnet, parsedHash, parsedName, key) && parsedHash == hash && parsedName == "a file & name.txt",
		"Generated magnet did not preserve hash and escaped filename");
	check(magnet.find("&xl=123") != std::string::npos, "Generated magnet lost file size");
	check(LinkActions::makeMagnet(TTHValue(hash), "empty", 0).find("&xl=") == std::string::npos,
		"Zero size magnet format changed");
	check(LinkActions::checkTTH(dwt::tstring(39, L'A')) && !LinkActions::checkTTH(dwt::tstring(39, L'0')) &&
		!LinkActions::checkTTH(dwt::tstring(38, L'A')), "TTH validation changed");
	check(!LinkActions::parseLink(L"https://example.invalid/", false), "Disabled external links were handled");
}
}

int dwtMain(dwt::Application&) {
	try {
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		tableSettings();
		chatAndLinks();
		check(ColorUtil::HLS2RGB(ColorUtil::RGB2HLS(RGB(0, 0, 0))) == RGB(0, 0, 0), "Black color conversion changed");
		check(ColorUtil::HLS2RGB(ColorUtil::RGB2HLS(RGB(255, 255, 255))) == RGB(255, 255, 255), "White color conversion changed");
		check(ColorUtil::modRed(RGB(250, 100, 100), 20) == RGB(255, 85, 85), "Color overflow adjustment changed");
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("UI table settings, chat commands, magnets and color helpers passed.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
