// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"
#include <win32/app/SingleInstance.h>
#include <win32/app/UiServices.h>
#include <win32/appearance/AppAppearance.h>
#include <win32/appearance/UiFonts.h>
#include <win32/controls/RichTextBox.h>
#include <dcpp/app/ResourceManager.h>
#include <dcpp/user/UserMatchManager.h>
#include <dwt/Application.h>
#include <dwt/widgets/Window.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 182;

namespace {
void check(bool value, const char* message) {
	if(!value) throw std::runtime_error(message);
}
struct Probe {
	unsigned formats = 0, replacements = 0, paints = 0, incompletePaints = 0;
	bool active = false, colorsReady = false;
};
LRESULT CALLBACK observe(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	auto& probe = *reinterpret_cast<Probe*>(data);
	if(probe.active) {
		if(message == EM_GETCHARFORMAT || message == EM_SETCHARFORMAT) ++probe.formats;
		if(message == EM_SETTEXTEX) ++probe.replacements;
		if(message == WM_PAINT) {
			++probe.paints;
			if(!probe.colorsReady) ++probe.incompletePaints;
		}
	}
	return DefSubclassProc(window, message, wParam, lParam);
}
}

int dwtMain(dwt::Application& app) {
	try {
		Util::PathsMap paths;
		for(int i = 0; i < Util::PATH_LAST; ++i) paths[static_cast<Util::Paths>(i)] = "build/theme-performance/";
		Util::initialize(paths);
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		UserMatchManager::newInstance();
		AppAppearance::init();
		UiFonts::initDefault();
		UiServices::initFont();
		AppAppearance::initStyleDefaults();
		const bool profile = std::getenv("DCPP_PROFILE_THEME") != nullptr;
		auto window = new dwt::Window;
		dwt::Window::Seed seed;
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		window->setText(L"DC++ theme profile (synthetic chat history)");
		window->resize(dwt::Rectangle(80, 80, 1000, 700));
		if(profile) ShowWindow(window->handle(), SW_SHOWNOACTIVATE);
		std::puts("messages_per_chat,round,refresh_ms,format_calls,replacements,paints_during_refresh");
		for(int count: profile ? std::vector<int>{100, 1000} : std::vector<int>{20}) {
			std::vector<RichTextBox*> chats;
			std::vector<tstring> original;
			Probe probe;
			auto& appearance = app.getAppearance();
			const auto callback = appearance.onChanged([&probe] { probe.colorsReady = true; });
			for(int i = 0; i < 3; ++i) {
				auto chat = new RichTextBox(window);
				chat->create(RichTextBox::Seed());
				chat->setFont(UiFonts::font);
				chat->resize(dwt::Rectangle(0, i * 210, 960, 200));
				chat->setTextLimit(1024 * 1024);
				chat->setReadOnly();
				AppAppearance::setColor(chat);
				std::vector<RichTextBox::RenderedMessage> messages;
				for(int n = 0; n < count; ++n) {
					messages.push_back({L"", L"", L"", "", 0,
						"<span id=\"text\">Message " + std::to_string(n) +
						" with ordinary text, <b>bold text</b> and <a href=\"https://example.invalid/\">a link</a>.</span>", ""});
				}
				chat->appendMessages(std::move(messages));
				chat->setSelection(2, 7);
				original.push_back(chat->getText());
				chats.push_back(chat);
				check(SetWindowSubclass(chat->handle(), observe, 1, reinterpret_cast<DWORD_PTR>(&probe)) != FALSE, "Cannot observe chat updates");
			}
			for(int round = 0; round < 3; ++round) {
				while(app.dispatch()) { }
				auto settings = SettingsManager::getInstance();
				const auto text = round % 2 ? RGB(20, 30, 40) : RGB(230, 235, 240);
				const auto background = round % 2 ? RGB(245, 245, 245) : RGB(25, 30, 35);
				settings->set(SettingsManager::TEXT_COLOR, text);
				settings->set(SettingsManager::BACKGROUND_COLOR, background);
				settings->set(SettingsManager::CHAT_TEXT_COLOR, text);
				settings->set(SettingsManager::CHAT_TEXT_BG_COLOR, background);
				settings->set(SettingsManager::THEME_MODE, round % 2 ? SettingsManager::THEME_LIGHT : SettingsManager::THEME_DARK);
				probe = {};
				probe.active = true;
				const auto start = std::chrono::steady_clock::now();
				AppAppearance::refreshTheme();
				const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				probe.active = false;
				check(probe.incompletePaints == 0, "Chat painted before all theme colors were updated");
				std::printf("%d,%d,%.3f,%u,%u,%u\n", count, round, milliseconds, probe.formats, probe.replacements, probe.paints);
				std::fflush(stdout);
				for(size_t i = 0; i < chats.size(); ++i) {
					check(chats[i]->getText() == original[i], "Theme change altered chat text");
					check(chats[i]->getCaretPosRange() == std::make_pair(2, 7), "Theme change altered selection");
					check(chats[i]->isReadOnly(), "Theme change left chat editable");
				}
			}
			for(auto chat: chats) {
				RemoveWindowSubclass(chat->handle(), observe, 1);
				DestroyWindow(chat->handle());
			}
			appearance.removeChanged(callback);
		}
		DestroyWindow(window->handle());
		UserMatchManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
