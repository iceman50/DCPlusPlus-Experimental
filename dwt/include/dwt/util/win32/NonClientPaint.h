/* Copyright (c) 2026 iceman50. Distributed under the DWT BSD license. */
#ifndef DWT_NON_CLIENT_PAINT_H
#define DWT_NON_CLIENT_PAINT_H

#include <dwt/WindowsHeaders.h>

namespace dwt { namespace util { namespace win32 {

// RedrawWindow regions use client coordinates. Exclude the entire client area
// so a title/icon/activation change cannot invalidate the dialog and its children.
inline void invalidateNonClient(HWND window) {
	RECT outer, client;
	if(!::GetWindowRect(window, &outer) || !::GetClientRect(window, &client)) return;
	::MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&outer), 2);
	auto frame = ::CreateRectRgnIndirect(&outer);
	auto content = ::CreateRectRgnIndirect(&client);
	if(frame && content) {
		::CombineRgn(frame, frame, content, RGN_DIFF);
		::RedrawWindow(window, nullptr, frame, RDW_INVALIDATE | RDW_FRAME | RDW_NOCHILDREN);
	}
	if(content) ::DeleteObject(content);
	if(frame) ::DeleteObject(frame);
}

// Let USER32 store titles/icons, without displaying an intermediate native
// caption. Preserve hidden windows and an enclosing redraw suspension.
template<class Chain>
LRESULT updateCaptionWithoutPainting(const MSG& message, Chain chain) {
	const bool visible = (::GetWindowLongPtr(message.hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
	// Unlike WM_SETREDRAW, temporarily masking visibility leaves pending client
	// paints intact. No ShowWindow/SetWindowPos call changes the displayed window.
	if(visible) ::SetWindowLongPtr(message.hwnd, GWL_STYLE,
		::GetWindowLongPtr(message.hwnd, GWL_STYLE) & ~WS_VISIBLE);
	const auto result = chain(message);
	if(visible) {
		::SetWindowLongPtr(message.hwnd, GWL_STYLE,
			::GetWindowLongPtr(message.hwnd, GWL_STYLE) | WS_VISIBLE);
	}
	return result;
}

} } }
#endif
