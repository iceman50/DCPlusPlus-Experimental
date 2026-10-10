/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
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
#include "ThemePage.h"

#include <dcpp/SettingsManager.h>
#include <cmath>

#include <dwt/widgets/Button.h>
#include <dwt/widgets/ColorDialog.h>
#include <dwt/widgets/FontDialog.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>

#include "resource.h"
#include "SettingsDialog.h"
#include "TypedTable.h"
#include "UserMatchPage.h"
#include "WinUtil.h"
#include "HtmlToRtf.h"
#include "RichTextBox.h"
#include "TextEffectsDlg.h"
#include <dcpp/File.h>
#include <dwt/widgets/LoadDialog.h>
#include <dwt/widgets/SaveDialog.h>
#include <dwt/widgets/MessageBox.h>

using dwt::Button;
using dwt::ColorDialog;
using dwt::FontDialog;
using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;

static const ColumnInfo columns[] = {
	{ "", 100, false }
};

ThemePage::ThemePage(dwt::Widget* parent) :
PropPage(parent, 2, 1),
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
	setHelpId(IDH_STYLESPAGE);

	grid->column(0).mode = GridInfo::FILL;
	grid->row(1).mode = GridInfo::FILL;
	grid->row(1).align = GridInfo::STRETCH;

	auto header = grid->addChild(Grid::Seed(2, 1));
	header->column(0).mode = GridInfo::FILL;
	auto choices = header->addChild(Grid::Seed(1, 4));
	choices->column(3).mode = GridInfo::FILL;
	choices->addChild(Label::Seed(T_("Mode")));
	themeMode = choices->addChild(WinUtil::Seeds::Dialog::comboBox);
	for(const auto& name : { T_("Follow Windows"), T_("Light"), T_("Dark") }) themeMode->addValue(name);
	themeMode->setSelected(std::clamp(SETTING(THEME_MODE), 0, 2));
	choices->addChild(Label::Seed(T_("Theme")));
	themePreset = choices->addChild(WinUtil::Seeds::Dialog::comboBox);
	themePreset->onSelectionChanged([this] {
		const auto selected = themePreset->getSelected();
		if(!loading && selected >= 0 && static_cast<size_t>(selected) < presets.size()) applyTheme(presets[selected]);
	});
	auto actions = header->addChild(Grid::Seed(1, 4));
	actions->addChild(Button::Seed(T_("Save theme...")))->onClicked([this] { saveTheme(); });
	actions->addChild(Button::Seed(T_("Import theme...")))->onClicked([this] { importTheme(); });
	actions->addChild(Button::Seed(T_("Open folder")))->onClicked([] { WinUtil::openFile(Text::toT(ThemeManager::getDirectory())); });
	actions->addChild(Button::Seed(T_("Reload themes")))->onClicked([this] { reloadThemes(); });

	{
		auto cur = grid->addChild(GroupBox::Seed(T_("Theme appearance")))->addChild(Grid::Seed(6, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->row(0).mode = GridInfo::FILL;
		cur->row(0).align = GridInfo::STRETCH;
		cur->setSpacing(grid->getSpacing());

		{
			auto seed = WinUtil::Seeds::Dialog::table;
			seed.style &= ~LVS_SHOWSELALWAYS;
			seed.style |= LVS_SINGLESEL | LVS_NOCOLUMNHEADER;
			table = cur->addChild(Table::Seed(seed));

			dwt::ImageListPtr images(new dwt::ImageList(dwt::Point(16, 16)));
			images->add(*WinUtil::createIcon(IDI_DCPP, 16));
			images->add(*WinUtil::createIcon(IDI_DCPP, 16));
			images->add(*WinUtil::createIcon(IDI_NET_STATS, 16)); /// @todo better icon for the "transfers" group?
			images->add(*WinUtil::createIcon(IDI_CHAT, 16));
			images->add(*WinUtil::createIcon(IDI_USERS, 16));
			table->setGroupImageList(images);
		}

		{
			auto seed = WinUtil::Seeds::Dialog::richTextBox;
			seed.style |= ES_READONLY;
			preview = cur->addChild(seed);
			preview->setTextLimit(128 * 1024);
			preview->setAccessibleName(T_("Live chat appearance preview"));
			preview->setHelpId(IDH_SETTINGS_STYLES_PREVIEW);
			cur->row(0).size = cur->scale(160);
			cur->row(1).mode = GridInfo::FILL;
			cur->row(1).size = cur->scale(160);
			cur->row(1).align = GridInfo::STRETCH;
		}

		auto row = cur->addChild(Grid::Seed(1, 2));
		row->column(0).mode = GridInfo::FILL;
		row->setSpacing(cur->getSpacing());

		auto cur2 = row->addChild(Grid::Seed(1, 3));
		cur2->setHelpId(IDH_SETTINGS_STYLES_FONT);

		customFont = cur2->addChild(CheckBox::Seed(T_("Custom font")));
		customFont->onClicked([this] { handleCustomFont(); });

		font = cur2->addChild(Button::Seed(T_("Select font")));
		font->onClicked([this] { handleFont(); });
		effectsButton = cur2->addChild(Button::Seed(T_("Text effects...")));
		effectsButton->onClicked([this] { handleEffects(); });

		cur2 = row->addChild(Grid::Seed(1, 2));
		cur2->setHelpId(IDH_SETTINGS_STYLES_TEXT);

		customTextColor = cur2->addChild(CheckBox::Seed(T_("Custom text color")));
		customTextColor->onClicked([this] { handleCustomTextColor(); });

		textColor = cur2->addChild(Button::Seed(T_("Select color")));
		textColor->onClicked([this] { handleTextColor(); });

		row = cur->addChild(Grid::Seed(1, 2));
		row->column(0).mode = GridInfo::FILL;
		row->column(0).align = GridInfo::BOTTOM_RIGHT;
		row->setSpacing(cur->getSpacing());

		row->addChild(Label::Seed());
		cur2 = row->addChild(Grid::Seed(1, 2));
		cur2->setHelpId(IDH_SETTINGS_STYLES_BG);

		customBgColor = cur2->addChild(CheckBox::Seed(T_("Custom background color")));
		customBgColor->onClicked([this] { handleCustomBgColor(); });

		bgColor = cur2->addChild(Button::Seed(T_("Select color")));
		bgColor->onClicked([this] { handleBgColor(); });

		row = cur->addChild(Grid::Seed(1, 2));
		row->setSpacing(cur->getSpacing());

		auto button = row->addChild(Button::Seed(T_("Configure user matching definitions")));
		button->setHelpId(IDH_SETTINGS_STYLES_CONF_USER_MATCHING);
		button->setImage(WinUtil::buttonIcon(IDI_USERS));
		button->onClicked([this] { static_cast<SettingsDialog*>(getRoot())->activatePage<UserMatchPage>(); });

		showGen = row->addChild(CheckBox::Seed(T_("Show those generated by DC++")));
		showGen->setHelpId(IDH_SETTINGS_STYLES_SHOW_GEN_MATCHERS);
		showGen->onClicked([this] { static_cast<SettingsDialog*>(getRoot())->getPage<UserMatchPage>()->updateStyles(); });
		cur->addChild(Label::Seed(T_("Preview uses the chat renderer. Some advanced effects depend on Windows RichEdit support.")));
	}

	WinUtil::makeColumns(table, columns, COLUMN_LAST);

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
	for(const auto& color : palette) add(Text::toT(_(color.first)), IDH_STYLESPAGE, GROUP_INTERFACE, -1, -1, color.second);

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

	reloadThemes();
	update(globalData);
	table->setSelected(table->find(globalData));

	handleSelectionChanged();

	table->onSelectionChanged([this] { handleSelectionChanged(); });

	table->setHelpId([this](unsigned& id) { handleTableHelpId(id); });

	// User matching rows are owned by UserMatchPage, which may not exist yet with lazy settings pages.
	callAsync([this] { static_cast<SettingsDialog*>(getRoot())->getPage<UserMatchPage>()->updateStyles(); });
}

ThemePage::~ThemePage() {
}

void ThemePage::layout() {
	PropPage::layout();

	table->setColumnWidth(COLUMN_TEXT, table->getWindowSize().x - 30);
}

void ThemePage::write() {
	SettingsManager::getInstance()->set(SettingsManager::THEME_MODE, themeMode->getSelected());
	table->forEach(&ThemePage::Data::write);
}

void ThemePage::updateUserMatches(std::vector<UserMatch>& userMatches) {
	updatingMatches = true;
	matches = &userMatches;
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
	for(auto& i: userMatches) {
		if(showGen->getChecked() || !i.isSet(UserMatch::GENERATED)) {
			table->insert(table->isGrouped() ? GROUP_USERS : -1, new UserMatchData(i));
		}
	}
	updatingMatches = false;
	handleSelectionChanged();
}

ThemePage::Data::Data(tstring&& text, const unsigned helpId) :
Flags(),
text(move(text)),
helpId(helpId),
customFont(false), font(), defaultFont(), customTextColor(false), textColor(-1), customBgColor(false), bgColor(-1)
{
}

const tstring& ThemePage::Data::getText(int) const {
	return text;
}

int ThemePage::Data::getStyle(HFONT&, COLORREF& foreground, COLORREF& background, int) const {
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

void ThemePage::Data::makeFont(Font& dest, const string& setting) {
	if(!setting.empty()) {
		WinUtil::decodeFont(Text::toT(setting), dest.second);
		dest.first.reset(new dwt::Font(dest.second));
	}
}

ThemePage::SettingsData::SettingsData(tstring&& text, unsigned helpId, int fontSetting, int textColorSetting, int bgColorSetting) :
Data(move(text), helpId),
fontSetting(fontSetting),
textColorSetting(textColorSetting),
bgColorSetting(bgColorSetting)
{
	if(fontSetting != -1) {
		setFlag(FONT_CHANGEABLE);
		customFont = !SettingsManager::getInstance()->isDefault(static_cast<SettingsManager::StrSetting>(fontSetting));
		makeFont(font, SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(fontSetting)));
		effects = FontStyle::decode(SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(fontSetting)));
		makeFont(defaultFont, SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::StrSetting>(fontSetting)));
	} else {
		customFont = false;
	}

	if(textColorSetting != -1) {
		setFlag(TEXT_COLOR_CHANGEABLE);
		customTextColor = !SettingsManager::getInstance()->isDefault(static_cast<SettingsManager::IntSetting>(textColorSetting));
		textColor = SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(textColorSetting));
	} else {
		customTextColor = false;
	}

	if(bgColorSetting != -1) {
		setFlag(BG_COLOR_CHANGEABLE);
		customBgColor = !SettingsManager::getInstance()->isDefault(static_cast<SettingsManager::IntSetting>(bgColorSetting));
		bgColor = SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(bgColorSetting));
	} else {
		customBgColor = false;
	}
}

