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
#include <win32/settings/ExpertsPage.h>
#include <dcpp/user/CID.h>
#include <atomic>
#include <thread>
#include <win32/util/ThemeManager.h>
#include <dcpp/user/UserMatchManager.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Spinner.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/ScrolledContainer.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/core/classes/Magnet.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/connection/ThrottleManager.h>
#include <dcpp/core/io/File.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
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

// The dialog border option itself is tested with the Experimental window (ExperimentalFrameTest).
void dialogBorderPreference() {
	auto& appearance = dwt::Application::instance().getAppearance();
	auto settings = SettingsManager::getInstance();
	appearance.configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
	check(settings->get(SettingsManager::THEMED_DIALOG_BORDERS), "Dialog borders were not enabled by default");
	settings->set(SettingsManager::THEMED_DIALOG_BORDERS, false);
	UserMatchManager::newInstance();
	auto theme = ThemeManager::capture();
	ThemeManager::apply(theme, false);
	check(!settings->get(SettingsManager::THEMED_DIALOG_BORDERS), "Applying a theme changed the border preference");
	UserMatchManager::deleteInstance();
	settings->set(SettingsManager::THEMED_DIALOG_BORDERS, true);
	appearance.setDialogBorders(true);
}

/** Answer the next "Generate new private ID" warning from another thread, as a user would. */
std::thread answerPrivateIdWarning(int button, std::atomic<bool>& answered) {
	const auto processId = GetCurrentProcessId();
	return std::thread([button, processId, &answered] {
		for(int attempt = 0; attempt < 500 && !answered; ++attempt) {
			std::pair<DWORD, HWND> search(processId, nullptr);
			EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
				auto& found = *reinterpret_cast<std::pair<DWORD, HWND>*>(data);
				DWORD owner = 0;
				GetWindowThreadProcessId(hwnd, &owner);
				wchar_t title[128] = {};
				GetWindowTextW(hwnd, title, 128);
				if(owner == found.first && std::wstring(title) == L"Generate new private ID") {
					found.second = hwnd;
					return FALSE;
				}
				return TRUE;
			}, reinterpret_cast<LPARAM>(&search));
			if(search.second) {
				PostMessage(search.second, WM_COMMAND, button, 0);
				answered = true;
				return;
			}
			Sleep(10);
		}
	});
}

void privateIdCanBeRegenerated() {
	auto settings = SettingsManager::getInstance();
	const auto original = settings->get(SettingsManager::PRIVATE_ID);
	auto window = new dwt::Window();
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	{
		auto page = new ExpertsPage(window);
		page->resize(dwt::Rectangle(0, 0, 640, 600));
		page->layout();
		std::vector<HWND> children;
		EnumChildWindows(page->handle(), collectLimitControls, reinterpret_cast<LPARAM>(&children));
		dwt::Button* generate = nullptr;
		dwt::TextBox* pid = nullptr;
		for(auto child: children) {
			auto button = dwt::hwnd_cast<dwt::Button*>(child);
			if(button && button->getText() == T_("&Generate new...")) generate = button;
		}
		check(generate != nullptr, "The private ID has no Generate new button");
		for(auto child: children) {
			auto box = dwt::hwnd_cast<dwt::TextBox*>(child);
			if(box && GetParent(box->handle()) == GetParent(generate->handle())) pid = box;
		}
		check(pid && Text::fromT(pid->getText()) == original, "The private ID field does not show the saved private ID");

		// Answering No keeps the current private ID.
		std::atomic<bool> answered { false };
		auto no = answerPrivateIdWarning(IDNO, answered);
		generate->sendMessage(BM_CLICK);
		no.join();
		check(answered && Text::fromT(pid->getText()) == original, "Declining the warning changed the private ID");

		// Answering Yes fills in a new, valid private ID, saved only with OK.
		answered = false;
		auto yes = answerPrivateIdWarning(IDYES, answered);
		generate->sendMessage(BM_CLICK);
		yes.join();
		const auto generated = Text::fromT(pid->getText());
		check(answered && generated != original && generated.size() == 39 && CID(generated),
			"Confirming the warning did not generate a new private ID");
		check(settings->get(SettingsManager::PRIVATE_ID) == original, "The new private ID was saved before OK");
		page->write();
		check(settings->get(SettingsManager::PRIVATE_ID) == generated, "OK did not save the new private ID");
		settings->set(SettingsManager::PRIVATE_ID, original);
		DestroyWindow(page->handle());
	}
	DestroyWindow(window->handle());
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
	check(LinkActions::makeMagnet(TTHValue(hash), "empty", 0).find("&xl=0") != std::string::npos,
		"Empty-file magnet lost its explicit zero size");
	check(LinkActions::checkTTH(dwt::tstring(39, L'A')) && !LinkActions::checkTTH(dwt::tstring(39, L'0')) &&
		!LinkActions::checkTTH(dwt::tstring(38, L'A')), "TTH validation changed");
	check(!LinkActions::parseLink(L"https://example.invalid/", false), "Disabled external links were handled");
}
}

int dwtMain(dwt::Application&) {
	try {
		wchar_t cwd[32768];
		check(GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd) != 0, "Cannot find workspace");
		const auto directory = Text::fromT(cwd) + "\\build\\ui-helpers-test\\";
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = directory;
		Util::initialize(paths);
		File::ensureDirectory(directory);
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		TimerManager::newInstance();
		ClientManager::newInstance();
		tableSettings();
		chatAndLinks();
		manualLimitControls();
		dialogBorderPreference();
		privateIdCanBeRegenerated();
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
