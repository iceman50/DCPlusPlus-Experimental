// Copyright (C) 2026 iceman50
// Noninteractive regression checks for tab switching and close-button handling.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Container.h>
#include <dwt/widgets/RichTextBox.h>

#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void paint(dwt::TabView* tabs) {
	if(!tabs->hasStyle(TCS_OWNERDRAWFIXED) && !dwt::Application::instance().getAppearance().isManual()) return;
	// Exercise the shared tab renderer without showing a window. Reserve space
	// for the extra item data installed by TabCtrl_SetItemExtra.
	struct {
		TCITEM item { TCIF_PARAM };
		ULONG_PTR extra[2] { };
	} data;
	check(TabCtrl_GetItem(tabs->handle(), 0, &data.item), "Cannot read tab item");
	DRAWITEMSTRUCT draw { };
	draw.CtlType = ODT_TAB;
	draw.itemState = ODS_SELECTED;
	draw.hwndItem = tabs->handle();
	draw.itemData = reinterpret_cast<ULONG_PTR>(&data.item.lParam);
	check(TabCtrl_GetItemRect(tabs->handle(), 0, &draw.rcItem), "Cannot read drawing bounds");
	draw.hDC = ::CreateCompatibleDC(nullptr);
	check(draw.hDC != nullptr, "Cannot create drawing context");
	dwt::TabView::handlePainting(draw);
	::DeleteDC(draw.hDC);
}

void click(dwt::TabView* tabs, POINT down, POINT up, WPARAM keys = 0) {
	::SendMessage(tabs->handle(), WM_LBUTTONDOWN, MK_LBUTTON | keys, MAKELPARAM(down.x, down.y));
	::SendMessage(tabs->handle(), WM_LBUTTONUP, keys, MAKELPARAM(up.x, up.y));
}

LRESULT CALLBACK countPaints(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
	UINT_PTR, DWORD_PTR data) {
	if(message == WM_PAINT) ++*reinterpret_cast<unsigned*>(data);
	return ::DefSubclassProc(window, message, wParam, lParam);
}

void checkChatSwitch(dwt::Window* window, dwt::Application& app, dwt::Appearance::Mode mode) {
	app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
	window->resize(dwt::Rectangle(-32000, -32000, 640, 400));
	window->setVisible(true);
	auto tabs = window->addChild(dwt::TabView::Seed());
	tabs->resize(dwt::Rectangle(0, 0, 620, 360));
	tabs->setVisible(true);
	dwt::Container* pages[2];
	dwt::RichTextBox* chats[2];
	unsigned paints[2] {};
	for(int i = 0; i < 2; ++i) {
		auto seed = dwt::Container::Seed();
		seed.style &= ~WS_VISIBLE;
		pages[i] = dwt::WidgetCreator<dwt::Container>::create(tabs, seed);
		pages[i]->setText(i ? L"Second hub" : L"First hub");
		tabs->add(pages[i]);
		// Hub chats are descendants of layout containers, not direct tab children.
		auto panel = pages[i]->addChild(dwt::Container::Seed());
		panel->resize(dwt::Rectangle(0, 0, 580, 300));
		chats[i] = panel->addChild(dwt::RichTextBox::Seed());
		chats[i]->resize(dwt::Rectangle(0, 0, 560, 260));
		chats[i]->addTextSteadyBatch(std::vector<dwt::tstring>(80,
			i ? L"{\\urtf1 Second hub chat}" : L"{\\urtf1 First hub chat}"), true);
		chats[i]->setReadOnly();
		check(::SetWindowSubclass(chats[i]->handle(), countPaints, 1,
			reinterpret_cast<DWORD_PTR>(&paints[i])), "Cannot observe chat repaints");
	}
	// Warm both pages so subsequent switches do not rely on initial layout/resize.
	tabs->setActive(pages[1]);
	tabs->setActive(pages[0]);
	for(int pass = 0; pass < 6; ++pass) {
		const int next = (pass + 1) % 2;
		auto chat = chats[next];
		chat->setSelection(3, 9);
		dwt::Point scroll(0, 100);
		chat->setScrollPos(scroll);
		const auto selection = chat->getCaretPosRange();
		const auto previousScroll = chat->getScrollPos();
		const auto text = chat->getText();
		paints[0] = paints[1] = 0;
		// No message pumping: the selected chat must paint even while other UI
		// work is queued, without requiring a scroll, resize or incoming message.
		if(pass % 3 == 0) {
			RECT rect {};
			check(TabCtrl_GetItemRect(tabs->handle(), next, &rect), "Cannot find hub tab header");
			POINT title { rect.left + tabs->scale(20), (rect.top + rect.bottom) / 2 };
			click(tabs, title, title);
		} else if(pass % 3 == 1) {
			tabs->next();
		} else {
			tabs->setActive(pages[next]);
		}
		check(tabs->getActive() == pages[next], "Hub tab activation selected the wrong page");
		check(chat->getVisible(), "Selected chat is hidden; the paint check requires a visible off-screen window");
		check(paints[next] != 0, "Switched hub chat waits for another event before repainting");
		check(paints[1 - next] == 0, "Switching tabs repainted the hidden chat");
		check(chat->getText() == text, "Switching tabs changed chat history");
		check(chat->getCaretPosRange() == selection, "Switching tabs changed the chat selection");
		const auto currentScroll = chat->getScrollPos();
		check(currentScroll.x == previousScroll.x && currentScroll.y == previousScroll.y,
			"Switching tabs moved the chat viewport");
		paints[next] = 0;
		tabs->setActive(pages[next]);
		check(paints[next] == 0, "Reselecting the active tab caused an unnecessary repaint");
	}
	for(auto chat: chats) ::RemoveWindowSubclass(chat->handle(), countPaints, 1);
	tabs->remove(pages[1]);
	tabs->remove(pages[0]);
	::DestroyWindow(tabs->handle());
	window->setVisible(false);
}

