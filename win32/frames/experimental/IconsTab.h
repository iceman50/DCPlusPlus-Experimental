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

#ifndef DCPLUSPLUS_WIN32_ICONS_TAB_H
#define DCPLUSPLUS_WIN32_ICONS_TAB_H

#include <string>
#include <vector>

#include <win32/frames/experimental/ExperimentalTabs.h>

/** Application icon packages: each one previewed in its own icons, on light and dark
 * backgrounds, before it is used. */
class IconsTab : public ExperimentalTab {
public:
	IconsTab(TabViewPtr parent, LiveSettings& live);
	void layout() override;
	void refresh() override;

	/** The preview sizes, in pixels before DPI scaling. */
	static constexpr long previewSizes[] = { 16, 24, 32, 48 };

private:
	class Gallery;

	struct Choice {
		/** The ICON_PACK value: empty for automatic, IconManager::EMBEDDED_PACK, or a package path. */
		std::string setting;
		tstring name;
		tstring version;
		tstring icons;
		tstring scheme;
	};

	TablePtr list;
	ButtonPtr useButton;
	LabelPtr status;
	ComboBoxPtr sizeBox;
	Gallery* gallery;
	GridPtr galleryGrid;
	LabelPtr hovered;
	std::vector<Choice> choices;

	void build() override;
	/** List the packages again, selecting the one with this ICON_PACK value. */
	void fill(const std::string& select);
	void addChoice(Choice choice);
	int selectedChoice() const;
	/** Mark the package in use and say so below the list. */
	void markInUse();
	void showSelected();
	void useSelected();
	void browse();
};

#endif // !defined(DCPLUSPLUS_WIN32_ICONS_TAB_H)
