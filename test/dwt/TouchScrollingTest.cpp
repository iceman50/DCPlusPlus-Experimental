// Copyright (C) 2026 iceman50
// Exercise the production pan handler against real Windows list-view controls.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/TableTree.h>
#include <dwt/widgets/Tree.h>
#include <dwt/util/win32/Version.h>
#include "../../dwt/src/widgets/TouchScrolling.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

dwt::GestureEvent gesture(dwt::GestureEvent::Type type, DWORD flags, long x, long y) {
	MSG message = {};
	dwt::GestureEvent event(message);
	event.valid = true;
	event.type = type;
	event.flags = flags;
	event.pos = dwt::ScreenCoordinate(dwt::Point(x, y));
	return event;
}

void populate(dwt::Table& table) {
	table.resize(dwt::Rectangle(0, 0, 320, 200));
	table.addColumn(L"Name", 600);
	table.addColumn(L"Details", 600);
	for(int i = 0; i < 250; ++i) {
		table.insert({ L"Touch test row " + std::to_wstring(i), L"Swipe horizontally too" });
	}
}

void exercise(dwt::Table& table) {
	dwt::detail::TablePan pan;
	const auto hwnd = table.handle();
	RECT row = {};
	check(ListView_GetItemRect(hwnd, 0, &row, LVIR_BOUNDS), "Cannot measure test row");
	const auto height = row.bottom - row.top;
	check(height > 2, "Invalid row height");
	table.setSelected(3);
	const auto selection = table.getSelected();

	check(!pan.handle(table, gesture(dwt::GestureEvent::Begin, 0, -100, -100)),
		"Generic gesture begin must reach the native control");
	check(pan.handle(table, gesture(dwt::GestureEvent::Pan, GF_BEGIN, -100, -100)), "Pan begin not handled");
	for(int i = 1; i < height; ++i) {
		pan.handle(table, gesture(dwt::GestureEvent::Pan, 0, -100, -100 - i));
		check(ListView_GetTopIndex(hwnd) == 0, "Sub-row movement rounded up prematurely");
	}
	pan.handle(table, gesture(dwt::GestureEvent::Pan, 0, -100, -100 - height));
	check(ListView_GetTopIndex(hwnd) == 1, "Slow one-pixel swipes did not accumulate");
	pan.handle(table, gesture(dwt::GestureEvent::Pan, GF_INERTIA, -150, -100 - 4 * height));
	check(ListView_GetTopIndex(hwnd) == 4, "Inertia did not scroll the table");
	check(::GetScrollPos(hwnd, SB_HORZ) == 50, "Horizontal pan has wrong direction or distance");
	check(table.getSelected() == selection, "Panning changed the selection");
	pan.handle(table, gesture(dwt::GestureEvent::Pan, GF_END, -150, -100 - 5 * height));
	check(ListView_GetTopIndex(hwnd) == 5, "Final pan delta was lost");
	pan.handle(table, gesture(dwt::GestureEvent::Pan, GF_INERTIA, -150, -100 - 8 * height));
	check(ListView_GetTopIndex(hwnd) == 5, "Ended gesture still scrolled");
	check(!pan.handle(table, gesture(dwt::GestureEvent::End, 0, 0, 0)), "Generic end was consumed");
	check(!pan.handle(table, gesture(dwt::GestureEvent::Zoom, 0, 0, 0)), "Unimplemented zoom was consumed");

	pan.handle(table, gesture(dwt::GestureEvent::Pan, GF_BEGIN, 0, 0));
	pan.handle(table, gesture(dwt::GestureEvent::Pan, 0, 0, -(height - 1)));
	pan.reset();
	pan.handle(table, gesture(dwt::GestureEvent::Pan, GF_BEGIN, 0, 0));
	pan.handle(table, gesture(dwt::GestureEvent::Pan, 0, 0, -1));
	check(ListView_GetTopIndex(hwnd) == 5, "Canceled gesture retained fractional motion");

	// Outward movement at the top must not delay a subsequent reverse swipe.
	table.ensureVisible(0, false);
	pan.handle(table, gesture(dwt::GestureEvent::Pan, GF_BEGIN, 0, 0));
	pan.handle(table, gesture(dwt::GestureEvent::Pan, 0, 0, height - 1));
	pan.handle(table, gesture(dwt::GestureEvent::Pan, 0, 0, -1));
	check(ListView_GetTopIndex(hwnd) == 1, "Overscroll delayed reversal at the top");

	table.ensureVisible(249, false);
	const auto bottom = ListView_GetTopIndex(hwnd);
	pan.handle(table, gesture(dwt::GestureEvent::Pan, GF_BEGIN, 0, 0));
	pan.handle(table, gesture(dwt::GestureEvent::Pan, 0, 0, -(height - 1)));
	pan.handle(table, gesture(dwt::GestureEvent::Pan, 0, 0, 1));
	check(ListView_GetTopIndex(hwnd) == bottom - 1, "Overscroll delayed reversal at the bottom");
}

