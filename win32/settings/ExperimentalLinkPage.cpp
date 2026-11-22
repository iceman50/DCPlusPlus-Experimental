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
#include <win32/settings/ExperimentalLinkPage.h>

#include <dwt/widgets/Button.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>

#include <win32/resource.h>
#include <win32/settings/base/SettingsDialog.h>
#include <win32/util/IconManager.h>

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;

ExperimentalLinkPage::ExperimentalLinkPage(dwt::Widget* parent) :
	PropPage(parent, 1, 1)
{
	setHelpId(IDH_EXPERIMENTALPAGE);
	grid->column(0).mode = GridInfo::FILL;

	auto group = grid->addChild(GroupBox::Seed(T_("Experimental")));
	group->setHelpId(IDH_EXPERIMENTALPAGE);
	auto cur = group->addChild(Grid::Seed(3, 1));
	cur->column(0).mode = GridInfo::FILL;
	cur->setSpacing(10);

	cur->addChild(Label::Seed(T_("Themes, icon and emoticon packages and the options DC++ Experimental adds\r\n"
		"have a window of their own: View > Experimental. Its tabs are Themes, Icons,\r\n"
		"Emoticons, Chat and sharing, Transfers and hashing, Private Send, Protocol limits\r\n"
		"and Other, which has the password lock and the country database.")));
	cur->addChild(Label::Seed(T_("Changes made there apply at once and are saved automatically.")));

	auto row = cur->addChild(Grid::Seed(1, 1));
	auto open = row->addChild(Button::Seed(T_("&Open the Experimental window")));
	open->setHelpId(IDH_EXPERIMENTALPAGE);
	open->setImage(IconManager::buttonIcon(IDI_EXPERT));
	// Settings is modal, so it closes first, keeping any changes as OK does.
	open->onClicked([this] { static_cast<SettingsDialog*>(getRoot())->closeAndOpenExperimental(); });
}
