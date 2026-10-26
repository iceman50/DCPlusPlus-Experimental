/* Copyright (C) 2026 iceman50. GPL-3.0-or-later. */
#include "../../dwt/src/util/win32/ThemedFileDialog.h"
#include <dwt/widgets/SaveDialog.h>
#include <filesystem>
#include <fstream>
#include <dwt/Application.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/ComboBox.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>

using dwt::tstring;
using dwt::util::win32::ThemedFileDialog;
using dwt::util::win32::FileDialogOptions;

namespace {
FileDialogOptions pickerOptions(dwt::Widget* parent, const tstring& title, const tstring& path) {
	FileDialogOptions options;
	options.owner = parent ? parent->handle() : nullptr;
	options.save = true;
	options.allowOverwrite = false;
	options.title = title;
	options.initialFileName = path;
	return options;
}
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

struct Controls {
	dwt::Table* table = nullptr;
	std::vector<dwt::TextBox*> edits;
	std::vector<HWND> buttons;
	std::vector<dwt::ComboBox*> combos;
};
Controls controls(HWND window) {
	Controls result;
	EnumChildWindows(window, [](HWND child, LPARAM parameter) -> BOOL {
		auto& result = *reinterpret_cast<Controls*>(parameter);
		auto widget = dwt::hwnd_cast<dwt::Widget*>(child);
		if(auto table = dynamic_cast<dwt::Table*>(widget)) result.table = table;
		if(auto edit = dynamic_cast<dwt::TextBox*>(widget)) result.edits.push_back(edit);
		if(dynamic_cast<dwt::Button*>(widget)) result.buttons.push_back(child);
		if(auto combo = dynamic_cast<dwt::ComboBox*>(widget)) result.combos.push_back(combo);
		return TRUE;
	}, reinterpret_cast<LPARAM>(&result));
	return result;
}

HWND button(const Controls& controls, const wchar_t* caption) {
	for(auto handle : controls.buttons) {
		wchar_t text[128] {};
		GetWindowText(handle, text, 128);
		if(lstrcmp(text, caption) == 0) return handle;
	}
	throw std::runtime_error("Missing picker button");
}

void pump(dwt::Application& app, const std::function<bool()>& ready) {
	const auto deadline = GetTickCount64() + 10000;
	do {
		for(unsigned i = 0; i < 50; ++i) app.dispatch();
		if(ready()) return;
		Sleep(1);
	} while(GetTickCount64() < deadline);
	throw std::runtime_error("Timed out waiting for the folder picker");
}

void click(HWND handle) { SendMessage(handle, BM_CLICK, 0, 0); }

void checkEditAlignment(dwt::TextBox* edit) {
	RECT field {}, bounds {};
	GetClientRect(GetParent(edit->handle()), &field);
	GetWindowRect(edit->handle(), &bounds);
	MapWindowPoints(HWND_DESKTOP, GetParent(edit->handle()), reinterpret_cast<POINT*>(&bounds), 2);
	auto dc = GetDC(edit->handle());
	auto previous = SelectObject(dc, edit->getFont()->handle());
	TEXTMETRIC metrics {};
	GetTextMetrics(dc, &metrics);
	SelectObject(dc, previous);
	ReleaseDC(edit->handle(), dc);
	const auto position = edit->sendMessage(EM_POSFROMCHAR, 0);
	const int top = bounds.top + static_cast<short>(HIWORD(position));
	const int bottom = field.bottom - top - metrics.tmHeight;
	check(top >= 2 && bottom >= 2 && std::abs(top - bottom) <= 1,
		"Filename/folder text and caret are not vertically centered in the field");
	check(!(GetWindowLongPtr(edit->handle(), GWL_STYLE) & ES_MULTILINE),
		"Padded filename field lost single-line keyboard behavior");
}

void checkButtonAlignment(HWND button, COLORREF textColor) {
	wchar_t caption[128] {};
	GetWindowText(button, caption, std::size(caption));
	SetWindowText(button, L"HHHH");
	RECT bounds {};
	GetClientRect(button, &bounds);
	auto dc = GetDC(button), actual = CreateCompatibleDC(dc), expected = CreateCompatibleDC(dc);
	auto actualBitmap = CreateCompatibleBitmap(dc, bounds.right, bounds.bottom);
	auto expectedBitmap = CreateCompatibleBitmap(dc, bounds.right, bounds.bottom);
	auto oldActual = SelectObject(actual, actualBitmap), oldExpected = SelectObject(expected, expectedBitmap);
	PrintWindow(button, actual, PW_CLIENTONLY);
	FillRect(expected, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
	auto oldFont = SelectObject(expected, reinterpret_cast<HFONT>(SendMessage(button, WM_GETFONT, 0, 0)));
	SetBkMode(expected, TRANSPARENT);
	SetTextColor(expected, textColor);
	DrawText(expected, L"HHHH", -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	auto inkBounds = [&](HDC source) {
		RECT ink { bounds.right, bounds.bottom, -1, -1 };
		for(int y = 2; y < bounds.bottom - 2; ++y) for(int x = 2; x < bounds.right - 2; ++x) {
			if(GetPixel(source, x, y) == textColor) {
				ink.left = std::min<LONG>(ink.left, x); ink.right = std::max<LONG>(ink.right, x);
				ink.top = std::min<LONG>(ink.top, y); ink.bottom = std::max<LONG>(ink.bottom, y);
			}
		}
		return ink;
	};
	const auto rendered = inkBounds(actual), reference = inkBounds(expected);
	SelectObject(expected, oldFont);
	SelectObject(actual, oldActual); SelectObject(expected, oldExpected);
	DeleteObject(actualBitmap); DeleteObject(expectedBitmap);
	DeleteDC(actual); DeleteDC(expected); ReleaseDC(button, dc);
	SetWindowText(button, caption);
	check(rendered.right >= rendered.left && EqualRect(&rendered, &reference),
		"Themed button text does not match centered text with symmetric padding");
}

bool folderPromptCompleted = false;
void CALLBACK fillFolderPrompt(HWND, UINT, UINT_PTR, DWORD) {
	auto dialog = FindWindow(nullptr, L"New folder");
	DWORD process = 0;
	GetWindowThreadProcessId(dialog, &process);
	if(!dialog || process != GetCurrentProcessId()) return;
	auto ui = controls(dialog);
	if(ui.edits.size() != 1) return;
	ui.edits.front()->setText(L"created-by-picker");
	for(auto handle : ui.buttons) {
		if(GetDlgCtrlID(handle) == IDOK) {
			folderPromptCompleted = true;
			click(handle);
			return;
		}
	}
}

bool cancelWithClose = false;
void CALLBACK cancelPicker(HWND, UINT, UINT_PTR, DWORD) {
	auto dialog = FindWindow(nullptr, L"Cancel picker regression");
	DWORD process = 0;
	GetWindowThreadProcessId(dialog, &process);
	if(!dialog || process != GetCurrentProcessId()) return;
	if(cancelWithClose) { PostMessage(dialog, WM_CLOSE, 0, 0); return; }
	for(auto handle : controls(dialog).buttons)
		if(GetDlgCtrlID(handle) == IDCANCEL) { click(handle); return; }
}

bool automaticSeen = false, automaticCancel = false;
void CALLBACK automaticPicker(HWND, UINT, UINT_PTR, DWORD) {
	auto dialog = FindWindow(nullptr, L"Automatic DWT SaveDialog");
	DWORD process = 0;
	GetWindowThreadProcessId(dialog, &process);
	if(!dialog || process != GetCurrentProcessId()) return;
	auto ui = controls(dialog);
	if(!ui.table || ui.edits.size() != 2) return;
	for(auto handle : ui.buttons) {
		const int command = automaticCancel ? IDCANCEL : IDOK;
		if(GetDlgCtrlID(handle) == command && IsWindowEnabled(handle)) {
			automaticSeen = true;
			if(!automaticCancel) ui.edits[1]->setText(L"automatic report");
			ui.edits[1]->setFocus();
			if(automaticCancel) PostMessage(ui.edits[1]->handle(), WM_KEYDOWN, VK_ESCAPE, 0);
			else click(handle);
			return;
		}
	}
}

int overwriteAnswer = IDNO;
bool overwriteSeen = false;
void CALLBACK answerOverwrite(HWND, UINT, UINT_PTR, DWORD) {
	auto dialog = FindWindow(WC_DIALOG, L"Overwrite regression");
	DWORD process = 0;
	GetWindowThreadProcessId(dialog, &process);
	if(!dialog || process != GetCurrentProcessId()) return;
	auto answer = GetDlgItem(dialog, overwriteAnswer);
	if(answer) { overwriteSeen = true; click(answer); }
}

void snapshot(HWND window, const tstring& path) {
	RECT rect {};
	GetWindowRect(window, &rect);
	const int width = rect.right - rect.left, height = rect.bottom - rect.top;
	auto dc = GetDC(window), memory = CreateCompatibleDC(dc);
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	void* pixels = nullptr;
	auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
	check(bitmap != nullptr, "Cannot capture picker");
	auto old = SelectObject(memory, bitmap);
	const bool rendered = PrintWindow(window, memory, 2) != FALSE;
	GdiFlush();
	BITMAPFILEHEADER header {};
	header.bfType = 0x4d42;
	header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
	header.bfSize = header.bfOffBits + width * height * 4;
	{
		std::ofstream file(std::filesystem::path(path), std::ios::binary);
		file.write(reinterpret_cast<const char*>(&header), sizeof(header));
		file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
		file.write(static_cast<const char*>(pixels), width * height * 4);
	}
	SelectObject(memory, old);
	DeleteObject(bitmap);
	DeleteDC(memory);
	ReleaseDC(window, dc);
	check(rendered, "Cannot render picker snapshot");
}
}

int dwtMain(dwt::Application& app) {
	try {
		wchar_t cwd[32768];
		check(GetCurrentDirectory(std::size(cwd), cwd) != 0, "Cannot locate test workspace");
		const auto directory = tstring(cwd) + L"\\build\\dwt-file-picker-test-data\\";
		std::filesystem::create_directories(directory + L"Downloads\\Subfolder");
		const auto downloads = directory + L"Downloads";
		RemoveDirectory((downloads + L"\\created-by-picker").c_str());
		for(unsigned i = 0; i < 200; ++i) {
			std::ofstream file(std::filesystem::path(downloads + L"\\existing-" + std::to_wstring(i) + L".txt"));
			file << "unchanged";
		}
		auto window = new dwt::Window;
		window->create(dwt::Window::Seed(L"Save picker test host"));
		unsigned translations = 0;
		LOGFONT logFont {};
		logFont.lfHeight = -14;
		logFont.lfWeight = FW_NORMAL;
		lstrcpy(logFont.lfFaceName, L"Segoe UI");
		dwt::FontPtr font = new dwt::Font(logFont);
		dwt::util::win32::setFileDialogDefaults({ font, [&](const char* text) {
			++translations;
			return tstring(text, text + std::strlen(text));
		} });
		auto palette = dwt::Appearance::defaultPalette();
		palette.background = RGB(12, 10, 30);
		palette.surface = RGB(29, 25, 57);
		palette.text = RGB(238, 231, 255);
		palette.accent = RGB(143, 111, 240);
		for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
			app.getAppearance().configure(mode, palette);
			ThemedFileDialog dialog(pickerOptions(window, L"Themed DWT - Save As", downloads + L"\\offered.txt"));
			dialog.create(dwt::ModalDialog::Seed(dwt::Point(780, 0)));
			dialog.setVisible(true);
			auto ui = controls(dialog.handle());
			check(ui.table && ui.edits.size() == 2, "Missing file list, folder or filename control");
			check(ui.edits[1]->getFont()->handle() == font->handle(), "Picker did not use the configured font");
			const auto save = button(ui, L"&Save");
			pump(app, [&] { return IsWindowEnabled(save) && ui.table->size() >= 201; });
			check(ui.edits[0]->getText() == downloads && ui.edits[1]->getText() == L"offered.txt",
				"Initial download folder and filename were not separated");
			for(auto edit : ui.edits) checkEditAlignment(edit);
			if(mode == dwt::Appearance::Mode::Dark)
				checkButtonAlignment(button(ui, L"Start &folder"), palette.text);
			SCROLLINFO scroll { sizeof(scroll), SIF_ALL };
			check(GetScrollInfo(ui.table->handle(), SB_VERT, &scroll) && scroll.nMax > static_cast<int>(scroll.nPage),
				"Folder list did not exercise its scrollbar");
			if(mode == dwt::Appearance::Mode::Dark)
				check(ListView_GetBkColor(ui.table->handle()) == app.getAppearance().getPalette().background,
					"Folder list did not use the selected palette");
			// Scroll input still reaches the native list beneath DWT's palette scrollbar.
			const auto initialTop = ui.table->getTopIndex();
			ui.table->sendMessage(WM_VSCROLL, SB_PAGEDOWN);
			check(ui.table->getTopIndex() > initialTop, "Themed list scrollbar does not scroll");
			ui.table->sendMessage(WM_VSCROLL, SB_TOP);
			if(mode == dwt::Appearance::Mode::Dark) {
				auto alternate = palette;
				alternate.background = RGB(9, 23, 31);
				app.getAppearance().configure(mode, alternate);
				check(ListView_GetBkColor(ui.table->handle()) == alternate.background, "Open picker did not follow a palette change");
				app.getAppearance().configure(mode, palette);
			}
			snapshot(dialog.handle(), directory + (mode == dwt::Appearance::Mode::Dark ? L"dark.bmp" : L"light.bmp"));

			// Existing files and invalid names must not dismiss the picker.
			for(auto name : { L"existing-0.txt", L"name:stream.txt", L"CON.txt", L"trailing. " }) {
				ui.edits[1]->setText(name);
				click(save);
				check(dialog.getPath().empty(), "Picker accepted an existing or invalid filename");
			}
			std::ifstream existing(std::filesystem::path(downloads + L"\\existing-0.txt"));
			std::string contents;
			std::getline(existing, contents);
			check(contents == "unchanged", "Browsing changed an existing file");

			// Enter on a folder row navigates; Back and Up restore the parent.
			ui.table->setSelected(0);
			ui.table->setFocus();
			PostMessage(ui.table->handle(), WM_KEYDOWN, VK_RETURN, 0);
			pump(app, [&] { return IsWindowEnabled(save) && ui.edits[0]->getText() == downloads + L"\\Subfolder"; });
			click(button(ui, L"&Back"));
			pump(app, [&] { return IsWindowEnabled(save) && ui.edits[0]->getText() == downloads; });

			// A later request wins even if an earlier scan is still in flight.
			ui.edits[0]->setText(downloads + L"\\Subfolder");
			click(button(ui, L"&Go"));
			click(button(ui, L"Start &folder"));
			pump(app, [&] { return IsWindowEnabled(save) && ui.table->size() >= 201; });
			check(ui.edits[0]->getText() == downloads, "Stale scan replaced the requested folder");
			ui.edits[0]->setText(downloads + L"\\does-not-exist");
			click(button(ui, L"&Go"));
			pump(app, [&] { return IsWindowEnabled(save); });
			check(ui.edits[0]->getText() == downloads, "Failed navigation lost the last usable folder");

			// Use the actual nested New Folder dialog, then return with Up.
			const auto created = downloads + L"\\created-by-picker";
			RemoveDirectory(created.c_str()); // Only the empty directory owned by this fixture.
			folderPromptCompleted = false;
			const auto timer = SetTimer(window->handle(), 9001, 50, fillFolderPrompt);
			check(timer != 0, "Cannot automate the new-folder prompt");
			click(button(ui, L"New f&older..."));
			KillTimer(window->handle(), timer);
			check(folderPromptCompleted, "New Folder prompt did not complete");
			pump(app, [&] { return IsWindowEnabled(save) && ui.edits[0]->getText() == created; });
			const auto attributes = GetFileAttributes(created.c_str());
			check(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY), "New Folder did not create a directory");
			click(button(ui, L"&Up"));
			pump(app, [&] { return IsWindowEnabled(save) && ui.edits[0]->getText() == downloads; });
			check(RemoveDirectory(created.c_str()) != FALSE, "Cannot remove the fixture's new folder");

			ui.edits[0]->setText(downloads + L"\\Subfolder");
			ui.edits[0]->setFocus();
			PostMessage(ui.edits[0]->handle(), WM_KEYDOWN, VK_RETURN, 0);
			pump(app, [&] { return IsWindowEnabled(save) && ui.table->size() == 0; });
			ui.edits[1]->setText(L"renamed \u00e9.txt");
			ui.edits[1]->setFocus();
			click(save);
			check(dialog.getPath() == downloads + L"\\Subfolder\\renamed \u00e9.txt", "Wrong chosen save path");
			check(GetFileAttributes(dialog.getPath().c_str()) == INVALID_FILE_ATTRIBUTES, "Picker created the destination prematurely");
			DestroyWindow(dialog.handle());
		}
		{
			const auto relative = std::filesystem::relative(std::filesystem::path(downloads + L"\\relative.txt"), cwd).wstring();
			ThemedFileDialog dialog(pickerOptions(window, L"Relative save path", relative));
			dialog.create(dwt::ModalDialog::Seed(dwt::Point(780, 0)));
			auto ui = controls(dialog.handle());
			pump(app, [&] { return IsWindowEnabled(button(ui, L"&Save")); });
			check(ui.edits[0]->getText() == downloads && ui.edits[1]->getText() == L"relative.txt", "Relative initial save path lost its parent folder");
			ui.edits[1]->setText(L"Subfolder\\typed.txt");
			ui.edits[1]->setFocus();
			click(button(ui, L"&Save"));
			pump(app, [&] { return IsWindowEnabled(button(ui, L"&Save")) && ui.edits[0]->getText() == downloads + L"\\Subfolder"; });
			click(button(ui, L"&Save"));
			check(dialog.getPath() == downloads + L"\\Subfolder\\typed.txt", "Typed relative save path lost its directory");
			DestroyWindow(dialog.handle());
		}
		// Public API must choose the themed renderer itself, including without an owner.
		for(bool cancel : { false, true }) {
			automaticSeen = false;
			automaticCancel = cancel;
			const auto timer = SetTimer(window->handle(), 9003, 50, automaticPicker);
			check(timer != 0, "Cannot automate the public SaveDialog API");
			tstring target = L"original";
			const auto selected = dwt::SaveDialog(cancel ? window : nullptr).setTitle(L"Automatic DWT SaveDialog")
				.setInitialDirectory(downloads).addFilter(L"Text and logs", L"*.txt;*.log")
				.setDefaultExtension(L"txt").setAllowOverwrite(false).open(target);
			KillTimer(window->handle(), timer);
			check(automaticSeen && selected != cancel, "SaveDialog did not automatically use the DWT picker");
			check(target == (cancel ? L"original" : downloads + L"\\automatic report.txt"), "Wrong extension or cancellation modified the path");
		}
		{
			auto options = pickerOptions(window, L"Filter regression", downloads + L"\\offered.txt");
			options.filters = { { L"Text", L"*.txt" }, { L"Logs", L"*.log" } };
			options.options = FOS_STRICTFILETYPES;
			ThemedFileDialog dialog(options);
			dialog.create(dwt::ModalDialog::Seed(dwt::Point(780, 0)));
			dialog.setVisible(true);
			auto ui = controls(dialog.handle());
			auto save = button(ui, L"&Save");
			pump(app, [&] { return IsWindowEnabled(save); });
			check(ui.combos.size() == 2 && ui.table->size() == 201, "Text filter lost files or folders");
			ui.combos[1]->setSelected(1);
			SendMessage(GetParent(ui.combos[1]->handle()), WM_COMMAND,
				MAKEWPARAM(GetDlgCtrlID(ui.combos[1]->handle()), CBN_SELENDOK), reinterpret_cast<LPARAM>(ui.combos[1]->handle()));
			check(ui.table->size() == 1, "Changing file type did not filter the directory list");
			ui.edits[1]->setFocus();
			ui.edits[1]->setText(L"wrong.txt");
			click(save);
			check(dialog.getPath().empty(), "Strict file type accepted the wrong extension");
			ui.edits[1]->setText(L"new report");
			click(save);
			check(dialog.getPath() == downloads + L"\\new report.log", "Selected filter extension was not applied");
			DestroyWindow(dialog.handle());
		}
		{
			auto options = pickerOptions(window, L"Overwrite regression", downloads + L"\\existing-0.txt");
			options.allowOverwrite = true;
			ThemedFileDialog dialog(options);
			dialog.create(dwt::ModalDialog::Seed(dwt::Point(780, 0)));
			dialog.setVisible(true);
			auto ui = controls(dialog.handle());
			auto save = button(ui, L"&Save");
			pump(app, [&] { return IsWindowEnabled(save); });
			for(int answer : { IDNO, IDYES }) {
				overwriteAnswer = answer;
				overwriteSeen = false;
				const auto timer = SetTimer(window->handle(), 9004, 50, answerOverwrite);
				check(timer != 0, "Cannot automate overwrite prompt");
				ui.edits[1]->setFocus();
				click(save);
				KillTimer(window->handle(), timer);
				check(overwriteSeen, "Existing file did not require overwrite confirmation");
				check((answer == IDNO) == dialog.getPath().empty(), "Overwrite answer was ignored");
			}
			std::ifstream file(std::filesystem::path(downloads + L"\\existing-0.txt"));
			std::string text;
			std::getline(file, text);
			check(text == "unchanged", "Picker truncated the existing file");
			DestroyWindow(dialog.handle());
		}
		for(bool close : { false, true }) {
			ThemedFileDialog dialog(pickerOptions(window, L"Cancel picker regression", downloads + L"\\cancelled.txt"));
			cancelWithClose = close;
			const auto timer = SetTimer(window->handle(), 9002, 50, cancelPicker);
			check(timer != 0, "Cannot automate Cancel");
			const auto result = dialog.run();
			KillTimer(window->handle(), timer);
			check(result == IDCANCEL && dialog.getPath().empty(), "Modal cancellation returned a destination");
			check(window->getEnabled(), "Cancelled picker left its owner disabled");
		}
		// Close while enumeration is pending: the worker must never retain widgets.
		for(unsigned i = 0; i < 12; ++i) {
			ThemedFileDialog dialog(pickerOptions(window, L"Cancel during scan", downloads + L"\\cancelled.txt"));
			dialog.create(dwt::ModalDialog::Seed(dwt::Point(780, 0)));
			click(button(controls(dialog.handle()), L"Cancel"));
			check(dialog.getPath().empty(), "Cancel supplied a destination");
			DestroyWindow(dialog.handle());
		}
		// Exercise the compact application font and larger user-selected fonts.
		for(int height : { 11, 20 }) {
			logFont.lfHeight = -height;
			dwt::FontPtr sizedFont = new dwt::Font(logFont);
			dwt::util::win32::setFileDialogDefaults({ sizedFont, {} });
			ThemedFileDialog dialog(pickerOptions(window, L"Font alignment", downloads + L"\\alignment.txt"));
			dialog.create(dwt::ModalDialog::Seed(dwt::Point(780, 0)));
			dialog.setVisible(true);
			auto ui = controls(dialog.handle());
			for(auto edit : ui.edits) checkEditAlignment(edit);
			checkButtonAlignment(button(ui, L"Start &folder"), palette.text);
			ui.edits[1]->setFocus();
			SendMessage(GetParent(ui.edits[0]->handle()), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
			check(GetFocus() == ui.edits[0]->handle(), "Clicking field padding does not focus the edit");
			PostMessage(ui.edits[0]->handle(), WM_KEYDOWN, VK_TAB, 0);
			pump(app, [&] { return GetFocus() == button(ui, L"&Go"); });
			snapshot(dialog.handle(), directory + L"font-" + std::to_wstring(height) + L".bmp");
			DestroyWindow(dialog.handle());
		}
		DestroyWindow(window->handle());
		check(translations > 0, "DWT localization callback was not used");
		dwt::util::win32::setFileDialogDefaults({});
		std::puts("PASS: automatic dark SaveDialog, ownerless dialogs, filters/extensions, overwrite policy, text alignment/font sizes, font/localization, scrolling, navigation and cancellation");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
