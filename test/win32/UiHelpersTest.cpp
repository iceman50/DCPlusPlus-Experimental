// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"

#include <win32/SingleInstance.h>
#include <win32/util/UiServices.h>
#include <win32/util/ColorUtil.h>
#include <win32/components/TableLayout.h>
#include <win32/ChatCommands.h>
#include <win32/ChatText.h>
#include <win32/util/LinkActions.h>
#include <win32/dialog/ParamDlg.h>
#include <win32/settings/BandwidthLimitPage.h>
#include <win32/settings/AppearancePage.h>
#include <win32/util/ThemeManager.h>
#include <dcpp/user/UserMatchManager.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Spinner.h>
#include <dwt/widgets/CheckBox.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/core/classes/Magnet.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/connection/ThrottleManager.h>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 180;

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

BOOL CALLBACK collectLimitControls(HWND hwnd, LPARAM data) {
	reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd);
	return TRUE;
}

void manualLimitControls() {
	SettingsManager::getInstance()->setDefault(SettingsManager::MAIN_FONT, "Segoe UI,-12,400,0,1");
	UiServices::initFont();
	auto window = new dwt::Window();
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	{
		ParamDlg dialog(window, L"Download limit");
		dialog.addIntTextBox(L"New limit (KiB/s) (0 = infinite)", L"0", 0, ThrottleManager::MAX_LIMIT);
		// Create the real controls without showing a modal window or touching a running client.
		dialog.create(dwt::ModalDialog::Seed(dwt::Point(365, 0)));
		std::vector<HWND> children;
		EnumChildWindows(dialog.handle(), collectLimitControls, reinterpret_cast<LPARAM>(&children));
		HWND spinner = nullptr, ok = nullptr;
		for(auto child: children) {
			if(dwt::hwnd_cast<dwt::Spinner*>(child)) spinner = child;
			if(GetDlgCtrlID(child) == IDOK) ok = child;
		}
		check(spinner && ok, "Manual limit dialog controls were not created");
		int lower = -1, upper = -1;
		SendMessage(spinner, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&lower), reinterpret_cast<LPARAM>(&upper));
		check(lower == 0 && upper == 16777216, "Manual limit dialog still uses the old cap");
		auto edit = reinterpret_cast<HWND>(SendMessage(spinner, UDM_GETBUDDY, 0, 0));
		for(int value: { 1048577, 2097152, 16777217, 16777216 }) {
			SetWindowTextW(edit, std::to_wstring(value).c_str());
			check(SendMessage(spinner, UDM_GETPOS32, 0, 0) == std::min(value, 16777216),
				"Manual input was clamped to the wrong limit");
		}
		SendMessage(ok, BM_CLICK, 0, 0);
		check(dialog.getValue() == L"16777216", "Manual limit dialog did not return the full 16 GiB/s value");
		DestroyWindow(dialog.handle());
	}
	{
		auto page = new BandwidthLimitPage(window);
		std::vector<HWND> children;
		EnumChildWindows(page->handle(), collectLimitControls, reinterpret_cast<LPARAM>(&children));
		int limits = 0;
		for(auto child: children) {
			if(!dwt::hwnd_cast<dwt::Spinner*>(child)) continue;
			int lower = -1, upper = -1;
			SendMessage(child, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&lower), reinterpret_cast<LPARAM>(&upper));
			if(upper == 16777216) {
				++limits;
				auto edit = reinterpret_cast<HWND>(SendMessage(child, UDM_GETBUDDY, 0, 0));
				SetWindowTextW(edit, L"16777216");
			}
		}
		check(limits == 4, "Bandwidth settings did not expose four 16 GiB/s controls");
		page->write();
		for(auto key: { SettingsManager::MAX_DOWNLOAD_SPEED_MAIN, SettingsManager::MAX_UPLOAD_SPEED_MAIN,
			SettingsManager::MAX_DOWNLOAD_SPEED_ALTERNATE, SettingsManager::MAX_UPLOAD_SPEED_ALTERNATE }) {
			check(SettingsManager::getInstance()->get(key) == 16777216, "Bandwidth settings lost a manually entered limit");
		}
	}
	DestroyWindow(window->handle());
}

