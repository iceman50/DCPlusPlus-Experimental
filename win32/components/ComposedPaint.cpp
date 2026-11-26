/*
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
#include <win32/components/ComposedPaint.h>

#include <dwt/Widget.h>
#include <dwt/widgets/Table.h>

#include <vector>

namespace {

UINT discardMessage() {
	static const UINT message = ::RegisterWindowMessage(_T("dcpp.ComposedPaint.Discard"));
	return message;
}

/** Whether a window and its parents below `top` are all shown, `top` itself showing or not. */
bool shownWithin(HWND window, HWND top) {
	for(; window && window != top; window = ::GetParent(window)) {
		if(!(::GetWindowLongPtr(window, GWL_STYLE) & WS_VISIBLE)) {
			return false;
		}
	}
	return window == top;
}

}

ComposedPaint::ComposedPaint(dwt::Widget* host_) :
	host(host_),
	alive(std::make_shared<bool>(true))
{
	host->addCallback(dwt::Message(WM_PAINT), [this](const MSG&, LRESULT&) -> bool {
		if(!composing) {
			// Only a composed paint shows snapshots; afterwards they would be out of date.
			dropSnapshots();
		} else if(!painted && ::IsWindowVisible(host->handle())) {
			painted = true;
			::PostMessage(host->handle(), discardMessage(), generation, 0);
		}
		return false;
	});

	host->addCallback(dwt::Message(discardMessage()), [this](const MSG& msg, LRESULT& result) -> bool {
		result = 0;
		if(msg.wParam != generation) {
			// Posted by a paint from before begin(): the change's own paint is still to come.
			return true;
		}
		painted = false;
		// Drawn for the composed paint, which is done.
		dropSnapshots();
		if(!composing) {
			return true;
		}
		// WM_PAINT is only retrieved once no posted message waits, so only the composed paint
		// ran since this was posted: whatever is invalid now, that paint invalidated over
		// finished contents.
		host->redrawWindow(RDW_VALIDATE | RDW_NOERASE | RDW_NOFRAME | RDW_NOINTERNALPAINT | RDW_ALLCHILDREN);
		if(!untilIdle) {
			end();
		} else if(!endQueued) {
			endQueued = true;
			queueEnd(3);
		}
		return true;
	});

	// Shown again (composed, by the host): tables that changed while hidden, resized with the
	// window, are drawn before the host is on screen.
	host->addCallback(dwt::Message(WM_SHOWWINDOW), [this](const MSG& msg, LRESULT&) -> bool {
		if(msg.wParam) {
			snapshotTables();
		}
		return false;
	});
}

ComposedPaint::~ComposedPaint() {
	dropSnapshots();
}

void ComposedPaint::begin() {
	start(true);
}

void ComposedPaint::beginForOnePaint() {
	start(false);
}

void ComposedPaint::start(bool idle) {
	untilIdle = (composing && untilIdle) || idle;
	setComposited(true);
	++generation;
	painted = false;
	// Also ends composition when nothing gets painted, as when the host is hidden or off screen.
	if(!endQueued) {
		endQueued = true;
		queueEnd(3);
	}
}

ComposedPaint::Hold::Hold(ComposedPaint& paint_, bool untilIdle) :
	paint(paint_),
	held(false)
{
	const auto window = paint.host->handle();
	if(paint.holds++ == 0 && ::IsWindowVisible(window)) {
		paint.start(untilIdle);
		// Hides the host from painting without hiding it from the screen, so nothing below
		// it is uncovered.
		::SendMessage(window, WM_SETREDRAW, FALSE, 0);
		held = true;
	}
}

ComposedPaint::Hold::~Hold() {
	--paint.holds;
	if(held) {
		const auto window = paint.host->handle();
		paint.snapshotTables();
		::SendMessage(window, WM_SETREDRAW, TRUE, 0);
		// Scrollbars are in the frames (non-client areas) of the controls.
		::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
	}
}

void ComposedPaint::end() {
	setComposited(false);
	untilIdle = false;
	dropSnapshots();
	repaintTables();
}

void ComposedPaint::setComposited(bool composited) {
	composing = composited;
	const auto style = ::GetWindowLongPtr(host->handle(), GWL_EXSTYLE);
	const auto wanted = composited ? style | WS_EX_COMPOSITED : style & ~WS_EX_COMPOSITED;
	if(wanted != style) {
		::SetWindowLongPtr(host->handle(), GWL_EXSTYLE, wanted);
	}
}

void ComposedPaint::queueEnd(int rounds) {
	std::weak_ptr<bool> guard(alive);
	host->callAsync([this, guard, rounds] {
		if(guard.expired()) {
			return;
		}
		if(rounds > 1) {
			queueEnd(rounds - 1);
			return;
		}
		endQueued = false;
		if(composing) {
			end();
		}
	});
}

