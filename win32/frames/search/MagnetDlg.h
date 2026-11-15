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

#ifndef DCPLUSPLUS_WIN32_MAGNET_DLG_H
#define DCPLUSPLUS_WIN32_MAGNET_DLG_H

#include <dcpp/core/classes/Magnet.h>
#include <win32/dialog/GridDialog.h>

class MagnetDlg : public GridDialog {
public:
	enum class Action { Cancel, Search, Download };
	MagnetDlg(dwt::Widget* parent, const dcpp::Magnet::Metadata& metadata, const string& target);
	Action getAction() const { return action; }
	const string& getTarget() const { return target; }
	bool rememberChoice() const { return remembered; }

private:
	dcpp::Magnet::Metadata metadata;
	string target;
	Action action = Action::Cancel;
	bool remembered = false;
	TextBoxPtr destination = nullptr;
	CheckBoxPtr remember = nullptr;
	bool handleInitDialog();
	void choose(Action selected);
};

#endif
