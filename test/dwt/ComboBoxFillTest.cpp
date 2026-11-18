// Copyright (C) 2026 iceman50
// Consecutive ComboBox insertions must not resize the drop-down list after every
// item, yet the list must open at its normal size however soon it is opened.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/ComboBox.h>

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void pump(dwt::Application& app) {
	while(app.dispatch()) { }
}

dwt::ComboBoxPtr makeCombo(dwt::Window* window) {
	auto combo = window->addChild(dwt::ComboBox::Seed());
	combo->resize(dwt::Rectangle(10, 10, combo->scale(200), combo->scale(300)));
	return combo;
}

SIZE droppedList(HWND combo) {
	::SendMessage(combo, CB_SHOWDROPDOWN, TRUE, 0);
	COMBOBOXINFO info { sizeof(info) };
	check(GetComboBoxInfo(combo, &info) != FALSE, "Cannot read combo box parts");
	RECT bounds {};
	GetWindowRect(info.hwndList, &bounds);
	::SendMessage(combo, CB_SHOWDROPDOWN, FALSE, 0);
	return { bounds.right - bounds.left, bounds.bottom - bounds.top };
}

// The same items added to a plain Win32 combo box, which Windows sizes after every item.
SIZE nativeList(dwt::Window* window, int count, double& milliseconds) {
	auto combo = ::CreateWindowEx(0, WC_COMBOBOX, L"", dwt::ComboBox::Seed().style | WS_VISIBLE,
		10, 10, window->scale(200), window->scale(300), window->handle(), nullptr, nullptr, nullptr);
	::SendMessage(combo, WM_SETFONT, reinterpret_cast<WPARAM>(window->getFont()->handle()), FALSE);
	const auto start = std::chrono::steady_clock::now();
	for(int i = 0; i < count; ++i) {
		const auto text = L"Item " + std::to_wstring(i);
		::SendMessage(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
	}
	milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	const auto size = droppedList(combo);
	::DestroyWindow(combo);
	return size;
}

void checkFill(dwt::Application& app, dwt::Window* window, int count, bool openAtOnce, bool insert) {
	double nativeMs = 0;
	const auto expected = nativeList(window, count, nativeMs);

	auto combo = makeCombo(window);
	combo->setFont(window->getFont());
	pump(app);
	const auto visible = ::SendMessage(combo->handle(), CB_GETMINVISIBLE, 0, 0);
	const auto start = std::chrono::steady_clock::now();
	for(int i = 0; i < count; ++i) {
		const auto text = L"Item " + std::to_wstring(i);
		if(insert) combo->insertValue(i, text); else combo->addValue(text);
	}
	const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	// Opening the list or reading the minimum straight away must already see the real one.
	if(!openAtOnce) pump(app);
	check(combo->sendMessage(CB_GETMINVISIBLE) == visible, "The minimum visible items were not restored");
	const auto actual = droppedList(combo->handle());
	check(actual.cx == expected.cx && actual.cy == expected.cy, "The drop-down list opened at the wrong size");
	check(combo->size() == static_cast<size_t>(count), "Items were lost");
	for(int i = 0; i < count; ++i) check(combo->getValue(i) == L"Item " + std::to_wstring(i), "Items changed order or text");
	if(openAtOnce && !insert) std::printf("%2d items: Win32 %5.2f ms, dwt %5.2f ms\n", count, nativeMs, ms);
	// Measured 6-12 times faster; allow a wide margin for a busy machine.
	if(count >= 24) check(ms * 2 < nativeMs, "Filling the combo box resized its list after every item");

	// A later single insertion keeps the minimum and the list size in step.
	pump(app);
	combo->addValue(L"Later");
	pump(app);
	check(combo->sendMessage(CB_GETMINVISIBLE) == visible, "A single insertion changed the minimum visible items");
	::DestroyWindow(combo->handle());
	pump(app);
}
}

int dwtMain(dwt::Application& app) {
	auto window = new dwt::Window();
	window->create(dwt::Window::Seed());
	window->resize(dwt::Rectangle(80, 80, 500, 400));
	int result = 0;
	try {
		for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			for(int count : { 0, 1, 2, 5, 24, 40 }) {
				for(bool openAtOnce : { true, false }) {
					for(bool insert : { false, true }) checkFill(app, window, count, openAtOnce, insert);
				}
			}
		}
		std::puts("PASS: combo box fills keep items, order, minimum visible items and drop-down size in light/dark modes");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
