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

#include <win32/frames/chat/ChatText.h>
#include <win32/forward.h>
#include <dcpp/util/Util.h>
#include <dwt/widgets/TextBox.h>

namespace ChatText {

void reducePaths(string& message) {
	string::size_type start = 0;
	while((start = message.find('<', start)) != string::npos) {
		string::size_type end = message.find('>', start);
		if(end == string::npos)
			return;

		if(count(message.begin() + start, message.begin() + end, PATH_SEPARATOR) >= 2) {
			message.erase(end, 1);
			message.erase(start, message.rfind(PATH_SEPARATOR, message.rfind(PATH_SEPARATOR, end) - 1) - start);
			message.insert(start, "...");
		}

		start++;
	}
}

void getChatSelText(dwt::TextBoxBase* box, tstring& text, const dwt::ScreenCoordinate& pt) {
	text = box->getSelection();

	if (text.empty() && box->hasFocus()) {
		text = box->textUnderCursor(pt);
	}
}

} // namespace ChatText
