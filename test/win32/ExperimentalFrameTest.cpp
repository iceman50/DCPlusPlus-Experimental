// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// The Experimental window holds the theme, icon and emoticon packages and every option DC++
// Experimental adds, on tabs laid out in two columns that scroll on their own. It applies each
// change at once: check boxes and lists when they change, number and text fields after a pause in
// typing, on Enter or when they lose the focus - never a half-typed number. Settings keeps a page
// that opens it, and only the options of DC++ itself.
#include "stdafx.h"
#include <win32/SingleInstance.h>
#include <win32/util/UiServices.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/GeoIPUpdate.h>
#include <win32/util/IconManager.h>
#include <dcpp/core/crypto/PasswordHash.h>
#include <dcpp/user/UserMatchManager.h>
#include <win32/resource.h>
#define private public
#define protected public
#include <win32/frames/experimental/ExperimentalFrame.h>
#include <win32/frames/experimental/ExperimentalTabs.h>
#undef protected
#undef private
#include <win32/dialog/ParamDlg.h>
#include <win32/settings/ExperimentalLinkPage.h>
#include <win32/settings/base/SettingsDialog.h>
#include <dcpp/core/io/File.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/core/timer/TimerManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dwt/Application.h>
#include <dwt/Appearance.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/ScrolledContainer.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/Tree.h>
#include <dwt/widgets/Window.h>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 190;

namespace {

void check(bool value, const char* message) {
	if(!value) throw std::runtime_error(message);
}

/** Run the message loop, timers included, until `done` or the time runs out. */
bool waitFor(dwt::Application& app, const std::function<bool ()>& done, unsigned milliseconds = 4000) {
	const auto end = GetTickCount64() + milliseconds;
	while(!done()) {
		if(GetTickCount64() > end) return false;
		// Sleep only when the queue is empty: asynchronous calls run then, and timers keep the
		// queue from ever being empty right after a sleep.
		if(!app.dispatch()) Sleep(5);
	}
	return true;
}

/** Whether the message queue empties: a window that keeps repainting itself never lets it. */
bool idle(dwt::Application& app) {
	for(unsigned i = 0; i < 20000; ++i) {
		if(!app.dispatch()) return true;
	}
	return false;
}

/** Run the message loop for a while, timers included. */
void wait(dwt::Application& app, unsigned milliseconds) {
	waitFor(app, [] { return false; }, milliseconds);
}

std::vector<HWND> children(HWND parent) {
	std::vector<HWND> result;
	EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
		reinterpret_cast<std::vector<HWND>*>(data)->push_back(child);
		return TRUE;
	}, reinterpret_cast<LPARAM>(&result));
	return result;
}

std::wstring text(HWND window) {
	wchar_t buffer[512] {};
	GetWindowTextW(window, buffer, static_cast<int>(std::size(buffer)));
	return buffer;
}

HWND findCaption(HWND parent, const std::wstring& caption) {
	for(auto child: children(parent)) {
		if(text(child) == caption) return child;
	}
	return nullptr;
}

ExperimentalTab* tabOf(HWND child) {
	for(; child; child = GetParent(child)) {
		if(auto tab = dynamic_cast<ExperimentalTab*>(dwt::hwnd_cast<dwt::Widget*>(child))) return tab;
	}
	return nullptr;
}

/** Show the tab holding `child`, as a user would before using it. */
void showTab(ExperimentalFrame* frame, HWND child) {
	auto tab = tabOf(child);
	check(tab != nullptr, "An option is not on a tab");
	frame->tabs->setActive(tab);
}

/** The number field after a label, on its tab. */
HWND field(ExperimentalFrame* frame, const std::wstring& label) {
	auto caption = findCaption(frame->handle(), label);
	if(!caption) throw std::runtime_error("Missing option: " + Text::fromT(label));
	auto edit = GetWindow(caption, GW_HWNDNEXT);
	check(dwt::hwnd_cast<dwt::TextBox*>(edit) != nullptr, "An option label is not followed by its field");
	showTab(frame, edit);
	return edit;
}

void type(HWND edit, const std::wstring& value) {
	SetWindowTextW(edit, value.c_str());
}

void enter(HWND edit) {
	SendMessage(edit, WM_KEYDOWN, VK_RETURN, 0);
}

void leave(HWND edit) {
	SendMessage(edit, WM_KILLFOCUS, 0, 0);
}

