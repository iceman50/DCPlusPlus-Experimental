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
#include <win32/util/IconManager.h>
#include <win32/frames/search/MagnetDlg.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/core/header/format.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/SaveDialog.h>
#include <win32/resource.h>

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;

MagnetDlg::MagnetDlg(dwt::Widget* parent, const Magnet::Metadata& metadata_, const string& target_) :
	GridDialog(parent, 560), metadata(metadata_), target(target_) {
	onInitDialog([this] { return handleInitDialog(); });
}

bool MagnetDlg::handleInitDialog() {
	grid = addChild(Grid::Seed(6, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->setSpacing(8);
	const auto wrappedLabel = [this](size_t row, const tstring& text) {
		auto label = grid->addChild(Label::Seed(text));
		auto dc = ::GetDC(label->handle());
		auto previous = ::SelectObject(dc, label->getFont()->handle());
		RECT bounds { 0, 0, std::max(1L, getContentRect().width() - 2 * static_cast<long>(grid->getSpacing())), 0 };
		::DrawText(dc, text.c_str(), static_cast<int>(text.size()), &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
		::SelectObject(dc, previous);
		::ReleaseDC(label->handle(), dc);
		grid->row(row).mode = GridInfo::STATIC;
		grid->row(row).align = GridInfo::STRETCH;
		grid->row(row).size = bounds.bottom - bounds.top;
	};
	wrappedLabel(0, metadata.hash.empty() ?
		T_("This magnet link contains search keywords. Search connected hubs for matching files.") :
		T_("Search for this file or add it to the download queue. Downloads start when a matching source is found."));

	auto details = grid->addChild(Grid::Seed(3, 2));
	details->column(1).mode = GridInfo::FILL;
	auto readOnly = ControlSeeds::Dialog::textBox;
	readOnly.style |= ES_READONLY;
	details->addChild(Label::Seed(T_("Filename:")));
	details->addChild(readOnly)->setText(metadata.name.empty() ? T_("Not supplied") : Text::toT(metadata.name));
	details->addChild(Label::Seed(metadata.hash.empty() ? T_("Search keywords:") : T_("File hash:")));
	details->addChild(readOnly)->setText(Text::toT(metadata.hash.empty() ? metadata.key : metadata.hash));
	details->addChild(Label::Seed(T_("Size:")));
	details->addChild(readOnly)->setText(metadata.size < 0 ? T_("Unknown") :
		str(TF_("%1% (%2% bytes)") % Text::toT(Util::formatBytes(metadata.size)) % metadata.size));

	auto location = grid->addChild(Grid::Seed(1, 3));
	location->column(1).mode = GridInfo::FILL;
	location->addChild(Label::Seed(T_("Save as:")));
	destination = location->addChild(ControlSeeds::Dialog::textBox);
	destination->setText(Text::toT(target));
	destination->setEnabled(metadata.canDownload());
	auto browse = location->addChild(Button::Seed(T_("Browse...")));
	browse->setEnabled(metadata.canDownload());
	browse->onClicked([this] {
		auto path = destination->getText();
		if(dwt::SaveDialog(this).setTitle(T_("Download magnet file"))
			.setAllowOverwrite(false).addFilter(T_("All files"), _T("*.*")).open(path)) destination->setText(path);
	});
	wrappedLabel(3, (metadata.canDownload() ?
		T_("Connect to a hub to find sources. Further source searches follow your download queue settings.") :
		T_("Downloading requires a file hash and size. Search to find the missing information.")) +
		_T("\n") + T_("Remembered downloads use the default download directory."));
	remember = grid->addChild(dwt::CheckBox::Seed(T_("Remember this action")));

	auto actions = grid->addChild(Grid::Seed(1, 3));
	actions->column(0).mode = GridInfo::FILL;
	actions->column(0).align = GridInfo::BOTTOM_RIGHT;
	auto searchSeed = Button::Seed(T_("Search"));
	searchSeed.style |= BS_DEFPUSHBUTTON;
	searchSeed.menuHandle = reinterpret_cast<HMENU>(IDOK);
	auto search = actions->addChild(searchSeed);
	search->onClicked([this] { choose(Action::Search); });
	auto download = actions->addChild(Button::Seed(T_("Download")));
	download->setEnabled(metadata.canDownload());
	download->onClicked([this] { choose(Action::Download); });
	auto cancelSeed = Button::Seed(T_("Cancel"));
	cancelSeed.menuHandle = reinterpret_cast<HMENU>(IDCANCEL);
	actions->addChild(cancelSeed)->onClicked([this] { endDialog(IDCANCEL); });

	setText(T_("Magnet link"));
	setSmallIcon(IconManager::load(IDI_MAGNET, 16));
	setLargeIcon(IconManager::load(IDI_MAGNET, 32));
	layout();
	centerWindow();
	search->setFocus();
	return false;
}

void MagnetDlg::choose(Action selected) {
	if(selected == Action::Download && (!metadata.canDownload() || destination->getText().empty())) {
		destination->setFocus();
		return;
	}
	action = selected;
	target = Text::fromT(destination->getText());
	remembered = remember->getChecked();
	endDialog(IDOK);
}
