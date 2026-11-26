/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include <win32/frames/experimental/EmoticonsTab.h>

#include <dcpp/util/Util.h>
#include <dcpp/message/EmoticonManager.h>
#include <dcpp/core/io/File.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/core/header/format.h>

#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/LoadDialog.h>
#include <dwt/widgets/MessageBox.h>

#include <win32/Emoticons.h>
#include <win32/RichTextBox.h>
#include <win32/resource.h>
#include <win32/settings/EmoticonPackDlg.h>
#include <win32/settings/base/LiveSettings.h>
#include <win32/util/AppAppearance.h>
#include <win32/util/ControlSeeds.h>

using dwt::Button;
using dwt::CheckBox;
using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using namespace dcpp;

namespace {

constexpr int BYTES_PER_KIB = 1024;
constexpr int MAX_KIB_SETTING = static_cast<int>(
	(static_cast<int64_t>(std::numeric_limits<int>::max()) + BYTES_PER_KIB - 1) / BYTES_PER_KIB);

const int emoticonSizes[] = { 20, 24, 28, 32 };
const int bitDepths[] = { 16, 24, 32 };

/** Bind a drop-down of numbers to an int setting, `fallback` being the entry to show for any other value. */
void addNumberCombo(LiveSettings& live, ComboBoxPtr box, SettingsManager::IntSetting setting, const int* values,
	size_t count, size_t fallback)
{
	live.addCombo(box, setting, [box, setting, values, count, fallback] {
		const auto stored = SettingsManager::getInstance()->get(setting);
		size_t selected = fallback;
		for(size_t i = 0; i < count; ++i) {
			if(values[i] == stored) selected = i;
		}
		box->setSelected(static_cast<int>(selected));
	}, [box, setting, values, count, fallback] {
		const auto index = box->getSelected();
		const auto value = values[index >= 0 && static_cast<size_t>(index) < count ? index : fallback];
		if(value == SettingsManager::getInstance()->get(setting)) return false;
		SettingsManager::getInstance()->set(setting, value);
		return true;
	});
}

}

EmoticonsTab::EmoticonsTab(TabViewPtr parent, LiveSettings& live) :
	ExperimentalTab(parent, T_("Emoticons"), IDH_SETTINGS_EXPERIMENTAL_EMOTICONS, live),
	packageBox(nullptr),
	changingPackageSelection(false),
	preview(nullptr),
	previewStatus(nullptr)
{
}

