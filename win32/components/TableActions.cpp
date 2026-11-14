/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
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

#include <win32/components/TableActions.h>
#include <dcpp/core/header/format.h>
#include <win32/util/ClipboardActions.h>
#include <win32/forward.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/Table.h>

namespace TableActions {

void addCopyMenu(Menu* menu, dwt::TablePtr table) {
	if(!table->hasSelected()) { return; }

	menu->appendSeparator();
	menu = menu->appendPopup(T_("Copy"));

	auto cols = table->getColumns();
	auto order = table->getColumnOrder();

	auto copyF = [table](unsigned col) -> std::function<void ()> { return [=] {
		tstring text;
		for(auto row: table->getSelection()) {
			if(!text.empty()) { text += _T("\r\n"); }
			text += table->getText(row, col);
		}
		ClipboardActions::setClipboard(text);
	}; };
	for(auto col: order) {
		menu->appendItem(cols[col].header, copyF(col));
	}

	menu->appendSeparator();
	menu->appendItem(T_("All columns"), [=] {
		tstring text;
		for(auto row: table->getSelection()) {
			tstring rowText;
			for(auto col: order) {
				if(!rowText.empty()) { rowText += _T("\r\n"); }
				rowText += str(TF_("%1%: %2%") % cols[col].header % table->getText(row, col));
			}
			if(!text.empty()) { text += _T("\r\n\r\n"); }
			text += move(rowText);
		}
		ClipboardActions::setClipboard(text);
	});
}

} // namespace TableActions
