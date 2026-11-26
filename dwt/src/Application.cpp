/*
 DC++ Widget Toolkit

 Copyright (c) 2007-2013, Jacek Sieka
 Copyright (C) 2026 iceman50

 SmartWin++

 Copyright (c) 2005 Thomas Hansen

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

#include <dwt/Application.h>

#include <dwt/tstring.h>
#include <dwt/DWTException.h>
#include <dwt/util/check.h>
#include <dwt/util/win32/Dpi.h>
#include <dwt/widgets/Control.h>
#include <algorithm>
#include <assert.h>
#include <vector>

namespace dwt {

Application* Application::itsInstance = 0;
HANDLE Application::itsMutex = 0;

// Application implementation

/** Initializes the runtime for SmartWin++
 Typically only called by WinMain or DllMain.
 */
void Application::init() {
#ifndef DWT_SHARED
	util::win32::enablePerMonitorDpiAwareness();
#endif
	if(itsInstance) {
		return;
	}
	itsInstance = new Application();

	// Initializing Common Controls...
	INITCOMMONCONTROLSEX init = {
		sizeof(INITCOMMONCONTROLSEX),
		ICC_ANIMATE_CLASS | ICC_BAR_CLASSES | ICC_COOL_CLASSES | ICC_DATE_CLASSES | ICC_HOTKEY_CLASS |
		ICC_INTERNET_CLASSES | ICC_LINK_CLASS | ICC_LISTVIEW_CLASSES | ICC_NATIVEFNTCTL_CLASS | ICC_PAGESCROLLER_CLASS |
		ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_USEREX_CLASSES
	};

	::InitCommonControlsEx(&init);
}

Application::Application() :
	itsCmdShow(0),
	appearance(new Appearance),
	quit(false),
	threadId(::GetCurrentThreadId())
{
}

Application::~Application() {
}

const CommandLine& Application::getCommandLine() const {
	return itsCmdLine;
}

bool Application::addWaitEvent(HANDLE hWaitEvent, const Application::Callback& pSignal) {
	// in case the maximum number of objects is already achieved return false
	if (itsVHEvents.size() >= MAXIMUM_WAIT_OBJECTS - 1)
		return false;

	if (hWaitEvent != INVALID_HANDLE_VALUE) {
		itsVSignals.push_back(pSignal);
		itsVHEvents.push_back(hWaitEvent);
	}
	return true;
}

void Application::removeWaitEvent(HANDLE hWaitEvent) {
	if (hWaitEvent != INVALID_HANDLE_VALUE) {
		std::vector<Callback>::iterator pSig;
		std::vector<HANDLE>::iterator pH;
		for (pSig = itsVSignals.begin(), pH = itsVHEvents.begin(); pSig != itsVSignals.end(); pSig++, pH++) {
			if (*pH == hWaitEvent) {
				itsVSignals.erase(pSig);
				itsVHEvents.erase(pH);
				break;
			}
		}
	}
}

void Application::uninit() {
	if(!itsInstance) {
		return;
	}
	delete itsInstance;
	itsInstance = 0;
	if (itsMutex) {
		::CloseHandle(itsMutex);
		itsMutex = 0;
	}
}

Application& Application::instance() {
	assert(itsInstance);
	if(!itsInstance) {
		throw DWTException("DWT application runtime is not initialized");
	}
	return *itsInstance;
}

bool Application::isInitialized() noexcept {
	return itsInstance != nullptr;
}

HMODULE Application::getModuleHandle() noexcept {
	HMODULE module = nullptr;
	if(::GetModuleHandleEx(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCTSTR>(&itsInstance), &module))
	{
		return module;
	}
	return ::GetModuleHandle(nullptr);
}

namespace {

tstring moduleFileName(HMODULE module) {
	std::vector<TCHAR> buffer(1024);
	for(;;) {
		::SetLastError(ERROR_SUCCESS);
		const auto length = ::GetModuleFileName(
			module, buffer.data(), static_cast<DWORD>(buffer.size()));
		if(length == 0) {
			return tstring();
		}
		if(length < buffer.size()) {
			return tstring(buffer.data(), length);
		}
		if(buffer.size() >= 32768) {
			return tstring();
		}
		buffer.resize(std::min<size_t>(buffer.size() * 2, 32768));
	}
}

}

tstring Application::getModulePath() const {
	auto fileName = moduleFileName(getModuleHandle());
	const auto separator = fileName.find_last_of(_T("\\/"));
	return separator == tstring::npos ? tstring() : fileName.substr(0, separator + 1);
}

tstring Application::getModuleFileName() const {
	return moduleFileName(getModuleHandle());
}

void Application::run() {
	while(!quit) {
		if(!dispatch()) {
			sleep();
		}
	}
}

