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

#ifndef DCPLUSPLUS_WIN32_CONTROLSEEDS_H
#define DCPLUSPLUS_WIN32_CONTROLSEEDS_H

#include <dwt/widgets/Button.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Tree.h>
#include <win32/RichTextBox.h>
#include <win32/forward.h>
struct ControlSeeds {
	static const Button::Seed button;
	static const ComboBox::Seed comboBox;
	static const ComboBox::Seed comboBoxEdit;
	static const CheckBox::Seed checkBox;
	static const CheckBox::Seed splitCheckBox; // +/-
	static const GroupBox::Seed group;
	static const Label::Seed label;
	static const Menu::Seed menu;
	static const Table::Seed table;
	static const TextBox::Seed textBox;
	static const RichTextBox::Seed richTextBox;
	static const TabView::Seed tabs;
	static const Tree::Seed treeView;

	struct Dialog {
		static const ComboBox::Seed comboBox;
		static const ComboBox::Seed comboBoxEdit;
		static const TextBox::Seed textBox;
		static const TextBox::Seed intTextBox;
		static const RichTextBox::Seed richTextBox;
		static const Table::Seed table;
		static const Table::Seed optionsTable;
		static const CheckBox::Seed checkBox;
		static const Button::Seed button;
	};
	static void init();
};

#endif
