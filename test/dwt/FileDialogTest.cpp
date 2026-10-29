// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// Exercise the real Shell picker without writing the selected files.
#include <dwt/Application.h>
#include <dwt/widgets/SaveDialog.h>
#include <dwt/util/win32/ComPtr.h>
#include <dwt/util/win32/FileDialog.h>
#include <cstdio>
#include <stdexcept>

namespace {
using namespace dwt;
using namespace dwt::util::win32;

void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

struct Observation {
	ComPtr<IFileDialog> dialog;
	HWND window = nullptr;
	tstring folder, name;
	bool inspected = false, accept = false;
	bool rejectExisting = false, rejected = false;
	ULONGLONG started = GetTickCount64();
};
Observation* observation = nullptr;

void CALLBACK inspectDialog(HWND, UINT, UINT_PTR, DWORD) {
	auto& state = *observation;
	if(!state.dialog) return;
	if(!state.window) {
		ComPtr<IOleWindow> window;
		if(FAILED(state.dialog->QueryInterface(IID_PPV_ARGS(window.put()))) ||
			FAILED(window->GetWindow(&state.window))) return;
	}
	const auto handle = state.window;
	if(state.rejectExisting && state.inspected) {
		auto notice = FindWindow(WC_DIALOG, L"Save As");
		DWORD process = 0;
		GetWindowThreadProcessId(notice, &process);
		if(notice && process == GetCurrentProcessId() && GetWindow(notice, GW_OWNER) == handle) {
			state.rejected = true;
			auto dismiss = GetDlgItem(notice, IDOK);
			if(!dismiss) dismiss = GetDlgItem(notice, IDCANCEL);
			if(dismiss) SendMessage(dismiss, BM_CLICK, 0, 0);
		}
	}
	if(!IsWindowVisible(handle)) return;
	// Allow the Shell to create and theme the navigation pane before inspecting.
	if(GetTickCount64() - state.started < 1000) return;
	if(!state.inspected) {
		state.inspected = true;
		ComPtr<IShellItem> folder;
		PWSTR path = nullptr, name = nullptr;
		if(SUCCEEDED(state.dialog->GetFolder(folder.put())) &&
			SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH, &path)))
		{
			state.folder = path;
			CoTaskMemFree(path);
		}
		if(SUCCEEDED(state.dialog->GetFileName(&name))) {
			state.name = name;
			CoTaskMemFree(name);
		}
		if(state.accept) {
			PostMessage(handle, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
				reinterpret_cast<LPARAM>(GetDlgItem(handle, IDOK)));
			return;
		}
	}
	if(!state.accept || state.rejected || GetTickCount64() - state.started > 10000)
		PostMessage(handle, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED),
			reinterpret_cast<LPARAM>(GetDlgItem(handle, IDCANCEL)));
}

void runCase(const tstring& folder, const tstring& input, const tstring& expectedName,
	bool explicitFolder, bool accept, bool rejectExisting = false)
{
	Observation state;
	state.accept = accept;
	state.rejectExisting = rejectExisting;
	observation = &state;
	// Isolate remembered Shell state from the application's file pickers.
	const GUID clientGuid = { 0xd0ebbf34, 0x932b, 0x4c8e,
		{ 0x9e, 0x67, 0x0b, 0xae, 0xca, 0x13, 0x38, 0x91 } };
	auto picker = SaveDialog().setTitle(L"DWT file picker regression")
		.addFilter(L"All files", L"*.*").setClientGuid(clientGuid).setAllowOverwrite(!rejectExisting)
		.onFileDialogCustomize([&](IFileDialogCustomize& customize) {
			check(SUCCEEDED(customize.QueryInterface(IID_PPV_ARGS(state.dialog.put()))),
				"Cannot inspect the configured Shell picker");
		});
	if(explicitFolder) picker.setInitialDirectory(folder);
	const auto timerWindow = CreateWindowEx(0, WC_STATIC, L"", WS_POPUP,
		0, 0, 0, 0, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
	check(timerWindow != nullptr, "Cannot create dialog inspection window");
	auto timer = SetTimer(timerWindow, 1, 100, inspectDialog);
	if(!timer) {
		DestroyWindow(timerWindow);
		observation = nullptr;
		throw std::runtime_error("Cannot start dialog inspection timer");
	}
	auto result = input;
	bool selected;
	try {
		selected = picker.open(result, OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT);
	} catch(...) {
		DestroyWindow(timerWindow);
		if(state.dialog) state.dialog->ClearClientData();
		observation = nullptr;
		throw;
	}
	DestroyWindow(timerWindow);
	if(state.dialog) state.dialog->ClearClientData();
	observation = nullptr;
	check(state.inspected, "Shell picker was not inspected");
	check(lstrcmpi(state.folder.c_str(), folder.c_str()) == 0,
		"Picker did not start in the requested destination folder");
	check(state.name == expectedName, "Filename edit does not contain just the requested name");
	check(selected == (accept && !rejectExisting), "Unexpected Save/Cancel result");
	if(rejectExisting) check(state.rejected, "Native picker did not explain the no-overwrite rejection");
	check(result == (selected ? folder + L"\\" + expectedName : input),
		"Save returned the wrong path or cancellation changed the caller's path");
}
}

int dwtMain(dwt::Application& app) {
	TCHAR temp[MAX_PATH] {}, folder[MAX_PATH] {};
	if(!GetTempPath(MAX_PATH, temp) || !GetTempFileName(temp, L"dwt", 0, folder)) return 1;
	DeleteFile(folder);
	if(!CreateDirectory(folder, nullptr)) return 1;
	int result = 0;
	const tstring existing = tstring(folder) + L"\\existing.txt";
	try {
		auto file = CreateFile(existing.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		check(file != INVALID_HANDLE_VALUE, "Cannot create overwrite fixture");
		CloseHandle(file);
		for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			const dwt::tstring path = dwt::tstring(folder) + L"\\private example.txt";
			runCase(folder, path, L"private example.txt", false, false);
			runCase(folder, L"chosen name.txt", L"chosen name.txt", true, true);
			runCase(folder, L"existing.txt", L"existing.txt", true, true, true);
		}
		std::puts("PASS: initial save folder/name, Save and Cancel in light/dark modes");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	DeleteFile(existing.c_str());
	RemoveDirectory(folder);
	return result;
}
