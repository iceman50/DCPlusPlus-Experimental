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

#include <dwt/Taskbar.h>
#include <dwt/resources/DibBitmap.h>

#include <objectarray.h>
#include <propkey.h>
#include <propsys.h>
#include <algorithm>

#include <dwt/util/check.h>
#include <dwt/util/StringConversion.h>
#include <dwt/util/win32/ComPtr.h>
#include <dwt/util/win32/Dwm.h>
#include <dwt/util/win32/Version.h>
#include <dwt/widgets/Container.h>
#include <dwt/widgets/Window.h>

namespace dwt {

namespace {

using util::win32::ComPtr;

std::wstring toWide(const tstring& value) {
	return util::UnicodeGuaranteed::doConvert(value, util::ConversionCodepage::ANSI);
}

std::wstring modulePath() {
	std::wstring value(MAX_PATH, L'\0');
	DWORD length = ::GetModuleFileNameW(nullptr, &value[0], static_cast<DWORD>(value.size()));
	while(length == value.size()) {
		value.resize(value.size() * 2, L'\0');
		length = ::GetModuleFileNameW(nullptr, &value[0], static_cast<DWORD>(value.size()));
	}
	value.resize(length);
	return value;
}

PROPVARIANT stringVariant(const std::wstring& value) {
	PROPVARIANT variant = { };
	variant.vt = VT_LPWSTR;
	variant.pwszVal = const_cast<PWSTR>(value.c_str());
	return variant;
}

PROPVARIANT boolVariant(bool value) {
	PROPVARIANT variant = { };
	variant.vt = VT_BOOL;
	variant.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
	return variant;
}

HRESULT setStringProperty(IPropertyStore& store, REFPROPERTYKEY key,
	const std::wstring& value)
{
	auto variant = stringVariant(value);
	return store.SetValue(key, variant);
}

HRESULT setBoolProperty(IPropertyStore& store, REFPROPERTYKEY key, bool value) {
	auto variant = boolVariant(value);
	return store.SetValue(key, variant);
}

HRESULT createShellLink(const JumpListLink& item, IShellLinkW** value) {
	if(!value) {
		return E_POINTER;
	}
	*value = nullptr;

	ComPtr<IShellLinkW> link;
	auto result = ::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
		IID_IShellLinkW, reinterpret_cast<void**>(link.put()));
	if(FAILED(result)) {
		return result;
	}

	ComPtr<IPropertyStore> properties;
	result = link->QueryInterface(IID_IPropertyStore,
		reinterpret_cast<void**>(properties.put()));
	if(FAILED(result)) {
		return result;
	}

	if(item.separator) {
		result = setBoolProperty(*properties.get(), PKEY_AppUserModel_IsDestListSeparator, true);
		if(FAILED(result)) {
			return result;
		}
		result = properties->Commit();
		if(FAILED(result)) {
			return result;
		}
		*value = link.detach();
		return S_OK;
	}

	if(item.title.empty()) {
		return E_INVALIDARG;
	}

	auto path = item.path.empty() ? modulePath() : toWide(item.path);
	result = link->SetPath(path.c_str());
	if(FAILED(result)) {
		return result;
	}

	auto arguments = toWide(item.arguments);
	if(!arguments.empty()) {
		result = link->SetArguments(arguments.c_str());
		if(FAILED(result)) {
			return result;
		}
	}

	auto workingDirectory = toWide(item.workingDirectory);
	if(!workingDirectory.empty()) {
		result = link->SetWorkingDirectory(workingDirectory.c_str());
		if(FAILED(result)) {
			return result;
		}
	}

	auto iconPath = item.iconPath.empty() ? path : toWide(item.iconPath);
	if(!iconPath.empty()) {
		result = link->SetIconLocation(iconPath.c_str(), item.iconIndex);
		if(FAILED(result)) {
			return result;
		}
	}

	auto description = toWide(item.description);
	if(!description.empty()) {
		result = link->SetDescription(description.c_str());
		if(FAILED(result)) {
			return result;
		}
	}

	result = setStringProperty(*properties.get(), PKEY_Title, toWide(item.title));
	if(FAILED(result)) {
		return result;
	}

	result = properties->Commit();
	if(FAILED(result)) {
		return result;
	}

