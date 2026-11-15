// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"
#include <win32/SingleInstance.h>
#include <win32/frames/search/MagnetDlg.h>
#include <win32/settings/DownloadPage.h>
#include <win32/util/LinkActions.h>
#include <win32/util/UiServices.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/Label.h>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 181;
namespace {
void check(bool condition, const char* message) {
    if(!condition) throw std::runtime_error(message);
}
BOOL CALLBACK collectLimitControls(HWND window, LPARAM data) {
    reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
    return TRUE;
}
void magnetControls() {
	auto window = new dwt::Window();
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	const string link = "magnet:?xt=urn:tree:tiger:LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ";
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::MAGNET_ASK, true);
	for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
		dwt::Application::instance().getAppearance().configure(mode, dwt::Appearance::defaultPalette());
		for(int kind = 0; kind < 3; ++kind) {
			Magnet::Metadata metadata;
			check(Magnet::parseUri(kind == 2 ? "magnet:?kt=search+words" : link +
				(kind == 0 ? "&xl=123&dn=download.txt" : ""), metadata), "Cannot parse magnet UI fixture");
			for(bool cancel : { false, true }) {
				MagnetDlg dialog(window, metadata, "C:\\Downloads\\download.txt");
				dialog.create(dwt::ModalDialog::Seed(dwt::Point(560, 0)));
				std::vector<HWND> children;
				EnumChildWindows(dialog.handle(), collectLimitControls, reinterpret_cast<LPARAM>(&children));
				HWND download = nullptr, search = nullptr, cancelButton = nullptr;
				for(auto child : children) {
					if(auto label = dwt::hwnd_cast<dwt::Label*>(child)) {
						const auto text = label->getText();
						RECT available {};
						GetClientRect(child, &available);
						auto measured = available;
						auto dc = GetDC(child);
						auto previous = SelectObject(dc, label->getFont()->handle());
						DrawText(dc, text.c_str(), static_cast<int>(text.size()), &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
						SelectObject(dc, previous);
						ReleaseDC(child, dc);
						if(measured.bottom > available.bottom)
							std::fwprintf(stderr, L"Clipped label '%ls': width=%ld height=%ld needed=%ld\n", text.c_str(), available.right, available.bottom, measured.bottom);
						check(measured.bottom <= available.bottom, "Magnet explanatory text is clipped");
					}
					if(auto button = dwt::hwnd_cast<dwt::Button*>(child)) {
						if(button->getText() == T_("Download")) download = child;
						if(button->getText() == T_("Search")) search = child;
						if(button->getText() == T_("Cancel")) cancelButton = child;
					}
					if(auto box = dwt::hwnd_cast<dwt::CheckBox*>(child)) box->setChecked(true);
					if(auto edit = dwt::hwnd_cast<dwt::TextBox*>(child)) {
						if(!(GetWindowLongPtr(child, GWL_STYLE) & ES_READONLY) && IsWindowEnabled(child))
							edit->setText(L"C:\\Downloads\\chosen.txt");
					}
				}
				check(download && search && cancelButton, "Magnet actions are missing");
				check((IsWindowEnabled(download) != FALSE) == (kind == 0), "Incomplete magnet allowed downloading");
				RECT dialogBounds {};
				GetClientRect(dialog.handle(), &dialogBounds);
				for(auto button : { search, download, cancelButton }) {
					RECT bounds {};
					GetWindowRect(button, &bounds);
					MapWindowPoints(HWND_DESKTOP, dialog.handle(), reinterpret_cast<POINT*>(&bounds), 2);
					check(bounds.right <= dialogBounds.right && bounds.bottom <= dialogBounds.bottom &&
						bounds.left >= 0 && bounds.top >= 0 && bounds.right > bounds.left, "Magnet action is clipped");
				}
				SendMessage(cancel ? cancelButton : kind == 0 ? download : search, BM_CLICK, 0, 0);
				check(dialog.getAction() == (cancel ? MagnetDlg::Action::Cancel : kind == 0 ? MagnetDlg::Action::Download : MagnetDlg::Action::Search),
					"Magnet dialog returned the wrong action");
				check(dialog.rememberChoice() == !cancel, "Cancel retained a pending preference");
				check(settings->get(SettingsManager::MAGNET_ASK), "Dialog changed preferences before an action succeeded");
				if(!cancel && kind == 0) check(dialog.getTarget() == "C:\\Downloads\\chosen.txt", "Magnet destination was lost");
				DestroyWindow(dialog.handle());
			}
		}
	}
	for(int action : { 0, 1, 2 }) {
		auto page = new DownloadPage(window);
		std::vector<HWND> children;
		EnumChildWindows(page->handle(), collectLimitControls, reinterpret_cast<LPARAM>(&children));
		dwt::ComboBox* option = nullptr;
		for(auto child : children) if(auto combo = dwt::hwnd_cast<dwt::ComboBox*>(child)) option = combo;
		check(option != nullptr, "Downloads settings cannot reset the magnet action");
		option->setSelected(action);
		page->write();
		check(settings->get(SettingsManager::MAGNET_ASK) == (action == 0), "Magnet ask preference was not saved");
		if(action > 0) check(settings->get(SettingsManager::MAGNET_ACTION) ==
			(action == 2 ? SettingsManager::MAGNET_AUTO_DOWNLOAD : SettingsManager::MAGNET_AUTO_SEARCH), "Magnet action was not saved");
		DestroyWindow(page->handle());
	}
	settings->set(SettingsManager::MAGNET_ASK, true);
	DestroyWindow(window->handle());
}

}
int dwtMain(dwt::Application&) {
    try {
        ResourceManager::newInstance();
        SettingsManager::newInstance();
        check(!LinkActions::parseLink(L"https://magnet/", false), "A web hostname was mistaken for a magnet URI");
        for(const auto font : { "Segoe UI,-12,400,0,1", "Segoe UI,-24,400,0,1" }) {
            SettingsManager::getInstance()->set(SettingsManager::MAIN_FONT, font);
            UiServices::initFont();
            magnetControls();
        }
        SettingsManager::deleteInstance();
        ResourceManager::deleteInstance();
        std::puts("PASS: magnet dialog actions, destinations, cancellation, palettes and preference settings");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
