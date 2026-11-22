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

#include "stdafx.h"
#include <win32/frames/experimental/IconsTab.h>

#include <algorithm>
#include <iterator>

#include <dcpp/core/io/File.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/util/Util.h>

#include <dwt/CanvasClasses.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/LoadDialog.h>
#include <dwt/widgets/MessageBox.h>
#include <dwt/widgets/Table.h>

#include <win32/components/TableLayout.h>
#include <win32/resource.h>
#include <win32/settings/base/LiveSettings.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/IconManager.h>
#include <win32/util/ShellActions.h>
#include <win32/util/UiFonts.h>

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;

namespace {

const TableLayout::ColumnInfo packageColumns[] = {
	{ N_("Package"), 220, false },
	{ N_("Version"), 60, false },
	{ N_("Icons"), 50, true },
	{ N_("Scheme"), 60, false }
};

enum { COLUMN_NAME, COLUMN_VERSION, COLUMN_ICONS, COLUMN_SCHEME, COLUMN_LAST };

}

/** Paints a package's icons on a light and on a dark background, and names the one under the
 * mouse. */
class IconsTab::Gallery : public dwt::Container {
public:
	typedef std::function<void (const tstring&)> HoverF;

	Gallery(dwt::Widget* parent, HoverF onHover_) : dwt::Container(parent), size(32), onHover(std::move(onHover_)) {
		Seed seed(0, 0);
		create(seed);
		onPainting([this](dwt::PaintCanvas& canvas) { paint(canvas.handle()); });
		// Printing and screenshots (WM_PRINT) ask for the same picture.
		addCallback(dwt::Message(WM_PRINTCLIENT), [this](const MSG& msg, LRESULT& result) -> bool {
			paint(reinterpret_cast<HDC>(msg.wParam));
			result = 0;
			return true;
		});
		onMouseMove([this](const dwt::MouseEvent& event) {
			const dwt::ClientCoordinate pos(event.pos, this);
			const auto index = hitTest(pos.getPoint());
			const auto name = index >= 0 ? Text::toT(IconManager::getFileName(icons[index].first)) : tstring();
			if(name != hoveredName) {
				hoveredName = name;
				if(onHover) onHover(name);
			}
			return false;
		});
	}

	void setIcons(std::vector<std::pair<unsigned, dwt::IconPtr>>&& icons_, long size_) {
		icons = std::move(icons_);
		size = size_;
		redraw(true);
	}

	dwt::Point getPreferredSize() override {
		const auto width = std::max<LONG>(getClientSize().x, cell() * 8 + 2 * padding());
		return dwt::Point(cell() * 4 + 2 * padding(), 2 * panelHeight(width));
	}

	void layout() override {
		redraw(true);
	}

private:
	std::vector<std::pair<unsigned, dwt::IconPtr>> icons;
	long size;
	HoverF onHover;
	tstring hoveredName;

	LONG padding() { return scale(8); }
	LONG cell() { return scale(size) + scale(12); }
	LONG header() { return scale(22); }

	LONG perRow(LONG width) {
		return std::max<LONG>(1, (width - 2 * padding()) / cell());
	}

	LONG panelHeight(LONG width) {
		const auto count = static_cast<LONG>(icons.size());
		const auto rows = (count + perRow(width) - 1) / perRow(width);
		return header() + rows * cell() + padding();
	}

	/** Where icon `index` is drawn in panel `panel` (0 light, 1 dark). */
	dwt::Point iconPos(size_t index, int panel, LONG width) {
		const auto row = static_cast<LONG>(index) / perRow(width);
		const auto column = static_cast<LONG>(index) % perRow(width);
		return dwt::Point(padding() + column * cell() + (cell() - scale(size)) / 2,
			panel * panelHeight(width) + header() + row * cell() + (cell() - scale(size)) / 2);
	}

	int hitTest(const dwt::Point& pt) {
		const auto width = getClientSize().x;
		for(int panel = 0; panel < 2; ++panel) {
			for(size_t i = 0; i < icons.size(); ++i) {
				const auto pos = iconPos(i, panel, width);
				if(pt.x >= pos.x && pt.x < pos.x + scale(size) && pt.y >= pos.y && pt.y < pos.y + scale(size)) {
					return static_cast<int>(i);
				}
			}
		}
		return -1;
	}

