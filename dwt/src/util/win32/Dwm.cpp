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

#include <dwt/util/win32/Dwm.h>

#include <dwt/LibraryLoader.h>
#include <cstring>

namespace dwt { namespace util { namespace win32 { namespace dwm {

namespace {

LibraryLoader& library() {
	static LibraryLoader instance(_T("dwmapi.dll"), true);
	return instance;
}

template<typename Function>
Function getProc(LPCTSTR name) {
	Function function = nullptr;
	auto& module = library();
	if(module.loaded()) {
		const auto address = module.getProcAddress(name);
		static_assert(sizeof(function) == sizeof(address),
			"Function pointer representations must have equal size");
		std::memcpy(&function, &address, sizeof(function));
	}
	return function;
}

}

HRESULT getWindowAttribute(HWND window, DWMWINDOWATTRIBUTE attribute,
	PVOID value, DWORD size) {
	static const auto function = getProc<decltype(&::DwmGetWindowAttribute)>(
		_T("DwmGetWindowAttribute"));
	return function ? function(window, attribute, value, size) : E_NOTIMPL;
}

HRESULT setWindowAttribute(HWND window, DWMWINDOWATTRIBUTE attribute,
	LPCVOID value, DWORD size) {
	static const auto function = getProc<decltype(&::DwmSetWindowAttribute)>(
		_T("DwmSetWindowAttribute"));
	return function ? function(window, attribute, value, size) : E_NOTIMPL;
}

HRESULT setDarkMode(HWND window, bool enabled) {
	const BOOL value = enabled ? TRUE : FALSE;
	return setWindowAttribute(window, darkModeAttribute, &value, sizeof(value));
}

HRESULT setCaptionColor(HWND window, COLORREF color) {
	return setWindowAttribute(window, captionColorAttribute, &color, sizeof(color));
}

HRESULT setTextColor(HWND window, COLORREF color) {
	return setWindowAttribute(window, textColorAttribute, &color, sizeof(color));
}

HRESULT setBorderColor(HWND window, COLORREF color) {
	return setWindowAttribute(window, borderColorAttribute, &color, sizeof(color));
}

HRESULT resetFrameAppearance(HWND window) {
	const HRESULT results[] = { setDarkMode(window, false), setCaptionColor(window),
		setTextColor(window), setBorderColor(window) };
	for(auto result : results) {
		if(FAILED(result)) return result;
	}
	return S_OK;
}

HRESULT setNonClientRendering(HWND window, DWMNCRENDERINGPOLICY policy) {
	return setWindowAttribute(window, DWMWA_NCRENDERING_POLICY,
		&policy, sizeof(policy));
}

HRESULT invalidateIconicBitmaps(HWND window) {
	static const auto function = getProc<decltype(&::DwmInvalidateIconicBitmaps)>(
		_T("DwmInvalidateIconicBitmaps"));
	return function ? function(window) : E_NOTIMPL;
}

HRESULT setIconicThumbnail(HWND window, HBITMAP bitmap, DWORD flags) {
	static const auto function = getProc<decltype(&::DwmSetIconicThumbnail)>(
		_T("DwmSetIconicThumbnail"));
	return function ? function(window, bitmap, flags) : E_NOTIMPL;
}

HRESULT setIconicLivePreviewBitmap(HWND window, HBITMAP bitmap,
	POINT* offset, DWORD flags) {
	static const auto function = getProc<decltype(&::DwmSetIconicLivePreviewBitmap)>(
		_T("DwmSetIconicLivePreviewBitmap"));
	return function ? function(window, bitmap, offset, flags) : E_NOTIMPL;
}

} } } }
