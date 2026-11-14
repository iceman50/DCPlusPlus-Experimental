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

#include <win32/util/ShellActions.h>
#include <win32/forward.h>
#include <dcpp/core/io/File.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/util/Util.h>

namespace ShellActions {

namespace {
PIDLIST_ABSOLUTE parseShellPath(const tstring& path) {
	PIDLIST_ABSOLUTE item = nullptr;
	if(SUCCEEDED(::SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr))) {
		return item;
	}
	const auto nativePath = File::toNativePath(Text::fromT(path));
	if(nativePath != path && SUCCEEDED(::SHParseDisplayName(nativePath.c_str(), nullptr, &item, 0, nullptr))) {
		return item;
	}
	return nullptr;
}

bool executeShellItem(const tstring& path) {
	auto item = parseShellPath(path);
	if(!item) {
		return false;
	}
	SHELLEXECUTEINFO info = { sizeof(SHELLEXECUTEINFO) };
	info.fMask = SEE_MASK_IDLIST | SEE_MASK_INVOKEIDLIST;
	info.lpIDList = item;
	info.nShow = SW_SHOWNORMAL;
	const auto result = ::ShellExecuteEx(&info) != FALSE;
	::CoTaskMemFree(item);
	return result;
}

bool showShellFolder(const tstring& path, bool selectItem) {
	auto item = parseShellPath(path);
	if(!item) {
		return false;
	}
	HRESULT result;
	if(selectItem) {
		auto parent = ::ILCloneFull(item);
		LPCITEMIDLIST child = ::ILFindLastID(item);
		if(!parent || !child || !::ILRemoveLastID(parent)) {
			if(parent) {
				::ILFree(parent);
			}
			::CoTaskMemFree(item);
			return false;
		}
		result = ::SHOpenFolderAndSelectItems(parent, 1, &child, 0);
		::ILFree(parent);
	} else {
		result = ::SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
	}
	::CoTaskMemFree(item);
	return SUCCEEDED(result);
}

}

void openFile(const tstring& file) {
	if(!executeShellItem(file)) {
		::ShellExecute(NULL, NULL, file.c_str(), NULL, NULL, SW_SHOWNORMAL);
	}
}

void openFolder(const tstring& file) {
	const auto path = Text::fromT(file);
	const auto isFile = File::getSize(path) != -1;
	const auto folder = isFile || !File::isDirectory(path) ? Text::toT(Util::getFilePath(path)) : file;
	if((isFile && showShellFolder(file, true)) || (!isFile && showShellFolder(folder, false))) {
		return;
	}
	::ShellExecute(NULL, NULL, Text::toT("explorer.exe").c_str(),
		Text::toT("/e, \"" + Text::fromT(folder) + "\"").c_str(), NULL, SW_SHOWNORMAL);
}

void openLink(const tstring& url) {
	::ShellExecute(NULL, NULL, url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

} // namespace ShellActions