void dialogBorderPreference() {
	auto& appearance = dwt::Application::instance().getAppearance();
	auto settings = SettingsManager::getInstance();
	appearance.configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
	check(settings->get(SettingsManager::THEMED_DIALOG_BORDERS), "Dialog borders were not enabled by default");
	auto window = new dwt::Window();
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	{
		ParamDlg dialog(window, L"Dialog border preview");
		dialog.addTextBox(L"Content", L"Preview");
		dialog.create(dwt::ModalDialog::Seed(dwt::Point(365, 0)));
		for(bool save: { false, true }) {
			auto page = new AppearancePage(window);
			page->resize(dwt::Rectangle(0, 0, 640, 600));
			page->layout();
			std::vector<HWND> children;
			EnumChildWindows(page->handle(), collectLimitControls, reinterpret_cast<LPARAM>(&children));
			dwt::CheckBox* option = nullptr;
			for(auto child: children) {
				auto box = dwt::hwnd_cast<dwt::CheckBox*>(child);
				if(box && box->getText() == T_("Show borders around themed dialogs")) option = box;
			}
			check(option && option->getChecked(), "Appearance did not expose the saved border preference");
			option->sendMessage(BM_CLICK);
			check(!appearance.getDialogBorders() && dialog.getDialogBorderWidth() == 0, "Border checkbox did not update an open dialog");
			check(settings->get(SettingsManager::THEMED_DIALOG_BORDERS), "Border preview prematurely saved the setting");
			if(save) page->write();
			DestroyWindow(page->handle());
			check(appearance.getDialogBorders() == !save && settings->get(SettingsManager::THEMED_DIALOG_BORDERS) == !save,
				"Closing Appearance did not respect OK/Cancel");
		}
		UserMatchManager::newInstance();
		auto theme = ThemeManager::capture();
		ThemeManager::apply(theme, false);
		check(!settings->get(SettingsManager::THEMED_DIALOG_BORDERS), "Applying a theme changed the border preference");
		UserMatchManager::deleteInstance();
		DestroyWindow(dialog.handle());
	}
	DestroyWindow(window->handle());
	settings->set(SettingsManager::THEMED_DIALOG_BORDERS, true);
	appearance.setDialogBorders(true);
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
		for(const auto* invalidLimit: { L"-1", L"16777217", L"4294967296", L"999999999999999999999999999999" }) {
			dwt::tstring command = dwt::tstring(limitCommand) + invalidLimit;
			dwt::tstring parameter, message, status;
			bool thirdPerson = false;
			check(ChatCommands::checkCommand(command, parameter, message, status, thirdPerson), "Limit command was not recognized");
			check(status == T_("Invalid speed value"), "Out-of-range limit command was not rejected");
		}
		for(int rate: { 2097152, 4194304, 16777216, 0 }) {
			dwt::tstring command = dwt::tstring(limitCommand) + std::to_wstring(rate);
			dwt::tstring parameter, message, status;
			bool thirdPerson = false;
			check(ChatCommands::checkCommand(command, parameter, message, status, thirdPerson), "Limit command was not recognized");
			check(status != T_("Invalid speed value"), "Supported high limit was rejected");
			check((command == L"upload" ? ThrottleManager::getUpLimit() : ThrottleManager::getDownLimit()) == rate,
				"High limit command did not preserve the requested rate");
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
		TimerManager::newInstance();
		ClientManager::newInstance();
		tableSettings();
		chatAndLinks();
		manualLimitControls();
		dialogBorderPreference();
		check(ColorUtil::HLS2RGB(ColorUtil::RGB2HLS(RGB(0, 0, 0))) == RGB(0, 0, 0), "Black color conversion changed");
		check(ColorUtil::HLS2RGB(ColorUtil::RGB2HLS(RGB(255, 255, 255))) == RGB(255, 255, 255), "White color conversion changed");
		check(ColorUtil::modRed(RGB(250, 100, 100), 20) == RGB(255, 85, 85), "Color overflow adjustment changed");
		ClientManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("UI table settings, chat commands, manual 16 GiB/s limit controls, magnets and color helpers passed.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
