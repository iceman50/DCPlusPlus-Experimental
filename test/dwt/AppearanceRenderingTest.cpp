// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/Application.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Pen.h>
#include <dwt/util/win32/NativeDialogAppearance.h>
#include <dwt/util/win32/Dwm.h>
#include <cstdio>
#include <stdexcept>

namespace {
bool simulatedHighContrast = false;
bool simulatedSystemDark = false;
}

// Exercise the production state transitions without changing Windows settings.
BOOL WINAPI appearanceSystemParametersInfo(UINT action, UINT size, PVOID value, UINT flags) {
	if(action == SPI_GETHIGHCONTRAST) {
		reinterpret_cast<HIGHCONTRAST*>(value)->dwFlags = simulatedHighContrast ? HCF_HIGHCONTRASTON : 0;
		return TRUE;
	}
	return ::SystemParametersInfo(action, size, value, flags);
}
LSTATUS WINAPI appearanceRegGetValue(HKEY key, LPCTSTR path, LPCTSTR name,
	DWORD flags, LPDWORD type, PVOID value, LPDWORD size) {
	if(name && lstrcmp(name, _T("AppsUseLightTheme")) == 0) {
		*static_cast<DWORD*>(value) = simulatedSystemDark ? 0 : 1;
		*size = sizeof(DWORD);
		return ERROR_SUCCESS;
	}
	return ::RegGetValue(key, path, name, flags, type, value, size);
}
#undef SystemParametersInfo
#define SystemParametersInfo appearanceSystemParametersInfo
#undef RegGetValue
#define RegGetValue appearanceRegGetValue
#include "../../dwt/src/Appearance.cpp"
#undef SystemParametersInfo
#undef RegGetValue

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}
void checkBrush(const dwt::BrushPtr& brush, COLORREF color) {
	LOGBRUSH value {};
	check(GetObject(brush->handle(), sizeof(value), &value) == sizeof(value) && value.lbColor == color,
		"Appearance brush does not match the active palette");
}
}

// Verify native attributes when the running OS implements them. On older
// Windows, the frame/style assertions still exercise the supported fallback.
void checkNativeColor(HWND window, DWMWINDOWATTRIBUTE attribute, COLORREF expected) {
	COLORREF actual = 0;
	if(SUCCEEDED(dwt::util::win32::dwm::getWindowAttribute(window, attribute, &actual, sizeof(actual)))) {
		check(actual == expected, "Native frame attribute did not follow appearance/reset policy");
	}
}

