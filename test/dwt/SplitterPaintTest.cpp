// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/ModalDialog.h>
#include <dwt/widgets/SplitterContainer.h>
#include <dwt/widgets/Splitter.h>
#include <dwt/resources/Brush.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

class Dialog : public dwt::ModalDialog {
public:
	explicit Dialog(dwt::Widget* parent) : ModalDialog(parent) { }
};

void checkPainting(dwt::Splitter* splitter, bool manual) {
	const auto size = splitter->getClientSize();
	const dwt::Rectangle bounds(size);
	auto dc = GetDC(splitter->handle());
	{
		dwt::BufferedCanvas<dwt::FreeCanvas> canvas(dc, bounds);
		const COLORREF sentinel = RGB(211, 9, 177);
		canvas.fill(bounds, dwt::Brush(sentinel));
		check(SendMessage(splitter->handle(), WM_ERASEBKGND, reinterpret_cast<WPARAM>(canvas.handle()), 0),
			"Splitter did not handle background erasure");
		check(GetPixel(canvas.handle(), 0, 0) == sentinel,
			"Splitter exposes an intermediate background before painting");
		SendMessage(splitter->handle(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.handle()), PRF_CLIENT);
		const auto& palette = dwt::Application::instance().getAppearance().getPalette();
		const auto background = manual ? palette.background : GetSysColor(COLOR_3DFACE);
		const auto border = manual ? palette.border : GetSysColor(COLOR_3DSHADOW);
		check(GetPixel(canvas.handle(), 0, 0) == background, "Splitter did not paint its own background");
		check(GetPixel(canvas.handle(), size.x / 2, size.y / 2) == border, "Splitter divider is missing");
		if(manual) {
			SendMessage(splitter->handle(), WM_MOUSEMOVE, 0, MAKELPARAM(1, 1));
			SendMessage(splitter->handle(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.handle()), PRF_CLIENT);
			check(GetPixel(canvas.handle(), 0, 0) == dwt::Appearance::blend(palette.background, palette.accent, 48),
				"Splitter hover background changed");
			check(GetPixel(canvas.handle(), size.x / 2, size.y / 2) == palette.accent, "Splitter hover divider changed");
			SendMessage(splitter->handle(), WM_MOUSELEAVE, 0, 0);
		}
	}
	ReleaseDC(splitter->handle(), dc);
}
}

int dwtMain(dwt::Application& app) {
	try {
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed();
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		for(auto mode: { dwt::Appearance::Mode::Dark, dwt::Appearance::Mode::Light }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			for(bool horizontal: { false, true }) {
				auto container = window->addChild(dwt::SplitterContainer::Seed(0.5, horizontal));
				auto splitter = container->addChild(dwt::Splitter::Seed(0.5, horizontal));
				splitter->resize(dwt::Rectangle(0, 0, horizontal ? 300 : 6, horizontal ? 6 : 300));
				checkPainting(splitter, app.getAppearance().isManual());
				Dialog dialog(window);
				dialog.create(dwt::ModalDialog::Seed(dwt::Point(200, 100)));
				dialog.resize(dwt::Rectangle(-32000, -32000, 200, 100));
				std::exception_ptr error;
				app.callAsync([&] {
					try {
						check(!window->getEnabled(), "Modal dialog did not disable its owner");
						checkPainting(splitter, app.getAppearance().isManual());
					} catch(...) { error = std::current_exception(); }
					dialog.endDialog(IDCANCEL);
				});
				dialog.show();
				if(error) std::rethrow_exception(error);
				checkPainting(splitter, app.getAppearance().isManual());
				DestroyWindow(container->handle());
			}
		}
		DestroyWindow(window->handle());
		std::puts("Splitter painting passed: erase isolation, background/divider colors, hover, both orientations, dark/light and modal open/close.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
