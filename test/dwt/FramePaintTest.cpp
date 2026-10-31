// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/Application.h>
#include <dwt/widgets/Frame.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/ScrolledContainer.h>
#include <dwt/util/win32/NonClientPaint.h>
#include <dwt/util/win32/NativeDialogAppearance.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}
INT_PTR CALLBACK dialogProc(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
class PaintDispatcher : public dwt::Dispatcher {
public:
	PaintDispatcher() : Dispatcher(L"DwtFramePaintRegression") { }
	unsigned nativeCaptionPaints = 0, nativeHover = 0, visibilityChanges = 0;
	LRESULT chain(const MSG& msg) override {
		if(msg.message == WM_NCPAINT) {
			MENUBARINFO menu { sizeof(MENUBARINFO) };
			RECT region {};
			if(msg.wParam <= 1 || !GetRgnBox(reinterpret_cast<HRGN>(msg.wParam), &region) ||
				!GetMenuBarInfo(msg.hwnd, OBJID_MENU, 0, &menu) || region.top < menu.rcBar.top) ++nativeCaptionPaints;
		}
		if(msg.message == WM_NCACTIVATE && msg.lParam != -1) ++nativeCaptionPaints;
		if(msg.message == WM_NCMOUSEMOVE || msg.message == WM_NCMOUSELEAVE) ++nativeHover;
		if(msg.message == WM_STYLECHANGED && msg.wParam == static_cast<WPARAM>(GWL_STYLE) && msg.lParam) {
			const auto styles = reinterpret_cast<STYLESTRUCT*>(msg.lParam);
			if((styles->styleOld ^ styles->styleNew) & WS_VISIBLE) ++visibilityChanges;
		}
		return DefWindowProc(msg.hwnd, msg.message, msg.wParam, msg.lParam);
	}
};
class TestFrame : public dwt::Frame {
public:
	TestFrame(PaintDispatcher& dispatcher) : Frame(nullptr, dispatcher) {
		create(Seed(L"Painting test", WS_OVERLAPPEDWINDOW, 0));
		finalizeAppearance();
	}
};
void validateTree(HWND window) {
	RedrawWindow(window, nullptr, nullptr, RDW_VALIDATE | RDW_NOERASE | RDW_NOFRAME | RDW_NOINTERNALPAINT | RDW_ALLCHILDREN);
}
bool clientNeedsPaint(HWND window) {
	RECT client;
	GetClientRect(window, &client);
	auto region = CreateRectRgn(0, 0, 0, 0);
	auto content = CreateRectRgnIndirect(&client);
	GetUpdateRgn(window, region, FALSE);
	const auto kind = CombineRgn(region, region, content, RGN_AND);
	DeleteObject(content);
	DeleteObject(region);
	return kind != NULLREGION;
}
}

