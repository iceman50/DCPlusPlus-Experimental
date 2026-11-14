// Copyright (C) 2026 iceman50
// Link the production DWT code against a simulated OS version for this test.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/Tree.h>
#include <dwt/util/win32/Version.h>

#include <cstdio>
#include <stdexcept>

namespace {
dwt::util::win32::Version simulatedVersion = dwt::util::win32::SEVEN;

void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

GESTURECONFIG configuration(HWND hwnd) {
	GESTURECONFIG config = { GID_PAN, 0, 0 };
	UINT count = 1;
	check(::GetGestureConfig(hwnd, 0, 0, &count, &config, sizeof(config)) != FALSE,
		"Cannot query pan configuration");
	return config;
}
}

namespace dwt { namespace util { namespace win32 {
bool ensureVersion(Version version) {
	return simulatedVersion >= version;
}
} } }

int dwtMain(dwt::Application&) {
	try {
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed();
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		const auto nativeTable = ::CreateWindowEx(0, WC_LISTVIEW, L"", WS_CHILD | LVS_REPORT,
			0, 0, 300, 200, window->handle(), nullptr, ::GetModuleHandle(nullptr), nullptr);
		const auto nativeTree = ::CreateWindowEx(0, WC_TREEVIEW, L"", WS_CHILD,
			0, 0, 300, 200, window->handle(), nullptr, ::GetModuleHandle(nullptr), nullptr);
		check(nativeTable && nativeTree, "Cannot create native reference controls");
		const auto tableDefault = configuration(nativeTable);
		const auto treeDefault = configuration(nativeTree);
		for(auto version: { dwt::util::win32::SEVEN, dwt::util::win32::EIGHT,
			dwt::util::win32::EIGHT_ONE, dwt::util::win32::TEN, dwt::util::win32::ELEVEN }) {
			simulatedVersion = version;
			auto table = window->addChild(dwt::Table::Seed());
			auto tree = window->addChild(dwt::Tree::Seed());
			const auto tableConfig = configuration(table->handle());
			const auto treeConfig = configuration(tree->treeHandle());
			if(version < dwt::util::win32::EIGHT_ONE) {
				check(tableConfig.dwWant == tableDefault.dwWant && tableConfig.dwBlock == tableDefault.dwBlock,
					"Windows 7/8 table gesture behavior was changed");
				check(treeConfig.dwWant == treeDefault.dwWant && treeConfig.dwBlock == treeDefault.dwBlock,
					"Windows 7/8 tree gesture behavior was changed");
			} else {
				const DWORD wanted = GC_PAN | GC_PAN_WITH_SINGLE_FINGER_VERTICALLY |
					GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY | GC_PAN_WITH_INERTIA;
				check((tableConfig.dwWant & wanted) == wanted && !(tableConfig.dwBlock & wanted),
					"Windows 8.1+ table pan support missing");
				check((treeConfig.dwWant & wanted) == wanted && !(treeConfig.dwBlock & wanted),
					"Windows 8.1+ tree pan support missing");
			}
		}
		::DestroyWindow(window->handle());
		std::puts("Touch scrolling version gate: simulated Windows 7/8 unchanged; 8.1/10/11 enabled.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
