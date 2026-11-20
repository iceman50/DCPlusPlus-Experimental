// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// Opens the real settings dialog in the dark (custom-drawn) appearance with share
// folders configured and checks on screen what a page switch paints, since a page
// switch paints the page host composited and discards what that paint invalidates:
// - the Sharing page's rows and column header, when switched to and when the dialog
//   opens on it (rows were blank, then headers were blank);
// - every page's own background on its first visit (pages with a report-view table
//   kept the light default colour until visited a second time).
#include "stdafx.h"
#include <win32/SingleInstance.h>
#include <win32/util/UiServices.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/IconManager.h>
#include <win32/settings/base/PropPage.h>
#include <win32/settings/base/SettingsDialog.h>
#include <dcpp/DCPlusPlus.h>
#include <dcpp/core/io/File.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/share/ShareManager.h>
#include <dwt/Application.h>
#include <dwt/Appearance.h>
#include <dwt/widgets/Window.h>
#include <commctrl.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <thread>
#include <vector>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 188;

namespace {

using Clock = std::chrono::steady_clock;
constexpr UINT WM_TICK = WM_APP + 1;
constexpr auto SETTLE = std::chrono::milliseconds(700);
constexpr auto TIMEOUT = std::chrono::seconds(60);

int failures = 0;
std::atomic<bool> tickPending { false };

void fail(const std::string& message) {
	std::fprintf(stderr, "FAIL: %s\n", message.c_str());
	std::fflush(stderr);
	++failures;
}

std::wstring className(HWND window) {
	wchar_t name[96] {};
	GetClassNameW(window, name, 96);
	return name;
}

// dwt subclasses common controls under its own class names, such as N3dwt5TableE.
bool isTable(HWND window) {
	const auto name = className(window);
	return name == WC_LISTVIEWW || name.find(L"dwt5Table") != std::wstring::npos;
}

/* Distinct colours in a screen rectangle. A painted row shows its text over the
background (several colours); an unpainted one is a single flat colour. */
size_t screenColours(const RECT& area) {
	const int width = area.right - area.left, height = area.bottom - area.top;
	if(width <= 0 || height <= 0) return 0;
	HDC screen = GetDC(nullptr);
	HDC memory = CreateCompatibleDC(screen);
	BITMAPINFO info {};
	info.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB };
	void* bits = nullptr;
	HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	auto old = SelectObject(memory, bitmap);
	BitBlt(memory, 0, 0, width, height, screen, area.left, area.top, SRCCOPY);
	GdiFlush();
	std::set<uint32_t> colours;
	auto pixels = static_cast<const uint32_t*>(bits);
	for(int i = 0; i < width * height; ++i) colours.insert(pixels[i] & 0xFFFFFF);
	SelectObject(memory, old);
	DeleteObject(bitmap);
	DeleteDC(memory);
	ReleaseDC(nullptr, screen);
	return colours.size();
}

struct Driver {
	enum class Phase { Opening, Switched, Captured, Done } phase = Phase::Opening;
	Clock::time_point started = Clock::now(), phaseStart;
	HWND dialog = nullptr, tree = nullptr, list = nullptr;
	size_t before = 0, headerBefore = 0;
	// The second dialog opens on the page the first one closed on.
	const bool opensOnSharing;
	const char* const scenario;

	explicit Driver(bool opensOnSharing) : opensOnSharing(opensOnSharing),
		scenario(opensOnSharing ? "opening the dialog on the Sharing page" : "switching to the Sharing page") { }

	void enter(Phase next) { phase = next; phaseStart = Clock::now(); }
	bool elapsed(Clock::duration d) const { return Clock::now() - phaseStart >= d; }