void ComposedPaint::snapshotTables() {
	const auto window = host->handle();
	std::vector<dwt::Table*> tables;
	struct Search { HWND top; std::vector<dwt::Table*>& tables; } search { window, tables };
	::EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL {
		auto& search = *reinterpret_cast<Search*>(data);
		if(shownWithin(child, search.top)) {
			if(auto table = dynamic_cast<dwt::Table*>(dwt::hwnd_cast<dwt::Widget*>(child))) {
				search.tables.push_back(table);
			}
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&search));
	if(tables.empty()) {
		return;
	}

	// Meanwhile the host is not composited, so a table's repaint of itself stays its own, and
	// not visible (it is held, or not shown yet; else its visibility is masked for the while,
	// which unlike WM_SETREDRAW leaves pending paints alone), so that repaint draws nothing.
	const auto style = ::GetWindowLongPtr(window, GWL_STYLE);
	const auto exStyle = ::GetWindowLongPtr(window, GWL_EXSTYLE);
	const bool mask = ::IsWindowVisible(window);
	if(exStyle & WS_EX_COMPOSITED) {
		::SetWindowLongPtr(window, GWL_EXSTYLE, exStyle & ~WS_EX_COMPOSITED);
	}
	if(mask) {
		::SetWindowLongPtr(window, GWL_STYLE, style & ~WS_VISIBLE);
	}
	auto screen = ::GetDC(nullptr);
	auto memory = ::CreateCompatibleDC(screen);
	for(auto table: tables) {
		RECT client;
		::GetClientRect(table->handle(), &client);
		if(client.right <= 0 || client.bottom <= 0) {
			continue;
		}
		// A snapshot not painted yet still holds: what changes a table's contents (timers,
		// asynchronous calls, input) waits until the window has painted. Showing the host lays
		// it out again, for one.
		auto existing = snapshots.find(table->handle());
		if(existing != snapshots.end() && existing->second.width == client.right && existing->second.height == client.bottom) {
			continue;
		}
		auto bitmap = ::CreateCompatibleBitmap(screen, client.right, client.bottom);
		if(!bitmap) {
			continue;
		}
		auto previous = ::SelectObject(memory, bitmap);
		::SendMessage(table->handle(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_ERASEBKGND);
		::SelectObject(memory, previous);

		dropSnapshot(table->handle());
		snapshots[table->handle()] = { bitmap, client.right, client.bottom };
		if(hooked.insert(table->handle()).second) {
			std::weak_ptr<bool> guard(alive);
			table->addCallback(dwt::Message(WM_PAINT), [this, guard](const MSG& msg, LRESULT& result) -> bool {
				if(guard.expired()) {
					return false;
				}
				if(paintSnapshot(msg.hwnd, result)) {
					return true;
				}
				// Painted by itself while composed: blank, to be painted again.
				if(composing) {
					fromSnapshot.erase(msg.hwnd);
				}
				return false;
			});
			table->addCallback(dwt::Message(WM_DESTROY), [this, guard](const MSG& msg, LRESULT&) -> bool {
				if(!guard.expired()) {
					dropSnapshot(msg.hwnd);
					hooked.erase(msg.hwnd);
					fromSnapshot.erase(msg.hwnd);
				}
				return false;
			});
		}
	}
	::DeleteDC(memory);
	::ReleaseDC(nullptr, screen);
	if(mask) {
		::SetWindowLongPtr(window, GWL_STYLE, style);
		// What a table validated while hidden is painted again.
		for(auto table: tables) {
			::RedrawWindow(table->handle(), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
		}
	}
	if(exStyle & WS_EX_COMPOSITED) {
		::SetWindowLongPtr(window, GWL_EXSTYLE, exStyle);
	}
}

bool ComposedPaint::paintSnapshot(HWND table, LRESULT& result) {
	auto i = snapshots.find(table);
	if(i == snapshots.end()) {
		return false;
	}
	const auto snapshot = i->second;
	snapshots.erase(i);
	RECT client;
	::GetClientRect(table, &client);
	if(!composing || client.right != snapshot.width || client.bottom != snapshot.height) {
		// Changed since: the table paints itself.
		::DeleteObject(snapshot.bitmap);
		return false;
	}
	PAINTSTRUCT paint;
	auto dc = ::BeginPaint(table, &paint);
	auto memory = ::CreateCompatibleDC(dc);
	auto previous = ::SelectObject(memory, snapshot.bitmap);
	::BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right - paint.rcPaint.left,
		paint.rcPaint.bottom - paint.rcPaint.top, memory, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
	::SelectObject(memory, previous);
	::DeleteDC(memory);
	::EndPaint(table, &paint);
	::DeleteObject(snapshot.bitmap);
	fromSnapshot.insert(table);
	result = 0;
	return true;
}

void ComposedPaint::dropSnapshot(HWND table) {
	auto i = snapshots.find(table);
	if(i != snapshots.end()) {
		::DeleteObject(i->second.bitmap);
		snapshots.erase(i);
	}
}

void ComposedPaint::dropSnapshots() {
	for(auto& snapshot: snapshots) {
		::DeleteObject(snapshot.second.bitmap);
	}
	snapshots.clear();
}

void ComposedPaint::repaintTables() {
	if(::IsWindowVisible(host->handle())) {
		::EnumChildWindows(host->handle(), [](HWND window, LPARAM data) -> BOOL {
			const auto& skip = *reinterpret_cast<const std::set<HWND>*>(data);
			if(::IsWindowVisible(window) && !skip.count(window) && dynamic_cast<dwt::Table*>(dwt::hwnd_cast<dwt::Widget*>(window))) {
				// At once, so the rows follow the composed paint as closely as they can.
				::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&fromSnapshot));
	}
	fromSnapshot.clear();
}
