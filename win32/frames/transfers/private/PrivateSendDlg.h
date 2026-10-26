/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DCPLUSPLUS_PRIVATE_SEND_DLG_H
#define DCPLUSPLUS_PRIVATE_SEND_DLG_H

#include <win32/dialogs/GridDialog.h>

#include <dcpp/transfer/private/DirectTransferManager.h>

/** A private queue view. Closing it leaves accepted transfers running. */
class PrivateSendDlg :
	public GridDialog
{
public:
	PrivateSendDlg(dwt::Widget* parent, const dcpp::HintedUser& peer, const std::string& transfer = std::string());

private:
	dcpp::HintedUser peer;
	std::string initialSelection;
	TablePtr table;
	LabelPtr status;
	LabelPtr readiness;
	GridPtr details;
	LabelPtr statistics;
	ButtonPtr send;
	ButtonPtr primary;
	ButtonPtr secondary;
	ButtonPtr detailsButton;
	bool detailsVisible;
	bool refreshing;
	tstring actionError;
	std::vector<dcpp::DirectTransferManager::View> rows;
	bool layingOut;

	void relayout();
	bool init();
	bool refresh();
	void act(const std::function<void()>& action);
	std::string selected() const;
	void sendFile();
	void verifyContact();
	void updateActions();
	void primaryAction();
	void secondaryAction();
	void toggleDetails();
};

#endif
