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
#include <dcpp/util/Util.h>
#include <win32/appearance/ControlSeeds.h>
#include <win32/controls/DialogControls.h>
#include <win32/services/HelpService.h>
#include <win32/frames/favorites/FavHubProperties.h>

#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/hub/HubEntry.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/version.h>

#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/MessageBox.h>

#include <win32/resource.h>
#include <win32/frames/favorites/FavHubGroupsDlg.h>
#include <win32/frames/hub/HubFrame.h>
#include <win32/controls/HoldRedraw.h>

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;

std::map<UINT, std::wstring> FavHubProperties::encodings;

FavHubProperties::FavHubProperties(dwt::Widget* parent, FavoriteHubEntry *_entry) :
GridDialog(parent, 620, DS_CONTEXTHELP),
name(0),
address(0),
hubDescription(0),
nick(0),
password(0),
description(0),
email(0),
userIp(0),
userIp6(0),
encoding(0),
showJoins(0),
favShowJoins(0),
logMainChat(0),
userIconSize(nullptr),
groups(0),
defaultShare(0),
shareFolders(0),
updatingShareFolders(false),
entry(_entry)
{
	onInitDialog([this] { return handleInitDialog(); });
	onHelp(&HelpService::help);
}

FavHubProperties::~FavHubProperties() {
}

