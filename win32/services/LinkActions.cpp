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

#include <win32/services/LinkActions.h>
#include <dcpp/text/format.h>
#include <dcpp/util/Util.h>
#include <win32/app/UiServices.h>
#include <win32/appearance/IconManager.h>
#include <win32/shell/ClipboardActions.h>
#include <win32/shell/ShellActions.h>
#include <win32/forward.h>
#include <dcpp/search/Magnet.h>
#include <dcpp/text/StringTokenizer.h>
#include <win32/app/MainWindow.h>
#include <win32/frames/hub/HubFrame.h>
#include <win32/frames/search/SearchFrame.h>
#include <win32/dialogs/MagnetDlg.h>
#include <win32/resource.h>
#include <dwt/util/StringUtils.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/MessageBox.h>

namespace LinkActions {

void handleDblClicks(dwt::TextBoxBase* box) {
	box->onLeftMouseDblClick([box](const dwt::MouseEvent& me) { return parseLink(box->textUnderCursor(me.pos)); });
}

void addHashItems(Menu* menu, const TTHValue& tth, const tstring& filename, int64_t size) {
	if(!tth) { return; }
	menu->appendItem(T_("Search for alternates"), [=] { searchHash(tth); }, IconManager::menuIcon(IDI_SEARCH));
	menu->appendItem(T_("Copy magnet link to clipboard"), [=] { copyMagnet(tth, filename, size); }, IconManager::menuIcon(IDI_MAGNET));
}

void copyMagnet(const TTHValue& aHash, const tstring& aFile, int64_t size) {
	if(!aFile.empty()) {
		ClipboardActions::setClipboard(Text::toT(makeMagnet(aHash, Text::fromT(aFile), size)));
	}
}

string makeMagnet(const TTHValue& aHash, const string& aFile, int64_t size) {
	string ret = "magnet:?xt=urn:tree:tiger:" + aHash.toBase32();
	if(size > 0)
		ret += "&xl=" + std::to_string(size);
	return ret + "&dn=" + Util::encodeURI(aFile);
}

void searchAny(const tstring& aSearch) {
	SearchFrame::openWindow(UiServices::mainWindow->getTabView(), aSearch, SearchManager::TYPE_ANY);
}

void searchHash(const TTHValue& aHash) {
	SearchFrame::openWindow(UiServices::mainWindow->getTabView(), Text::toT(aHash.toBase32()), SearchManager::TYPE_TTH);
}

void searchHub(const tstring& aUrl) {
	SearchFrame::openWindow(UiServices::mainWindow->getTabView(), Util::emptyStringT, SearchManager::TYPE_ANY, aUrl);
}

bool parseLink(const tstring& str, bool followExternal) {
	auto url = Text::fromT(str);
	Util::sanitizeUrl(url);

	string proto, host, port, file, query, fragment;
	Util::decodeUrl(url, proto, host, port, file, query, fragment);

	if(Util::stricmp(proto.c_str(), "adc") == 0 ||
	   Util::stricmp(proto.c_str(), "adcs") == 0 ||
	   Util::stricmp(proto.c_str(), "dchub") == 0 ||
	   Util::stricmp(proto.c_str(), "nmdcs") == 0 )
	{
		HubFrame::openWindow(UiServices::mainWindow->getTabView(), url);

		/// @todo parse other params when RFCs for these schemes have been published.

		return true;
	}

	if(host == "magnet") {
		string hash, name, key;
		if(Magnet::parseUri(Text::fromT(str), hash, name, key)) {
			MagnetDlg(UiServices::mainWindow, Text::toT(hash), Text::toT(name), Text::toT(key)).run();
		} else {
			dwt::MessageBox(UiServices::mainWindow).show(
				T_("A MAGNET link was given to DC++, but it didn't contain a valid file hash for use on the Direct Connect network.  No action will be taken."),
				T_("MAGNET Link detected"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONEXCLAMATION);
		}
		return true;
	}

	if(followExternal)
	{
		TStringList lst = StringTokenizer<tstring>(Text::toT(SETTING(WHITELIST_OPEN_URIS)), ';').getTokens();
		for(auto& s : lst)
		{
			if(Util::strnicmp(str.c_str(), s.c_str(), s.size()) == 0)
			{
				ShellActions::openLink(str);
				return true;
			}
		}

		if(!proto.empty())
		{
			auto boxResult = dwt::MessageBox(UiServices::mainWindow).show(
									T_(
									"Warning: Allowing content to open a program can be useful, but it can potentially harm your computer. "
									"Do not allow it unless you trust the source of the content.") + _T("\n\n") +
									T_("Any program that is launched will have the same access rights as DC++.") + _T("\n\n") +
									T_("The requested link is ") + str + _T("\n\n") +
									T_("Do you still want to allow to open a program on your computer?"),
									T_("External protocol request"),
									dwt::MessageBox::BOX_YESNO, dwt::MessageBox::BOX_ICONEXCLAMATION);
			if(boxResult == dwt::MessageBox::RETBOX_YES) {
				ShellActions::openLink(str);
			}
			return true;
		}
	}

	return false;
}

void addSearchMenu(Menu* menu, tstring& searchText, const string& hash) {
	Util::trim(searchText);
	if (searchText.empty() && hash.empty()) { return; }

	menu->appendSeparator();

	if (!searchText.empty()) {
		auto disp = dwt::util::escapeMenu(searchText);
		dwt::util::cutStr(disp, 50);
		menu->appendItem(str(TF_("Search Hubs for \"%1%\"") % disp), [=] { searchAny(searchText.substr(0, MAX_PATH)); });
	}

	string searchTTH = hash.empty() && checkTTH(searchText) ? Text::fromT(searchText) : hash;

	if (!searchTTH.empty()) {
		menu->appendItem(T_("Search by TTH"), [=] { searchHash(TTHValue(searchTTH)); });
	}
}

} // namespace LinkActions