void checkTabs(dwt::Window* window, dwt::Application& app, bool ownerDraw, bool closeable) {
	auto& appearance = app.getAppearance();
	appearance.configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
	auto seed = dwt::TabView::Seed();
	if(!ownerDraw) seed.style &= ~TCS_OWNERDRAWFIXED;
	seed.widthConfig = ownerDraw ? 180 : 0;
	seed.closeable = closeable;
	auto tabs = window->addChild(seed);
	tabs->resize(dwt::Rectangle(0, 0, 600, 300));
	tabs->setVisible(true);
	auto page = dwt::WidgetCreator<dwt::Container>::create(tabs);
	page->setText(_T("Tab close regression"));
	int closeRequests = 0;
	page->onClosing([&] { ++closeRequests; return false; });
	tabs->add(page);
	paint(tabs);

	RECT rect { };
	check(TabCtrl_GetItemRect(tabs->handle(), 0, &rect), "Cannot read tab bounds");
	POINT close { rect.right - tabs->scale(12), (rect.top + rect.bottom) / 2 };
	POINT title { rect.left + tabs->scale(20), close.y };
	click(tabs, close, title);
	check(closeRequests == 0, "Releasing outside the close button closed the tab");
	click(tabs, title, close);
	check(closeRequests == 0, "Pressing outside the close button closed the tab");
	click(tabs, title, title);
	check(closeRequests == 0, "Clicking the title closed the tab");
	click(tabs, close, close);
	check(closeRequests == (closeable ? 1 : 0), "Dark-mode close button did not respect closeable");

	if(!ownerDraw) {
		closeRequests = 0;
		appearance.configure(dwt::Appearance::Mode::Light, dwt::Appearance::defaultPalette());
		paint(tabs);
		click(tabs, close, close);
		check(closeRequests == 0, "Native light-mode tab retained an invisible close button");
		appearance.configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		paint(tabs);
		click(tabs, close, close);
		check(closeRequests == (closeable ? 1 : 0), "Close button failed after a live theme switch");
	}
	closeRequests = 0;
	click(tabs, title, title, MK_SHIFT);
	check(closeRequests == (closeable ? 1 : 0), "Shift-click close behavior changed");
	tabs->remove(page);
	::DestroyWindow(tabs->handle());
}
}

int dwtMain(dwt::Application& app) {
	auto window = new dwt::Window();
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	// Consume the launcher's initial ShowWindow override before the paint test
	// explicitly shows the fixture off screen.
	window->setVisible(false);
	int result = 0;
	try {
		check(!app.getAppearance().isHighContrast(), "Run this check with Windows high contrast disabled");
		checkTabs(window, app, false, true);
		checkTabs(window, app, true, true);
		checkTabs(window, app, false, false);
		checkTabs(window, app, true, false);
		checkChatSwitch(window, app, dwt::Appearance::Mode::Light);
		checkChatSwitch(window, app, dwt::Appearance::Mode::Dark);
		std::puts("PASS: tab clicks, adjacent/programmatic switching, immediate chat paint, viewport preservation and close handling");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
