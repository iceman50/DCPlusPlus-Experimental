// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"
#include <win32/SplashWindow.h>
#include <win32/SingleInstance.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/core/io/File.h>
#include <dwt/Appearance.h>
#include <dwt/resources/DibBitmap.h>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 186;

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}
void settle(dwt::Application& app) {
	unsigned count = 0;
	while(app.dispatch()) check(++count < 1000, "Splash updates flooded the UI queue");
}
struct Snapshot {
	dwt::UpdateCanvas target;
	dwt::CompatibleCanvas canvas;
	dwt::DibBitmap bitmap;
	dwt::Point size;
	Snapshot(SplashWindow* splash) : target(splash), canvas(target.handle()),
		bitmap(target.handle(), splash->getClientSize(), dwt::DibBitmap::RowOrder::TopDown), size(splash->getClientSize()) {
		check(bitmap.handle() != nullptr, "Cannot allocate splash snapshot");
		auto select = canvas.select(bitmap);
		splash->sendMessage(WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.handle()), PRF_CLIENT);
		::GdiFlush();
	}
	COLORREF pixel(int x, int y) const {
		const auto& p = static_cast<const RGBQUAD*>(bitmap.pixels())[y * size.x + x];
		return RGB(p.rgbRed, p.rgbGreen, p.rgbBlue);
	}
	int pixels(int value) const { return ::MulDiv(value, size.x, 440); }
	void checkProgress(float expected) const {
		const long left = pixels(32), width = size.x - pixels(64);
		const auto filled = static_cast<int>(width * expected);
		for(int x = 0; x < width; ++x) {
			check((pixel(left + x, pixels(273)) == RGB(22, 136, 240)) == (x < filled),
				"Progress lost its completed prefix, moved backwards, or left a gap");
		}
	}
	void save(const string& path) {
		BITMAPINFOHEADER info {};
		info.biSize = sizeof(info);
		info.biWidth = size.x;
		info.biHeight = -size.y;
		info.biPlanes = 1;
		info.biBitCount = 32;
		BITMAPFILEHEADER header {};
		header.bfType = 0x4D42;
		header.bfOffBits = sizeof(header) + sizeof(info);
		header.bfSize = header.bfOffBits + size.x * size.y * 4;
		File file(path, File::WRITE, File::CREATE | File::TRUNCATE);
		file.write(&header, sizeof(header));
		file.write(&info, sizeof(info));
		file.write(bitmap.pixels(), size.x * size.y * 4);
	}
};
}

int dwtMain(dwt::Application& app) {
	try {
		Util::PathsMap paths;
		for(int i = 0; i < Util::PATH_LAST; ++i) paths[static_cast<Util::Paths>(i)] = "build/splash-validation/profile/";
		Util::initialize(paths);
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		SettingsManager::getInstance()->set(SettingsManager::THEME_MODE, SettingsManager::THEME_DARK);
		// Keep the test message loop alive when a splash closes.
		auto anchor = new dwt::Window;
		dwt::Window::Seed seed;
		seed.style = WS_POPUP;
		anchor->create(seed);
		auto fast = new SplashWindow;
		const auto fastHandle = fast->handle();
		check(!fast->getVisible(), "Splash flashed during fast startup");
		fast->settingsLoaded();
		(*fast)("Fast startup");
		settle(app);
		check(!fast->getVisible(), "Splash appeared before the delay");
		fast->finish();
		check(!::IsWindow(fastHandle), "Finishing startup did not immediately close the splash");

		auto splash = new SplashWindow;
		splash->settingsLoaded();
		std::thread worker([splash] {
			(*splash)("Download Queue");
			for(int i = 0; i < 10000; ++i) (*splash)(i / 20000.0f);
			(*splash)(0.5f);
		});
		worker.join();
		settle(app);
		check(app.getAppearance().isDark(), "Saved startup theme was not applied");
		check(splash->getAccessibleHelpText().find(L"Download Queue") != tstring::npos, "Startup status was lost");
		::Sleep(380);
		settle(app);
		check(splash->getVisible(), "Slow startup did not show its splash");
		splash->close();
		check(::IsWindow(splash->handle()), "User close destroyed a splash still in use by the loader");

		const auto palette = dwt::Appearance::defaultPalette();
		for(const auto dpi : { 96U, 144U, 192U }) {
			RECT suggested = splash->getWindowRect();
			splash->sendMessage(WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
			check(splash->getClientSize() == dwt::Point(::MulDiv(440, dpi, 96), ::MulDiv(300, dpi, 96)), "Splash did not scale with DPI");
			for(const auto mode : { dwt::Appearance::Mode::Dark, dwt::Appearance::Mode::Light }) {
				app.getAppearance().configure(mode, palette);
				Snapshot shot(splash);
				check(shot.pixel(shot.pixels(20), shot.pixels(20)) ==
					(mode == dwt::Appearance::Mode::Dark ? palette.background : ::GetSysColor(COLOR_WINDOW)), "Wrong splash background");
				shot.checkProgress(0.5f);
				shot.save("build/splash-validation/" + string(mode == dwt::Appearance::Mode::Dark ? "dark-" : "light-") + std::to_string(dpi) + ".bmp");
			}
		}
		const auto before = splash->getWindowRect();
		(*splash)(string(400, 'W'));
		settle(app);
		check(splash->getWindowRect().size == before.size, "Long stage text resized the splash");
		Snapshot(splash).checkProgress(0.5f);
		// Stage changes, invalid values, and time spent waiting must never erase
		// the blue prefix. This used to switch to a moving quarter-width segment.
		for(const auto value : { 0.0f, -2.0f, 0.25f, std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity() }) {
			(*splash)(value);
			settle(app);
			Snapshot(splash).checkProgress(0.5f);
		}
		for(const auto value : { 0.65f, 0.75f, 0.9f }) {
			(*splash)(value);
			settle(app);
			Snapshot(splash).checkProgress(value);
			(*splash)("Next stage without a progress estimate");
			::Sleep(100);
			settle(app);
			Snapshot(splash).checkProgress(value);
		}
		(*splash)(2.0f);
		settle(app);
		Snapshot(splash).checkProgress(1.0f);
		(*splash)(0.0f);
		settle(app);
		Snapshot(splash).checkProgress(1.0f);
		splash->finish();
		anchor->close();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("SplashWindowTest passed: startup delay, saved theme, worker updates, progress, close safety, light/dark and 96/144/192 DPI");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "SplashWindowTest failed: %s\n", e.what());
		return 1;
	}
}
