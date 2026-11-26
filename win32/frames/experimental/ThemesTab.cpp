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
#include <dcpp/core/io/xml/SimpleXML.h>
#include <dcpp/util/Util.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/IconManager.h>
#include <win32/util/UiFonts.h>
#include <win32/util/UiServices.h>
#include <win32/components/TableLayout.h>
#include <win32/util/ShellActions.h>
#include <win32/frames/experimental/ThemesTab.h>

#include <dcpp/settings/SettingsManager.h>
#include <dcpp/user/UserMatchManager.h>
#include <cmath>

#include <dwt/Appearance.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ColorDialog.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/FontDialog.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>

#include <win32/resource.h>
#include <win32/MainWindow.h>
#include <win32/components/TypedTable.h>
#include <win32/settings/UserMatchPage.h>
#include <win32/settings/base/LiveSettings.h>
#include <win32/HtmlToRtf.h>
#include <win32/RichTextBox.h>
#include <win32/settings/TextEffectsDlg.h>
#include <dcpp/core/io/File.h>
#include <dwt/widgets/LoadDialog.h>
#include <dwt/widgets/SaveDialog.h>
#include <dwt/widgets/MessageBox.h>

using dwt::Button;
using dwt::ColorDialog;
using dwt::FontDialog;
using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;

static const TableLayout::ColumnInfo styleColumns[] = {
	{ "", 100, false }
};

ThemesTab::ThemesTab(TabViewPtr parent, LiveSettings& live) :
ExperimentalTab(parent, T_("Themes"), IDH_STYLESPAGE, live),
globalData(0),
table(0),
preview(0),
customFont(0),
font(0),
customTextColor(0),
textColor(0),
customBgColor(0),
bgColor(0),
showGen(0)
{
}

