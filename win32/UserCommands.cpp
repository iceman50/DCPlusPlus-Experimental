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

#include <win32/UserCommands.h>
#include <win32/forward.h>
#include <dcpp/util/text/StringTokenizer.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/util/Util.h>
#include <win32/dialog/ParamDlg.h>

namespace UserCommands {

bool getUCParams(dwt::Widget* parent, const UserCommand& uc, ParamMap& params) noexcept {
	ParamDlg dlg(parent, Text::toT(Util::toString(" > ", uc.getDisplayName())));

	string::size_type i = 0;
	StringList names;

	while((i = uc.getCommand().find("%[line:", i)) != string::npos) {
		i += 7;
		string::size_type j = uc.getCommand().find(']', i);
		if(j == string::npos)
			break;

		const string name = uc.getCommand().substr(i, j - i);
		if(find(names.begin(), names.end(), name) == names.end()) {
			tstring caption = Text::toT(name);
			if(uc.adc()) {
				Util::replace(_T("\\\\"), _T("\\"), caption);
				Util::replace(_T("\\s"), _T(" "), caption);
			}

			// let's break between slashes (while ignoring double-slashes) to see if it's a combo
			int combo_sel = -1;
			tstring combo_caption = caption;
			Util::replace(_T("//"), _T("\t"), combo_caption);
			TStringList combo_values = StringTokenizer<tstring>(combo_caption, _T('/')).getTokens();
			if(combo_values.size() > 2) { // must contain at least: caption, default sel, 1 value

				auto first = combo_values.begin();
				combo_caption = *first;
				combo_values.erase(first);

				first = combo_values.begin();
				try { combo_sel = static_cast<int>(std::stoull(Text::fromT(*first))); }
				catch(const std::invalid_argument&) { combo_sel = -1; }
				catch(const std::out_of_range&) { combo_sel = -1; }
				combo_values.erase(first);
				if(static_cast<size_t>(combo_sel) >= combo_values.size())
					combo_sel = -1; // default selection value too high
			}

			if(combo_sel >= 0) {
				for(auto& i: combo_values)
					Util::replace(_T("\t"), _T("/"), i);

				// if the combo has already been displayed before, retrieve the prev value and bypass combo_sel
				auto prev = find(combo_values.begin(), combo_values.end(), Text::toT(std::get<string>(params["line:" + name])));
				if(prev != combo_values.end())
					combo_sel = prev - combo_values.begin();

				dlg.addComboBox(combo_caption, combo_values, combo_sel);

			} else {
				dlg.addTextBox(caption, Text::toT(std::get<string>(params["line:" + name])));
			}

			names.push_back(name);
		}
		i = j + 1;
	}

	if(names.empty())
		return true;

	if(dlg.run() == IDOK) {
		const TStringList& values = dlg.getValues();
		for(size_t i = 0, iend = values.size(); i < iend; ++i) {
			params["line:" + names[i]] = Text::fromT(values[i]);
		}
		return true;
	}
	return false;
}

} // namespace UserCommands
