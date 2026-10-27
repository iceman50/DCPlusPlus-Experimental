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

#include <win32/appearance/ControlSeeds.h>
#include <win32/appearance/UiFonts.h>
#include <win32/forward.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/text/Text.h>
#include <dwt/util/win32/FileDialog.h>

const Button::Seed ControlSeeds::button;
const ComboBox::Seed ControlSeeds::comboBox;
const ComboBox::Seed ControlSeeds::comboBoxEdit;
const CheckBox::Seed ControlSeeds::checkBox;
const CheckBox::Seed ControlSeeds::splitCheckBox;
const GroupBox::Seed ControlSeeds::group;
const Label::Seed ControlSeeds::label;
const Menu::Seed ControlSeeds::menu;
const Table::Seed ControlSeeds::table;
const TextBox::Seed ControlSeeds::textBox;
const RichTextBox::Seed ControlSeeds::richTextBox;
const TabView::Seed ControlSeeds::tabs;
const Tree::Seed ControlSeeds::treeView;

const ComboBox::Seed ControlSeeds::Dialog::comboBox;
const ComboBox::Seed ControlSeeds::Dialog::comboBoxEdit;
const TextBox::Seed ControlSeeds::Dialog::textBox;
const TextBox::Seed ControlSeeds::Dialog::intTextBox;
const RichTextBox::Seed ControlSeeds::Dialog::richTextBox;
const Table::Seed ControlSeeds::Dialog::table;
const Table::Seed ControlSeeds::Dialog::optionsTable;
const CheckBox::Seed ControlSeeds::Dialog::checkBox;
const Button::Seed ControlSeeds::Dialog::button;

void ControlSeeds::init() {
	dwt::util::win32::setFileDialogDefaults({ UiFonts::font, [](const char* text) { return Text::toT(gettext(text)); } });
	// Const so that noone else will change them after they've been initialized
	Button::Seed& xbutton = const_cast<Button::Seed&> (ControlSeeds::button);
	ComboBox::Seed& xcomboBoxEdit = const_cast<ComboBox::Seed&> (ControlSeeds::comboBoxEdit);
	ComboBox::Seed& xcomboBox = const_cast<ComboBox::Seed&> (ControlSeeds::comboBox);
	CheckBox::Seed& xCheckBox = const_cast<CheckBox::Seed&> (ControlSeeds::checkBox);
	CheckBox::Seed& xSplitCheckBox = const_cast<CheckBox::Seed&> (ControlSeeds::splitCheckBox);
	GroupBox::Seed& xgroup = const_cast<GroupBox::Seed&> (ControlSeeds::group);
	Label::Seed& xlabel = const_cast<Label::Seed&> (ControlSeeds::label);
	Menu::Seed& xmenu = const_cast<Menu::Seed&> (ControlSeeds::menu);
	Table::Seed& xTable = const_cast<Table::Seed&> (ControlSeeds::table);
	TextBox::Seed& xtextBox = const_cast<TextBox::Seed&> (ControlSeeds::textBox);
	RichTextBox::Seed& xRichTextBox = const_cast<RichTextBox::Seed&> (ControlSeeds::richTextBox);
	TabView::Seed& xTabs = const_cast<TabView::Seed&> (ControlSeeds::tabs);
	Tree::Seed& xtreeView = const_cast<Tree::Seed&> (ControlSeeds::treeView);
	ComboBox::Seed& xdComboBox = const_cast<ComboBox::Seed&> (ControlSeeds::Dialog::comboBox);
	TextBox::Seed& xdTextBox = const_cast<TextBox::Seed&> (ControlSeeds::Dialog::textBox);
	TextBox::Seed& xdintTextBox = const_cast<TextBox::Seed&> (ControlSeeds::Dialog::intTextBox);
	RichTextBox::Seed& xdRichTextBox = const_cast<RichTextBox::Seed&> (ControlSeeds::Dialog::richTextBox);
	Table::Seed& xdTable = const_cast<Table::Seed&> (ControlSeeds::Dialog::table);
	Table::Seed& xdoptionsTable = const_cast<Table::Seed&> (ControlSeeds::Dialog::optionsTable);

	xbutton.font = UiFonts::font;

	xcomboBox.style |= CBS_DROPDOWNLIST;
	xcomboBox.font = UiFonts::font;

	xcomboBoxEdit.font = UiFonts::font;

	xCheckBox.font = UiFonts::font;

	xSplitCheckBox = xCheckBox;
	xSplitCheckBox.caption = _T("+/-");
	xSplitCheckBox.style &= ~WS_TABSTOP;

	xgroup.font = UiFonts::font;

	xlabel.font = UiFonts::font;

	// LibDWT automatically owner-draws existing menus while a manual
	// appearance is active. Keep this seed as the user's independent request so
	// returning to the system appearance can restore native menu painting.
	xmenu.ownerDrawn = SETTING(OWNER_DRAWN_MENUS);
	xmenu.font = UiFonts::font;

	xTable.style |= WS_HSCROLL | WS_VSCROLL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS;
	xTable.exStyle = WS_EX_CLIENTEDGE;
	xTable.lvStyle |= LVS_EX_HEADERDRAGDROP | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP;
	xTable.font = UiFonts::font;

	xtextBox.exStyle = WS_EX_CLIENTEDGE;
	xtextBox.font = UiFonts::font;

	xRichTextBox.exStyle = WS_EX_CLIENTEDGE;
	xRichTextBox.font = UiFonts::font;

	xTabs.font = UiFonts::font;

	xtreeView.style |= TVS_HASBUTTONS | TVS_LINESATROOT;
	xtreeView.exStyle = WS_EX_CLIENTEDGE;
	xtreeView.font = UiFonts::font;

	xdComboBox.style |= CBS_DROPDOWNLIST;

	xdTextBox.style |= ES_AUTOHSCROLL;

	xdintTextBox = xdTextBox;
	xdintTextBox.style |= ES_NUMBER;

	xdRichTextBox.style |= ES_READONLY | ES_SUNKEN;
	xdRichTextBox.exStyle = WS_EX_CLIENTEDGE;

	xdTable.style |= WS_HSCROLL | WS_VSCROLL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER;
	xdTable.exStyle = WS_EX_CLIENTEDGE;
	xdTable.lvStyle |= LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP;

	xdoptionsTable = xdTable;
	xdoptionsTable.style |= LVS_SINGLESEL | LVS_NOCOLUMNHEADER;
	xdoptionsTable.lvStyle |= LVS_EX_CHECKBOXES;

	xtextBox.menuSeed = ControlSeeds::menu;
	xdTextBox.menuSeed = ControlSeeds::menu;
	xRichTextBox.menuSeed = ControlSeeds::menu;
	xdRichTextBox.menuSeed = ControlSeeds::menu;
}