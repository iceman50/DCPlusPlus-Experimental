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

#include <dwt/Events.h>

#include <cstring>

namespace dwt {

namespace {

const UINT32 maxPointerHistory = 256;

template<typename T>
T user32Function(const char* name) {
	T function = nullptr;
	auto address = ::GetProcAddress(::GetModuleHandle(_T("user32.dll")), name);
	static_assert(sizeof(function) == sizeof(address), "Function pointer size mismatch");
	std::memcpy(&function, &address, sizeof(function));
	return function;
}

Rectangle touchContactRect(const TOUCHINPUT& input) {
	if((input.dwMask & TOUCHINPUTMASKF_CONTACTAREA) == 0) {
		return Rectangle();
	}

	const long x = TOUCH_COORD_TO_PIXEL(input.x);
	const long y = TOUCH_COORD_TO_PIXEL(input.y);
	const long width = TOUCH_COORD_TO_PIXEL(input.cxContact);
	const long height = TOUCH_COORD_TO_PIXEL(input.cyContact);
	return Rectangle(x - width / 2, y - height / 2, width, height);
}

}

DpiChangedEvent::DpiChangedEvent(unsigned oldDpi_, const MSG& msg) :
	oldDpi(oldDpi_),
	newDpi(LOWORD(msg.wParam)),
	suggestedBounds(*reinterpret_cast<const RECT*>(msg.lParam))
{
}

SystemSettingsEvent::SystemSettingsEvent(const MSG& msg) :
	action(static_cast<UINT>(msg.wParam)),
	section(msg.lParam ? reinterpret_cast<const TCHAR*>(msg.lParam) : _T("")),
	highContrast(false),
	clientAreaAnimation(true)
{
	HIGHCONTRAST value = { sizeof(HIGHCONTRAST) };
	if(::SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(value), &value, 0)) {
		highContrast = (value.dwFlags & HCF_HIGHCONTRASTON) != 0;
	}

	BOOL animation = TRUE;
	if(::SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0)) {
		clientAreaAnimation = animation != FALSE;
	}
}

SizedEvent::SizedEvent( const MSG& msg ) :
	size(Point::fromLParam(msg.lParam)),
	isMaximized(msg.wParam == SIZE_MAXIMIZED),
	isMinimized(msg.wParam == SIZE_MINIMIZED),
	isRestored(msg.wParam == SIZE_RESTORED)
{
}

MouseEvent::MouseEvent(const MSG& msg) {
	POINT pt = { GET_X_LPARAM( msg.lParam ), GET_Y_LPARAM( msg.lParam ) };
	::ClientToScreen(msg.hwnd, &pt);

	pos = ScreenCoordinate(Point(pt));

	DWORD keys;
	if(msg.message == WM_XBUTTONDOWN || msg.message == WM_XBUTTONUP || msg.message == WM_XBUTTONDBLCLK) {
		keys = GET_KEYSTATE_WPARAM(msg.wParam);

		DWORD button = GET_XBUTTON_WPARAM(msg.wParam);
		ButtonPressed = (
			XBUTTON1 & button ? MouseEvent::X1 : (
				XBUTTON2 & button ? MouseEvent::X2 : MouseEvent::OTHER
			)
		);
	} else {
		keys = static_cast<DWORD>(msg.wParam);

		// These might be an issue when porting to Windows CE since CE does only support LEFT and RIGHT (or something...)
		ButtonPressed = (
			MK_LBUTTON & keys ? MouseEvent::LEFT : (
				MK_RBUTTON & keys ? MouseEvent::RIGHT : (
					MK_MBUTTON & keys ? MouseEvent::MIDDLE : (
						MK_XBUTTON1 & keys ? MouseEvent::X1 : (
							MK_XBUTTON2 & keys ? MouseEvent::X2 : MouseEvent::OTHER
						)
					)
				)
			)
		);
	}

	isControlPressed = keys & MK_CONTROL;
	isShiftPressed = keys & MK_SHIFT;
}

