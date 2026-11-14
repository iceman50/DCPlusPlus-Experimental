// Copyright (C) 2026 iceman50
// Live tab sizing must preserve pages and restore labels after truncation.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Container.h>

#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

dwt::tstring nativeLabel(dwt::TabView* tabs, int index) {
	wchar_t text[256] {};
	struct { TCITEM item { TCIF_TEXT }; ULONG_PTR extra[2] {}; } data;
	data.item.pszText = text;
	data.item.cchTextMax = 256;
	check(TabCtrl_GetItem(tabs->handle(), index, &data.item), "Cannot read tab label");
	return text;
}

void checkWidth(dwt::Window* window, bool ownerDraw, bool buttons) {
	auto seed = dwt::TabView::Seed();
	seed.widthConfig = ownerDraw ? 100 : 10;
	if(!ownerDraw) seed.style &= ~TCS_OWNERDRAWFIXED;
	if(buttons) seed.style |= TCS_BUTTONS;
	auto tabs = window->addChild(seed);
	tabs->resize(dwt::Rectangle(0, 0, tabs->scale(650), tabs->scale(400)));
	dwt::Container* pages[4];
	for(auto& page : pages) {
		page = dwt::WidgetCreator<dwt::Container>::create(tabs);
		page->setText(L"A long hub title with its description and address");
		tabs->add(page);
	}
	const dwt::tstring custom = L"My custom & favorite hub label";
	tabs->setTabText(pages[0], custom);
	tabs->setActive(pages[1]);
	tabs->mark(pages[0]);
	const auto originalArea = tabs->getClientSize();
	const auto originalRows = TabCtrl_GetRowCount(tabs->handle());
	RECT originalRect {};
	check(TabCtrl_GetItemRect(tabs->handle(), 0, &originalRect), "Cannot read initial tab bounds");
	const auto truncated = ownerDraw ? dwt::tstring() : nativeLabel(tabs, 0);
	unsigned activations = 0;
	tabs->onTitleChanged([&](const dwt::tstring&) { ++activations; });

	tabs->setWidthConfig(ownerDraw ? 300 : 100);
	RECT wideRect {};
	check(TabCtrl_GetItemRect(tabs->handle(), 0, &wideRect), "Cannot read resized tab bounds");
	check(wideRect.right - wideRect.left > originalRect.right - originalRect.left,
		"Increasing width did not resize existing tabs");
	check(TabCtrl_GetRowCount(tabs->handle()) > originalRows, "Wider tabs did not wrap into more rows");
	check(tabs->getClientSize().height() < originalArea.height(), "Page area did not follow the new tab rows");
	check(pages[1]->getWindowSize().y == tabs->getClientSize().height(), "Active page was not resized immediately");
	check(tabs->getActive() == pages[1] && tabs->size() == 4 && activations == 0,
		"Resizing changed tab selection or recreated pages");
	check(tabs->getTabText(pages[0]) == custom, "Resizing discarded the custom hub label");
	if(!ownerDraw) {
		check(nativeLabel(tabs, 0) == L"My custom && favorite hub label", "Widening did not restore the full custom label");
		check(nativeLabel(tabs, 1) == pages[1]->getText(), "Widening did not restore the full page title");
	}

	// This is also the operation used when canceling the Settings dialog.
	tabs->setWidthConfig(ownerDraw ? 100 : 10);
	check(TabCtrl_GetRowCount(tabs->handle()) == originalRows, "Restoring width did not restore row count");
	check(tabs->getClientSize() == originalArea, "Restoring width did not restore the page area");
	if(!ownerDraw) {
		check(nativeLabel(tabs, 0) == truncated, "Restoring width changed title truncation");
		tabs->setWidthConfig(3);
		check(nativeLabel(tabs, 0) == L"My custom && favorite hub label", "Unlimited native title width was not restored");
	}
	tabs->onTitleChanged({});
	for(auto page : pages) tabs->remove(page);
	::DestroyWindow(tabs->handle());
}
}

int dwtMain(dwt::Application& app) {
	auto window = new dwt::Window();
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	int result = 0;
	try {
		for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			for(bool ownerDraw : { false, true }) {
				for(bool buttons : { false, true }) checkWidth(window, ownerDraw, buttons);
			}
		}
		std::puts("PASS: live tab width, row layout, selection, custom labels and width restoration in light/dark modes");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
