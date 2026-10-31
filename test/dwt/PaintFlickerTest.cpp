// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Tree.h>
#include <dwt/widgets/Rebar.h>
#include <dwt/widgets/ToolBar.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/resources/Bitmap.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void checkPaint(HWND window, COLORREF background, bool buffered) {
	RECT bounds {};
	GetClientRect(window, &bounds);
	check(bounds.right > 0 && bounds.bottom > 0, "Empty paint probe");
	auto screen = GetDC(window);
	auto memory = CreateCompatibleDC(screen);
	dwt::Bitmap bitmap(CreateCompatibleBitmap(screen, bounds.right, bounds.bottom));
	auto previous = SelectObject(memory, bitmap.handle());
	const COLORREF sentinel = RGB(255, 0, 255);
	auto brush = CreateSolidBrush(sentinel);
	FillRect(memory, &bounds, brush);
	DeleteObject(brush);
	const auto erased = SendMessage(window, WM_ERASEBKGND, reinterpret_cast<WPARAM>(memory), 0);
	bool preserved = true;
	// Include the exposed left edge and the first band's grip area.
	for(int y = 0; y < bounds.bottom; ++y) {
		for(int x = 0; x < std::min(10L, bounds.right); ++x) {
			preserved &= GetPixel(memory, x, y) == sentinel;
		}
	}
	SendMessage(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
	const auto afterPaint = GetPixel(memory, 0, 0);
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(window, screen);
	check(erased != 0, "Control did not acknowledge its background erase");
	check(!buffered || preserved, "Background erase overwrote pixels before the buffered paint");
	check(afterPaint == background, "Buffered paint left an unpainted or incorrectly colored background");
}

void settle(dwt::Application& app) {
	for(unsigned i = 0; app.dispatch(); ++i) check(i < 2000, "Paint queue did not settle");
}

