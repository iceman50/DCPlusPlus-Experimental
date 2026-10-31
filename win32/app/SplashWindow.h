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

#ifndef DCPLUSPLUS_WIN32_SPLASHWINDOW_H
#define DCPLUSPLUS_WIN32_SPLASHWINDOW_H

#include <dcpp/typedefs.h>

#include <dwt/widgets/Window.h>
#include <mutex>

class SplashWindow : public dwt::Window  {
public:
	SplashWindow();
	virtual ~SplashWindow();

	void operator()(const string& status);
	void operator()(float progress);
	// Called by the loader once settings are available, before its first stage.
	void settingsLoaded();
	void finish();

private:
	void queueUpdate(); // pendingMutex must be held
	void updateLayout(unsigned dpi);
	int pixels(int value) const { return ::MulDiv(value, fontDpi, 96); }
	void draw(dwt::Canvas& canvas);
	void appearanceChanged() override;

	dwt::IconPtr icon;
	dwt::FontPtr titleFont, detailFont;
	unsigned fontDpi = 0;
	ULONGLONG started;
	bool settingsReady = false, finished = false;

	tstring status;
	float progress = 0;
	std::mutex pendingMutex;
	tstring pendingStatus;
	float pendingProgress = 0;
	bool updatePending = false;
};

#endif
