// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/Application.h>
#include <dwt/Taskbar.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Container.h>
#include <dwmapi.h>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <algorithm>

namespace {
void check(bool value, const char* message) {
	if(!value) throw std::runtime_error(message);
}
struct Calls {
	std::unordered_set<HWND> registered, buttons;
	std::vector<HWND> order;
	HWND active = nullptr;
	unsigned registrations = 0, unregistrations = 0, deletions = 0;
	bool invalidHandle = false, failRegistration = false, nativeFailure = false;
	std::function<void()> reenter;
	void record(HRESULT result, const char* method) {
		if(FAILED(result)) {
			if(!nativeFailure) std::fprintf(stderr, "%s failed: 0x%08lx (%zu live tabs)\n",
				method, static_cast<unsigned long>(result), registered.size());
			nativeFailure = true;
		}
	}
};

// Record shell calls without requiring Explorer in automated runs. The opt-in
// desktop run forwards the same calls to the real Windows taskbar interface.
class RecordingTaskbar final : public ITaskbarList3 {
public:
	RecordingTaskbar(Calls& calls, ITaskbarList3* native) : calls(calls), native(native) { }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** result) override { *result = nullptr; return E_NOINTERFACE; }
	ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
	ULONG STDMETHODCALLTYPE Release() override {
		const auto remaining = --references;
		if(!remaining) { if(native) native->Release(); delete this; }
		return remaining;
	}
	HRESULT STDMETHODCALLTYPE HrInit() override { return S_OK; }
	HRESULT STDMETHODCALLTYPE AddTab(HWND) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE DeleteTab(HWND window) override {
		calls.invalidHandle |= !IsWindow(window);
		++calls.deletions;
		calls.buttons.erase(window);
		const auto result = native ? native->DeleteTab(window) : S_OK;
		calls.record(result, "DeleteTab");
		return result;
	}
	HRESULT STDMETHODCALLTYPE ActivateTab(HWND) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetActiveAlt(HWND) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE MarkFullscreenWindow(HWND, BOOL) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetProgressValue(HWND, ULONGLONG, ULONGLONG) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetProgressState(HWND, TBPFLAG) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE RegisterTab(HWND tab, HWND parent) override {
		if(calls.failRegistration) return E_FAIL;
		++calls.registrations;
		calls.registered.insert(tab);
		calls.buttons.insert(tab);
		const auto result = native ? native->RegisterTab(tab, parent) : S_OK;
		calls.record(result, "RegisterTab");
		return result;
	}
	HRESULT STDMETHODCALLTYPE UnregisterTab(HWND tab) override {
		calls.invalidHandle |= !IsWindow(tab);
		++calls.unregistrations;
		calls.registered.erase(tab);
		if(calls.reenter) { auto callback = std::move(calls.reenter); calls.reenter = {}; callback(); }
		const auto result = native ? native->UnregisterTab(tab) : S_OK;
		calls.record(result, "UnregisterTab");
		return result;
	}
	HRESULT STDMETHODCALLTYPE SetTabOrder(HWND tab, HWND before) override {
		calls.order.erase(std::remove(calls.order.begin(), calls.order.end(), tab), calls.order.end());
		calls.order.insert(std::find(calls.order.begin(), calls.order.end(), before), tab);
		const auto result = native ? native->SetTabOrder(tab, before) : S_OK;
		calls.record(result, "SetTabOrder");
		return result;
	}
	HRESULT STDMETHODCALLTYPE SetTabActive(HWND tab, HWND parent, DWORD flags) override {
		calls.active = tab;
		const auto result = native ? native->SetTabActive(tab, parent, flags) : S_OK;
		calls.record(result, "SetTabActive");
		return result;
	}
	HRESULT STDMETHODCALLTYPE ThumbBarAddButtons(HWND, UINT, LPTHUMBBUTTON) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE ThumbBarUpdateButtons(HWND, UINT, LPTHUMBBUTTON) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE ThumbBarSetImageList(HWND, HIMAGELIST) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetOverlayIcon(HWND, HICON, LPCWSTR) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetThumbnailTooltip(HWND, LPCWSTR) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE SetThumbnailClip(HWND, RECT*) override { return S_OK; }
private:
	Calls& calls;
	ITaskbarList3* native;
	ULONG references = 1;
};

class Host : public dwt::Taskbar {
public:
	Host(dwt::Window* window, Calls& calls, bool shell) {
		initTaskbar(window);
		check(!shell || taskbar, "Windows taskbar interface unavailable");
		if(taskbar4) { taskbar4->Release(); taskbar4 = nullptr; }
		auto native = taskbar;
		if(!shell && native) { native->Release(); native = nullptr; }
		taskbar = new RecordingTaskbar(calls, native);
	}
	using Taskbar::addToTaskbar;
	using Taskbar::removeFromTaskbar;
	using Taskbar::moveOnTaskbar;
	using Taskbar::setActiveOnTaskbar;
private:
	void setActive(dwt::ContainerPtr) override { }
};
}

