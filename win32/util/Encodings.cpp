/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"

#include <win32/util/Encodings.h>

#include <map>

#include <dcpp/util/text/Text.h>
#include <dwt/widgets/ComboBox.h>

namespace Encodings {

namespace {

std::map<UINT, std::wstring> codePages;

BOOL CALLBACK addCodePage(LPTSTR codePageString) {
	if(codePageString && *codePageString) {
		const auto id = static_cast<UINT>(_ttoi(codePageString));
		CPINFOEX info = { 0 };
		if(GetCPInfoEx(id, 0, &info) && info.CodePageName[0]) {
			codePages[id] = info.CodePageName;
		}
	}
	return TRUE;
}

const std::map<UINT, std::wstring>& installedCodePages() {
	// EnumSystemCodePages only reports through a plain callback, and is only used from the UI thread.
	codePages.clear();
	::EnumSystemCodePages(addCodePage, CP_INSTALLED);
	return codePages;
}

}

void fill(dwt::ComboBoxPtr box, const dwt::tstring& defaultLabel, const std::string& current) {
	box->addValue(defaultLabel);

	const auto currentCodePage = current.empty() ? 0u : static_cast<UINT>(Text::getCodePage(current));
	int selectedItem = 0, item = 1;
	for(const auto& page: installedCodePages()) {
		box->addValue(page.second);
		if(page.first == currentCodePage) {
			selectedItem = item;
		}
		++item;
	}
	box->setSelected(selectedItem);
}

std::string selected(dwt::ComboBoxPtr box) {
	return box->getSelected() <= 0 ? std::string() : Text::fromT(box->getText());
}

}
