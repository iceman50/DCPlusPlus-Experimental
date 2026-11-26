/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdafx.h"
#include <win32/frames/experimental/ExperimentalFrame.h>

#include <dcpp/hub/ClientManager.h>
#include <dcpp/message/EmoticonManager.h>
#include <dcpp/private_chat/MessageOutboxManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/transfer/private/DirectTransferManager.h>
#include <dcpp/util/Util.h>

#include <dwt/Application.h>
#include <dwt/Appearance.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/TabView.h>

#include <win32/MainWindow.h>
#include <win32/RichTextBox.h>
#include <win32/components/ComposedPaint.h>
#include <win32/components/ContextHelp.h>
#include <win32/frames/experimental/EmoticonsTab.h>
#include <win32/frames/experimental/ExperimentalTabs.h>
#include <win32/frames/experimental/IconsTab.h>
#include <win32/frames/experimental/ThemesTab.h>
#include <win32/frames/hub/HubFrame.h>
#include <win32/frames/private_chat/PrivateFrame.h>
#include <win32/util/AppAppearance.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/IconManager.h>
#include <win32/util/UiFonts.h>
#include <win32/util/UiServices.h>

using dwt::Grid;
using dwt::GridInfo;

const string ExperimentalFrame::id = "Experimental";
const string& ExperimentalFrame::getId() const { return id; }

