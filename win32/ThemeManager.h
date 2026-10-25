/*
 * Copyright (C) 2001-2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_THEME_MANAGER_H
#define DCPLUSPLUS_WIN32_THEME_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <dcpp/Style.h>
#include <dcpp/UserMatch.h>

#include <dwt/Appearance.h>
#include <dwt/tstring.h>

/** A complete application theme. Matching rules are never imported from theme files. */
class ThemeManager {
public:
	struct Theme {
		Theme(const dwt::tstring& name_, const dwt::Appearance::Palette& palette_, const std::string& path_ = std::string());

		dwt::tstring name;
		dwt::Appearance::Palette palette;
		std::string path;
		std::map<int, std::string> fonts;
		std::map<int, int> colors;
		// Occurrence 0 is a legacy name-wide style; duplicates use 1-based order.
		std::map<std::pair<std::string, unsigned>, dcpp::Style> userStyles;
		// Draft preview resolution only; session identities never enter theme XML.
		std::map<std::string, dcpp::Style> liveUserStyles;
		void captureUserStyles(const std::vector<dcpp::UserMatch>& matches);
		void applyUserStyles(std::vector<dcpp::UserMatch>& matches) const;
	};
	static const std::vector<int>& fontSettings();
	static const std::vector<int>& colorSettings();
	static Theme capture();
	static void apply(const Theme& theme, bool updateUsers = true);
	static bool isCurrent(const Theme& theme);

	static std::string getDirectory();
	static std::vector<Theme> getThemes();
	static Theme load(const std::string& path);
	static std::string getImportPath(const std::string& source);
	static std::string importTheme(const std::string& source);
	static std::string saveTheme(const std::string& target, const Theme& theme);
};

#endif