bool FavHubProperties::handleInitDialog() {
	setHelpId(IDH_FAVORITE_HUB);

	grid = addChild(Grid::Seed(6, 2));
	grid->column(0).mode = GridInfo::FILL;
	grid->column(1).mode = GridInfo::FILL;

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Hub")));
		grid->setWidget(group, 0, 0, 1, 2);

		bool isAdcHub = Util::isAdcUrl(entry->getServer()) || Util::isAdcsUrl(entry->getServer());

		int rows = isAdcHub ? 3 : 4;

		auto cur = group->addChild(Grid::Seed(rows, 2));
		cur->column(0).align = GridInfo::BOTTOM_RIGHT;
		cur->column(1).mode = GridInfo::FILL;

		cur->addChild(Label::Seed(T_("Name")))->setHelpId(IDH_FAVORITE_HUB_NAME);
		name = cur->addChild(ControlSeeds::Dialog::textBox);
		name->setText(Text::toT(entry->getName()));
		name->setHelpId(IDH_FAVORITE_HUB_NAME);

		cur->addChild(Label::Seed(T_("Address")))->setHelpId(IDH_FAVORITE_HUB_ADDRESS);
		address = cur->addChild(ControlSeeds::Dialog::textBox);
		address->setText(Text::toT(entry->getServer()));
		address->setHelpId(IDH_FAVORITE_HUB_ADDRESS);
		DialogControls::preventSpaces(address);

		cur->addChild(Label::Seed(T_("Description")))->setHelpId(IDH_FAVORITE_HUB_DESC);
		hubDescription = cur->addChild(ControlSeeds::Dialog::textBox);
		hubDescription->setText(Text::toT(entry->getHubDescription()));
		hubDescription->setHelpId(IDH_FAVORITE_HUB_DESC);

		if(!isAdcHub)
		{
			cur->addChild(Label::Seed(T_("Encoding")))->setHelpId(IDH_FAVORITE_HUB_ENCODING);
			encoding = cur->addChild(ControlSeeds::Dialog::comboBox);
			encoding->setHelpId(IDH_FAVORITE_HUB_ENCODING);

			fillEncodings();
		}
	}

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Identification (leave blank for defaults)")));
		grid->setWidget(group, 1, 0, 1, 2);

		auto cur = group->addChild(Grid::Seed(6, 2));
		cur->column(0).align = GridInfo::BOTTOM_RIGHT;
		cur->column(1).mode = GridInfo::FILL;

		cur->addChild(Label::Seed(T_("Nick")))->setHelpId(IDH_FAVORITE_HUB_NICK);
		nick = cur->addChild(ControlSeeds::Dialog::textBox);
		nick->setText(Text::toT(entry->get(HubSettings::Nick)));
		nick->setHelpId(IDH_FAVORITE_HUB_NICK);
		DialogControls::preventSpaces(nick);

		cur->addChild(Label::Seed(T_("Password")))->setHelpId(IDH_FAVORITE_HUB_PASSWORD);
		password = cur->addChild(ControlSeeds::Dialog::textBox);
		password->setPassword();
		password->setText(Text::toT(entry->getPassword()));
		password->setHelpId(IDH_FAVORITE_HUB_PASSWORD);
		DialogControls::preventSpaces(password);

		cur->addChild(Label::Seed(T_("Description")))->setHelpId(IDH_FAVORITE_HUB_USER_DESC);
		description = cur->addChild(ControlSeeds::Dialog::textBox);
		description->setText(Text::toT(entry->get(HubSettings::Description)));
		description->setHelpId(IDH_FAVORITE_HUB_USER_DESC);

		cur->addChild(Label::Seed(T_("Email")))->setHelpId(IDH_FAVORITE_HUB_EMAIL);
		email = cur->addChild(ControlSeeds::Dialog::textBox);
		email->setText(Text::toT(entry->get(HubSettings::Email)));
		email->setHelpId(IDH_FAVORITE_HUB_EMAIL);

		cur->addChild(Label::Seed(T_("IPv4")))->setHelpId(IDH_FAVORITE_HUB_USER_IP);
		userIp = cur->addChild(ControlSeeds::Dialog::textBox);
		userIp->setText(Text::toT(entry->get(HubSettings::UserIp)));
		userIp->setHelpId(IDH_FAVORITE_HUB_USER_IP);
		DialogControls::preventSpaces(userIp);

		cur->addChild(Label::Seed(T_("IPv6")))->setHelpId(IDH_FAVORITE_HUB_USER_IP6);
		userIp6 = cur->addChild(ControlSeeds::Dialog::textBox);
		userIp6->setText(Text::toT(entry->get(HubSettings::UserIp6)));
		userIp6->setHelpId(IDH_FAVORITE_HUB_USER_IP6);
		DialogControls::preventSpaces(userIp6);
	}

	{
		auto cur = grid->addChild(Grid::Seed(4, 2));
		grid->setWidget(cur, 2, 0, 1, 2);
		cur->column(0).mode = GridInfo::FILL;
		cur->column(0).align = GridInfo::BOTTOM_RIGHT;

		cur->addChild(Label::Seed(T_("Show joins / parts in chat by default")));
		showJoins = cur->addChild(ControlSeeds::Dialog::comboBox);
		DialogControls::fillTriboolCombo(showJoins);
		showJoins->setSelected(toInt(entry->get(HubSettings::ShowJoins)));

		cur->addChild(Label::Seed(T_("Only show joins / parts for favorite users")));
		favShowJoins = cur->addChild(ControlSeeds::Dialog::comboBox);
		DialogControls::fillTriboolCombo(favShowJoins);
		favShowJoins->setSelected(toInt(entry->get(HubSettings::FavShowJoins)));

		cur->addChild(Label::Seed(T_("Log main chat")));
		logMainChat = cur->addChild(ControlSeeds::Dialog::comboBox);
		DialogControls::fillTriboolCombo(logMainChat);
		logMainChat->setSelected(toInt(entry->get(HubSettings::LogMainChat)));

		cur->addChild(Label::Seed(T_("User icon size")));
		userIconSize = cur->addChild(ControlSeeds::Dialog::comboBox);
		userIconSize->addValue(T_("Default"));
		int selectedSize = 0;
		for(size_t i = 0; i < std::size(HubSettings::userIconSizes); ++i) {
			userIconSize->addValue(Text::toT(std::to_string(HubSettings::userIconSizes[i])) + _T(" px"));
			if(entry->get(HubSettings::UserIconSize) == HubSettings::userIconSizes[i]) selectedSize = static_cast<int>(i + 1);
		}
		userIconSize->setSelected(selectedSize);
	}

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Group")));
		grid->setWidget(group, 3, 0, 1, 2);

		auto cur = group->addChild(Grid::Seed(1, 2));
		cur->column(0).mode = GridInfo::FILL;

		auto seed = ControlSeeds::Dialog::comboBox;
		seed.style |= CBS_SORT;
		groups = cur->addChild(seed);
		groups->setHelpId(IDH_FAVORITE_HUB_GROUP);

		auto manage = cur->addChild(Button::Seed(T_("Manage &groups")));
		manage->setHelpId(IDH_FAVORITE_HUBS_MANAGE_GROUPS);
		manage->onClicked([this] { handleGroups(); });
	}

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Shared folders")));
		grid->setWidget(group, 4, 0, 1, 2);

		auto cur = group->addChild(Grid::Seed(2, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->row(1).mode = GridInfo::STATIC;
		cur->row(1).size = 180;
		cur->row(1).align = GridInfo::STRETCH;

		defaultShare = cur->addChild(CheckBox::Seed(T_("Use the default share on this hub")));
		defaultShare->setChecked(!entry->hasShareProfile());

		dwt::Tree::Seed seed;
		seed.checkBoxes = true;
		seed.style |= TVS_HASBUTTONS | TVS_LINESATROOT;
		shareFolders = cur->addChild(seed);
		shareFolders->addColumn(T_("Virtual folder"), 120);
		shareFolders->addColumn(T_("Shared directories"), 240);
		shareFolders->addColumn(T_("Hashed size"), 100, dwt::Column::RIGHT);

		fillShareFolders();
		defaultShare->onClicked([this] {
			if(defaultShare->getChecked()) {
				updatingShareFolders = true;
				selectedShareDirectories.clear();
				for(const auto& item: shareFolderPaths) {
					shareFolders->setChecked(item.first, true);
					selectedShareDirectories.insert(item.second.begin(), item.second.end());
				}
				updatingShareFolders = false;
			}
		});
		shareFolders->onItemChanged([this](const NMTVITEMCHANGE& change) {
			if((change.uChanged & TVIF_STATE) == 0 ||
				((change.uStateOld ^ change.uStateNew) & TVIS_STATEIMAGEMASK) == 0) {
				return;
			}

			if(updatingShareFolders) {
				return;
			}

			const auto item = change.hItem;
			auto paths = shareFolderPaths.find(item);
			if(paths == shareFolderPaths.end()) {
				return;
			}

			if(shareFolders->getChecked(item)) {
				selectedShareDirectories.insert(paths->second.begin(), paths->second.end());
			} else {
				for(const auto& path: paths->second) {
					selectedShareDirectories.erase(path);
				}
			}
			defaultShare->setChecked(false);
		});
	}

	DialogControls::addDlgButtons(grid,
		[this] { handleOKClicked(); },
		[this] { endDialog(IDCANCEL); });

	fillGroups();

	setText(T_("Favorite Hub Properties"));

	layout();
	centerWindow();

	return false;
}