int dwtMain(dwt::Application& app) {
	try {
		const bool shell = std::getenv("DCPP_TEST_TASKBAR_SHELL") != nullptr;
		if(shell) dwt::Taskbar::setCurrentAppId(L"DCPlusPlus.TaskbarLifecycleTest");
		auto window = new dwt::Window;
		dwt::Window::Seed seed;
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		window->setText(L"DC++ taskbar lifecycle test");
		window->resize(dwt::Rectangle(-32000, -32000, 600, 300));
		Calls calls;
		Host host(window, calls, shell);
		bool ready = false;
		const auto buttonCreated = RegisterWindowMessage(L"TaskbarButtonCreated");
		window->onRaw([&](WPARAM, LPARAM) {
			host.taskbarButtonCreated();
			ready = true;
			return LRESULT(0);
		}, dwt::Message(buttonCreated));
		const auto addPage = [&] {
			auto page = dwt::WidgetCreator<dwt::Container>::create(window);
			page->setText(L"Synthetic file list");
			host.addToTaskbar(page);
			return page;
		};
		// Match the client: pages open before the main window is shown.
		auto first = addPage();
		auto second = addPage();
		auto closedBeforeReady = addPage();
		DestroyWindow(closedBeforeReady->handle());
		host.moveOnTaskbar(second, first);
		host.setActiveOnTaskbar(first);
		check(calls.registrations == 0 && calls.unregistrations == 0 && calls.deletions == 0,
			"Preview shell calls occurred before TaskbarButtonCreated");
		if(shell) {
			ShowWindow(window->handle(), SW_SHOWNOACTIVATE);
			const auto start = GetTickCount64();
			while(!ready && GetTickCount64() - start < 5000) { if(!app.dispatch()) Sleep(1); }
			check(ready, "Explorer did not create the test taskbar button");
		} else {
			SendMessage(window->handle(), buttonCreated, 0, 0);
		}
		check(calls.registered.size() == 2 && calls.order.size() == 2, "Startup pages lost their previews");
		check(calls.active == calls.order.back(), "Startup selection or reordered tabs were lost");
		if(!shell) {
			const auto order = calls.order;
			const auto active = calls.active;
			calls.registered.clear(); calls.buttons.clear(); calls.order.clear(); calls.active = nullptr;
			SendMessage(window->handle(), buttonCreated, 0, 0);
			check(calls.registered.size() == 2 && calls.order == order && calls.active == active,
				"Taskbar recreation lost previews, order or selection");
			// Registrations before the simulated Explorer restart no longer exist.
			calls.registrations -= 2;
		}
		DestroyWindow(first->handle());
		DestroyWindow(second->handle());
		// Explorer caps the number of ordered previews (SetTabOrder returns
		// ERROR_INSUFFICIENT_BUFFER at 30 here). Exercise 300 real closes in
		// smaller batches; keep the 100-page stress batches in the recorder.
		const int batchSize = shell ? 20 : 100;
		for(int pass = 0; pass < 300 / batchSize; ++pass) {
			std::vector<dwt::Container*> pages;
			for(int i = 0; i < batchSize; ++i) pages.push_back(addPage());
			check(calls.registered.size() == static_cast<size_t>(batchSize), "Missing preview registrations");
			const auto proxies = calls.registered;
			for(auto page: pages) {
				// Mix normal tab removal with page destruction before tab-strip cleanup.
				if(pass == 0) host.removeFromTaskbar(page);
				DestroyWindow(page->handle());
			}
			check(calls.registered.empty() && calls.buttons.empty(), "Closed pages left shell entries");
			for(auto proxy: proxies) check(!IsWindow(proxy), "Closed page retained its proxy HWND");
		}
		auto page = addPage();
		const auto beforeDuplicate = calls.registrations;
		host.addToTaskbar(page);
		check(calls.registrations == beforeDuplicate, "Duplicate registration leaked a preview");
		DestroyWindow(*calls.registered.begin());
		check(calls.registered.empty() && calls.buttons.empty(), "Independent proxy destruction left shell entries");
		page->setText(L"Late title update");
		page->resize(dwt::Rectangle(0, 0, 200, 100));
		DestroyWindow(page->handle());

		page = addPage();
		page->resize(dwt::Rectangle(0, 0, 200, 100));
		unsigned prints = 0;
		page->onRaw([&](WPARAM, LPARAM) { ++prints; return LRESULT(0); }, dwt::Message(WM_PRINT));
		const auto retiringProxy = *calls.registered.begin();
		calls.reenter = [&] {
			host.removeFromTaskbar(page);
			SendMessage(retiringProxy, WM_DWMSENDICONICTHUMBNAIL, 0, MAKELPARAM(128, 128));
			SendMessage(retiringProxy, WM_DWMSENDICONICLIVEPREVIEWBITMAP, 0, 0);
		};
		host.removeFromTaskbar(page);
		check(prints == 0, "A retiring proxy rendered the closing page");
		DestroyWindow(page->handle());
		check(calls.registered.empty() && calls.buttons.empty(), "Reentrant removal retained a preview");
		check(calls.registrations == calls.unregistrations && calls.registrations == calls.deletions,
			"Preview cleanup was duplicated or skipped");
		if(!shell) {
			calls.failRegistration = true;
			page = addPage();
			check(calls.registered.empty() && calls.buttons.empty(), "Failed registration retained a shell entry");
			calls.failRegistration = false;
			SendMessage(window->handle(), buttonCreated, 0, 0);
			check(calls.registered.size() == 1, "Failed registration was not retried when the taskbar returned");
			DestroyWindow(page->handle());
		}
		for(int i = 0; i < 10; ++i) addPage();
		DestroyWindow(window->handle());
		check(calls.registered.empty() && calls.buttons.empty(), "Owner shutdown retained previews");
		check(!calls.invalidHandle, "Preview was unregistered after its HWND was freed");
		check(!calls.nativeFailure, "Windows rejected a preview registration or cleanup call");
		std::printf("PASS: startup previews, taskbar recreation, 300 bulk closes, proxy lifetimes, late updates, reentrancy and owner shutdown (%s).\n", shell ? "Windows taskbar" : "recorded taskbar");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