	*value = link.detach();
	return S_OK;
}

HRESULT makeObjectArray(const std::vector<JumpListLink>& items, IObjectArray** value) {
	if(!value) {
		return E_POINTER;
	}
	*value = nullptr;

	ComPtr<IObjectCollection> collection;
	auto result = ::CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr,
		CLSCTX_INPROC_SERVER, IID_IObjectCollection,
		reinterpret_cast<void**>(collection.put()));
	if(FAILED(result)) {
		return result;
	}

	for(const auto& item : items) {
		ComPtr<IShellLinkW> link;
		result = createShellLink(item, link.put());
		if(FAILED(result)) {
			return result;
		}
		result = collection->AddObject(link.get());
		if(FAILED(result)) {
			return result;
		}
	}

	return collection->QueryInterface(IID_IObjectArray, reinterpret_cast<void**>(value));
}

}

Taskbar::Taskbar() :
taskbar(0),
taskbar4(0),
window(0),
taskbarReady(false),
activeTab(nullptr)
{
}

Taskbar::~Taskbar() {
	while(!tabs.empty()) removeFromTaskbar(tabs.begin()->first);
	if(taskbar4)
		taskbar4->Release();
	if(taskbar)
		taskbar->Release();
}

void Taskbar::initTaskbar(WindowPtr window_) {
	window = window_;
	dwtassert(window, "Taskbar: no widget set");

	// Create the interface now, but defer tab registration until the shell has
	// created the owner's taskbar button. Startup pages must survive that delay.
#ifdef __GNUC__
	CLSID CLSID_TaskbarList;
	OLECHAR tbl[] = L"{56FDF344-FD6D-11d0-958A-006097C9A090}";
	CLSIDFromString(tbl, &CLSID_TaskbarList);
	IID IID_ITaskbarList3;
	OLECHAR itbl[] = L"{EA1AFB91-9E28-4B86-90E9-9E9F8A5EEFAF}";
	CLSIDFromString(itbl, &IID_ITaskbarList3);
#endif
	if(::CoCreateInstance(CLSID_TaskbarList, 0, CLSCTX_INPROC_SERVER, IID_ITaskbarList3,
		reinterpret_cast<LPVOID*>(&taskbar)) != S_OK) { taskbar = 0; }
	if(taskbar && taskbar->HrInit() != S_OK) {
			taskbar->Release();
			taskbar = 0;
	}
	if(taskbar && taskbar->QueryInterface(IID_ITaskbarList4,
		reinterpret_cast<void**>(&taskbar4)) != S_OK) {
		taskbar4 = 0;
	}
}

void Taskbar::taskbarButtonCreated() {
	if(!taskbar || !window) return;
	taskbarReady = true;
	// Explorer loses all registrations when it recreates the taskbar. Keep the
	// same proxies (and their titles/icons), restoring the current strip order.
	for(auto tab : tabOrder) {
		auto i = tabs.find(tab);
		if(i != tabs.end() && SUCCEEDED(taskbar->RegisterTab(i->second->handle(), window->handle()))) {
			taskbar->SetTabOrder(i->second->handle(), nullptr);
		}
	}
	setActiveOnTaskbar(activeTab);
}

HRESULT Taskbar::setCurrentAppId(const tstring& appId) {
	auto value = toWide(appId);
	return ::SetCurrentProcessExplicitAppUserModelID(value.empty() ? nullptr : value.c_str());
}

HRESULT Taskbar::setWindowAppId(HWND window, const tstring& appId) {
	if(!window) {
		return E_HANDLE;
	}

	ComPtr<IPropertyStore> properties;
	auto result = ::SHGetPropertyStoreForWindow(window, IID_IPropertyStore,
		reinterpret_cast<void**>(properties.put()));
	if(FAILED(result)) {
		return result;
	}

	auto value = toWide(appId);
	if(value.empty()) {
		PROPVARIANT empty = { };
		result = properties->SetValue(PKEY_AppUserModel_ID, empty);
	} else {
		result = setStringProperty(*properties.get(), PKEY_AppUserModel_ID, value);
	}
	if(FAILED(result)) {
		return result;
	}

	return properties->Commit();
}

HRESULT Taskbar::setWindowAppId(WindowPtr window, const tstring& appId) {
	return window ? setWindowAppId(window->handle(), appId) : E_HANDLE;
}

