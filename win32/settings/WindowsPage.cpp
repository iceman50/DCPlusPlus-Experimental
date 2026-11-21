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
#include <win32/settings/WindowsPage.h>

#include <dcpp/core/crypto/PasswordHash.h>
#include <dcpp/settings/SettingsManager.h>

#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/MessageBox.h>

#include <win32/dialog/ParamDlg.h>
#include <win32/resource.h>

using dwt::Grid;
using dwt::GridInfo;

WindowsPage::ListItem WindowsPage::optionItems[] = {
	{ SettingsManager::POPUP_PMS, N_("Open private messages in their own window"), IDH_SETTINGS_WINDOWS_POPUP_PMS },
	{ SettingsManager::POPUP_HUB_PMS, N_("Open private messages from bots in their own window"), IDH_SETTINGS_WINDOWS_POPUP_HUB_PMS },
	{ SettingsManager::POPUP_BOT_PMS, N_("Open private messages from the hub in their own window"), IDH_SETTINGS_WINDOWS_POPUP_BOT_PMS },
	{ SettingsManager::POPUNDER_FILELIST, N_("Open new file list windows in the background"), IDH_SETTINGS_WINDOWS_POPUNDER_FILELIST },
	{ SettingsManager::POPUNDER_PM, N_("Open new private message windows in the background"), IDH_SETTINGS_WINDOWS_POPUNDER_PM },
	{ SettingsManager::JOIN_OPEN_NEW_WINDOW, N_("Open new window when using /join"), IDH_SETTINGS_WINDOWS_JOIN_OPEN_NEW_WINDOW },
	{ SettingsManager::IGNORE_HUB_PMS, N_("Ignore private messages from the hub"), IDH_SETTINGS_WINDOWS_IGNORE_HUB_PMS },
	{ SettingsManager::IGNORE_BOT_PMS, N_("Ignore private messages from bots"), IDH_SETTINGS_WINDOWS_IGNORE_BOT_PMS },
	{ SettingsManager::TOGGLE_ACTIVE_WINDOW, N_("Toggle window when selecting an active tab"), IDH_SETTINGS_WINDOWS_TOGGLE_ACTIVE_WINDOW },
	{ SettingsManager::PROMPT_PASSWORD, N_("Popup box to input password for hubs"), IDH_SETTINGS_WINDOWS_PROMPT_PASSWORD },
	{ 0, 0 }
};

WindowsPage::ListItem WindowsPage::confirmItems[] = {
	{ SettingsManager::CONFIRM_EXIT, N_("Confirm application exit"), IDH_SETTINGS_WINDOWS_CONFIRM_EXIT },
	{ SettingsManager::CONFIRM_HUB_CLOSING, N_("Confirm hub closing"), IDH_SETTINGS_WINDOWS_CONFIRM_HUB_CLOSING },
	{ SettingsManager::CONFIRM_HUB_REMOVAL, N_("Confirm favorite hub removal"), IDH_SETTINGS_WINDOWS_CONFIRM_HUB_REMOVAL },
	{ SettingsManager::CONFIRM_USER_REMOVAL, N_("Confirm favorite user removal"), IDH_SETTINGS_WINDOWS_CONFIRM_USER_REMOVAL },
	{ SettingsManager::CONFIRM_ITEM_REMOVAL, N_("Confirm item removal in download queue"), IDH_SETTINGS_WINDOWS_CONFIRM_ITEM_REMOVAL },
	{ SettingsManager::CONFIRM_ADLS_REMOVAL, N_("Confirm ADL Search removal"), IDH_SETTINGS_WINDOWS_CONFIRM_ADLS_REMOVAL },
	{ 0, 0 }
};

namespace {

/// Check a password typed into a dialog against a stored hash, without keeping a UTF-8 copy around.
bool matches(const tstring& password, const string& hash) {
	if(password.empty()) {
		return false;
	}
	auto utf8 = Text::fromT(password);
	const bool match = PasswordHash::verify(utf8, hash);
	::SecureZeroMemory(&utf8[0], utf8.size());
	return match;
}

}

