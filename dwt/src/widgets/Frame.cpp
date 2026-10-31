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

#include <dwt/widgets/Frame.h>
#include <dwt/CanvasClasses.h>
#include <dwt/DWTException.h>
#include <dwt/resources/Brush.h>
#include <dwt/util/win32/Dwm.h>

#include "ClientFrame.h"

#include <algorithm>

namespace dwt {

namespace {

UINT frameLayoutMessage() {
	static const UINT message = ::RegisterWindowMessage(_T("dwt.Frame.Layout"));
	return message;
}

}

Frame::Frame(Widget* parent, Dispatcher& dispatcher) : Composite(parent, dispatcher), smallIcon(0), largeIcon(0), clientFrame(nullptr), captionStyle(0), edgeStyle(0), manualFrame(false), changingFrame(false) {
	addCallback(Message(WM_CREATE), [this](const MSG&, LRESULT&) {
		updateCaptionAppearance();
		return false;
	});
}

Frame::~Frame() {
	// Child HWNDs (including ClientFrame) are destroyed by USER32 before us.
}

void Frame::setFrameRendering(FrameRendering rendering) {
	if(frameRendering == rendering) return;
	frameRendering = rendering;
	updateCaptionAppearance();
}

Rectangle Frame::getContentRect() const {
	auto size = getClientSize();
	const long top = manualFrame && clientFrame ? std::min(size.y, clientFrame->height()) : 0;
	const bool resize = manualFrame && hasStyle(WS_THICKFRAME) && !::IsZoomed(handle());
	const long edgeX = resize ? getSystemMetric(SM_CXSIZEFRAME) + getSystemMetric(SM_CXPADDEDBORDER) : 0;
	const long edgeY = resize ? getSystemMetric(SM_CYSIZEFRAME) + getSystemMetric(SM_CXPADDEDBORDER) : 0;
	return Rectangle(std::min(edgeX, size.x / 2), top, std::max(0L, size.x - edgeX * 2), std::max(0L, size.y - top - edgeY));
}

bool Frame::adjustContentRect(RECT& rect) const {
	if(!manualFrame) return adjustWindowRect(rect, ::GetMenu(handle()) != nullptr);
	rect.top -= clientFrame ? clientFrame->height() : 0;
	if(hasStyle(WS_THICKFRAME) && !::IsZoomed(handle())) {
		const auto edgeX = getSystemMetric(SM_CXSIZEFRAME) + getSystemMetric(SM_CXPADDEDBORDER);
		rect.left -= edgeX;
		rect.right += edgeX;
		rect.bottom += getSystemMetric(SM_CYSIZEFRAME) + getSystemMetric(SM_CXPADDEDBORDER);
	}
	return true;
}

bool Frame::isMenuBarFocused() const {
	if(manualFrame && clientFrame) return clientFrame->menuFocused();
	MENUBARINFO info = { sizeof(MENUBARINFO) };
	return ::GetMenuBarInfo(handle(), OBJID_MENU, 0, &info) && info.fBarFocused;
}

void Frame::setMenuBar(HMENU menu) {
	if(!::SetMenu(handle(), menu)) throw Win32Exception("SetMenu in Frame::setMenuBar failed");
	refreshClientFrame();
	::DrawMenuBar(handle());
}

void Frame::refreshClientFrame() {
	if(!manualFrame || !clientFrame || changingFrame) return;
	const long previousHeight = clientFrame->height();
	clientFrame->layout();
	if(previousHeight != clientFrame->height()) ::PostMessage(handle(), frameLayoutMessage(), 0, 0);
}

void Frame::appearanceChanged() {
	BaseType::appearanceChanged();
	updateCaptionAppearance();
	if(manualFrame && clientFrame) clientFrame->refresh();
}

bool Frame::handleMessage(const MSG& msg, LRESULT& result) {
	// These messages must never reach menu callbacks or the native frame painter.
	if(manualFrame) {
		switch(msg.message) {
		case WM_NCCALCSIZE:
			if(msg.wParam && msg.lParam && ::IsZoomed(handle())) {
				auto params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg.lParam);
				MONITORINFO monitor = { sizeof(MONITORINFO) };
				if(::GetMonitorInfo(::MonitorFromRect(&params->rgrc[0], MONITOR_DEFAULTTONEAREST), &monitor)) params->rgrc[0] = monitor.rcWork;
			}
			result = 0;
			return true;
		case WM_NCPAINT:
			refreshClientFrame();
			result = 0;
			return true;
		case WM_NCMOUSEMOVE:
		case WM_NCMOUSELEAVE:
			// Caption/menu buttons have client input and their own hover state.
			result = 0;
			return true;
		case WM_NCACTIVATE:
			if(clientFrame) clientFrame->handleFrameMessage(msg, result);
			result = TRUE;
			return true;
		case WM_NCHITTEST:
			if(clientFrame) {
				result = clientFrame->hitTest(msg.lParam);
				return true;
			}
			break;
		case 0x00ae: // WM_NCUAHDRAWCAPTION (undocumented native caption painting)
		case 0x00af: // WM_NCUAHDRAWFRAME
			result = 0;
			return true;
		case WM_SIZE:
		case WM_WINDOWPOSCHANGED:
			refreshClientFrame();
			break;
		}
		if(clientFrame && clientFrame->handleFrameMessage(msg, result)) return true;
	}

