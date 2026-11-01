// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/ModalDialog.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/resources/Bitmap.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void pump() {
	for(unsigned i = 0; i < 1000 && dwt::Application::instance().dispatch(); ++i) { }
}

class Dialog : public dwt::ModalDialog {
public:
	explicit Dialog(dwt::Widget* parent) : ModalDialog(parent) {
		onInitDialog([this] {
			content = addChild(dwt::TextBox::Seed());
			layout();
			return true;
		});
	}
	void layout() override {
		if(content) content->resize(getContentRect());
	}
	dwt::TextBox* content = nullptr;
};

void checkBorder(Dialog& dialog, COLORREF color) {
	const auto size = dialog.getClientSize();
	const auto width = dialog.getDialogBorderWidth();
	check(width > 0, "Dialog did not reserve a border");
	auto dc = GetDC(dialog.handle());
	auto memory = CreateCompatibleDC(dc);
	dwt::Bitmap bitmap(CreateCompatibleBitmap(dc, size.x, size.y));
	auto previous = SelectObject(memory, bitmap.handle());
	SendMessage(dialog.handle(), WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
	bool continuous = true;
	for(int inset = 0; inset < width; ++inset) {
		for(int x = 0; x < size.x; ++x)
			continuous &= GetPixel(memory, x, inset) == color && GetPixel(memory, x, size.y - 1 - inset) == color;
		for(int y = 0; y < size.y; ++y)
			continuous &= GetPixel(memory, inset, y) == color && GetPixel(memory, size.x - 1 - inset, y) == color;
	}
	SelectObject(memory, previous);
	DeleteDC(memory);
	ReleaseDC(dialog.handle(), dc);
	check(continuous, "Dialog border was missing or covered by title/content controls");
	const auto content = dialog.getContentRect();
	check(content.left() >= width && content.right() <= size.x - width && content.bottom() <= size.y - width,
		"Content overlaps the dialog outline");
	RECT bounds { 0, 0, content.width(), content.height() };
	dialog.adjustContentRect(bounds);
	check(bounds.right - bounds.left == size.x && bounds.bottom - bounds.top == size.y,
		"Dialog content and outer-size calculations disagree");
}
}

int dwtMain(dwt::Application& app) {
	try {
		auto& appearance = app.getAppearance();
		auto palette = dwt::Appearance::defaultPalette();
		palette.border = RGB(0, 230, 255);
		appearance.configure(dwt::Appearance::Mode::Dark, palette);
		check(appearance.getDialogBorders(), "Dialog borders should be enabled by default");
		auto owner = new dwt::Window();
		auto ownerSeed = dwt::Window::Seed(L"Dialog border test");
		ownerSeed.style &= ~WS_VISIBLE;
		owner->create(ownerSeed);
		check(owner->getDialogBorderWidth() == 0, "Modal border preference affected the main window");
		{
			Dialog dialog(owner);
			dialog.create(dwt::ModalDialog::Seed(dwt::Point(420, 240)));
			SetWindowPos(dialog.handle(), nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			ShowWindow(dialog.handle(), SW_SHOWNOACTIVATE);
			pump();
			check(dialog.usesClientFrame(), "Dark dialog did not use a custom frame");
			for(int dpi: { 96, 144, 192 }) {
				SendMessage(dialog.handle(), WM_DPICHANGED, MAKELONG(dpi, dpi), 0);
				pump();
				SendMessage(dialog.handle(), WM_NCACTIVATE, TRUE, 0);
				// Synthetic notifications exercise relayout; scaling still uses the window's actual monitor DPI.
				check(dialog.getDialogBorderWidth() == std::max(1, dialog.scale(1)), "Border did not match the window DPI");
				checkBorder(dialog, palette.border);
				SendMessage(dialog.handle(), WM_NCACTIVATE, FALSE, 0);
				checkBorder(dialog, dwt::Appearance::blend(palette.background, palette.border, 128));
			}
			POINT caption { dialog.getClientSize().x / 2, dialog.getContentRect().top() - 2 };
			ClientToScreen(dialog.handle(), &caption);
			check(SendMessage(dialog.handle(), WM_NCHITTEST, 0, MAKELPARAM(caption.x, caption.y)) == HTCAPTION,
				"Inset title bar lost its draggable caption area");
			appearance.setDialogBorders(false);
			pump();
			check(dialog.getDialogBorderWidth() == 0 && dialog.getContentRect().left() == 0 &&
				dialog.getContentRect().bottom() == dialog.getClientSize().y, "Disabling the border left an inset");
			appearance.setDialogBorders(true);
			palette.border = RGB(255, 220, 0);
			appearance.configure(dwt::Appearance::Mode::Dark, palette);
			pump();
			SendMessage(dialog.handle(), WM_NCACTIVATE, TRUE, 0);
			checkBorder(dialog, palette.border);
			dialog.resize(dwt::Rectangle(-32000, -32000, 510, 300));
			pump();
			checkBorder(dialog, palette.border);
			dialog.addRemoveStyle(WS_THICKFRAME, true);
			pump();
			SendMessage(dialog.handle(), WM_NCACTIVATE, TRUE, 0);
			checkBorder(dialog, palette.border);
			dialog.addRemoveStyle(WS_THICKFRAME, false);
			dialog.setFrameRendering(dwt::Frame::FrameRendering::Native);
			pump();
			check(!dialog.usesClientFrame() && dialog.getDialogBorderWidth() == 0, "Native frame acquired a custom border");
			check((GetWindowLongPtr(dialog.handle(), GWL_EXSTYLE) & WS_EX_DLGMODALFRAME) != 0, "Native modal frame style was lost");
			dialog.setFrameRendering(dwt::Frame::FrameRendering::Automatic);
			dialog.setAppearancePolicy(dwt::AppearancePolicy::Native);
			pump();
			check(dialog.getDialogBorderWidth() == 0, "Native appearance policy acquired a custom border");
			dialog.setAppearancePolicy(dwt::AppearancePolicy::Inherit);
			appearance.configure(dwt::Appearance::Mode::Light, palette);
			pump();
			check(!dialog.usesClientFrame() && dialog.getDialogBorderWidth() == 0, "System light frame acquired a custom border");
			DestroyWindow(dialog.handle());
		}
		DestroyWindow(owner->handle());
		std::puts("Dialog borders passed: all four edges, child clipping, DPI notifications, caption hit testing, activation, toggling, resizing, theme and native-frame transitions.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
