/*
  DC++ Widget Toolkit

  Copyright (c) 2026 iceman50

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DWT_UTIL_WIN32_DARKMODE_H
#define DWT_UTIL_WIN32_DARKMODE_H

#include "../../WindowsHeaders.h"

namespace dwt { namespace util { namespace win32 { namespace darkmode {

/** Windows' own dark rendering ("native dark mode"), for the parts it can draw dark: the caption,
 * menus, scroll bars and the controls that have a DarkMode_* visual style.
 *
 * Windows has no public API for this. It uses functions uxtheme.dll exports by ordinal only:
 * 104 RefreshImmersiveColorPolicyState, 133 AllowDarkModeForWindow, 135 SetPreferredAppMode and
 * 136 FlushMenuThemes. Their meaning is stable from Windows 10 1903 on; before that ordinal 135 is
 * AllowDarkModeForApp, which takes a BOOL, and before 1809 none of them exist. So everything here
 * does nothing before 1903, checked by version before an ordinal is looked up. */

/** Whether native dark mode is available: Windows 10 1903 or later with every ordinal present. */
bool isSupported();

/** Whether Windows has the DarkMode_DarkTheme visual style (Windows 11), the only one that draws
 * check box, radio button and group box text light. Without it those keep DWT's painting. */
bool hasDarkTheme();

/** Have Windows draw what it draws for the whole application (menus, the window menu, shell menu
 * items) dark, or as usual. Returns false where native dark mode is unavailable. */
bool setApplicationDark(bool dark);

/** Let Windows draw one window dark (its scroll bars and dark visual styles), or not. */
void allowForWindow(HWND window, bool allow);

/** Ask DWM for a dark caption and frame, or the usual ones (dwm::setDarkMode). */
HRESULT setDarkCaption(HWND window, bool dark);

/** The DarkMode_* visual styles a control can be given with SetWindowTheme. */
enum class Theme {
	None,
	/** Scroll bars, push buttons, list views, tree views, tooltips. */
	Explorer,
	/** Edit and combo boxes (from the common file dialog). */
	Cfd,
	/** List view headers. */
	ItemsView,
	/** Windows 11: check boxes, radio buttons and group boxes with light text. */
	DarkTheme
};
LPCWSTR themeName(Theme theme);

/* Menu bar painting messages ("UAH", user api hook), undocumented. With a visual style, Windows
 * sends the menu bar's window WM_UAHDRAWMENU to paint the bar's background and
 * WM_UAHDRAWMENUITEM for each item that isn't owner-drawn; a window that handles them (and
 * doesn't pass them on) draws the bar itself. The layouts below are those uxtheme uses. */
constexpr UINT WM_UAHDRAWMENU = 0x0091;
constexpr UINT WM_UAHDRAWMENUITEM = 0x0092;

struct UAHMENU {
	HMENU hmenu;
	HDC hdc;
	DWORD dwFlags;
};

union UAHMENUITEMMETRICS {
	struct { DWORD cx; DWORD cy; } rgsizeBar[2];
	struct { DWORD cx; DWORD cy; } rgsizePopup[4];
};

struct UAHMENUPOPUPMETRICS {
	DWORD rgcx[4];
	DWORD fUpdateMaxWidths : 2;
};

struct UAHMENUITEM {
	int iPosition;
	UAHMENUITEMMETRICS umim;
	UAHMENUPOPUPMETRICS umpm;
};

struct UAHDRAWMENUITEM {
	DRAWITEMSTRUCT dis;
	UAHMENU um;
	UAHMENUITEM umi;
};

} } } }

#endif