/** Choose a drop-down entry as a user would, so that it notifies its parent. */
void choose(dwt::ComboBox* combo, int index) {
	combo->setSelected(index);
	SendMessage(GetParent(combo->handle()), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo->handle()), CBN_SELENDOK),
		reinterpret_cast<LPARAM>(combo->handle()));
}

void click(HWND button) {
	SendMessage(button, BM_CLICK, 0, 0);
}

ExperimentalFrame* open(dwt::Window* window, dwt::Application& app, int width, int height) {
	auto tabs = window->addChild(dwt::TabView::Seed());
	tabs->resize(dwt::Rectangle(0, 0, width, height));
	ExperimentalFrame::openWindow(tabs);
	auto frame = ExperimentalFrame::frame;
	check(frame != nullptr, "The Experimental window did not open");
	// Tabs create their controls when first shown; these tests use them all.
	for(auto page: frame->pages) {
		page->ensureBuilt();
	}
	check(idle(app), "The Experimental window keeps the message queue busy after opening");
	return frame;
}

void close(ExperimentalFrame* frame, dwt::Application& app) {
	frame->close(true);
	check(waitFor(app, [] { return !ExperimentalFrame::frame; }), "The Experimental window did not close");
}

/** Save a window's look to a .bmp file, for reviewing the layout. */
void snapshot(HWND window, const string& path) {
	RECT rect;
	GetWindowRect(window, &rect);
	const auto width = rect.right - rect.left, height = rect.bottom - rect.top;
	auto dc = GetDC(window);
	auto memory = CreateCompatibleDC(dc);
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	void* pixels = nullptr;
	auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
	check(bitmap != nullptr, "Cannot capture the Experimental window");
	auto previous = SelectObject(memory, bitmap);
	SendMessage(window, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
	BITMAPFILEHEADER header {};
	header.bfType = 0x4D42;
	header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
	header.bfSize = header.bfOffBits + width * height * 4;
	File output(path, File::WRITE, File::CREATE | File::TRUNCATE);
	output.write(&header, sizeof(header));
	output.write(&info.bmiHeader, sizeof(info.bmiHeader));
	output.write(pixels, width * height * 4);
	SelectObject(memory, previous);
	DeleteObject(bitmap);
	DeleteDC(memory);
	ReleaseDC(window, dc);
}

/** With DCPP_EXPERIMENTAL_SNAPSHOTS set, save each tab's look, for reviewing the layout. */
void snapshots(dwt::Window* window, dwt::Application& app, const string& directory) {
	if(!GetEnvironmentVariableW(L"DCPP_EXPERIMENTAL_SNAPSHOTS", nullptr, 0)) {
		return;
	}
	auto frame = open(window, app, 1600, 900);
	for(size_t i = 0; i < frame->pages.size(); ++i) {
		frame->tabs->setActive(frame->pages[i]);
		idle(app);
		snapshot(frame->handle(), directory + "tab-" + std::to_string(i) + ".bmp");
	}
	close(frame, app);

	// And in a light custom palette (Paper's), where a control painting the wrong one of
	// background and surface stands out.
	auto& appearance = dwt::Application::instance().getAppearance();
	const auto mode = appearance.getMode();
	const auto palette = appearance.getPalette();
	const auto rendering = appearance.getRendering();
	appearance.configure(dwt::Appearance::Mode::Light, { RGB(0xFA, 0xFA, 0xF7), RGB(0xF0, 0xF1, 0xEC),
		RGB(0x27, 0x31, 0x3A), RGB(0x62, 0x6C, 0x72), RGB(0xB5, 0xBD, 0xBB), RGB(0x31, 0x5E, 0x9C),
		RGB(0xFF, 0xFF, 0xFF) }, dwt::Appearance::Rendering::CustomPalette);
	frame = open(window, app, 1600, 900);
	for(size_t i = 0; i < frame->pages.size(); ++i) {
		frame->tabs->setActive(frame->pages[i]);
		idle(app);
		snapshot(frame->handle(), directory + "paper-tab-" + std::to_string(i) + ".bmp");
	}
	close(frame, app);
	appearance.configure(mode, palette, rendering);
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

/** The window opens at once, each tab is quick to show, and showing the window again is cheap. */
void loadsQuickly(dwt::Window* window, dwt::Application& app) {
	auto tabs = window->addChild(dwt::TabView::Seed());
	tabs->resize(dwt::Rectangle(0, 0, 1600, 900));
	auto start = std::chrono::steady_clock::now();
	ExperimentalFrame::openWindow(tabs);
	const auto created = millisecondsSince(start);
	// First, each tab built on demand, as when it is clicked before the background build reached it.
	ExperimentalFrame::frame->setTimer(std::function<bool ()>(), 0, ExperimentalFrame::TIMER_PREBUILD);
	check(idle(app), "The Experimental window keeps the message queue busy after opening");
	const auto opened = millisecondsSince(start);
	auto frame = ExperimentalFrame::frame;
	size_t built = 0;
	for(auto page: frame->pages) {
		built += page->isBuilt() ? 1 : 0;
	}
	check(built == 1, "The Experimental window built more than the tab in front");
	std::printf("Experimental window: created in %.1f ms, settled in %.1f ms\n", created, opened);

	double slowest = 0;
	for(size_t i = 1; i < frame->pages.size(); ++i) {
		start = std::chrono::steady_clock::now();
		frame->tabs->setActive(frame->pages[i]);
		idle(app);
		const auto shown = millisecondsSince(start);
		slowest = std::max(slowest, shown);
		std::printf("  %s tab first shown in %.1f ms\n", Text::fromT(frame->pages[i]->getText()).c_str(), shown);
		// Built as it was shown, it is laid out too.
		size_t groups = 0;
		for(auto child: children(frame->pages[i]->handle())) {
			if(auto group = dwt::hwnd_cast<dwt::GroupBox*>(child)) {
				const auto size = group->getWindowSize();
				check(size.x > 0 && size.y > 0, "A tab built as it was shown is not laid out");
				++groups;
			}
		}
		check(groups > 0, "A tab was not built when it was shown");
	}
	double again = 0;
	for(size_t i = 0; i < frame->pages.size(); ++i) {
		start = std::chrono::steady_clock::now();
		frame->tabs->setActive(frame->pages[i]);
		idle(app);
		again = std::max(again, millisecondsSince(start));
	}
	start = std::chrono::steady_clock::now();
	frame->setVisible(false);
	frame->setVisible(true);
	idle(app);
	const auto reshown = millisecondsSince(start);
	std::printf("  slowest tab shown again in %.1f ms; window shown again in %.1f ms\n", again, reshown);

	// Generous bounds that still catch blocking work (disk, packages) on the way to the screen.
	check(opened < 400, "The Experimental window takes too long to open");
	check(slowest < 400, "A tab of the Experimental window takes too long to show");
	check(again < 100 && reshown < 150, "The Experimental window is slow to show again");
	close(frame, app);

	// Left open, the window builds its other tabs while idle, so the first switch to each is quick.
	tabs = window->addChild(dwt::TabView::Seed());
	tabs->resize(dwt::Rectangle(0, 0, 1600, 900));
	ExperimentalFrame::openWindow(tabs);
	frame = ExperimentalFrame::frame;
	check(waitFor(app, [frame] {
		for(auto page: frame->pages) {
			if(!page->isBuilt()) return false;
		}
		return true;
	}, 5000), "The Experimental window did not build its other tabs while idle");
	idle(app);
	double prebuilt = 0;
	for(size_t i = 1; i < frame->pages.size(); ++i) {
		start = std::chrono::steady_clock::now();
		frame->tabs->setActive(frame->pages[i]);
		idle(app);
		prebuilt = std::max(prebuilt, millisecondsSince(start));
	}
	std::printf("  slowest first switch to a tab built in the background: %.1f ms\n", prebuilt);
	check(prebuilt < 150, "A tab built in the background is slow to show");
	close(frame, app);
}

void layout(dwt::Window* window, dwt::Application& app) {
	auto frame = open(window, app, 1800, 1400);
	check(frame->pages.size() == ExperimentalFrame::PAGE_LAST && frame->tabs->size() == ExperimentalFrame::PAGE_LAST,
		"The Experimental window does not have its eight tabs");

	// Every group is placed (a Grid leaves children past its last row unplaced) and scrolls with
	// its tab, and in a wide window each tab's groups share the width in two columns.
	size_t groups = 0;
	std::map<ExperimentalTab*, std::vector<RECT>> placed;
	for(auto child: children(frame->handle())) {
		auto group = dwt::hwnd_cast<dwt::GroupBox*>(child);
		if(!group) continue;
		showTab(frame, child);
		if(!idle(app)) throw std::runtime_error("A tab keeps the message queue busy: " + Text::fromT(tabOf(child)->getText()));
		bool scrolled = false;
		for(HWND parent = GetParent(child); parent && parent != frame->tabs->handle(); parent = GetParent(parent)) {
			scrolled = scrolled || dwt::hwnd_cast<dwt::ScrolledContainer*>(parent) != nullptr;
		}
		if(!scrolled) throw std::runtime_error("An Experimental group cannot scroll: " + Text::fromT(group->getText()));
		const auto size = group->getWindowSize();
		if(size.x <= 0 || size.y <= 0) throw std::runtime_error("An Experimental group is not laid out: " + Text::fromT(group->getText()));
		RECT rect;
		GetWindowRect(child, &rect);
		placed[tabOf(child)].push_back(rect);
		++groups;
	}
	check(groups == 24, "The Experimental groups were not all found");
	for(const auto& tab: placed) {
		bool sideBySide = false;
		for(const auto& a: tab.second) {
			for(const auto& b: tab.second) {
				sideBySide = sideBySide || a.left >= b.right;
			}
		}
		if(!sideBySide) throw std::runtime_error("A tab's groups are not in two columns in a wide window: " + Text::fromT(tab.first->getText()));
	}
	close(frame, app);
}

void smallWindowScrolls(dwt::Window* window, dwt::Application& app) {
	auto frame = open(window, app, 640, 300);
	auto first = findCaption(frame->handle(), L"Keep messages for offline users and send them later");
	auto last = findCaption(frame->handle(), L"Ask for the lock password before exiting");
	check(first && last, "The Other tab's first and last options were not found");
	showTab(frame, first);

	// Too narrow for two columns: the groups are stacked, the left column's first.
	std::vector<RECT> stacked;
	for(auto child: children(tabOf(first)->handle())) {
		if(!dwt::hwnd_cast<dwt::GroupBox*>(child)) continue;
		RECT rect;
		GetWindowRect(child, &rect);
		stacked.push_back(rect);
	}
	check(stacked.size() == 5, "The Other tab's groups were not found");
	for(const auto& rect: stacked) {
		check(rect.left == stacked.front().left, "A narrow tab did not stack its groups in one column");
	}

	dwt::ScrolledContainer* scroller = nullptr;
	for(HWND parent = first; parent && !scroller; parent = GetParent(parent)) {
		scroller = dwt::hwnd_cast<dwt::ScrolledContainer*>(parent);
	}
	check(scroller != nullptr, "The Other tab has no scroll container");

	SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
	GetScrollInfo(scroller->handle(), SB_VERT, &info);
	check(info.nMax - info.nMin + 1 > static_cast<int>(info.nPage) && info.nPos == 0,
		"The Other tab does not scroll in a small window");

	// Wheel input over an option is passed on to the scroller.
	SendMessage(first, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
	GetScrollInfo(scroller->handle(), SB_VERT, &info);
	check(info.nPos > 0, "The mouse wheel did not scroll the Other tab");

	for(int i = 0; i < 100; ++i) SendMessage(first, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
	RECT visible, bottom;
	GetWindowRect(scroller->handle(), &visible);
	GetWindowRect(last, &bottom);
	check(bottom.top >= visible.top && bottom.bottom <= visible.bottom, "The last option cannot be scrolled into view");

	for(int i = 0; i < 100; ++i) SendMessage(first, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
	GetScrollInfo(scroller->handle(), SB_VERT, &info);
	check(info.nPos == 0, "The mouse wheel did not scroll the Other tab back to the top");
	close(frame, app);
}

/** On a number field without a spinner, a number out of range waits until the field is left: it
 * may still be being typed ("1" on the way to "1024"). Leaving applies the nearest value. */
void numberOutOfRangeWaits(dwt::Window* window, dwt::Application& app) {
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::MAX_SUDP_PACKET, 4096);
	std::vector<int> changes;
	LiveSettings live([&](int setting) { changes.push_back(setting); });
	auto box = window->addChild(ControlSeeds::Dialog::intTextBox);
	live.addInt(box, SettingsManager::MAX_SUDP_PACKET, 32, 65536);
	check(text(box->handle()) == L"4096", "A number field did not show its setting");

	type(box->handle(), L"1");
	wait(app, LiveSettings::typingPause * 2);
	check(settings->get(SettingsManager::MAX_SUDP_PACKET) == 4096 && changes.empty(),
		"A number below its range applied while it could still be typed");
	type(box->handle(), L"1024");
	check(waitFor(app, [&] { return settings->get(SettingsManager::MAX_SUDP_PACKET) == 1024; }),
		"A number in range did not apply after typing paused");
	check(changes == std::vector<int> { SettingsManager::MAX_SUDP_PACKET }, "A change was not reported once");

	type(box->handle(), L"1");
	leave(box->handle());
	check(settings->get(SettingsManager::MAX_SUDP_PACKET) == 32 && text(box->handle()) == L"32",
		"Leaving a field did not apply and show the lowest value in range");
	type(box->handle(), L"999999");
	enter(box->handle());
	check(settings->get(SettingsManager::MAX_SUDP_PACKET) == 65536 && text(box->handle()) == L"65536",
		"Enter did not apply and show the highest value in range");
	DestroyWindow(box->handle());
	settings->unset(SettingsManager::MAX_SUDP_PACKET);
}

void changesApplyAtOnce(dwt::Window* window, dwt::Application& app, const string& directory) {
	auto settings = SettingsManager::getInstance();
	auto& appearance = dwt::Application::instance().getAppearance();
	settings->set(SettingsManager::MAX_HASH_SPEED, 0);
	settings->set(SettingsManager::MAX_HASH_VOLUMES, 2);
	settings->set(SettingsManager::HASHING_THREADS, 2);
	settings->set(SettingsManager::THEMED_DIALOG_BORDERS, true);
	appearance.setDialogBorders(true);
	File::deleteFile(directory + "DCPlusPlus.xml");

	auto frame = open(window, app, 900, 900);

	// Check boxes apply when clicked.
	{
		auto zstd = findCaption(frame->handle(), L"Prefer Zstandard over zlib when the peer supports both");
		check(zstd != nullptr, "The Zstandard option is missing");
		showTab(frame, zstd);
		const auto before = settings->get(SettingsManager::PREFER_ZSTD);
		click(zstd);
		check(settings->get(SettingsManager::PREFER_ZSTD) == !before, "A check box did not apply at once");
		click(zstd);
		check(settings->get(SettingsManager::PREFER_ZSTD) == before, "A check box did not apply at once");

		// An open dialog loses its border at once.
		ParamDlg dialog(window, L"Dialog border preview");
		dialog.addTextBox(L"Content", L"Preview");
		dialog.create(dwt::ModalDialog::Seed(dwt::Point(365, 0)));
		auto borders = findCaption(frame->handle(), L"Show borders around themed dialogs");
		showTab(frame, borders);
		click(borders);
		check(!settings->get(SettingsManager::THEMED_DIALOG_BORDERS) && !appearance.getDialogBorders(),
			"The dialog border option did not apply at once");
		check(dialog.getDialogBorderWidth() == 0, "The dialog border option did not update an open dialog");
		DestroyWindow(dialog.handle());
	}

	// A number applies once typing pauses.
	auto speed = field(frame, L"Maximum hash speed");
	type(speed, L"5");
	check(settings->get(SettingsManager::MAX_HASH_SPEED) == 0, "A number applied before typing paused");
	check(waitFor(app, [&] { return settings->get(SettingsManager::MAX_HASH_SPEED) == 5; }),
		"A number did not apply after typing paused");

	// A field's spinner keeps a typed number within its range (as everywhere in DC++); the
	// nearest value in range applies after the pause.
	auto volumes = field(frame, L"Drives hashed at the same time");
	type(volumes, L"40");
	check(text(volumes) == std::to_wstring(SettingsManager::MAX_HASH_VOLUMES_MAX),
		"A field's spinner did not keep a typed number in range");
	check(waitFor(app, [&] { return settings->get(SettingsManager::MAX_HASH_VOLUMES) == SettingsManager::MAX_HASH_VOLUMES_MAX; }),
		"The nearest number in range did not apply");

	// Enter applies at once.
	auto threads = field(frame, L"Hashing worker threads");
	type(threads, L"3");
	enter(threads);
	check(settings->get(SettingsManager::HASHING_THREADS) == 3, "Enter did not apply a number");

	// Text that isn't a number changes nothing and shows the stored value again.
	type(speed, L"abc");
	leave(speed);
	check(settings->get(SettingsManager::MAX_HASH_SPEED) == 5 && text(speed) == L"5",
		"A field that isn't a number changed the setting or kept its text");

	// A field shown in KiB stores bytes; leaving it unedited keeps an exact byte value.
	settings->set(SettingsManager::RICH_TEXT_MAX_SIZE, 1000);
	ExperimentalFrame::settingsChanged();
	auto richText = field(frame, L"Rich text message size limit");
	check(text(richText) == L"1", "A byte value is not shown in whole KiB, rounded up");
	leave(richText);
	check(settings->get(SettingsManager::RICH_TEXT_MAX_SIZE) == 1000, "Leaving an unedited field changed its value");
	type(richText, L"32");
	enter(richText);
	check(settings->get(SettingsManager::RICH_TEXT_MAX_SIZE) == 32 * 1024, "A KiB field did not store bytes");

	// Drop-downs apply when chosen.
	{
		dwt::ComboBox* encoding = nullptr;
		for(auto child: children(frame->handle())) {
			auto combo = dwt::hwnd_cast<dwt::ComboBox*>(child);
			if(combo && combo->getAccessibleName() == T_("Default encoding for NMDC hubs")) encoding = combo;
		}
		check(encoding && encoding->getSelected() == 0, "The NMDC encoding does not default to the Windows code page");
		showTab(frame, encoding->handle());
		int utf8 = -1;
		for(int i = 0; i < static_cast<int>(encoding->size()); ++i) {
			if(encoding->getValue(i).compare(0, 6, _T("65001 ")) == 0) utf8 = i;
		}
		check(utf8 > 0, "The NMDC encoding list has no UTF-8 entry");
		choose(encoding, utf8);
		check(Text::getCodePage(settings->get(SettingsManager::NMDC_ENCODING)) == CP_UTF8, "The NMDC encoding did not apply at once");
		choose(encoding, 0);
		check(settings->get(SettingsManager::NMDC_ENCODING).empty(), "The Windows default NMDC encoding was not stored as empty");
	}

	// The longest reconnect delay follows the first one up.
	{
		auto base = field(frame, L"Initial automatic reconnect delay");
		auto longest = field(frame, L"Maximum automatic reconnect delay");
		type(base, L"120");
		enter(base);
		type(longest, L"60");
		enter(longest);
		check(settings->get(SettingsManager::CCPM_RECONNECT_MAX_DELAY) == 120 && text(longest) == L"120",
			"The longest reconnect delay can be shorter than the first");
	}

	// Text fields apply after a pause too; the license key is stored encrypted.
	{
		HWND key = nullptr;
		for(auto child: children(frame->handle())) {
			if(dwt::hwnd_cast<dwt::TextBox*>(child) && SendMessage(child, EM_GETPASSWORDCHAR, 0, 0)) key = child;
		}
		check(key != nullptr, "The MaxMind license key field is missing or unmasked");
		showTab(frame, key);
		type(key, L"test-license-key");
		check(waitFor(app, [&] { return GeoIPUpdate::unprotectKey(SETTING(MAXMIND_LICENSE_KEY)) == "test-license-key"; }),
			"The license key did not apply after typing paused");
		check(SETTING(MAXMIND_LICENSE_KEY) != "test-license-key", "The license key was stored unencrypted");
	}

	// Changes made elsewhere show, except in a field still being edited.
	settings->set(SettingsManager::MAX_HASH_SPEED, 7);
	ExperimentalFrame::settingsChanged();
	check(text(speed) == L"7", "A setting changed elsewhere did not show");
	type(speed, L"9");
	settings->set(SettingsManager::MAX_HASH_SPEED, 8);
	ExperimentalFrame::settingsChanged();
	check(text(speed) == L"9", "Showing settings changed elsewhere overwrote a field being edited");
	enter(speed);
	check(settings->get(SettingsManager::MAX_HASH_SPEED) == 9, "The edited field did not apply");

	// Changes are saved shortly after, without an OK button.
	check(waitFor(app, [&] { return File::getSize(directory + "DCPlusPlus.xml") > 0; },
		ExperimentalFrame::savePause + 3000), "Changes were not saved");

	// Closing the window applies an edit still waiting for its typing pause, and keeps changes.
	type(speed, L"11");
	close(frame, app);
	check(settings->get(SettingsManager::MAX_HASH_SPEED) == 11, "Closing the window lost an edit");
	check(!settings->get(SettingsManager::THEMED_DIALOG_BORDERS) && !appearance.getDialogBorders(),
		"Closing the window undid a change");

	settings->set(SettingsManager::THEMED_DIALOG_BORDERS, true);
	appearance.setDialogBorders(true);
	settings->unset(SettingsManager::MAXMIND_LICENSE_KEY);
}

void movedOptionsApplyAtOnce(dwt::Window* window, dwt::Application& app) {
	auto settings = SettingsManager::getInstance();
	auto frame = open(window, app, 1800, 1000);

	// File lists: the download limit and the XML limit, both in MiB.
	auto download = field(frame, L"Largest file list to download");
	type(download, L"100");
	enter(download);
	check(settings->get(SettingsManager::MAX_FILELIST_DOWNLOAD_SIZE) == 100, "The file list download limit did not apply");
	auto xml = field(frame, L"Largest file list to open (XML)");
	type(xml, L"300");
	enter(xml);
	check(settings->get(SettingsManager::MAX_FILELIST_SIZE) == 300, "The file list XML limit did not apply");

	// Check boxes moved from Settings > Advanced and from the Message outbox dialog.
	for(const auto& option: { std::make_pair(L"Report each hashed file in the system log", SettingsManager::LOG_HASHED_FILES),
		std::make_pair(L"Keep messages for offline users and send them later", SettingsManager::ENABLE_MESSAGE_OUTBOX),
		std::make_pair(L"Enable emoticons in chat", SettingsManager::ENABLE_EMOTICONS) })
	{
		auto box = findCaption(frame->handle(), option.first);
		if(!box) throw std::runtime_error("Missing option: " + Text::fromT(option.first));
		showTab(frame, box);
		const auto before = settings->get(option.second);
		click(box);
		if(settings->get(option.second) == before) throw std::runtime_error("An option did not apply at once: " + Text::fromT(option.first));
		click(box);
	}

	// The country database URL, moved from Settings > Appearance.
	auto url = field(frame, L"Download URL");
	type(url, L"https://example.invalid/country.mmdb");
	check(waitFor(app, [&] { return SETTING(GEOIP_DOWNLOAD_URL) == "https://example.invalid/country.mmdb"; }),
		"The country database URL did not apply after typing paused");

	// Emoticon display size.
	{
		dwt::ComboBox* size = nullptr;
		for(auto child: children(frame->handle())) {
			auto combo = dwt::hwnd_cast<dwt::ComboBox*>(child);
			if(combo && combo->getAccessibleName() == T_("Emoticon display size")) size = combo;
		}
		check(size != nullptr, "The emoticon display size is missing");
		showTab(frame, size->handle());
		choose(size, 3);
		check(settings->get(SettingsManager::EMOTICON_SIZE) == 32, "The emoticon display size did not apply at once");
	}

	// The password lock, moved from Settings > Appearance > Windows: its options need a password.
	{
		settings->set(SettingsManager::LOCK_PASSWORD, string());
		settings->set(SettingsManager::LOCK_ON_MINIMIZE, false);
		ExperimentalFrame::settingsChanged();
		auto minimize = findCaption(frame->handle(), L"Lock DC++ when it is minimized");
		check(minimize && !IsWindowEnabled(minimize), "The lock options are offered without a lock password");
		settings->set(SettingsManager::LOCK_PASSWORD, PasswordHash::create("test password", PasswordHash::minIterations));
		ExperimentalFrame::settingsChanged();
		check(IsWindowEnabled(minimize), "The lock options are not offered with a lock password");
		showTab(frame, minimize);
		click(minimize);
		check(settings->get(SettingsManager::LOCK_ON_MINIMIZE), "A lock option did not apply at once");
		// A damaged hash can't be checked, so it counts as no password and the options turn off.
		settings->set(SettingsManager::LOCK_PASSWORD, string("damaged"));
		ExperimentalFrame::settingsChanged();
		check(!settings->get(SettingsManager::LOCK_ON_MINIMIZE) && !IsWindowEnabled(minimize) &&
			SETTING(LOCK_PASSWORD).empty(), "A damaged lock password left the lock options on");
	}

	// Icon packages: selecting one previews it; Use this package switches to it.
	{
		auto icons = frame->pages[ExperimentalFrame::PAGE_ICONS];
		dwt::Table* list = nullptr;
		for(auto child: children(icons->handle())) {
			if(!list) list = dwt::hwnd_cast<dwt::Table*>(child);
		}
		auto use = findCaption(icons->handle(), L"&Use this package");
		check(list && use && list->size() >= 2, "The icon package list is missing");
		frame->tabs->setActive(icons);
		check(list->getText(1, 0).find(T_("Embedded")) == 0, "The built-in icons are not listed second");
		list->setSelected(1);
		check(IsWindowEnabled(use), "The icon package in use can be used again");
		click(use);
		check(SETTING(ICON_PACK) == IconManager::EMBEDDED_PACK, "Use this package did not switch the icon package");
		check(list->getText(1, 0).find(T_(" (in use)")) != tstring::npos, "The icon package in use is not marked");
		list->setSelected(0);
		click(use);
		check(SETTING(ICON_PACK).empty(), "Use this package did not switch back to automatic icons");
	}

	close(frame, app);
	for(auto setting: { SettingsManager::MAX_FILELIST_DOWNLOAD_SIZE, SettingsManager::MAX_FILELIST_SIZE,
		SettingsManager::EMOTICON_SIZE })
	{
		settings->unset(setting);
	}
	settings->unset(SettingsManager::GEOIP_DOWNLOAD_URL);
}

void settingsOpensTheWindow(dwt::Window* window) {
	auto settings = SettingsManager::getInstance();
	// Open Settings on its Experimental page, the only page then created.
	settings->set(SettingsManager::SETTINGS_PAGE, 22);
	SettingsDialog dialog(window);
	dialog.create(dwt::ModalDialog::Seed(dwt::Point(650, 420), WS_SIZEBOX));
	// The Icons and Themes pages moved to the Experimental window.
	dwt::Tree* navigation = nullptr;
	for(auto child: children(dialog.handle())) {
		if(!navigation) navigation = dwt::hwnd_cast<dwt::Tree*>(child);
	}
	check(navigation && TreeView_GetCount(navigation->treeHandle()) == 24, "Settings does not have its 24 pages");
	check(navigation->getText(navigation->getSelected()) == T_("Experimental"), "Settings did not open on its Experimental page");
	auto open = findCaption(dialog.handle(), L"&Open the Experimental window");
	check(open != nullptr, "Settings has no way to open the Experimental window");
	check(!findCaption(dialog.handle(), L"Prefer Zstandard over zlib when the peer supports both"),
		"Settings still holds the experimental options");
	check(!dialog.experimentalWindowRequested(), "Settings asked for the Experimental window by itself");
	click(open);
	check(dialog.experimentalWindowRequested(), "The Settings button did not ask for the Experimental window");
	DestroyWindow(dialog.handle());
}

}

int dwtMain(dwt::Application& app) {
	try {
		wchar_t cwd[32768];
		check(GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd) != 0, "Cannot find test workspace");
		const auto directory = Text::fromT(cwd) + "\\build\\experimental-frame-test\\";
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = directory;
		Util::initialize(paths);
		File::ensureDirectory(directory);
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		ClientManager::newInstance();
		UserMatchManager::newInstance();
		HashManager::newInstance();
		SearchManager::newInstance();
		UploadManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
		IconManager::initialize();
		UiServices::initFont();
		ControlSeeds::init();

		auto window = new dwt::Window();
		dwt::Window::Seed seed;
		seed.caption = L"Experimental window test";
		seed.location = dwt::Rectangle(-32000, -32000, 1800, 1400);
		window->create(seed);
		loadsQuickly(window, app);
		layout(window, app);
		snapshots(window, app, directory);
		smallWindowScrolls(window, app);
		numberOutOfRangeWaits(window, app);
		changesApplyAtOnce(window, app, directory);
		movedOptionsApplyAtOnce(window, app);
		settingsOpensTheWindow(window);
		DestroyWindow(window->handle());

		ShareManager::deleteInstance();
		QueueManager::deleteInstance();
		UploadManager::deleteInstance();
		SearchManager::deleteInstance();
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		UserMatchManager::deleteInstance();
		ClientManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("PASS: the Experimental window lays out its eight tabs in two columns (one when narrow) that scroll, "
			"applies changes at once without half-typed numbers, saves them, and Settings opens it.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
