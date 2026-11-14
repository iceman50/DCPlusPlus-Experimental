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

#include <win32/util/UiFonts.h>
#include <dcpp/core/header/format.h>
#include <unordered_map>
#include <win32/util/ColorUtil.h>
#include <win32/forward.h>
#include <dcpp/message/FontStyle.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/user/UserMatchManager.h>
#include <dcpp/util/text/StringTokenizer.h>
#include <dcpp/util/Util.h>
#include <dcpp/core/version.h>
#include <dwt/util/GDI.h>

namespace UiFonts {

dwt::FontPtr font, uploadFont, downloadFont;
namespace {
std::unordered_map<string, dwt::FontPtr> userMatchFonts;
}
void initDefault() {
	{
		NONCLIENTMETRICS metrics = { sizeof(NONCLIENTMETRICS) };
		::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics, 0);
		SettingsManager::getInstance()->setDefault(SettingsManager::MAIN_FONT, Text::fromT(encodeFont(metrics.lfMessageFont)));
	}

}
tstring encodeFont(LOGFONT const& font) {
	tstring res(font.lfFaceName);
	res += _T(',');
	res += Text::toT(std::to_string(static_cast<int>(std::floor(static_cast<float>(font.lfHeight) / dwt::util::dpiFactor()))));
	res += _T(',');
	res += Text::toT(std::to_string(font.lfWeight));
	res += _T(',');
	res += Text::toT(std::to_string(font.lfItalic));
	res += _T(',');
	res += Text::toT(std::to_string(font.lfCharSet));
	FontStyle effects;
	effects.underline = font.lfUnderline ? 1 : 0;
	effects.strike = font.lfStrikeOut ? 1 : 0;
	return Text::toT(effects.encode(Text::fromT(res)));
}

void decodeFont(const tstring& setting, LOGFONT &dest) {
	StringTokenizer<tstring> st(setting, _T(','));
	TStringList &sl = st.getTokens();

	NONCLIENTMETRICS metrics = { sizeof(NONCLIENTMETRICS) };
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics, 0);
	dest = metrics.lfMessageFont;

	tstring face;
	if(sl.size() >= 4) {
		face = sl[0];
		dest.lfHeight = std::ceil(static_cast<float>(Util::toInt(Text::fromT(sl[1]))) * dwt::util::dpiFactor());
		dest.lfWeight = Util::toInt(Text::fromT(sl[2]));
		dest.lfItalic = static_cast<BYTE>(Util::toInt(Text::fromT(sl[3])));
		if(sl.size() >= 5) {
			dest.lfCharSet = static_cast<BYTE>(Util::toInt(Text::fromT(sl[4])));
		}
	}

	if(!face.empty()) {
		_tcsncpy(dest.lfFaceName, face.c_str(), LF_FACESIZE - 1);
		dest.lfFaceName[LF_FACESIZE - 1] = 0;
	}
	const auto effects = FontStyle::decode(Text::fromT(setting));
	dest.lfUnderline = effects.underline != 0;
	dest.lfStrikeOut = effects.strike != 0;
}

void updateFont(dwt::FontPtr& font, int setting) {
	auto text = SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(setting));
	if(text.empty()) {
		font.reset();
	} else {
		LOGFONT lf;
		decodeFont(Text::toT(text), lf);
		font.reset(new dwt::Font(lf));
	}
}

void initUserMatching() {
	// make sure predefined definitions are here.
	auto here = UserMatchManager::getInstance()->checkPredef();

	if(!here.first || !here.second) {
		auto newList = UserMatchManager::getInstance()->getList();

		if(!here.first) {
			// add a matcher for favs.
			UserMatch matcher;
			matcher.setFlag(UserMatch::PREDEFINED);
			matcher.setFlag(UserMatch::FAVS);
			matcher.name = str(F_("Favorite users (added by %1%)") % APPNAME);
			matcher.style.font = Text::fromT(encodeFont(font->makeBold()->getLogFont()));
			matcher.style.textColor = ColorUtil::modRed(SETTING(TEXT_COLOR), 127); // more red
			newList.push_back(std::move(matcher));
		}

		if(!here.second) {
			// add a matcher for ops.
			UserMatch matcher;
			matcher.setFlag(UserMatch::PREDEFINED);
			matcher.setFlag(UserMatch::OPS);
			matcher.name = str(F_("Operators (added by %1%)") % APPNAME);
			matcher.style.textColor = ColorUtil::modBlue(SETTING(TEXT_COLOR), 127); // more blue
			newList.push_back(std::move(matcher));
		}

		UserMatchManager::getInstance()->setList(std::move(newList));
	}

	updateUserMatchFonts();
}

void updateUserMatchFonts() {
	userMatchFonts.clear();

	for(auto& font: UserMatchManager::getInstance()->getFonts()) {
		LOGFONT lf;
		decodeFont(Text::toT(font), lf);
		userMatchFonts[font] = new dwt::Font(lf);
	}
}

dwt::FontPtr getUserMatchFont(const string& key) {
	// cache lookup might fail when refreshing the list of user matching defs...
	auto cached = userMatchFonts.find(key);
	if(cached != userMatchFonts.end()) {
		return cached->second;
	}
	return nullptr;
}

void updateUploadFont() {
	updateFont(uploadFont, SettingsManager::UPLOAD_FONT);
}

void updateDownloadFont() {
	updateFont(downloadFont, SettingsManager::DOWNLOAD_FONT);
}

} // namespace UiFonts
