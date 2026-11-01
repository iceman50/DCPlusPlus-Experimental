/* Copyright (c) 2026 iceman50. Distributed under GPL-3.0-or-later. */
#include "ClientFrame.h"

#include <dwt/CanvasClasses.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Pen.h>
#include <dwt/widgets/Frame.h>

#include <algorithm>
#include <cwctype>

namespace dwt {

namespace {

Point clientPoint(HWND window, LPARAM position) {
	// RECT mapping normalizes mirrored windows into physical screen coordinates.
	RECT client;
	::GetClientRect(window, &client);
	::MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&client), 2);
	return Point(GET_X_LPARAM(position) - client.left, GET_Y_LPARAM(position) - client.top);
}

}

thread_local ClientFrame* ClientFrame::trackingFrame = nullptr;

ClientFrame::ClientFrame(Frame* owner) : Composite(owner, NormalDispatcher::getDefault()), owner(owner), captionHeight(0), frameHeight(0), active(false), layingOut(false), tracking(-1), nextMenu(-1), selectedMenu(nullptr), selectedFlags(0), previousFocus(nullptr), captionFocus(nullptr), menuHook(nullptr) {
	create(Seed(tstring(), WS_CHILD | WS_CLIPSIBLINGS, WS_EX_NOINHERITLAYOUT));
	// Layout is mirrored explicitly, including the button order and glyphs.
	addRemoveExStyle(WS_EX_LAYOUTRTL, false);
	setAppearancePolicy(AppearancePolicy::FrameworkChrome);
	enableAccessibility(accessibility::TitleBar);
	setAccessibleName(_T("Window title bar"));
	onPainting([this](PaintCanvas& target) {
		const Rectangle bounds(getClientSize());
		BufferedCanvas<FreeCanvas> canvas(target.handle(), bounds);
		paint(canvas);
		canvas.blast(bounds);
	});
	onPrinting([this](Canvas& canvas) { paint(canvas); });
	onRaw([](WPARAM, LPARAM) -> LRESULT { return TRUE; }, Message(WM_ERASEBKGND));
	onRaw([](WPARAM, LPARAM) -> LRESULT { return HTTRANSPARENT; }, Message(WM_NCHITTEST));
	addCaptionButton(_T("Minimize"), SC_MINIMIZE);
	addCaptionButton(_T("Maximize"), SC_MAXIMIZE);
	addCaptionButton(_T("Help"), SC_CONTEXTHELP);
	addCaptionButton(_T("Close"), SC_CLOSE);
	refresh();
	finalizeAppearance();
}

ClientFrame::~ClientFrame() {
	restoreSystemMenu();
	if(menuHook) ::UnhookWindowsHookEx(menuHook);
	if(trackingFrame == this) {
		trackingFrame = nullptr;
		::EndMenu();
	}
}

Button* ClientFrame::addCaptionButton(const tstring& label, UINT command) {
	Button::Seed seed(label, BS_PUSHBUTTON);
	seed.style &= ~WS_TABSTOP;
	auto button = addChild(seed);
	button->setAccessibleName(label);
	button->onCustomDraw([this, button, command](NMCUSTOMDRAW& data) { return drawButton(button, command, data); });
	button->onRaw([this](WPARAM, LPARAM position) -> LRESULT { return resizeHitTest(position) == HTCLIENT ? HTCLIENT : HTTRANSPARENT; }, Message(WM_NCHITTEST));
	button->onClicked([this, command] {
		if(captionFocus && ::IsWindow(captionFocus)) ::SetFocus(captionFocus);
		const auto action = command == SC_MAXIMIZE && ::IsZoomed(owner->handle()) ? SC_RESTORE : command;
		// Preserve the owner's normal close/minimize policy, including hide-to-tray.
		::PostMessage(owner->handle(), WM_SYSCOMMAND, action, 0);
	});
	button->addCallback(Message(WM_SETFOCUS), [this](const MSG& msg, LRESULT&) {
		captionFocus = reinterpret_cast<HWND>(msg.wParam);
		return false;
	});
	button->addCallback(Message(WM_CAPTURECHANGED), [this, button](const MSG&, LRESULT&) {
		if(::GetFocus() == button->handle() && captionFocus && ::IsWindow(captionFocus)) ::SetFocus(captionFocus);
		return false;
	});
	buttons.emplace_back(button, command);
	return button;
}

