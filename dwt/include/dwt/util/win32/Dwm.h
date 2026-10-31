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

#ifndef DWT_UTIL_WIN32_DWM_H
#define DWT_UTIL_WIN32_DWM_H

#include "../../WindowsHeaders.h"
#include "../../dwt_dwmapi.h"

namespace dwt { namespace util { namespace win32 { namespace dwm {

/**
 * Shared DWM entry points. The library and exports are resolved once on demand.
 * Return E_NOTIMPL if an export is unavailable; otherwise preserve DWM's HRESULT
 * (including failures when composition is disabled or an attribute unsupported).
 * Callers remain responsible for rendering policy and fallbacks.
 */
// Documented Windows 11 attributes, also available with older SDK headers.
// https://learn.microsoft.com/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
constexpr auto darkModeAttribute = static_cast<DWMWINDOWATTRIBUTE>(20);
constexpr auto borderColorAttribute = static_cast<DWMWINDOWATTRIBUTE>(34);
constexpr auto captionColorAttribute = static_cast<DWMWINDOWATTRIBUTE>(35);
constexpr auto textColorAttribute = static_cast<DWMWINDOWATTRIBUTE>(36);
constexpr COLORREF defaultColor = 0xffffffff;

HRESULT getWindowAttribute(HWND window, DWMWINDOWATTRIBUTE attribute,
	PVOID value, DWORD size);
HRESULT setWindowAttribute(HWND window, DWMWINDOWATTRIBUTE attribute,
	LPCVOID value, DWORD size);
/** Optional native-frame styling. Unsupported attributes return a failure.
 * defaultColor restores Windows' choice; dark mode remains a native preference.
 * These calls do not style client controls or implement high-contrast policy. */
HRESULT setDarkMode(HWND window, bool enabled);
HRESULT setCaptionColor(HWND window, COLORREF color = defaultColor);
HRESULT setTextColor(HWND window, COLORREF color = defaultColor);
HRESULT setBorderColor(HWND window, COLORREF color = defaultColor);
/** Reset all four styling attributes, even if some fail; return the first failure. */
HRESULT resetFrameAppearance(HWND window);

HRESULT setNonClientRendering(HWND window, DWMNCRENDERINGPOLICY policy);
HRESULT invalidateIconicBitmaps(HWND window);
HRESULT setIconicThumbnail(HWND window, HBITMAP bitmap, DWORD flags = 0);
HRESULT setIconicLivePreviewBitmap(HWND window, HBITMAP bitmap,
	POINT* offset, DWORD flags = 0);

} } } }

#endif