PointerEvent::PointerEvent(const MSG& msg) :
	id(GET_POINTERID_WPARAM(msg.wParam)),
	type(Unknown),
	pos(Point(GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam))),
	flags(HIWORD(msg.wParam)),
	wheelDelta(0),
	infoAvailable(false),
	touchInfoAvailable(false),
	penInfoAvailable(false),
	primary(false),
	inContact(false),
	canceled((flags & POINTER_MESSAGE_FLAG_CANCELED) != 0),
	info(),
	touchInfo(),
	penInfo()
{
	typedef BOOL (WINAPI *GetPointerTypeFunction)(UINT32, POINTER_INPUT_TYPE*);
	typedef BOOL (WINAPI *GetPointerInfoFunction)(UINT32, POINTER_INFO*);
	typedef BOOL (WINAPI *GetPointerInfoHistoryFunction)(UINT32, UINT32*, POINTER_INFO*);
	typedef BOOL (WINAPI *GetPointerTouchInfoFunction)(UINT32, POINTER_TOUCH_INFO*);
	typedef BOOL (WINAPI *GetPointerTouchInfoHistoryFunction)(UINT32, UINT32*, POINTER_TOUCH_INFO*);
	typedef BOOL (WINAPI *GetPointerPenInfoFunction)(UINT32, POINTER_PEN_INFO*);
	typedef BOOL (WINAPI *GetPointerPenInfoHistoryFunction)(UINT32, UINT32*, POINTER_PEN_INFO*);

	static auto getPointerType = user32Function<GetPointerTypeFunction>("GetPointerType");
	static auto getPointerInfo = user32Function<GetPointerInfoFunction>("GetPointerInfo");
	static auto getPointerInfoHistory =
		user32Function<GetPointerInfoHistoryFunction>("GetPointerInfoHistory");
	static auto getPointerTouchInfo =
		user32Function<GetPointerTouchInfoFunction>("GetPointerTouchInfo");
	static auto getPointerTouchInfoHistory =
		user32Function<GetPointerTouchInfoHistoryFunction>("GetPointerTouchInfoHistory");
	static auto getPointerPenInfo =
		user32Function<GetPointerPenInfoFunction>("GetPointerPenInfo");
	static auto getPointerPenInfoHistory =
		user32Function<GetPointerPenInfoHistoryFunction>("GetPointerPenInfoHistory");

	POINTER_INPUT_TYPE nativeType = 0;
	if(getPointerType && getPointerType(id, &nativeType)) {
		type = static_cast<Type>(nativeType);
	}

	if(getPointerInfo && getPointerInfo(id, &info)) {
		infoAvailable = true;
		type = static_cast<Type>(info.pointerType);
		flags = info.pointerFlags;
		pos = ScreenCoordinate(Point(info.ptPixelLocation));
		wheelDelta = info.InputData;
		primary = (info.pointerFlags & POINTER_FLAG_PRIMARY) != 0;
		inContact = (info.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
		canceled = (info.pointerFlags & POINTER_FLAG_CANCELED) != 0;

		if(getPointerInfoHistory && info.historyCount > 1 &&
			info.historyCount <= maxPointerHistory) {
			auto count = info.historyCount;
			history.resize(count);
			if(!getPointerInfoHistory(id, &count, history.data())) {
				history.clear();
			} else {
				history.resize(count);
			}
		}
	} else {
		primary = (flags & POINTER_MESSAGE_FLAG_PRIMARY) != 0;
		inContact = (flags & POINTER_MESSAGE_FLAG_INCONTACT) != 0;
	}

	if(type == Touch && getPointerTouchInfo &&
		getPointerTouchInfo(id, &touchInfo)) {
		touchInfoAvailable = true;
		if(getPointerTouchInfoHistory && infoAvailable && info.historyCount > 1 &&
			info.historyCount <= maxPointerHistory) {
			auto count = info.historyCount;
			touchHistory.resize(count);
			if(!getPointerTouchInfoHistory(id, &count, touchHistory.data())) {
				touchHistory.clear();
			} else {
				touchHistory.resize(count);
			}
		}
	}

	if(type == Pen && getPointerPenInfo && getPointerPenInfo(id, &penInfo)) {
		penInfoAvailable = true;
		if(getPointerPenInfoHistory && infoAvailable && info.historyCount > 1 &&
			info.historyCount <= maxPointerHistory) {
			auto count = info.historyCount;
			penHistory.resize(count);
			if(!getPointerPenInfoHistory(id, &count, penHistory.data())) {
				penHistory.clear();
			} else {
				penHistory.resize(count);
			}
		}
	}
}

TouchEvent::TouchEvent(const MSG& msg) :
	valid(false)
{
	const auto count = static_cast<UINT>(LOWORD(msg.wParam));
	if(!count) {
		return;
	}

	std::vector<TOUCHINPUT> inputs(count);
	if(!::GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(msg.lParam), count,
		inputs.data(), sizeof(TOUCHINPUT))) {
		return;
	}

	points.reserve(inputs.size());
	for(const auto& input: inputs) {
		TouchPoint point;
		point.id = input.dwID;
		point.pos = ScreenCoordinate(
			Point(TOUCH_COORD_TO_PIXEL(input.x), TOUCH_COORD_TO_PIXEL(input.y)));
		point.contact = touchContactRect(input);
		point.flags = input.dwFlags;
		point.mask = input.dwMask;
		point.time = input.dwTime;
		point.extraInfo = input.dwExtraInfo;
		point.down = (input.dwFlags & TOUCHEVENTF_DOWN) != 0;
		point.up = (input.dwFlags & TOUCHEVENTF_UP) != 0;
		point.move = (input.dwFlags & TOUCHEVENTF_MOVE) != 0;
		point.primary = (input.dwFlags & TOUCHEVENTF_PRIMARY) != 0;
		point.palm = (input.dwFlags & TOUCHEVENTF_PALM) != 0;
		point.pen = (input.dwFlags & TOUCHEVENTF_PEN) != 0;
		point.hasContact = (input.dwMask & TOUCHINPUTMASKF_CONTACTAREA) != 0;
		point.raw = input;
		points.push_back(point);
	}
	valid = true;
}