	const bool handled = BaseType::handleMessage(msg, result);
	if(msg.message == WM_UNINITMENUPOPUP && clientFrame && reinterpret_cast<HMENU>(msg.wParam) == ::GetSystemMenu(handle(), FALSE)) clientFrame->restoreSystemMenu();
	if(msg.message == frameLayoutMessage()) {
		refreshClientFrame();
		layout();
		result = 0;
		return true;
	}
	if(!manualFrame) {
		if(!changingFrame && (msg.message == WM_THEMECHANGED || msg.message == WM_DWMCOMPOSITIONCHANGED)) {
			changingFrame = true;
			updateNativeFrameAppearance();
			changingFrame = false;
		}
		return handled;
	}

	switch(msg.message) {
	case WM_INITMENUPOPUP:
		if(clientFrame && reinterpret_cast<HMENU>(msg.wParam) == ::GetSystemMenu(handle(), FALSE)) {
			if(!handled) result = getDispatcher().chain(msg);
			clientFrame->themeSystemMenu(reinterpret_cast<HMENU>(msg.wParam));
			return true;
		}
		break;
	case WM_GETMINMAXINFO:
		if(msg.lParam) {
			if(!handled) result = getDispatcher().chain(msg);
			auto info = reinterpret_cast<MINMAXINFO*>(msg.lParam);
			MONITORINFO monitor = { sizeof(MONITORINFO) };
			if(::GetMonitorInfo(::MonitorFromWindow(handle(), MONITOR_DEFAULTTONEAREST), &monitor)) {
				// A captionless WS_THICKFRAME window otherwise maximizes over the
				// taskbar. Match its outer bounds to the client work area as well.
				info->ptMaxPosition = { monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top };
				info->ptMaxSize = { monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top };
			}
			info->ptMinTrackSize.x = std::max(info->ptMinTrackSize.x, static_cast<LONG>(scale(220)));
			info->ptMinTrackSize.y = std::max(info->ptMinTrackSize.y, getContentRect().top() + scale(40));
			return true;
		}
		break;
	case WM_ERASEBKGND:
		if(!handled && handleEraseBackground(reinterpret_cast<HDC>(msg.wParam))) { result = TRUE; return true; }
		break;
	case WM_SETTEXT:
	case WM_SETICON:
	case WM_ENABLE:
		// The native caption is absent. Store its metadata without masking
		// WS_VISIBLE, which would disturb child visibility and queue full layouts.
		if(!handled) result = getDispatcher().chain(msg);
		if(clientFrame) {
			clientFrame->layout();
			clientFrame->redrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN);
		}
		return true;
	case WM_THEMECHANGED:
	case WM_DWMCOMPOSITIONCHANGED:
	case WM_DPICHANGED:
	case WM_STYLECHANGED:
		if(!changingFrame && clientFrame) {
			clientFrame->refresh();
			::PostMessage(handle(), frameLayoutMessage(), 0, 0);
		}
		break;
	}
	return handled;
}

