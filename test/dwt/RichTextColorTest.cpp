// Copyright (C) 2026 iceman50
// Check live RichEdit color changes without requiring scrolling or user input.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/RichTextBox.h>

#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

LRESULT CALLBACK countPaints(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
	UINT_PTR, DWORD_PTR data) {
	if(message == WM_PAINT) ++*reinterpret_cast<unsigned*>(data);
	return ::DefSubclassProc(window, message, wParam, lParam);
}
}

int dwtMain(dwt::Application&) {
	try {
		auto window = new dwt::Window();
		window->create();
		// Exercise real WM_PAINT handling off screen without disturbing the desktop.
		window->resize(dwt::Rectangle(-32000, -32000, 600, 300));
		auto box = window->addChild(dwt::RichTextBox::Seed());
		box->resize(dwt::Rectangle(0, 0, 550, 220));
		box->setColor(RGB(0, 0, 0), RGB(255, 255, 255));
		std::vector<dwt::tstring> documents(50, L"{\\urtf1 Chat message}");
		box->addTextSteadyBatch(documents, true);
		box->setReadOnly();
		window->setVisible(true);
		box->setSelection(3, 9);
		dwt::Point scroll(0, 100);
		box->setScrollPos(scroll);
		const auto selection = box->getCaretPosRange();
		const auto previousScroll = box->getScrollPos();
		box->redraw(true);
		unsigned paints = 0;
		check(::SetWindowSubclass(box->handle(), countPaints, 1, reinterpret_cast<DWORD_PTR>(&paints)),
			"Could not observe RichEdit paint messages");

		box->setColor(RGB(240, 240, 240), RGB(24, 24, 24));
		check(paints != 0, "Color update did not repaint until another event");
		check(box->getCaretPosRange() == selection, "Color update changed the user's selection");
		const auto currentScroll = box->getScrollPos();
		check(currentScroll.x == previousScroll.x && currentScroll.y == previousScroll.y,
			"Color update moved the chat viewport");
		check(box->isReadOnly(), "Color update changed read-only mode");
		box->setSelection(0, 1);
		CHARFORMAT2 format { sizeof(CHARFORMAT2) };
		box->sendMessage(EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
		check(format.crTextColor == RGB(240, 240, 240), "Existing chat text retained the old color");
		::RemoveWindowSubclass(box->handle(), countPaints, 1);
		::DestroyWindow(window->handle());
		std::puts("RichEdit immediate color repaint, selection and scroll checks passed.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