void ThemesTab::build() {
	{
		auto group = addGroup(LEFT, T_("Theme"), IDH_STYLESPAGE);
		auto cur = group->addChild(Grid::Seed(5, 1));
		cur->column(0).mode = GridInfo::FILL;

		// Each selector keeps its content width; the row's last column takes the rest.
		auto modeRow = cur->addChild(Grid::Seed(1, 3));
		modeRow->column(2).mode = GridInfo::FILL;
		modeRow->addChild(Label::Seed(T_("Mode")));
		themeMode = modeRow->addChild(ControlSeeds::Dialog::comboBox);
		themeMode->setAccessibleName(T_("Theme mode"));
		for(const auto& name : { T_("Follow Windows"), T_("Light"), T_("Dark") }) themeMode->addValue(name);
		themeMode->setSelected(std::clamp(SETTING(THEME_MODE), 0, 2));
		themeMode->onSelectionChanged([this] { if(!loading) scheduleApply(); });

		auto presetRow = cur->addChild(Grid::Seed(1, 3));
		presetRow->column(2).mode = GridInfo::FILL;
		presetRow->addChild(Label::Seed(T_("Theme")));
		themePreset = presetRow->addChild(ControlSeeds::Dialog::comboBox);
		themePreset->setAccessibleName(T_("Theme"));
		themePreset->sendMessage(CB_SETMINVISIBLE, 12);
		themePreset->onSelectionChanged([this] {
			const auto selected = themePreset->getSelected();
			if(!loading && selected >= 0 && static_cast<size_t>(selected) < presets.size()) applyTheme(presets[selected]);
		});

		auto actions = cur->addChild(Grid::Seed(1, 4));
		actions->addChild(Button::Seed(T_("Save theme...")))->onClicked([this] { saveTheme(); });
		actions->addChild(Button::Seed(T_("Import theme...")))->onClicked([this] { importTheme(); });
		actions->addChild(Button::Seed(T_("Open folder")))->onClicked([] { ShellActions::openFile(Text::toT(ThemeManager::getDirectory())); });
		actions->addChild(Button::Seed(T_("Reload themes")))->onClicked([this] {
			reloadThemes();
			if(UiServices::mainWindow) UiServices::mainWindow->reloadThemes();
		});

		dialogBorders = addCheck(cur, T_("Show borders around themed dialogs"), SettingsManager::THEMED_DIALOG_BORDERS,
			IDH_SETTINGS_APPEARANCE_DIALOG_BORDERS);
		// The undocumented functions it uses exist with this meaning from Windows 10 1903 on.
		auto nativeDark = addCheck(cur, T_("Let Windows draw dark controls (Windows 10 1903 and later)"),
			SettingsManager::THEME_NATIVE_DARK, IDH_SETTINGS_APPEARANCE_NATIVE_DARK);
		nativeDark->setEnabled(dwt::Appearance::supportsNativeDark());
		// Whether Windows draws dark changes with this option, the mode and Windows' own mode.
		appearanceCallback = dwt::Application::instance().getAppearance().onChanged([this] { updateDialogBorders(); });
		observingAppearance = true;
		updateDialogBorders();
	}

	{
		auto cur = addGroup(LEFT, T_("Styles"), IDH_STYLESPAGE, true)->addChild(Grid::Seed(6, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->row(0).mode = GridInfo::FILL;
		cur->row(0).size = cur->scale(160);
		cur->row(0).align = GridInfo::STRETCH;

		{
			auto seed = ControlSeeds::Dialog::table;
			seed.style &= ~LVS_SHOWSELALWAYS;
			seed.style |= LVS_SINGLESEL | LVS_NOCOLUMNHEADER;
			table = cur->addChild(Table::Seed(seed));

			dwt::ImageListPtr images(new dwt::ImageList(dwt::Point(16, 16)));
			images->add(*IconManager::load(IDI_DCPP, 16));
			images->add(*IconManager::load(IDI_DCPP, 16));
			images->add(*IconManager::load(IDI_NET_STATS, 16)); /// @todo better icon for the "transfers" group?
			images->add(*IconManager::load(IDI_CHAT, 16));
			images->add(*IconManager::load(IDI_USERS, 16));
			table->setGroupImageList(images);
		}

		auto row = cur->addChild(Grid::Seed(1, 3));
		row->setHelpId(IDH_SETTINGS_STYLES_FONT);
		customFont = row->addChild(CheckBox::Seed(T_("Custom font")));
		customFont->onClicked([this] { handleCustomFont(); });
		font = row->addChild(Button::Seed(T_("Select font")));
		font->onClicked([this] { handleFont(); });
		effectsButton = row->addChild(Button::Seed(T_("Text effects...")));
		effectsButton->onClicked([this] { handleEffects(); });

		row = cur->addChild(Grid::Seed(1, 2));
		row->setHelpId(IDH_SETTINGS_STYLES_TEXT);
		customTextColor = row->addChild(CheckBox::Seed(T_("Custom text color")));
		customTextColor->onClicked([this] { handleCustomTextColor(); });
		textColor = row->addChild(Button::Seed(T_("Select color")));
		textColor->onClicked([this] { handleTextColor(); });

		row = cur->addChild(Grid::Seed(1, 2));
		row->setHelpId(IDH_SETTINGS_STYLES_BG);
		customBgColor = row->addChild(CheckBox::Seed(T_("Custom background color")));
		customBgColor->onClicked([this] { handleCustomBgColor(); });
		bgColor = row->addChild(Button::Seed(T_("Select color")));
		bgColor->onClicked([this] { handleBgColor(); });

		row = cur->addChild(Grid::Seed(1, 2));
		auto button = row->addChild(Button::Seed(T_("Configure user matching definitions")));
		button->setHelpId(IDH_SETTINGS_STYLES_CONF_USER_MATCHING);
		button->setImage(IconManager::buttonIcon(IDI_USERS));
		// Themed drawing puts more room around the icon than the button's ideal size allows for;
		// an AUTO column takes the larger of this and the ideal size.
		row->column(0).size = static_cast<size_t>(button->getPreferredSize().x + button->scale(24));
		button->onClicked([this] {
			// The rules are edited in Settings, which shows the styles applied here.
			if(applyPending) apply();
			if(UiServices::mainWindow) UiServices::mainWindow->openSettings(typeid(UserMatchPage));
		});
		showGen = row->addChild(CheckBox::Seed(T_("Show those generated by DC++")));
		showGen->setHelpId(IDH_SETTINGS_STYLES_SHOW_GEN_MATCHERS);
		showGen->onClicked([this] { updateUserMatches(); });

		cur->addChild(Label::Seed(T_("Changes apply to the whole client as you make them.")));
	}

	{
		auto cur = addGroup(RIGHT, T_("Preview"), IDH_SETTINGS_STYLES_PREVIEW, true)->addChild(Grid::Seed(2, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->row(0).mode = GridInfo::FILL;
		cur->row(0).size = cur->scale(160);
		cur->row(0).align = GridInfo::STRETCH;

		auto seed = ControlSeeds::Dialog::richTextBox;
		seed.style |= ES_READONLY;
		preview = cur->addChild(seed);
		preview->setTextLimit(128 * 1024);
		preview->setAccessibleName(T_("Live chat appearance preview"));
		preview->setHelpId(IDH_SETTINGS_STYLES_PREVIEW);
		cur->addChild(Label::Seed(T_("Preview uses the chat renderer. Some advanced effects depend on Windows RichEdit support.")));
	}

	TableLayout::makeColumns(table, styleColumns, COLUMN_LAST);

	TStringList groups(GROUP_LAST);
	groups[GROUP_INTERFACE] = T_("Interface palette");
	groups[GROUP_GENERAL] = T_("General");
	groups[GROUP_TRANSFERS] = T_("Transfers");
	groups[GROUP_CHAT] = T_("Chat");
	groups[GROUP_USERS] = T_("Users");
	table->setGroups(groups);
	auto grouped = table->isGrouped();

	auto add = [this, grouped](tstring&& text, unsigned helpId, int group, int fontSetting, int textColorSetting, int bgColorSetting) -> Data* {
		auto data = new SettingsData(move(text), helpId, fontSetting, textColorSetting, bgColorSetting);
		this->table->insert(grouped ? group : -1, data);
		return data;
	};

	const std::pair<const char*, int> palette[] = {
		{ N_("Window background"), SettingsManager::THEME_BACKGROUND_COLOR },
		{ N_("Control surface"), SettingsManager::THEME_SURFACE_COLOR },
		{ N_("Primary text"), SettingsManager::THEME_TEXT_COLOR },
		{ N_("Disabled text"), SettingsManager::THEME_DISABLED_TEXT_COLOR },
		{ N_("Borders and separators"), SettingsManager::THEME_BORDER_COLOR },
		{ N_("Selection and accent"), SettingsManager::THEME_ACCENT_COLOR },
		{ N_("Selected text"), SettingsManager::THEME_HIGHLIGHT_TEXT_COLOR }
	};
	for(const auto& color : palette) {
		auto data = add(Text::toT(_(color.first)), IDH_STYLESPAGE, GROUP_INTERFACE, -1, -1, color.second);
		if(color.second == SettingsManager::THEME_TEXT_COLOR) interfaceTextData = data;
	}

	globalData = add(T_("Global application style"), IDH_SETTINGS_STYLES_GLOBAL, GROUP_GENERAL,
		SettingsManager::MAIN_FONT, SettingsManager::TEXT_COLOR, SettingsManager::BACKGROUND_COLOR);

	add(T_("Text viewer"), IDH_SETTINGS_STYLES_GLOBAL, GROUP_GENERAL, SettingsManager::TEXT_VIEWER_FONT, -1, -1);

	add(T_("Uploads"), IDH_SETTINGS_STYLES_UPLOADS, GROUP_TRANSFERS,
		SettingsManager::UPLOAD_FONT, SettingsManager::UPLOAD_TEXT_COLOR, SettingsManager::UPLOAD_BG_COLOR);
	add(T_("Downloads"), IDH_SETTINGS_STYLES_DOWNLOADS, GROUP_TRANSFERS,
		SettingsManager::DOWNLOAD_FONT, SettingsManager::DOWNLOAD_TEXT_COLOR, SettingsManager::DOWNLOAD_BG_COLOR);

	add(T_("Links"), IDH_SETTINGS_STYLES_LINKS, GROUP_CHAT,
		SettingsManager::LINK_FONT, SettingsManager::LINK_COLOR, SettingsManager::LINK_BG_COLOR);
	add(T_("Logs"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		SettingsManager::LOG_FONT, SettingsManager::LOG_COLOR, SettingsManager::LOG_BG_COLOR);
	add(T_("System log: verbose"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		-1, SettingsManager::SYSTEM_LOG_VERBOSE_COLOR, -1);
	add(T_("System log: information"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		-1, SettingsManager::SYSTEM_LOG_INFO_COLOR, -1);
	add(T_("System log: warning"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		-1, SettingsManager::SYSTEM_LOG_WARNING_COLOR, -1);
	add(T_("System log: error"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		-1, SettingsManager::SYSTEM_LOG_ERROR_COLOR, -1);
	add(T_("System log: area"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		-1, SettingsManager::SYSTEM_LOG_AREA_COLOR, -1);
	add(T_("Chat timestamps"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		SettingsManager::CHAT_TIMESTAMP_FONT, SettingsManager::CHAT_TIMESTAMP_COLOR, SettingsManager::CHAT_TIMESTAMP_BG_COLOR);
	add(T_("Chat nicknames"), IDH_SETTINGS_STYLES_LINKS, GROUP_CHAT,
		SettingsManager::CHAT_NICK_FONT, SettingsManager::CHAT_NICK_COLOR, SettingsManager::CHAT_NICK_BG_COLOR);
	add(T_("Chat text"), IDH_SETTINGS_STYLES_GLOBAL, GROUP_CHAT,
		SettingsManager::CHAT_TEXT_FONT, SettingsManager::CHAT_TEXT_COLOR, SettingsManager::CHAT_TEXT_BG_COLOR);
	add(T_("Own chat timestamps"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		SettingsManager::CHAT_OWN_TIMESTAMP_FONT, SettingsManager::CHAT_OWN_TIMESTAMP_COLOR, SettingsManager::CHAT_OWN_TIMESTAMP_BG_COLOR);
	add(T_("Own chat nickname"), IDH_SETTINGS_STYLES_LINKS, GROUP_CHAT,
		SettingsManager::CHAT_OWN_NICK_FONT, SettingsManager::CHAT_OWN_NICK_COLOR, SettingsManager::CHAT_OWN_NICK_BG_COLOR);
	add(T_("Own chat text"), IDH_SETTINGS_STYLES_GLOBAL, GROUP_CHAT,
		SettingsManager::CHAT_OWN_TEXT_FONT, SettingsManager::CHAT_OWN_TEXT_COLOR, SettingsManager::CHAT_OWN_TEXT_BG_COLOR);
	add(T_("Chat system messages"), IDH_SETTINGS_STYLES_LOGS, GROUP_CHAT,
		SettingsManager::CHAT_SYSTEM_FONT, SettingsManager::CHAT_SYSTEM_COLOR, SettingsManager::CHAT_SYSTEM_BG_COLOR);
	add(T_("Chat mentions"), IDH_SETTINGS_STYLES_LINKS, GROUP_CHAT, SettingsManager::CHAT_MENTION_FONT,
		SettingsManager::CHAT_MENTION_COLOR, SettingsManager::CHAT_MENTION_BG_COLOR);

	// Showing the stored theme is not an edit, so nothing is applied, and the preview is drawn
	// once, at the end.
	loading = true;
	building = true;
	matches = UserMatchManager::getInstance()->getList();
	updateUserMatches();

	themePreset->addValue(T_("Custom / current settings"));
	themePreset->setSelected(0);
	update(globalData);
	table->setSelected(table->find(globalData));
	building = false;

	handleSelectionChanged();
	loading = false;

	// Reading the theme files waits until the tab is on screen.
	callAsync([this] { reloadThemes(); });

	table->onSelectionChanged([this] { handleSelectionChanged(); });

	table->setHelpId([this](unsigned& id) { handleTableHelpId(id); });
}

ThemesTab::~ThemesTab() {
	if(observingAppearance) {
		dwt::Application::instance().getAppearance().removeChanged(appearanceCallback);
	}
}

void ThemesTab::updateDialogBorders() {
	if(dialogBorders) {
		dialogBorders->setEnabled(!dwt::Application::instance().getAppearance().isNativeDark());
	}
}

void ThemesTab::layout() {
	ExperimentalTab::layout();

	// The window is laid out before its first tab is built.
	if(table) {
		table->setColumnWidth(COLUMN_TEXT, table->getWindowSize().x - 30);
	}
}

void ThemesTab::refresh() {
	// Edits made here come first.
	if(applyPending) {
		apply();
	}

	// Settings (user matching) or the toolbar's theme picker may have changed what is shown.
	const auto current = UserMatchManager::getInstance()->getList();
	bool sameMatches = current.size() == matches.size();
	for(size_t i = 0; sameMatches && i < current.size(); ++i) {
		const auto& a = current[i];
		const auto& b = matches[i];
		sameMatches = a.name == b.name && a.style.font == b.style.font &&
			a.style.textColor == b.style.textColor && a.style.bgColor == b.style.bgColor &&
			a.isSet(UserMatch::GENERATED) == b.isSet(UserMatch::GENERATED);
	}
	const auto shown = snapshot();
	const auto stored = ThemeManager::capture();
	if(sameMatches && shown.fonts == stored.fonts && shown.colors == stored.colors &&
		themeMode->getSelected() == std::clamp(SETTING(THEME_MODE), 0, 2))
	{
		return;
	}
	reloadFromSettings();
}

void ThemesTab::scheduleApply() {
	applyPending = true;
	setTimer([this] {
		apply();
		return false;
	}, applyPause, TIMER_APPLY);
}

void ThemesTab::apply() {
	applyPending = false;
	setTimer(std::function<bool ()>(), 0, TIMER_APPLY);

	const auto previousFont = SETTING(MAIN_FONT);
	const auto previousUploadFont = SETTING(UPLOAD_FONT);
	const auto previousDownloadFont = SETTING(DOWNLOAD_FONT);

	SettingsManager::getInstance()->set(SettingsManager::THEME_MODE, themeMode->getSelected());
	table->forEach(&ThemesTab::Data::write);
	if(matchesChanged) {
		matchesChanged = false;
		auto list = matches;
		UserMatchManager::getInstance()->setList(std::move(list));
		UiFonts::updateUserMatchFonts();
	}

	if(UiServices::mainWindow) {
		UiServices::mainWindow->themeChanged(previousFont, previousUploadFont, previousDownloadFont);
	}
	// Saved with the window's other changes.
	live.notify(SettingsManager::THEME_MODE);
}

void ThemesTab::reloadFromSettings() {
	loading = true;
	matches.clear();
	updateUserMatches();
	matches = UserMatchManager::getInstance()->getList();
	for(size_t i = 0; i < table->size(); ++i) {
		if(auto data = dynamic_cast<SettingsData*>(table->getData(static_cast<int>(i)))) {
			data->read();
		}
	}
	updateUserMatches();
	themeMode->setSelected(std::clamp(SETTING(THEME_MODE), 0, 2));
	reloadThemes();
	matchesChanged = false;
	update(globalData);
	handleSelectionChanged();
	table->Control::redraw(true);
	loading = false;
}

void ThemesTab::updateUserMatches() {
	updatingMatches = true;
	// remove previous user matching rows.
	for(int i = 0, n = static_cast<int>(table->size()); i < n;) {
		if(dynamic_cast<UserMatchData*>(table->getData(i))) {
			table->erase(i);
			--n;
		} else {
			++i;
		}
	}

	// add current user matching rows.
	for(auto& i: matches) {
		if(showGen->getChecked() || !i.isSet(UserMatch::GENERATED)) {
			table->insert(table->isGrouped() ? GROUP_USERS : -1, new UserMatchData(i));
		}
	}
	updatingMatches = false;
	handleSelectionChanged();
}

ThemesTab::Data::Data(tstring&& text, const unsigned helpId) :
Flags(),
text(move(text)),
helpId(helpId),
customFont(false), font(), defaultFont(), customTextColor(false), textColor(-1), customBgColor(false), bgColor(-1)
{
}

const tstring& ThemesTab::Data::getText(int) const {
	return text;
}

int ThemesTab::Data::getStyle(HFONT&, COLORREF& foreground, COLORREF& background, int) const {
	const auto text = getTextColor();
	const auto bg = getBgColor();
	if(text >= 0) foreground = text;
	if(bg >= 0) background = bg;
	if(!isSet(TEXT_COLOR_CHANGEABLE) && isSet(BG_COLOR_CHANGEABLE) && bg >= 0) {
		// Palette entries represent a single color. Show that exact color in the
		// label, with a contrasting backdrop so even white and black stay visible.
		foreground = bg;
		const auto linear = [](BYTE value) {
			const auto channel = value / 255.0;
			return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
		};
		const auto luminance = 0.2126 * linear(GetRValue(bg)) +
			0.7152 * linear(GetGValue(bg)) + 0.0722 * linear(GetBValue(bg));
		background = (luminance + 0.05) / 0.05 >= 1.05 / (luminance + 0.05) ? RGB(0, 0, 0) : RGB(255, 255, 255);
	}
	return CDRF_NEWFONT;
}

void ThemesTab::Data::makeFont(Font& dest, const string& setting) {
	if(!setting.empty()) {
		UiFonts::decodeFont(Text::toT(setting), dest.second);
		dest.first.reset(new dwt::Font(dest.second));
	}
}

ThemesTab::SettingsData::SettingsData(tstring&& text, unsigned helpId, int fontSetting, int textColorSetting, int bgColorSetting) :
Data(move(text), helpId),
fontSetting(fontSetting),
textColorSetting(textColorSetting),
bgColorSetting(bgColorSetting)
{
	if(fontSetting != -1) setFlag(FONT_CHANGEABLE);
	if(textColorSetting != -1) setFlag(TEXT_COLOR_CHANGEABLE);
	if(bgColorSetting != -1) setFlag(BG_COLOR_CHANGEABLE);
	read();
}

void ThemesTab::SettingsData::read() {
	auto settings = SettingsManager::getInstance();
	font = Font();
	defaultFont = Font();
	if(fontSetting != -1) {
		const auto setting = static_cast<SettingsManager::StrSetting>(fontSetting);
		customFont = !settings->isDefault(setting);
		makeFont(font, settings->get(setting));
		effects = FontStyle::decode(settings->get(setting));
		makeFont(defaultFont, settings->getDefault(setting));
	} else {
		customFont = false;
	}

	if(textColorSetting != -1) {
		const auto setting = static_cast<SettingsManager::IntSetting>(textColorSetting);
		customTextColor = !settings->isDefault(setting);
		textColor = settings->get(setting);
	} else {
		customTextColor = false;
	}

	if(bgColorSetting != -1) {
		const auto setting = static_cast<SettingsManager::IntSetting>(bgColorSetting);
		customBgColor = !settings->isDefault(setting);
		bgColor = settings->get(setting);
	} else {
		customBgColor = false;
	}
}

const ThemesTab::Data::Font& ThemesTab::SettingsData::getFont() const {
	return customFont ? font : defaultFont;
}

int ThemesTab::SettingsData::getTextColor() const {
	return customTextColor ? textColor :
		(textColorSetting != -1) ? SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::IntSetting>(textColorSetting)) :
		-1;
}

int ThemesTab::SettingsData::getBgColor() const {
	return customBgColor ? bgColor :
		(bgColorSetting != -1) ? SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::IntSetting>(bgColorSetting)) :
		-1;
}

void ThemesTab::SettingsData::write() {
	if(fontSetting != -1) {
		if(customFont) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::StrSetting>(fontSetting), encodedFont());
		} else {
			SettingsManager::getInstance()->unset(static_cast<SettingsManager::StrSetting>(fontSetting));
		}
	}

	if(textColorSetting != -1) {
		if(customTextColor) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::IntSetting>(textColorSetting), textColor);
		} else {
			SettingsManager::getInstance()->unset(static_cast<SettingsManager::IntSetting>(textColorSetting));
		}
	}

	if(bgColorSetting != -1) {
		if(customBgColor) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::IntSetting>(bgColorSetting), bgColor);
		} else {
			SettingsManager::getInstance()->unset(static_cast<SettingsManager::IntSetting>(bgColorSetting));
		}
	}
}

ThemesTab::UserMatchData::UserMatchData(UserMatch& matcher) :
Data(Text::toT(matcher.name), IDH_SETTINGS_STYLES_USER_MATCH),
matcher(matcher)
{
	setFlag(FONT_CHANGEABLE);
	customFont = !matcher.style.font.empty();
	makeFont(font, matcher.style.font);
	effects = FontStyle::decode(matcher.style.font);

	setFlag(TEXT_COLOR_CHANGEABLE);
	customTextColor = matcher.style.textColor >= 0;
	textColor = matcher.style.textColor;

	setFlag(BG_COLOR_CHANGEABLE);
	customBgColor = matcher.style.bgColor >= 0;
	bgColor = matcher.style.bgColor;
}

const ThemesTab::Data::Font& ThemesTab::UserMatchData::getFont() const {
	return customFont ? font : defaultFont;
}

int ThemesTab::UserMatchData::getTextColor() const {
	return customTextColor ? textColor : -1;
}

int ThemesTab::UserMatchData::getBgColor() const {
	return customBgColor ? bgColor : -1;
}

void ThemesTab::UserMatchData::update() {
	matcher.style.font = customFont ? encodedFont() : Util::emptyString;
	matcher.style.textColor = customTextColor ? textColor : -1;
	matcher.style.bgColor = customBgColor ? bgColor : -1;
	matcher.unsetFlag(UserMatch::GENERATED);
}

void ThemesTab::handleSelectionChanged() {
	if(updatingMatches) return;
	auto data = table->getSelectedData();
	updatePreview(data);
	preview->getParent()->layout();

	bool enable = data && data->customFont;
	customFont->setChecked(enable);
	customFont->setEnabled(data && data->isSet(Data::FONT_CHANGEABLE));
	font->setEnabled(enable);
	effectsButton->setEnabled(enable);

	enable = data && data->customTextColor;
	customTextColor->setChecked(enable);
	customTextColor->setEnabled(data && data->isSet(Data::TEXT_COLOR_CHANGEABLE));
	textColor->setEnabled(enable);

	enable = data && data->customBgColor;
	customBgColor->setChecked(enable);
	customBgColor->setEnabled(data && data->isSet(Data::BG_COLOR_CHANGEABLE));
	bgColor->setEnabled(enable);
}

void ThemesTab::handleTableHelpId(unsigned& id) {
	// same as PropPage::handleListHelpId
	int item = isAnyKeyPressed() ? table->getSelected() :
		table->hitTest(dwt::ScreenCoordinate(dwt::Point::fromLParam(::GetMessagePos()))).first;
	if(item >= 0)
		id = table->getData(item)->helpId;
}

void ThemesTab::handleCustomFont() {
	auto data = table->getSelectedData();
	data->customFont = customFont->getChecked();
	if(data->customFont) {
		initFont(data);
	}
	update(data);
	handleSelectionChanged();
}

void ThemesTab::handleFont() {
	auto data = table->getSelectedData();
	initFont(data);
	FontDialog::Options options;
	options.strikeout = true;
	options.underline = true;
	options.color = false;
	options.bgColor = getBgColor(data);
	auto color = getTextColor(data);
	if(FontDialog(this).open(data->font.second, color, &options)) {
		if(!data->font.second.lfUnderline) data->effects.underline = 0;
		else if(!data->effects.underline) data->effects.underline = 1;
		if(!data->font.second.lfStrikeOut) data->effects.strike = 0;
		else if(!data->effects.strike) data->effects.strike = 1;
		data->font.first.reset(new dwt::Font(data->font.second));
		update(data);
	}
}

void ThemesTab::handleCustomTextColor() {
	auto data = table->getSelectedData();
	data->customTextColor = customTextColor->getChecked();
	if(data->customTextColor) {
		data->textColor = getTextColor(data);
	}
	updateGlobalTextColor(data);
	update(data);
	handleSelectionChanged();
}

void ThemesTab::handleTextColor() {
	auto data = table->getSelectedData();
	auto color = colorDialog(getTextColor(data));
	if(color >= 0) {
		data->textColor = color;
		updateGlobalTextColor(data);
	}
	update(data);
}

void ThemesTab::updateGlobalTextColor(Data* data) {
	if(data != globalData || !interfaceTextData) return;
	// A global text edit includes chrome; the palette row remains available as
	// an explicit override, and loading a preset preserves its authored colors.
	interfaceTextData->customBgColor = true;
	interfaceTextData->bgColor = globalData->getTextColor();
	table->update(interfaceTextData);
}

void ThemesTab::handleCustomBgColor() {
	auto data = table->getSelectedData();
	data->customBgColor = customBgColor->getChecked();
	if(data->customBgColor) {
		data->bgColor = getBgColor(data);
	}
	update(data);
	handleSelectionChanged();
}

void ThemesTab::handleBgColor() {
	auto data = table->getSelectedData();
	auto color = colorDialog(getBgColor(data));
	if(color >= 0) {
		data->bgColor = color;
	}
	update(data);
}

int ThemesTab::colorDialog(COLORREF color) {
	ColorDialog::ColorParams colorParams(color);
	if(ColorDialog(this).open(colorParams)) {
		return colorParams.getColor();
	}
	return -1;
}

void ThemesTab::initFont(Data* const data) const {
	if(!data->font.first) {
		// initialize the LOGFONT structure.
		data->font.second = data->defaultFont.first ? data->defaultFont.second : globalData->getFont().second;
		data->font.first.reset(new dwt::Font(data->font.second));
	}
}

COLORREF ThemesTab::getTextColor(const Data* const data) const {
	auto color = data->getTextColor();
	return (color >= 0) ? color : globalData->getTextColor();
}

COLORREF ThemesTab::getBgColor(const Data* const data) const {
	auto color = data->getBgColor();
	return (color >= 0) ? color : globalData->getBgColor();
}

void ThemesTab::update(Data* const data) {
	data->update();
	if(!loading) themePreset->setSelected(static_cast<int>(presets.size()));

	if(data == globalData) {
		table->setFont(UiFonts::font);
		table->setColor(globalData->getTextColor(), globalData->getBgColor());
		table->Control::redraw(true);
	} else {
		table->update(data);
	}

	updatePreview(data);
	preview->getParent()->layout();

	if(dynamic_cast<UserMatchData* const>(data)) {
		matchesChanged = true;
	}
	if(!loading) {
		scheduleApply();
	}
}

string ThemesTab::Data::encodedFont() const {
	return effects.encode(Text::fromT(UiFonts::encodeFont(font.second)));
}

void ThemesTab::SettingsData::capture(ThemeManager::Theme& theme) const {
	if(fontSetting >= 0) theme.fonts[fontSetting] = customFont ? encodedFont() :
		SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::StrSetting>(fontSetting));
	if(textColorSetting >= 0) theme.colors[textColorSetting] = getTextColor();
	if(bgColorSetting >= 0) theme.colors[bgColorSetting] = getBgColor();
}

void ThemesTab::SettingsData::load(const ThemeManager::Theme& theme) {
	if(fontSetting >= 0) {
		const auto& value = theme.fonts.at(fontSetting);
		customFont = !value.empty();
		font.first.reset();
		makeFont(font, value);
		effects = FontStyle::decode(value);
	}
	if(textColorSetting >= 0) { customTextColor = true; textColor = theme.colors.at(textColorSetting); }
	if(bgColorSetting >= 0) { customBgColor = true; bgColor = theme.colors.at(bgColorSetting); }
}

void ThemesTab::UserMatchData::load(const ThemeManager::Theme&) {
	// applyUserStyles already updated every definition, including hidden rows.
	customFont = !matcher.style.font.empty();
	font.first.reset();
	makeFont(font, matcher.style.font);
	effects = FontStyle::decode(matcher.style.font);
	textColor = matcher.style.textColor;
	bgColor = matcher.style.bgColor;
	customTextColor = textColor >= 0;
	customBgColor = bgColor >= 0;
}

ThemeManager::Theme ThemesTab::snapshot() const {
	auto result = ThemeManager::capture();
	result.captureUserStyles(matches);
	for(size_t i = 0; i < table->size(); ++i) table->getData(static_cast<int>(i))->capture(result);
	result.palette = { static_cast<COLORREF>(result.colors.at(SettingsManager::THEME_BACKGROUND_COLOR)),
		static_cast<COLORREF>(result.colors.at(SettingsManager::THEME_SURFACE_COLOR)),
		static_cast<COLORREF>(result.colors.at(SettingsManager::THEME_TEXT_COLOR)),
		static_cast<COLORREF>(result.colors.at(SettingsManager::THEME_DISABLED_TEXT_COLOR)),
		static_cast<COLORREF>(result.colors.at(SettingsManager::THEME_BORDER_COLOR)),
		static_cast<COLORREF>(result.colors.at(SettingsManager::THEME_ACCENT_COLOR)),
		static_cast<COLORREF>(result.colors.at(SettingsManager::THEME_HIGHLIGHT_TEXT_COLOR)) };
	return result;
}

void ThemesTab::reloadThemes() {
	const auto wasLoading = loading;
	loading = true;
	presets = ThemeManager::getThemes();
	themePreset->clear();
	int selected = static_cast<int>(presets.size());
	for(size_t i = 0; i < presets.size(); ++i) {
		themePreset->addValue(presets[i].name);
		if(selected == static_cast<int>(presets.size()) && ThemeManager::isCurrent(presets[i])) {
			selected = static_cast<int>(i);
		}
	}
	themePreset->addValue(T_("Custom / current settings"));
	themePreset->setSelected(selected);
	loading = wasLoading;
}

void ThemesTab::applyTheme(const ThemeManager::Theme& theme) {
	loading = true;
	theme.applyUserStyles(matches);
	matchesChanged = true;
	for(size_t i = 0; i < table->size(); ++i) table->getData(static_cast<int>(i))->load(theme);
	// A named theme uses the dark palette renderer, as the toolbar's theme picker does.
	themeMode->setSelected(SettingsManager::THEME_DARK);
	update(globalData);
	handleSelectionChanged();
	table->Control::redraw(true);
	loading = false;
	scheduleApply();
}

void ThemesTab::importTheme() {
	tstring path;
	if(!dwt::LoadDialog(this).addFilter(T_("DC++ themes"), _T("*.xml")).setInitialDirectory(Text::toT(ThemeManager::getDirectory())).open(path)) return;
	try {
		// Applied like any other edit; installing the file is an explicit Save.
		const auto theme = ThemeManager::load(Text::fromT(path));
		applyTheme(theme);
		themePreset->setSelected(static_cast<int>(presets.size()));
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(Text::toT(e.getError()), T_("Unable to import theme"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
}

void ThemesTab::saveTheme() {
	tstring path = _T("Custom theme.xml");
	File::ensureDirectory(ThemeManager::getDirectory());
	if(!dwt::SaveDialog(this).addFilter(T_("DC++ themes"), _T("*.xml")).setDefaultExtension(_T("xml"))
		.setInitialDirectory(Text::toT(ThemeManager::getDirectory())).open(path)) return;
	try {
		ThemeManager::saveTheme(Text::fromT(path), snapshot());
		reloadThemes();
		if(UiServices::mainWindow) UiServices::mainWindow->reloadThemes();
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(Text::toT(e.getError()), T_("Unable to save theme"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
}

void ThemesTab::handleEffects() {
	auto data = table->getSelectedData();
	if(!data || !data->customFont) return;
	initFont(data);
	TextEffectsDlg dialog(this, data->text, data->font.second, data->effects, getTextColor(data), getBgColor(data));
	if(dialog.run() != IDOK) return;
	data->effects = dialog.getEffects();
	data->font.second = dialog.getFontValue();
	data->font.first.reset(new dwt::Font(data->font.second));
	update(data);
}

void ThemesTab::updatePreview(Data* const data) {
	if(!globalData || building) return;
	const auto theme = snapshot();
	preview->setText(L"");
	preview->clearMessageMetadata();
	preview->setFont(globalData->getFont().first);
	preview->setColor(globalData->getTextColor(), globalData->getBgColor());
	std::vector<string> samples {
		"<span><span id=\"timestamp\">[12:34]</span> <span id=\"nick\">&lt;Alice&gt;</span> <span id=\"text\">Welcome to the hub! This is regular chat text.</span></span>",
		"<span><span id=\"ownTimestamp\">[12:35]</span> <span id=\"ownNick\">&lt;You&gt;</span> <span id=\"ownText\">This is your own message.</span></span>",
		"<span id=\"text\"><span id=\"mention\">You</span>, see <a href=\"https://example.org/\">the project website</a>.</span>",
		"<span id=\"systemMessage\">Connected. System announcements appear like this.</span>",
		"<span id=\"log\">Previous chat history restored from the log.</span>",
		"<span><span id=\"systemLogInfo\">Information</span> / <span id=\"systemLogWarning\">Warning</span> / <span id=\"systemLogError\">Error</span></span>"
	};
	if(data) {
		const string fontText = data->customFont ? data->encodedFont() : theme.fonts.at(SettingsManager::MAIN_FONT);
		string tmp;
		string css = "font:" + Util::cssFont(fontText) + ";dc-font:" + fontText + ";color:#" + Util::cssColor(getTextColor(data)) +
			";background-color:#" + Util::cssColor(getBgColor(data)) + ";";
		const auto escapedCss = SimpleXML::escape(css, tmp, true);
		const auto label = SimpleXML::escape(Text::fromT(data->text), tmp, false);
		samples.insert(samples.begin(), "<span id=\"text\" style=\"" + escapedCss + "\">" + label + ": The quick brown fox 0123456789</span>");
	}
	std::vector<RichTextBox::RenderedMessage> batch;
	for(const auto& html : samples) batch.push_back({ HtmlToRtf::convert(html, preview, string(), &theme) });
	preview->appendMessages(std::move(batch));
	preview->setSelection(0, 0);
	preview->sendMessage(WM_VSCROLL, SB_TOP);
	preview->redraw(true);
}
