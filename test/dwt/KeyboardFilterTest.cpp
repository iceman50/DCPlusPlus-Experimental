// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// Keyboard filters must see keys before any control's accelerators: the client's lock relies on
// them so that a key pressed while locked never runs a shortcut of a window behind the lock panel.
#include <dwt/Application.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/TextBox.h>
#include <cstdio>
#include <stdexcept>

namespace {

void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void pump() {
	for(unsigned i = 0; i < 1000 && dwt::Application::instance().dispatch(); ++i) { }
}

/// Post Ctrl+S to the window as the keyboard would, with Ctrl held for TranslateAccelerator.
void pressCtrlS(HWND target) {
	BYTE keys[256] = { };
	::GetKeyboardState(keys);
	keys[VK_CONTROL] = 0x80;
	::SetKeyboardState(keys);
	::PostMessage(target, WM_KEYDOWN, 'S', 1);
	pump();
	keys[VK_CONTROL] = 0;
	::SetKeyboardState(keys);
	pump();
}

}

int dwtMain(dwt::Application& app) {
	try {
		auto window = new dwt::Window;
		int shortcuts = 0;
		window->addAccel(FCONTROL, 'S', [&] { ++shortcuts; });
		dwt::Window::Seed seed;
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		auto box = window->addChild(dwt::TextBox::Seed());

		// Without keyboard filters, the main window's shortcut runs for a key in its child.
		pressCtrlS(box->handle());
		check(shortcuts == 1, "The test shortcut did not run; the harness is broken");

		int keyboardSeen = 0, laterSeen = 0;
		bool drop = true;
		auto later = app.addFilter([&](MSG& msg) {
			if(msg.message == WM_KEYDOWN && msg.hwnd == box->handle()) ++laterSeen;
			return false;
		});
		auto gate = app.addKeyboardFilter([&](MSG& msg) {
			check(msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST, "A keyboard filter saw a non-keyboard message");
			if(msg.hwnd != box->handle()) return false;
			if(msg.message == WM_KEYDOWN) ++keyboardSeen;
			return drop;
		});

		// A keyboard filter that takes the key stops the shortcut and the ordinary filters.
		pressCtrlS(box->handle());
		check(keyboardSeen == 1, "The keyboard filter did not see the key");
		check(shortcuts == 1, "A shortcut ran although a keyboard filter took the key first");
		check(laterSeen == 0, "An ordinary filter saw a key a keyboard filter had taken");

		// Messages other than keys don't reach keyboard filters.
		::PostMessage(box->handle(), WM_MOUSEMOVE, 0, 0);
		::PostMessage(box->handle(), WM_NULL, 0, 0);
		pump();

		// A keyboard filter that lets the key through changes nothing.
		drop = false;
		pressCtrlS(box->handle());
		check(keyboardSeen == 2, "The keyboard filter missed a key");
		check(shortcuts == 2, "A key a keyboard filter let through did not run the shortcut");

		// A filter can redirect a key, as the lock does to its password box.
		drop = true;
		app.removeKeyboardFilter(gate);
		auto other = window->addChild(dwt::TextBox::Seed());
		int redirected = 0;
		gate = app.addKeyboardFilter([&](MSG& msg) {
			if(msg.hwnd != box->handle()) return false;
			msg.hwnd = other->handle();
			if(msg.message == WM_KEYDOWN) ++redirected;
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			return true;
		});
		::PostMessage(box->handle(), WM_KEYDOWN, 'A', 1);
		pump();
		check(redirected == 1 && box->getText().empty() && !other->getText().empty(),
			"A redirected key did not reach the other text box");

		// Removing the keyboard filter restores the shortcut.
		app.removeKeyboardFilter(gate);
		pressCtrlS(box->handle());
		check(shortcuts == 3, "The shortcut did not run after its keyboard filter was removed");

		app.removeFilter(later);
		::DestroyWindow(window->handle());
		std::printf("PASS: keyboard filters run before accelerators and ordinary filters, can drop, pass or redirect keys, and see only keyboard messages.\n");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