void EmoticonsTab::build() {
	{
		auto group = addGroup(LEFT, T_("Emoticons"), IDH_SETTINGS_EXPERIMENTAL_EMOTICONS);
		auto content = group->addChild(Grid::Seed(5, 1));
		content->column(0).mode = GridInfo::FILL;

		addCheck(content, T_("Enable emoticons in chat"), SettingsManager::ENABLE_EMOTICONS, IDH_SETTINGS_EXPERIMENTAL_EMOTICONS);

		auto row = content->addChild(Grid::Seed(1, 4));
		row->column(0).mode = GridInfo::FILL;
		packageBox = row->addChild(ControlSeeds::Dialog::comboBox);
		packageBox->setHelpId(IDH_SETTINGS_EXPERIMENTAL_EMOTICONS);
		packageBox->setAccessibleName(T_("Emoticon package"));
		packageBox->onSelectionChanged([this] { handlePackageSelection(); });
		row->addChild(Button::Seed(T_("Browse...")))->onClicked([this] {
			const auto current = selectedPackagePath();
			auto path = Text::toT(current.empty() ? EmoticonManager::getDirectory() : Util::getFilePath(current));
			if(dwt::LoadDialog(this).addFilter(T_("DC++ emoticon packages"), _T("*.dcemo"))
				.addFilter(T_("All files"), _T("*.*")).open(path)) {
				selectPackage(Text::fromT(path));
				applyPackage();
			}
		});
		row->addChild(Button::Seed(T_("Build...")))->onClicked([this] {
			EmoticonPackDlg dialog(this, tstring(), Text::toT(EmoticonManager::getDirectory() + "Custom.dcemo"));
			if(dialog.run() == IDOK) {
				reloadPackages(Text::fromT(dialog.getExportedPath()));
				applyPackage();
			}
		});
		row->addChild(Button::Seed(T_("Import XML...")))->onClicked([this] {
			tstring source;
			if(!dwt::LoadDialog(this).addFilter(T_("XML emoticon packages"), _T("*.xml"))
				.addFilter(T_("All files"), _T("*.*")).open(source)) return;
			EmoticonPackDlg dialog(this, std::move(source));
			if(dialog.run() == IDOK) {
				reloadPackages(Text::fromT(dialog.getExportedPath()));
				applyPackage();
			}
		});

		auto sizeRow = content->addChild(Grid::Seed(1, 2));
		sizeRow->column(1).mode = GridInfo::FILL;
		sizeRow->addChild(Label::Seed(T_("Display size")));
		auto sizeBox = sizeRow->addChild(ControlSeeds::Dialog::comboBox);
		sizeBox->setAccessibleName(T_("Emoticon display size"));
		for(auto size: emoticonSizes) {
			sizeBox->addValue(Text::toT(std::to_string(size) + " x " + std::to_string(size)));
		}
		addNumberCombo(live, sizeBox, SettingsManager::EMOTICON_SIZE, emoticonSizes, std::size(emoticonSizes), 1);

		auto depthRow = content->addChild(Grid::Seed(1, 2));
		depthRow->column(1).mode = GridInfo::FILL;
		depthRow->addChild(Label::Seed(T_("Preferred emoticon bit depth")));
		auto depthBox = depthRow->addChild(ControlSeeds::Dialog::comboBox);
		depthBox->setAccessibleName(T_("Preferred emoticon bit depth"));
		for(auto depth: bitDepths) {
			depthBox->addValue(Text::toT(std::to_string(depth) + " bpp"));
		}
		addNumberCombo(live, depthBox, SettingsManager::EMOTICON_BIT_DEPTH, bitDepths, std::size(bitDepths), 0);

		content->addChild(Label::Seed(T_("Choose Custom / edit selected from the package list\r\nto add, update, or remove emoticons.")));
	}

	{
		auto group = addGroup(LEFT, T_("Package extraction limits"), IDH_SETTINGS_EXPERIMENTAL_ARCHIVE_MAX_ENTRIES);
		auto cur = group->addChild(Grid::Seed(3, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Maximum package entries"), SettingsManager::ARCHIVE_MAX_ENTRIES,
			IDH_SETTINGS_EXPERIMENTAL_ARCHIVE_MAX_ENTRIES, T_("entries"),
			SettingsManager::ARCHIVE_MAX_ENTRIES_MIN, SettingsManager::ARCHIVE_MAX_ENTRIES_MAX);
		addInt(cur, T_("Maximum extracted file size"), SettingsManager::ARCHIVE_MAX_FILE_BYTES,
			IDH_SETTINGS_EXPERIMENTAL_ARCHIVE_MAX_FILE_BYTES, T_("KiB"),
			SettingsManager::ARCHIVE_MAX_BYTES_MIN / BYTES_PER_KIB, MAX_KIB_SETTING, BYTES_PER_KIB);
		addInt(cur, T_("Maximum total extracted size"), SettingsManager::ARCHIVE_MAX_TOTAL_BYTES,
			IDH_SETTINGS_EXPERIMENTAL_ARCHIVE_MAX_TOTAL_BYTES, T_("KiB"),
			SettingsManager::ARCHIVE_MAX_BYTES_MIN / BYTES_PER_KIB, MAX_KIB_SETTING, BYTES_PER_KIB);
	}

	{
		auto group = addGroup(RIGHT, T_("Loaded emoticons"), IDH_SETTINGS_EXPERIMENTAL_EMOTICONS, true);
		auto content = group->addChild(Grid::Seed(2, 1));
		content->column(0).mode = GridInfo::FILL;
		content->row(0).mode = GridInfo::FILL;
		content->row(0).size = content->scale(160);
		content->row(0).align = GridInfo::STRETCH;
		RichTextBox::Seed seed = ControlSeeds::Dialog::richTextBox;
		seed.style |= ES_READONLY | WS_VSCROLL;
		preview = dwt::WidgetCreator<RichTextBox>::create(content, seed);
		preview->setAccessibleName(T_("Emoticon package preview"));
		AppAppearance::setColor(preview);
		// Filled in once the preview is read; a line high already, so nothing moves then.
		previewStatus = content->addChild(Label::Seed(_T(" ")));
	}

	// Reading the packages and extracting the preview wait until the tab is on screen.
	callAsync([this] {
		reloadPackages(SETTING(EMOTICON_PACK));
		updatePreview();
	});
}

void EmoticonsTab::refresh() {
	if(packageBox->size() == 0) {
		// Not listed yet.
		return;
	}
	if(Util::stricmp(selectedPackagePath(), SETTING(EMOTICON_PACK)) != 0) {
		selectPackage(SETTING(EMOTICON_PACK));
		updatePreview();
	}
}

void EmoticonsTab::applyPackage() {
	const auto path = selectedPackagePath();
	if(path != SETTING(EMOTICON_PACK)) {
		SettingsManager::getInstance()->set(SettingsManager::EMOTICON_PACK, path);
		live.notify(SettingsManager::EMOTICON_PACK);
	}
	updatePreview();
}

std::string EmoticonsTab::selectedPackagePath() const {
	const auto selected = packageBox ? packageBox->getSelected() : 0;
	return selected > 0 && static_cast<size_t>(selected) <= packages.size() ? packages[selected - 1].path : std::string();
}

void EmoticonsTab::selectPackage(const std::string& path) {
	changingPackageSelection = true;
	if(path.empty()) {
		packageBox->setSelected(0);
		retainedPackagePath.clear();
		changingPackageSelection = false;
		return;
	}
	for(size_t index = 0; index < packages.size(); ++index) {
		if(Util::stricmp(packages[index].path, path) == 0) {
			packageBox->setSelected(static_cast<int>(index + 1));
			retainedPackagePath = packages[index].path;
			changingPackageSelection = false;
			return;
		}
	}
	if(File::getSize(path) < 0) {
		for(size_t index = 0; index < packages.size(); ++index) {
			if(Util::stricmp(Util::getFileName(packages[index].path), Util::getFileName(path)) == 0) {
				packageBox->setSelected(static_cast<int>(index + 1));
				retainedPackagePath = packages[index].path;
				changingPackageSelection = false;
				return;
			}
		}
	}
	try {
		packages.push_back(EmoticonManager::inspectPackage(path));
		packageBox->insertValue(static_cast<int>(packages.size()), Text::toT(packages.back().name) + T_(" (external DCEMO)"));
		packageBox->setSelected(static_cast<int>(packages.size()));
		retainedPackagePath = packages.back().path;
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(T_("Unable to load the emoticon package:\r\n") + Text::toT(e.getError()),
			T_("Emoticon package"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
	changingPackageSelection = false;
}

void EmoticonsTab::reloadPackages(const std::string& selectedPath) {
	packages = EmoticonManager::getPackages();
	packageBox->clear();
	packageBox->addValue(T_("(No package)"));
	for(const auto& package: packages) packageBox->addValue(Text::toT(package.name) + T_(" (DCEMO)"));
	packageBox->addValue(T_("Custom / edit selected..."));
	selectPackage(selectedPath);
}

void EmoticonsTab::handlePackageSelection() {
	if(changingPackageSelection) return;
	if(packageBox->getSelected() == static_cast<int>(packages.size() + 1)) {
		editSelectedPackage();
		return;
	}
	retainedPackagePath = selectedPackagePath();
	applyPackage();
}

void EmoticonsTab::editSelectedPackage() {
	const auto source = retainedPackagePath;
	changingPackageSelection = true;
	selectPackage(source);
	changingPackageSelection = false;
	const auto fileName = source.empty() ? string("Custom.dcemo") : Util::getFileName(source);
	const auto target = EmoticonManager::getDirectory() + fileName;
	EmoticonPackDlg dialog(this, Text::toT(source), Text::toT(target));
	if(dialog.run() == IDOK) {
		reloadPackages(Text::fromT(dialog.getExportedPath()));
		applyPackage();
	}
}

void EmoticonsTab::updatePreview() {
	preview->setText(Util::emptyStringT);
	const auto path = selectedPackagePath();
	if(path.empty()) {
		previewStatus->setText(T_("No emoticon package selected."));
		return;
	}

	try {
		const auto package = EmoticonManager::previewPackage(path);
		constexpr size_t maxPreviewItems = 128;
		const auto previewItems = std::min(package.items.size(), maxPreviewItems);
		std::vector<tstring> documents;
		documents.reserve(previewItems);
		size_t ruleCount = 0;
		for(const auto& item: package.items) ruleCount += item.rules.size();
		size_t failedImages = 0;
		for(size_t index = 0; index < previewItems; ++index) {
			const auto& item = package.items[index];
			tstring rules;
			for(const auto& rule: item.rules) {
				if(!rules.empty()) rules += _T("   ");
				rules += Text::toT(rule);
			}
			tstring document = _T("{\\urtf1\\viewkind4\\uc1 ");
			const auto image = Emoticons::fileRtf(item.iconPath, 40);
			if(image.empty()) {
				document += dwt::RichTextBox::rtfEscape(T_("[image unavailable]"));
				++failedImages;
			} else {
				document += image;
			}
			document += _T("\\tab\\b ") + dwt::RichTextBox::rtfEscape(Text::toT(item.name));
			document += _T("\\b0\\tab ") + dwt::RichTextBox::rtfEscape(rules);
			if(index + 1 < previewItems) document += _T("\\line ");
			document += _T("}");
			documents.push_back(std::move(document));
		}
		preview->addTextSteadyBatch(documents);
		auto status = str(TF_("%1% version %2%: %3% emoticons, %4% shortcuts") %
			Text::toT(package.name) % Text::toT(package.version) % package.items.size() % ruleCount);
		if(previewItems < package.items.size()) status += str(TF_("; showing the first %1%") % previewItems);
		if(failedImages) status += str(TF_("; %1% images unavailable") % failedImages);
		previewStatus->setText(status);
	} catch(const Exception& e) {
		previewStatus->setText(T_("Unable to preview package: ") + Text::toT(e.getError()));
	}
}