	void paint(HDC dc) {
		const auto client = getClientSize();
		struct Panel { COLORREF background; COLORREF text; tstring title; };
		const Panel panels[] = {
			{ RGB(250, 250, 250), RGB(32, 32, 32), T_("On a light background") },
			{ RGB(32, 32, 32), RGB(230, 230, 230), T_("On a dark background") }
		};

		HGDIOBJ oldFont = UiFonts::font ? ::SelectObject(dc, UiFonts::font->handle()) : nullptr;
		::SetBkMode(dc, TRANSPARENT);
		const auto height = panelHeight(client.x);
		for(int panel = 0; panel < 2; ++panel) {
			RECT area = { 0, panel * height, client.x, panel == 1 ? std::max<LONG>(client.y, 2 * height) : height };
			auto brush = ::CreateSolidBrush(panels[panel].background);
			::FillRect(dc, &area, brush);
			::DeleteObject(brush);

			RECT title = { padding(), area.top, client.x - padding(), area.top + header() };
			::SetTextColor(dc, panels[panel].text);
			::DrawText(dc, panels[panel].title.c_str(), static_cast<int>(panels[panel].title.size()), &title,
				DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);

			for(size_t i = 0; i < icons.size(); ++i) {
				if(!icons[i].second) continue;
				const auto pos = iconPos(i, panel, client.x);
				::DrawIconEx(dc, pos.x, pos.y, icons[i].second->handle(), scale(size), scale(size), 0, nullptr, DI_NORMAL);
			}
		}
		if(oldFont) {
			::SelectObject(dc, oldFont);
		}
	}
};

IconsTab::IconsTab(TabViewPtr parent, LiveSettings& live) :
	ExperimentalTab(parent, T_("Icons"), IDH_SETTINGS_EXPERIMENTAL_ICON_PACK, live),
	list(nullptr),
	useButton(nullptr),
	status(nullptr),
	sizeBox(nullptr),
	gallery(nullptr),
	galleryGrid(nullptr),
	hovered(nullptr)
{
}