void FavHubProperties::handleOKClicked() {
	const auto oldServer = entry->getServer();
	const auto oldShareProfile = entry->hasShareProfile();
	const auto oldShareDirectories = entry->getShareDirectories();

	tstring addressText = address->getText();
	if(addressText.empty()) {
		dwt::MessageBox(this).show(T_("Hub address cannot be empty"), _T(APPNAME) _T(" ") _T(VERSIONSTRING), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONEXCLAMATION);
		return;
	}
	entry->setServer(Text::fromT(addressText));
	entry->setName(Text::fromT(name->getText()));
	entry->setHubDescription(Text::fromT(hubDescription->getText()));
	if(encoding) { // the box is optional.
		entry->setEncoding(
			encoding->getSelected() == 0 ? string() : // "Default" item.
			Text::fromT(encoding->getText()));
	}
	entry->get(HubSettings::Nick) = Text::fromT(nick->getText());
	entry->setPassword(Text::fromT(password->getText()));
	entry->get(HubSettings::Description) = Text::fromT(description->getText());
	entry->get(HubSettings::Email) = Text::fromT(email->getText());
	entry->get(HubSettings::UserIp) = Text::fromT(userIp->getText());
	entry->get(HubSettings::UserIp6) = Text::fromT(userIp6->getText());
	entry->get(HubSettings::ShowJoins) = to3bool(showJoins->getSelected());
	entry->get(HubSettings::FavShowJoins) = to3bool(favShowJoins->getSelected());
	entry->get(HubSettings::LogMainChat) = to3bool(logMainChat->getSelected());
	const auto iconSizeIndex = userIconSize->getSelected();
	entry->get(HubSettings::UserIconSize) = iconSizeIndex > 0 && static_cast<size_t>(iconSizeIndex - 1) < std::size(HubSettings::userIconSizes) ?
		HubSettings::userIconSizes[iconSizeIndex - 1] : HubSettings::getMinInt();
	entry->setGroup(Text::fromT(groups->getText()));

	if(defaultShare->getChecked()) {
		entry->clearShareProfile();
	} else {
		entry->setShareDirectories(selectedShareDirectories);
	}
	FavoriteManager::getInstance()->save();
	HubFrame::refreshUserIconSizes();
	if(oldShareProfile != entry->hasShareProfile() || oldShareDirectories != entry->getShareDirectories()) {
		ConnectionManager::getInstance()->disconnectUploads(oldServer);
	}
	ClientManager::getInstance()->infoUpdated();
	endDialog(IDOK);
}