void checkFramePainting(dwt::Application& app, bool customLight) {
	auto palette = dwt::Appearance::defaultPalette();
	if(customLight) {
		palette = { RGB(245, 245, 245), RGB(255, 255, 255), RGB(25, 25, 25),
			RGB(110, 110, 110), RGB(175, 175, 175), RGB(0, 90, 180), RGB(255, 255, 255) };
	}
	app.getAppearance().configure(customLight ? dwt::Appearance::Mode::Light : dwt::Appearance::Mode::Dark,
		palette, customLight ? dwt::Appearance::Rendering::CustomPalette : dwt::Appearance::Rendering::Automatic);
	PaintDispatcher dispatcher;
	auto frame = new TestFrame(dispatcher);
	frame->resize(dwt::Rectangle(-32000, -32000, 650, 450));
	ShowWindow(frame->handle(), SW_SHOWNOACTIVATE);
	auto group = frame->addChild(dwt::GroupBox::Seed(L"Opaque dark group"));
	group->resize(dwt::Rectangle(10, 10, 200, 100));
	check(!(GetWindowLongPtr(group->handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT), "Dark group still requests transparent parent painting");
	auto host = frame->addChild(dwt::ScrolledContainer::Seed());
	check((GetWindowLongPtr(host->handle(), GWL_STYLE) & WS_CLIPCHILDREN) != 0, "Page host erases over its children");
	validateTree(frame->handle());
	dispatcher.nativeCaptionPaints = dispatcher.nativeHover = dispatcher.visibilityChanges = 0;
	frame->sendMessage(WM_NCPAINT, 1);
	check(!clientNeedsPaint(frame->handle()), "Frame painting invalidated the client area");
	frame->sendMessage(WM_NCACTIVATE, FALSE);
	frame->sendMessage(WM_NCACTIVATE, TRUE);
	check(!clientNeedsPaint(frame->handle()), "Activation invalidated the client area");
	RECT bounds;
	GetWindowRect(frame->handle(), &bounds);
	const auto point = MAKELPARAM(bounds.right - 20, bounds.top + 15);
	for(int i = 0; i < 20; ++i) frame->sendMessage(WM_NCMOUSEMOVE, HTCLOSE, point);
	frame->sendMessage(WM_NCMOUSELEAVE);
	check(!clientNeedsPaint(frame->handle()), "Hover invalidated the client area");
	dwt::util::win32::invalidateNonClient(frame->handle());
	check(!clientNeedsPaint(frame->handle()), "Non-client invalidation reached the client area");
	frame->setText(L"Updated title");
	check(!clientNeedsPaint(frame->handle()), "Title update invalidated the client area");
	frame->sendMessage(WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIcon(nullptr, IDI_APPLICATION)));
	check(frame->getText() == L"Updated title", "Title update was lost");
	check(IsWindowVisible(frame->handle()), "Title update hid the window");
	check(dispatcher.nativeCaptionPaints == 0 && dispatcher.nativeHover == 0,
		"Native caption drawing can overwrite custom buttons");
	check(dispatcher.visibilityChanges == 0, "Caption updates changed the visibility of the frame and its descendants");
	check(!clientNeedsPaint(frame->handle()), "Caption update invalidated the client area");
	check(!clientNeedsPaint(group->handle()), "Caption update invalidated a child control");

	auto menuSeed = dwt::Menu::Seed();
	menuSeed.popup = false;
	auto menu = frame->addChild(menuSeed);
	menu->appendItem(L"File");
	menu->setMenu();
	validateTree(frame->handle());
	dispatcher.nativeCaptionPaints = 0;
	frame->sendMessage(WM_NCPAINT, 1);
	frame->sendMessage(WM_NCACTIVATE, FALSE);
	check(dispatcher.nativeCaptionPaints == 0, "Menu repainted the native caption");
	check(!clientNeedsPaint(frame->handle()), "Menu activation invalidated dialog content");
	check(frame->usesClientFrame(), "Dark frame retained a native caption");
	check((GetWindowLongPtr(frame->handle(), GWL_STYLE) & WS_CAPTION) == 0, "Native caption style survived client framing");
	dispatcher.nativeHover = 0;
	frame->sendMessage(WM_NCMOUSEMOVE, HTMENU, point);
	frame->sendMessage(WM_NCMOUSEMOVE, HTCLOSE, point);
	frame->sendMessage(WM_NCMOUSEMOVE, HTCLOSE, point);
	frame->sendMessage(WM_NCMOUSELEAVE);
	check(dispatcher.nativeHover == 0 && dispatcher.nativeCaptionPaints == 0, "Client caption forwarded native hover painting");
	RECT dirty { 300, 300, 335, 345 };
	InvalidateRect(frame->handle(), &dirty, FALSE);
	check(clientNeedsPaint(frame->handle()), "Pending client repaint was not set up");
	InvalidateRect(group->handle(), nullptr, FALSE);
	frame->setText(L"Pending client paint");
	check(clientNeedsPaint(frame->handle()), "Title update discarded a pending client repaint");
	check(clientNeedsPaint(group->handle()), "Title update discarded a pending child repaint");

	frame->sendMessage(WM_SETREDRAW, FALSE);
	frame->setText(L"Suspended title");
	check(!(GetWindowLongPtr(frame->handle(), GWL_STYLE) & WS_VISIBLE), "Title update ended an outer redraw suspension");
	frame->sendMessage(WM_SETREDRAW, TRUE);
	ShowWindow(frame->handle(), SW_HIDE);
	frame->setText(L"Hidden title");
	check(!IsWindowVisible(frame->handle()), "Title update exposed a hidden window");
	{
		dwt::util::win32::NativeDialogAppearance appearance(app.getAppearance());
		struct {
			DLGTEMPLATE dialog {};
			WORD menu = 0, windowClass = 0, title = 0;
		} resource;
		resource.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
		resource.dialog.cx = 200;
		resource.dialog.cy = 100;
		auto native = CreateDialogIndirectParam(GetModuleHandle(nullptr), &resource.dialog, nullptr, dialogProc, 0);
		check(native != nullptr, "Could not create native dialog regression window");
		SetWindowPos(native, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		appearance.apply(native);
		ShowWindow(native, SW_SHOWNOACTIVATE);
		validateTree(native);
		check(IsWindowVisible(native) && !clientNeedsPaint(native), "Native dialog did not start with a clean client");
		SetWindowText(native, L"Updated native dialog");
		check(IsWindowVisible(native) && !clientNeedsPaint(native), "Native dialog title update invalidated its client");
		SendMessage(native, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIcon(nullptr, IDI_APPLICATION)));
		check(IsWindowVisible(native) && !clientNeedsPaint(native), "Native dialog icon update invalidated its client");
		SendMessage(native, WM_NCACTIVATE, FALSE, 0);
		SendMessage(native, WM_NCACTIVATE, TRUE, 0);
		check(IsWindowVisible(native) && !clientNeedsPaint(native), "Native dialog caption invalidated its contents or hid the window");
		DestroyWindow(native);
	}
	frame->setFrameRendering(dwt::Frame::FrameRendering::Native);
	check(!frame->usesClientFrame() && app.getAppearance().isManual(), "Native frame disabled control palette painting");
	check((GetWindowLongPtr(frame->handle(), GWL_STYLE) & WS_CAPTION) == WS_CAPTION,
		"Native frame choice did not restore caption styles");
	ShowWindow(frame->handle(), SW_SHOWNOACTIVATE);
	validateTree(frame->handle());
	dispatcher.nativeCaptionPaints = dispatcher.nativeHover = 0;
	frame->sendMessage(WM_NCPAINT, 1);
	frame->sendMessage(WM_NCACTIVATE, TRUE);
	check(dispatcher.nativeCaptionPaints > 0, "Palette menu suppressed native caption painting");
	check(!(GetWindowLongPtr(group->handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT),
		"Native frame changed manual control painting");
	frame->setFrameRendering(dwt::Frame::FrameRendering::Automatic);
	check(frame->usesClientFrame(), "Automatic frame choice did not restore client framing");
	validateTree(frame->handle());
	dispatcher.nativeCaptionPaints = 0;
	frame->sendMessage(WM_NCPAINT, 1);
	frame->sendMessage(WM_NCACTIVATE, TRUE);
	check(dispatcher.nativeCaptionPaints == 0, "Restored client frame forwarded native caption painting");
	app.getAppearance().configure(dwt::Appearance::Mode::Light, dwt::Appearance::defaultPalette());
	check((GetWindowLongPtr(group->handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0, "Light mode did not restore native group painting");
	DestroyWindow(frame->handle());
}

int dwtMain(dwt::Application& app) {
	try {
		checkFramePainting(app, false);
		checkFramePainting(app, true);
		std::puts("Caption ownership, menu clipping, client invalidation and group painting passed for dark and custom light palettes.");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