int dwtMain(dwt::Application& app) {
	using Mode = dwt::Appearance::Mode;
	using Rendering = dwt::Appearance::Rendering;
	try {
		auto& appearance = app.getAppearance();
		const dwt::Appearance::Palette palette {
			RGB(245, 245, 245), RGB(255, 255, 255), RGB(25, 25, 25),
			RGB(110, 110, 110), RGB(175, 175, 175), RGB(0, 90, 180), RGB(255, 255, 255)
		};
		appearance.configure(Mode::Light, palette);
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed(L"Appearance rendering transitions");
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		auto group = window->addChild(dwt::GroupBox::Seed(L"Palette group"));
		for(unsigned i = 0; i < 1000 && app.dispatch(); ++i) { }
		unsigned notifications = 0;
		auto callback = appearance.onChanged([&] { ++notifications; });
		check(!appearance.isManual() && !window->usesClientFrame(), "Legacy light mode changed");
		const auto generation = appearance.getGeneration();
		appearance.configure(Mode::Light, palette, Rendering::CustomPalette);
		check(!appearance.isDark() && appearance.isManual(), "Custom light rendering still depends on dark mode");
		check(appearance.getGeneration() == generation + 1 && notifications == 1,
			"Rendering-only change did not notify exactly once");
		check(window->usesClientFrame() && !(GetWindowLongPtr(group->handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT),
			"Rendering-only change did not update existing widgets");
		checkBrush(appearance.getBackgroundBrush(), palette.background);
		checkBrush(appearance.getSurfaceBrush(), palette.surface);
		{
			dwt::util::win32::NativeDialogAppearance native(appearance);
			check(native.active(), "Custom light palette did not enable native-dialog adaptation");
		}
		window->setAppearancePolicy(dwt::AppearancePolicy::Native);
		check(!window->usesClientFrame(), "Native widget override lost precedence");
		window->setAppearancePolicy(dwt::AppearancePolicy::Inherit);
		check(window->usesClientFrame(), "Restoring inheritance did not restore custom framing");

		for(auto mode : { Mode::Light, Mode::Dark, Mode::System }) {
			for(bool systemDark : { false, true }) {
				simulatedSystemDark = systemDark;
				appearance.configure(mode, palette);
				const bool expectedDark = mode == Mode::Dark || (mode == Mode::System && systemDark);
				check(appearance.getRendering() == Rendering::Automatic && appearance.isDark() == expectedDark &&
					appearance.isManual() == expectedDark && window->usesClientFrame() == expectedDark,
					"Existing mode selection or automatic-rendering reset changed");
				appearance.configure(mode, palette, Rendering::CustomPalette);
				check(appearance.isManual() && appearance.isDark() == expectedDark && window->usesClientFrame(),
					"Custom rendering changed the light/dark preference");

				simulatedHighContrast = true;
				check(appearance.refreshSystemState(), "High-contrast transition was not reported");
				check(appearance.isHighContrast() && !appearance.isManual() && !appearance.isDark() &&
					!window->usesClientFrame(), "High contrast did not restore native rendering");
				check(appearance.getRendering() == Rendering::CustomPalette, "High contrast discarded the rendering preference");
				checkBrush(appearance.getBackgroundBrush(), GetSysColor(COLOR_BTNFACE));
				check(appearance.getContentText() == GetSysColor(COLOR_WINDOWTEXT), "High contrast lost system text colors");
				{
					dwt::util::win32::NativeDialogAppearance native(appearance);
					check(!native.active(), "High contrast enabled native-dialog adaptation");
				}
				simulatedHighContrast = false;
				check(appearance.refreshSystemState() && appearance.isManual() && window->usesClientFrame(),
					"Leaving high contrast did not restore custom rendering");
				checkBrush(appearance.getBackgroundBrush(), palette.background);
			}
		}
		appearance.configure(Mode::System, palette, Rendering::CustomPalette);
		simulatedSystemDark = !simulatedSystemDark;
		check(appearance.refreshSystemState() && appearance.isDark() == simulatedSystemDark && appearance.isManual(),
			"System preference refresh disabled custom painting");
		window->setFrameRendering(dwt::Frame::FrameRendering::Native);
		check(!window->usesClientFrame() && !(GetWindowLongPtr(group->handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT), "Native frame disabled the control palette");
		namespace dwm = dwt::util::win32::dwm;
		checkNativeColor(window->handle(), dwm::captionColorAttribute, palette.surface);
		checkNativeColor(window->handle(), dwm::textColorAttribute, palette.text);
		checkNativeColor(window->handle(), dwm::borderColorAttribute, palette.border);
		simulatedHighContrast = true;
		appearance.refreshSystemState();
		checkNativeColor(window->handle(), dwm::captionColorAttribute, dwm::defaultColor);
		checkNativeColor(window->handle(), dwm::textColorAttribute, dwm::defaultColor);
		checkNativeColor(window->handle(), dwm::borderColorAttribute, dwm::defaultColor);
		simulatedHighContrast = false;
		appearance.refreshSystemState();
		checkNativeColor(window->handle(), dwm::captionColorAttribute, palette.surface);
		window->setAppearancePolicy(dwt::AppearancePolicy::Native);
		checkNativeColor(window->handle(), dwm::captionColorAttribute, dwm::defaultColor);
		window->setAppearancePolicy(dwt::AppearancePolicy::Inherit);
		checkNativeColor(window->handle(), dwm::captionColorAttribute, palette.surface);
		window->setFrameRendering(dwt::Frame::FrameRendering::Automatic);
		check(window->usesClientFrame(), "Automatic framing did not restore the custom caption");
		checkNativeColor(window->handle(), dwm::captionColorAttribute, dwm::defaultColor);
		// The choice must also work before HWND creation and survive theme updates.
		auto nativeWindow = new dwt::Window();
		nativeWindow->setFrameRendering(dwt::Frame::FrameRendering::Native);
		nativeWindow->create(seed);
		check(!nativeWindow->usesClientFrame() && nativeWindow->hasStyle(WS_CAPTION), "Precreation frame choice was ignored");
		checkNativeColor(nativeWindow->handle(), dwm::captionColorAttribute, palette.surface);
		DestroyWindow(nativeWindow->handle());
		appearance.configure(Mode::Light, palette);
		check(!window->usesClientFrame() && (GetWindowLongPtr(group->handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT),
			"Automatic light mode did not restore native frame/control handling");
		appearance.removeChanged(callback);
		DestroyWindow(window->handle());
		std::puts("PASS: custom light palettes, existing modes, live transitions, native overrides and high-contrast restoration");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
