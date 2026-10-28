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

#include <win32/dialogs/GridDialog.h>

#include <dwt/widgets/Grid.h>

#include <dwt/util/GDI.h>

using dwt::Grid;
using dwt::GridInfo;

GridDialog::GridDialog(dwt::Widget* parent, const long width_, const DWORD styles_) :
dwt::ModalDialog(parent),
grid(0),
width(width_),
styles(styles_)
{
}

int GridDialog::run() {
	create(Seed(dwt::Point(width, 0), styles)); // the height will be set later on
	return show();
}

void GridDialog::layout() {
	long spacing = static_cast<long>(grid->getSpacing());

	const auto content = getContentRect();
	auto sz = content.size;
	sz.y = grid->getPreferredSize().y;
	grid->resize(dwt::Rectangle(content.left() + spacing, content.top() + spacing, sz.x - spacing * 2, sz.y));

	// now resize the dialog itself
	sz.x = width * dwt::util::dpiFactor(); // don't change the horizontal size
	sz.y += spacing * 2;

	RECT rect = { 0, 0, 0, sz.y };
	adjustContentRect(rect);
	sz.y = rect.bottom - rect.top;

	resize(dwt::Rectangle(getWindowRect().pos, sz));
}
