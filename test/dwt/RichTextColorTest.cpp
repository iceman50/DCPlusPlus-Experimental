// Copyright (C) 2026 iceman50
// Check live RichEdit color changes without requiring scrolling or user input.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/RichTextBox.h>
#include <dwt/Application.h>
#include <dwt/resources/Font.h>

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
LRESULT CALLBACK withoutTextDocument(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
	if(message == EM_GETOLEINTERFACE) {
		*reinterpret_cast<void**>(lParam) = nullptr;
		return 0;
	}
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
		paints = 0;
		{
			dwt::Appearance::UpdateScope update(dwt::Application::instance().getAppearance());
			{
				dwt::Appearance::UpdateScope nested(dwt::Application::instance().getAppearance());
				box->setColor(RGB(0, 0, 0), RGB(255, 255, 255));
			}
			check(paints == 0, "Nested theme change painted before the update completed");
			box->redraw(dwt::Rectangle(0, 0, 40, 40), true);
			check(paints == 0, "Partial redraw bypassed the theme update scope");
		}
		check(paints != 0, "Completed theme update left chat waiting for paint");

		// Compare every character across mixed formatting, paragraph boundaries,
		// Unicode and links; range batching must preserve the old per-character behavior.
		box->setReadOnly(false);
		box->setText(L"");
		box->addTextSteady(L"{\\urtf1{\\colortbl;\\red0\\green0\\blue0;\\red180\\green30\\blue60;"
			L"\\red255\\green255\\blue255;\\red240\\green220\\blue100;}"
			L"\\cf1\\highlight3 Ordinary {\\b bold} {\\i italic} {\\ul underlined} "
			L"{\\uldb double} {\\strike struck} {\\cf2\\highlight4 custom}\\par "
			L"Unicode \\u945?\\u946? \\u-10179?\\u-8704? "
			L"{\\field{\\*\\fldinst HYPERLINK \"https://example.invalid/\"}{\\fldrslt link}}}");
		const auto readFormats = [&] {
			std::vector<CHARFORMAT2> result(box->length());
			for(size_t i = 0; i < result.size(); ++i) {
				box->setSelection(static_cast<int>(i), static_cast<int>(i + 1));
				result[i].cbSize = sizeof(CHARFORMAT2);
				box->sendMessage(EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&result[i]));
			}
			return result;
		};
		const auto before = readFormats();
		const auto content = box->getText();
		box->setColor(RGB(210, 220, 230), RGB(15, 20, 25));
		const auto after = readFormats();
		check(box->getText() == content && before.size() == after.size(), "Range recoloring changed text");
		for(size_t i = 0; i < before.size(); ++i) {
			check(after[i].crTextColor == (before[i].crTextColor == RGB(0, 0, 0) ? RGB(210, 220, 230) : before[i].crTextColor),
				"Range recoloring changed a custom text color");
			check(after[i].crBackColor == (before[i].crBackColor == RGB(255, 255, 255) ? RGB(15, 20, 25) : before[i].crBackColor),
				"Range recoloring changed a custom background");
			check(after[i].dwEffects == before[i].dwEffects && after[i].bUnderlineType == before[i].bUnderlineType &&
				after[i].yHeight == before[i].yHeight && std::wcscmp(after[i].szFaceName, before[i].szFaceName) == 0,
				"Range recoloring changed font effects or links");
		}
		LOGFONT font {};
		GetObject(GetStockObject(DEFAULT_GUI_FONT), sizeof(font), &font);
		font.lfHeight = -18;
		box->setFont(dwt::FontPtr(new dwt::Font(font)));
		const auto afterFont = readFormats();
		for(size_t i = 0; i < after.size(); ++i) {
			check(afterFont[i].crTextColor == after[i].crTextColor && afterFont[i].crBackColor == after[i].crBackColor &&
				((afterFont[i].dwEffects ^ after[i].dwEffects) & (CFE_LINK | CFE_UNDERLINE)) == 0,
				"Font update lost custom colors or link formatting");
		}
		check(SetWindowSubclass(box->handle(), withoutTextDocument, 2, 0), "Cannot test TOM fallback");
		box->setSelection(4, 11);
		box->setColor(RGB(0, 0, 0), RGB(255, 255, 255));
		check(box->getCaretPosRange() == std::make_pair(4, 11), "Fallback recoloring moved the selection");
		const auto fallback = readFormats();
		check(box->getText() == content && fallback.size() == before.size(), "Fallback recoloring changed text");
		for(size_t i = 0; i < before.size(); ++i) {
			check(fallback[i].crTextColor == before[i].crTextColor && fallback[i].crBackColor == before[i].crBackColor &&
				((fallback[i].dwEffects ^ before[i].dwEffects) & (CFE_LINK | CFE_UNDERLINE)) == 0,
				"Fallback recoloring lost colors or links");
		}
		RemoveWindowSubclass(box->handle(), withoutTextDocument, 2);
		::RemoveWindowSubclass(box->handle(), countPaints, 1);
		::DestroyWindow(window->handle());
		std::puts("RichEdit immediate color repaint, selection and scroll checks passed.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