WindowsPage::WindowsPage(dwt::Widget* parent) :
PropPage(parent, 3, 1),
options(0),
confirm(0),
lockState(0),
lockSet(0),
lockRemove(0),
lockOnMinimize(0),
lockOnStartup(0),
lockToExit(0)
{
	setHelpId(IDH_WINDOWSPAGE);

	grid->column(0).mode = GridInfo::FILL;
	grid->row(0).mode = GridInfo::FILL;
	grid->row(0).align = GridInfo::STRETCH;
	grid->row(1).mode = GridInfo::FILL;
	grid->row(1).align = GridInfo::STRETCH;

	options = grid->addChild(GroupBox::Seed(T_("Window options")))->addChild(ControlSeeds::Dialog::optionsTable);
	confirm = grid->addChild(GroupBox::Seed(T_("Confirm dialog options")))->addChild(ControlSeeds::Dialog::optionsTable);

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Password lock")));
		group->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);

		auto cur = group->addChild(Grid::Seed(5, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->setSpacing(grid->getSpacing());

		{
			auto row = cur->addChild(Grid::Seed(1, 3));
			row->column(0).mode = GridInfo::FILL;
			row->row(0).align = GridInfo::CENTER;

			lockState = row->addChild(Label::Seed(Util::emptyStringT));
			lockState->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);

			lockSet = row->addChild(Button::Seed(T_("Set password...")));
			lockSet->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);
			lockSet->onClicked([this] { handleSetLockPassword(); });

			lockRemove = row->addChild(Button::Seed(T_("Remove password...")));
			lockRemove->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);
			lockRemove->onClicked([this] { handleRemoveLockPassword(); });
		}

		lockOnMinimize = cur->addChild(CheckBox::Seed(T_("Lock DC++ when it is minimized")));
		lockOnMinimize->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_ON_MINIMIZE);
		lockItems.emplace_back(lockOnMinimize, SettingsManager::LOCK_ON_MINIMIZE, PropPage::T_BOOL);

		lockOnStartup = cur->addChild(CheckBox::Seed(T_("Lock DC++ when it starts")));
		lockOnStartup->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_ON_STARTUP);
		lockItems.emplace_back(lockOnStartup, SettingsManager::LOCK_ON_STARTUP, PropPage::T_BOOL);

		lockToExit = cur->addChild(CheckBox::Seed(T_("Ask for the lock password before exiting")));
		lockToExit->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD_TO_EXIT);
		lockItems.emplace_back(lockToExit, SettingsManager::LOCK_PASSWORD_TO_EXIT, PropPage::T_BOOL);

		auto hint = cur->addChild(Label::Seed(T_("Type /lock in any chat to lock DC++ straight away.")));
		hint->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);
	}

	PropPage::read(optionItems, options);
	PropPage::read(confirmItems, confirm);
	PropPage::read(lockItems);

	// A damaged hash can't be checked, so it counts as no password; saving the page clears it.
	lockHash = SETTING(LOCK_PASSWORD);
	if(!PasswordHash::isValid(lockHash)) {
		lockHash.clear();
	}
	updateLockControls();
}

WindowsPage::~WindowsPage() {
}

void WindowsPage::write() {
	PropPage::write(options);
	PropPage::write(confirm);
	PropPage::write(lockItems);

	if(lockHash != SETTING(LOCK_PASSWORD)) {
		SettingsManager::getInstance()->set(SettingsManager::LOCK_PASSWORD, lockHash);
	}
}

void WindowsPage::handleSetLockPassword() {
	const bool change = !lockHash.empty();

	ParamDlg dlg(this, change ? T_("Change the lock password") : T_("Set a lock password"));
	if(change) {
		dlg.addTextBox(T_("Current password"), Util::emptyStringT, true);
	}
	dlg.addTextBox(T_("New password"), Util::emptyStringT, true);
	dlg.addTextBox(T_("Type the new password again"), Util::emptyStringT, true);
	if(dlg.run() != IDOK) {
		return;
	}

	const auto& values = dlg.getValues();
	size_t i = 0;
	if(change && !matches(values[i++], lockHash)) {
		showLockError(T_("The current password is wrong."));
		return;
	}
	const auto& password = values[i];
	if(password.empty()) {
		showLockError(T_("The new password can't be empty."));
		return;
	}
	if(password != values[i + 1]) {
		showLockError(T_("The two new passwords don't match."));
		return;
	}

	auto utf8 = Text::fromT(password);
	auto hash = PasswordHash::create(utf8);
	::SecureZeroMemory(&utf8[0], utf8.size());
	if(hash.empty()) {
		showLockError(T_("The password could not be stored."));
		return;
	}

	lockHash = std::move(hash);
	updateLockControls();
}

void WindowsPage::handleRemoveLockPassword() {
	if(lockHash.empty()) {
		return;
	}

	ParamDlg dlg(this, T_("Remove the lock password"), T_("Current password"), Util::emptyStringT, true);
	if(dlg.run() != IDOK) {
		return;
	}
	if(!matches(dlg.getValue(), lockHash)) {
		showLockError(T_("The current password is wrong."));
		return;
	}

	lockHash.clear();
	updateLockControls();
}

void WindowsPage::showLockError(const tstring& message) {
	dwt::MessageBox(this).show(message, T_("Password lock"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONEXCLAMATION);
}

void WindowsPage::updateLockControls() {
	// The lock options do nothing without a password, so they are only offered once one is set.
	const bool set = !lockHash.empty();
	lockState->setText(set ? T_("A lock password is set.") : T_("No lock password is set."));
	lockSet->setText(set ? T_("Change password...") : T_("Set password..."));
	lockRemove->setEnabled(set);
	for(auto box: { lockOnMinimize, lockOnStartup, lockToExit }) {
		box->setEnabled(set);
		if(!set) {
			box->setChecked(false);
		}
	}
	layout();
}