void ClientFrame::releaseFrameInput() {
	// Reframing can hide the currently focused caption/menu button. Return input
	// to content before hiding it, without activating an unrelated window.
	const auto focus = ::GetFocus();
	const auto target = menuFocused() ? previousFocus : captionFocus;
	const auto capture = ::GetCapture();
	if(capture == handle() || ::IsChild(handle(), capture)) ::ReleaseCapture();
	if(focus == handle() || ::IsChild(handle(), focus)) {
		if(target && ::IsWindow(target) && ::IsWindowVisible(target) &&
			::IsWindowEnabled(target) && !::IsChild(handle(), target)) {
			::SetFocus(target);
		} else {
			::SetFocus(owner->handle());
		}
	}
	previousFocus = captionFocus = nullptr;
	if(trackingFrame == this) {
		nextMenu = -1;
		::EndMenu();
	}
}

void ClientFrame::refresh() {
	NONCLIENTMETRICS metrics = { sizeof(NONCLIENTMETRICS) };
	if(owner->getSystemParameters(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics)) {
		captionFont = new Font(owner->hasExStyle(WS_EX_TOOLWINDOW) ? metrics.lfSmCaptionFont : metrics.lfCaptionFont);
		menuFont = new Font(metrics.lfMenuFont);
	} else {
		captionFont = new Font(Font::DefaultGui);
		menuFont = captionFont;
	}
	captionHeight = std::max(owner->scale(28), owner->getSystemMetric(owner->hasExStyle(WS_EX_TOOLWINDOW) ? SM_CYSMCAPTION : SM_CYCAPTION));
	active = ::GetActiveWindow() == owner->handle();
	layout();
	redrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void ClientFrame::layout() {
	if(layingOut || !owner->usesClientFrame()) return;
	layingOut = true;
	const auto border = owner->getDialogBorderWidth();
	const auto width = std::max(0L, owner->getClientSize().x - border * 2);
	const bool rtl = owner->hasExStyle(WS_EX_LAYOUTRTL);
	const long buttonWidth = owner->scale(42);
	long edge = width;
	for(auto i = buttons.rbegin(); i != buttons.rend(); ++i) {
		auto button = i->first;
		const auto command = i->second;
		bool visible = owner->hasStyle(WS_SYSMENU);
		if(command == SC_CONTEXTHELP) visible &= owner->hasExStyle(WS_EX_CONTEXTHELP);
		if(command == SC_MINIMIZE || command == SC_MAXIMIZE) visible &= !owner->hasExStyle(WS_EX_CONTEXTHELP) && (owner->hasStyle(WS_MINIMIZEBOX) || owner->hasStyle(WS_MAXIMIZEBOX));
		button->setVisible(visible);
		if(!visible) continue;
		edge -= buttonWidth;
		button->resize(Rectangle(rtl ? width - edge - buttonWidth : edge, 0, buttonWidth, captionHeight));
		bool enabled = owner->getEnabled();
		if(command == SC_MINIMIZE) enabled &= owner->hasStyle(WS_MINIMIZEBOX);
		if(command == SC_MAXIMIZE) enabled &= owner->hasStyle(WS_MAXIMIZEBOX);
		const auto action = command == SC_MAXIMIZE && ::IsZoomed(owner->handle()) ? SC_RESTORE : command;
		const auto state = ::GetMenuState(::GetSystemMenu(owner->handle(), FALSE), action, MF_BYCOMMAND);
		if(state != static_cast<UINT>(-1)) enabled &= !(state & (MF_DISABLED | MF_GRAYED));
		button->setEnabled(enabled);
		if(command == SC_MAXIMIZE) {
			const auto label = ::IsZoomed(owner->handle()) ? _T("Restore") : _T("Maximize");
			if(button->getText() != label) {
				button->setText(label);
				button->setAccessibleName(label);
			}
		}
	}

	const auto menu = ::GetMenu(owner->handle());
	const int count = menu ? std::max(0, ::GetMenuItemCount(menu)) : 0;
	// A popup runs a nested message loop. Keep its originating controls alive.
	if(tracking < 0) {
		while(items.size() > static_cast<size_t>(count)) {
			::DestroyWindow(items.back().button->handle());
			items.pop_back();
		}
		while(items.size() < static_cast<size_t>(count)) {
			const auto index = items.size();
			Button::Seed seed(tstring(), BS_PUSHBUTTON);
			seed.style &= ~WS_TABSTOP;
			auto button = addChild(seed);
			items.push_back({ button, 0 });
			button->onCustomDraw([this, index](NMCUSTOMDRAW& data) { return drawMenuButton(index, data); });
			button->onClicked([this, index] { openMenu(index, false); });
			button->addCallback(Message(WM_SETFOCUS), [this](const MSG& msg, LRESULT&) {
				const auto old = reinterpret_cast<HWND>(msg.wParam);
				const bool fromMenu = std::any_of(items.begin(), items.end(), [old](const MenuItem& item) { return item.button->handle() == old; });
				if(!fromMenu) previousFocus = old;
				return false;
			});
			for(auto message: { WM_KEYDOWN, WM_SYSKEYDOWN, WM_CHAR, WM_SYSCHAR, WM_GETDLGCODE }) {
				button->addCallback(Message(message), [this, index](const MSG& msg, LRESULT& result) { return menuKey(index, msg, result); });
			}
		}
	}
	UpdateCanvas canvas(this);
	auto selectFont = canvas.select(*menuFont);
	long rowHeight = std::max<long>(owner->getSystemMetric(SM_CYMENU), canvas.getTextExtent(_T("Mg")).y + owner->scale(6));
	std::vector<long> widths;
	for(size_t index = 0; index < items.size(); ++index) {
		const int length = ::GetMenuString(menu, static_cast<UINT>(index), nullptr, 0, MF_BYPOSITION);
		std::vector<TCHAR> text(length + 1);
		::GetMenuString(menu, static_cast<UINT>(index), text.data(), static_cast<int>(text.size()), MF_BYPOSITION);
		auto& item = items[index];
		item.state = ::GetMenuState(menu, static_cast<UINT>(index), MF_BYPOSITION);
		item.button->setEnabled(owner->getEnabled() && !(item.state & (MF_DISABLED | MF_GRAYED)));
		if(item.button->getText() != text.data()) item.button->setText(text.data());
		if(item.button->getFont() != menuFont) item.button->setFont(menuFont);
		long itemWidth = canvas.getTextExtent(text.data()).x;
		MENUITEMINFO info = { sizeof(MENUITEMINFO), MIIM_FTYPE | MIIM_DATA | MIIM_ID };
		if(::GetMenuItemInfo(menu, static_cast<UINT>(index), TRUE, &info) && (info.fType & MFT_OWNERDRAW)) {
			MEASUREITEMSTRUCT measure = {};
			measure.CtlType = ODT_MENU;
			measure.itemID = info.wID;
			measure.itemData = info.dwItemData;
			if(owner->sendMessage(WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure))) {
				itemWidth = measure.itemWidth;
				rowHeight = std::max(rowHeight, static_cast<long>(measure.itemHeight));
			}
		}
		widths.push_back(std::min(width, itemWidth + owner->scale(18)));
	}
	long x = 0;
	long y = captionHeight;
	for(size_t index = 0; index < items.size(); ++index) {
		const long itemWidth = widths[index];
		if(x && x + itemWidth > width) { x = 0; y += rowHeight; }
		items[index].button->resize(Rectangle(rtl ? width - x - itemWidth : x, y, itemWidth, rowHeight));
		x += itemWidth;
	}
	// Keep the one-pixel separator outside child bounds so their painting and
	// WS_CLIPCHILDREN cannot obscure it beneath caption buttons or menu headings.
	frameHeight = y + (items.empty() ? 0 : rowHeight) + 1;
	::SetWindowPos(handle(), HWND_TOP, border, border, width, frameHeight, SWP_NOACTIVATE);
	layingOut = false;
}