	HTREEITEM findItem(HTREEITEM item, const std::wstring& title) const {
		for(; item; item = TreeView_GetNextSibling(tree, item)) {
			wchar_t text[128] {};
			TVITEMW info { TVIF_TEXT, item };
			info.pszText = text;
			info.cchTextMax = 128;
			TreeView_GetItem(tree, &info);
			if(title == text) return item;
			if(auto found = findItem(TreeView_GetChild(tree, item), title)) return found;
		}
		return nullptr;
	}

	// Screen rectangle of the first row's cells.
	RECT firstRow() const {
		RECT row {};
		ListView_GetItemRect(list, 0, &row, LVIR_BOUNDS);
		MapWindowPoints(list, nullptr, reinterpret_cast<POINT*>(&row), 2);
		return row;
	}

	// Screen rectangle of the column header.
	RECT header() const {
		RECT area {};
		if(auto window = ListView_GetHeader(list)) GetWindowRect(window, &area);
		return area;
	}

	void findList() {
		list = nullptr;
		EnumChildWindows(dialog, [](HWND window, LPARAM data) -> BOOL {
			if(isTable(window) && IsWindowVisible(window) && ListView_GetItemCount(window) >= 2) {
				*reinterpret_cast<HWND*>(data) = window;
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&list));
	}

	void tick() {
		if(Clock::now() - started > TIMEOUT) {
			fail("timed out");
			finish();
			return;
		}
		switch(phase) {
		case Phase::Opening: {
			EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
				if(IsWindowVisible(window) && className(window).find(L"ModalDialog") != std::wstring::npos) {
					*reinterpret_cast<HWND*>(data) = window;
					return FALSE;
				}
				return TRUE;
			}, reinterpret_cast<LPARAM>(&dialog));
			if(!dialog) return;
			// Keep the dialog in front so the screen shows what it painted.
			SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			EnumChildWindows(dialog, [](HWND window, LPARAM data) -> BOOL {
				if(className(window).find(L"TreeView") != std::wstring::npos) {
					*reinterpret_cast<HWND*>(data) = window;
					return FALSE;
				}
				return TRUE;
			}, reinterpret_cast<LPARAM>(&tree));
			auto sharing = tree ? findItem(TreeView_GetRoot(tree), L"Sharing") : nullptr;
			if(!sharing) {
				fail("Sharing page not found in the settings tree");
				finish();
				return;
			}
			if(opensOnSharing) {
				if(TreeView_GetSelection(tree) != sharing) {
					fail("the reopened dialog did not open on the Sharing page");
					finish();
					return;
				}
			} else {
				TreeView_SelectItem(tree, sharing);
			}
			enter(Phase::Switched);
			return;
		}
		case Phase::Switched:
			if(!elapsed(SETTLE)) return;
			findList();
			if(!list) {
				fail("Sharing page has no visible table holding the share folders");
				finish();
				return;
			}
			before = screenColours(firstRow());
			headerBefore = screenColours(header());
			enter(Phase::Captured);
			// Force the table and its header to paint themselves, as hovering or scrolling would.
			RedrawWindow(list, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
			return;
		case Phase::Captured: {
			if(!elapsed(std::chrono::milliseconds(300))) return;
			const auto after = screenColours(firstRow());
			const auto headerAfter = screenColours(header());
			std::printf("%s: first share row shows %zu colours (%zu after a forced repaint), header %zu (%zu)\n",
				scenario, before, after, headerBefore, headerAfter);
			if(after <= 1 || headerAfter <= 1) {
				fail("could not read the table from the screen (dialog covered or capture failed)");
			} else {
				if(before <= 1)
					fail(std::string("the shared folders are in the table but were not painted after ") + scenario);
				if(headerBefore <= 1)
					fail(std::string("the table's column header was not painted after ") + scenario);
			}
			finish();
			return;
		}
		case Phase::Done:
			return;
		}
	}

	void finish() {
		phase = Phase::Done;
		if(dialog) PostMessageW(dialog, WM_CLOSE, 0, 0);
	}
};

