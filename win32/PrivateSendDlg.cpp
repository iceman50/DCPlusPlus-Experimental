/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stdafx.h"

#include "PrivateSendDlg.h"

#include "ParamDlg.h"
#include "WinUtil.h"

#include <dcpp/ClientManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/ScopedFunctor.h>
#include <dcpp/format.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/LoadDialog.h>
#include <dwt/widgets/MessageBox.h>
#include <dwt/widgets/SaveDialog.h>

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dcpp::DirectTransferManager;

namespace {

const ColumnInfo columns[] = {
	{ N_("File"), 230, false },
	{ N_("Status"), 230, false },
	{ N_("Progress"), 180, true }
};
}

PrivateSendDlg::PrivateSendDlg(dwt::Widget* parent, const HintedUser& user, const string& transfer) :
	GridDialog(parent, 680),
	peer(user),
	initialSelection(transfer),
	table(0),
	status(0),
	readiness(0),
	details(0),
	statistics(0),
	send(0),
	primary(0),
	secondary(0),
	detailsButton(0),
	detailsVisible(false),
	refreshing(false),
	layingOut(false)
{
	onInitDialog([this] { return init(); });
	onWindowPosChanged([this](const dwt::Rectangle&) {
		if(grid)
			relayout();
	});
}

void PrivateSendDlg::relayout() {
	if(layingOut)
		return;

	// A first resize may switch between native and custom caption metrics.
	// Recompute against the resulting client frame before positioning controls.
	layingOut = true;
	layout();
	layout();
	layingOut = false;
}

