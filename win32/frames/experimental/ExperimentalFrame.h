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

#ifndef DCPLUSPLUS_WIN32_EXPERIMENTAL_FRAME_H
#define DCPLUSPLUS_WIN32_EXPERIMENTAL_FRAME_H

#include <memory>
#include <vector>

#include <win32/frames/StaticFrame.h>
#include <win32/settings/base/LiveSettings.h>

class ComposedPaint;
class ContextHelp;
class ExperimentalTab;

/** The Experimental window: the experimental options, on tabs, outside the Settings dialog.
 * Every change applies at once (see LiveSettings) and is saved shortly after. */
class ExperimentalFrame :
	public StaticFrame<ExperimentalFrame>
{
	typedef StaticFrame<ExperimentalFrame> BaseType;

public:
	enum Status {
		STATUS_STATUS,
		STATUS_LAST
	};

	static const string id;
	const string& getId() const;

	/** The tabs, in order. */
	enum Page {
		PAGE_THEMES,
		PAGE_ICONS,
		PAGE_EMOTICONS,
		PAGE_CHAT,
		PAGE_TRANSFERS,
		PAGE_PRIVATE_SEND,
		PAGE_PROTOCOL,
		PAGE_OTHER,
		PAGE_LAST
	};

	/** Open the window, or bring it to the front, on this tab. */
	static void openPage(TabViewPtr parent, Page page);

	/** Show settings that something else (Settings, About:config) changed, if the window is open. */
	static void settingsChanged();

	/** How long after the last change the settings are written to disk. */
	static constexpr unsigned savePause = 1500;

protected:
	friend class StaticFrame<ExperimentalFrame>;
	friend class MDIChildFrame<ExperimentalFrame>;

	ExperimentalFrame(TabViewPtr parent);
	virtual ~ExperimentalFrame();

	void layout();

	bool preClosing();
	void postClosing();

private:
	/** What else must happen after a setting changed, beyond storing it. */
	enum Effect {
		EFFECT_CHAT = 1 << 0, ///< tell hubs about changed features, and refresh chat windows
		EFFECT_PRIVATE_SEND = 1 << 1,
		EFFECT_OUTBOX = 1 << 2,
		EFFECT_USER_ICONS = 1 << 3,
		EFFECT_GEOIP = 1 << 4,
		EFFECT_ICONS = 1 << 5,
		EFFECT_EMOTICONS = 1 << 6,
		EFFECT_OUTBOX_SWITCH = 1 << 7
	};

	enum { TIMER_SAVE = 1, TIMER_PREBUILD };

	/** How long after opening the other tabs start being built in the background. */
	static constexpr unsigned prebuildDelay = 150;

	LiveSettings live;
	/** Paints a tab switch, a resize or the window being shown in one step. */
	std::unique_ptr<ComposedPaint> composed;
	/** The client size the window was last laid out at. */
	dwt::Point laidOut;
	GridPtr grid;
	TabViewPtr tabs;
	RichTextBoxPtr help;
	std::unique_ptr<ContextHelp> contextHelp;
	std::vector<ExperimentalTab*> pages;
	bool savePending;
	/** The font the tabs were measured with. */
	HFONT lastFont;

	void changed(int setting);
	static void apply(unsigned effects);
	void save();
	void refresh();
	void layoutActivePage();
	/** Build the next tab not built yet, then the one after, each when the window is idle. */
	void prebuildNext();
	/** Whether the application font changed since this was last asked. */
	bool fontsChanged();
};

#endif // !defined(DCPLUSPLUS_WIN32_EXPERIMENTAL_FRAME_H)
