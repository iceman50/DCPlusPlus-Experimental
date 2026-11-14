// Copyright (C) 2026 iceman50
// Distributed under the DWT license; see dwt/License.txt.
#ifndef DWT_TOUCHSCROLLING_H
#define DWT_TOUCHSCROLLING_H

#include <dwt/Events.h>

namespace dwt {
class Control;
class Table;

namespace detail {

// Configure the actual input HWND. Older Windows retain their native behavior.
bool configureTouchPanning(Control& control);
void enableTableTouchScrolling(Table& table);

// Per-control gesture state, separate from handle ownership in onGesture().
class TablePan {
public:

	bool handle(Table& table, const GestureEvent& event);
	void reset();

private:
	bool active = false;
	Point previous;
	long remainderY = 0;
};

}
}

#endif