void FavHubProperties::fillShareFolders() {
	updatingShareFolders = true;
	selectedShareDirectories.clear();

	std::map<string, std::set<string>> grouped;
	for(const auto& item: ShareManager::getInstance()->getDirectories()) {
		grouped[item.first].insert(item.second);
	}

	for(const auto& group: grouped) {
		tstring paths;
		int64_t size = 0;
		bool checked = !entry->hasShareProfile();

		if(entry->hasShareProfile()) {
			checked = true;
			for(const auto& path: group.second) {
				auto found = find_if(entry->getShareDirectories().begin(), entry->getShareDirectories().end(), [&path](const string& allowed) {
					return Util::stricmp(path, allowed) == 0;
				});
				if(found == entry->getShareDirectories().end()) {
					checked = false;
					break;
				}
			}
		}

		for(const auto& path: group.second) {
			if(!paths.empty()) {
				paths += _T("; ");
			}
			paths += Text::toT(path);
			auto pathSize = ShareManager::getInstance()->getShareSize(path);
			if(pathSize > 0) {
				size += pathSize;
			}
		}

		auto item = shareFolders->insert(Text::toT(group.first), nullptr);
		shareFolders->setText(item, 1, paths);
		shareFolders->setText(item, 2, Text::toT(Util::formatBytes(size)));
		shareFolderPaths.emplace(item, group.second);
		shareFolders->setChecked(item, checked);
		if(checked) {
			selectedShareDirectories.insert(group.second.begin(), group.second.end());
		}
	}

	updatingShareFolders = false;
}

void FavHubProperties::handleGroups() {
	FavHubGroupsDlg(this, entry).run();

	HoldRedraw hold { groups };
	groups->clear();
	fillGroups();
}

void FavHubProperties::fillGroups() {
	const string& entryGroup = entry->getGroup();
	bool needSel = true;

	groups->addValue(_T(""));

	for(auto& i: FavoriteManager::getInstance()->getFavHubGroups()) {
		const string& name = i.first;
		auto pos = groups->addValue(Text::toT(name));
		if(needSel && name == entryGroup) {
			groups->setSelected(pos);
			needSel = false;
		}
	}

	if(needSel)
		groups->setSelected(0);
}

void FavHubProperties::fillEncodings()
{
	// Load all available code pages
	::EnumSystemCodePages(EnumCodePageProc, CP_INSTALLED);

	// Add a default code page
	encoding->addValue(T_("Default"));

	UINT currentCodePage = Text::getCodePage(entry->getEncoding());

	// Go through all code pages and add them to the view
	int selectedItem = 0, counter = 1;
	for(auto& e: encodings)
	{
		encoding->addValue(e.second);

		// This is so we keep track of which code page should be the one to be selected
		if(currentCodePage == e.first)
		{
			selectedItem = counter;
		}
		counter++;
	}

	encoding->setSelected(selectedItem);
}

BOOL CALLBACK FavHubProperties::EnumCodePageProc(LPTSTR lpCodePageString)
{
	if(wcslen(lpCodePageString) != 0)
	{
		UINT pageId = _ttoi(lpCodePageString);

		CPINFOEX cpInfoEx = { 0 };
		GetCPInfoEx(pageId, 0, &cpInfoEx);

		if(wcslen(cpInfoEx.CodePageName) != 0)
		{
			encodings[pageId] = cpInfoEx.CodePageName;
		}
	}

	return TRUE;
}
