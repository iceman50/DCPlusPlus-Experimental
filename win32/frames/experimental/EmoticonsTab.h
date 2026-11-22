/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_EMOTICONS_TAB_H
#define DCPLUSPLUS_WIN32_EMOTICONS_TAB_H

#include <limits>
#include <string>
#include <vector>

#include <dcpp/message/EmoticonManager.h>

#include <win32/frames/experimental/ExperimentalTabs.h>

/** Emoticons in chat: the package, with a preview of what it contains, its display size and the
 * limits on extracting a package. */
class EmoticonsTab : public ExperimentalTab {
public:
	EmoticonsTab(TabViewPtr parent, LiveSettings& live);
	void refresh() override;

private:
	void build() override;
	void updatePreview();
	void reloadPackages(const std::string& selectedPath = std::string());
	void selectPackage(const std::string& path);
	void handlePackageSelection();
	void editSelectedPackage();
	std::string selectedPackagePath() const;
	/** Use the selected package, at once. */
	void applyPackage();

	ComboBoxPtr packageBox;
	std::vector<dcpp::EmoticonManager::Package> packages;
	std::string retainedPackagePath;
	bool changingPackageSelection;
	RichTextBoxPtr preview;
	LabelPtr previewStatus;
};

#endif // !defined(DCPLUSPLUS_WIN32_EMOTICONS_TAB_H)
