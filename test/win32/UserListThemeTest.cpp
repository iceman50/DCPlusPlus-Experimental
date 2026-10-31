// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"
#include <win32/app/SingleInstance.h>
#include <win32/app/UiServices.h>
#include <win32/appearance/AppAppearance.h>
#include <win32/appearance/ThemeManager.h>
#include <win32/appearance/UiFonts.h>
#include <dcpp/app/ResourceManager.h>
#include <dcpp/user/UserMatchManager.h>
#include <dwt/Application.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/Window.h>
#include <win32/controls/TypedTable.h>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 183;

namespace {
void check(bool value, const char* message) {
	if(!value) throw std::runtime_error(message);
}

struct Row {
	tstring name;
	Style snapshot;
	COLORREF expectedText = 0, expectedBackground = 0;
	std::string expectedFont;
	unsigned paints = 0;
	bool incorrect = false;
	const tstring& getText(int) const { return name; }
	int getStyle(HFONT& font, COLORREF& text, COLORREF& background, int) {
		AppAppearance::applyUserStyle(snapshot, font, text, background);
		++paints;
		incorrect |= text != expectedText || background != expectedBackground;
		if(!expectedFont.empty()) {
			const auto cached = UiFonts::getUserMatchFont(expectedFont);
			incorrect |= !cached || font != cached->handle();
		} else {
			incorrect |= font != nullptr;
		}
		return CDRF_NEWFONT;
	}
};
}

int dwtMain(dwt::Application& app) {
	try {
		Util::PathsMap paths;
		for(int i = 0; i < Util::PATH_LAST; ++i) paths[static_cast<Util::Paths>(i)] = "build/user-list-theme/";
		Util::initialize(paths);
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		UserMatchManager::newInstance();
		AppAppearance::init();
		UiFonts::initDefault();
		UiServices::initFont();
		AppAppearance::initStyleDefaults();

		UserMatch favorite, op;
		favorite.name = "Favorites";
		op.name = "Operators";
		favorite.style.textColor = RGB(180, 0, 0);
		op.style.textColor = RGB(0, 0, 180);
		UserMatchManager::getInstance()->setList({favorite, op}, false);
		auto theme = ThemeManager::capture();
		Row rows[4];
		rows[0].name = L"Favorite";
		rows[0].snapshot = favorite.style;
		rows[0].snapshot.matchingStyles = {favorite.styleId};
		rows[1].name = L"Operator";
		rows[1].snapshot = op.style;
		rows[1].snapshot.matchingStyles = {op.styleId};
		rows[2].name = L"Favorite operator";
		rows[2].snapshot = favorite.style;
		rows[2].snapshot.matchingStyles = {favorite.styleId, op.styleId};
		rows[3].name = L"Ordinary user";

		auto window = new dwt::Window;
		dwt::Window::Seed seed;
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		window->resize(dwt::Rectangle(0, 0, 600, 300));
		using UserTable = TypedTable<Row, false, dwt::Table>;
		auto table = new UserTable(window);
		table->create(UserTable::Seed(dwt::Table::Seed()));
		table->resize(dwt::Rectangle(0, 0, 580, 260));
		table->addColumn(L"User", 300);
		table->setFont(UiFonts::font);
		AppAppearance::setColor(table);
		for(auto& row : rows) table->insert(&row);

		const auto dc = CreateCompatibleDC(nullptr);
		const auto bitmap = CreateBitmap(600, 300, 1, 32, nullptr);
		const auto previous = SelectObject(dc, bitmap);
		for(int round = 0; round < 6; ++round) {
			const bool dark = round % 2 == 0;
			const auto text = dark ? RGB(230, 235, 240) : RGB(20, 30, 40);
			const auto background = dark ? RGB(25, 30, 35) : RGB(245, 245, 245);
			theme.colors[SettingsManager::TEXT_COLOR] = text;
			theme.colors[SettingsManager::BACKGROUND_COLOR] = background;
			favorite.style = Style();
			op.style = Style();
			if(dark) {
				favorite.style.font = "Segoe UI,-16,700,0,1";
				favorite.style.textColor = RGB(255, 150, 160);
				favorite.style.bgColor = RGB(50, 40, 30);
				op.style.textColor = RGB(160, 180, 255);
			} else {
				// Clearing the favorite foreground must reveal the operator's override.
				op.style.textColor = RGB(25, 40, 180);
				op.style.bgColor = RGB(210, 225, 245);
			}
			theme.captureUserStyles({favorite, op});
			for(auto& row : rows) {
				row.expectedText = text;
				row.expectedBackground = background;
				row.expectedFont.clear();
				row.paints = 0;
				row.incorrect = false;
			}
			rows[0].expectedText = dark ? favorite.style.textColor : text;
			rows[0].expectedBackground = dark ? favorite.style.bgColor : background;
			rows[0].expectedFont = favorite.style.font;
			rows[1].expectedText = op.style.textColor;
			rows[1].expectedBackground = dark ? background : op.style.bgColor;
			rows[2].expectedText = dark ? favorite.style.textColor : op.style.textColor;
			rows[2].expectedBackground = dark ? favorite.style.bgColor : op.style.bgColor;
			rows[2].expectedFont = favorite.style.font;
			{
				dwt::Appearance::UpdateScope update(app.getAppearance());
				// Deliberately leave every row's identity snapshot untouched, just as
				// it would be while a hub update waits for its batching timer.
				ThemeManager::apply(theme, false);
				AppAppearance::refreshTheme();
			}
			// Exercise real list-view custom drawing before dispatching any messages.
			SendMessage(table->handle(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
			for(const auto& row : rows) {
				check(row.paints > 0, "List-view did not paint the test row");
				check(!row.incorrect, "First row paint used stale user colors or font");
			}
		}
		SelectObject(dc, previous);
		DeleteObject(bitmap);
		DeleteDC(dc);
		DestroyWindow(window->handle());
		UserMatchManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("User-list colors and fonts refreshed on the first paint for all six theme switches.");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
