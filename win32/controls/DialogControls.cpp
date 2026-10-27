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

#include <win32/controls/DialogControls.h>
#include <win32/appearance/AppAppearance.h>
#include <win32/appearance/IconManager.h>
#include <win32/forward.h>
#include <dcpp/base/debug.h>
#include <dcpp/text/StringMatch.h>
#include <dcpp/util/Util.h>
#include <dwt/WidgetCreator.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/TextBox.h>
#include <win32/resource.h>

using dwt::Grid;

namespace DialogControls {

pair<ButtonPtr, ButtonPtr> addDlgButtons(GridPtr grid) {
	Button::Seed seed;

	seed.caption = T_("OK");
	seed.style |= BS_DEFPUSHBUTTON;
	seed.menuHandle = reinterpret_cast<HMENU> (IDOK);
	seed.padding.x = 20;
	ButtonPtr ok = grid->addChild(seed);
	ok->setHelpId(IDH_DCPP_OK);
	ok->setImage(IconManager::buttonIcon(IDI_OK));

	seed.caption = T_("Cancel");
	seed.style &= ~BS_DEFPUSHBUTTON;
	seed.menuHandle = reinterpret_cast<HMENU> (IDCANCEL);
	seed.padding.x = 10;
	ButtonPtr cancel = grid->addChild(seed);
	cancel->setHelpId(IDH_DCPP_CANCEL);
	cancel->setImage(IconManager::buttonIcon(IDI_CANCEL));

	return std::make_pair(ok, cancel);
}

ButtonPtr addHelpButton(GridPtr grid) {
	Button::Seed seed(T_("Help"));
	seed.padding.x = 10;
	ButtonPtr button = grid->addChild(seed);
	button->setHelpId(IDH_DCPP_HELP);
	button->setImage(IconManager::buttonIcon(IDI_HELP));
	return button;
}

void addSearchIcon(TextBoxPtr box) {
	// add a search icon by creating a transparent label on top of the text control.

	dcassert(box->hasStyle(WS_CLIPCHILDREN));

	// structure of the right border: text | spacing | icon | margin | right border
	const int spacing = 2, size = 16;
	const auto margin = HIWORD(box->sendMessage(EM_GETMARGINS));
	box->sendMessage(EM_SETMARGINS, EC_RIGHTMARGIN, MAKELONG(0, spacing + size + margin));

	auto label = dwt::WidgetCreator<Label>::create(box, Label::Seed(IconManager::load(IDI_SEARCH, 16)));
	AppAppearance::setColor(label);
	box->onSized([box, label, size, margin](const dwt::SizedEvent&) {
		auto sz = box->getClientSize();
		label->resize(dwt::Rectangle(sz.x - margin - size, std::max(sz.y - size, 0L) / 2, size, size));
	});
}

void addFilterMethods(ComboBoxPtr box) {
	tstring methods[StringMatch::METHOD_LAST] = { T_("Partial match"), T_("Exact match"), T_("Regular Expression") };
	std::for_each(methods, methods + StringMatch::METHOD_LAST, [box](const tstring& str) { box->addValue(str); });
}

void fillTriboolCombo(ComboBoxPtr box) {
	box->addValue(T_("Default"));
	box->addValue(T_("Yes"));
	box->addValue(T_("No"));
}

void preventSpaces(TextBoxPtr box) {
	box->onUpdated([box] {
		auto text = box->getText();
		bool update = false;

		// Strip ' '
		tstring::size_type i;
		while((i = text.find(' ')) != string::npos) {
			text.erase(i, 1);
			update = true;
		}

		if(update) {
			// Something changed; update window text without changing cursor pos
			long caretPos = box->getCaretPos() - 1;
			box->setText(text);
			box->setSelection(caretPos, caretPos);
		}
	});
}

} // namespace DialogControls
