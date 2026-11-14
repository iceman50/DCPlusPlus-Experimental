// Copyright (C) 2026 iceman50
// Distributed under the DWT license; see dwt/License.txt.
#include "TouchScrolling.h"

#include <dwt/util/win32/Version.h>
#include <dwt/widgets/Table.h>

#include <memory>

namespace dwt { namespace detail {

bool configureTouchPanning(Control& control) {
	if(!util::win32::ensureVersion(util::win32::EIGHT_ONE)) {
		return false;
	}
	// Keep Windows' recognition of taps, hold-to-right-click and inertia. Raw
	// WM_TOUCH registration would disable these gesture messages.
	return control.setGestureConfig({ { GID_PAN,
		GC_PAN | GC_PAN_WITH_SINGLE_FINGER_VERTICALLY |
		GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY | GC_PAN_WITH_GUTTER |
		GC_PAN_WITH_INERTIA, 0 } });
}

void TablePan::reset() {
	active = false;
	remainderY = 0;
}

bool TablePan::handle(Table& table, const GestureEvent& event) {
	if(!event.valid || event.type == GestureEvent::Begin || event.type == GestureEvent::End) {
		reset();
		// Windows needs the generic begin/end messages for legacy input handling.
		return false;
	}
	if(event.type != GestureEvent::Pan) {
		return false;
	}
	if(event.flags & GF_BEGIN) {
		reset();
		active = true;
		previous = event.pos.getPoint();
	} else if(active) {
		const auto delta = previous - event.pos.getPoint();
		previous = event.pos.getPoint();
		remainderY += delta.y;

		// Report views round LVM_SCROLL vertically to whole rows. Keep the
		// fraction ourselves so slow swipes do not disappear or round up early.
		RECT row = {};
		const auto top = ListView_GetTopIndex(table.handle());
		long scrollY = 0;
		if(ListView_GetItemRect(table.handle(), top < 0 ? 0 : top, &row, LVIR_BOUNDS) &&
			row.bottom > row.top) {
			const auto height = row.bottom - row.top;
			scrollY = remainderY / height * height;
			remainderY -= scrollY;
		} else {
			remainderY = 0;
		}
		if(delta.x || scrollY) {
			table.scroll(static_cast<int>(delta.x), static_cast<int>(scrollY));
		}

		// Discard outward motion at the boundary, including fractional rows.
		// Reversing direction must respond immediately, even after a long fling.
		SCROLLINFO scroll = { sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS };
		if(!::GetScrollInfo(table.handle(), SB_VERT, &scroll) ||
			(remainderY < 0 && scroll.nPos <= scroll.nMin) ||
			(remainderY > 0 && scroll.nPos >= scroll.nMax - static_cast<int>(scroll.nPage) + 1)) {
			remainderY = 0;
		}
	}
	if(event.flags & GF_END) {
		reset();
	}
	return true;
}

void enableTableTouchScrolling(Table& table) {
	// Other list-view layouts have different scroll units.
	if((::GetWindowLongPtr(table.handle(), GWL_STYLE) & LVS_TYPEMASK) != LVS_REPORT ||
		!configureTouchPanning(table)) {
		return;
	}
	auto pan = std::make_shared<TablePan>();
	table.onGesture([&table, pan](const GestureEvent& event) { return pan->handle(table, event); });
	for(auto message: { WM_CANCELMODE, WM_KILLFOCUS, WM_SIZE, WM_MOUSEWHEEL, WM_MOUSEHWHEEL }) {
		table.addCallback(Message(message), [pan](const MSG&, LRESULT&) {
			pan->reset();
			return false;
		});
	}
	table.addCallback(Message(WM_SHOWWINDOW), [pan](const MSG& msg, LRESULT&) {
		if(!msg.wParam) pan->reset();
		return false;
	});
}

} }
