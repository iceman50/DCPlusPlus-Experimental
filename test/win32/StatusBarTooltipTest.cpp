// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// The status history tooltip must stay small whatever a hub sends as status text: ten multi-line
// hub banners made it taller than the screen, covering every window (DC++ Experimental bug 2174725).
#include "stdafx.h"
#include <win32/SingleInstance.h>
#define private public
#include <dwt/widgets/StatusBar.h>
#undef private
#include <dwt/Application.h>
#include <dwt/widgets/ToolTip.h>
#include <dwt/widgets/Window.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 183;

namespace {

void check(bool value, const char* message) {
	if(!value) throw std::runtime_error(message);
}

/** The height the tooltip needs for its text, measured as the tooltip control does. */
long tipHeight(dwt::ToolTipPtr tip, const tstring& text, long width) {
	auto dc = ::GetDC(tip->handle());
	auto old = ::SelectObject(dc, reinterpret_cast<HFONT>(tip->sendMessage(WM_GETFONT)));
	RECT r { 0, 0, width, 0 };
	::DrawText(dc, text.c_str(), static_cast<int>(text.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
	::SelectObject(dc, old);
	::ReleaseDC(tip->handle(), dc);
	return r.bottom - r.top;
}

}

int dwtMain(dwt::Application&) {
	try {
		// Single lines are kept; other lines and overlong text are cut and marked.
		check(dwt::StatusBar::historyLine(_T("[12:00] Connected")) == _T("[12:00] Connected"), "A single line was changed");
		check(dwt::StatusBar::historyLine(_T("[12:00] Welcome\r\n")) == _T("[12:00] Welcome"), "A trailing line break was kept");
		check(dwt::StatusBar::historyLine(_T("[12:00] Welcome\r\nRule 1\r\nRule 2")) == _T("[12:00] Welcome..."),
			"Further lines of a banner were kept");
		for(auto lineBreak: { _T("\n"), _T("\r"), _T("\v"), _T("\f"), _T("\x85"), _T("\x2028"), _T("\x2029") })
			check(dwt::StatusBar::historyLine(tstring(_T("a")) + lineBreak + _T("b")) == _T("a..."), "A line break was missed");
		const auto longLine = dwt::StatusBar::historyLine(tstring(5000, _T('x')));
		check(longLine.size() == dwt::StatusBar::MAX_LINE_LENGTH && longLine.compare(longLine.size() - 3, 3, _T("...")) == 0,
			"An overlong line was not shortened");

		auto window = new dwt::Window();
		dwt::Window::Seed seed;
		seed.caption = L"Status bar tooltip test";
		seed.location = dwt::Rectangle(-32000, -32000, 1920, 300);
		window->create(seed);
		auto status = window->addChild(dwt::StatusBar::Seed(2, 0));
		status->setSize(1, 100);
		status->refresh();

		tstring banner;
		for(int line = 0; line < 25; ++line)
			banner += _T("Welcome to the hub - rules line ") + std::to_wstring(line) + _T("\r\n");
		for(int i = 0; i < 12; ++i)
			status->setText(0, _T("[12:00] ") + banner);
		status->setText(0, _T("[12:01] Connected"));

		const auto& tip = status->getPart(0).tip;
		check(std::count(tip.begin(), tip.end(), _T('\n')) == dwt::StatusBar::MAX_LINES - 1, "The tooltip does not hold the last ten entries");
		check(tip.find(_T("rules line 1")) == tstring::npos, "Banner lines reached the tooltip");
		check(tip.compare(tip.size() - 17, 17, _T("[12:01] Connected")) == 0, "The newest entry is not last");

		MONITORINFO monitor { sizeof(monitor) };
		::GetMonitorInfo(::MonitorFromWindow(window->handle(), MONITOR_DEFAULTTOPRIMARY), &monitor);
		const auto height = tipHeight(status->tip, tip, static_cast<long>(status->getPart(0).actualSize));
		check(height > 0 && height < (monitor.rcWork.bottom - monitor.rcWork.top) / 3, "The tooltip is still too tall");
		// The longest entries stay well within the screen too.
		for(int i = 0; i < 10; ++i)
			status->setText(0, tstring(5000, _T('w')));
		const auto longest = tipHeight(status->tip, status->getPart(0).tip, static_cast<long>(status->getPart(0).actualSize));
		check(longest < (monitor.rcWork.bottom - monitor.rcWork.top) / 2, "Long entries make the tooltip too tall");

		DestroyWindow(window->handle());
		std::printf("Status history tooltip stays within %ld px for banners and %ld px for the longest entries.\n", height, longest);
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
