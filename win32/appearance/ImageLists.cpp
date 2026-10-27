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

#include <win32/appearance/ImageLists.h>
#include <win32/appearance/IconManager.h>
#include <win32/forward.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/util/Util.h>
#include <dcpp/text/Text.h>
#include <dwt/util/GDI.h>
#include <win32/appearance/CountryFlags.h>
#include <win32/resource.h>
#include <unordered_map>

namespace ImageLists {

dwt::ImageListPtr fileImages, userImages;
int fileFlagOffset = 0, userFlagOffset = 0;
namespace {
std::unordered_map<string, size_t> fileIndexes;
}
void init() {
	fileImages = dwt::ImageListPtr(new dwt::ImageList(dwt::Point(16, 16)));

	// get the directory icon (DIR_ICON).
	if(SETTING(USE_SYSTEM_ICONS)) {
		::SHFILEINFO info;
		if(::SHGetFileInfo(_T("./"), FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
			SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) && info.hIcon)
		{
			dwt::Icon icon { info.hIcon };
			fileImages->add(icon);
		}
	}
	if(fileImages->empty()) {
		fileImages->add(*IconManager::load(IDI_DIRECTORY, 16));
	}

	// create the incomplete directory icon (DIR_ICON_INCOMPLETE).
	{
		dwt::ImageList icons(dwt::Point(16, 16));
		icons.add(*fileImages->getIcon(DIR_ICON));
		icons.add(*IconManager::load(IDI_EXEC, 16));
		fileImages->add(*dwt::util::merge(icons));
	}

	// add the generic file icon (FILE_ICON_GENERIC).
	fileImages->add(*IconManager::load(IDI_FILE, 16));

	// add icons used by the transfer list (TRANSFER_ICON_*).
	fileImages->add(*IconManager::load(IDI_USER, 16));
	fileImages->add(*IconManager::load(IDI_PRIVATE, 16));

	fileFlagOffset = CountryFlags::append(*fileImages);
	userImages = createUserImages(16);

}
size_t getFileIcon(const string& fileName) {
	if(SETTING(USE_SYSTEM_ICONS)) {
		string ext = Text::toLower(Util::getFileExt(fileName));
		if(!ext.empty()) {
			auto index = fileIndexes.find(ext);
			if(index != fileIndexes.end())
				return index->second;
		}

		::SHFILEINFO info;
		if(::SHGetFileInfo(Text::toT(Text::toLower(Util::getFileName(fileName))).c_str(), FILE_ATTRIBUTE_NORMAL,
			&info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) && info.hIcon)
		{
			size_t ret = fileImages->size();
			fileIndexes[ext] = ret;

			dwt::Icon icon { info.hIcon };
			fileImages->add(icon);

			return ret;
		}
	}

	return FILE_ICON_GENERIC;
}

size_t getFileIcon(const tstring& fileName) {
	return getFileIcon(Text::fromT(fileName));
}

dwt::ImageListPtr createUserImages(long pixels) {
	const dwt::Point size(pixels, pixels);
	auto images = dwt::ImageListPtr(new dwt::ImageList(size));

	const unsigned baseCount = USER_ICON_MOD_START;
	const unsigned modifierCount = USER_ICON_LAST - USER_ICON_MOD_START;

	auto userIcon = [pixels](unsigned id) { return IconManager::load(id, pixels); };
	// Bot artwork is a corner overlay; retain the normal user underneath it.
	dwt::IconPtr bases[baseCount] = { userIcon(IDI_USER), userIcon(IDI_USER_AWAY), mergeIcons({ IDI_USER, IDI_USER_BOT }, pixels) };
	dwt::IconPtr modifiers[modifierCount] = { userIcon(IDI_USER_NOCON), userIcon(IDI_USER_NOSLOT), userIcon(IDI_USER_OP) };

	for(size_t iBase = 0; iBase < baseCount; ++iBase) {
		for(size_t i = 0, n = modifierCount * modifierCount; i < n; ++i) {
			dwt::ImageList icons(size);

			icons.add(*bases[iBase]);

			for(size_t iMod = 0; iMod < modifierCount; ++iMod)
				if(i & (static_cast<size_t>(1) << iMod))
					icons.add(*modifiers[iMod]);

			images->add(*dwt::util::merge(icons));
		}
	}
	userFlagOffset = CountryFlags::append(*images);
	return images;
}

dwt::IconPtr mergeIcons(const std::vector<int>& iconIds, long pixels)
{
	const dwt::Point size(pixels, pixels);
	dwt::ImageList icons(size);

	for(auto& item : iconIds)
	{
		auto icon = IconManager::load(item, pixels);
		icons.add(*icon);
	}

	return dwt::util::merge(icons);
}

} // namespace ImageLists
