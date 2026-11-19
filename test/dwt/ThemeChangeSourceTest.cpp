// Copyright (C) 2026 iceman50
// Theming one window (a tooltip, for example) sends it WM_THEMECHANGED. That must not
// be mistaken for a system theme change, which refreshes and repaints every widget;
// a real theme change must still do so.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/ToolTip.h>
#include <dwt/util/win32/UxTheme.h>

#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}
}

int dwtMain(dwt::Application& app) {
	auto window = new dwt::Window();
	window->create(dwt::Window::Seed());
	int result = 0;
	try {
		auto box = window->addChild(dwt::TextBox::Seed());
		for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			while(app.dispatch()) { }
			int refreshes = 0;
			auto counter = app.getAppearance().onChanged([&refreshes] { ++refreshes; });

			auto tip = window->addChild(dwt::ToolTip::Seed());
			tip->addTool(box, const_cast<LPTSTR>(L"tip"));
			while(app.dispatch()) { }
			check(refreshes == 0, "Creating a tooltip refreshed the whole application");

			dwt::util::win32::uxtheme::setWindowTheme(window->handle(), L"", L"");
			dwt::util::win32::uxtheme::setWindowTheme(window->handle(), nullptr, nullptr);
			while(app.dispatch()) { }
			check(refreshes == 0, "Theming a top-level window refreshed the whole application");

			::SendMessage(window->handle(), WM_THEMECHANGED, 0, 0);
			while(app.dispatch()) { }
			check(refreshes == 1, "A system theme change no longer refreshes the application");

			app.getAppearance().removeChanged(counter);
			::DestroyWindow(tip->handle());
		}
		std::puts("PASS: theming a single window is not mistaken for a system theme change");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