GestureEvent::GestureEvent(const MSG& msg) :
	valid(false),
	type(Begin),
	pos(Point()),
	flags(0),
	instanceId(0),
	sequenceId(0),
	arguments(0),
	info()
{
	info.cbSize = sizeof(info);
	if(!::GetGestureInfo(reinterpret_cast<HGESTUREINFO>(msg.lParam), &info)) {
		return;
	}

	valid = true;
	type = static_cast<Type>(info.dwID);
	pos = ScreenCoordinate(Point(info.ptsLocation.x, info.ptsLocation.y));
	flags = info.dwFlags;
	instanceId = info.dwInstanceID;
	sequenceId = info.dwSequenceID;
	arguments = info.ullArguments;

	if(info.cbExtraArgs) {
		extraArgs.resize(info.cbExtraArgs);
		if(!::GetGestureExtraArgs(reinterpret_cast<HGESTUREINFO>(msg.lParam),
			info.cbExtraArgs, extraArgs.data())) {
			extraArgs.clear();
		}
	}
}

GestureNotifyEvent::GestureNotifyEvent(const MSG& msg) :
	valid(false),
	pos(Point()),
	flags(0),
	instanceId(0),
	info()
{
	auto source = reinterpret_cast<GESTURENOTIFYSTRUCT*>(msg.lParam);
	if(!source || source->cbSize < sizeof(GESTURENOTIFYSTRUCT)) {
		return;
	}

	info = *source;
	valid = true;
	pos = ScreenCoordinate(Point(info.ptsLocation.x, info.ptsLocation.y));
	flags = info.dwFlags;
	instanceId = info.dwInstanceID;
}

}
