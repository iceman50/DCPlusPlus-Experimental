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

#ifndef DCPLUSPLUS_WIN32_DIALOGCONTROLS_H
#define DCPLUSPLUS_WIN32_DIALOGCONTROLS_H

#include <dwt/widgets/Button.h>
#include <dwt/forward.h>
#include <utility>

namespace DialogControls {
std::pair<dwt::ButtonPtr, dwt::ButtonPtr> addDlgButtons(dwt::GridPtr grid);
template<typename Tok, typename Tcancel> std::pair<dwt::ButtonPtr, dwt::ButtonPtr> addDlgButtons(dwt::GridPtr grid, Tok ok, Tcancel cancel) {
    auto buttons = addDlgButtons(grid);
    buttons.first->onClicked(ok);
    buttons.second->onClicked(cancel);
    return buttons;
}
dwt::ButtonPtr addHelpButton(dwt::GridPtr grid);
void addSearchIcon(dwt::TextBoxPtr box);
void addFilterMethods(dwt::ComboBoxPtr box);
void fillTriboolCombo(dwt::ComboBoxPtr box);
void preventSpaces(dwt::TextBoxPtr box);
}

#endif