HRESULT Taskbar::commitJumpList(const JumpList& jumpList, UINT* minSlots) {
	ComPtr<ICustomDestinationList> list;
	auto result = ::CoCreateInstance(CLSID_DestinationList, nullptr,
		CLSCTX_INPROC_SERVER, IID_ICustomDestinationList,
		reinterpret_cast<void**>(list.put()));
	if(FAILED(result)) {
		return result;
	}

	auto appId = toWide(jumpList.appId);
	if(!appId.empty()) {
		result = list->SetAppID(appId.c_str());
		if(FAILED(result)) {
			return result;
		}
	}

	UINT slots = 0;
	ComPtr<IObjectArray> removed;
	result = list->BeginList(&slots, IID_IObjectArray,
		reinterpret_cast<void**>(removed.put()));
	if(FAILED(result)) {
		return result;
	}

	auto abortList = [&list] {
		list->AbortList();
	};

	if(jumpList.showFrequent) {
		result = list->AppendKnownCategory(KDC_FREQUENT);
		if(FAILED(result)) {
			abortList();
			return result;
		}
	}

	if(jumpList.showRecent) {
		result = list->AppendKnownCategory(KDC_RECENT);
		if(FAILED(result)) {
			abortList();
			return result;
		}
	}

	for(const auto& category : jumpList.categories) {
		if(category.name.empty() || category.links.empty()) {
			continue;
		}
		ComPtr<IObjectArray> items;
		result = makeObjectArray(category.links, items.put());
		if(FAILED(result)) {
			abortList();
			return result;
		}
		result = list->AppendCategory(toWide(category.name).c_str(), items.get());
		if(FAILED(result)) {
			abortList();
			return result;
		}
	}

	if(!jumpList.userTasks.empty()) {
		ComPtr<IObjectArray> tasks;
		result = makeObjectArray(jumpList.userTasks, tasks.put());
		if(FAILED(result)) {
			abortList();
			return result;
		}
		result = list->AddUserTasks(tasks.get());
		if(FAILED(result)) {
			abortList();
			return result;
		}
	}

	result = list->CommitList();
	if(SUCCEEDED(result) && minSlots) {
		*minSlots = slots;
	}
	return result;
}

HRESULT Taskbar::deleteJumpList(const tstring& appId) {
	ComPtr<ICustomDestinationList> list;
	auto result = ::CoCreateInstance(CLSID_DestinationList, nullptr,
		CLSCTX_INPROC_SERVER, IID_ICustomDestinationList,
		reinterpret_cast<void**>(list.put()));
	if(FAILED(result)) {
		return result;
	}

	auto value = toWide(appId);
	return list->DeleteList(value.empty() ? nullptr : value.c_str());
}

class Proxy : public Frame {
	typedef Frame BaseType;
	friend class WidgetCreator<Proxy>;

public:
	typedef Proxy ThisType;
	typedef ThisType* ObjectType;

	struct Seed : BaseType::Seed {
		typedef ThisType WidgetType;
		Seed(const tstring& caption) : BaseType::Seed(caption, 0, 0) {
			style = WS_POPUP | WS_CAPTION;
			exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
			location = Rectangle();
		}
	};

	Proxy(Widget* parent) : BaseType(parent, NormalDispatcher::getDefault()) {
		// This hidden HWND represents a taskbar tab, not an application surface.
		setAppearancePolicy(AppearancePolicy::Native);
	}
};

