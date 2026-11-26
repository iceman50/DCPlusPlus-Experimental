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

#include <dwt/widgets/Spinner.h>

#include <dwt/util/check.h>

#include <algorithm>
#include <limits>

#include "AppearanceDraw.h"

namespace dwt {

const TCHAR Spinner::windowClass[] = UPDOWN_CLASS;

Spinner::Seed::Seed(int minValue_, int maxValue_, Control* buddy_) :
BaseType::Seed(WS_CHILD | UDS_ARROWKEYS | UDS_NOTHOUSANDS),
minValue(minValue_),
maxValue(maxValue_),
buddy(buddy_)
{
	if(buddy || (style & UDS_AUTOBUDDY))
		style |= UDS_ALIGNRIGHT | UDS_SETBUDDYINT;
}

Spinner::Spinner(Widget * parent) :
BaseType(parent, ChainingDispatcher::superClass<Spinner>())
{
}

void Spinner::create(const Seed& cs) {
	BaseType::create(cs);
	setRange(cs.minValue, cs.maxValue);
	if(cs.buddy)
		assignBuddy(cs.buddy);
}

void Spinner::setRange(int minimum, int maximum) {
	sendMessage(UDM_SETRANGE32, static_cast<WPARAM>(minimum), static_cast<LPARAM>(maximum));
}

std::pair<int, int> Spinner::getRange() const {
	int minimum = 0;
	int maximum = 0;
	sendMessage(UDM_GETRANGE32, reinterpret_cast<WPARAM>(&minimum),
		reinterpret_cast<LPARAM>(&maximum));
	return std::make_pair(minimum, maximum);
}

void Spinner::assignBuddy(Control* buddy) {
	dwtassert(buddy && buddy->handle() && buddy->getParent() == getParent(), "A spinner and its buddy must have the same parent");
	assignBuddy_(buddy);
	buddy->onSized([this](const SizedEvent&) { handleSized(); });

	// check range when the value of the buddy control changes - this is automatic the other way round only
	buddy->onCommand([this]() { this->setValue(this->getValue()); }, EN_CHANGE);

	// initial sync with the buddy
	setValue(getValue());
}

Control* Spinner::getBuddy() const {
	return hwnd_cast<Control*>(reinterpret_cast<HWND>(sendMessage(UDM_GETBUDDY)));
}

int Spinner::getValue() {
	return static_cast<int>(sendMessage(UDM_GETPOS32));
}

bool Spinner::tryGetValue(int& value) const {
	BOOL error = FALSE;
	value = static_cast<int>(sendMessage(UDM_GETPOS32, 0,
		reinterpret_cast<LPARAM>(&error)));
	return error == FALSE;
}

int Spinner::setValue(int v) {
	return static_cast<int>(sendMessage(UDM_SETPOS32, 0, v));
}

int Spinner::getBase() const {
	return static_cast<int>(sendMessage(UDM_GETBASE));
}

int Spinner::setBase(int base) {
	return static_cast<int>(sendMessage(UDM_SETBASE, base));
}

std::vector<UDACCEL> Spinner::getAcceleration() const {
	const auto countResult = sendMessage(UDM_GETACCEL);
	if(countResult <= 0) {
		return std::vector<UDACCEL>();
	}
	const auto count = static_cast<size_t>(countResult);
	std::vector<UDACCEL> result(count);
	if(count) {
		const auto copied = static_cast<size_t>(sendMessage(UDM_GETACCEL,
			count, reinterpret_cast<LPARAM>(result.data())));
		result.resize((std::min)(count, copied));
	}
	return result;
}

bool Spinner::setAcceleration(const std::vector<UDACCEL>& acceleration) {
	return !acceleration.empty() &&
		acceleration.size() <= static_cast<size_t>((std::numeric_limits<UINT>::max)()) &&
		sendMessage(UDM_SETACCEL,
		acceleration.size(), reinterpret_cast<LPARAM>(acceleration.data())) != FALSE;
}

bool Spinner::getUnicodeFormat() const {
	return sendMessage(UDM_GETUNICODEFORMAT) != FALSE;
}

bool Spinner::setUnicodeFormat(bool unicode) {
	return sendMessage(UDM_SETUNICODEFORMAT, unicode ? TRUE : FALSE) != FALSE;
}

void Spinner::onUpdate(UpdateF f) {
	setCallback(Message(WM_NOTIFY, UDN_DELTAPOS), [f](const MSG& msg, LRESULT& ret) -> bool {
		auto lpnmud = reinterpret_cast<LPNMUPDOWN>(msg.lParam);
		ret = !f(lpnmud->iPos, lpnmud->iDelta);
		return true;
	});
}

void Spinner::handleSized() {
	// the widget will be resized to accomodate the spinner - avoid recursion.
	static bool recursion = false;
	if(recursion)
		return;
	recursion = true;

	assignBuddy_(getBuddy());

	recursion = false;
}

void Spinner::assignBuddy_(Control* buddy) {
	sendMessage(UDM_SETBUDDY, reinterpret_cast<WPARAM>(buddy->handle()));
}

bool Spinner::handleMessage(const MSG& msg, LRESULT& retVal) {
	const bool handled = BaseType::handleMessage(msg, retVal);
	if(handled || !isManualAppearance() ||
		getAppearancePolicy() == AppearancePolicy::Native) {
		return handled;
	}

	switch(msg.message) {
	case WM_ERASEBKGND:
		retVal = TRUE;
		return true;

	case WM_PAINT:
		{
			RECT update = { };
			if(!::GetUpdateRect(handle(), &update, FALSE)) {
				::GetClientRect(handle(), &update);
			}
			const Rectangle paint(update);
			BufferedCanvas<PaintCanvas> canvas(this, paint);
			appearance_detail::drawSpinner(*this, handle(), canvas,
				getAppearance().getPalette());
			canvas.blast(paint);
			retVal = 0;
			return true;
		}

	case WM_PRINTCLIENT:
		if(msg.wParam) {
			FreeCanvas canvas(reinterpret_cast<HDC>(msg.wParam));
			appearance_detail::drawSpinner(*this, handle(), canvas,
				getAppearance().getPalette());
			retVal = 0;
			return true;
		}
		break;
	}
	return false;
}

}