/* Visits every settings page once and checks on screen that each page's own background
(its margins around the controls) uses the dark palette the first time it is shown,
instead of the window class's light default. */
struct PageVisitDriver {
	enum class Phase { Opening, Settling, Done } phase = Phase::Opening;
	Clock::time_point started = Clock::now(), phaseStart;
	HWND dialog = nullptr, tree = nullptr;
	HTREEITEM item = nullptr;
	std::wstring page;
	std::vector<std::wstring> lightPages;

	void enter(Phase next) { phase = next; phaseStart = Clock::now(); }

	HTREEITEM nextItem(HTREEITEM current) const {
		if(auto child = TreeView_GetChild(tree, current)) return child;
		while(current) {
			if(auto sibling = TreeView_GetNextSibling(tree, current)) return sibling;
			current = TreeView_GetParent(tree, current);
		}
		return nullptr;
	}

	void select(HTREEITEM next) {
		item = next;
		wchar_t text[128] {};
		TVITEMW info { TVIF_TEXT, item };
		info.pszText = text;
		info.cchTextMax = 128;
		TreeView_GetItem(tree, &info);
		page = text;
		TreeView_SelectItem(tree, item);
		enter(Phase::Settling);
	}

	HWND visiblePage() const {
		HWND found = nullptr;
		EnumChildWindows(dialog, [](HWND window, LPARAM data) -> BOOL {
			if(IsWindowVisible(window) && dynamic_cast<PropPage*>(dwt::hwnd_cast<dwt::Widget*>(window))) {
				*reinterpret_cast<HWND*>(data) = window;
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&found));
		return found;
	}

	/* Pixels just inside the page's corners belong to the page's own background. A page
	taller or wider than the dialog scrolls inside its host, so only its visible part counts. */
	bool cornersLight(HWND window) const {
		RECT r {}, host {};
		GetWindowRect(window, &r);
		GetClientRect(GetParent(window), &host);
		MapWindowPoints(GetParent(window), nullptr, reinterpret_cast<POINT*>(&host), 2);
		IntersectRect(&r, &r, &host);
		const POINT points[] = { { r.left + 2, r.top + 2 }, { r.right - 3, r.top + 2 },
			{ r.left + 2, r.bottom - 3 }, { r.right - 3, r.bottom - 3 } };
		HDC screen = GetDC(nullptr);
		int light = 0;
		for(const auto& p: points) {
			const auto colour = GetPixel(screen, p.x, p.y);
			if(colour != CLR_INVALID && GetRValue(colour) >= 224 && GetGValue(colour) >= 224 && GetBValue(colour) >= 224) ++light;
		}
		ReleaseDC(nullptr, screen);
		return light > 0;
	}

	void tick() {
		if(Clock::now() - started > TIMEOUT) {
			fail("timed out visiting the settings pages");
			finish();
			return;
		}
		switch(phase) {
		case Phase::Opening:
			EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
				if(IsWindowVisible(window) && className(window).find(L"ModalDialog") != std::wstring::npos) {
					*reinterpret_cast<HWND*>(data) = window;
					return FALSE;
				}
				return TRUE;
			}, reinterpret_cast<LPARAM>(&dialog));
			if(!dialog) return;
			SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			EnumChildWindows(dialog, [](HWND window, LPARAM data) -> BOOL {
				if(className(window).find(L"TreeView") != std::wstring::npos) {
					*reinterpret_cast<HWND*>(data) = window;
					return FALSE;
				}
				return TRUE;
			}, reinterpret_cast<LPARAM>(&tree));
			if(!tree) {
				fail("settings tree not found");
				finish();
				return;
			}
			select(TreeView_GetRoot(tree));
			return;
		case Phase::Settling: {
			if(Clock::now() - phaseStart < SETTLE) return;
			auto window = visiblePage();
			if(window && cornersLight(window)) lightPages.push_back(page);
			if(auto next = nextItem(item)) {
				select(next);
				return;
			}
			std::printf("visited every settings page: %zu showed a light background on their first visit\n", lightPages.size());
			for(const auto& name: lightPages) {
				char name8[128] {};
				WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, name8, sizeof(name8), nullptr, nullptr);
				fail(std::string("settings page '") + name8 + "' showed a light background on its first visit in the dark appearance");
			}
			finish();
			return;
		}
		case Phase::Done:
			return;
		}
	}

	void finish() {
		phase = Phase::Done;
		if(dialog) PostMessageW(dialog, WM_CLOSE, 0, 0);
	}
};