bool PrivateSendDlg::init() {
	setText(T_("Private Send") + _T(" - ") + WinUtil::getNicks(peer.user->getCID()));

	grid = addChild(Grid::Seed(7, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->addChild(Label::Seed(SETTING(PFT_REQUIRE_VERIFICATION) ?
		T_("Verify this contact before sending or accepting files. Contact settings are under Details.") :
		T_("Testing mode: contact verification is disabled.\nConnections are encrypted; contact identity is not verified.")));

	{
		auto controls = grid->addChild(Grid::Seed(1, 3));
		controls->column(1).mode = GridInfo::FILL;
		send = controls->addChild(Button::Seed(T_("&Send file...")));
		send->onClicked([this] { act([this] { sendFile(); }); });
		controls->addChild(Label::Seed());
		detailsButton = controls->addChild(Button::Seed(T_("&Details >>")));
		detailsButton->onClicked([this] { toggleDetails(); });
	}

	readiness = grid->addChild(Label::Seed());
	grid->row(2).mode = GridInfo::STATIC;
	grid->row(2).size = 40;
	grid->row(2).align = GridInfo::STRETCH;

	{
		auto seed = WinUtil::Seeds::Dialog::table;
		seed.exStyle &= ~WS_EX_CLIENTEDGE;
		seed.style |= LVS_SINGLESEL;
		table = grid->addChild(seed);
		WinUtil::makeColumns(table, columns, std::size(columns));
		grid->row(3).mode = GridInfo::STATIC;
		grid->row(3).size = 210;
		grid->row(3).align = GridInfo::STRETCH;
		table->onGetEmptyText([] { return T_("No private transfers. Choose Send file... to get started."); });
		table->onSelectionChanged([this] {
			if(!refreshing)
				updateActions();
		});
	}

	{
		details = grid->addChild(Grid::Seed(4, 1));
		details->column(0).mode = GridInfo::FILL;
		statistics = details->addChild(Label::Seed());
		details->row(0).mode = GridInfo::STATIC;
		details->row(0).size = 60;
		details->row(0).align = GridInfo::STRETCH;

		auto identity = details->addChild(WinUtil::Seeds::Dialog::textBox);
		identity->setReadOnly(true);
		identity->setText(Text::toT("Your CID: " + ClientManager::getInstance()->getMyCID().toBase32()));
		auto fingerprint = details->addChild(WinUtil::Seeds::Dialog::textBox);
		fingerprint->setReadOnly(true);
		fingerprint->setText(Text::toT("Your SHA-256: " + DirectTransferManager::getInstance()->ownFingerprint()));

		auto contact = details->addChild(Grid::Seed(1, 2));
		contact->addChild(Button::Seed(T_("&Verify contact...")))->onClicked([this] {
			act([this] { verifyContact(); });
		});
		contact->addChild(Button::Seed(T_("&Forget contact...")))->onClicked([this] {
			act([this] {
				if(dwt::MessageBox(this).show(T_("Forget this contact and cancel their private transfers?"),
					T_("Forget contact"), dwt::MessageBox::BOX_YESNO, dwt::MessageBox::BOX_ICONQUESTION) == IDYES)
					DirectTransferManager::getInstance()->forgetPeer(peer);
			});
		});
		details->setVisible(false);
		grid->row(4).mode = GridInfo::STATIC;
		grid->row(4).size = 0;
		grid->row(4).align = GridInfo::STRETCH;
	}

	{
		auto actions = grid->addChild(Grid::Seed(1, 4));
		actions->column(2).mode = GridInfo::FILL;
		primary = actions->addChild(Button::Seed(T_("&Accept...")));
		primary->onClicked([this] { act([this] { primaryAction(); }); });
		secondary = actions->addChild(Button::Seed(T_("&Decline")));
		secondary->onClicked([this] { act([this] { secondaryAction(); }); });
		actions->addChild(Label::Seed());
		actions->addChild(Button::Seed(T_("Close")))->onClicked([this] { endDialog(IDOK); });
	}

	status = grid->addChild(Label::Seed());

	refresh();
	setTimer([this] { return refresh(); }, 1000);
	relayout();
	centerWindow();

	return false;
}

void PrivateSendDlg::toggleDetails() {
	detailsVisible = !detailsVisible;
	details->setVisible(detailsVisible);
	grid->row(4).mode = detailsVisible ? GridInfo::AUTO : GridInfo::STATIC;
	detailsButton->setText(detailsVisible ? T_("&Details <<") : T_("&Details >>"));
	relayout();
}

void PrivateSendDlg::updateActions() {
	using State = DirectTransferManager::State;
	const auto index = table->getSelected();
	const auto row = index >= 0 && static_cast<size_t>(index) < rows.size() ? &rows[index] : nullptr;
	const auto offered = row && row->incoming && row->state == State::Offered && row->remainingSeconds;
	const auto pause = row && (row->state == State::Connecting || row->state == State::Sending || row->state == State::Receiving);
	const auto resume = row && row->canResume;
	const auto active = row && row->state < State::Completed;

	const auto primaryText = offered ? T_("&Accept...") : resume ? T_("&Resume") : T_("&Pause");
	const auto secondaryText = offered ? T_("&Decline") : T_("&Cancel transfer");
	const auto primaryVisible = offered || pause || resume;
	const auto changed = primary->getText() != primaryText || secondary->getText() != secondaryText ||
		primary->getVisible() != primaryVisible || secondary->getVisible() != active;
	if((!primaryVisible && ::GetFocus() == primary->handle()) || (!active && ::GetFocus() == secondary->handle()))
		table->setFocus();
	primary->setText(primaryText);
	primary->setVisible(primaryVisible);
	primary->setEnabled(primaryVisible);
	secondary->setText(secondaryText);
	secondary->setVisible(active);
	secondary->setEnabled(active);
	if(changed)
		relayout();

	tstring text = T_("Select a transfer to see its details.");
	if(row) {
		text = str(TF_("%1%: %2% / %3%\nSpeed: %4%/s   Expires in: %5%\n%6%") %
			(row->incoming ? T_("Incoming") : T_("Outgoing")) % Text::toT(Util::formatBytes(row->bytes)) %
			Text::toT(Util::formatBytes(row->size)) % Text::toT(Util::formatBytes(row->bytesPerSecond)) %
			(row->state < State::Completed ? Text::toT(std::to_string(row->remainingSeconds) + " s") : _T("-")) %
			(row->state == State::Paused && !row->incoming ? T_("Waiting for the recipient to resume.") : Text::toT(row->status)));
	}
	if(statistics->getText() != text)
		statistics->setText(text);
	const auto location = row && row->incoming && !row->destinationPath.empty() ?
		str(TF_("Save to: %1%") % Text::toT(row->destinationPath)) :
		str(TF_("Default download folder: %1%") % Text::toT(SETTING(DOWNLOAD_DIRECTORY)));
	if(status->getText() != location)
		status->setText(location);
}

void PrivateSendDlg::primaryAction() {
	using State = DirectTransferManager::State;
	const auto id = selected();
	const auto row = rows[table->getSelected()];
	if(row.incoming && row.state == State::Offered) {
		auto target = Text::toT(dcpp::pft::defaultDownloadPath(row.name));
		if(dwt::SaveDialog(this).setTitle(str(TF_("Accept private file (%1%) - Save As") % Text::toT(Util::formatBytes(row.size))))
			.addFilter(T_("All files"), _T("*.*")).open(target, OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT))
			DirectTransferManager::getInstance()->accept(id, Text::fromT(target));
	} else if(row.incoming && row.state == State::Paused) {
		DirectTransferManager::getInstance()->resume(id);
	} else {
		DirectTransferManager::getInstance()->pause(id);
	}
}

void PrivateSendDlg::secondaryAction() {
	const auto id = selected();
	const auto& row = rows[table->getSelected()];
	if(row.incoming && row.state == DirectTransferManager::State::Offered)
		DirectTransferManager::getInstance()->decline(id);
	else
		DirectTransferManager::getInstance()->cancel(id);
}

std::string PrivateSendDlg::selected() const {
	const auto index = table->getSelected();
	if(index < 0 || static_cast<size_t>(index) >= rows.size())
		throw dcpp::pft::ProtocolException("Select a private transfer first");

	return rows[index].id;
}

void PrivateSendDlg::act(const std::function<void()>& action) {
	actionError.clear();
	try {
		action();
	} catch(const Exception& e) {
		actionError = Text::toT(e.getError());
	} catch(const std::exception& e) {
		actionError = Text::toT(e.what());
	} catch(...) {
		actionError = T_("Private Send could not complete the action.");
	}
	refresh();
	if(!actionError.empty())
		dwt::MessageBox(this).show(actionError, T_("Private Send"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
}

bool PrivateSendDlg::refresh() {
	refreshing = true;
	ScopedFunctor([this] { refreshing = false; });
	tstring message;
	bool ready = false;
	try {
		DirectTransferManager::getInstance()->checkSend(peer);
		ready = true;
		message = SETTING(PFT_REQUIRE_VERIFICATION) ?
			T_("Contact verified. Choose Send file... to offer a file; the recipient must accept it.") :
			T_("Ready for testing. Choose Send file... to offer a file; the recipient must accept it.");
	} catch(const Exception& e) {
		message = Text::toT(e.getError());
	}
	send->setEnabled(ready);
	if(!actionError.empty())
		message = actionError;
	if(readiness->getText() != message)
		readiness->setText(message);

	string id;
	id.swap(initialSelection);
	const auto selectedRow = table->getSelected();
	if(selectedRow >= 0 && static_cast<size_t>(selectedRow) < rows.size())
		id = rows[selectedRow].id;
	auto next = DirectTransferManager::getInstance()->snapshot(peer);
	const bool rebuild = rows.size() != next.size() ||
		!std::equal(rows.begin(), rows.end(), next.begin(), [](const auto& a, const auto& b) { return a.id == b.id; });
	rows = std::move(next);

	if(rebuild)
		table->clear();
	for(size_t i = 0; i < rows.size(); ++i) {
		const auto& r = rows[i];
		const TStringList cells = {
			Text::toT(r.name),
			(r.incoming ? T_("Incoming: ") : T_("Outgoing: ")) + Text::toT(r.status),
			r.state == DirectTransferManager::State::Preparing ? _T("-") :
				str(TF_("%1$.1f%% of %2%") % r.percent % Text::toT(Util::formatBytes(r.size)))
		};

		if(rebuild)
			table->insert(cells);
		else
			for(size_t col = 0; col < cells.size(); ++col)
				if(table->getText(i, col) != cells[col])
					table->setText(i, col, cells[col]);

		if(rebuild && r.id == id)
			table->select(static_cast<int>(i));
	}

	if(table->getSelected() < 0 && !rows.empty()) {
		auto pending = std::find_if(rows.begin(), rows.end(), [](const auto& row) {
			return row.incoming && row.state == DirectTransferManager::State::Offered;
		});
		table->select(pending == rows.end() ? 0 : static_cast<int>(pending - rows.begin()));
	}
	updateActions();

	return true;
}

void PrivateSendDlg::sendFile() {
	DirectTransferManager::getInstance()->checkSend(peer);

	tstring path;
	if(dwt::LoadDialog(this).setTitle(T_("Choose a file to send privately")).open(path)) {
		const auto id = DirectTransferManager::getInstance()->offer(peer, Text::fromT(path));
		refresh();
		for(size_t i = 0; i < rows.size(); ++i)
			if(rows[i].id == id) {
				table->select(static_cast<int>(i));
				table->ensureVisible(static_cast<int>(i));
				break;
			}
	}
}

void PrivateSendDlg::verifyContact() {
	DirectTransferManager::getInstance()->peerFingerprint(peer);

	ParamDlg dialog(this, T_("Verify Private Send contact"));
	dialog.addTextBox(T_("Paste peer CID received through an independent trusted channel"));
	dialog.addTextBox(T_("Paste SHA-256 received through an independent trusted channel"));
	if(dialog.run() == IDOK) {
		if(Text::fromT(dialog.getValues()[0]) != peer.user->getCID().toBase32())
			throw dcpp::pft::ProtocolException("The independently verified CID does not match this contact");

		DirectTransferManager::getInstance()->approvePeer(peer, Text::fromT(dialog.getValues()[1]));
	}
}
