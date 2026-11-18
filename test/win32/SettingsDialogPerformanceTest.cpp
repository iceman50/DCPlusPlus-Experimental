// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// Opens the real settings dialog with the dark (custom-drawn) appearance, visits
// every page and checks that an idle dialog stops repainting. Prints how long
// opening and each page switch take and what the dialog costs in GDI/USER
// objects and memory.
#include "stdafx.h"
#include <win32/SingleInstance.h>
#include <win32/util/UiServices.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/IconManager.h>
#include <win32/settings/base/SettingsDialog.h>
#include <dcpp/DCPlusPlus.h>
#include <dcpp/core/io/File.h>
#include <dcpp/settings/SettingsManager.h>
#include <dwt/Application.h>
#include <dwt/Appearance.h>
#include <dwt/widgets/Window.h>
#include <commctrl.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 188;

namespace {

using Clock = std::chrono::steady_clock;

// An idle page may legitimately repaint a few times while it settles; the bug
// this guards against repainted thousands of times per second.
constexpr long MAX_IDLE_PAINTS = 25;
constexpr auto SETTLE = std::chrono::milliseconds(250);
constexpr auto MEASURE = std::chrono::milliseconds(500);
constexpr auto TIMEOUT = std::chrono::seconds(120);
constexpr UINT WM_TICK = WM_APP + 1;

long paints = 0;
// Which messages each window class receives while a page is measured, so a
// failure names what keeps the page repainting.
bool recording = false;
std::map<std::pair<std::wstring, UINT>, long> received;
HWND watched = nullptr;
long watchedPaints = 0;
int failures = 0;

std::wstring className(HWND window);

void countMessage(HWND window, UINT message) {
	if(message == WM_PAINT) {
		++paints;
		if(window && window == watched) ++watchedPaints;
	}
	if(recording && window) ++received[{ className(window), message }];
}

LRESULT CALLBACK sentHook(int code, WPARAM w, LPARAM l) {
	if(code == HC_ACTION) {
		const auto& message = *reinterpret_cast<CWPSTRUCT*>(l);
		countMessage(message.hwnd, message.message);
	}
	return CallNextHookEx(nullptr, code, w, l);
}

LRESULT CALLBACK postedHook(int code, WPARAM w, LPARAM l) {
	if(code == HC_ACTION && w == PM_REMOVE) {
		const auto& message = *reinterpret_cast<MSG*>(l);
		countMessage(message.hwnd, message.message);
	}
	return CallNextHookEx(nullptr, code, w, l);
}

void fail(const std::string& message) {
	std::fprintf(stderr, "FAIL: %s\n", message.c_str());
	std::fflush(stderr);
	++failures;
}

double cpuMilliseconds() {
	FILETIME created, exited, kernel, user;
	GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
	auto value = [](FILETIME time) { return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
	return (value(kernel) + value(user)) / 10000.0;
}

struct Resources { DWORD gdi, user; double privateMb; };

Resources resources() {
	PROCESS_MEMORY_COUNTERS_EX memory { sizeof(memory) };
	GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
	return { GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
		GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), memory.PrivateUsage / 1048576.0 };
}

void printResources(const char* when, const Resources& value) {
	std::printf("%s: %lu GDI objects, %lu USER objects, %.1f MB private\n",
		when, value.gdi, value.user, value.privateMb);
}

double millisecondsSince(Clock::time_point start) {
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::wstring className(HWND window) {
	wchar_t name[96] {};
	GetClassNameW(window, name, 96);
	return name;
}

struct Driver {
	enum class Phase { Opening, Settle, Measure, CheckOverlay, Done } phase = Phase::Opening;
	const bool measuring;
	Clock::time_point started = Clock::now(), phaseStart;
	HWND dialog = nullptr, tree = nullptr;
	HTREEITEM item = nullptr;
	std::wstring page;
	double cpuStart = 0;

	explicit Driver(bool measuring) : measuring(measuring) { }

	void enter(Phase next) {
		phase = next;
		phaseStart = Clock::now();
	}

	bool elapsed(Clock::duration duration) const { return Clock::now() - phaseStart >= duration; }

	void select(HTREEITEM next) {
		item = next;
		wchar_t text[128] {};
		TVITEMW info { TVIF_TEXT, item };
		info.pszText = text;
		info.cchTextMax = 128;
		TreeView_GetItem(tree, &info);
		page = text;
		const auto windowsBefore = windowCount();
		const auto start = Clock::now();
		TreeView_SelectItem(tree, item);
		RedrawWindow(dialog, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
		if(measuring) {
			std::printf("%-28ls switch %6.1f ms %4ld new windows", page.c_str(),
				millisecondsSince(start), windowCount() - windowsBefore);
			std::fflush(stdout);
		}
		enter(Phase::Settle);
	}

	long windowCount() const {
		long count = 0;
		EnumChildWindows(dialog, [](HWND, LPARAM data) -> BOOL {
			++*reinterpret_cast<long*>(data);
			return TRUE;
		}, reinterpret_cast<LPARAM>(&count));
		return count;
	}

	HTREEITEM nextItem(HTREEITEM current) const {
		if(auto child = TreeView_GetChild(tree, current)) return child;
		while(current) {
			if(auto sibling = TreeView_GetNextSibling(tree, current)) return sibling;
			current = TreeView_GetParent(tree, current);
		}
		return nullptr;
	}

	void selectNextOrFinish() {
		if(auto next = nextItem(item)) {
			select(next);
			return;
		}
		if(measuring) printResources("all pages visited", resources());
		finish();
	}

	// Returns a control on the current page and its visible custom-drawn vertical scrollbar.
	std::pair<HWND, HWND> decorated() const {
		std::pair<HWND, HWND> found {};
		EnumChildWindows(dialog, [](HWND window, LPARAM data) -> BOOL {
			auto decoration = static_cast<HWND>(GetPropW(window, L"dwt.Appearance.VerticalScrollDecoration"));
			if(decoration && IsWindowVisible(window) && IsWindowVisible(decoration)) {
				*reinterpret_cast<std::pair<HWND, HWND>*>(data) = { window, decoration };
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&found));
		return found;
	}

	void tick() {
		if(Clock::now() - started > TIMEOUT) {
			fail("settings dialog test timed out");
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
			RedrawWindow(dialog, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
			std::printf("dialog open %.1f ms\n", millisecondsSince(started));
			if(measuring) printResources("open", resources());
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
		case Phase::Settle:
			if(!measuring) {
				selectNextOrFinish();
				return;
			}
			if(!elapsed(SETTLE)) return;
			paints = 0;
			received.clear();
			recording = true;
			cpuStart = cpuMilliseconds();
			enter(Phase::Measure);
			return;
		case Phase::Measure: {
			if(!elapsed(MEASURE)) return;
			const auto idlePaints = paints;
			recording = false;
			std::printf("  idle %4ld paints %6.1f ms CPU\n", idlePaints, cpuMilliseconds() - cpuStart);
			std::fflush(stdout);
			if(idlePaints > MAX_IDLE_PAINTS) {
				char page8[128] {};
				WideCharToMultiByte(CP_UTF8, 0, page.c_str(), -1, page8, sizeof(page8), nullptr, nullptr);
				fail(std::string("idle settings page '") + page8 + "' repainted " +
					std::to_string(idlePaints) + " times in " + std::to_string(MEASURE.count()) + " ms");
				std::vector<std::pair<long, std::pair<std::wstring, UINT>>> busiest;
				for(const auto& entry: received) busiest.push_back({ entry.second, entry.first });
				std::sort(busiest.rbegin(), busiest.rend());
				for(size_t i = 0; i < busiest.size() && i < 8; ++i)
					std::fprintf(stderr, "  %6ld x message 0x%04X to %ls\n", busiest[i].first,
						busiest[i].second.second, busiest[i].second.first.c_str());
			}
			if(item == TreeView_GetRoot(tree)) {
				/* The first page's multi-line away message has a custom-drawn
				 * scrollbar. Repainting the control's frame must still repaint it. */
				const auto control = decorated();
				watched = control.second;
				if(!watched) {
					fail("no custom-drawn scrollbar on the first settings page");
				} else {
					watchedPaints = 0;
					RedrawWindow(control.first, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
				}
				enter(Phase::CheckOverlay);
				return;
			}
			selectNextOrFinish();
			return;
		}
		case Phase::CheckOverlay:
			if(!elapsed(SETTLE)) return;
			if(watched && watchedPaints == 0)
				fail("repainting a control's frame left its custom-drawn scrollbar unpainted");
			watched = nullptr;
			selectNextOrFinish();
			return;
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
std::atomic<bool> tickPending { false };

LRESULT CALLBACK tickProc(HWND window, UINT message, WPARAM w, LPARAM l) {
	if(message == WM_TICK) {
		tickPending = false;
		if(driver) driver->tick();
		return 0;
	}
	return DefWindowProcW(window, message, w, l);
}

}

int dwtMain(dwt::Application& app) {
	wchar_t cwd[32768];
	GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
	const auto directory = Text::fromT(cwd) + "\\build\\settings-dialog-performance\\";
	Util::PathsMap paths;
	for(int path = 0; path < Util::PATH_LAST; ++path)
		paths[static_cast<Util::Paths>(path)] = directory;
	Util::initialize(paths);
	File::ensureDirectory(directory);
	dcpp::startup();
	SettingsManager::getInstance()->set(SettingsManager::NICK, "settings-test");
	IconManager::initialize();
	UiServices::initFont();
	ControlSeeds::init();
	app.getAppearance().configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
	if(!app.getAppearance().isManual()) {
		// High contrast uses native controls, which this test does not cover.
		std::puts("SKIP: custom-drawn appearance unavailable (high contrast)");
		dcpp::shutdown();
		return 0;
	}

	auto owner = new dwt::Window();
	dwt::Window::Seed seed;
	seed.caption = L"Settings dialog performance";
	seed.location = dwt::Rectangle(60, 60, 300, 200);
	owner->create(seed);
	printResources("before", resources());

	/* Posted messages outrank WM_PAINT, so the test keeps advancing (and fails
	 * with a count) even if the dialog repaints without end. A timer would starve. */
	WNDCLASSW tickClass {};
	tickClass.lpfnWndProc = tickProc;
	tickClass.hInstance = GetModuleHandleW(nullptr);
	tickClass.lpszClassName = L"SettingsDialogPerformanceTick";
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
	const auto sent = SetWindowsHookExW(WH_CALLWNDPROC, sentHook, nullptr, GetCurrentThreadId());
	const auto posted = SetWindowsHookExW(WH_GETMESSAGE, postedHook, nullptr, GetCurrentThreadId());

	/* The first pass measures every page. The second only opens every page again,
	 * so anything the first pass leaked shows up as growth after the second close. */
	Resources closed[2] {};
	for(int pass = 0; pass < 2; ++pass) {
		Driver state(pass == 0);
		driver = &state;
		SettingsDialog(owner).run();
		driver = nullptr;
		while(app.dispatch()) { }
		closed[pass] = resources();
		printResources(pass == 0 ? "closed" : "closed again", closed[pass]);
	}
	stop = true;
	pulse.join();
	UnhookWindowsHookEx(sent);
	UnhookWindowsHookEx(posted);
	DestroyWindow(ticker);
	if(closed[1].gdi > closed[0].gdi + 5 || closed[1].user > closed[0].user + 5)
		fail("reopening the settings dialog kept more GDI/USER objects alive");

	DestroyWindow(owner->handle());
	dcpp::shutdown();

	if(failures) return 1;
	std::puts("PASS: settings dialog idles without repainting on every page in the dark appearance");
	return 0;
}
