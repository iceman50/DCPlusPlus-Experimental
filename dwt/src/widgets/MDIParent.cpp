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

#include <dwt/widgets/MDIParent.h>
#include <dwt/CanvasClasses.h>
#include <dwt/DWTException.h>
#include <dwt/resources/Bitmap.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Icon.h>
#include <dwt/widgets/Menu.h>

namespace dwt {

namespace {

Rectangle getCenteredImageRect(const Point& imageSize, const Rectangle& client)
{
	long destWidth = imageSize.x;
	long destHeight = imageSize.y;
	if(destWidth > client.width() || destHeight > client.height()) {
		if(imageSize.x * client.height() > client.width() * imageSize.y) {
			destWidth = client.width();
			destHeight = ::MulDiv(imageSize.y, static_cast<int>(destWidth), imageSize.x);
		} else {
			destHeight = client.height();
			destWidth = ::MulDiv(imageSize.x, static_cast<int>(destHeight), imageSize.y);
		}
	}

	return Rectangle(
		(client.width() - destWidth) / 2,
		(client.height() - destHeight) / 2,
		destWidth,
		destHeight);
}

void drawBackground(MDIParent* parent, Canvas& canvas, COLORREF color, const BitmapPtr& bitmap, const IconPtr& icon)
{
	const Rectangle client(parent->getClientSize());
	canvas.fill(client, Brush(color));

	if(bitmap && bitmap->handle()) {
		const Point imageSize = bitmap->getSize();
		if(imageSize.x > 0 && imageSize.y > 0 && client.width() > 0 && client.height() > 0) {
			CompatibleCanvas imageCanvas(canvas.handle());
			auto imageSelection = imageCanvas.select(*bitmap);
			const Rectangle imageRect = getCenteredImageRect(imageSize, client);

			const int oldMode = ::SetStretchBltMode(canvas.handle(), HALFTONE);
			::SetBrushOrgEx(canvas.handle(), 0, 0, nullptr);
			::StretchBlt(canvas.handle(),
				static_cast<int>(imageRect.x()),
				static_cast<int>(imageRect.y()),
				static_cast<int>(imageRect.width()),
				static_cast<int>(imageRect.height()),
				imageCanvas.handle(),
				0,
				0,
				imageSize.x,
				imageSize.y,
				SRCCOPY);
			::SetStretchBltMode(canvas.handle(), oldMode);
		}
	} else if(icon && icon->handle()) {
		const Point imageSize = icon->getSize();
		if(imageSize.x > 0 && imageSize.y > 0 && client.width() > 0 && client.height() > 0) {
			canvas.drawIcon(icon, getCenteredImageRect(imageSize, client));
		}
	}
}

}

MDIParent::Seed::Seed() :
	BaseType::Seed(WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL | WS_HSCROLL, WS_EX_CLIENTEDGE),
	idFirstChild(0),
	windowMenu(NULL)
{
}

MDIParent::~MDIParent()
{
	if(hasAcceleratorFilter) {
		Application::instance().removeFilter(acceleratorFilter);
	}
}

void MDIParent::create( const Seed & cs )
{
	CLIENTCREATESTRUCT ccs;
	ccs.hWindowMenu = cs.windowMenu;
	ccs.idFirstChild = cs.idFirstChild;

	HWND wnd = ::CreateWindowEx( cs.exStyle,
		_T("MDICLIENT"),
		cs.caption.c_str(),
		cs.style,
		cs.location.x(), cs.location.y(), cs.location.width(), cs.location.height(),
		getParentHandle(),
		cs.menuHandle,
		::GetModuleHandle(NULL),
		reinterpret_cast< LPVOID >( &ccs ) );

	if (wnd == NULL) {
		// The most common error is to forget WS_CHILD in the styles
		throw Win32Exception("CreateWindowEx failed");
	}

	static_cast<MDIClientDispatcher&>(getDispatcher()).setWindowProc(
		reinterpret_cast<WNDPROC>(::GetWindowLongPtr(wnd, GWLP_WNDPROC)));
	setHandle(wnd);

	acceleratorFilter = Application::instance().addFilter([this](MSG& msg) -> bool {
		if(msg.message < WM_KEYFIRST || msg.message > WM_KEYLAST || !handle() || !::IsWindow(handle())) {
			return false;
		}

		return ::TranslateMDISysAccel(handle(), &msg) != 0;
	});
	hasAcceleratorFilter = true;

	addCallback(Message(WM_ERASEBKGND), [this](const MSG& msg, LRESULT& ret) -> bool {
		ret = handleEraseBackground(reinterpret_cast<HDC>(msg.wParam)) ? TRUE : FALSE;
		return true;
	});

	addCallback(Message(WM_PAINT), [this](const MSG&, LRESULT& ret) -> bool {
		ret = handlePaintBackground() ? 0 : 1;
		return true;
	});
}

void MDIParent::setMenu(HMENU frameMenu, HMENU windowMenu)
{
	sendMessage(WM_MDISETMENU, reinterpret_cast<WPARAM>(frameMenu), reinterpret_cast<LPARAM>(windowMenu));
	::DrawMenuBar(getParent()->handle());
}

void MDIParent::setMenu(Menu* frameMenu, Menu* windowMenu)
{
	setMenu(frameMenu ? frameMenu->handle() : NULL, windowMenu ? windowMenu->handle() : NULL);
}

void MDIParent::setBackgroundColor(COLORREF color)
{
	backgroundColor = color;
	redrawBackground();
}

void MDIParent::setBackgroundImage(const BitmapPtr& bitmap)
{
	backgroundImage = bitmap;
	backgroundIcon = IconPtr();
	redrawBackground();
}

void MDIParent::setBackgroundImage(const IconPtr& icon)
{
	backgroundIcon = icon;
	backgroundImage = BitmapPtr();
	redrawBackground();
}

void MDIParent::clearBackgroundImage()
{
	backgroundImage = BitmapPtr();
	backgroundIcon = IconPtr();
	redrawBackground();
}

bool MDIParent::handlePaintBackground()
{
	PaintCanvas canvas(this);
	drawBackground(this, canvas, backgroundColor, backgroundImage, backgroundIcon);
	return true;
}

bool MDIParent::handleEraseBackground(HDC hdc)
{
	if(!hdc) {
		return false;
	}

	FreeCanvas canvas(hdc);
	drawBackground(this, canvas, backgroundColor, backgroundImage, backgroundIcon);
	return true;
}

void MDIParent::redrawBackground()
{
	if(handle() && ::IsWindow(handle())) {
		::InvalidateRect(handle(), nullptr, TRUE);
	}
}

}