void Taskbar::addToTaskbar(ContainerPtr tab) {
	if(!taskbar || !window || tabs.find(tab) != tabs.end()) {
		return;
	}

	/* for Windows to acknowledge that our tab window is worthy of having its own thumbnail in the
	taskbar, we have to create an invisible popup window that will act as a proxy between the
	taskbar and the actual tab window.
	this technique is illustrated in MFC as well as the Windows SDK sample at
	"Samples\winui\shell\appshellintegration\TabThumbnails". */

	auto proxy = window->addChild(Proxy::Seed(tab->getText()));
	tabs[tab] = proxy;
	tabOrder.push_back(tab);
	// A proxy can also be destroyed by its owning window. Unregister while its
	// HWND is still valid, and forget it before shell calls can re-enter us.
	proxy->onDestroy([this, tab, proxy] {
		auto i = tabs.find(tab);
		if(i == tabs.end() || i->second != proxy) return;
		tabs.erase(i);
		tabOrder.erase(std::remove(tabOrder.begin(), tabOrder.end(), tab), tabOrder.end());
		if(activeTab == tab) activeTab = nullptr;
		if(taskbar && taskbarReady) {
			taskbar->UnregisterTab(proxy->handle());
			taskbar->DeleteTab(proxy->handle());
		}
	});
	// Retire previews at HWND destruction, before a derived page's destructor
	// removes its tab-strip item or releases the contents used by Aero Peek.
	tab->onDestroy([this, tab] { removeFromTaskbar(tab); });

	/* call ChangeWindowMessageFilterEx on the 2 messages we use to dispatch bitmaps to the
	destktop manager to prevent blockings because of different privilege levels. */
	::ChangeWindowMessageFilterEx(proxy->handle(), WM_DWMSENDICONICTHUMBNAIL, 1/*MSGFLT_ALLOW*/, 0);
	::ChangeWindowMessageFilterEx(proxy->handle(), WM_DWMSENDICONICLIVEPREVIEWBITMAP, 1/*MSGFLT_ALLOW*/, 0);

	// keep the proxy window in sync with the actual tab window.
	tab->onTextChanging([this, tab](const tstring& text) {
		auto i = tabs.find(tab);
		if(i != tabs.end()) i->second->setText(text);
	});
	tab->onSized([this, tab](const SizedEvent&) {
		auto i = tabs.find(tab);
		if(i != tabs.end()) util::win32::dwm::invalidateIconicBitmaps(i->second->handle());
	});
	const auto registered = [this, tab, proxy] {
		auto i = tabs.find(tab);
		return i != tabs.end() && i->second == proxy;
	};

	// forward taskbar events that were sent to the proxy window to the actual tab window.
	proxy->onActivate([this, tab, registered](bool activate) {
		if(activate && registered()) {
			setActive(tab);
			::SetForegroundWindow(window->handle());
			if(window->isIconic())
				window->restore();
			else
				window->setVisible(true);
		}
	});
	proxy->onClosing([this, tab, registered]() -> bool {
		if(registered() && window->getEnabled()) {
			tab->close(true);
		}
		return false; // don't close the proxy window just yet; wait for removeFromTaskbar.
	});

	proxy->onRaw([this, tab, proxy, registered](WPARAM, LPARAM lParam) -> LRESULT {
		if(!registered()) return 0;
		// generate a thumbnail to be displayed in the taskbar.
		BitmapPtr bitmap = getBitmap(tab, lParam);
		if(bitmap) {
			util::win32::dwm::setIconicThumbnail(proxy->handle(), bitmap->handle(), 0);
		}
		return 0;
	}, Message(WM_DWMSENDICONICTHUMBNAIL));

	proxy->onRaw([this, tab, proxy, registered](WPARAM, LPARAM) -> LRESULT {
		if(!registered()) return 0;
		// generate a preview of the tab to be shown in "Aero Peek" when the user hovers the thumbnail.
		BitmapPtr bitmap = getBitmap(tab, 0);
		if(bitmap) {
			POINT offset = { 0 };
			::MapWindowPoints(tab->handle(), window->handle(), &offset, 1);
			MENUBARINFO info = { sizeof(MENUBARINFO) };
			if(!window->usesClientFrame() && ::GetMenuBarInfo(window->handle(), OBJID_MENU, 0, &info))
				offset.y += Rectangle(info.rcBar).height();
			util::win32::dwm::setIconicLivePreviewBitmap(proxy->handle(), bitmap->handle(), &offset, 0);
		}
		return 0;
	}, Message(WM_DWMSENDICONICLIVEPREVIEWBITMAP));

	// indicate to the window manager that it should always use the bitmaps we provide.
	BOOL attrib = TRUE;
	util::win32::dwm::setWindowAttribute(proxy->handle(), DWMWA_FORCE_ICONIC_REPRESENTATION, &attrib, sizeof(attrib));
	util::win32::dwm::setWindowAttribute(proxy->handle(), DWMWA_HAS_ICONIC_BITMAP, &attrib, sizeof(attrib));

	if(taskbarReady && SUCCEEDED(taskbar->RegisterTab(proxy->handle(), window->handle()))) {
		taskbar->SetTabOrder(proxy->handle(), nullptr);
	}
}

