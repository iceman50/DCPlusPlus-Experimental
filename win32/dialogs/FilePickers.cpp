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

#include <win32/dialogs/FilePickers.h>
#include <dcpp/text/format.h>
#include <win32/forward.h>
#include <dcpp/util/Util.h>
#include <dcpp/text/Text.h>
#include <dwt/widgets/SaveDialog.h>
#include <dwt/widgets/LoadDialog.h>

using dwt::LoadDialog;
using dwt::SaveDialog;

namespace FilePickers {

TStringList lastDirs;

void addLastDir(const tstring& dir) {
	auto i = find(lastDirs.begin(), lastDirs.end(), dir);
	if(i != lastDirs.end()) {
		lastDirs.erase(i);
	}
	while(lastDirs.size() >= 10) {
		lastDirs.erase(lastDirs.begin());
	}
	lastDirs.push_back(dir);
}

bool browseSaveFile(dwt::Widget* parent, tstring& file) {
	auto ext = Util::getFileExt(file);
	auto path = Util::getFilePath(file);

	SaveDialog dlg(parent);

	if(!ext.empty()) {
		ext = ext.substr(1); // remove leading dot so default extension works when browsing for file
		dlg.addFilter(str(TF_("%1% files") % ext), _T("*.") + ext);
		dlg.setDefaultExtension(ext);
	}
	dlg.addFilter(T_("All files"), _T("*.*"));
	dlg.setInitialDirectory(path);

	return dlg.open(file);
}

bool browseFileList(dwt::Widget* parent, tstring& file) {
	return LoadDialog(parent).addFilter(T_("File Lists"), _T("*.xml.bz2;*.xml.zst;*.xml")) .addFilter(T_("All files"), _T("*.*")) .setInitialDirectory(
		Text::toT(Util::getListPath())) .open(file);
}

} // namespace FilePickers
