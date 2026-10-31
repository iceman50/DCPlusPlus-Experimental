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

#ifndef DCPLUSPLUS_DCPP_STYLE_H
#define DCPLUSPLUS_DCPP_STYLE_H

#include <string>
#include <vector>

namespace dcpp {

using std::string;

struct Style {
	string font;
	int textColor;
	int bgColor;
	// Session-local definition identities for live refresh; each component can come from
	// a different matching rule. These are derived, not persisted rule data.
	string fontSource, textSource, bgSource;
	// All matching definitions, including ones with no visual overrides yet.
	// Row painting uses these identities to resolve the current theme without
	// waiting for an asynchronous user update to replace its identity snapshot.
	std::vector<string> matchingStyles;

	Style() : textColor(-1), bgColor(-1) { }
};

} // namespace dcpp

#endif