ExperimentalFrame::ExperimentalFrame(TabViewPtr parent) :
	BaseType(parent, T_("Experimental"), IDH_EXPERIMENTALPAGE, IDI_EXPERT, false),
	live([this](int setting) { changed(setting); }),
	grid(nullptr),
	tabs(nullptr),
	help(nullptr),
	savePending(false),
	lastFont(UiFonts::font ? UiFonts::font->handle() : nullptr)
{
	// Settings may have changed elsewhere while another window was in front. Shown before the
	// ComposedPaint below draws the tables for the window's first paint, so it draws them as shown.
	onVisibilityChanged([this](bool visible) {
		if(visible && composed) {
			composed->begin();
			refresh();
		}
	});

	// What is built below is painted in one step once it is laid out, and contents filled in
	// asynchronously with it. (Usually the window is built hidden and shown afterwards.)
	composed = std::make_unique<ComposedPaint>(this);
	ComposedPaint::Hold hold(*composed, true);

	grid = addChild(Grid::Seed(2, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->row(0).mode = GridInfo::FILL;
	grid->row(0).align = GridInfo::STRETCH;
	grid->setSpacing(6);

	{
		auto seed = ControlSeeds::tabs;
		seed.style &= ~(TCS_OWNERDRAWFIXED | TCS_MULTILINE | TCS_RAGGEDRIGHT | TCS_TOOLTIPS);
		seed.exStyle |= WS_EX_CONTROLPARENT;
		seed.widthConfig = 0;
		seed.closeable = false;
		tabs = grid->addChild(seed);
		tabs->setHelpId(IDH_EXPERIMENTALPAGE);
		// Another tab appears in one step, not control by control over what is left of the
		// previous one.
		tabs->onSwitching([this] { return std::make_shared<ComposedPaint::Hold>(*composed, true); });

		// In the order of Page. Each tab creates its controls when it is first shown.
		pages.push_back(new ThemesTab(tabs, live));
		pages.push_back(new IconsTab(tabs, live));
		pages.push_back(new EmoticonsTab(tabs, live));
		pages.push_back(new ChatSharingTab(tabs, live));
		pages.push_back(new TransfersTab(tabs, live));
		pages.push_back(new PrivateSendTab(tabs, live));
		pages.push_back(new ProtocolLimitsTab(tabs, live));
		pages.push_back(new OtherTab(tabs, live));
	}

	{
		// The help for the option with the focus, as in the Settings dialog.
		Grid::Seed seed(1, 1);
		seed.style |= WS_BORDER;
		auto helpGrid = grid->addChild(seed);
		helpGrid->column(0).mode = GridInfo::FILL;

		auto helpSeed = ControlSeeds::Dialog::richTextBox;
		helpSeed.style &= ~ES_SUNKEN;
		helpSeed.exStyle &= ~WS_EX_CLIENTEDGE;
		helpSeed.lines = 4;
		help = helpGrid->addChild(helpSeed);
		help->setAccessibleName(T_("Help for the selected option"));
		AppAppearance::setColor(help);
		help->onRaw([](WPARAM key, LPARAM) -> LRESULT {
			return key == VK_TAB || key == VK_RETURN ? 0 : DLGC_WANTMESSAGE;
		}, dwt::Message(WM_GETDLGCODE));
	}

	contextHelp = std::make_unique<ContextHelp>(this, help);
	for(auto page: pages) {
		page->onBuilt([this, page] { contextHelp->watch(page->handle()); });
		page->buildWhenShown();
	}

	initStatus();
	status->setText(STATUS_STATUS, T_("Changes apply at once and are saved automatically."));

	// The window is laid out first, so the tab in front is built at its final size and laid out
	// once.
	layout();
	tabs->setActive(pages.front());
	pages.front()->ensureBuilt();

	// Once the window is up, build the other tabs while it is idle, so switching to one is quick.
	setTimer([this] {
		prebuildNext();
		return false;
	}, prebuildDelay, TIMER_PREBUILD);
}

void ExperimentalFrame::prebuildNext() {
	for(auto page: pages) {
		if(!page->isBuilt()) {
			page->ensureBuilt();
			// Laid out at the size it will have when shown, so that showing it needs no layout.
			if(auto active = tabs->getActive()) {
				RECT bounds;
				::GetWindowRect(active->handle(), &bounds);
				::MapWindowPoints(nullptr, tabs->handle(), reinterpret_cast<POINT*>(&bounds), 2);
				page->resize(dwt::Rectangle(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top));
				page->layout();
			}
			// Asynchronous calls run only when no message waits, so input and painting come first.
			callAsync([this] { prebuildNext(); });
			return;
		}
	}
}

void ExperimentalFrame::layoutActivePage() {
	if(auto active = tabs->getActive()) {
		active->layout();
	}
}

ExperimentalFrame::~ExperimentalFrame() {
}

void ExperimentalFrame::layout() {
	if(!grid || !status) {
		return;
	}
	// Resizing moves and repaints every option; the window is painted once it is all done. A drag
	// of the border resizes many times over, each painted in one step. Being shown lays out too,
	// at the same size, which moves nothing.
	const auto size = getClientSize();
	std::unique_ptr<ComposedPaint::Hold> hold;
	if(size.x != laidOut.x || size.y != laidOut.y) {
		hold = std::make_unique<ComposedPaint::Hold>(*composed, false);
		laidOut = size;
	}

	dwt::Rectangle r(size);
	r.size.y -= status->refresh();
	grid->resize(dwt::Rectangle(r.left() + 4, r.top() + 4, std::max<LONG>(0, r.width() - 8),
		std::max<LONG>(0, r.height() - 8)));
}

bool ExperimentalFrame::preClosing() {
	return true;
}

void ExperimentalFrame::postClosing() {
	// A field still waiting for its typing pause applies now, and nothing waits to be saved.
	live.commitAll();
	setTimer(std::function<bool ()>(), 0, TIMER_SAVE);
	save();
}

void ExperimentalFrame::openPage(TabViewPtr parent, Page page) {
	openWindow(parent, false);
	if(frame && page < static_cast<Page>(frame->pages.size())) {
		frame->tabs->setActive(frame->pages[page]);
	}
}

void ExperimentalFrame::settingsChanged() {
	if(frame) {
		frame->refresh();
	}
}

void ExperimentalFrame::refresh() {
	live.reload();
	const bool fonts = fontsChanged();
	for(auto page: pages) {
		if(!page->isBuilt()) {
			continue;
		}
		if(fonts) {
			page->invalidateLayout();
		}
		page->refresh();
	}
	if(fonts) {
		layoutActivePage();
	}
}

bool ExperimentalFrame::fontsChanged() {
	const auto font = UiFonts::font ? UiFonts::font->handle() : nullptr;
	if(font == lastFont) {
		return false;
	}
	lastFont = font;
	return true;
}

void ExperimentalFrame::changed(int setting) {
	unsigned effects = 0;
	switch(setting) {
	case SettingsManager::THEMED_DIALOG_BORDERS:
		dwt::Application::instance().getAppearance().setDialogBorders(SETTING(THEMED_DIALOG_BORDERS));
		break;
	case SettingsManager::THEME_NATIVE_DARK:
		AppAppearance::refreshTheme();
		break;
	case SettingsManager::ENABLE_RICH_TEXT:
	case SettingsManager::ENABLE_RTF_TEMP_SHARES:
		effects = EFFECT_CHAT;
		break;
	case SettingsManager::ENABLE_PRIVATE_SEND:
		effects = EFFECT_CHAT | EFFECT_PRIVATE_SEND;
		break;
	case SettingsManager::PFT_REQUIRE_VERIFICATION:
	case SettingsManager::PFT_OFFER_MINUTES:
	case SettingsManager::PFT_TRANSFER_MINUTES:
		effects = EFFECT_PRIVATE_SEND;
		break;
	case SettingsManager::OUTBOX_MAX_ENTRIES:
	case SettingsManager::OUTBOX_MAX_MESSAGE_BYTES:
	case SettingsManager::OUTBOX_EXPIRY_DAYS:
		effects = EFFECT_OUTBOX;
		break;
	case SettingsManager::HUB_USER_ICON_SIZE:
		effects = EFFECT_USER_ICONS;
		break;
	case SettingsManager::ENABLE_MESSAGE_OUTBOX:
		effects = EFFECT_OUTBOX_SWITCH;
		break;
	case SettingsManager::ICON_PACK:
		effects = EFFECT_ICONS;
		break;
	case SettingsManager::ENABLE_EMOTICONS:
	case SettingsManager::EMOTICON_PACK:
	case SettingsManager::EMOTICON_SIZE:
	case SettingsManager::EMOTICON_BIT_DEPTH:
		effects = EFFECT_EMOTICONS;
		break;
	case SettingsManager::GEOIP_DOWNLOAD_URL:
	case SettingsManager::GEOIP_USE_MAXMIND:
	case SettingsManager::MAXMIND_ACCOUNT_ID:
	case SettingsManager::MAXMIND_LICENSE_KEY:
		effects = EFFECT_GEOIP;
		break;
	case SettingsManager::CCPM_RECONNECT_BASE_DELAY:
	case SettingsManager::CCPM_RECONNECT_MAX_DELAY:
		// The longest delay is never shorter than the first one.
		if(SETTING(CCPM_RECONNECT_MAX_DELAY) < SETTING(CCPM_RECONNECT_BASE_DELAY)) {
			SettingsManager::getInstance()->set(SettingsManager::CCPM_RECONNECT_MAX_DELAY,
				SETTING(CCPM_RECONNECT_BASE_DELAY));
			live.reload();
		}
		break;
	}
	apply(effects);

	// A theme that changed the font changes the size of every option.
	if(setting == SettingsManager::THEME_MODE && fontsChanged()) {
		for(auto page: pages) {
			if(page->isBuilt()) {
				page->invalidateLayout();
			}
		}
		layoutActivePage();
	}

	// Typing pauses already batch edits; this batches a burst of clicks into one write.
	savePending = true;
	setTimer([this] {
		save();
		return false;
	}, savePause, TIMER_SAVE);
}

void ExperimentalFrame::apply(unsigned effects) {
	if(effects & EFFECT_CHAT) {
		if(auto clients = ClientManager::getInstance()) {
			clients->infoUpdated();
		}
		HubFrame::refreshRichTextSettings();
		PrivateFrame::refreshRichTextSettings();
	}
	if((effects & EFFECT_PRIVATE_SEND) && DirectTransferManager::available()) {
		DirectTransferManager::getInstance()->configure();
	}
	if((effects & EFFECT_OUTBOX) && MessageOutboxManager::available()) {
		MessageOutboxManager::getInstance()->configure();
	}
	if(effects & EFFECT_USER_ICONS) {
		HubFrame::refreshUserIconSizes();
	}
	if((effects & EFFECT_GEOIP) && UiServices::mainWindow) {
		UiServices::mainWindow->geoSettingsChanged();
	}
	if((effects & EFFECT_OUTBOX_SWITCH) && MessageOutboxManager::available()) {
		// Starts or stops the outbox's sender.
		MessageOutboxManager::getInstance()->setEnabled(SETTING(ENABLE_MESSAGE_OUTBOX));
	}
	if(effects & EFFECT_ICONS) {
		// Windows opened from now on use the new icons.
		IconManager::reload();
	}
	if(effects & EFFECT_EMOTICONS) {
		EmoticonManager::reload();
	}
}

void ExperimentalFrame::save() {
	if(!savePending) {
		return;
	}
	savePending = false;
	SettingsManager::getInstance()->save();
	if(status) {
		status->setText(STATUS_STATUS, str(TF_("Changes apply at once; saved at %1%") %
			Text::toT(Util::getShortTimeString())));
	}
}