void checkConfiguration(HWND hwnd) {
	GESTURECONFIG config = { GID_PAN, 0, 0 };
	UINT count = 1;
	check(::GetGestureConfig(hwnd, 0, 0, &count, &config, sizeof(config)) != FALSE,
		"Cannot query gesture configuration");
	const DWORD wanted = GC_PAN | GC_PAN_WITH_SINGLE_FINGER_VERTICALLY |
		GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY | GC_PAN_WITH_INERTIA;
	check((config.dwWant & wanted) == wanted && !(config.dwBlock & wanted),
		"Single-finger pan/inertia configuration missing from the input HWND");
	check(!::IsTouchWindow(hwnd, nullptr), "Raw touch registration disabled gesture recognition");
}

// Optional desktop integration check: Windows generates real gesture handles
// from injected contacts. Run with --inject; the test owns the visible window.
void checkInjectedPan(dwt::Application& app, dwt::Window& window, dwt::Table& table) {
	using Initialize = BOOL (WINAPI*)(UINT32, DWORD);
	using Inject = BOOL (WINAPI*)(UINT32, const POINTER_TOUCH_INFO*);
	auto initAddress = ::GetProcAddress(::GetModuleHandle(L"user32.dll"), "InitializeTouchInjection");
	auto injectAddress = ::GetProcAddress(::GetModuleHandle(L"user32.dll"), "InjectTouchInput");
	check(initAddress && injectAddress, "Touch injection APIs unavailable");
	Initialize initialize;
	Inject inject;
	std::memcpy(&initialize, &initAddress, sizeof(initialize));
	std::memcpy(&inject, &injectAddress, sizeof(inject));
	check(initialize(1, 3), "InitializeTouchInjection failed"); // TOUCH_FEEDBACK_NONE
	auto pump = [&app](unsigned milliseconds) {
		const auto end = ::GetTickCount64() + milliseconds;
		do {
			for(unsigned i = 0; i < 1000 && app.dispatch(); ++i) { }
			::Sleep(5);
		} while(::GetTickCount64() < end);
	};
	window.resize(dwt::Rectangle(100, 100, 640, 480));
	table.resize(dwt::Rectangle(0, 0, 550, 350));
	window.setVisible(true);
	table.setVisible(true);
	::SetForegroundWindow(window.handle());
	::SetFocus(table.handle());
	table.ensureVisible(0, false);
	table.scroll(-2000, 0);
	pump(150);
	POINT start = { 180, 250 };
	::ClientToScreen(table.handle(), &start);
	check(::WindowFromPoint(start) == table.handle(), "Touch fixture is obscured by another window");
	POINTER_TOUCH_INFO contact = {};
	contact.pointerInfo.pointerType = PT_TOUCH;
	contact.pointerInfo.pointerId = 1;
	contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
	contact.orientation = 90;
	contact.pressure = 32000;
	auto send = [&](long x, long y, POINTER_FLAGS flags) {
		contact.pointerInfo.ptPixelLocation = { x, y };
		contact.pointerInfo.pointerFlags = flags;
		contact.rcContact = { x - 2, y - 2, x + 2, y + 2 };
		if(!inject(1, &contact)) {
			const auto error = ::GetLastError();
			throw std::runtime_error("InjectTouchInput failed: " + std::to_string(error) +
				", flags=" + std::to_string(flags) + ", x=" + std::to_string(x) +
				", y=" + std::to_string(y) + ", size=" + std::to_string(sizeof(contact)));
		}
		pump(25);
	};
	const auto touching = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
	send(start.x, start.y, touching | POINTER_FLAG_DOWN);
	for(int i = 1; i <= 15; ++i) send(start.x, start.y - i * 8, touching | POINTER_FLAG_UPDATE);
	send(start.x, start.y - 120, POINTER_FLAG_UP);
	pump(500);
	check(ListView_GetTopIndex(table.handle()) > 0, "Real injected swipe did not scroll through WM_GESTURE");
	std::puts("Windows touch injection: vertical swipe delivered and scrolled successfully.");
}
}

int dwtMain(dwt::Application& app) {
	try {
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed(L"DWT touchscreen scrolling test");
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		auto table = window->addChild(dwt::Table::Seed());
		populate(*table);
		exercise(*table);
		auto grouped = window->addChild(dwt::TableTree::Seed(dwt::Table::Seed()));
		populate(*grouped);
		exercise(*grouped);
		auto tree = window->addChild(dwt::Tree::Seed());
		if(dwt::util::win32::ensureVersion(dwt::util::win32::EIGHT_ONE)) {
			checkConfiguration(table->handle());
			checkConfiguration(grouped->handle());
			const auto nativeTree = tree->treeHandle();
			check(nativeTree != nullptr, "Missing native tree child");
			checkConfiguration(nativeTree);
		}
		auto empty = window->addChild(dwt::Table::Seed());
		dwt::detail::TablePan pan;
		pan.handle(*empty, gesture(dwt::GestureEvent::Pan, GF_BEGIN, 0, 0));
		pan.handle(*empty, gesture(dwt::GestureEvent::Pan, GF_END, -100, -100));
		if(std::wcsstr(::GetCommandLineW(), L"--inject")) {
			grouped->setVisible(false);
			tree->setVisible(false);
			empty->setVisible(false);
			checkInjectedPan(app, *window, *table);
		}
		::DestroyWindow(window->handle());
		std::puts("Touch scrolling: slow pan, inertia, horizontal movement, selection, end/cancel, boundaries, TableTree and native tree configuration passed.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
