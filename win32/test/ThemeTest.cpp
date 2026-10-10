// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"
#include "ThemeManager.h"
#include "ThemePage.h"
#include "TextEffectsDlg.h"
#include <dwt/widgets/CheckBox.h>
#include "SettingsDialog.h"
#include "SingleInstance.h"
#include "UserMatchPage.h"
#include "WindowsPage.h"
#include "HtmlToRtf.h"
#include "WinUtil.h"
#include "RichTextBox.h"
#include "resource.h"
#include <dcpp/FontStyle.h>
#include <dcpp/File.h>
#include <dcpp/ResourceManager.h>
#include <dcpp/UserMatchManager.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/ComboBox.h>
#include <cstdio>
#include <chrono>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 177;

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}
CHARFORMAT2 formatAt(dwt::RichTextBox* box, int position) {
	box->setSelection(position, position + 1);
	CHARFORMAT2 result { sizeof(CHARFORMAT2) };
	box->sendMessage(EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&result));
	return result;
}
void render(dwt::RichTextBox* box, const ThemeManager::Theme& theme, const string& html) {
	box->setText(L"");
	box->addTextSteady(L"{\\urtf1" + HtmlToRtf::convert(html, box, string(), &theme) + L"}");
}
void snapshot(HWND window, const string& path) {
	RECT rect;
	GetWindowRect(window, &rect);
	const auto width = rect.right - rect.left, height = rect.bottom - rect.top;
	auto dc = GetDC(window);
	auto memory = CreateCompatibleDC(dc);
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	void* pixels = nullptr;
	auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
	check(bitmap != nullptr, "Cannot capture settings layout");
	auto previous = SelectObject(memory, bitmap);
	SendMessage(window, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
	BITMAPFILEHEADER header {};
	header.bfType = 0x4D42;
	header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
	header.bfSize = header.bfOffBits + width * height * 4;
	File output(path, File::WRITE, File::CREATE | File::TRUNCATE);
	output.write(&header, sizeof(header));
	output.write(&info.bmiHeader, sizeof(info.bmiHeader));
	output.write(pixels, width * height * 4);
	SelectObject(memory, previous);
	DeleteObject(bitmap);
	DeleteDC(memory);
	ReleaseDC(window, dc);
}
BOOL CALLBACK collectChildren(HWND window, LPARAM data) {
	auto children = reinterpret_cast<std::vector<HWND>*>(data);
	children->push_back(window);
	return TRUE;
}
LRESULT CALLBACK countLayout(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	if(message == WM_WINDOWPOSCHANGING) ++*reinterpret_cast<unsigned*>(data);
	return DefSubclassProc(window, message, wParam, lParam);
}
struct SettingsPaintProbe { unsigned layouts = 0, treeLayouts = 0, treeErases = 0, treePaints = 0; };
LRESULT CALLBACK watchSettingsPaint(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	static const UINT layoutMessage = RegisterWindowMessage(L"dwt.Frame.Layout");
	auto& probe = *reinterpret_cast<SettingsPaintProbe*>(data);
	if(message == layoutMessage) ++probe.layouts;
	if(dwt::hwnd_cast<dwt::Tree*>(window)) {
		if(message == WM_WINDOWPOSCHANGING) ++probe.treeLayouts;
		if(message == WM_ERASEBKGND) ++probe.treeErases;
		if(message == WM_PAINT) ++probe.treePaints;
	}
	return DefSubclassProc(window, message, wParam, lParam);
}
bool clientNeedsPaint(HWND window) {
	RECT client;
	GetClientRect(window, &client);
	auto region = CreateRectRgn(0, 0, 0, 0);
	auto content = CreateRectRgnIndirect(&client);
	GetUpdateRgn(window, region, FALSE);
	const auto kind = CombineRgn(region, region, content, RGN_AND);
	DeleteObject(content);
	DeleteObject(region);
	return kind != NULLREGION;
}
void checkPresentedPage(PropPage* page) {
	const auto host = page->getParent()->handle();
	check(IsWindowVisible(page->handle()), "Selected settings page is hidden");
	check(GetWindowLongPtr(host, GWL_EXSTYLE) & WS_EX_COMPOSITED, "Settings page descendants paint without a shared buffer");
	unsigned visiblePages = 0;
	for(auto child = GetWindow(host, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
		if(dynamic_cast<PropPage*>(dwt::hwnd_cast<dwt::Widget*>(child)) && IsWindowVisible(child)) ++visiblePages;
	}
	check(visiblePages == 1, "Settings page switch exposed multiple pages");
	RECT pageBounds, hostBounds;
	GetWindowRect(page->handle(), &pageBounds);
	MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&pageBounds), 2);
	GetClientRect(host, &hostBounds);
	check(pageBounds.left == 0 && pageBounds.top == 0 && pageBounds.right >= hostBounds.right &&
		pageBounds.bottom >= hostBounds.bottom, "Page switch left a gap or retained a previous scroll offset");
}
void settleSettingsMessages() {
	MSG msg;
	unsigned count = 0;
	while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		check(++count < 2000, "Settings paint queue did not settle");
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}
struct PageCreationProbe { unsigned created = 0, unprotected = 0; };
LRESULT CALLBACK watchPageCreation(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	if(message == WM_PARENTNOTIFY && LOWORD(wParam) == WM_CREATE) {
		auto& probe = *reinterpret_cast<PageCreationProbe*>(data);
		++probe.created;
		if(GetWindowLongPtr(window, GWL_STYLE) & WS_VISIBLE) ++probe.unprotected;
	}
	return DefSubclassProc(window, message, wParam, lParam);
}

}

