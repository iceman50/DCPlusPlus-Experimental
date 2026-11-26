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

#include <dwt/widgets/ModalDialog.h>

#include <dwt/Application.h>
#include <dwt/DWTException.h>
#include <dwt/util/win32/Dpi.h>

namespace dwt {

namespace {

class EnableWindowGuard {
public:
	explicit EnableWindowGuard(HWND window_) :
		window(window_), restoreEnabled(false)
	{
		if(window && ::IsWindow(window) && ::IsWindowEnabled(window)) {
			::EnableWindow(window, FALSE);
			restoreEnabled = !::IsWindowEnabled(window);
		}
	}

	~EnableWindowGuard() {
		restore();
	}

	bool disabled() const {
		return restoreEnabled;
	}

	void restore() noexcept {
		if(restoreEnabled && window && ::IsWindow(window)) {
			::EnableWindow(window, TRUE);
		}
		restoreEnabled = false;
	}

private:
	HWND window;
	bool restoreEnabled;
};

class DestroyWindowGuard {
public:
	explicit DestroyWindowGuard(HWND window_) : window(window_) { }
	~DestroyWindowGuard() {
		if(window && ::IsWindow(window)) {
			::DestroyWindow(window);
		}
	}

private:
	HWND window;
};

}

const TCHAR* ModalDialog::windowClass = WC_DIALOG;

ModalDialog::Seed::Seed(const Point& size, DWORD styles_) :
BaseType::Seed(tstring(), styles_ | WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE)
{
	location.size = size;
}

ModalDialog::ModalDialog(Widget* parent) :
BaseType(parent, ChainingDispatcher::superClass<ModalDialog>()),
quit(false),
ret(0),
filterRegistered(false)
{
	onClosing([this] { return this->ThisType::defaultClosing(); });
	onDestroy([this] {
		quit.store(true, std::memory_order_release);
		if(Application::isInitialized()) {
			Application::instance().wake();
		}
	});

	filterIter = dwt::Application::instance().addFilter([this](MSG& msg) { return this->ThisType::filter(msg); });
	filterRegistered = true;
}

ModalDialog::~ModalDialog() {
	if(filterRegistered && dwt::Application::isInitialized()) {
		dwt::Application::instance().removeFilter(filterIter);
	}
}

void ModalDialog::create(const Seed& cs) {
	Seed cs2 = cs;
	const auto parent = getParent();
	const auto targetDpi = parent ? parent->getDpi() :
		util::win32::getDpi(nullptr);
	cs2.location.size = util::win32::scale(
		cs.location.size, targetDpi, util::win32::defaultDpi);
	quit.store(false, std::memory_order_relaxed);
	ret.store(0, std::memory_order_relaxed);

	if((cs.style & DS_CONTEXTHELP) == DS_CONTEXTHELP) {
		cs2.exStyle |= WS_EX_CONTEXTHELP;
	}

	cs2.style &= ~WS_VISIBLE;

	BaseType::create(cs2);

	::SetLastError(ERROR_SUCCESS);
	const auto previous = ::SetWindowLongPtr(
		handle(), DWLP_DLGPROC, reinterpret_cast<LONG_PTR>(dialogProc));
	if(!previous && ::GetLastError() != ERROR_SUCCESS) {
		throw Win32Exception("Unable to install the dialog procedure");
	}

	HWND hwndDefaultFocus = GetNextDlgTabItem(handle(), NULL, FALSE);
	if (sendMessage(WM_INITDIALOG, (WPARAM)hwndDefaultFocus)) {
		if(hwndDefaultFocus) {
			sendMessage(WM_NEXTDLGCTL, (WPARAM)hwndDefaultFocus, TRUE);
		}
	}
}

int ModalDialog::show() {
	auto parent = getParent();
	auto root = parent ? parent->getRoot() : nullptr;
	const auto dialogWindow = handle();
	const auto rootWindow = root ? root->handle() : nullptr;
	DestroyWindowGuard destroyDialog(dialogWindow);
	if(!dialogWindow || !::IsWindow(dialogWindow) ||
		!rootWindow || !::IsWindow(rootWindow) ||
		!::IsWindowEnabled(rootWindow))
	{
		return IDCANCEL;
	}

	EnableWindowGuard enableRoot(rootWindow);
	if(!enableRoot.disabled()) {
		return IDCANCEL;
	}

	setVisible(true);

	while(!quit.load(std::memory_order_acquire)) {
		if(!::IsWindow(dialogWindow) || !::IsWindow(rootWindow) ||
			!Application::isInitialized())
		{
			quit.store(true, std::memory_order_release);
			break;
		}
		if(!Application::instance().dispatch() &&
			!Application::instance().sleep())
		{
			quit.store(true, std::memory_order_release);
		}
	}

	enableRoot.restore();
	return ret.load(std::memory_order_relaxed);
}

bool ModalDialog::filter(MSG& msg) {
	const auto window = handle();
	if(!quit.load(std::memory_order_acquire) &&
		window && ::IsWindow(window) && ::IsDialogMessage(window, &msg))
	{
		return true;
	}
	return false;
}

void ModalDialog::kill() {
	// do nothing
}
}