void ClientFrame::paint(Canvas& canvas) {
	const auto& colors = getAppearance().getPalette();
	canvas.fill(Rectangle(getClientSize()), Brush(colors.surface));
	const long width = getClientSize().x;
	Rectangle text(owner->scale(8), 0, std::max(0L, width - owner->scale(16)), captionHeight);
	const bool rtl = owner->hasExStyle(WS_EX_LAYOUTRTL);
	for(const auto& item: buttons) {
		if(!item.first->hasStyle(WS_VISIBLE)) continue;
		RECT rect;
		::GetWindowRect(item.first->handle(), &rect);
		::MapWindowPoints(nullptr, handle(), reinterpret_cast<POINT*>(&rect), 2);
		if(rtl) {
			const long right = text.right();
			text.pos.x = std::max(text.left(), rect.right + owner->scale(4));
			text.size.x = std::max(0L, right - text.left());
		} else {
			text.size.x = std::max(0L, rect.left - owner->scale(4) - text.left());
		}
	}
	auto icon = reinterpret_cast<HICON>(owner->sendMessage(WM_GETICON, ICON_SMALL2));
	if(!icon) icon = reinterpret_cast<HICON>(::GetClassLongPtr(owner->handle(), GCLP_HICONSM));
	if(icon && !owner->hasExStyle(WS_EX_TOOLWINDOW) && owner->hasStyle(WS_SYSMENU)) {
		const long size = owner->getSystemMetric(SM_CXSMICON);
		::DrawIconEx(canvas.handle(), rtl ? text.right() - size : text.left(), (captionHeight - size) / 2, icon, size, size, 0, nullptr, DI_NORMAL);
		if(!rtl) text.pos.x += size + owner->scale(6);
		text.size.x = std::max(0L, text.size.x - size - owner->scale(6));
	}
	auto selectFont = canvas.select(*captionFont);
	auto transparent = canvas.setBkMode(true);
	canvas.setTextColor(active ? colors.text : colors.disabledText);
	canvas.drawText(owner->getText(), text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | (rtl ? DT_RIGHT | DT_RTLREADING : 0));
	Pen border(colors.border);
	auto selectPen = canvas.select(border);
	canvas.line(0, frameHeight - 1, width, frameHeight - 1);
}

