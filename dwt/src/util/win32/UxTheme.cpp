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

#include <dwt/util/win32/UxTheme.h>

namespace dwt { namespace util { namespace win32 { namespace uxtheme {

BOOL isAppThemed() {
	return ::IsAppThemed();
}

HTHEME openThemeData(HWND window, LPCWSTR classes) {
	return ::OpenThemeData(window, classes);
}

HRESULT closeThemeData(HTHEME theme) {
	return ::CloseThemeData(theme);
}

HRESULT setWindowTheme(HWND window, LPCWSTR application, LPCWSTR classes) {
	return ::SetWindowTheme(window, application, classes);
}

HRESULT drawThemeParentBackground(HWND window, HDC dc, RECT* rect) {
	return ::DrawThemeParentBackground(window, dc, rect);
}

HRESULT drawThemeBackground(HTHEME theme, HDC dc, int part, int state,
	const RECT* rect, const RECT* clip) {
	return ::DrawThemeBackground(theme, dc, part, state, rect, clip);
}

HRESULT drawThemeText(HTHEME theme, HDC dc, int part, int state,
	LPCWSTR text, int length, DWORD flags, DWORD flags2, const RECT* rect) {
	return ::DrawThemeText(theme, dc, part, state, text, length, flags, flags2, rect);
}

HRESULT getThemeMargins(HTHEME theme, HDC dc, int part, int state,
	int property, RECT* rect, MARGINS* margins) {
	return ::GetThemeMargins(theme, dc, part, state, property, rect, margins);
}

HRESULT getThemeTextExtent(HTHEME theme, HDC dc, int part, int state,
	LPCWSTR text, int length, DWORD flags, const RECT* bounds, RECT* extent) {
	return ::GetThemeTextExtent(theme, dc, part, state, text, length, flags, bounds, extent);
}

HRESULT getThemeColor(HTHEME theme, int part, int state, int property, COLORREF* color) {
	return ::GetThemeColor(theme, part, state, property, color);
}

HRESULT getThemePartSize(HTHEME theme, HDC dc, int part, int state,
	RECT* rect, THEMESIZE sizeType, SIZE* size) {
	return ::GetThemePartSize(theme, dc, part, state, rect, sizeType, size);
}

BOOL isThemeBackgroundPartiallyTransparent(HTHEME theme, int part, int state) {
	return ::IsThemeBackgroundPartiallyTransparent(theme, part, state);
}

} } } }