bool Application::sleep() {
	if (quit) {
		return false;
	}

	while(true) {
		size_t n = itsVHEvents.size();

		DWORD ret = ::MsgWaitForMultipleObjectsEx(static_cast<DWORD> (n), itsVHEvents.empty() ? 0 : &itsVHEvents[0],
		    INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

		if (ret == WAIT_OBJECT_0 + n) {
			return true;
		} else if (ret < WAIT_OBJECT_0 + itsVHEvents.size()) {
			// the wait event was signaled by Windows
			// signal its handlers
			itsVSignals[ret - WAIT_OBJECT_0]();
		} else {
			throw Win32Exception("Unexpected return value from MsgWaitForMultipleObjectsEx");
		}
	}
}

bool Application::dispatch() {
	MSG msg { 0 };
	if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) == 0) {
		return dispatchAsync();
	}

	if (msg.message == WM_QUIT) {
		// Make sure outer message loops see it...
		::PostQuitMessage(static_cast<int>(msg.wParam));
		quit = true;
		return false;
	}

	// heavily inspired by SWT (Display.java - filterMessage & findControl)
	if(msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) {
		for(auto& i: keyboardFilters) {
			if(i(msg)) {
				return true;
			}
		}

		HWND hwnd = msg.hwnd;
		HWND owner = 0;
		while(hwnd && hwnd != owner) {
			// make sure the window is ours or hwnd_cast might crash.
			TCHAR className[128];
			if(::GetClassName(hwnd, className, 128) && Dispatcher::isRegistered(className)) {
				Control* control = hwnd_cast<Control*>(hwnd);
				if(control && control->filter(msg)) {
					return true;
				}
			}
			owner = ::GetWindow(hwnd, GW_OWNER);
			hwnd = ::GetParent(hwnd);
		}
	}

	for(auto& i: filters) {
		if(i(msg)) {
			return true;
		}
	}

	::TranslateMessage(&msg);
	::DispatchMessage(&msg);

	return true;
}

bool Application::processMessages() {
	while(!quit && dispatch()) {
	}

	return !quit;
}

void Application::wake() {
	::PostThreadMessage(threadId, WM_NULL, 0, 0);
}

int Application::getCmdShow() const {
	return itsCmdShow;
}

void Application::setCmdShow(int cmdShow) {
	itsCmdShow = cmdShow;
}

bool Application::dispatchAsync() {
	Callback callback;
	{
		std::lock_guard<std::mutex> lock(tasksMutex);
		if(tasks.empty()) {
			return false;
		}
		callback = std::move(tasks.front());
		tasks.pop();
	}

	/* Queued callbacks start in FIFO order, also inside a nested message loop that one of them
	 * runs (a modal dialog opened from a callback, or processMessages()). Holding them back until
	 * the outer callback returned would stall every window behind that dialog, and the dialog
	 * itself where its controls rely on queued callbacks. */
	callback();
	return true;
}

#ifndef DWT_SHARED

void Application::callAsync(const Callback& callback) {
	//We're going to really need to stress test this and dispatchAsync...
	{
		std::lock_guard<std::mutex> lock(tasksMutex);
		tasks.push(callback);
	}
	wake();
}

#else

// async calls don't work when we aren't in charge of the message loop - run them synchronously.
void Application::callAsync(const Callback& callback) {
	callback();
}

#endif

Application::FilterIter Application::addFilter(const FilterFunction& f) {
	return filters.insert(filters.end(), f);
}

void Application::removeFilter(const FilterIter& i) {
	filters.erase(i);
}

Application::FilterIter Application::addKeyboardFilter(const FilterFunction& f) {
	return keyboardFilters.insert(keyboardFilters.end(), f);
}

void Application::removeKeyboardFilter(const FilterIter& i) {
	keyboardFilters.erase(i);
}

} // namespace dwt

#ifndef DWT_SHARED

namespace { bool ensureCPUSupport() {
	// Ensure the CPU supports SSE3.
	if(!::IsProcessorFeaturePresent(PF_SSE3_INSTRUCTIONS_AVAILABLE)) {
		::MessageBox(nullptr, _T(
			"Your processor does not support the SSE3 instruction set;\r\n"
			"this program cannot start."
		), _T("SSE3 CPU required."), MB_OK | MB_ICONERROR);
		return false;
	}
	return true;
} }

extern int dwtMain(dwt::Application& app);

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	if(!ensureCPUSupport()) {
		return 1;
	}

	dwt::Application::init();

	auto hr = ::OleInitialize(nullptr);
	if(FAILED(hr))
		return hr;

	dwt::Application::instance().setCmdShow(nCmdShow);

	int ret = dwtMain(dwt::Application::instance()); // Call library user's startup function.

	::OleUninitialize();

	dwt::Application::uninit();

	return ret;
}

#endif
