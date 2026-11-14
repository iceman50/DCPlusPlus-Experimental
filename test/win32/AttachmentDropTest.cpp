/* Copyright (C) 2026 iceman50. GPL-3.0-or-later. */

#include "stdafx.h"
#include <win32/util/UiServices.h>
#include <win32/util/ControlSeeds.h>

#include <win32/AspectChat.h>
#include <win32/SingleInstance.h>

#include <dcpp/hub/ClientManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/connection/http/HttpManager.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/core/localization/ResourceManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/transfer/upload/UploadManager.h>
#include <dwt/widgets/Window.h>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 179;

namespace {

void check(bool value, const char* error) {
	if(!value)
		throw std::runtime_error(error);
}

class DropData final :
	public IDataObject
{
public:
	DropData(CLIPFORMAT format, HGLOBAL memory) :
		format(format),
		memory(memory)
	{ }

	~DropData() { GlobalFree(memory); }

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
		if(!result)
			return E_POINTER;

		*result = nullptr;
		if(iid != IID_IUnknown && iid != IID_IDataObject)
			return E_NOINTERFACE;

		*result = this;
		AddRef();

		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }

	ULONG STDMETHODCALLTYPE Release() override {
		auto left = --refs;
		if(!left)
			delete this;
		return left;
	}

	HRESULT STDMETHODCALLTYPE GetData(FORMATETC* requested, STGMEDIUM* result) override {
		if(QueryGetData(requested) != S_OK)
			return DV_E_FORMATETC;

		result->tymed = TYMED_HGLOBAL;
		// Copy the exact allocation, including malformed text used by the bounds test.
		const auto bytes = GlobalSize(memory);
		result->hGlobal = GlobalAlloc(GMEM_MOVEABLE, bytes);
		if(result->hGlobal) {
			auto destination = GlobalLock(result->hGlobal);
			auto source = GlobalLock(memory);
			memcpy(destination, source, bytes);
			GlobalUnlock(memory);
			GlobalUnlock(result->hGlobal);
		}
		result->pUnkForRelease = nullptr;

		return result->hGlobal ? S_OK : E_OUTOFMEMORY;
	}

	HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* requested) override {
		return requested && requested->cfFormat == format && (requested->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_FORMATETC;
	}

	HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }

	HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }

	HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }

	HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
	ULONG refs = 1;
	CLIPFORMAT format;
	HGLOBAL memory;
};

class ChatWindow :
	public dwt::Window,
	public AspectChat<ChatWindow>
{
public:
	ChatWindow() {
		dwt::Window::Seed seed;
		seed.style &= ~WS_VISIBLE;
		create(seed);
		createChat(this);
		setChatAvailability(false, true, "adc://attachments.example.invalid");
	}

	void addStatus(const tstring& text) { status = text; }

	void layout() {}

	void enterImpl(const tstring&) {}

	void drop(const tstring& path, bool onChat, bool ole = false) {
		const auto bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
		auto memory = GlobalAlloc(GHND, bytes);
		auto files = static_cast<DROPFILES*>(GlobalLock(memory));
		files->pFiles = sizeof(DROPFILES);
		files->fWide = TRUE;
		memcpy(reinterpret_cast<char*>(files) + sizeof(DROPFILES), path.c_str(), path.size() * sizeof(wchar_t));
		GlobalUnlock(memory);
		if(ole)
			dropOle(onChat, CF_HDROP, memory);
		else
			::SendMessage(onChat ? chat->handle() : message->handle(), WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
	}

	void dropText(const tstring& text, bool onChat) {
		auto memory = GlobalAlloc(GHND, (text.size() + 1) * sizeof(wchar_t));
		auto buffer = GlobalLock(memory);
		memcpy(buffer, text.c_str(), text.size() * sizeof(wchar_t));
		GlobalUnlock(memory);
		dropOle(onChat, CF_UNICODETEXT, memory);
	}

	void dropUnterminatedText(bool onChat) {
		auto memory = GlobalAlloc(GMEM_MOVEABLE, 64);
		auto buffer = GlobalLock(memory);
		memset(buffer, 'X', GlobalSize(memory));
		GlobalUnlock(memory);
		dropOle(onChat, CF_UNICODETEXT, memory, false);
	}

	void dropOle(bool onChat, CLIPFORMAT format, HGLOBAL memory, bool accepted = true) {
		auto data = new DropData(format, memory);
		auto target = reinterpret_cast<IDropTarget*>(GetProp(onChat ? chat->handle() : message->handle(), L"OleDropTargetInterface"));
		check(target != nullptr, "Missing OLE drop target");
		DWORD effect = DROPEFFECT_COPY;
		POINTL point { 0, 0 };
		const auto entered = target->DragEnter(data, 0, point, &effect);
		check(entered == S_OK && effect == DROPEFFECT_COPY, "OLE drag was rejected");
		effect = DROPEFFECT_COPY;
		const auto dropped = target->Drop(data, 0, point, &effect);
		data->Release();
		check(accepted ? dropped == S_OK && effect == DROPEFFECT_COPY :
			dropped == DV_E_FORMATETC && effect == DROPEFFECT_NONE, "Unexpected OLE drop result");
	}

	tstring draft() const { return message->getText(); }

	void clearDraft() { message->setText(L"existing draft"); }

	tstring status;
};
}

int dwtMain(dwt::Application& app) {
	try {
		wchar_t cwd[32768];
		check(GetCurrentDirectoryW(std::size(cwd), cwd) != 0, "Cannot find workspace");
		const auto directory = Text::fromT(cwd) + "\\build\\attachment-drop-test\\";
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = directory;
		Util::initialize(paths);
		File::ensureDirectory(directory);
		ResourceManager::newInstance();
		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		HashManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		UploadManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		HashManager::getInstance()->startup([](float) {});
		UiServices::initFont();
		ControlSeeds::init();
		auto window = new ChatWindow;
		const auto path = directory + "dropped.txt";
		{
			File file(path, File::WRITE, File::CREATE | File::TRUNCATE);
			file.write("dropped file " + std::to_string(GET_TICK()));
		}

		for(bool ole : { false, true })
			for(bool onChat : { true, false }) {
				window->clearDraft();
				window->drop(Text::toT(path), onChat, ole);
				const auto deadline = GetTickCount64() + 10000;
				while(window->draft().find(L"magnet:?") == tstring::npos && GetTickCount64() < deadline) {
					app.dispatch();
					Sleep(1);
				}

				std::fwprintf(stderr, L"Draft: %ls\nStatus: %ls\n", window->draft().c_str(), window->status.c_str());
				check(window->draft().find(L"magnet:?") != tstring::npos, "Dropped attachment did not reach the composer");
				check(window->draft().find(L"existing draft") == 0, "Drop replaced the existing draft");
				check(window->status.find(L"Preparing") == tstring::npos, "Drop status remained stuck preparing");
			}

		const tstring magnet = L"magnet:?xt=urn:tree:tiger:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA&xl=12&dn=test.txt";
		for(bool onChat : { true, false }) {
			window->clearDraft();
			window->dropUnterminatedText(onChat);
			check(window->draft() == L"existing draft", "Malformed text changed the draft");
			window->clearDraft();
			window->dropText(magnet, onChat);
			check(window->draft().find(magnet) != tstring::npos, "Dropped magnet did not reach the composer");
		}

		DestroyWindow(window->handle());
		HashManager::getInstance()->shutdown();
		ShareManager::deleteInstance();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		QueueManager::deleteInstance();
		UploadManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		HashManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("Attachment drops into chat and composer passed.");

		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());

		return 1;
	}
}
