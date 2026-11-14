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

#ifndef DCPLUSPLUS_WIN32_TABLELAYOUT_H
#define DCPLUSPLUS_WIN32_TABLELAYOUT_H

#include <dcpp/util/Util.h>
#include <dwt/widgets/Table.h>
#include <string>
#include <utility>
#include <vector>
namespace TableLayout {
struct ColumnInfo { const char* name; const int size; const bool numerical; };
void makeColumns(dwt::TablePtr table, const ColumnInfo* columnInfo, size_t columnCount,
    const std::string& order = dcpp::Util::emptyString, const std::string& widths = dcpp::Util::emptyString);
int tableSortSetting(int column, bool ascending);
std::pair<int, bool> tableSortSetting(int columnCount, int setting, int defaultCol, bool defaultAscending);
template<typename T> int getTableSort(T table) { return tableSortSetting(table->getSortColumn(), table->isAscending()); }
template<typename T> void setTableSort(T table, int columnCount, int setting, int defaultCol, bool defaultAscending = true) {
    auto sort = tableSortSetting(columnCount, setting, defaultCol, defaultAscending);
    table->setSort(sort.first, sort.second);
}
std::string toString(const std::vector<int>& tokens);
void toInts(const std::string& str, std::vector<int>& tokens);
}

#endif