void checkComboBand(dwt::Rebar* rebar, dwt::ComboBox* combo, COLORREF background) {
	RECT bounds {}, child {};
	GetClientRect(rebar->handle(), &bounds);
	GetWindowRect(combo->handle(), &child);
	MapWindowPoints(nullptr, rebar->handle(), reinterpret_cast<POINT*>(&child), 2);
	COMBOBOXINFO info { sizeof(info) };
	GetComboBoxInfo(combo->handle(), &info);
	auto screen = GetDC(rebar->handle());
	auto memory = CreateCompatibleDC(screen);
	dwt::Bitmap bitmap(CreateCompatibleBitmap(screen, bounds.right, bounds.bottom));
	auto previous = SelectObject(memory, bitmap.handle());
	SendMessage(rebar->handle(), WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
	const auto color = GetPixel(memory, child.left + 5, child.top + info.rcButton.bottom + 5);
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(rebar->handle(), screen);
	check(color == background, "Combo band painted a surface-colored block below the closed dropdown");
}

void checkBandEdge(dwt::Rebar* rebar, COLORREF expected) {
	RECT bounds {}, band {};
	GetClientRect(rebar->handle(), &bounds);
	check(rebar->sendMessage(RB_GETRECT, 0, reinterpret_cast<LPARAM>(&band)), "Cannot read toolbar band bounds");
	auto screen = GetDC(rebar->handle());
	auto memory = CreateCompatibleDC(screen);
	dwt::Bitmap bitmap(CreateCompatibleBitmap(screen, bounds.right, bounds.bottom));
	auto previous = SelectObject(memory, bitmap.handle());
	SendMessage(rebar->handle(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
	const auto color = GetPixel(memory, band.right - 2, band.bottom - 1);
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(rebar->handle(), screen);
	check(color == expected, "Rebar band edge does not respect its border style");
}

}

int dwtMain(dwt::Application& app) {
	try {
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed(L"Buffered painting regression");
		seed.location = dwt::Rectangle(-32000, -32000, 650, 450);
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		ShowWindow(window->handle(), SW_SHOWNOACTIVATE);
		auto treeSeed = dwt::Tree::Seed();
		treeSeed.tvExStyle |= TVS_EX_DOUBLEBUFFER;
		auto tree = window->addChild(treeSeed);
		tree->resize(dwt::Rectangle(10, 120, 220, 220));
		auto item = tree->insert(L"Settings page", TVI_ROOT);
		tree->setSelected(item);
		auto rebar = window->addChild(dwt::Rebar::Seed());
		auto toolbar = dwt::WidgetCreator<dwt::ToolBar>::create(rebar, dwt::ToolBar::Seed());
		toolbar->addButton("test", -1, L"Test", true, 0, [] { });
		toolbar->setLayout({ "test" });
		toolbar->sendMessage(TB_SETBUTTONSIZE, 0, MAKELPARAM(100, 64));
		rebar->add(toolbar);
		auto comboSeed = dwt::ComboBox::Seed();
		comboSeed.style = (comboSeed.style & ~CBS_DROPDOWN) | CBS_DROPDOWNLIST;
		auto combo = dwt::WidgetCreator<dwt::ComboBox>::create(rebar, comboSeed);
		combo->addValue(L"Theme test");
		combo->setSelected(0);
		rebar->add(combo, RBBS_NOGRIPPER, L"Theme");
		rebar->refresh();
		check(GetWindowLongPtr(tree->handle(), GWL_STYLE) & WS_CLIPCHILDREN,
			"Tree wrapper background can erase over the native tree and header");
		settle(app);
		checkPaint(tree->treeHandle(), app.getAppearance().getPalette().background, true);
		checkPaint(rebar->handle(), app.getAppearance().getPalette().background, true);
		checkComboBand(rebar, combo, app.getAppearance().getPalette().background);
		checkBandEdge(rebar, app.getAppearance().getPalette().background);
		rebar->addRemoveStyle(RBS_BANDBORDERS, true);
		rebar->refresh();
		checkBandEdge(rebar, app.getAppearance().getPalette().border);
		rebar->addRemoveStyle(RBS_BANDBORDERS, false);
		rebar->refresh();
		checkBandEdge(rebar, app.getAppearance().getPalette().background);
		// Exercise resizing, header layout, selection and repeated theme changes.
		tree->addColumn(L"Pages", 160, dwt::Column::LEFT);
		for(int scenario = 0; scenario < 6; ++scenario) {
			const bool customLight = scenario >= 4;
			auto palette = dwt::Appearance::defaultPalette();
			if(customLight) {
				palette = { RGB(245, 245, 245), RGB(255, 255, 255), RGB(25, 25, 25),
					RGB(110, 110, 110), RGB(175, 175, 175), RGB(0, 90, 180), RGB(255, 255, 255) };
			}
			app.getAppearance().configure(!customLight && scenario % 2 ? dwt::Appearance::Mode::Dark : dwt::Appearance::Mode::Light,
				palette, customLight ? dwt::Appearance::Rendering::CustomPalette : dwt::Appearance::Rendering::Automatic);
			for(auto width : { 420, 700 }) {
				window->resize(dwt::Rectangle(-32000, -32000, width, 450));
				rebar->refresh();
				tree->resize(dwt::Rectangle(10, 120, width / 2, 220));
				tree->setSelected(item);
				settle(app);
				const bool manual = app.getAppearance().isManual();
				checkPaint(tree->treeHandle(), manual ? app.getAppearance().getPalette().background : GetSysColor(COLOR_WINDOW), true);
				if(manual) {
					checkPaint(rebar->handle(), app.getAppearance().getPalette().background, true);
					checkComboBand(rebar, combo, app.getAppearance().getPalette().background);
				}
			}
		}
		const auto explicitBackground = RGB(43, 51, 67);
		rebar->setColor(RGB(230, 230, 230), explicitBackground);
		checkPaint(rebar->handle(), explicitBackground, true);
		rebar->remove(toolbar);
		checkPaint(rebar->handle(), explicitBackground, true);
		std::puts("Buffered tree/rebar painting, tree child clipping, theme switches and resizing passed.");
		DestroyWindow(window->handle());
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
