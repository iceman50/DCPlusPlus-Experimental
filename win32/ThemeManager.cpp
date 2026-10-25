/*
 * Copyright (C) 2001-2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "ThemeManager.h"

#include <algorithm>
#include <cstdint>

#include <dcpp/File.h>
#include <dcpp/FontStyle.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/StringTokenizer.h>
#include <dcpp/UserMatchManager.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/Util.h>

namespace {

constexpr int64_t MAX_THEME_FILE_SIZE = 64 * 1024;

int hexDigit(char value) {
	if(value >= '0' && value <= '9') return value - '0';
	if(value >= 'a' && value <= 'f') return value - 'a' + 10;
	if(value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

COLORREF parseColor(std::string value, const std::string& attribute, const std::string& path) {
	Util::trim(value);
	if(!value.empty() && value.front() == '#') value.erase(value.begin());
	if(value.size() != 6) throw Exception("Invalid " + attribute + " color in " + path + "; expected #RRGGBB");

	unsigned rgb = 0;
	for(const auto valuePart: value) {
		const auto digit = hexDigit(valuePart);
		if(digit < 0) throw Exception("Invalid " + attribute + " color in " + path + "; expected #RRGGBB");
		rgb = rgb * 16 + static_cast<unsigned>(digit);
	}
	return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

COLORREF readColor(SimpleXML& xml, const std::string& attribute, const std::string& path) {
	const auto value = xml.getChildAttrib(attribute);
	if(value.empty()) throw Exception("Missing " + attribute + " palette attribute in " + path);
	return parseColor(value, attribute, path);
}

dwt::tstring fileThemeName(const std::string& path) {
	auto name = Util::getFileName(path);
	const auto extension = Util::getFileExt(name);
	if(!extension.empty()) name.erase(name.size() - extension.size());
	return Text::toT(name);
}

std::string writeColor(COLORREF color) {
	return "#" + Util::cssColor(color);
}

std::string readFont(const std::string& value) {
	if(value.empty()) return value;
	StringTokenizer<std::string> tokens(value, ',');
	const auto& fields = tokens.getTokens();
	if(fields.size() < 5 || fields.size() > 12 || fields[0].empty() || fields[0].size() > 120 ||
		fields[0].find_first_of(";\r\n\t") != std::string::npos)
		throw Exception("Invalid theme font");
	for(size_t i = 1; i < fields.size(); ++i) {
		const auto& field = fields[i];
		const auto start = !field.empty() && field[0] == '-' ? 1U : 0U;
		if(start == field.size() || field.size() > 7 || field.find_first_not_of("0123456789", start) != std::string::npos)
			throw Exception("Invalid numeric theme font field");
	}
	if(std::abs(Util::toInt(fields[1])) < 1 || std::abs(Util::toInt(fields[1])) > 256 ||
		Util::toInt(fields[2]) < 0 || Util::toInt(fields[2]) > 1000 ||
		Util::toInt(fields[3]) < 0 || Util::toInt(fields[3]) > 1 ||
		Util::toInt(fields[4]) < 0 || Util::toInt(fields[4]) > 255) throw Exception("Theme font is out of range");
	return FontStyle::decode(value).encode(value);
}

int settingKey(const std::string& name, const std::vector<int>& allowed) {
	const auto& tags = SettingsManager::getInstance()->getSettingTags();
	for(int key : allowed) if(tags[key] == name) return key;
	throw Exception("Unknown theme appearance setting: " + name);
}

}

const std::vector<int>& ThemeManager::fontSettings() {
	using S = SettingsManager;
	static const std::vector<int> values { S::MAIN_FONT, S::TEXT_VIEWER_FONT, S::UPLOAD_FONT, S::DOWNLOAD_FONT,
		S::LINK_FONT, S::LOG_FONT, S::CHAT_TIMESTAMP_FONT, S::CHAT_NICK_FONT, S::CHAT_TEXT_FONT,
		S::CHAT_SYSTEM_FONT, S::CHAT_OWN_TIMESTAMP_FONT, S::CHAT_OWN_NICK_FONT, S::CHAT_OWN_TEXT_FONT, S::CHAT_MENTION_FONT };
	return values;
}

const std::vector<int>& ThemeManager::colorSettings() {
	using S = SettingsManager;
	static const std::vector<int> values { S::TEXT_COLOR, S::BACKGROUND_COLOR,
		S::UPLOAD_TEXT_COLOR, S::UPLOAD_BG_COLOR, S::DOWNLOAD_TEXT_COLOR, S::DOWNLOAD_BG_COLOR,
		S::LINK_COLOR, S::LINK_BG_COLOR, S::LOG_COLOR, S::LOG_BG_COLOR,
		S::SYSTEM_LOG_VERBOSE_COLOR, S::SYSTEM_LOG_INFO_COLOR, S::SYSTEM_LOG_WARNING_COLOR,
		S::SYSTEM_LOG_ERROR_COLOR, S::SYSTEM_LOG_AREA_COLOR,
		S::CHAT_TIMESTAMP_COLOR, S::CHAT_TIMESTAMP_BG_COLOR, S::CHAT_NICK_COLOR, S::CHAT_NICK_BG_COLOR,
		S::CHAT_TEXT_COLOR, S::CHAT_TEXT_BG_COLOR, S::CHAT_SYSTEM_COLOR, S::CHAT_SYSTEM_BG_COLOR,
		S::CHAT_OWN_TIMESTAMP_COLOR, S::CHAT_OWN_TIMESTAMP_BG_COLOR, S::CHAT_OWN_NICK_COLOR, S::CHAT_OWN_NICK_BG_COLOR,
		S::CHAT_OWN_TEXT_COLOR, S::CHAT_OWN_TEXT_BG_COLOR, S::CHAT_MENTION_COLOR, S::CHAT_MENTION_BG_COLOR };
	return values;
}

ThemeManager::Theme::Theme(const dwt::tstring& name_, const dwt::Appearance::Palette& palette_, const std::string& path_) :
	name(name_), palette(palette_), path(path_) {
	auto settings = SettingsManager::getInstance();
	for(int key : fontSettings()) fonts[key] = key == SettingsManager::MAIN_FONT ? settings->getDefault(SettingsManager::MAIN_FONT) : std::string();
	const auto& tags = settings->getSettingTags();
	for(int key : colorSettings()) {
		const auto& tag = tags[key];
		colors[key] = tag.find("BgColor") != std::string::npos || key == SettingsManager::BACKGROUND_COLOR ? palette.background : palette.text;
	}
	for(int key : { SettingsManager::LINK_COLOR, SettingsManager::CHAT_NICK_COLOR, SettingsManager::CHAT_OWN_NICK_COLOR,
		SettingsManager::CHAT_MENTION_COLOR, SettingsManager::SYSTEM_LOG_AREA_COLOR }) colors[key] = palette.accent;
	colors[SettingsManager::CHAT_TIMESTAMP_COLOR] = palette.disabledText;
	colors[SettingsManager::CHAT_OWN_TIMESTAMP_COLOR] = palette.disabledText;
	colors[SettingsManager::SYSTEM_LOG_VERBOSE_COLOR] = palette.disabledText;
	colors[SettingsManager::SYSTEM_LOG_WARNING_COLOR] = RGB(220, 150, 40);
	colors[SettingsManager::SYSTEM_LOG_ERROR_COLOR] = RGB(230, 80, 80);
	const int keys[] = { SettingsManager::THEME_BACKGROUND_COLOR, SettingsManager::THEME_SURFACE_COLOR,
		SettingsManager::THEME_TEXT_COLOR, SettingsManager::THEME_DISABLED_TEXT_COLOR, SettingsManager::THEME_BORDER_COLOR,
		SettingsManager::THEME_ACCENT_COLOR, SettingsManager::THEME_HIGHLIGHT_TEXT_COLOR };
	const COLORREF values[] = { palette.background, palette.surface, palette.text, palette.disabledText, palette.border, palette.accent, palette.highlightText };
	for(size_t i = 0; i < std::size(keys); ++i) colors[keys[i]] = values[i];
}

void ThemeManager::Theme::captureUserStyles(const std::vector<UserMatch>& matches) {
	userStyles.clear();
	liveUserStyles.clear();
	std::map<std::string, unsigned> counts, occurrences;
	for(const auto& match : matches) ++counts[match.name];
	for(const auto& match : matches) {
		const auto occurrence = counts[match.name] > 1 ? ++occurrences[match.name] : 0;
		userStyles[{ match.name, occurrence }] = match.style;
		liveUserStyles[match.styleId] = match.style;
	}
}

void ThemeManager::Theme::applyUserStyles(std::vector<UserMatch>& matches) const {
	std::map<std::string, unsigned> occurrences;
	for(auto& match : matches) {
		const auto occurrence = ++occurrences[match.name];
		auto found = userStyles.find({ match.name, occurrence });
		if(found == userStyles.end()) found = userStyles.find({ match.name, 0 });
		match.style = found == userStyles.end() ? Style() : found->second;
	}
}

ThemeManager::Theme ThemeManager::capture() {
	auto settings = SettingsManager::getInstance();
	Theme theme(T_("Custom"), { static_cast<COLORREF>(SETTING(THEME_BACKGROUND_COLOR)), static_cast<COLORREF>(SETTING(THEME_SURFACE_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_TEXT_COLOR)), static_cast<COLORREF>(SETTING(THEME_DISABLED_TEXT_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_BORDER_COLOR)), static_cast<COLORREF>(SETTING(THEME_ACCENT_COLOR)), static_cast<COLORREF>(SETTING(THEME_HIGHLIGHT_TEXT_COLOR)) });
	for(int key : fontSettings()) theme.fonts[key] = settings->get(static_cast<SettingsManager::StrSetting>(key));
	for(int key : colorSettings()) theme.colors[key] = settings->get(static_cast<SettingsManager::IntSetting>(key));
	theme.captureUserStyles(UserMatchManager::getInstance()->getList());
	return theme;
}

void ThemeManager::apply(const Theme& theme, bool updateUsers) {
	auto settings = SettingsManager::getInstance();
	auto matches = UserMatchManager::getInstance()->getList();
	theme.applyUserStyles(matches);
	for(const auto& item : theme.fonts) {
		const auto key = static_cast<SettingsManager::StrSetting>(item.first);
		if(item.second.empty()) settings->unset(key);
		else settings->set(key, item.second);
	}
	for(const auto& item : theme.colors) {
		const auto key = static_cast<SettingsManager::IntSetting>(item.first);
		if(item.second < 0) settings->unset(key);
		else settings->set(key, item.second);
	}
	// Named palettes use DWT's custom renderer, including palettes with light colors.
	settings->set(SettingsManager::THEME_MODE, SettingsManager::THEME_DARK);
	UserMatchManager::getInstance()->setList(std::move(matches), updateUsers);
}

bool ThemeManager::isCurrent(const Theme& theme) {
	if(SETTING(THEME_MODE) != SettingsManager::THEME_DARK) return false;
	auto settings = SettingsManager::getInstance();
	for(const auto& item : theme.fonts) {
		const auto key = static_cast<SettingsManager::StrSetting>(item.first);
		if(settings->get(key) != (item.second.empty() ? settings->getDefault(key) : item.second)) return false;
	}
	for(const auto& item : theme.colors) {
		const auto key = static_cast<SettingsManager::IntSetting>(item.first);
		if(settings->get(key) != (item.second < 0 ? settings->getDefault(key) : item.second)) return false;
	}
	const auto current = UserMatchManager::getInstance()->getList();
	auto expected = current;
	theme.applyUserStyles(expected);
	for(size_t i = 0; i < current.size(); ++i) {
		const auto& a = current[i].style;
		const auto& b = expected[i].style;
		if(a.font != b.font || a.textColor != b.textColor || a.bgColor != b.bgColor) return false;
	}
	return true;
}

std::string ThemeManager::getDirectory() {
	return Util::getPath(Util::PATH_USER_CONFIG) + "Themes" PATH_SEPARATOR_STR;
}

std::vector<ThemeManager::Theme> ThemeManager::getThemes() {
	std::vector<Theme> themes = {
		{ T_("Default dark"), dwt::Appearance::defaultPalette() },
		{ T_("Midnight blue"), { RGB(17, 24, 39), RGB(31, 41, 55), RGB(229, 231, 235), RGB(156, 163, 175), RGB(55, 65, 81), RGB(59, 130, 246), RGB(255, 255, 255) } },
		{ T_("Graphite"), { RGB(24, 24, 24), RGB(38, 38, 38), RGB(235, 235, 235), RGB(155, 155, 155), RGB(70, 70, 70), RGB(58, 150, 221), RGB(255, 255, 255) } },
		{ T_("Dark slate"), { RGB(24, 28, 32), RGB(38, 44, 50), RGB(232, 235, 238), RGB(151, 158, 164), RGB(68, 76, 84), RGB(38, 166, 154), RGB(255, 255, 255) } }
	};

	std::vector<Theme> files;
	auto loadDirectory = [&files](const std::string& directory) {
		for(const auto& path: File::findFiles(directory, "*.xml")) {
			try {
				auto theme = load(path);
				auto existing = std::find_if(files.begin(), files.end(), [&path](const Theme& item) {
					return Util::stricmp(Util::getFileName(item.path), Util::getFileName(path)) == 0;
				});
				if(existing == files.end()) {
					files.push_back(std::move(theme));
				} else {
					*existing = std::move(theme);
				}
			} catch(const Exception&) {
			}
		}
	};

	// Distribution themes stay beside the executable in the flat Themes directory.
	// Loading the per-user directory afterward lets an imported file with the
	// same name override its application counterpart without producing duplicates.
	loadDirectory(Util::getPath(Util::PATH_GLOBAL_CONFIG) + "Themes" PATH_SEPARATOR_STR);
	const auto userDirectory = getDirectory();
	File::ensureDirectory(userDirectory);
	loadDirectory(userDirectory);
	std::sort(files.begin(), files.end(), [](const Theme& lhs, const Theme& rhs) { return lhs.name < rhs.name; });
	themes.insert(themes.end(), files.begin(), files.end());
	return themes;
}

ThemeManager::Theme ThemeManager::load(const std::string& path) {
	const auto size = File::getSize(path);
	if(size < 0) throw Exception("Theme file does not exist: " + path);
	if(size > MAX_THEME_FILE_SIZE) throw Exception("Theme file is larger than 64 KiB: " + path);

	SimpleXML xml;
	xml.fromXML(File(path, File::READ, File::OPEN).read());
	if(!xml.findChild("DCPlusPlusTheme")) throw Exception("Invalid theme root in " + path + "; expected DCPlusPlusTheme");
	auto name = Text::toT(xml.getChildAttrib("Name"));
	if(name.empty()) name = fileThemeName(path);
	xml.stepIn();
	if(!xml.findChild("Palette")) throw Exception("Missing Palette element in " + path);

	dwt::Appearance::Palette palette = {
		readColor(xml, "Background", path),
		readColor(xml, "Surface", path),
		readColor(xml, "Text", path),
		readColor(xml, "DisabledText", path),
		readColor(xml, "Border", path),
		readColor(xml, "Accent", path),
		readColor(xml, "HighlightText", path)
	};
	Theme result(name, palette, path);
	xml.resetCurrentChild();
	if(xml.findChild("Fonts")) {
		xml.stepIn();
		while(xml.findChild("Font")) result.fonts[settingKey(xml.getChildAttrib("Setting"), fontSettings())] = readFont(xml.getChildAttrib("Value"));
		xml.stepOut();
	}
	xml.resetCurrentChild();
	if(xml.findChild("Colors")) {
		xml.stepIn();
		while(xml.findChild("Color")) result.colors[settingKey(xml.getChildAttrib("Setting"), colorSettings())] = readColor(xml, "Value", path);
		xml.stepOut();
	}
	xml.resetCurrentChild();
	if(xml.findChild("UserStyles")) {
		xml.stepIn();
		while(xml.findChild("Style")) {
			const auto name = xml.getChildAttrib("Name");
			const auto number = xml.getChildAttrib("Occurrence");
			unsigned occurrence = 0;
			if(!number.empty()) {
				if(number.size() > 6 || number.find_first_not_of("0123456789") != std::string::npos ||
					(occurrence = Util::toInt(number)) == 0) throw Exception("Invalid user style occurrence in " + path);
			}
			const auto key = std::make_pair(name, occurrence);
			if(name.empty() || result.userStyles.count(key)) throw Exception("Empty or duplicate user style identity in " + path);
			Style style;
			style.font = readFont(xml.getChildAttrib("Font"));
			if(!xml.getChildAttrib("TextColor").empty()) style.textColor = readColor(xml, "TextColor", path);
			if(!xml.getChildAttrib("BgColor").empty()) style.bgColor = readColor(xml, "BgColor", path);
			result.userStyles.emplace(key, std::move(style));
		}
		xml.stepOut();
		std::map<std::string, unsigned> occurrences;
		for(const auto& item : result.userStyles) {
			if(item.first.second && (result.userStyles.count({ item.first.first, 0 }) ||
				item.first.second != ++occurrences[item.first.first]))
				throw Exception("Ambiguous or nonconsecutive user style occurrences in " + path);
		}
	}
	return result;
}

std::string ThemeManager::getImportPath(const std::string& source) {
	auto name = Util::getFileName(source);
	if(Text::toLower(Util::getFileExt(name)) != ".xml") name += ".xml";
	return getDirectory() + name;
}

std::string ThemeManager::importTheme(const std::string& source) {
	load(source);
	const auto target = getImportPath(source);
	File::ensureDirectory(target);
	if(Util::stricmp(source, target) != 0) File::copyFile(source, target);
	return target;
}

std::string ThemeManager::saveTheme(const std::string& target, const Theme& theme) {
	const auto& palette = theme.palette;
	auto path = target;
	if(Text::toLower(Util::getFileExt(path)) != ".xml") path += ".xml";
	File::ensureDirectory(path);

	SimpleXML xml;
	xml.addTag("DCPlusPlusTheme");
	xml.addChildAttrib("Name", Text::fromT(fileThemeName(path)));
	const bool duplicates = std::any_of(theme.userStyles.begin(), theme.userStyles.end(),
		[](const auto& item) { return item.first.second != 0; });
	xml.addChildAttrib("Version", duplicates ? 3 : 2);
	xml.stepIn();
	xml.addTag("Palette");
	xml.addChildAttrib("Background", writeColor(palette.background));
	xml.addChildAttrib("Surface", writeColor(palette.surface));
	xml.addChildAttrib("Text", writeColor(palette.text));
	xml.addChildAttrib("DisabledText", writeColor(palette.disabledText));
	xml.addChildAttrib("Border", writeColor(palette.border));
	xml.addChildAttrib("Accent", writeColor(palette.accent));
	xml.addChildAttrib("HighlightText", writeColor(palette.highlightText));
	const auto& tags = SettingsManager::getInstance()->getSettingTags();
	xml.addTag("Fonts");
	xml.stepIn();
	for(int key : fontSettings()) {
		xml.addTag("Font");
		xml.addChildAttrib("Setting", tags[key]);
		xml.addChildAttrib("Value", theme.fonts.at(key));
	}
	xml.stepOut();
	xml.addTag("Colors");
	xml.stepIn();
	for(int key : colorSettings()) {
		xml.addTag("Color");
		xml.addChildAttrib("Setting", tags[key]);
		xml.addChildAttrib("Value", writeColor(theme.colors.at(key)));
	}
	xml.stepOut();
	xml.addTag("UserStyles");
	xml.stepIn();
	for(const auto& item : theme.userStyles) {
		xml.addTag("Style");
		xml.addChildAttrib("Name", item.first.first);
		if(item.first.second) xml.addChildAttrib("Occurrence", item.first.second);
		xml.addChildAttrib("Font", item.second.font);
		if(item.second.textColor >= 0) xml.addChildAttrib("TextColor", writeColor(item.second.textColor));
		if(item.second.bgColor >= 0) xml.addChildAttrib("BgColor", writeColor(item.second.bgColor));
	}
	xml.stepOut();
	xml.stepOut();

	const auto temporary = path + ".tmp";
	try {
		File output(temporary, File::WRITE, File::CREATE | File::TRUNCATE);
		output.write(SimpleXML::utf8Header);
		const auto document = xml.toXML();
		if(document.size() + SimpleXML::utf8Header.size() > MAX_THEME_FILE_SIZE) throw Exception("Theme is larger than 64 KiB");
		output.write(document);
		output.close();
		File::renameFile(temporary, path);
	} catch(...) {
		File::deleteFile(temporary);
		throw;
	}
	return path;
}
