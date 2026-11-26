/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka

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

#include <dwt/widgets/DateTime.h>
#include <dwt/resources/DibBitmap.h>
#include "AppearanceDraw.h"
#include <algorithm>

namespace dwt {

const TCHAR DateTime::windowClass[] = DATETIMEPICK_CLASS;

DateTime::Seed::Seed() :
	BaseType::Seed(WS_CHILD | DTS_SHORTDATEFORMAT),
	font(0),
	format(_T( "yyyy.MM.dd" )),
	backgroundColor(0x000080),
	monthBackgroundColor(0x808080),
	monthTextColor(0xFFFFFF),
	titleBackgroundColor(0x202020),
	titleTextColor(0x008080),
	trailingTextColor(0x000000)
{
	::GetSystemTime( &initialDateTime );
}

void DateTime::create( const Seed & cs )
{
	ControlType::create(cs);
	setFont(cs.font);
	setFormat( cs.format );
	setDateTime( cs.initialDateTime );
	setBackgroundColor( cs.backgroundColor );
	setMonthBackgroundColor( cs.monthBackgroundColor );
	setMonthTextColor( cs.monthTextColor );
	setTitleBackgroundColor( cs.titleBackgroundColor );
	setTrailingTextColor( cs.trailingTextColor );
	setTitleTextColor( cs.titleTextColor );
}

Point DateTime::getPreferredSize() {
	auto ideal = getIdealSize();
	if(ideal.x > 0 && ideal.y > 0) return ideal;
	return scale(Point(100, 24));
}

void DateTime::appearanceChanged() {
	if(hasStyle(DTS_TIMEFORMAT)) {
		if(auto spin = getPickerInfo().hwndUD) {
			if(isManualAppearance())
				::SetWindowSubclass(spin, spinProc, 0, reinterpret_cast<DWORD_PTR>(this));
			else
				::RemoveWindowSubclass(spin, spinProc, 0);
			::InvalidateRect(spin, nullptr, TRUE);
		}
	}
	BaseType::appearanceChanged();
}

LRESULT CALLBACK DateTime::spinProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
	UINT_PTR id, DWORD_PTR data)
{
	auto* picker = reinterpret_cast<DateTime*>(data);
	if(message == WM_NCDESTROY) ::RemoveWindowSubclass(window, spinProc, id);
	try {
		if(message == WM_ERASEBKGND) return TRUE;
		if(message == WM_PAINT) {
			PaintCanvas canvas(window);
			appearance_detail::drawSpinner(*picker, window, canvas, picker->getAppearance().getPalette());
			return 0;
		}
		if(message == WM_PRINTCLIENT && wParam) {
			FreeCanvas canvas(reinterpret_cast<HDC>(wParam));
			appearance_detail::drawSpinner(*picker, window, canvas, picker->getAppearance().getPalette());
			return 0;
		}
	} catch(const std::exception&) {
		// Keep the native control usable if a GDI resource cannot be allocated.
	}
	return ::DefSubclassProc(window, message, wParam, lParam);
}

namespace {
DWORD dibColor(COLORREF color) {
	return RGB(GetBValue(color), GetGValue(color), GetRValue(color));
}

int brightness(DWORD pixel) {
	return (GetBValue(pixel) * 77 + GetGValue(pixel) * 150 + GetRValue(pixel) * 29) / 256;
}
}

void DateTime::paintTimeField(HDC dc) {
	const auto size = getClientSize();
	if(size.x <= 0 || size.y <= 0) return;
	// Preserve comctl32's field layout, callback formats and selection geometry.
	MSG print{handle(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT};
	DibBitmap bitmap(dc, size, DibBitmap::RowOrder::TopDown);
	if(!bitmap.handle()) {
		getDispatcher().chain(print);
		return;
	}
	CompatibleCanvas buffer(dc);
	auto selected = buffer.select(bitmap);
	::PatBlt(buffer.handle(), 0, 0, size.x, size.y, WHITENESS);
	print.wParam = reinterpret_cast<WPARAM>(buffer.handle());
	getDispatcher().chain(print);
	::GdiFlush();

	const auto& colors = getAppearance().getPalette();
	const auto surface = hasExplicitColors() ? getExplicitBackgroundColor() : colors.surface;
	const bool enabled = getEnabled();
	const auto text = enabled ? (hasExplicitColors() ? getExplicitTextColor() : colors.text) : colors.disabledText;
	const auto highlight = dibColor(::GetSysColor(COLOR_HIGHLIGHT));
	const int highlightLevel = brightness(highlight);
	const int highlightText = brightness(dibColor(::GetSysColor(COLOR_HIGHLIGHTTEXT)));
	const int paper = brightness(dibColor(::GetSysColor(enabled ? COLOR_WINDOW : COLOR_BTNFACE)));
	const int ink = brightness(dibColor(::GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT)));
	auto inkWeight = [](int level, int paper, int ink) {
		return static_cast<BYTE>(paper == ink ? 0 : std::clamp(255 * (level - paper) / (ink - paper), 0, 255));
	};
	DWORD normal[256], active[256];
	for(int level = 0; level < 256; ++level) {
		normal[level] = dibColor(Appearance::blend(surface, text, inkWeight(level, paper, ink)));
		active[level] = dibColor(Appearance::blend(colors.accent, colors.highlightText,
			inkWeight(level, highlightLevel, highlightText)));
	}
	const bool focused = ::GetFocus() == handle();
	auto* pixels = static_cast<DWORD*>(bitmap.pixels());
	// Native highlight text can be the same color as the normal field background.
	// Determine which palette to use from the selected segment's scanline.
	for(long y = 0; y < size.y; ++y, pixels += size.x) {
		long first = size.x, last = -1;
		if(focused) {
			for(long x = 0; x < size.x; ++x) {
				if((pixels[x] & 0xffffff) == highlight) {
					first = std::min(first, x);
					last = x;
				}
			}
		}
		for(long x = 0; x < size.x; ++x) {
			const int level = brightness(pixels[x]);
			pixels[x] = x >= first && x <= last ? active[level] : normal[level];
		}
	}
	::BitBlt(dc, 0, 0, size.x, size.y, buffer.handle(), 0, 0, SRCCOPY);
}

bool DateTime::handleMessage(const MSG& msg, LRESULT& result) {
	if(BaseType::handleMessage(msg, result)) return true;
	if(msg.message != WM_PAINT && msg.message != WM_PRINTCLIENT && msg.message != WM_ERASEBKGND) return false;
	if(!isManualAppearance() || !hasStyle(DTS_TIMEFORMAT)) return false;
	if(msg.message == WM_ERASEBKGND) {
		result = TRUE;
		return true;
	} else if(msg.message == WM_PAINT) {
		PaintCanvas canvas(this);
		paintTimeField(canvas.handle());
	} else if(msg.message == WM_PRINTCLIENT && msg.wParam) {
		paintTimeField(reinterpret_cast<HDC>(msg.wParam));
	} else {
		return false;
	}
	result = 0;
	return true;
}

}