LRESULT ClientFrame::drawButton(Button* button, UINT command, NMCUSTOMDRAW& data) {
	if(data.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
	const auto& colors = getAppearance().getPalette();
	const bool disabled = !button->getEnabled() || !owner->getEnabled();
	const bool hot = !disabled && (data.uItemState & (CDIS_HOT | CDIS_SELECTED));
	const bool closeHot = hot && command == SC_CLOSE;
	const auto background = closeHot ? ((data.uItemState & CDIS_SELECTED) ? RGB(197, 15, 31) : RGB(232, 17, 35)) : hot ? Appearance::blend(colors.surface, colors.accent, (data.uItemState & CDIS_SELECTED) ? 128 : 64) : colors.surface;
	FreeCanvas canvas(data.hdc);
	const Rectangle bounds(data.rc);
	canvas.fill(bounds, Brush(background));
	Pen pen(closeHot ? RGB(255, 255, 255) : disabled || !active ? colors.disabledText : colors.text, Pen::Solid, std::max(1, owner->scale(1)));
	auto selectPen = canvas.select(pen);
	const long x = (bounds.left() + bounds.right()) / 2;
	const long y = (bounds.top() + bounds.bottom()) / 2;
	const long radius = owner->scale(5);
	if(command == SC_CLOSE) {
		canvas.line(x - radius, y - radius, x + radius + 1, y + radius + 1);
		canvas.line(x + radius, y - radius, x - radius - 1, y + radius + 1);
	} else if(command == SC_MINIMIZE) {
		canvas.line(x - radius, y + radius / 2, x + radius + 1, y + radius / 2);
	} else if(command == SC_MAXIMIZE) {
		Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)), false);
		auto selectBrush = canvas.select(hollow);
		if(::IsZoomed(owner->handle())) {
			const long offset = owner->scale(2);
			canvas.rectangle(Rectangle(x - radius + offset, y - radius, radius * 2, radius * 2));
			canvas.fill(Rectangle(x - radius, y - radius + offset, radius * 2, radius * 2), Brush(background));
			canvas.rectangle(Rectangle(x - radius, y - radius + offset, radius * 2, radius * 2));
		} else canvas.rectangle(Rectangle(x - radius, y - radius, radius * 2, radius * 2));
	} else {
		auto selectFont = canvas.select(*captionFont);
		auto transparent = canvas.setBkMode(true);
		canvas.setTextColor(disabled ? colors.disabledText : colors.text);
		auto text = bounds;
		canvas.drawText(_T("?"), text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	return CDRF_SKIPDEFAULT;
}

LRESULT ClientFrame::drawMenuButton(size_t index, NMCUSTOMDRAW& data) {
	if(data.dwDrawStage != CDDS_PREPAINT || index >= items.size()) return CDRF_DODEFAULT;
	const auto& colors = getAppearance().getPalette();
	const auto button = items[index].button;
	const bool selected = tracking >= 0 ? tracking == static_cast<int>(index) : ::GetFocus() == button->handle() || (data.uItemState & (CDIS_HOT | CDIS_SELECTED));
	const auto hidePrefix = !menuFocused() && (owner->sendMessage(WM_QUERYUISTATE) & UISF_HIDEACCEL);
	const auto menu = ::GetMenu(owner->handle());
	MENUITEMINFO info = { sizeof(MENUITEMINFO), MIIM_FTYPE | MIIM_DATA | MIIM_ID | MIIM_STATE };
	if(::GetMenuItemInfo(menu, static_cast<UINT>(index), TRUE, &info) && (info.fType & MFT_OWNERDRAW)) {
		DRAWITEMSTRUCT draw = {};
		draw.CtlType = ODT_MENU;
		draw.itemID = info.wID;
		draw.itemAction = ODA_DRAWENTIRE;
		draw.itemState = (selected ? ODS_SELECTED : 0) | (button->getEnabled() ? 0 : ODS_DISABLED | ODS_GRAYED) | (hidePrefix ? ODS_NOACCEL : 0) | (info.fState & MFS_CHECKED ? ODS_CHECKED : 0) | (info.fState & MFS_DEFAULT ? ODS_DEFAULT : 0);
		draw.hwndItem = reinterpret_cast<HWND>(menu);
		draw.hDC = data.hdc;
		draw.rcItem = data.rc;
		draw.itemData = info.dwItemData;
		// The menu renderer excludes the painted area from its caller's clip region.
		CanvasState saved(data.hdc);
		const auto painted = owner->sendMessage(WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
		saved.restore();
		if(painted) return CDRF_SKIPDEFAULT;
	}
	FreeCanvas canvas(data.hdc);
	Rectangle bounds(data.rc);
	canvas.fill(bounds, Brush(selected && button->getEnabled() ? colors.accent : colors.surface));
	auto font = canvas.select(*menuFont);
	auto transparent = canvas.setBkMode(true);
	canvas.setTextColor(!button->getEnabled() ? colors.disabledText : selected ? colors.highlightText : colors.text);
	canvas.drawText(button->getText(), bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | (hidePrefix ? DT_HIDEPREFIX : 0) | (owner->hasExStyle(WS_EX_LAYOUTRTL) ? DT_RTLREADING : 0));
	return CDRF_SKIPDEFAULT;
}

LRESULT ClientFrame::resizeHitTest(LPARAM position) const {
	if(::IsZoomed(owner->handle()) || !owner->hasStyle(WS_THICKFRAME)) return HTCLIENT;
	const auto point = clientPoint(owner->handle(), position);
	const auto size = owner->getClientSize();
	const long x = owner->getSystemMetric(SM_CXSIZEFRAME) + owner->getSystemMetric(SM_CXPADDEDBORDER);
	const long y = owner->getSystemMetric(SM_CYSIZEFRAME) + owner->getSystemMetric(SM_CXPADDEDBORDER);
	const bool left = point.x < x, right = point.x >= size.x - x;
	const bool top = point.y < y, bottom = point.y >= size.y - y;
	if(top && left) return HTTOPLEFT;
	if(top && right) return HTTOPRIGHT;
	if(bottom && left) return HTBOTTOMLEFT;
	if(bottom && right) return HTBOTTOMRIGHT;
	if(left) return HTLEFT;
	if(right) return HTRIGHT;
	if(top) return HTTOP;
	if(bottom) return HTBOTTOM;
	return HTCLIENT;
}

LRESULT ClientFrame::hitTest(LPARAM position) const {
	const auto resize = resizeHitTest(position);
	if(resize != HTCLIENT) return resize;
	const auto point = clientPoint(handle(), position);
	if(point.y >= 0 && point.y < captionHeight) {
		// Buttons are real child controls; their own hit tests retain client input.
		const auto width = getClientSize().x;
		const bool icon = owner->hasExStyle(WS_EX_LAYOUTRTL) ? point.x >= width - owner->scale(32) : point.x < owner->scale(32);
		return icon && owner->hasStyle(WS_SYSMENU) ? HTSYSMENU : HTCAPTION;
	}
	return HTCLIENT;
}

bool ClientFrame::menuFocused() const {
	if(tracking >= 0) return true;
	const auto focus = ::GetFocus();
	for(const auto& item: items) if(item.button->handle() == focus) return true;
	return false;
}

int ClientFrame::mnemonic(TCHAR character) const {
	for(size_t index = 0; index < items.size(); ++index) {
		const auto text = items[index].button->getText();
		for(size_t i = 0; i + 1 < text.size(); ++i) {
			if(text[i] != _T('&')) continue;
			if(text[++i] == _T('&')) continue;
			if(std::towupper(text[i]) == std::towupper(character) && items[index].button->getEnabled()) return static_cast<int>(index);
		}
	}
	return -1;
}

void ClientFrame::focusMenu(int index) {
	if(items.empty()) return;
	if(!menuFocused()) previousFocus = ::GetFocus();
	const int count = static_cast<int>(items.size());
	index = (index % count + count) % count;
	if(!items[index].button->getEnabled()) index = adjacentMenu(index, 1);
	::SetFocus(items[index].button->handle());
	redrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN);
}

int ClientFrame::adjacentMenu(int index, int direction) const {
	const int count = static_cast<int>(items.size());
	for(int i = 0; i < count; ++i) {
		index = (index + direction + count) % count;
		if(items[index].button->getEnabled()) return index;
	}
	return index;
}

void ClientFrame::leaveMenu() {
	// Closing a menu after deactivation must not activate the old window again.
	if(::GetActiveWindow() == owner->handle()) {
		if(previousFocus && ::IsWindow(previousFocus) && ::IsWindowEnabled(previousFocus)) ::SetFocus(previousFocus);
		else if(menuFocused()) ::SetFocus(owner->handle());
	}
	previousFocus = nullptr;
	redrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN);
	owner->sendMessage(WM_EXITMENULOOP, FALSE);
}