bool Frame::handleEraseBackground(HDC dc) {
	if(!dc || !isManualAppearance() || getAppearancePolicy() == AppearancePolicy::ApplicationContent || hasExplicitColors()) return false;
	FreeCanvas canvas(dc);
	canvas.fill(Rectangle(getClientSize()), *getAppearance().getBackgroundBrush());
	return true;
}

void Frame::updateCaptionAppearance() {
	if(!handle() || !::IsWindow(handle()) || changingFrame) return;
	const auto style = static_cast<DWORD>(::GetWindowLongPtr(handle(), GWL_STYLE));
	const auto exStyle = static_cast<DWORD>(::GetWindowLongPtr(handle(), GWL_EXSTYLE));
	// Child frames and intentionally captionless windows retain their own layout.
	const bool manual = frameRendering == FrameRendering::Automatic && isManualAppearance() && !(style & WS_CHILD) && (manualFrame || (style & WS_CAPTION) == WS_CAPTION);
	changingFrame = true;
	updateNativeFrameAppearance();
	if(manual == manualFrame) {
		changingFrame = false;
		return;
	}
	manualFrame = manual;
	if(manual) {
		captionStyle = style & WS_CAPTION;
		edgeStyle = exStyle & (WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
		if(!clientFrame) {
			clientFrame = new ClientFrame(this);
			clientFrame->onDestroy([this] { clientFrame = nullptr; });
		}
		::SetWindowLongPtr(handle(), GWL_STYLE, style & ~WS_CAPTION);
		::SetWindowLongPtr(handle(), GWL_EXSTYLE, exStyle & ~edgeStyle);
		clientFrame->setVisible(true);
	} else {
		if(clientFrame) {
			clientFrame->releaseFrameInput();
			clientFrame->restoreSystemMenu();
			clientFrame->setVisible(false);
		}
		::SetWindowLongPtr(handle(), GWL_STYLE, style | captionStyle);
		::SetWindowLongPtr(handle(), GWL_EXSTYLE, exStyle | edgeStyle);
	}
	MONITORINFO monitor = { sizeof(MONITORINFO) };
	if(manual && ::IsZoomed(handle()) && ::GetMonitorInfo(::MonitorFromWindow(handle(), MONITOR_DEFAULTTONEAREST), &monitor)) {
		// Creation and live theme changes can replace an already maximized native
		// frame, whose outer rectangle still includes its old non-client margins.
		const auto& work = monitor.rcWork;
		::SetWindowPos(handle(), nullptr, work.left, work.top, work.right - work.left, work.bottom - work.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	} else {
		::SetWindowPos(handle(), nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
	changingFrame = false;
	if(manual && clientFrame) clientFrame->refresh();
	::PostMessage(handle(), frameLayoutMessage(), 0, 0);
	redrawWindow(RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void Frame::updateNativeFrameAppearance() {
	const bool eligible = frameRendering == FrameRendering::Native && isManualAppearance() &&
		!hasStyle(WS_CHILD) && (manualFrame || hasStyle(WS_CAPTION));
	if(eligible) {
		const auto& palette = getAppearance().getPalette();
		// Try independently: older systems can support only part of this contract.
		// A failed attribute must not prevent Windows from owning the native frame.
		nativeFrameAppearanceApplied |= SUCCEEDED(util::win32::dwm::setDarkMode(handle(), getAppearance().isDark()));
		nativeFrameAppearanceApplied |= SUCCEEDED(util::win32::dwm::setCaptionColor(handle(), palette.surface));
		nativeFrameAppearanceApplied |= SUCCEEDED(util::win32::dwm::setTextColor(handle(), palette.text));
		nativeFrameAppearanceApplied |= SUCCEEDED(util::win32::dwm::setBorderColor(handle(), palette.border));
	} else if(nativeFrameAppearanceApplied) {
		util::win32::dwm::resetFrameAppearance(handle());
		nativeFrameAppearanceApplied = false;
	}
}

}
