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

#ifndef DCPLUSPLUS_WIN32_IMAGELISTS_H
#define DCPLUSPLUS_WIN32_IMAGELISTS_H

#include <dwt/resources/ImageList.h>
#include <dwt/tstring.h>
#include <string>
#include <vector>
namespace ImageLists {
// pre-defined icon indexes used by the "fileImages" image list - see also getFileIcon.
// also contains icons used by the transfer list.
enum {
	DIR_ICON,
	DIR_ICON_INCOMPLETE,

	FILE_ICON_GENERIC,

	TRANSFER_ICON_USER,
	TRANSFER_ICON_PM
};

// icon indexes to use with the "userImages" image list.
enum {
	// base icons
	USER_ICON,
	USER_ICON_AWAY,
	USER_ICON_BOT,

	// modifiers
	USER_ICON_MOD_START,
	USER_ICON_NOCON = USER_ICON_MOD_START,
	USER_ICON_NOSLOT,
	USER_ICON_OP,

	USER_ICON_LAST
};

extern dwt::ImageListPtr fileImages, userImages;
extern int fileFlagOffset, userFlagOffset;
void init();
size_t getFileIcon(const std::string& fileName);
size_t getFileIcon(const dwt::tstring& fileName);
dwt::ImageListPtr createUserImages(long pixels);
dwt::IconPtr mergeIcons(const std::vector<int>& iconIds, long pixels = 16);
}

#endif
