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

#ifndef DCPLUSPLUS_WIN32_LINKACTIONS_H
#define DCPLUSPLUS_WIN32_LINKACTIONS_H

#include <dcpp/hash/value/MerkleTree.h>
#include <dwt/widgets/TextBox.h>
#include <dcpp/hash/value/Encoder.h>
#include <dcpp/util/text/Text.h>
#include <dcpp/util/Util.h>
#include <dwt/forward.h>
#include <dwt/tstring.h>
namespace LinkActions {
void handleDblClicks(dwt::TextBoxBase* box);
void addHashItems(dwt::Menu* menu, const dcpp::TTHValue& tth, const dwt::tstring& filename, int64_t size);
void copyMagnet(const dcpp::TTHValue& hash, const dwt::tstring& file, int64_t size);
std::string makeMagnet(const dcpp::TTHValue& hash, const std::string& file, int64_t size);
void searchAny(const dwt::tstring& search);
void searchHash(const dcpp::TTHValue& hash);
void searchHub(const dwt::tstring& url);
bool parseLink(const dwt::tstring& str, bool followExternal = true);
void addSearchMenu(dwt::Menu* menu, dwt::tstring& searchText, const std::string& hash = dcpp::Util::emptyString);
inline bool checkTTH(const dwt::tstring& text) { return text.size() == 39 && dcpp::Encoder::isBase32(dcpp::Text::fromT(text)); }
}

#endif