void IconsTab::build() {
	{
		auto group = addGroup(LEFT, T_("Icon packages"), IDH_SETTINGS_EXPERIMENTAL_ICON_PACK, true);
		auto cur = group->addChild(Grid::Seed(4, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->row(0).mode = GridInfo::FILL;
		cur->row(0).size = cur->scale(160);
		cur->row(0).align = GridInfo::STRETCH;

		auto seed = ControlSeeds::Dialog::table;
		seed.style |= LVS_SINGLESEL;
		seed.exStyle &= ~WS_EX_CLIENTEDGE;
		list = cur->addChild(seed);
		list->setHelpId(IDH_SETTINGS_EXPERIMENTAL_ICON_PACK);
		TableLayout::makeColumns(list, packageColumns, COLUMN_LAST);
		list->onSelectionChanged([this] { showSelected(); });
		list->onDblClicked([this] { useSelected(); });

		auto buttons = cur->addChild(Grid::Seed(1, 5));
		buttons->column(4).mode = GridInfo::FILL;
		useButton = buttons->addChild(Button::Seed(T_("&Use this package")));
		useButton->setHelpId(IDH_SETTINGS_EXPERIMENTAL_ICON_PACK);
		useButton->onClicked([this] { useSelected(); });
		buttons->addChild(Button::Seed(T_("&Browse...")))->onClicked([this] { browse(); });
		buttons->addChild(Button::Seed(T_("Open &folder")))->onClicked([] {
			ShellActions::openFile(Text::toT(IconManager::getDirectory()));
		});
		buttons->addChild(Button::Seed(T_("&Reload")))->onClicked([this] { fill(SETTING(ICON_PACK)); });

		status = cur->addChild(Label::Seed(tstring()));
		status->setHelpId(IDH_SETTINGS_EXPERIMENTAL_ICON_PACK);
		cur->addChild(Label::Seed(T_("Packages are .dcico ZIP files containing info.xml and multi-resolution ICO files.\r\n"
			"Automatic uses Dark.dcico for the dark appearance and the built-in icons otherwise.")));
	}

	{
		auto group = addGroup(RIGHT, T_("Preview"), IDH_SETTINGS_EXPERIMENTAL_ICON_PACK, true);
		auto cur = group->addChild(Grid::Seed(3, 1));
		galleryGrid = cur;
		cur->column(0).mode = GridInfo::FILL;
		cur->row(1).mode = GridInfo::FILL;
		cur->row(1).align = GridInfo::STRETCH;

		auto row = cur->addChild(Grid::Seed(1, 2));
		row->addChild(Label::Seed(T_("Icon size")));
		sizeBox = row->addChild(ControlSeeds::Dialog::comboBox);
		sizeBox->setAccessibleName(T_("Preview icon size"));
		for(auto size: previewSizes) {
			sizeBox->addValue(Text::toT(std::to_string(size)) + _T(" px"));
		}
		sizeBox->setSelected(2);
		sizeBox->onSelectionChanged([this] { showSelected(); });

		gallery = new Gallery(cur, [this](const tstring& name) { hovered->setText(name.empty() ? _T(" ") : name); });
		gallery->setHelpId(IDH_SETTINGS_EXPERIMENTAL_ICON_PACK);
		gallery->setAccessibleName(T_("Icon package preview"));
		cur->setWidget(gallery, 1, 0);
		hovered = cur->addChild(Label::Seed(_T(" ")));
	}

	// Reading the packages and their icons waits until the tab is on screen.
	callAsync([this] { fill(SETTING(ICON_PACK)); });
}

void IconsTab::layout() {
	// The gallery's row stretches, and a stretching row reports only its own size: make that the
	// gallery's height, so the tab scrolls when the icons don't fit.
	if(galleryGrid && gallery) {
		const auto height = static_cast<size_t>(gallery->getPreferredSize().y);
		if(galleryGrid->row(1).size != height) {
			galleryGrid->row(1).size = height;
			invalidateLayout(false);
		}
	}
	ExperimentalTab::layout();
}

void IconsTab::refresh() {
	markInUse();
}


void IconsTab::fill(const std::string& select) {
	choices.clear();
	list->clear();
	addChoice({ string(), T_("Automatic (appearance-aware)"), tstring(), tstring(), tstring() });
	addChoice({ IconManager::EMBEDDED_PACK, T_("Embedded icons (EXE)"), tstring(), tstring(), tstring() });
	for(const auto& package: IconManager::getPackages()) {
		addChoice({ package.path, Text::toT(package.name), Text::toT(package.version),
			Text::toT(std::to_string(package.iconCount)), Text::toT(package.scheme) });
	}

	// A package chosen from elsewhere on disk.
	const auto inUse = SETTING(ICON_PACK);
	for(const auto& path: { select, inUse }) {
		if(path.empty() || path == IconManager::EMBEDDED_PACK) continue;
		const auto known = std::any_of(choices.begin(), choices.end(), [&path](const Choice& choice) {
			return Util::stricmp(choice.setting, path) == 0;
		});
		if(known || File::getSize(path) <= 0) continue;
		try {
			const auto package = IconManager::inspectPackage(path);
			addChoice({ package.path, Text::toT(package.name) + T_(" (external)"), Text::toT(package.version),
				Text::toT(std::to_string(package.iconCount)), Text::toT(package.scheme) });
		} catch(const Exception&) {
		}
	}

	int row = 0;
	for(size_t i = 0; i < choices.size(); ++i) {
		if(Util::stricmp(choices[i].setting, select) == 0) {
			row = static_cast<int>(i);
			break;
		}
	}
	markInUse();
	list->setSelected(row);
	list->ensureVisible(row);
	showSelected();
}

void IconsTab::addChoice(Choice choice) {
	list->insert({ choice.name, choice.version, choice.icons, choice.scheme }, static_cast<LPARAM>(choices.size()));
	choices.push_back(std::move(choice));
}

int IconsTab::selectedChoice() const {
	const auto row = list->getSelected();
	return row >= 0 && static_cast<size_t>(row) < choices.size() ? row : -1;
}

void IconsTab::markInUse() {
	if(choices.empty()) {
		// Not listed yet.
		return;
	}
	const auto inUse = SETTING(ICON_PACK);
	tstring name;
	for(size_t i = 0; i < choices.size(); ++i) {
		const bool used = Util::stricmp(choices[i].setting, inUse) == 0;
		list->setText(static_cast<unsigned>(i), COLUMN_NAME, used ? choices[i].name + T_(" (in use)") : choices[i].name);
		if(used) name = choices[i].name;
	}
	status->setText(name.empty() ? T_("The package in use can't be found; the built-in icons are shown.") :
		str(TF_("In use: %1%. Windows that are already open keep their icons until DC++ restarts.") % name));
	const auto selected = selectedChoice();
	useButton->setEnabled(selected >= 0 && Util::stricmp(choices[selected].setting, inUse) != 0);
}

void IconsTab::showSelected() {
	const auto selected = selectedChoice();
	if(selected < 0 || !gallery) {
		return;
	}
	const auto& choice = choices[selected];
	const auto sizeIndex = sizeBox->getSelected();
	const auto size = previewSizes[sizeIndex >= 0 && static_cast<size_t>(sizeIndex) < std::size(previewSizes) ? sizeIndex : 2];

	// Automatic previews the package it picks for the current appearance.
	IconManager::Preview preview(choice.setting.empty() ? IconManager::resolvePackage(string()) : choice.setting);
	std::vector<std::pair<unsigned, dwt::IconPtr>> icons;
	for(auto id: IconManager::getIconIds()) {
		icons.emplace_back(id, preview.load(id, gallery->scale(size)));
	}
	gallery->setIcons(std::move(icons), size);
	useButton->setEnabled(Util::stricmp(choice.setting, SETTING(ICON_PACK)) != 0);
	// The gallery's height depends on how many icons fit in a row.
	layout();
}

void IconsTab::useSelected() {
	const auto selected = selectedChoice();
	if(selected < 0) {
		return;
	}
	const auto& setting = choices[selected].setting;
	if(setting != SETTING(ICON_PACK)) {
		SettingsManager::getInstance()->set(SettingsManager::ICON_PACK, setting);
		live.notify(SettingsManager::ICON_PACK);
	}
	markInUse();
}

void IconsTab::browse() {
	auto path = Text::toT(IconManager::getDirectory());
	if(!dwt::LoadDialog(this).addFilter(T_("DC++ icon packages"), _T("*.dcico"))
		.addFilter(T_("All files"), _T("*.*")).open(path)) return;
	try {
		IconManager::inspectPackage(Text::fromT(path));
		fill(Text::fromT(path));
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(T_("Unable to load the icon package:\r\n") + Text::toT(e.getError()),
			T_("Icon package"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
}
