#pragma once

#include "../Application.h"
#include "../widgets/Window.h"

namespace dwt {

// Opt a modeless form into Windows dialog-style Tab and Shift+Tab traversal.
// Windows follows child z-order, WS_TABSTOP and nested WS_EX_CONTROLPARENT hosts.
inline void enableTabNavigation(Window* window) {
	auto& app = Application::instance();
	auto filter = app.addKeyboardFilter([window](MSG& msg) {
		if(msg.message != WM_KEYDOWN || msg.wParam != VK_TAB ||
			(::GetKeyState(VK_CONTROL) & 0x8000) || (::GetKeyState(VK_MENU) & 0x8000)) return false;
		const auto host = window->handle();
		if(msg.hwnd != host && !::IsChild(host, msg.hwnd)) return false;
		return ::IsDialogMessage(host, &msg) != FALSE;
	});
	window->onDestroy([&app, filter] { app.removeKeyboardFilter(filter); });
}

} // namespace dwt
