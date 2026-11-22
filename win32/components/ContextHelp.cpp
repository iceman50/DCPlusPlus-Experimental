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
#include <win32/components/ContextHelp.h>

#include <dwt/WidgetCreator.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/ToolTip.h>

#include <win32/RichTextBox.h>
#include <win32/util/HelpService.h>

ContextHelp::ContextHelp(dwt::Control* owner, RichTextBoxPtr box_) :
	box(box_),
	tip(nullptr),
	lastTableHelpId(-1)
{
	/* A hidden tooltip decides when help is wanted, so there are no timers to manage; on
	 * TTN_SHOW it is hidden and the rich help tooltip shown in its place. */
	tip = dwt::WidgetCreator<dwt::ToolTip>::create(owner, dwt::ToolTip::Seed());
	tip->setDelay(TTDT_AUTOPOP, tip->getDelay(TTDT_AUTOPOP) * 3);
	tip->setDelay(TTDT_INITIAL, tip->getDelay(TTDT_INITIAL) + 1000);
	tip->setDelay(TTDT_RESHOW, tip->getDelay(TTDT_INITIAL));

	tip->onRaw([this](WPARAM, LPARAM lParam) -> LRESULT {
		auto pos = tip->getWindowRect().pos;
		::SetWindowPos(tip->handle(), 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOACTIVATE);
		auto& info = *reinterpret_cast<LPNMTTDISPINFO>(lParam);
		auto widget = dwt::hwnd_cast<dwt::Control*>(reinterpret_cast<HWND>(info.hdr.idFrom));
		if(widget) {
			HelpService::helpTooltip(widget, pos);
		}
		return TRUE;
	}, dwt::Message(WM_NOTIFY, TTN_SHOW));

	tip->onRaw([](WPARAM, LPARAM) -> LRESULT {
		HelpService::killHelpTooltip();
		return 0;
	}, dwt::Message(WM_NOTIFY, TTN_POP));
}

void ContextHelp::watch(HWND root) {
	::EnumChildWindows(root, watchChild, reinterpret_cast<LPARAM>(this));
}

BOOL CALLBACK ContextHelp::watchChild(HWND hwnd, LPARAM lParam) {
	auto help = reinterpret_cast<ContextHelp*>(lParam);
	auto widget = dwt::hwnd_cast<dwt::Control*>(hwnd);
	if(!widget || hwnd == help->box->handle() || hwnd == help->tip->handle()) {
		return TRUE;
	}

	widget->onFocus([help, widget] { help->show(widget); });

	auto table = dynamic_cast<TablePtr>(widget);
	if(table) {
		table->onSelectionChanged([help, widget] { help->show(widget); });
		// A table's rows may have help IDs of their own.
		table->onMouseMove([help, table](const dwt::MouseEvent&) -> bool {
			const auto id = static_cast<int>(table->getHelpId());
			if(id != help->lastTableHelpId) {
				help->lastTableHelpId = id;
				help->tip->refresh();
			}
			return false;
		});
	}

	// Any text will do: the tooltip itself is never shown (see TTN_SHOW).
	help->tip->addTool(widget, const_cast<LPTSTR>(_T("M")));
	return TRUE;
}

void ContextHelp::show(dwt::Control* widget) {
	const auto text = Text::toT(HelpService::getHelpText(widget->getHelpId()).second);
	if(box->getText() != text) {
		box->setText(text);
	}
}
