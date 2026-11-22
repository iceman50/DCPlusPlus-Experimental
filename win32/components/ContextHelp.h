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

#ifndef DCPLUSPLUS_WIN32_CONTEXT_HELP_H
#define DCPLUSPLUS_WIN32_CONTEXT_HELP_H

#include <win32/forward.h>

/**
 * Shows the context help of controls (from their help IDs), as the Settings dialog does:
 * - in a help box, for the control with the focus or a table's selected row;
 * - as a rich tooltip when the mouse rests on a control.
 */
class ContextHelp {
public:
	/** `owner` holds the tooltip; `box` shows the help text. */
	ContextHelp(dwt::Control* owner, RichTextBoxPtr box);

	/** Watch every control inside `root`. Call it once per root, after its controls exist. */
	void watch(HWND root);

private:
	static BOOL CALLBACK watchChild(HWND hwnd, LPARAM lParam);
	void show(dwt::Control* widget);

	RichTextBoxPtr box;
	ToolTipPtr tip;
	int lastTableHelpId;
};

#endif // !defined(DCPLUSPLUS_WIN32_CONTEXT_HELP_H)
