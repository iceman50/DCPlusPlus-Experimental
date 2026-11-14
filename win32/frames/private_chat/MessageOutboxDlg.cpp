/*
 * Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "stdafx.h"
#include <win32/frames/private_chat/MessageOutboxDlg.h>
#include <win32/util/ControlSeeds.h>
#include <win32/components/HoldRedraw.h>
#include <dcpp/private_chat/MessageOutboxManager.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/TextBox.h>

using dwt::Grid;
using dwt::GridInfo;

namespace {
tstring stateText(dcpp::MessageOutbox::State state) {
	switch(state) {
	case dcpp::MessageOutbox::QUEUED: return T_("Pending");
	case dcpp::MessageOutbox::SENDING: return T_("Sending");
	case dcpp::MessageOutbox::SENT: return T_("Sent (receipt unconfirmed)");
	case dcpp::MessageOutbox::UNCERTAIN: return T_("Needs review (may be sent)");
	case dcpp::MessageOutbox::EXPIRED: return T_("Expired");
	}
	return T_("Unknown");
}
}

MessageOutboxDlg::MessageOutboxDlg(dwt::Widget* parent) : dwt::ModalDialog(parent) {
	onInitDialog([this] { return init(); });
}

int MessageOutboxDlg::run() {
	create(Seed(dwt::Point(850, 480), WS_THICKFRAME));
	return show();
}

bool MessageOutboxDlg::init() {
	setText(T_("Message outbox"));
	grid = addChild(Grid::Seed(6, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->row(3).mode = GridInfo::FILL;
	grid->row(3).align = GridInfo::STRETCH;
	grid->row(4).mode = GridInfo::STATIC;
	grid->row(4).size = grid->scale(85);
	grid->row(4).align = GridInfo::STRETCH;
	auto enable = grid->addChild(dwt::CheckBox::Seed(T_("Enable offline messages (save locally and send automatically)")));
	enable->setChecked(MessageOutboxManager::getInstance()->isEnabled());
	enable->onClicked([this, enable] {
		MessageOutboxManager::getInstance()->setEnabled(enable->getChecked());
		refresh();
	});
	grid->addChild(dwt::Label::Seed(T_("Messages are stored as local text. Limits and expiry: Settings > Experimental > Interface. NMDC uses nickname and hub.")));
	status = grid->addChild(dwt::Label::Seed());
	auto seed = ControlSeeds::Dialog::table;
	seed.style |= LVS_SINGLESEL;
	list = grid->addChild(seed);
	list->addColumn(T_("Status"), 180);
	list->addColumn(T_("Recipient"), 120);
	list->addColumn(T_("Original hub"), 180);
	list->addColumn(T_("Route"), 120);
	list->addColumn(T_("Created"), 140);
	list->addColumn(T_("Message"), 280);
	list->onSelectionChanged([this] { if(!refreshing) selectionChanged(); });
	auto textSeed = ControlSeeds::Dialog::textBox;
	textSeed.style |= ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL;
	preview = grid->addChild(textSeed);
	auto buttons = grid->addChild(Grid::Seed(1, 4));
	buttons->column(2).mode = GridInfo::FILL;
	remove = buttons->addChild(dwt::Button::Seed(T_("Cancel / remove")));
	remove->onClicked([this] { action(false); });
	retry = buttons->addChild(dwt::Button::Seed(T_("Retry selected...")));
	retry->onClicked([this] { action(true); });
	buttons->addChild(dwt::Label::Seed());
	auto closeSeed = dwt::Button::Seed(T_("Close"));
	closeSeed.menuHandle = reinterpret_cast<HMENU>(IDCANCEL);
	buttons->addChild(closeSeed)->onClicked([this] { endDialog(IDCANCEL); });
	onSized([this](const dwt::SizedEvent&) { layout(); });
	layout();
	selectionChanged();
	refresh();
	setTimer([this] { refresh(); return true; }, 2000);
	centerWindow();
	return false;
}

void MessageOutboxDlg::layout() {
	const auto rect = getContentRect();
	grid->resize(dwt::Rectangle(rect.left() + 6, rect.top() + 6, rect.width() - 12, rect.height() - 12));
}

int64_t MessageOutboxDlg::selectedId() const {
	const auto i = list->getNext(-1, LVNI_SELECTED);
	return i >= 0 && static_cast<size_t>(i) < entries.size() ? entries[i].id : 0;
}

void MessageOutboxDlg::refresh() {
	try {
		auto manager = MessageOutboxManager::getInstance();
		auto error = manager->getError();
		status->setText(!error.empty() ? T_("Outbox stopped: ") + Text::toT(error) :
			manager->isEnabled() ? T_("Sending automatically when the original route is ready. Sent does not confirm receipt.") :
			T_("Paused. Enable offline messages to queue and send messages."));
		auto id = selectedId();
		auto snapshot = manager->getStore().list();
		// Avoid resetting selection, scroll position and message preview on idle ticks.
		if(snapshot.size() == entries.size() && std::equal(snapshot.begin(), snapshot.end(), entries.begin(),
			[](const auto& a, const auto& b) { return a.id == b.id && a.state == b.state && a.expires == b.expires; }))
		{
			return;
		}
		HoldRedraw redraw { list };
		refreshing = true;
		list->clear();
		entries = std::move(snapshot);
		for(const auto& e: entries) {
			const auto row = list->insert({stateText(e.state), Text::toT(e.nick), Text::toT(e.hub),
				e.direct ? T_("Direct encrypted") : T_("Via original hub"),
				Text::toT(Util::getTimeString(e.created, "%Y-%m-%d %H:%M")), Text::toT(e.text)});
			if(e.id == id) list->select(row);
		}
		refreshing = false;
		selectionChanged();
	} catch(const std::exception& e) {
		refreshing = false;
		status->setText(T_("Outbox unavailable: ") + Text::toT(e.what()));
	}
}

void MessageOutboxDlg::selectionChanged() {
	const auto i = list->getNext(-1, LVNI_SELECTED);
	const auto e = i >= 0 && static_cast<size_t>(i) < entries.size() ? &entries[i] : nullptr;
	remove->setEnabled(e && e->state != MessageOutbox::SENDING);
	retry->setEnabled(e && (e->state == MessageOutbox::UNCERTAIN || e->state == MessageOutbox::EXPIRED));
	preview->setText(e ? Text::toT(e->text) : Util::emptyStringT);
}

void MessageOutboxDlg::action(bool retryMessage) {
	const auto id = selectedId();
	if(!id) return;
	if(retryMessage && ::MessageBox(handle(), T_("The recipient may already have received this message. Retrying can send a duplicate. Retry?").c_str(),
		T_("Retry message").c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
	try {
		auto& store = MessageOutboxManager::getInstance()->getStore();
		const auto changed = retryMessage ? store.retry(id, time(nullptr)) : store.erase(id);
		if(!changed) ::MessageBox(handle(), T_("The message state changed. A message already being sent cannot be cancelled.").c_str(), T_("Message outbox").c_str(), MB_OK);
		refresh();
	} catch(const std::exception& e) {
		::MessageBox(handle(), Text::toT(e.what()).c_str(), T_("Message outbox").c_str(), MB_OK | MB_ICONERROR);
	}
}
