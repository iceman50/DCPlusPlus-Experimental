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
#include <win32/util/ControlSeeds.h>

#include <win32/settings/DownloadPage.h>

#include <dcpp/settings/SettingsManager.h>

#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/Spinner.h>
#include <dwt/widgets/MessageBox.h>
#include <dwt/widgets/ComboBox.h>

#include <win32/resource.h>
#include <win32/frames/hublist/HubListsDlg.h>

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dwt::Spinner;

DownloadPage::DownloadPage(dwt::Widget* parent) :
PropPage(parent, 4, 1)
{
	setHelpId(IDH_DOWNLOADPAGE);

	grid->column(0).mode = GridInfo::FILL;

	{
		GroupBoxPtr group = grid->addChild(GroupBox::Seed(T_("Directories")));
		group->setHelpId(IDH_SETTINGS_DOWNLOAD_DOWNLOADDIR);

		GridPtr cur = group->addChild(Grid::Seed(4, 2));
		cur->column(0).mode = GridInfo::FILL;

		LabelPtr label = cur->addChild(Label::Seed(T_("Default download directory")));
		cur->setWidget(label, 0, 0, 1, 2);
		label->setHelpId(IDH_SETTINGS_DOWNLOAD_DOWNLOADDIR);

		TextBoxPtr box = cur->addChild(ControlSeeds::Dialog::textBox);
		items.emplace_back(box, SettingsManager::DOWNLOAD_DIRECTORY, PropPage::T_STR);
		box->setHelpId(IDH_SETTINGS_DOWNLOAD_DOWNLOADDIR);

		ButtonPtr browse = cur->addChild(Button::Seed(T_("Browse...")));
		browse->onClicked([this, box] { handleBrowseDir(box, SettingsManager::DOWNLOAD_DIRECTORY); });
		browse->setHelpId(IDH_SETTINGS_DOWNLOAD_DOWNLOADDIR);

		label = cur->addChild(Label::Seed(T_("Unfinished downloads directory")));
		cur->setWidget(label, 2, 0, 1, 2);
		label->setHelpId(IDH_SETTINGS_DOWNLOAD_TEMP_DOWNLOAD_DIRECTORY);

		box = cur->addChild(ControlSeeds::Dialog::textBox);
		items.emplace_back(box, SettingsManager::TEMP_DOWNLOAD_DIRECTORY, PropPage::T_STR);
		box->setHelpId(IDH_SETTINGS_DOWNLOAD_TEMP_DOWNLOAD_DIRECTORY);

		browse = cur->addChild(Button::Seed(T_("Browse...")));
		browse->onClicked([this, box] { handleBrowseDir(box, SettingsManager::TEMP_DOWNLOAD_DIRECTORY); });
		browse->setHelpId(IDH_SETTINGS_DOWNLOAD_TEMP_DOWNLOAD_DIRECTORY);
	}

	{
		GroupBoxPtr group = grid->addChild(GroupBox::Seed(T_("Limits")));
		group->setHelpId(IDH_SETTINGS_DOWNLOAD_LIMITS);

		GridPtr cur = group->addChild(Grid::Seed(2, 1));

		GridPtr cur2 = cur->addChild(Grid::Seed(2, 2));
		cur2->column(0).size = 40;
		cur2->column(0).mode = GridInfo::STATIC;

		TextBoxPtr box = cur2->addChild(ControlSeeds::Dialog::intTextBox);
		items.emplace_back(box, SettingsManager::DOWNLOAD_SLOTS, PropPage::T_INT_WITH_SPIN);
		box->setHelpId(IDH_SETTINGS_DOWNLOAD_DOWNLOADS);

		auto spin = cur2->addChild(Spinner::Seed(0, 100, box));
		cur2->setWidget(spin);
		spin->setHelpId(IDH_SETTINGS_DOWNLOAD_DOWNLOADS);

		cur2->addChild(Label::Seed(T_("Maximum simultaneous downloads (0 = infinite)")))->setHelpId(IDH_SETTINGS_DOWNLOAD_DOWNLOADS);

		box = cur2->addChild(ControlSeeds::Dialog::intTextBox);
		items.emplace_back(box, SettingsManager::MAX_DOWNLOAD_SPEED, PropPage::T_INT_WITH_SPIN);
		box->setHelpId(IDH_SETTINGS_DOWNLOAD_MAXSPEED);

		spin = cur2->addChild(Spinner::Seed(0, 10000, box));
		cur2->setWidget(spin);
		spin->setHelpId(IDH_SETTINGS_DOWNLOAD_MAXSPEED);

		cur2->addChild(Label::Seed(T_("No new downloads if speed exceeds (KiB/s, 0 = disable)")))->setHelpId(IDH_SETTINGS_DOWNLOAD_MAXSPEED);

		// xgettext:no-c-format
		cur->addChild(Label::Seed(T_("Note; because of changing download speeds, this is not 100% accurate...")))->setHelpId(IDH_SETTINGS_DOWNLOAD_LIMITS);
	}

	{
		GridPtr cur = grid->addChild(GroupBox::Seed(T_("Public Hubs list")))->addChild(Grid::Seed(4, 1));
		cur->column(0).mode = GridInfo::FILL;

		cur->addChild(Label::Seed(T_("Public Hubs list URL")));

		GridPtr cur2 = cur->addChild(Grid::Seed(1, 2));
		cur2->column(1).mode = GridInfo::FILL; 
		cur2->column(1).align = GridInfo::BOTTOM_RIGHT;
		cur2->addChild(Button::Seed(T_("Configure Public Hub Lists")))->onClicked([this] { handleConfigHubLists(); });

		auto resetList = cur2->addChild(Button::Seed(T_("Reset Public Hub Lists")));
		resetList->setHelpId(IDH_SETTINGS_DOWNLOAD_RESETLISTS);
		resetList->onClicked([this] { handleResetHubLists(); });

		cur->addChild(Label::Seed(T_("HTTP proxy (hub lists and GeoLite2)")))->setHelpId(IDH_SETTINGS_DOWNLOAD_PROXY);
		TextBoxPtr box = cur->addChild(ControlSeeds::Dialog::textBox);
		items.emplace_back(box, SettingsManager::HTTP_PROXY, PropPage::T_STR);
		box->setHelpId(IDH_SETTINGS_DOWNLOAD_PROXY);
	}

	{
		auto cur = grid->addChild(GroupBox::Seed(T_("Magnet links")))->addChild(Grid::Seed(2, 1));
		cur->column(0).mode = GridInfo::FILL;
		magnetAction = cur->addChild(ControlSeeds::Dialog::comboBox);
		magnetAction->addValue(T_("Ask each time"));
		magnetAction->addValue(T_("Always search"));
		magnetAction->addValue(T_("Always download complete links"));
		const auto action = SETTING(MAGNET_ACTION);
		magnetAction->setSelected(SETTING(MAGNET_ASK) ? 0 :
			action == SettingsManager::MAGNET_AUTO_SEARCH ? 1 : action == SettingsManager::MAGNET_AUTO_DOWNLOAD ? 2 : 0);
		cur->addChild(Label::Seed(T_("Incomplete links still ask. Downloads use the default directory.")));
	}
	PropPage::read(items);
}