int dwtMain(dwt::Application& app) {
	try {
		wchar_t cwd[32768];
		check(GetCurrentDirectoryW(std::size(cwd), cwd) != 0, "Cannot find test workspace");
		const auto directory = Text::fromT(cwd) + "\\build\\theme-test-data\\";
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path) paths[static_cast<Util::Paths>(path)] = directory;
		Util::initialize(paths);
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		UserMatchManager::newInstance();
		auto settings = SettingsManager::getInstance();
		settings->setDefault(SettingsManager::MAIN_FONT, "Segoe UI,-12,400,0,1");
		settings->set(SettingsManager::NICK, "Theme test");
		WinUtil::initFont();
		WinUtil::textColor = RGB(0, 0, 0);
		WinUtil::bgColor = RGB(255, 255, 255);
		File::ensureDirectory(directory);

		UserMatch alice, bob;
		alice.name = bob.name = "Friends";
		alice.style.textColor = RGB(200, 30, 40);
		bob.style.textColor = RGB(20, 40, 210);
		alice.style.font = "Segoe UI,-12,400,0,1";
		bob.style.font = "Segoe UI,-16,700,1,1";
		alice.style.bgColor = RGB(230, 220, 210);
		bob.style.bgColor = RGB(210, 220, 230);
		UserMatchManager::getInstance()->setList({ alice, bob }, false);
		const auto duplicates = ThemeManager::capture();
		check(duplicates.userStyles.size() == 2, "Theme capture collapsed duplicate user-match names");
		const auto duplicatePath = ThemeManager::saveTheme(directory + "duplicates.xml", duplicates);
		const auto duplicateXml = File(duplicatePath, File::READ, File::OPEN).read();
		check(duplicateXml.find("Version=\"3\"") != string::npos, "Duplicate appearances did not use theme version 3");
		const auto duplicateTheme = ThemeManager::load(duplicatePath);
		check(duplicateTheme.userStyles.at({ "Friends", 1 }).textColor == alice.style.textColor &&
			duplicateTheme.userStyles.at({ "Friends", 2 }).textColor == bob.style.textColor,
			"Export/import merged duplicate appearances");
		// A separate session has different runtime identities but the same rule order.
		std::vector<UserMatch> imported(3);
		for(auto& match : imported) match.name = "Friends";
		imported[2].style.textColor = RGB(90, 90, 90);
		imported[1].setFlag(UserMatch::IGNORE_CHAT | UserMatch::GENERATED);
		UserMatch::Rule bobRule;
		bobRule.field = UserMatch::Rule::NICK;
		bobRule.pattern = "Bob";
		bobRule.setMethod(StringMatch::EXACT);
		imported[1].addRule(std::move(bobRule));
		const auto rulesBefore = imported[1].rules;
		const auto idBefore = imported[1].styleId;
		duplicateTheme.applyUserStyles(imported);
		check(imported[0].style.textColor == alice.style.textColor &&
			imported[1].style.font == FontStyle::decode(bob.style.font).encode(bob.style.font) &&
			imported[1].style.bgColor == bob.style.bgColor, "Applying duplicate appearances lost style components");
		check(imported[2].style.textColor == -1, "Missing occurrence reused a sibling's appearance");
		check(imported[1].rules == rulesBefore && imported[1].styleId == idBefore &&
			imported[1].isSet(UserMatch::IGNORE_CHAT | UserMatch::GENERATED), "Theme import changed matching rules or identity");
		auto nameOnly = duplicates;
		nameOnly.userStyles.clear();
		nameOnly.userStyles[{ "Friends", 0 }] = alice.style;
		const auto legacyNames = ThemeManager::load(ThemeManager::saveTheme(directory + "name-only.xml", nameOnly));
		legacyNames.applyUserStyles(imported);
		check(imported[0].style.textColor == alice.style.textColor && imported[1].style.textColor == alice.style.textColor,
			"Legacy name-only theme stopped applying to every matching name");
		for(const auto& replacement : { "", "0", "-1", "bad", "1", "3", "9999999" }) {
			auto malformed = duplicateXml;
			const auto at = malformed.find("Occurrence=\"2\"");
			check(at != string::npos, "Duplicate export omitted occurrence");
			malformed.replace(at, 14, string("Occurrence=\"") + replacement + "\"");
			const auto invalidPath = directory + "invalid-occurrence.xml";
			File(invalidPath, File::WRITE, File::CREATE | File::TRUNCATE).write(malformed);
			bool rejected = false;
			try { ThemeManager::load(invalidPath); } catch(const Exception&) { rejected = true; }
			check(rejected, "Invalid/duplicate/nonconsecutive occurrence was accepted");
		}
		UserMatchManager::getInstance()->setList({}, false);

		FontStyle effects;
		effects.underline = 2;
		effects.strike = 1;
		effects.spacing = 12;
		const auto font = effects.encode("Segoe UI,-16,700,1,1");
		check(FontStyle::decode(font).encode(font) == font, "Font effects did not round-trip");
		check(FontStyle::decode("Segoe UI,-12,400,0,1").underline == 0, "Legacy font gained effects");
		LOGFONT nativeFont;
		WinUtil::decodeFont(Text::toT(font), nativeFont);
		check(nativeFont.lfUnderline && nativeFont.lfStrikeOut, "Native font lost underline/strikeout");
		check(FontStyle::decode(Text::fromT(WinUtil::encodeFont(nativeFont))).strike == 1, "Native font serializer dropped strikeout");

		auto theme = ThemeManager::capture();
		theme.fonts[SettingsManager::CHAT_TEXT_FONT] = font;
		theme.colors[SettingsManager::CHAT_TEXT_COLOR] = RGB(7, 44, 99);
		theme.colors[SettingsManager::CHAT_TEXT_BG_COLOR] = RGB(222, 230, 240);
		Style userStyle;
		userStyle.font = font;
		userStyle.textColor = RGB(40, 70, 90);
		theme.userStyles[{ "Operators & friends", 0 }] = userStyle;
		const auto path = ThemeManager::saveTheme(directory + "roundtrip.xml", theme);
		const auto loaded = ThemeManager::load(path);
		check(loaded.fonts.at(SettingsManager::CHAT_TEXT_FONT) == font, "Theme export dropped font effects");
		check(loaded.colors == theme.colors, "Theme export changed colors");
		check(loaded.userStyles.at({ "Operators & friends", 0 }).font == font, "Theme export dropped user appearance");
		check(UserMatchManager::getInstance()->getList().empty(), "Theme loading changed matching definitions");
		const auto legacy = directory + "legacy.xml";
		File(legacy, File::WRITE, File::CREATE | File::TRUNCATE).write(
			"<DCPlusPlusTheme Name=\"Legacy\"><Palette Background=\"#101010\" Surface=\"#202020\" Text=\"#EEEEEE\" "
			"DisabledText=\"#999999\" Border=\"#555555\" Accent=\"#44AAEE\" HighlightText=\"#FFFFFF\"/></DCPlusPlusTheme>");
		const auto old = ThemeManager::load(legacy);
		check(old.colors.at(SettingsManager::CHAT_TEXT_COLOR) == RGB(238, 238, 238), "Legacy palette did not seed chat colors");
		check(old.colors.at(SettingsManager::CHAT_TEXT_BG_COLOR) == RGB(16, 16, 16), "Legacy palette did not seed chat background");

		for(const auto& file : File::findFiles(Text::fromT(cwd) + "\\Themes\\", "*.xml")) ThemeManager::load(file);
		const auto invalid = directory + "invalid.xml";
		auto invalidXml = File(path, File::READ, File::OPEN).read();
		const auto setting = invalidXml.find("ChatTextFont");
		check(setting != string::npos, "Export omitted ChatTextFont");
		invalidXml.replace(setting, 12, "DownloadDirectory");
		File(invalid, File::WRITE, File::CREATE | File::TRUNCATE).write(invalidXml);
		bool rejected = false;
		try { ThemeManager::load(invalid); } catch(const Exception&) { rejected = true; }
		check(rejected, "Theme import accepted a non-appearance setting");

		auto window = new dwt::Window();
		window->create();
		auto box = window->addChild(dwt::RichTextBox::Seed());
		box->resize(dwt::Rectangle(0, 0, 700, 200));
		box->setFont(WinUtil::font);
		const auto originalColor = SETTING(CHAT_TEXT_COLOR);
		render(box, loaded, "<span id=\"text\">Hello</span>");
		const auto format = formatAt(box, 0);
		check(format.crTextColor == RGB(7, 44, 99), "Preview does not use draft theme text color");
		check(format.crBackColor == RGB(222, 230, 240), "Preview does not use draft highlight color");
		check((format.dwEffects & (CFE_BOLD | CFE_ITALIC | CFE_STRIKEOUT | CFE_UNDERLINE)) ==
			(CFE_BOLD | CFE_ITALIC | CFE_STRIKEOUT | CFE_UNDERLINE), "RichEdit did not render configured font effects");
		check(SETTING(CHAT_TEXT_COLOR) == originalColor, "Preview changed active settings");
		auto deliberate = loaded;
		deliberate.colors[SettingsManager::CHAT_TEXT_COLOR] = deliberate.colors[SettingsManager::CHAT_TEXT_BG_COLOR];
		render(box, deliberate, "<span id=\"text\"><b>Theme colors</b></span>");
		check(formatAt(box, 0).crTextColor == static_cast<COLORREF>(deliberate.colors.at(SettingsManager::CHAT_TEXT_COLOR)),
			"Nested markup overrode an explicit theme color");
		auto advanced = loaded;
		auto advancedEffects = effects;
		advancedEffects.script = 1;
		advanced.fonts[SettingsManager::CHAT_TEXT_FONT] = advancedEffects.encode(font);
		render(box, advanced, "<span id=\"text\">Superscript</span>");
		check((formatAt(box, 0).dwEffects & CFE_SUPERSCRIPT) != 0, "Superscript did not reach RichEdit");
		check(formatAt(box, 0).bUnderlineType == CFU_UNDERLINEDOUBLE, "Double underline did not reach RichEdit");
		auto plain = loaded;
		plain.fonts[SettingsManager::MAIN_FONT] = "Segoe UI,-12,700,1,1";
		plain.fonts[SettingsManager::CHAT_TEXT_FONT] = "Segoe UI,-12,400,0,1";
		render(box, plain, "<b><i><span id=\"text\">Normal</span></i></b>");
		check((formatAt(box, 0).dwEffects & (CFE_BOLD | CFE_ITALIC)) == 0, "Explicit normal font inherited bold/italic");
		auto previewTheme = loaded;
		previewTheme.liveUserStyles[alice.styleId] = userStyle;
		render(box, previewTheme, "<span id=\"nick\" themeFontStyle=\"" + alice.styleId + "\" themeTextStyle=\"" + alice.styleId + "\" style=\"color:#FF0000\">Alice</span>");
		check(formatAt(box, 0).crTextColor == static_cast<COLORREF>(userStyle.textColor), "User-match preview retained stale inline colors");

		auto chat = new RichTextBox(window);
		chat->create(RichTextBox::Seed());
		chat->setFont(WinUtil::font);
		chat->setTextLimit(128 * 1024);
		chat->resize(dwt::Rectangle(0, 0, 700, 200));
		chat->setReadOnly();
		WinUtil::setColor(chat);
		UserMatchManager::getInstance()->setList({ alice, bob }, false);
		const auto bobHtml = "<span id=\"nick\" themeFontStyle=\"" + bob.styleId + "\" themeTextStyle=\"" + bob.styleId +
			"\" themeBgStyle=\"" + alice.styleId + "\">Bob</span>";
		chat->appendMessages({ { L"", L"Bob", L"Bob", "", 0, bobHtml, "" } });
		check(formatAt(chat, 0).crTextColor == static_cast<COLORREF>(bob.style.textColor) &&
			formatAt(chat, 0).crBackColor == static_cast<COLORREF>(alice.style.bgColor) &&
			(formatAt(chat, 0).dwEffects & (CFE_BOLD | CFE_ITALIC)) == (CFE_BOLD | CFE_ITALIC),
			"Duplicate-name chat resolved the wrong component source");
		render(box, duplicates, bobHtml);
		check(formatAt(box, 0).crTextColor == static_cast<COLORREF>(bob.style.textColor), "Draft preview merged duplicate names");
		auto renamedBob = bob;
		renamedBob.name = "Renamed";
		renamedBob.style.textColor = RGB(40, 150, 70);
		UserMatchManager::getInstance()->setList({ renamedBob, alice }, false);
		chat->refreshMessageStyles();
		check(formatAt(chat, 0).crTextColor == static_cast<COLORREF>(renamedBob.style.textColor),
			"Reordering/renaming detached retained chat from its appearance");
		UserMatchManager::getInstance()->setList({ alice }, false);
		chat->refreshMessageStyles();
		check(formatAt(chat, 0).crTextColor == static_cast<COLORREF>(SETTING(CHAT_NICK_COLOR)),
			"Deleting a definition retargeted retained chat to another duplicate");
		UserMatchManager::getInstance()->setList({}, false);
		chat->setText(L"");
		chat->clearMessageMetadata();
		chat->appendMessages({ { L"", L"Hello", L"", "", 0, "<span id=\"text\">Hello</span>", "" },
			{ L"", L"World", L"", "", 0, "<span id=\"text\">World</span>", "" } });
		chat->setSelection(1, 3);
		const auto chatText = chat->getText();
		settings->set(SettingsManager::CHAT_TEXT_COLOR, RGB(90, 40, 170));
		settings->set(SettingsManager::CHAT_TEXT_FONT, font);
		chat->sendCommand(ID_UPDATECOLOR);
		check(chat->getText() == chatText, "Live theme refresh changed chat content");
		check(chat->getCaretPosRange() == std::make_pair(1, 3), "Live theme refresh moved the selection");
		check(chat->isReadOnly(), "Live theme refresh left chat editable");
		check(formatAt(chat, 7).crTextColor == RGB(90, 40, 170), "Live theme refresh did not recolor existing messages");
		check((formatAt(chat, 7).dwEffects & CFE_STRIKEOUT) != 0, "Live theme refresh lost font effects");
		chat->setTextLimit(8);
		chat->appendMessages({ { L"", L"abcdefghijklmno", L"", "", 0, "<span id=\"text\">abcdefghijklmno</span>", "" } });
		const auto trimmed = chat->getText();
		chat->sendCommand(ID_UPDATECOLOR);
		check(chat->getText() == trimmed, "Live refresh resurrected trimmed chat text");
		settings->set(SettingsManager::CHAT_TEXT_COLOR, originalColor);
		settings->unset(SettingsManager::CHAT_TEXT_FONT);


		TextEffectsDlg effectsDialog(window, L"Chat text", nativeFont, effects, RGB(240, 230, 220), RGB(24, 24, 32));
		effectsDialog.create(dwt::ModalDialog::Seed(dwt::Point(650, 0)));
		std::vector<HWND> effectChildren;
		EnumChildWindows(effectsDialog.handle(), collectChildren, reinterpret_cast<LPARAM>(&effectChildren));
		auto control = [&](const wchar_t* label) -> dwt::Control* {
			for(auto child : effectChildren) {
				auto widget = dwt::hwnd_cast<dwt::Control*>(child);
				wchar_t caption[256];
				GetWindowTextW(child, caption, 256);
				if(widget && std::wstring(caption) == label) return widget;
			}
			throw std::runtime_error("Missing text effects control");
		};
		auto single = dynamic_cast<dwt::CheckBox*>(control(L"Strikethrough"));
		auto doubleStrike = dynamic_cast<dwt::CheckBox*>(control(L"Double strikethrough"));
		check(single && doubleStrike && single->getChecked(), "Initial strikethrough state was lost");
		doubleStrike->sendMessage(BM_CLICK);
		check(effectsDialog.getEffects().strike == 2 && !::IsWindowEnabled(single->handle()), "Double strike did not supersede single");
		doubleStrike->sendMessage(BM_CLICK);
		check(effectsDialog.getEffects().strike == 1 && ::IsWindowEnabled(single->handle()), "Turning off double strike lost single strike");
		auto sub = dynamic_cast<dwt::CheckBox*>(control(L"Subscript"));
		auto super = dynamic_cast<dwt::CheckBox*>(control(L"Superscript"));
		sub->sendMessage(BM_CLICK);
		super->sendMessage(BM_CLICK);
		check(!sub->getChecked() && super->getChecked() && effectsDialog.getEffects().script == 1,
			"Subscript and superscript are not exclusive");
		auto small = dynamic_cast<dwt::CheckBox*>(control(L"Small capitals"));
		auto all = dynamic_cast<dwt::CheckBox*>(control(L"All capitals"));
		small->sendMessage(BM_CLICK);
		all->sendMessage(BM_CLICK);
		check(!small->getChecked() && effectsDialog.getEffects().effects == FontStyle::AllCaps,
			"Capitalization options are not exclusive");
		dwt::TextBox* spacingBox = nullptr;
		dwt::RichTextBox* effectPreview = nullptr;
		for(auto child : effectChildren) {
			auto widget = dwt::hwnd_cast<dwt::Control*>(child);
			if(auto preview = dynamic_cast<dwt::RichTextBox*>(widget)) effectPreview = preview;
			else if(auto input = dynamic_cast<dwt::TextBox*>(widget); input && !spacingBox) spacingBox = input;
		}
		check(spacingBox && effectPreview, "Effects dialog is missing its editor or RichEdit preview");
		spacingBox->setText(L"99");
		check(!::IsWindowEnabled(control(L"OK")->handle()), "Invalid spacing can be applied");
		spacingBox->setText(L"1.25");
		check(::IsWindowEnabled(control(L"OK")->handle()) && effectsDialog.getEffects().spacing == 25, "Point spacing conversion failed");
		control(L"Reset effects")->sendMessage(BM_CLICK);
		check(effectsDialog.getEffects().strike == 0 && effectsDialog.getEffects().script == 0 &&
			effectsDialog.getEffects().spacing == 0 && effectsDialog.getFontValue().lfWeight == FW_NORMAL,
			"Reset effects did not restore normal text");
		control(L"Bold")->sendMessage(BM_CLICK);
		control(L"Underline")->sendMessage(BM_CLICK);
		check((formatAt(effectPreview, 0).dwEffects & (CFE_BOLD | CFE_UNDERLINE)) == (CFE_BOLD | CFE_UNDERLINE),
			"Checkbox changes did not reach the live RichEdit preview");
		effectsDialog.resize(dwt::Rectangle(dwt::Point(-32000, -32000), effectsDialog.getWindowSize()));
		ShowWindow(effectsDialog.handle(), SW_SHOWNOACTIVATE);
		RECT effectsClient, okBounds;
		GetClientRect(effectsDialog.handle(), &effectsClient);
		GetWindowRect(control(L"OK")->handle(), &okBounds);
		MapWindowPoints(nullptr, effectsDialog.handle(), reinterpret_cast<POINT*>(&okBounds), 2);
		check(okBounds.bottom <= effectsClient.bottom, "Text effects footer is clipped");
		snapshot(effectsDialog.handle(), directory + "text-effects.bmp");
		DestroyWindow(effectsDialog.handle());
		check(SETTING(CHAT_TEXT_COLOR) == originalColor, "Effects preview changed active settings");

		UserMatch definition;
		definition.name = "Operators & friends";
		definition.setFlag(UserMatch::OPS | UserMatch::IGNORE_CHAT);
		definition.style = userStyle;
		UserMatch::Rule rule;
		rule.field = UserMatch::Rule::NICK;
		rule.pattern = "Alice";
		rule.setMethod(StringMatch::EXACT);
		definition.addRule(std::move(rule));
		UserMatchManager::getInstance()->setList({ definition }, false);

		// Construct the real settings pages, select a preset through the native
		// combo notification, and verify that cancellation leaves settings alone.
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		SettingsDialog dialog(window);
		dialog.create(dwt::ModalDialog::Seed(dwt::Point(1000, 800), WS_SIZEBOX));
		auto page = dialog.getPage<ThemePage>();
		dialog.getPage<UserMatchPage>()->updateStyles();
		dialog.activatePage<ThemePage>();
		page->layout();
		std::vector<HWND> children;
		EnumChildWindows(page->handle(), collectChildren, reinterpret_cast<LPARAM>(&children));
		HWND preset = nullptr;
		bool foundPreview = false;
		for(auto child : children) {
			auto widget = dwt::hwnd_cast<dwt::Control*>(child);
			if(dynamic_cast<dwt::ComboBox*>(widget) && SendMessage(child, CB_GETCOUNT, 0, 0) > 3) preset = child;
			if(dynamic_cast<dwt::RichTextBox*>(widget)) foundPreview = true;
		}
		check(preset && foundPreview, "Theme editor is missing its preset selector or RichEdit preview");
		SendMessage(preset, CB_SETCURSEL, 0, 0);
		SendMessage(GetParent(preset), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(preset), CBN_SELENDOK), reinterpret_cast<LPARAM>(preset));
		check(SETTING(CHAT_TEXT_COLOR) == originalColor, "Selecting a theme applied settings before OK");
		const auto definitions = UserMatchManager::getInstance()->getList();
		check(definitions.size() == 1 && definitions[0].rules == definition.rules &&
			definitions[0].isSet(UserMatch::IGNORE_CHAT) && definitions[0].style.font == userStyle.font,
			"Theme draft changed active user matching definitions or appearance");
		dialog.resize(dwt::Rectangle(-32000, -32000, 1100, 850));
		page->layout();
		ShowWindow(dialog.handle(), SW_SHOWNOACTIVATE);
		RedrawWindow(dialog.handle(), nullptr, nullptr, RDW_VALIDATE | RDW_NOERASE | RDW_NOFRAME | RDW_ALLCHILDREN);
		dialog.setText(L"Settings painting regression");
		dialog.sendMessage(WM_NCACTIVATE, FALSE);
		dialog.sendMessage(WM_NCACTIVATE, TRUE);
		check(!clientNeedsPaint(dialog.handle()), "Settings caption invalidated its client area");
		for(auto child : children) check(!clientNeedsPaint(child), "Settings caption invalidated a page control");
		unsigned layouts = 0;
		for(auto child : children) SetWindowSubclass(child, countLayout, 1, reinterpret_cast<DWORD_PTR>(&layouts));
		SetWindowPos(dialog.handle(), nullptr, -31990, -31990, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		for(auto child : children) RemoveWindowSubclass(child, countLayout, 1);
		check(layouts == 0, "Moving settings relaid out its page controls");
		dialog.activatePage<UserMatchPage>();
		check(!IsWindowVisible(page->handle()), "Switching settings left the previous page visible");
		dialog.activatePage<ThemePage>();
		check(IsWindowVisible(page->handle()), "Switching settings did not show the theme page");
		PageCreationProbe creation;
		SetWindowSubclass(page->getParent()->handle(), watchPageCreation, 9, reinterpret_cast<DWORD_PTR>(&creation));
		SettingsPaintProbe paintProbe;
		std::vector<HWND> dialogChildren;
		EnumChildWindows(dialog.handle(), collectChildren, reinterpret_cast<LPARAM>(&dialogChildren));
		HWND settingsTree = nullptr;
		for(auto child : dialogChildren) {
			if(dwt::hwnd_cast<dwt::Tree*>(child)) { settingsTree = child; break; }
		}
		check(settingsTree != nullptr, "Settings navigation tree is missing");
		SetWindowSubclass(dialog.handle(), watchSettingsPaint, 10, reinterpret_cast<DWORD_PTR>(&paintProbe));
		SetWindowSubclass(settingsTree, watchSettingsPaint, 10, reinterpret_cast<DWORD_PTR>(&paintProbe));
		unsigned switchLayouts = 0, switchTreeLayouts = 0;
		for(auto mode : { dwt::Appearance::Mode::Dark, dwt::Appearance::Mode::Light }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			for(auto size : { dwt::Point(650, 420), dwt::Point(1100, 850) }) {
				dialog.resize(dwt::Rectangle(dwt::Point(-32000, -32000), size));
				settleSettingsMessages();
				paintProbe = {};
				const auto switchStart = std::chrono::steady_clock::now();
				// Windows is constructed lazily on its first selection; subsequent
				// visits exercise cached pages and changes in scrollbar visibility.
				dialog.activatePage<WindowsPage>();
				settleSettingsMessages();
				checkPresentedPage(dialog.getPage<WindowsPage>());
				dialog.activatePage<UserMatchPage>();
				settleSettingsMessages();
				checkPresentedPage(dialog.getPage<UserMatchPage>());
				dialog.activatePage<ThemePage>();
				settleSettingsMessages();
				checkPresentedPage(page);
				const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - switchStart).count();
				std::printf("Three settings switches (%s, %ldx%ld): %lld ms; dialog layouts %u, tree layouts %u, tree erases %u, tree paints %u\n", mode == dwt::Appearance::Mode::Dark ? "dark" : "light", size.x, size.y, static_cast<long long>(elapsed), paintProbe.layouts, paintProbe.treeLayouts, paintProbe.treeErases, paintProbe.treePaints);
				switchLayouts += paintProbe.layouts;
				switchTreeLayouts += paintProbe.treeLayouts;
			}
		}
		RemoveWindowSubclass(settingsTree, watchSettingsPaint, 10);
		RemoveWindowSubclass(dialog.handle(), watchSettingsPaint, 10);
		check(switchLayouts == 0 && switchTreeLayouts == 0, "Settings page switches relaid out the entire dialog and navigation tree");
		RemoveWindowSubclass(page->getParent()->handle(), watchPageCreation, 9);
		check(creation.created > 0 && creation.unprotected == 0, "Lazy page creation happened before suspending redraw");
		app.getAppearance().configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		snapshot(page->handle(), directory + "theme-editor.bmp");
		page->write();
		check(static_cast<COLORREF>(SETTING(CHAT_TEXT_COLOR)) == dwt::Appearance::defaultPalette().text, "Applying a preset did not save chat colors");
		DestroyWindow(dialog.handle());
		DestroyWindow(window->handle());
		std::puts("Theme files, live effects, RichEdit previews, dark settings painting, layout and settings isolation passed.");
		UserMatchManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