void Taskbar::removeFromTaskbar(ContainerPtr tab) {
	auto i = tabs.find(tab);
	if(i == tabs.end() || !i->second) {
		return;
	}
	const auto proxy = i->second->handle();
	tabs.erase(i);
	tabOrder.erase(std::remove(tabOrder.begin(), tabOrder.end(), tab), tabOrder.end());
	if(activeTab == tab) activeTab = nullptr;
	if(taskbar && taskbarReady) {
		taskbar->UnregisterTab(proxy);
		// Remove any shell button as well as the registered tab thumbnail, before
		// freeing the HWND. Bulk closes must not leave blank standalone entries.
		taskbar->DeleteTab(proxy);
	}
	::DestroyWindow(proxy);
}

void Taskbar::moveOnTaskbar(ContainerPtr tab, ContainerPtr rightNeighbor) {
	auto i = tabs.find(tab);
	if(i == tabs.end() || tab == rightNeighbor) return;
	tabOrder.erase(std::remove(tabOrder.begin(), tabOrder.end(), tab), tabOrder.end());
	tabOrder.insert(std::find(tabOrder.begin(), tabOrder.end(), rightNeighbor), tab);
	if(taskbar && taskbarReady && i->second) {
		auto right = rightNeighbor ? tabs.find(rightNeighbor) : tabs.end();
		taskbar->SetTabOrder(i->second->handle(),
			right != tabs.end() && right->second ? right->second->handle() : 0);
	}
}

void Taskbar::setActiveOnTaskbar(ContainerPtr tab) {
	activeTab = tab;
	auto i = tabs.find(tab);
	if(taskbar && taskbarReady && window && i != tabs.end() && i->second) {
		taskbar->SetTabActive(i->second->handle(), window->handle(), 0);
	}
}

void Taskbar::setTaskbarIcon(ContainerPtr tab, const IconPtr& icon) {
	auto i = tabs.find(tab);
	if(i != tabs.end() && i->second) {
		i->second->setSmallIcon(icon);
	}
}

void Taskbar::setOverlayIcon(ContainerPtr tab, const IconPtr& icon, const tstring& description) {
	if(taskbar && window) {
		taskbar->SetOverlayIcon(window->handle(), icon ? icon->handle() : nullptr,
			description.c_str());
	}
}

void Taskbar::setProgressState(TBPFLAG state) {
	if(taskbar && window) {
		taskbar->SetProgressState(window->handle(), state);
	}
}

void Taskbar::setProgressValue(ULONGLONG completed, ULONGLONG total) {
	if(taskbar && window) {
		taskbar->SetProgressValue(window->handle(), completed, total);
	}
}

void Taskbar::addThumbnailToolbarButtons(const std::vector<THUMBBUTTON>& buttons) {
	if(taskbar && window && !buttons.empty()) {
		auto copy = buttons;
		taskbar->ThumbBarAddButtons(window->handle(), static_cast<UINT>(copy.size()), copy.data());
	}
}

void Taskbar::updateThumbnailToolbarButtons(const std::vector<THUMBBUTTON>& buttons) {
	if(taskbar && window && !buttons.empty()) {
		auto copy = buttons;
		taskbar->ThumbBarUpdateButtons(window->handle(), static_cast<UINT>(copy.size()), copy.data());
	}
}

void Taskbar::setThumbnailTooltip(const tstring& tooltip) {
	if(taskbar && window) {
		taskbar->SetThumbnailTooltip(window->handle(), tooltip.empty() ? nullptr : tooltip.c_str());
	}
}

void Taskbar::setThumbnailClip(const Rectangle& clip) {
	if(taskbar && window) {
		auto rect = clip.toRECT();
		taskbar->SetThumbnailClip(window->handle(), &rect);
	}
}

void Taskbar::clearThumbnailClip() {
	if(taskbar && window) {
		taskbar->SetThumbnailClip(window->handle(), nullptr);
	}
}

void Taskbar::addThumbnailToolbarButtons(ContainerPtr tab,
	const std::vector<THUMBBUTTON>& buttons)
{
	auto handle = getTaskbarWindow(tab);
	if(taskbar && handle && !buttons.empty()) {
		auto copy = buttons;
		taskbar->ThumbBarAddButtons(handle, static_cast<UINT>(copy.size()), copy.data());
	}
}