bool ClientFrame::menuKey(size_t index, const MSG& msg, LRESULT& result) {
	result = 0;
	if(msg.message == WM_GETDLGCODE) { result = DLGC_WANTALLKEYS; return true; }
	if(msg.message == WM_CHAR || msg.message == WM_SYSCHAR) {
		const int found = mnemonic(static_cast<TCHAR>(msg.wParam));
		if(found >= 0) openMenu(found, true);
		return true;
	}
	switch(msg.wParam) {
	case VK_LEFT: focusMenu(adjacentMenu(static_cast<int>(index), owner->hasExStyle(WS_EX_LAYOUTRTL) ? 1 : -1)); return true;
	case VK_RIGHT: focusMenu(adjacentMenu(static_cast<int>(index), owner->hasExStyle(WS_EX_LAYOUTRTL) ? -1 : 1)); return true;
	case VK_DOWN:
	case VK_UP:
	case VK_RETURN:
	case VK_SPACE: openMenu(index, true); return true;
	case VK_ESCAPE:
	case VK_MENU:
	case VK_F10: leaveMenu(); return true;
	}
	return false;
}

LRESULT CALLBACK ClientFrame::filterMenu(int code, WPARAM wParam, LPARAM lParam) {
	auto frame = trackingFrame;
	if(code == MSGF_MENU && frame) {
		const auto& msg = *reinterpret_cast<MSG*>(lParam);
		if(msg.message == WM_KEYDOWN) {
			const auto popup = ::GetSubMenu(::GetMenu(frame->owner->handle()), frame->tracking);
			const bool rtl = frame->owner->hasExStyle(WS_EX_LAYOUTRTL);
			const bool previous = msg.wParam == (rtl ? VK_RIGHT : VK_LEFT) && frame->selectedMenu == popup;
			const bool next = msg.wParam == (rtl ? VK_LEFT : VK_RIGHT) && !(frame->selectedFlags & MF_POPUP);
			if(previous || next) {
				frame->nextMenu = frame->adjacentMenu(frame->tracking, previous ? -1 : 1);
				::EndMenu();
				return TRUE;
			}
		} else if(msg.message == WM_MOUSEMOVE || msg.message == WM_NCMOUSEMOVE) {
			for(size_t i = 0; i < frame->items.size(); ++i) {
				RECT rect;
				::GetWindowRect(frame->items[i].button->handle(), &rect);
				if(::PtInRect(&rect, msg.pt) && static_cast<int>(i) != frame->tracking && frame->items[i].button->getEnabled()) {
					frame->nextMenu = static_cast<int>(i);
					::EndMenu();
					return TRUE;
				}
			}
		}
	}
	return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

void ClientFrame::openMenu(size_t index, bool keyboard) {
	if(tracking >= 0 || index >= items.size() || !items[index].button->getEnabled()) return;
	focusMenu(static_cast<int>(index));
	const auto window = owner->handle();
	const auto chrome = handle();
	const auto root = ::GetMenu(window);
	owner->sendMessage(WM_INITMENU, reinterpret_cast<WPARAM>(root));
	if(hwnd_cast<ClientFrame*>(chrome) != this || !::IsMenu(root) || ::GetMenu(window) != root || index >= items.size()) return;
	trackingFrame = this;
	menuHook = ::SetWindowsHookEx(WH_MSGFILTER, filterMenu, nullptr, ::GetCurrentThreadId());
	do {
		tracking = static_cast<int>(index);
		nextMenu = -1;
		selectedMenu = ::GetSubMenu(root, static_cast<int>(index));
		selectedFlags = 0;
		// The original heading can still have focus or a native hot state while
		// TrackPopupMenu captures input. Repaint every heading for the new selection.
		redrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		if(selectedMenu) {
			RECT rect;
			::GetWindowRect(items[index].button->handle(), &rect);
			if(keyboard) ::PostMessage(window, WM_KEYDOWN, VK_DOWN, 0);
			const bool rtl = owner->hasExStyle(WS_EX_LAYOUTRTL);
			::TrackPopupMenu(selectedMenu, TPM_LEFTBUTTON | (rtl ? TPM_RIGHTALIGN | TPM_LAYOUTRTL : TPM_LEFTALIGN), rtl ? rect.right : rect.left, rect.bottom, 0, window, nullptr);
			// Commands and modal menu messages can destroy the owner synchronously.
			if(hwnd_cast<ClientFrame*>(chrome) != this) return;
		} else {
			MENUINFO info = { sizeof(MENUINFO), MIM_STYLE };
			::GetMenuInfo(root, &info);
			if(info.dwStyle & MNS_NOTIFYBYPOS) ::PostMessage(window, WM_MENUCOMMAND, index, reinterpret_cast<LPARAM>(root));
			else ::PostMessage(window, WM_COMMAND, ::GetMenuItemID(root, static_cast<int>(index)), 0);
		}
		index = static_cast<size_t>(nextMenu);
	} while(nextMenu >= 0 && index < items.size() && ::IsMenu(root));
	if(menuHook) ::UnhookWindowsHookEx(menuHook);
	menuHook = nullptr;
	trackingFrame = nullptr;
	tracking = -1;
	leaveMenu();
	if(hwnd_cast<ClientFrame*>(chrome) == this) layout();
}

void ClientFrame::themeSystemMenu(HMENU menu) {
	if(!menu || menu != ::GetSystemMenu(owner->handle(), FALSE)) return;
	restoreSystemMenu();
	nativeSystemMenu = menu;
	systemMenu = owner->addChild(Menu::Seed(true, Point(owner->scale(16), owner->scale(16)), menuFont));
	MENUINFO background = { sizeof(MENUINFO), MIM_BACKGROUND };
	::GetMenuInfo(menu, &background);
	systemMenuBackground = background.hbrBack;
	systemMenuBrush = new Brush(getAppearance().getPalette().surface);
	background.hbrBack = systemMenuBrush->handle();
	::SetMenuInfo(menu, &background);
	const int count = ::GetMenuItemCount(menu);
	for(int index = 0; index < count; ++index) {
		MENUITEMINFO original = { sizeof(MENUITEMINFO), MIIM_FTYPE | MIIM_DATA | MIIM_BITMAP | MIIM_ID | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING };
		if(!::GetMenuItemInfo(menu, index, TRUE, &original) || (original.fType & MFT_OWNERDRAW)) continue;
		std::vector<TCHAR> text(original.cch + 1);
		original.dwTypeData = text.data();
		original.cch = static_cast<UINT>(text.size());
		if(!::GetMenuItemInfo(menu, index, TRUE, &original)) continue;
		const auto target = systemMenu->size();
		if(original.fType & MFT_SEPARATOR) systemMenu->appendSeparator();
		else if(original.hSubMenu) {
			systemMenu->appendPopup(text.data(), IconPtr(), false);
			systemMenu->setItemEnabled(target, !(original.fState & MFS_DISABLED));
		} else systemMenu->appendItem(text.data(), {}, IconPtr(), !(original.fState & MFS_DISABLED), (original.fState & MFS_DEFAULT) != 0);
		if(original.fState & MFS_CHECKED) systemMenu->checkItem(target);
		MENUITEMINFO themed = { sizeof(MENUITEMINFO), MIIM_DATA };
		::GetMenuItemInfo(systemMenu->handle(), target, TRUE, &themed);
		original.dwTypeData = nullptr;
		systemItems.push_back({ original, themed.dwItemData });
		themed.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_BITMAP;
		themed.fType = original.fType | MFT_OWNERDRAW;
		themed.hbmpItem = nullptr;
		// Only borrow DWT drawing data. USER32 keeps the original command IDs,
		// labels, state, submenus and system-menu input/dispatch behavior.
		::SetMenuItemInfo(menu, index, TRUE, &themed);
	}
}

void ClientFrame::restoreSystemMenu() {
	if(nativeSystemMenu && ::IsMenu(nativeSystemMenu)) {
		const int count = ::GetMenuItemCount(nativeSystemMenu);
		for(int index = 0; index < count; ++index) {
			MENUITEMINFO info = { sizeof(MENUITEMINFO), MIIM_DATA };
			if(!::GetMenuItemInfo(nativeSystemMenu, index, TRUE, &info)) continue;
			const auto item = std::find_if(systemItems.begin(), systemItems.end(), [&info](const SystemMenuItem& entry) { return entry.data == info.dwItemData; });
			if(item == systemItems.end()) continue;
			info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_BITMAP;
			info.fType = item->original.fType;
			info.dwItemData = item->original.dwItemData;
			info.hbmpItem = item->original.hbmpItem;
			::SetMenuItemInfo(nativeSystemMenu, index, TRUE, &info);
		}
		MENUINFO background = { sizeof(MENUINFO), MIM_BACKGROUND };
		background.hbrBack = systemMenuBackground;
		::SetMenuInfo(nativeSystemMenu, &background);
	}
	nativeSystemMenu = nullptr;
	systemItems.clear();
	systemMenu.reset();
	systemMenuBrush.reset();
}

bool ClientFrame::drawSystemMenuItem(DRAWITEMSTRUCT& data) {
	if(data.CtlType != ODT_MENU || !nativeSystemMenu) return false;
	const auto item = std::find_if(systemItems.begin(), systemItems.end(), [&data](const SystemMenuItem& entry) { return entry.data == data.itemData; });
	if(item == systemItems.end()) return false;
	CanvasState saved(data.hDC);
	Menu::handlePainting(data);
	saved.restore();
	const auto command = item->original.wID;
	if(command != SC_CLOSE && command != SC_MINIMIZE && command != SC_MAXIMIZE && command != SC_RESTORE) return true;
	const auto& colors = getAppearance().getPalette();
	const bool disabled = (data.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
	const bool selected = (data.itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
	FreeCanvas canvas(data.hDC);
	Pen pen(disabled ? colors.disabledText : selected ? colors.highlightText : colors.text);
	auto selectPen = canvas.select(pen);
	const long x = data.rcItem.left + owner->scale(8) + Menu::textIconGap / 2;
	const long y = (data.rcItem.top + data.rcItem.bottom) / 2;
	const long radius = owner->scale(4);
	if(command == SC_CLOSE) {
		canvas.line(x - radius, y - radius, x + radius + 1, y + radius + 1);
		canvas.line(x + radius, y - radius, x - radius - 1, y + radius + 1);
	} else if(command == SC_MINIMIZE) {
		canvas.line(x - radius, y + radius / 2, x + radius + 1, y + radius / 2);
	} else {
		Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)), false);
		auto selectBrush = canvas.select(hollow);
		if(command == SC_RESTORE) {
			const long offset = owner->scale(2);
			canvas.rectangle(Rectangle(x - radius + offset, y - radius, radius * 2, radius * 2));
			canvas.fill(Rectangle(x - radius, y - radius + offset, radius * 2, radius * 2), Brush(selected && !disabled ? colors.accent : colors.background));
			canvas.rectangle(Rectangle(x - radius, y - radius + offset, radius * 2, radius * 2));
		} else canvas.rectangle(Rectangle(x - radius, y - radius, radius * 2, radius * 2));
	}
	return true;
}

