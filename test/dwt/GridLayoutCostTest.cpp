// Copyright (C) 2026 iceman50
// A grid layout must measure a deeply nested control about once per nesting level,
// not once per row pass, column pass and placement at every level (3 * 2^depth).
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/ComboBox.h>

#include <commctrl.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

// ComboBox::getPreferredSize asks for the selection field height exactly once.
LRESULT CALLBACK countMeasurements(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	if(message == CB_GETITEMHEIGHT && static_cast<int>(wParam) == -1) ++*reinterpret_cast<unsigned*>(data);
	return DefSubclassProc(window, message, wParam, lParam);
}
}

int dwtMain(dwt::Application& app) {
	auto window = new dwt::Window();
	window->create(dwt::Window::Seed());
	window->resize(dwt::Rectangle(80, 80, 500, 400));
	int result = 0;
	try {
		const unsigned depth = 7;
		auto outer = window->addChild(dwt::Grid::Seed(1, 1));
		outer->resize(dwt::Rectangle(0, 0, 400, 300));
		auto grid = outer;
		for(unsigned i = 1; i < depth; ++i) grid = grid->addChild(dwt::Grid::Seed(1, 1));
		auto combo = grid->addChild(dwt::ComboBox::Seed());
		combo->addValue(L"A value wide enough to measure");
		while(app.dispatch()) { }

		unsigned measurements = 0;
		check(SetWindowSubclass(combo->handle(), countMeasurements, 1, reinterpret_cast<DWORD_PTR>(&measurements)) != FALSE,
			"Cannot observe the combo box");
		outer->layout();
		const auto preferred = combo->getPreferredSize();
		RemoveWindowSubclass(combo->handle(), countMeasurements, 1);
		std::printf("%u grids deep: %u measurements for one layout\n", depth, measurements);
		check(measurements <= 2 * depth, "A grid layout measured a nested control once per row, column and placement pass at every level");

		RECT bounds {};
		GetWindowRect(combo->handle(), &bounds);
		check(bounds.right - bounds.left == preferred.x, "The nested control was not given its preferred width");
		::DestroyWindow(outer->handle());
		std::puts("PASS: nested grid layout measures each control about once per level");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