Driver* driver = nullptr;
PageVisitDriver* visitDriver = nullptr;

LRESULT CALLBACK tickProc(HWND window, UINT message, WPARAM w, LPARAM l) {
	if(message == WM_TICK) {
		tickPending = false;
		if(driver) driver->tick();
		if(visitDriver) visitDriver->tick();
		return 0;
	}
	return DefWindowProcW(window, message, w, l);
}

}

int dwtMain(dwt::Application& app) {
	wchar_t cwd[32768];
	GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
	const auto directory = Text::fromT(cwd) + "\\build\\settings-list-paint\\";
	Util::PathsMap paths;
	for(int path = 0; path < Util::PATH_LAST; ++path)
		paths[static_cast<Util::Paths>(path)] = directory;
	Util::initialize(paths);
	File::ensureDirectory(directory);
	dcpp::startup();
	SettingsManager::getInstance()->set(SettingsManager::NICK, "settings-test");
	SettingsManager::getInstance()->set(SettingsManager::SETTINGS_PAGE, 0);
	for(const char* name: { "First share", "Second share" }) {
		const auto folder = directory + name + "\\";
		File::ensureDirectory(folder);
		try {
			ShareManager::getInstance()->addDirectory(folder, name);
		} catch(const Exception& e) {
			std::fprintf(stderr, "share setup: %s\n", e.getError().c_str());
		}
	}
	IconManager::initialize();
	UiServices::initFont();
	ControlSeeds::init();
	app.getAppearance().configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
	if(!app.getAppearance().isManual()) {
		std::puts("SKIP: custom-drawn appearance unavailable (high contrast)");
		dcpp::shutdown();
		return 0;
	}

	auto owner = new dwt::Window();
	dwt::Window::Seed seed;
	seed.caption = L"Settings list paint";
	seed.location = dwt::Rectangle(60, 60, 300, 200);
	owner->create(seed);

	WNDCLASSW tickClass {};
	tickClass.lpfnWndProc = tickProc;
	tickClass.hInstance = GetModuleHandleW(nullptr);
	tickClass.lpszClassName = L"SettingsListPaintTick";
	RegisterClassW(&tickClass);
	const auto ticker = CreateWindowW(tickClass.lpszClassName, L"", 0, 0, 0, 0, 0,
		HWND_MESSAGE, nullptr, tickClass.hInstance, nullptr);
	std::atomic<bool> stop { false };
	std::thread pulse([&stop, ticker] {
		while(!stop) {
			if(!tickPending.exchange(true)) PostMessageW(ticker, WM_TICK, 0, 0);
			std::this_thread::sleep_for(std::chrono::milliseconds(15));
		}
	});

	for(bool opensOnSharing: { false, true }) {
		Driver state(opensOnSharing);
		driver = &state;
		SettingsDialog(owner).run();
		driver = nullptr;
		while(app.dispatch()) { }
	}
	{
		PageVisitDriver state;
		visitDriver = &state;
		SettingsDialog(owner).run();
		visitDriver = nullptr;
		while(app.dispatch()) { }
	}
	stop = true;
	pulse.join();
	DestroyWindow(ticker);
	DestroyWindow(owner->handle());
	dcpp::shutdown();

	if(failures) return 1;
	std::puts("PASS: the Sharing page paints its shared folders when switched to and when the dialog opens on it");
	return 0;
}
