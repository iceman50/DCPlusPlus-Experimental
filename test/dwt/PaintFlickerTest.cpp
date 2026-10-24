// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Tree.h>
#include <dwt/widgets/Rebar.h>
#include <dwt/widgets/ToolBar.h>
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
		rebar->add(toolbar);
		rebar->refresh();
		check(GetWindowLongPtr(tree->handle(), GWL_STYLE) & WS_CLIPCHILDREN,
			"Tree wrapper background can erase over the native tree and header");
		settle(app);
		checkPaint(tree->treeHandle(), app.getAppearance().getPalette().background, true);
		checkPaint(rebar->handle(), app.getAppearance().getPalette().background, true);
		// Exercise resizing, header layout, selection and repeated theme changes.
		tree->addColumn(L"Pages", 160, dwt::Column::LEFT);
		for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark,
			dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			for(auto width : { 420, 700 }) {
				window->resize(dwt::Rectangle(-32000, -32000, width, 450));
				rebar->refresh();
				tree->resize(dwt::Rectangle(10, 120, width / 2, 220));
				tree->setSelected(item);
				settle(app);
				const bool dark = mode == dwt::Appearance::Mode::Dark;
				checkPaint(tree->treeHandle(), dark ? app.getAppearance().getPalette().background : GetSysColor(COLOR_WINDOW), true);
				if(dark) checkPaint(rebar->handle(), app.getAppearance().getPalette().background, true);
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
