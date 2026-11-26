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

#ifndef DCPLUSPLUS_WIN32_COMPOSED_PAINT_H
#define DCPLUSPLUS_WIN32_COMPOSED_PAINT_H

#include <map>
#include <memory>
#include <set>

namespace dwt { class Widget; }

/**
 * Paints a window and everything inside it in one step (composited, double buffered) while
 * its contents change, so a page switch, a resize or a first showing reaches the screen as
 * one picture instead of control by control, which shows parts of the old and new contents
 * together for several frames.
 *
 * Composition lasts only until the window is idle. Report-view list views and custom-drawn
 * scrollbars invalidate themselves while painting, which inside a composited window repaints
 * everything without end; so after each composed paint a posted message (handled before any
 * further WM_PAINT) discards what that paint invalidated, and composition ends once no
 * message and no asynchronous call is waiting. The Settings dialog's page host works the
 * same way.
 *
 * Tables need more: inside a composited window a report-view list view paints nothing (the
 * window shows with the table black) and repaints itself at once, from inside its paint, which
 * there is a second, nested composed paint of the whole window. So before a Hold ends and before
 * the host is shown, each shown table is drawn into a bitmap, where nothing can be seen, and
 * the composed paint shows that bitmap in its place; once composition ends, each table that
 * painted itself while composed (for changes no Hold covered) paints again.
 */
class ComposedPaint {
public:
	explicit ComposedPaint(dwt::Widget* host);
	~ComposedPaint();
	ComposedPaint(const ComposedPaint&) = delete;
	ComposedPaint& operator=(const ComposedPaint&) = delete;

	/** Compose the host's painting until it is idle again. Call it before the change. */
	void begin();
	/** Compose only the host's next paint, for changes that come in a series: while the window's
	 * border is dragged, Windows' sizing loop runs no asynchronous call, so composition would
	 * last the whole drag. A begin() already under way still lasts until idle. */
	void beginForOnePaint();

	/**
	 * Changes a shown host in one composed step: while it lives, nothing in the host paints;
	 * afterwards the host is painted again, composed (until idle, or for one paint), its tables
	 * ready. Without it, some controls paint themselves at once while they change (a table
	 * showing or hiding a scrollbar), which inside a composited window paints the whole window,
	 * half changed. Holds may nest; only the outermost one holds.
	 */
	class Hold {
	public:
		Hold(ComposedPaint& paint, bool untilIdle);
		~Hold();
		Hold(const Hold&) = delete;
		Hold& operator=(const Hold&) = delete;
	private:
		ComposedPaint& paint;
		bool held;
	};

private:
	dwt::Widget* host;
	/** Guards asynchronous calls, which may run after this object is gone. */
	std::shared_ptr<bool> alive;
	bool composing = false;
	/** Whether the composition under way lasts until idle rather than one paint. */
	bool untilIdle = false;
	bool painted = false;
	bool endQueued = false;
	/** How many Holds are alive. */
	int holds = 0;

	struct Snapshot {
		HBITMAP bitmap;
		LONG width;
		LONG height;
	};
	/** Tables drawn beforehand, shown in their place by the next composed paint. */
	std::map<HWND, Snapshot> snapshots;
	/** The tables that show their snapshot when they have one. */
	std::set<HWND> hooked;
	/** The tables whose last composed paint showed their snapshot: they need not paint again
	 * when composition ends. */
	std::set<HWND> fromSnapshot;
	/** Tags each posted "discard what the composed paint invalidated" message, so one posted
	 * before begin() can't validate the invalidation of the change that follows. */
	WPARAM generation = 0;

	void start(bool idle);
	void end();
	void setComposited(bool composited);
	/** End composition after `rounds` rounds of asynchronous calls: contents that fill in
	 * asynchronously (with calls that queue further calls) are composed as well. */
	void queueEnd(int rounds);
	/** Draw each table that is shown within the host, which must not be painting, for the next
	 * composed paint. */
	void snapshotTables();
	/** Show a table's snapshot as its paint, if it has one that still fits. */
	bool paintSnapshot(HWND table, LRESULT& result);
	void dropSnapshot(HWND table);
	void dropSnapshots();
	/** Paint each shown table that painted itself while composed, or might have, once more
	 * (tables buffer their own painting, so without erasing). */
	void repaintTables();
};

#endif // !defined(DCPLUSPLUS_WIN32_COMPOSED_PAINT_H)
