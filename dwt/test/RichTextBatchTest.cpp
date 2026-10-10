// Copyright (C) 2026 iceman50
// Noninteractive checks against the real Windows RichEdit control.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/RichTextBox.h>

#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

std::wstring text(dwt::RichTextBox* box) {
	std::vector<wchar_t> buffer(256 * 1024);
	GETTEXTEX config { static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr };
	auto copied = box->sendMessage(EM_GETTEXTEX, reinterpret_cast<WPARAM>(&config),
		reinterpret_cast<LPARAM>(buffer.data()));
	return std::wstring(buffer.data(), static_cast<size_t>(copied));
}

void checkBatches(dwt::RichTextBox* box, const wchar_t* separator, wchar_t plainSeparator,
	bool separateParagraphs = false) {
	box->setText(L"");
	box->setTextLimit(128 * 1024);
	box->setReadOnly();
	std::wstring expected;
	for(int batch = 0; batch < 4; ++batch) {
		std::vector<dwt::tstring> documents;
		std::vector<std::pair<int, int>> expectedRanges;
		for(int i = 0; i < 3; ++i) {
			const auto begin = static_cast<int>(expected.size());
			std::wstring document = L"{\\urtf1\n";
			if(!expected.empty()) {
				if(!separateParagraphs) document += separator;
				expected += plainSeparator;
			}
			document += L"\\pard{\\colortbl;\\red0\\green128\\blue255;}"
				L"{\\cf1 [05:52] <nick> caf\u00e9\\line second line}}";
			documents.push_back(document);
			expected += L"[05:52] <nick> caf\u00e9\vsecond line";
			expectedRanges.emplace_back(begin, static_cast<int>(expected.size()));
		}
		auto ranges = box->addTextSteadyBatch(documents, separateParagraphs);
		check(text(box) == expected, "Batch insertion changed message text or separators");
		check(box->length() == expected.size(), "Length is not in RichEdit character positions");
		check(ranges == expectedRanges, "Message ranges drift after line breaks");
		check(box->isReadOnly(), "Batch insertion did not restore read-only mode");
		check(box->getTextLimit() == 128 * 1024, "Batch insertion changed the configured limit");
		if(batch) check(box->getCaretPosRange() == std::make_pair(2, 9), "Batch insertion moved the selection");
		box->setSelection(2, 9);
	}

	box->setTextLimit(180);
	auto ranges = box->addTextSteadyBatch({ separateParagraphs ? L"{\\urtf1\\pard Last message}" :
		L"{\\urtf1\\par\\pard Last message}" }, separateParagraphs);
	const auto retained = text(box);
	check(retained.size() <= 180, "History was not trimmed to its character limit");
	check(box->length() == retained.size(), "Length drifted after history trimming");
	check(ranges.size() == 1 && ranges[0].second == static_cast<int>(retained.size()),
		"Trimmed message range does not end at the document end");
	check(retained.substr(ranges[0].first) == L"\rLast message", "Trimmed message range points into another message");
}

void checkParagraphs(dwt::RichTextBox* box) {
	box->setText(L"");
	box->setTextLimit(128 * 1024);
	box->addTextSteadyBatch({ L"{\\urtf1 Centered announcement}" }, true);
	PARAFORMAT before { sizeof(PARAFORMAT) };
	before.dwMask = PFM_ALIGNMENT | PFM_STARTINDENT | PFM_RIGHTINDENT;
	before.wAlignment = PFA_CENTER;
	before.dxStartIndent = 720;
	before.dxRightIndent = 1440;
	box->setSelection(0, 1);
	box->sendMessage(EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&before));
	box->addTextSteadyBatch({ L"{\\urtf1\\pard Normal chat}", L"{\\urtf1\\pard Next message}" }, true);
	check(text(box) == L"Centered announcement\rNormal chat\rNext message", "Messages do not occupy separate paragraphs");
	PARAFORMAT format { sizeof(PARAFORMAT) };
	box->setSelection(0, 1);
	box->sendMessage(EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format));
	check(format.wAlignment == PFA_CENTER, "Appending chat changed the announcement alignment");
	check(format.dxStartIndent == 720 && format.dxRightIndent == 1440, "Appending chat changed the announcement margins");
	box->setSelection(22, 23);
	box->sendMessage(EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format));
	check(format.wAlignment == PFA_LEFT, "Announcement alignment leaked into normal chat");
	check(format.dxStartIndent == 0 && format.dxRightIndent == 0, "Announcement margins leaked into normal chat");
}
}

int dwtMain(dwt::Application&) {
	try {
		auto window = new dwt::Window();
		window->create();
		auto box = window->addChild(dwt::RichTextBox::Seed());
		box->resize(dwt::Rectangle(0, 0, 800, 600));
		checkBatches(box, L"\\line\n", L'\v');
		checkBatches(box, L"\\par\n", L'\r');
		checkBatches(box, L"", L'\r', true);
		checkParagraphs(box);
		::DestroyWindow(window->handle());
		std::puts("RichEdit batch, character range, trimming and paragraph checks passed.");
		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}

