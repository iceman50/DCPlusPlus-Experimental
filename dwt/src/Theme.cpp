/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka
  Copyright (C) 2026 iceman50

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor SmartWin++ nor the names of its contributors
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

#include <dwt/Theme.h>

#include <dwt/Dispatchers.h>
#include <dwt/util/check.h>
#include <dwt/dwt_vssym32.h>

namespace dwt {

Theme::Theme() : theme(0), w(nullptr)
{
}

Theme::~Theme() {
	close();
}

void Theme::load(const tstring& classes, Widget* w_, bool handleThemeChanges) {
	w = w_;
	this->classes = classes;
	dwtassert(w, "Theme: no widget set");

	reload();

	if(handleThemeChanges) {
		w->addCallback(Message(WM_THEMECHANGED), Dispatchers::VoidVoid<0, false>([this] {
			reload();
		}));
	}
}

void Theme::reload() {
	close();
	if(w && !classes.empty()) {
		open(classes);
	}
}

void Theme::unload() {
	close();
}

void Theme::drawBackground(Canvas& canvas, int part, int state, const Rectangle& rect, bool drawParent, std::optional<Rectangle> clip) {
	::RECT rc = rect;
	std::optional<RECT> rcClip(clip);
	if(drawParent && isBackgroundPartiallyTransparent(part, state)) {
		util::win32::uxtheme::drawThemeParentBackground(w->handle(), canvas.handle(), clip ? &rcClip.value() : &rc);
	}
	util::win32::uxtheme::drawThemeBackground(theme, canvas.handle(), part, state, &rc, clip ? &rcClip.value() : 0);
}

void Theme::drawText(Canvas& canvas, int part, int state, const tstring& text, unsigned textFlags, const Rectangle& rect) {
	::RECT rc = rect;
	util::win32::uxtheme::drawThemeText(theme, canvas.handle(), part, state, text.c_str(), static_cast<int>(text.size()), textFlags, 0, &rc);
}

void Theme::formatRect(Canvas& canvas, int part, int state, Rectangle& rect) {
	::RECT rc = rect;
	MARGINS margins = { 0 };
	if(util::win32::uxtheme::getThemeMargins(theme, canvas.handle(), part, state, TMT_CONTENTMARGINS, &rc, &margins) == S_OK) {
		rc.left += margins.cxLeftWidth; rc.right -= margins.cxRightWidth;
		rc.top += margins.cyTopHeight; rc.bottom -= margins.cyBottomHeight;
		rect = Rectangle(rc);
	}
}

void Theme::formatTextRect(Canvas& canvas, int part, int state, const tstring& text, unsigned textFlags, Rectangle& rect) {
	formatRect(canvas, part, state, rect);

	::RECT rc = rect;
	::RECT rcOut;
	if(util::win32::uxtheme::getThemeTextExtent(theme, canvas.handle(), part, state, text.c_str(), static_cast<int>(text.size()), textFlags, &rc, &rcOut) == S_OK) {
		rect = Rectangle(rcOut);
	}
}

COLORREF Theme::getColor(int part, int state, int specifier) {
	COLORREF color;
	if(util::win32::uxtheme::getThemeColor(theme, part, state, specifier, &color) == S_OK)
		return color;
	return NaC;
}

bool Theme::getPartSize(Canvas& canvas, int part, int state, Point& ret) {
	SIZE size;
	if(util::win32::uxtheme::getThemePartSize(theme, canvas.handle(), part, state, 0, TS_TRUE, &size) == S_OK) {
		ret.x = size.cx;
		ret.y = size.cy;
		return true;
	}
	return false;
}

bool Theme::isBackgroundPartiallyTransparent(int part, int state) {
	return util::win32::uxtheme::isThemeBackgroundPartiallyTransparent(theme, part, state);
}

Theme::operator bool() const {
	return theme;
}

void Theme::open(const tstring& classes) {
	if(util::win32::uxtheme::isAppThemed()) {
		theme = util::win32::uxtheme::openThemeData(w->handle(), classes.c_str());
	}
}

void Theme::close() {
	if(theme) {
		util::win32::uxtheme::closeThemeData(theme);
		theme = 0;
	}
}

}
