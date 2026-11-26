// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// A queued callback may run a nested message loop: a modal dialog (ModalDialog::show) opened from
// one, as DC++ does for Settings on first start and for a magnet link given on the command line.
// Callbacks queued meanwhile must still run inside that loop: the dialog's own controls depend on
// them (the Settings page host ends its composited painting from one), and so do all other windows.
#include <dwt/Application.h>
#include <dwt/widgets/ModalDialog.h>
#include <dwt/widgets/Window.h>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

class TestDialog : public dwt::ModalDialog {
public:
	explicit TestDialog(dwt::Widget* parent) : ModalDialog(parent) { }
	int run() {
		create(Seed(dwt::Point(120, 60)));
		return show();
	}
	using ModalDialog::endDialog;
};
}

int dwtMain(dwt::Application& app) {
	try {
		auto window = new dwt::Window();
		auto seed = dwt::Window::Seed(L"Nested async test");
		seed.location = dwt::Rectangle(-32000, -32000, 200, 100);
		window->create(seed);

		// A nested loop run by hand.
		bool outerDone = false, innerRan = false, innerRanInside = false;
		app.callAsync([&] {
			app.callAsync([&] { innerRan = true; });
			for(int i = 0; i < 10000 && !innerRan; ++i) {
				app.dispatch();
			}
			innerRanInside = innerRan;
			outerDone = true;
		});
		for(int i = 0; i < 20000 && !outerDone; ++i) {
			app.dispatch();
		}
		check(outerDone, "The queued callback did not run");
		check(innerRanInside, "A callback queued during a nested loop waited for the outer callback");

		// A modal dialog opened from a queued callback, closed by a callback queued from inside it.
		bool closedInside = false;
		int result = 0;
		app.callAsync([&] {
			TestDialog dialog(window);
			app.callAsync([&] { closedInside = true; dialog.endDialog(IDOK); });
			result = dialog.run();
		});
		for(int i = 0; i < 20000 && !closedInside; ++i) {
			if(!app.dispatch()) app.sleep();
		}
		check(closedInside && result == IDOK, "A modal dialog opened from a queued callback blocked other callbacks");

		DestroyWindow(window->handle());
		std::puts("PASS: callbacks queued during a nested loop or a modal dialog run inside it");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}
