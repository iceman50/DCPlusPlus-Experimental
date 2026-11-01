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
#include <win32/app/SplashWindow.h>
#include <win32/appearance/IconManager.h>
#include <win32/resource.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/text/format.h>
#include <dcpp/text/Text.h>
#include <dcpp/version.h>
#include <dwt/Appearance.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/DibBitmap.h>
#include <dwt/resources/Font.h>
#include <cmath>

SplashWindow::SplashWindow() : dwt::Window(nullptr), started(GetTickCount64()) {
	const auto title = _T(APPNAME) _T(" ") _T(VERSIONSTRING) _T(" ") + Text::toT(buildLabel);
	Seed seed(title);
	seed.style = WS_POPUP;
	seed.exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
	create(seed);
	setFrameRendering(dwt::Frame::FrameRendering::Native);
	setFont(nullptr);
	status = T_("Starting up...");
	pendingStatus = status;
	enableAccessibility();
	setAccessibleName(title);
	setAccessibleHelpText(status);
	onClosing([this] { return finished; });
	onPainting([this](dwt::PaintCanvas& canvas) { draw(canvas); });
	onPrinting([this](dwt::Canvas& canvas) { draw(canvas); });
	onRaw([](WPARAM, LPARAM) -> LRESULT { return 1; }, dwt::Message(WM_ERASEBKGND));
	onDpiChanged([this](const dwt::DpiChangedEvent& event) { updateLayout(event.newDpi); });
	updateLayout(getDpi());
	// Fast startup never flashes a splash; slow startup has no minimum display time.
	setTimer([this] {
		if(!getVisible() && settingsReady && GetTickCount64() - started >= 350)
			::ShowWindow(handle(), SW_SHOWNOACTIVATE);
		return !getVisible();
	}, 50, 1);
}

SplashWindow::~SplashWindow() = default;

void SplashWindow::settingsLoaded() {
	// Snapshot on the loader thread after SettingsManager::load(), before posting.
	const auto setting = SETTING(THEME_MODE);
	const auto mode = setting == SettingsManager::THEME_LIGHT ? dwt::Appearance::Mode::Light :
		setting == SettingsManager::THEME_DARK ? dwt::Appearance::Mode::Dark : dwt::Appearance::Mode::System;
	const dwt::Appearance::Palette palette {
		static_cast<COLORREF>(SETTING(THEME_BACKGROUND_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_SURFACE_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_TEXT_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_DISABLED_TEXT_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_BORDER_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_ACCENT_COLOR)),
		static_cast<COLORREF>(SETTING(THEME_HIGHLIGHT_TEXT_COLOR))
	};
	callAsync([this, mode, palette] {
		dwt::Application::instance().getAppearance().configure(mode, palette);
		settingsReady = true;
	});
}

void SplashWindow::finish() {
	finished = true;
	close();
}

void SplashWindow::operator()(const string& stage) {
	std::lock_guard<std::mutex> lock(pendingMutex);
	pendingStatus = str(TF_("Loading: %1%") % Text::toT(stage));
	queueUpdate();
}

void SplashWindow::operator()(float value) {
	std::lock_guard<std::mutex> lock(pendingMutex);
	// Keep completed work visible even across stages and invalid/stale updates.
	if(!std::isfinite(value)) return;
	pendingProgress = std::max(pendingProgress, std::clamp(value, 0.0f, 1.0f));
	queueUpdate();
}

void SplashWindow::queueUpdate() {
	if(updatePending) return;
	updatePending = true;
	callAsync([this] {
		{
			std::lock_guard<std::mutex> lock(pendingMutex);
			status = pendingStatus;
			progress = pendingProgress;
			updatePending = false;
		}
		setAccessibleHelpText(status);
		redraw();
	});
}

void SplashWindow::appearanceChanged() {
	dwt::Window::appearanceChanged();
	redraw();
}

void SplashWindow::updateLayout(unsigned dpi) {
	if(fontDpi != dpi) {
		fontDpi = dpi;
		auto lf = getFont()->getLogFont();
		lf.lfHeight = -pixels(26);
		lf.lfWeight = FW_SEMIBOLD;
		titleFont = new dwt::Font(lf);
		lf.lfHeight = -pixels(13);
		lf.lfWeight = FW_NORMAL;
		detailFont = new dwt::Font(lf);
		icon = IconManager::load(IDI_DCPP, pixels(80));
	}
	MONITORINFO monitor { sizeof(monitor) };
	::GetMonitorInfo(::MonitorFromWindow(handle(), MONITOR_DEFAULTTOPRIMARY), &monitor);
	const auto width = pixels(440), height = pixels(300);
	::SetWindowPos(handle(), nullptr,
		monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
		monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
		width, height, SWP_NOZORDER | SWP_NOACTIVATE);
	redraw();
}

void SplashWindow::draw(dwt::Canvas& target) {
	const auto size = getClientSize();
	if(size.x <= 0 || size.y <= 0 || !titleFont) return;
	dwt::CompatibleCanvas canvas(target.handle());
	dwt::DibBitmap bitmap(target.handle(), size, dwt::DibBitmap::RowOrder::TopDown);
	if(!bitmap.handle()) return;
	auto selectBitmap = canvas.select(bitmap);
	const auto& appearance = dwt::Application::instance().getAppearance();
	auto palette = appearance.getPalette();
	if(!appearance.isManual()) {
		palette.background = ::GetSysColor(COLOR_WINDOW);
		palette.text = ::GetSysColor(COLOR_WINDOWTEXT);
		palette.disabledText = ::GetSysColor(COLOR_GRAYTEXT);
		palette.border = ::GetSysColor(COLOR_WINDOWFRAME);
	}
	const auto background = palette.background;
	const auto blue = appearance.isHighContrast() ? palette.accent : RGB(22, 136, 240);
	canvas.fill(dwt::Rectangle(0, 0, size.x, size.y), dwt::Brush(palette.border));
	const auto edge = pixels(1);
	canvas.fill(dwt::Rectangle(edge, edge, size.x - 2 * edge, size.y - 2 * edge), dwt::Brush(background));
	canvas.drawIcon(icon, dwt::Rectangle((size.x - pixels(80)) / 2, pixels(34), pixels(80), pixels(80)));
	auto bkMode = canvas.setBkMode(true);
	auto text = [&](const tstring& value, int top, int height, const dwt::FontPtr& font, COLORREF color, UINT align) {
		auto selectFont = canvas.select(*font);
		canvas.setTextColor(color);
		auto rect = dwt::Rectangle(pixels(32), pixels(top), size.x - pixels(64), pixels(height));
		canvas.drawText(value, rect, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	};
	text(_T(APPNAME), 128, 36, titleFont, palette.text, DT_CENTER);
	text(_T(VERSIONSTRING) _T(" ") + Text::toT(buildLabel), 168, 22, detailFont, palette.disabledText, DT_CENTER);
	text(T_("Starting up..."), 196, 22, detailFont, palette.disabledText, DT_CENTER);
	text(status, 240, 22, detailFont, palette.text, DT_LEFT);
	const long left = pixels(32), top = pixels(272), width = size.x - pixels(64), height = pixels(4);
	const auto track = appearance.isHighContrast() ? palette.background :
		dwt::Appearance::blend(background, palette.text, 32);
	canvas.fill(dwt::Rectangle(left, top, width, height), dwt::Brush(track));
	canvas.fill(dwt::Rectangle(left, top, static_cast<long>(width * progress), height), dwt::Brush(blue));
	::BitBlt(target.handle(), 0, 0, size.x, size.y, canvas.handle(), 0, 0, SRCCOPY);
}