void Taskbar::updateThumbnailToolbarButtons(ContainerPtr tab,
	const std::vector<THUMBBUTTON>& buttons)
{
	auto handle = getTaskbarWindow(tab);
	if(taskbar && handle && !buttons.empty()) {
		auto copy = buttons;
		taskbar->ThumbBarUpdateButtons(handle, static_cast<UINT>(copy.size()), copy.data());
	}
}

void Taskbar::setThumbnailTooltip(ContainerPtr tab, const tstring& tooltip) {
	auto handle = getTaskbarWindow(tab);
	if(taskbar && handle) {
		taskbar->SetThumbnailTooltip(handle, tooltip.empty() ? nullptr : tooltip.c_str());
	}
}

void Taskbar::setThumbnailClip(ContainerPtr tab, const Rectangle& clip) {
	auto handle = getTaskbarWindow(tab);
	if(taskbar && handle) {
		auto rect = clip.toRECT();
		taskbar->SetThumbnailClip(handle, &rect);
	}
}

void Taskbar::clearThumbnailClip(ContainerPtr tab) {
	auto handle = getTaskbarWindow(tab);
	if(taskbar && handle) {
		taskbar->SetThumbnailClip(handle, nullptr);
	}
}

void Taskbar::setTabProperties(ContainerPtr tab, STPFLAG properties) {
	auto handle = getTaskbarWindow(tab);
	if(taskbar4 && handle) {
		taskbar4->SetTabProperties(handle, properties);
	}
}

HWND Taskbar::getTaskbarWindow(ContainerPtr tab) const {
	if(!tab) {
		return window ? window->handle() : nullptr;
	}
	auto i = tabs.find(tab);
	if(i != tabs.end() && i->second) {
		return i->second->handle();
	}
	return tab->handle();
}

BitmapPtr Taskbar::getBitmap(ContainerPtr tab, LPARAM thumbnailSize) {
	UpdateCanvas canvas { tab };

	// get the actual size of the tab.
	const Point size_full { tab->getClientSize() };
	if(size_full.x <= 0 || size_full.y <= 0)
		return 0;

	// this DIB will hold a full capture of the tab.
	BitmapPtr bitmap_full { new DibBitmap(canvas.handle(), size_full, DibBitmap::RowOrder::BottomUp) };

	CompatibleCanvas canvas_full { canvas.handle() };
	auto select_full(canvas_full.select(*bitmap_full));

	tab->sendMessage(WM_PRINT, reinterpret_cast<WPARAM>(canvas_full.handle()), PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN | PRF_ERASEBKGND);

	// get rid of some transparent bits.
	::BitBlt(canvas_full.handle(), 0, 0, size_full.x, size_full.y, canvas_full.handle(), 0, 0, MERGECOPY);

	if(!thumbnailSize)
		return bitmap_full;

	// compute the size of the thumbnail, must not exceed the LPARAM values.
	double factor { std::min(static_cast<double>(HIWORD(thumbnailSize)) / static_cast<double>(size_full.x),
		static_cast<double>(LOWORD(thumbnailSize)) / static_cast<double>(size_full.y)) };
	const Point size_thumb { static_cast<long>(size_full.x * factor), static_cast<long>(size_full.y * factor) };
	if(size_thumb.x <= 0 || size_thumb.y <= 0)
		return 0;

	// this DIB will hold a resized view of the tab, to be used as a thumbnail.
	BitmapPtr bitmap_thumb { new DibBitmap(canvas.handle(), size_thumb, DibBitmap::RowOrder::BottomUp) };

	CompatibleCanvas canvas_thumb { canvas.handle() };
	auto select_thumb(canvas_thumb.select(*bitmap_thumb));

	::SetStretchBltMode(canvas_thumb.handle(), HALFTONE);
	::SetBrushOrgEx(canvas_thumb.handle(), 0, 0, 0);
	::StretchBlt(canvas_thumb.handle(), 0, 0, size_thumb.x, size_thumb.y,
		canvas_full.handle(), 0, 0, size_full.x, size_full.y, SRCCOPY);

	return bitmap_thumb;
}

}
