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

#include <dwt/util/win32/DarkMode.h>

#include <dwt/util/win32/Dwm.h>
#include <dwt/util/win32/Version.h>

#include <cstring>
#include <uxtheme.h>
#include <vssym32.h>

namespace dwt { namespace util { namespace win32 { namespace darkmode {

namespace {

typedef void (WINAPI *RefreshImmersiveColorPolicyState)();
typedef bool (WINAPI *AllowDarkModeForWindow)(HWND window, bool allow);
typedef int (WINAPI *SetPreferredAppMode)(int mode);
typedef void (WINAPI *FlushMenuThemes)();

enum PreferredAppMode { Default = 0, ForceDark = 2 };

struct Functions {
	RefreshImmersiveColorPolicyState refresh = nullptr;
	AllowDarkModeForWindow allowForWindow = nullptr;
	SetPreferredAppMode setPreferredAppMode = nullptr;
	FlushMenuThemes flushMenuThemes = nullptr;
	bool complete = false;
};

template<typename Function>
Function ordinal(HMODULE module, WORD number) {
	const auto address = ::GetProcAddress(module, MAKEINTRESOURCEA(number));
	Function function = nullptr;
	static_assert(sizeof(function) == sizeof(address), "Function pointer representations must have equal size");
	std::memcpy(&function, &address, sizeof(function));
	return function;
}

const Functions& functions() {
	static const Functions result = [] {
		Functions loaded;
		// Before 1903 these ordinals are other functions (or missing): check the version first.
		if(!ensureVersion(TEN_1903)) {
			return loaded;
		}
		// Linked, so already loaded.
		const auto module = ::GetModuleHandleW(L"uxtheme.dll");
		if(!module) {
			return loaded;
		}
		loaded.refresh = ordinal<RefreshImmersiveColorPolicyState>(module, 104);
		loaded.allowForWindow = ordinal<AllowDarkModeForWindow>(module, 133);
		loaded.setPreferredAppMode = ordinal<SetPreferredAppMode>(module, 135);
		loaded.flushMenuThemes = ordinal<FlushMenuThemes>(module, 136);
		loaded.complete = loaded.refresh && loaded.allowForWindow && loaded.setPreferredAppMode &&
			loaded.flushMenuThemes;
		return loaded;
	}();
	return result;
}

bool isLight(COLORREF color) {
	return GetRValue(color) + GetGValue(color) + GetBValue(color) > 3 * 128;
}

}

bool isSupported() {
	return functions().complete;
}

bool hasDarkTheme() {
	static const bool result = [] {
		if(!isSupported()) {
			return false;
		}
		// An unknown class name falls back to the plain Button style, whose group box text is dark.
		const auto theme = ::OpenThemeData(nullptr, L"DarkMode_DarkTheme::Button");
		if(!theme) {
			return false;
		}
		COLORREF color = 0;
		const bool light = SUCCEEDED(::GetThemeColor(theme, BP_GROUPBOX, GBS_NORMAL, TMT_TEXTCOLOR, &color)) &&
			isLight(color);
		::CloseThemeData(theme);
		return light;
	}();
	return result;
}

bool setApplicationDark(bool dark) {
	const auto& api = functions();
	if(!api.complete) {
		return false;
	}
	// Default, not ForceLight: out of dark mode, Windows' own menus stay as they are.
	static int current = -1;
	const int mode = dark ? ForceDark : Default;
	if(mode != current) {
		current = mode;
		api.setPreferredAppMode(mode);
		api.refresh();
		api.flushMenuThemes();
	}
	return true;
}

void allowForWindow(HWND window, bool allow) {
	const auto& api = functions();
	if(api.complete && window) {
		api.allowForWindow(window, allow);
	}
}

HRESULT setDarkCaption(HWND window, bool dark) {
	return dwm::setDarkMode(window, dark);
}

LPCWSTR themeName(Theme theme) {
	switch(theme) {
	case Theme::Explorer: return L"DarkMode_Explorer";
	case Theme::Cfd: return L"DarkMode_CFD";
	case Theme::ItemsView: return L"DarkMode_ItemsView";
	case Theme::DarkTheme: return L"DarkMode_DarkTheme";
	case Theme::None: break;
	}
	return nullptr;
}

} } } }
