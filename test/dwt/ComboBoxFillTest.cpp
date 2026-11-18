// Copyright (C) 2026 iceman50
// ComboBox::addValues must add the same items as addValue and leave the drop-down
// list the same size, while avoiding the per-item resizing of the list.
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

dwt::ComboBoxPtr makeCombo(dwt::Window* window) {
	auto combo = window->addChild(dwt::ComboBox::Seed());
	combo->resize(dwt::Rectangle(10, 10, combo->scale(200), combo->scale(300)));
	return combo;
}

RECT droppedList(dwt::ComboBoxPtr combo) {
	combo->sendMessage(CB_SHOWDROPDOWN, TRUE);
	COMBOBOXINFO info { sizeof(info) };
	check(GetComboBoxInfo(combo->handle(), &info) != FALSE, "Cannot read combo box parts");
	RECT bounds {};
	GetWindowRect(info.hwndList, &bounds);
	combo->sendMessage(CB_SHOWDROPDOWN, FALSE);
	return bounds;
}

void checkFill(dwt::Window* window, int count) {
	std::vector<dwt::tstring> values;
	for(int i = 0; i < count; ++i) values.push_back(L"Item " + std::to_wstring(i));

	auto single = makeCombo(window);
	const auto singleStart = std::chrono::steady_clock::now();
	for(const auto& value: values) single->addValue(value);
	const auto singleMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - singleStart).count();

	auto batch = makeCombo(window);
	const auto visible = batch->sendMessage(CB_GETMINVISIBLE);
	const auto batchStart = std::chrono::steady_clock::now();
	batch->addValues(values);
	const auto batchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - batchStart).count();

	check(batch->size() == values.size(), "addValues added the wrong number of items");
	for(int i = 0; i < count; ++i) check(batch->getValue(i) == values[i], "addValues changed the item order or text");
	check(batch->sendMessage(CB_GETMINVISIBLE) == visible, "addValues did not restore the minimum visible items");
	const auto expected = droppedList(single), actual = droppedList(batch);
	check(expected.right - expected.left == actual.right - actual.left &&
		expected.bottom - expected.top == actual.bottom - actual.top, "addValues changed the drop-down list size");
	std::printf("%2d items: addValue %.2f ms, addValues %.2f ms\n", count, singleMs, batchMs);
	::DestroyWindow(single->handle());
	::DestroyWindow(batch->handle());
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
			for(int count : { 0, 1, 5, 24, 40 }) checkFill(window, count);
		}
		std::puts("PASS: combo box batch fill keeps items, order and drop-down size in light/dark modes");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
