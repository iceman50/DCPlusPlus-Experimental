/*
 * Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef DCPLUSPLUS_MESSAGE_OUTBOX_DLG_H
#define DCPLUSPLUS_MESSAGE_OUTBOX_DLG_H
#include <dwt/widgets/ModalDialog.h>
#include <dcpp/private_chat/MessageOutbox.h>
#include <win32/forward.h>

class MessageOutboxDlg : public dwt::ModalDialog {
public:
	explicit MessageOutboxDlg(dwt::Widget* parent);
	int run();
private:
	GridPtr grid = nullptr;
	TablePtr list = nullptr;
	TextBoxPtr preview = nullptr;
	LabelPtr status = nullptr;
	ButtonPtr remove = nullptr, retry = nullptr;
	std::vector<dcpp::MessageOutbox::Entry> entries;
	bool refreshing = false;
	bool init();
	void refresh();
	void selectionChanged();
	void action(bool retryMessage);
	int64_t selectedId() const;
	void layout();
};
#endif
