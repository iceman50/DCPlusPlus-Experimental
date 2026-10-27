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

#include <win32/controls/TableLayout.h>
#include <dcpp/util/Util.h>
#include <win32/forward.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/text/StringTokenizer.h>
#include <dcpp/text/Text.h>

namespace TableLayout {

void makeColumns(dwt::TablePtr table, const ColumnInfo* columnInfo, size_t columnCount, const string& order,
	const string& widths)
{
	std::vector<dwt::Column> n(columnCount);
	std::vector<int> o(columnCount);

	for(size_t i = 0; i < columnCount; ++i) {
		n[i].header = T_(columnInfo[i].name);
		n[i].width = columnInfo[i].size;
		n[i].alignment = columnInfo[i].numerical ? dwt::Column::RIGHT : dwt::Column::LEFT;
		o[i] = static_cast<int>(i);
	}

	toInts(order, o);
	size_t i = 0;
	Text::tokenize(widths, ',', [&] (const string& w) { if(i < n.size()) n[i++].width = Util::toInt(w); });

	table->setColumns(n);
	table->setColumnOrder(o);
}

int tableSortSetting(int column, bool ascending) {
	return ascending || column == -1 ? column : -column - 2;
}

pair<int, bool> tableSortSetting(int columnCount, int setting, int defaultCol, bool defaultAscending) {
	SettingsManager::getInstance()->setDefault(static_cast<SettingsManager::IntSetting>(setting),
		tableSortSetting(defaultCol, defaultAscending));
	auto s = SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(setting));
	auto ret = s >= -1 ? std::make_pair(s, true) : std::make_pair(-s - 2, false);
	if(ret.first >= columnCount) {
		return std::make_pair(defaultCol, defaultAscending);
	}
	return ret;
}

std::string toString(const std::vector<int>& tokens) {
	std::string ret;
	auto start = tokens.cbegin();
	for(auto i = start, iend = tokens.cend(); i != iend; ++i) {
		if(i != start)
			ret += ',';
		ret += std::to_string(*i);
	}
	return ret;
}

void toInts(const string& str, std::vector<int>& array) {
	StringTokenizer<string> t(str, _T(','));
	StringList& l = t.getTokens();

	for(size_t i = 0; i < l.size() && i < array.size(); ++i) {
		array[i] = Util::toInt(l[i]);
	}
}

} // namespace TableLayout