DownloadPage::~DownloadPage() {
}

void DownloadPage::write() {
	PropPage::write(items);
	const auto action = magnetAction->getSelected();
	SettingsManager::getInstance()->set(SettingsManager::MAGNET_ASK, action <= 0);
	if(action > 0) SettingsManager::getInstance()->set(SettingsManager::MAGNET_ACTION,
		action == 2 ? SettingsManager::MAGNET_AUTO_DOWNLOAD : SettingsManager::MAGNET_AUTO_SEARCH);

	const string& s = SETTING(DOWNLOAD_DIRECTORY);
	if(s.length() > 0 && s[s.length() - 1] != '\\') {
		SettingsManager::getInstance()->set(SettingsManager::DOWNLOAD_DIRECTORY, s + '\\');
	}
	const string& t = SETTING(TEMP_DOWNLOAD_DIRECTORY);
	if(t.length() > 0 && t[t.length() - 1] != '\\') {
		SettingsManager::getInstance()->set(SettingsManager::TEMP_DOWNLOAD_DIRECTORY, t + '\\');
	}

}

void DownloadPage::handleConfigHubLists() {
	HubListsDlg(this).run();
}

void DownloadPage::handleResetHubLists() {
	if(dwt::MessageBox(this).show(T_("This will remove all existing hublist entries and reset to the default preconfigured ones. Do you really want to proceed?"), T_("Public Hubs list"), dwt::MessageBox::BOX_YESNO, dwt::MessageBox::BOX_ICONQUESTION) == IDYES) { 
		SettingsManager::getInstance()->unset(SettingsManager::HUBLIST_SERVERS);
	}
}
