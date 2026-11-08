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

struct ChatPaints {
	unsigned paints = 0;
	unsigned erases = 0;
};

LRESULT CALLBACK countPaints(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
	UINT_PTR, DWORD_PTR data) {
	auto& counts = *reinterpret_cast<ChatPaints*>(data);
	if(message == WM_PAINT) ++counts.paints;
	if(message == WM_ERASEBKGND) ++counts.erases;
	return ::DefSubclassProc(window, message, wParam, lParam);
}

void checkChatSwitch(dwt::Window* window, dwt::Application& app, dwt::Appearance::Mode mode) {
	app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
	// Keep a real drawable surface without displaying the fixture to the user.
	// Off-screen windows have an empty paint clip and skip background erasure.
	window->addRemoveExStyle(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, true);
	check(::SetLayeredWindowAttributes(window->handle(), 0, 0, LWA_ALPHA), "Cannot conceal chat paint fixture");
	window->resize(dwt::Rectangle(0, 0, 640, 400));
	window->setVisible(true);
	auto tabs = window->addChild(dwt::TabView::Seed());
	tabs->resize(dwt::Rectangle(0, 0, 620, 360));
	tabs->setVisible(true);
	dwt::Container* pages[2];
	dwt::RichTextBox* chats[2];
	ChatPaints paints[2];
	bool damageOnFocus = false;
	unsigned damageCount[2] {};
	COLORREF writtenPixel[2] {};
	unsigned overlappingShows = 0;
	constexpr COLORREF stalePixel = RGB(231, 17, 123);
	auto cornerPixel = [](dwt::RichTextBox* chat) {
		RECT rect {};
		::GetClientRect(chat->handle(), &rect);
		return POINT { rect.right - 40, rect.bottom - 10 };
	};
	// Remove callbacks while their captured state still exists, including on a
	// failed check, before dwtMain destroys the fixture's parent window.
	struct Cleanup {
		dwt::TabView* tabs;
		~Cleanup() { ::DestroyWindow(tabs->handle()); }
	} cleanup { tabs };
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
		pages[i]->onVisibilityChanged([&, i](bool visible) {
			if(visible && damageOnFocus && pages[1 - i]->getVisible()) ++overlappingShows;
		});
		pages[i]->onFocus([&, i] {
			if(!damageOnFocus) return;
			// Seed a stale surface after ShowWindow's incidental paint. The final
			// tab-switch repaint must replace it even where there is no text.
			pages[i]->redrawWindow(RDW_ALLCHILDREN | RDW_UPDATENOW);
			const auto point = cornerPixel(chats[i]);
			const RECT rect { point.x, point.y, point.x + 2, point.y + 2 };
			const auto dc = ::GetDC(chats[i]->handle());
			const auto brush = ::CreateSolidBrush(stalePixel);
			::FillRect(dc, &rect, brush);
			const auto pixel = ::GetPixel(dc, point.x, point.y);
			::DeleteObject(brush);
			::ReleaseDC(chats[i]->handle(), dc);
			++damageCount[i];
			writtenPixel[i] = pixel;
			paints[i] = {};
		});
	}
	// Warm both pages so subsequent switches do not rely on initial layout/resize.
	tabs->setActive(pages[1]);
	tabs->setActive(pages[0]);
	damageOnFocus = true;
	for(int pass = 0; pass < 12; ++pass) {
		const int next = (pass + 1) % 2;
		auto chat = chats[next];
		if(pass == 6) {
			// A short report with blank lines exposes retained pixels much more
			// readily than two chat histories covering the entire viewport.
			chat->setText(L"Second hub\n\n[ System Stats ]\n\nCPU: test\n\nRAM: test");
		}
		chat->setSelection(3, 9);
		dwt::Point scroll(0, 100);
		chat->setScrollPos(scroll);
		const auto selection = chat->getCaretPosRange();
		const auto previousScroll = chat->getScrollPos();
		const auto text = chat->getText();
		paints[0] = paints[1] = {};
		damageCount[next] = 0;
		writtenPixel[next] = CLR_INVALID;
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
		check(chat->getVisible(), "Selected chat is hidden; the paint check requires a visible test surface");
		check(overlappingShows == 0, "Incoming hub was shown while the previous hub was still visible");
		check(damageCount[next] != 0 && writtenPixel[next] == stalePixel, "Chat paint fixture has no drawable surface");
		check(paints[next].paints != 0, "Switched hub chat waits for another event before repainting");
		const auto point = cornerPixel(chat);
		const auto dc = ::GetDC(chat->handle());
		const auto pixel = ::GetPixel(dc, point.x, point.y);
		::ReleaseDC(chat->handle(), dc);
		check(pixel != CLR_INVALID && pixel != stalePixel, "Switching tabs retained stale pixels in blank chat space");
		check(paints[next].erases != 0, "Switched hub chat did not erase the previous tab's pixels");
		check(paints[1 - next].paints == 0, "Switching tabs repainted the hidden chat");
		check(chat->getText() == text, "Switching tabs changed chat history");
		check(chat->getCaretPosRange() == selection, "Switching tabs changed the chat selection");
		const auto currentScroll = chat->getScrollPos();
		check(currentScroll.x == previousScroll.x && currentScroll.y == previousScroll.y,
			"Switching tabs moved the chat viewport");
		paints[next] = {};
		tabs->setActive(pages[next]);
		check(paints[next].paints == 0, "Reselecting the active tab caused an unnecessary repaint");
		// Incoming traffic in a hidden tab must update only that tab's history
		// and leave the visible tab's pixels alone until the next activation.
		auto background = chats[1 - next];
		background->addTextSteadyBatch({ L"{\\urtf1 Background-only incoming message}" }, true);
		::UpdateWindow(background->handle());
		check(!background->getVisible(), "Background traffic exposed a hidden chat");
		check(background->getText().find(L"Background-only incoming message") != dwt::tstring::npos,
			"Hidden chat discarded an incoming message");
		check(paints[1 - next].paints == 0 && paints[next].paints == 0,
			"Background traffic repainted a chat while its tab was hidden");
		check(chat->getText() == text, "Background traffic changed the selected chat history");
	}
	for(auto chat: chats) ::RemoveWindowSubclass(chat->handle(), countPaints, 1);
	tabs->remove(pages[1]);
	tabs->remove(pages[0]);
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
	const auto originalTitle = page->getText();
	const dwt::tstring customTitle = L"My hub & \u2605";
	dwt::tstring windowTitle;
	tabs->onTitleChanged([&](const dwt::tstring& title) { windowTitle = title; });
	tabs->setTabText(page, customTitle);
	check(tabs->getTabText(page) == customTitle, "Custom tab label was not applied");
	check(page->getText() == originalTitle, "Custom tab label replaced the full hub title");
	page->setText(L"Updated hub description (adcs://example.invalid)");
	check(tabs->getTabText(page) == customTitle, "Hub title update replaced the custom tab label");
	check(windowTitle == page->getText(), "Custom tab label replaced the main window title");
	if(!ownerDraw) {
		wchar_t text[128] {};
		struct { TCITEM item { TCIF_TEXT }; ULONG_PTR extra[2] {}; } data;
		data.item.pszText = text;
		data.item.cchTextMax = 128;
		check(TabCtrl_GetItem(tabs->handle(), 0, &data.item), "Cannot read custom tab label");
		check(dwt::tstring(text) == L"My hub && \u2605", "Native tab lost the custom label or ampersand escaping");
	}
	paint(tabs);
	tabs->setTabText(page, L"");
	check(tabs->getTabText(page) == page->getText(), "Clearing the custom label did not restore the current hub title");
	page->setText(originalTitle);
	check(tabs->getTabText(page) == originalTitle, "Default tab label stopped following the hub title");
	tabs->onTitleChanged({});
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
		std::puts("PASS: tab switching, immediate chat paint/background erase, hidden traffic, viewport preservation and close handling");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