bool ClientFrame::handleFrameMessage(const MSG& msg, LRESULT& result) {
	if(msg.message == WM_MENUCHAR && nativeSystemMenu && reinterpret_cast<HMENU>(msg.lParam) == nativeSystemMenu) {
		std::vector<int> matches;
		int highlighted = -1;
		for(int index = 0, count = ::GetMenuItemCount(nativeSystemMenu); index < count; ++index) {
			const auto state = ::GetMenuState(nativeSystemMenu, index, MF_BYPOSITION);
			if(state & MF_HILITE) highlighted = index;
			if(state & (MF_DISABLED | MF_GRAYED | MF_SEPARATOR)) continue;
			const int length = ::GetMenuString(nativeSystemMenu, index, nullptr, 0, MF_BYPOSITION);
			std::vector<TCHAR> text(length + 1);
			::GetMenuString(nativeSystemMenu, index, text.data(), static_cast<int>(text.size()), MF_BYPOSITION);
			for(int i = 0; i + 1 < length; ++i) {
				if(text[i] != _T('&')) continue;
				if(text[++i] == _T('&')) continue;
				if(std::towupper(text[i]) == std::towupper(static_cast<TCHAR>(LOWORD(msg.wParam)))) matches.push_back(index);
				break;
			}
		}
		if(!matches.empty()) {
			const auto next = std::find_if(matches.begin(), matches.end(), [highlighted](int index) { return index > highlighted; });
			result = MAKELRESULT(next == matches.end() ? matches.front() : *next, matches.size() == 1 ? MNC_EXECUTE : MNC_SELECT);
			return true;
		}
	}
	if(msg.message == WM_DRAWITEM && msg.lParam && drawSystemMenuItem(*reinterpret_cast<DRAWITEMSTRUCT*>(msg.lParam))) {
		result = TRUE;
		return true;
	}
	if(msg.message == WM_NCACTIVATE || msg.message == WM_ACTIVATE) {
		active = msg.message == WM_NCACTIVATE ? msg.wParam != FALSE : LOWORD(msg.wParam) != WA_INACTIVE;
		redrawWindow(RDW_INVALIDATE | RDW_ALLCHILDREN);
	}
	if(msg.message == WM_MENUSELECT && tracking >= 0) {
		selectedMenu = reinterpret_cast<HMENU>(msg.lParam);
		selectedFlags = HIWORD(msg.wParam);
	}
	if(msg.message == WM_SYSCOMMAND && (msg.wParam & 0xfff0) == SC_KEYMENU && msg.lParam != VK_SPACE && !items.empty()) {
		const int index = mnemonic(static_cast<TCHAR>(msg.lParam));
		if(index >= 0) openMenu(index, true);
		else if(menuFocused()) leaveMenu();
		else focusMenu(0);
		result = 0;
		return true;
	}
	return false;
}

}