const ThemePage::Data::Font& ThemePage::SettingsData::getFont() const {
	return customFont ? font : defaultFont;
}

int ThemePage::SettingsData::getTextColor() const {
	return customTextColor ? textColor :
		(textColorSetting != -1) ? SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::IntSetting>(textColorSetting)) :
		-1;
}

int ThemePage::SettingsData::getBgColor() const {
	return customBgColor ? bgColor :
		(bgColorSetting != -1) ? SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::IntSetting>(bgColorSetting)) :
		-1;
}

void ThemePage::SettingsData::write() {
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

ThemePage::UserMatchData::UserMatchData(UserMatch& matcher) :
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

const ThemePage::Data::Font& ThemePage::UserMatchData::getFont() const {
	return customFont ? font : defaultFont;
}

int ThemePage::UserMatchData::getTextColor() const {
	return customTextColor ? textColor : -1;
}

int ThemePage::UserMatchData::getBgColor() const {
	return customBgColor ? bgColor : -1;
}

void ThemePage::UserMatchData::update() {
	matcher.style.font = customFont ? encodedFont() : Util::emptyString;
	matcher.style.textColor = customTextColor ? textColor : -1;
	matcher.style.bgColor = customBgColor ? bgColor : -1;
	matcher.unsetFlag(UserMatch::GENERATED);
}

void ThemePage::handleSelectionChanged() {
	if(updatingMatches) return;
	auto data = table->getSelectedData();
	bool enable = data;

	if(data) {
		updatePreview(data);
	}
	if(!data) updatePreview(nullptr);
	preview->getParent()->layout();

	enable = data && data->customFont;
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

void ThemePage::handleTableHelpId(unsigned& id) {
	// same as PropPage::handleListHelpId
	int item = isAnyKeyPressed() ? table->getSelected() :
		table->hitTest(dwt::ScreenCoordinate(dwt::Point::fromLParam(::GetMessagePos()))).first;
	if(item >= 0)
		id = table->getData(item)->helpId;
}

void ThemePage::handleCustomFont() {
	auto data = table->getSelectedData();
	data->customFont = customFont->getChecked();
	if(data->customFont) {
		initFont(data);
	}
	update(data);
	handleSelectionChanged();
}

void ThemePage::handleFont() {
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

void ThemePage::handleCustomTextColor() {
	auto data = table->getSelectedData();
	data->customTextColor = customTextColor->getChecked();
	if(data->customTextColor) {
		data->textColor = getTextColor(data);
	}
	update(data);
	handleSelectionChanged();
}

void ThemePage::handleTextColor() {
	auto data = table->getSelectedData();
	auto color = colorDialog(getTextColor(data));
	if(color >= 0) {
		data->textColor = color;
	}
	update(data);
}

void ThemePage::handleCustomBgColor() {
	auto data = table->getSelectedData();
	data->customBgColor = customBgColor->getChecked();
	if(data->customBgColor) {
		data->bgColor = getBgColor(data);
	}
	update(data);
	handleSelectionChanged();
}

void ThemePage::handleBgColor() {
	auto data = table->getSelectedData();
	auto color = colorDialog(getBgColor(data));
	if(color >= 0) {
		data->bgColor = color;
	}
	update(data);
}

int ThemePage::colorDialog(COLORREF color) {
	ColorDialog::ColorParams colorParams(color);
	if(ColorDialog(this).open(colorParams)) {
		return colorParams.getColor();
	}
	return -1;
}

void ThemePage::initFont(Data* const data) const {
	if(!data->font.first) {
		// initialize the LOGFONT structure.
		data->font.second = data->defaultFont.first ? data->defaultFont.second : globalData->getFont().second;
		data->font.first.reset(new dwt::Font(data->font.second));
	}
}

COLORREF ThemePage::getTextColor(const Data* const data) const {
	auto color = data->getTextColor();
	return (color >= 0) ? color : globalData->getTextColor();
}

COLORREF ThemePage::getBgColor(const Data* const data) const {
	auto color = data->getBgColor();
	return (color >= 0) ? color : globalData->getBgColor();
}

void ThemePage::update(Data* const data) {
	data->update();
	if(!loading) themePreset->setSelected(static_cast<int>(presets.size()));

	if(data == globalData) {
		table->setFont(WinUtil::font);
		table->setColor(globalData->getTextColor(), globalData->getBgColor());
		table->Control::redraw(true);
	} else {
		table->update(data);
	}

	updatePreview(data);
	preview->getParent()->layout();

	if(dynamic_cast<UserMatchData* const>(data)) {
		static_cast<SettingsDialog*>(getRoot())->getPage<UserMatchPage>()->setDirty();
	}
}

string ThemePage::Data::encodedFont() const {
	return effects.encode(Text::fromT(WinUtil::encodeFont(font.second)));
}

void ThemePage::SettingsData::capture(ThemeManager::Theme& theme) const {
	if(fontSetting >= 0) theme.fonts[fontSetting] = customFont ? encodedFont() :
		SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::StrSetting>(fontSetting));
	if(textColorSetting >= 0) theme.colors[textColorSetting] = getTextColor();
	if(bgColorSetting >= 0) theme.colors[bgColorSetting] = getBgColor();
}

void ThemePage::SettingsData::load(const ThemeManager::Theme& theme) {
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

void ThemePage::UserMatchData::capture(ThemeManager::Theme& theme) const {
	theme.userStyles[matcher.name] = matcher.style;
}

void ThemePage::UserMatchData::load(const ThemeManager::Theme& theme) {
	const auto found = theme.userStyles.find(matcher.name);
	matcher.style = found == theme.userStyles.end() ? Style() : found->second;
	customFont = !matcher.style.font.empty();
	font.first.reset();
	makeFont(font, matcher.style.font);
	effects = FontStyle::decode(matcher.style.font);
	textColor = matcher.style.textColor;
	bgColor = matcher.style.bgColor;
	customTextColor = textColor >= 0;
	customBgColor = bgColor >= 0;
}

ThemeManager::Theme ThemePage::snapshot() const {
	auto result = ThemeManager::capture();
	if(matches) {
		result.userStyles.clear();
		for(const auto& match : *matches) result.userStyles[match.name] = match.style;
	}
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

void ThemePage::reloadThemes() {
	loading = true;
	presets = ThemeManager::getThemes();
	themePreset->clear();
	for(const auto& theme : presets) themePreset->addValue(theme.name);
	themePreset->addValue(T_("Custom / current settings"));
	themePreset->setSelected(static_cast<int>(presets.size()));
	loading = false;
}

void ThemePage::applyTheme(const ThemeManager::Theme& theme) {
	auto definitions = static_cast<SettingsDialog*>(getRoot())->getPage<UserMatchPage>();
	definitions->updateStyles();
	loading = true;
	if(matches) for(auto& match : *matches) {
		const auto found = theme.userStyles.find(match.name);
		match.style = found == theme.userStyles.end() ? Style() : found->second;
	}
	for(size_t i = 0; i < table->size(); ++i) table->getData(static_cast<int>(i))->load(theme);
	definitions->setDirty();
	update(globalData);
	handleSelectionChanged();
	table->Control::redraw(true);
	loading = false;
}

void ThemePage::importTheme() {
	tstring path;
	if(!dwt::LoadDialog(this).addFilter(T_("DC++ themes"), _T("*.xml")).setInitialDirectory(Text::toT(ThemeManager::getDirectory())).open(path)) return;
	try {
		// Import into the draft. Installing or overwriting a file is an explicit Save action.
		const auto theme = ThemeManager::load(Text::fromT(path));
		applyTheme(theme);
		themePreset->setSelected(static_cast<int>(presets.size()));
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(Text::toT(e.getError()), T_("Unable to import theme"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
}

void ThemePage::saveTheme() {
	tstring path = _T("Custom theme.xml");
	File::ensureDirectory(ThemeManager::getDirectory());
	if(!dwt::SaveDialog(this).addFilter(T_("DC++ themes"), _T("*.xml")).setDefaultExtension(_T("xml"))
		.setInitialDirectory(Text::toT(ThemeManager::getDirectory())).open(path)) return;
	try {
		ThemeManager::saveTheme(Text::fromT(path), snapshot());
		reloadThemes();
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(Text::toT(e.getError()), T_("Unable to save theme"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
}

void ThemePage::handleEffects() {
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

void ThemePage::updatePreview(Data* const data) {
	if(!globalData) return;
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

