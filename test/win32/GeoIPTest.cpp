/* Copyright (C) 2026 iceman50. GPL-3.0-or-later. */

#include "stdafx.h"

#include <win32/appearance/CountryFlags.h>
#include <win32/resource.h>
#include <win32/services/GeoIPUpdate.h>
#include <win32/app/SingleInstance.h>
#include <win32/controls/TypedTable.h>
#include <win32/settings/appearance/AppearancePage.h>
#include <win32/settings/advanced/ExperimentalPage.h>
#include <win32/frames/transfers/private/PrivateSendDlg.h>
#include <win32/frames/chat/PrivateFrame.h>
#include <dcpp/chat/PrivateChatManager.h>
#include <dcpp/app/WindowManager.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Button.h>
#include <win32/frames/transfers/TransferView.h>

#include <dcpp/transfer/private/DirectTransferManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/net/CryptoManager.h>

#include <win32/appearance/IconManager.h>

#include <dwt/widgets/TabView.h>
#include <dcpp/net/HttpManager.h>
#include <dcpp/net/HttpConnection.h>
#include <dcpp/geo/GeoManager.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/search/SearchManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/transfer/UploadManager.h>
#include <dcpp/transfer/DownloadManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <atomic>
#include <dcpp/system/File.h>
#include <dcpp/app/ResourceManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dwt/Appearance.h>
#include <dwt/Application.h>
#include <dwt/widgets/Window.h>
#include <dwt/widgets/TableTree.h>
#include <cstdio>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 178;

namespace {

void check(bool value, const char* message) {
	if(!value)
		throw std::runtime_error(message);
}

struct TransferEvents :
	HttpManagerListener
{
	std::atomic<unsigned> added { 0 }, bytes { 0 }, completed { 0 }, failed { 0 };

	void on(Added, HttpConnection*) noexcept override { ++added; }

	void on(Updated, HttpConnection*, const uint8_t*, size_t n) noexcept override { bytes += static_cast<unsigned>(n); }

	void on(Complete, HttpConnection*, OutputStream*) noexcept override { ++completed; }

	void on(Failed, HttpConnection*, const std::string&) noexcept override { ++failed; }
};

struct Row {
	tstring text[2];
	int flag;

	const tstring& getText(int col) const { return text[col]; }

	int getImage(int col) const { return col == 1 ? flag : -1; }
};

template <class Table> void verify(Table* table, int row, int expected) {
	LVITEM item { LVIF_IMAGE, row, 1 };
	check(ListView_GetItem(table->handle(), &item) != FALSE, "Could not retrieve country subitem");
	check(item.iImage == expected, "Country subitem lost its callback image");
}

void snapshot(HWND window, const std::string& path, bool includeFrame = false) {
	RECT rect;
	GetClientRect(window, &rect);
	if(includeFrame) {
		GetWindowRect(window, &rect);
		rect.right -= rect.left;
		rect.bottom -= rect.top;
		rect.left = rect.top = 0;
	}

	auto dc = GetDC(window), memory = CreateCompatibleDC(dc);
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = rect.right;
	info.bmiHeader.biHeight = -rect.bottom;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	void* pixels = nullptr;
	auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
	check(bitmap != nullptr, "Cannot capture GeoIP settings layout");
	auto old = SelectObject(memory, bitmap);
	SendMessage(window, WM_PRINT, reinterpret_cast<WPARAM>(memory),
		PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND | (includeFrame ? PRF_NONCLIENT : 0));
	BITMAPFILEHEADER header {};
	header.bfType = 0x4D42;
	header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
	header.bfSize = header.bfOffBits + rect.right * rect.bottom * 4;
	dcpp::File output(path, dcpp::File::WRITE, dcpp::File::CREATE | dcpp::File::TRUNCATE);
	output.write(&header, sizeof(header));
	output.write(&info.bmiHeader, sizeof(info.bmiHeader));
	output.write(pixels, rect.right * rect.bottom * 4);
	SelectObject(memory, old);
	DeleteObject(bitmap);
	DeleteDC(memory);
	ReleaseDC(window, dc);
}

void verifyPrivateSendIndicator(dwt::Window* window, dwt::Application& app) {
	for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
		app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
		auto status = window->addChild(dwt::StatusBar::Seed(3, 0, false));
		status->setText(0, L"Ready");
		status->setIcon(1, WinUtil::statusIcon(IDI_FILE));
		status->setText(1, L"2");
		status->setText(2, L"Other status");
		status->refresh();
		RECT original {};
		SendMessage(status->handle(), SB_GETRECT, 1, reinterpret_cast<LPARAM>(&original));
		for(unsigned i = 0; i < 6; ++i) {
			status->setIcon(1, i % 2 ? WinUtil::statusIcon(IDI_FILE) : nullptr);
			RECT current {};
			SendMessage(status->handle(), SB_GETRECT, 1, reinterpret_cast<LPARAM>(&current));
			check(EqualRect(&original, &current), "Flashing the private offer icon moved status fields");
		}
		bool clicked = false;
		status->onClicked(1, [&clicked] { clicked = true; });
		// StatusBar hit testing uses GetMessagePos. Align the off-screen fixture
		// with that message position without moving the user's cursor.
		const auto bounds = window->getWindowRect();
		const auto messagePoint = dwt::Point::fromLParam(GetMessagePos());
		POINT target { (original.left + original.right) / 2, (original.top + original.bottom) / 2 };
		ClientToScreen(status->handle(), &target);
		window->resize(dwt::Rectangle(bounds.pos.x + messagePoint.x - target.x, bounds.pos.y, bounds.width(), bounds.height()));
		const auto point = MAKELPARAM((original.left + original.right) / 2, (original.top + original.bottom) / 2);
		SendMessage(status->handle(), WM_LBUTTONDOWN, MK_LBUTTON, point);
		SendMessage(status->handle(), WM_LBUTTONUP, 0, point);
		window->resize(bounds);
		check(clicked, "Private offer status icon did not dispatch its click");
		DestroyWindow(status->handle());
	}
}

void verifyPrivateSendPM(dwt::Window* window, dwt::Application& app) {
	SettingsManager::getInstance()->set(SettingsManager::STATUS_IN_CHAT, false);
	SettingsManager::getInstance()->set(SettingsManager::MAX_PM_WINDOWS, 1);
	SettingsManager::getInstance()->set(SettingsManager::PM_LAST_LOG_LINES, 0);
	SettingsManager::getInstance()->set(SettingsManager::LOG_PRIVATE_CHAT, true);
	auto tabs = window->addChild(dwt::TabView::Seed());
	tabs->resize(dwt::Rectangle(0, 0, 620, 240));
	const HintedUser first(UserPtr(new User(CID::generate())), "adcs://private-test.invalid");
	const HintedUser second(UserPtr(new User(CID::generate())), first.hint);
	const tstring notice = L"Private Send: <literal & file.txt> (2 KB)";
	check(PrivateFrame::privateSendNotice(tabs, first, notice), "Private offer did not create a PM");
	check(PrivateFrame::isOpen(first.user), "Private offer PM was not registered");
	check(!PrivateFrame::privateSendNotice(tabs, second, notice), "Private offer bypassed the PM window limit");
	for(unsigned i = 0; i < 200; ++i)
		app.dispatch();

	bool found = false;
	EnumChildWindows(tabs->handle(), [](HWND child, LPARAM value) -> BOOL {
		wchar_t text[4096] {};
		GetWindowTextW(child, text, std::size(text));
		if(std::wstring(text).find(L"Private Send: <literal & file.txt> (2 KB)") != std::wstring::npos)
			*reinterpret_cast<bool*>(value) = true;
		return TRUE;
	}, reinterpret_cast<LPARAM>(&found));
	check(found, "Private offer text was lost, interpreted as markup, or hidden by STATUS_IN_CHAT");
	const auto frame = static_cast<PrivateFrame*>(tabs->getChildren().front());
	const auto params = frame->getWindowParams();
	check(File::getSize(params.at("LogPath").content) == -1, "Private offer leaked into the ordinary PM log");
	check(PrivateFrame::privateSendNotice(tabs, first, L"Second private file"), "Existing PM rejected another offer at the window limit");
	PrivateFrame::closeAll(false);
	for(unsigned i = 0; i < 200; ++i)
		app.dispatch();
	check(!PrivateFrame::isOpen(first.user), "Private offer PM did not close cleanly");
	check(PrivateFrame::privateSendNotice(tabs, second, notice), "Deferred private offer could not open after a PM closed");
	PrivateFrame::closeAll(false);
	for(unsigned i = 0; i < 200; ++i)
		app.dispatch();
	DestroyWindow(tabs->handle());
}

void verifyTransferIcons(dwt::Window* window, dwt::Application& app, const std::string& directory) {
	WinUtil::fileImages = dwt::ImageListPtr(new dwt::ImageList(dwt::Point(16, 16)));
	for(auto resource : { IDI_DIRECTORY, IDI_DIRECTORY, IDI_FILE, IDI_USER, IDI_PRIVATE })
		WinUtil::fileImages->add(*WinUtil::createIcon(resource, 16));
	WinUtil::fileFlagOffset = CountryFlags::append(*WinUtil::fileImages);

	for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
		app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
		auto view = new TransferView(window, nullptr);
		view->resize(dwt::Rectangle(0, 0, 1200, 200));
		const auto tables = view->getChildren<dwt::TableTree>();
		check(tables.first != tables.second, "TransferView table is missing");
		auto table = *tables.first;
		const auto& palette = app.getAppearance().getPalette();
		table->setColor(palette.text, palette.background);
		for(int column = 0; column < Header_GetItemCount(ListView_GetHeader(table->handle())); ++column)
			table->setColumnWidth(column, 60);
		table->setColumnWidth(2, 310);
		table->setColumnWidth(3, 160);
		table->setColumnWidth(4, 150);

		// Feed the real connection listener and timer, then inspect every native
		// subitem. A synthetic Row callback would miss TransferInfo's PM branch.
		auto waitRows = [&](int expected) {
			const auto deadline = GetTickCount64() + 5000;
			while(GetTickCount64() < deadline) {
				app.dispatch();
				if(expected > 1 && table->size() && table->getText(0, 3) == str(TF_("%1% users") % 2)) {
					LVITEM parent { LVIF_PARAM, 0 };
					check(ListView_GetItem(table->handle(), &parent) != FALSE, "Cannot retrieve transfer group");
					table->expand(parent.lParam);
				}
				if(static_cast<int>(table->size()) == expected)
					return;
				Sleep(1);
			}
			check(false, "TransferView did not process the connection event");
		};

		auto verifyRow = [&](int row, int expectedIcon) {
			const auto columns = Header_GetItemCount(ListView_GetHeader(table->handle()));
			for(int column = 0; column < columns; ++column) {
				LVITEM item { LVIF_IMAGE, row, column };
				check(ListView_GetItem(table->handle(), &item) != FALSE, "Cannot retrieve transfer subitem image");
				check(column == 0 ? item.iImage == expectedIcon : item.iImage < 0,
					"TransferView repeated a user icon outside the File column");
			}
		};

		HintedUser peer(UserPtr(new User(CID::generate())), "adcs://transfer-icons.invalid:411");
		ConnectionQueueItem first(peer, CONNECTION_TYPE_PM, "icon-test-first");
		ConnectionQueueItem second(peer, CONNECTION_TYPE_PM, "icon-test-second");
		ConnectionManager::getInstance()->fire(ConnectionManagerListener::Added(), &first);
		waitRows(1);
		verifyRow(0, WinUtil::TRANSFER_ICON_PM);

		ConnectionManager::getInstance()->fire(ConnectionManagerListener::Added(), &second);
		waitRows(3);
		LVITEM parent { LVIF_PARAM, 0 };
		check(ListView_GetItem(table->handle(), &parent) != FALSE, "Cannot retrieve expanded transfer group");
		for(int iteration = 0; iteration < 3; ++iteration) {
			verifyRow(0, WinUtil::TRANSFER_ICON_PM);
			verifyRow(1, WinUtil::TRANSFER_ICON_USER);
			verifyRow(2, WinUtil::TRANSFER_ICON_USER);
			table->collapse(parent.lParam);
			verifyRow(0, WinUtil::TRANSFER_ICON_PM);
			table->expand(parent.lParam);
		}

		snapshot(table->handle(), directory + (mode == dwt::Appearance::Mode::Dark ? "transfer-icons-dark.bmp" : "transfer-icons-light.bmp"));
		view->prepareClose();
		DestroyWindow(view->handle());
	}
}
}

int dwtMain(dwt::Application& app) {
	try {
		wchar_t cwd[32768];
		check(GetCurrentDirectoryW(std::size(cwd), cwd) != 0, "Cannot find test workspace");
		const auto directory = Text::fromT(cwd) + "\\build\\geolite2\\ui\\";
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
		CryptoManager::newInstance();
		ConnectionManager::newInstance();
		DirectTransferManager::newInstance();
		PrivateChatManager::newInstance();
		WindowManager::newInstance();
		UploadManager::newInstance();
		DownloadManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		GeoManager::newInstance();
		IconManager::initialize();
		SettingsManager::getInstance()->set(SettingsManager::ICON_PACK, IconManager::EMBEDDED_PACK);
		check(GeoIPUpdate::validHostedUrl("https://github.com/example/data/releases/latest/download/GeoLite2-Country.mmdb"),
			"GitHub release URL was rejected");
		check(!GeoIPUpdate::validHostedUrl("http://example.test/data.mmdb") &&
				!GeoIPUpdate::validHostedUrl("https://user:secret@example.test/data.mmdb") &&
				!GeoIPUpdate::validHostedUrl("https://example.test/\r\nAuthorization: secret"),
			"Unsafe hosted URL was accepted");
		WinUtil::initFont();
		WinUtil::initSeeds();
		const std::string key = "synthetic-test-key-not-a-MaxMind-credential";
		const auto encrypted = GeoIPUpdate::protectKey(key);
		check(encrypted != key && encrypted.find(key) == std::string::npos, "License key was not protected");
		check(GeoIPUpdate::unprotectKey(encrypted) == key, "License key did not round-trip through Windows protection");
		check(GeoIPUpdate::unprotectKey("invalid").empty(), "Invalid protected key was accepted");
		check(GeoIPUpdate::protectKey("").empty(), "Clearing a key did not clear storage");
		SettingsManager::getInstance()->set(SettingsManager::ICON_PACK, Text::fromT(cwd) + "\\test\\dcpp\\data\\geoip\\flag-override.dcico");
		IconManager::reload();
		check(IconManager::load(IDI_FLAG_GB, 16) != nullptr, "Flag package override failed");
		check(IconManager::load(IDI_FLAG_JP, 16) != nullptr, "Broken flag override did not fall back");
		const auto packState = IconManager::getRuntimeInfo();
		check(packState.cachedPackageIcons == 1 && packState.cachedEmbeddedIcons == 1,
			"Flag package selection/fallback did not use the expected sources");
		SettingsManager::getInstance()->set(SettingsManager::ICON_PACK, IconManager::EMBEDDED_PACK);
		IconManager::reload();
		auto window = new dwt::Window();
		dwt::Window::Seed seed;
		seed.caption = L"GeoLite2 flag test";
		seed.location = dwt::Rectangle(-32000, -32000, 620, 240);
		window->create(seed);
		verifyPrivateSendIndicator(window, app);
		verifyPrivateSendPM(window, app);
		verifyTransferIcons(window, app, directory);
		for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			for(int size : { 16, 24, 32, 48 }) {
				auto images = dwt::ImageListPtr(new dwt::ImageList(dwt::Point(size, size)));
				const auto first = CountryFlags::append(*images);
				const auto gb = CountryFlags::index("GB", first), jp = CountryFlags::index("JP", first);
				check(gb >= 0 && jp >= 0 && gb != jp, "Country resource mapping failed");
				check(CountryFlags::index("", first) == -1 && CountryFlags::index("ZZ", first) == -1, "Unknown countries received a flag");
				Row parent { { L"Peer", L"81.2.69.160" }, gb }, child { { L"Peer IPv6", L"2001:218::" }, jp };
				using Tree = TypedTable<Row, false, dwt::TableTree>;
				auto table = window->addChild(Tree::Seed(dwt::Table::Seed()));
				table->resize(dwt::Rectangle(0, 0, 600, 200));
				table->addColumn(L"Peer", 180);
				table->addColumn(L"IP", 380);
				table->setSmallImageList(images);
				table->setImageColumns({ 1 });
				table->insert(&parent);
				verify(table, 0, gb);
				table->insertChild(reinterpret_cast<LPARAM>(&parent), reinterpret_cast<LPARAM>(&child));
				for(int iteration = 0; iteration < 3; ++iteration) {
					table->expand(reinterpret_cast<LPARAM>(&parent));
					check(table->size() == 2, "Tree child did not expand");
					verify(table, 1, jp);
					check(table->getText(1, 0) == child.text[0], "Flag callbacks erased the grouped peer name");
					table->collapse(reinterpret_cast<LPARAM>(&parent));
					verify(table, 0, gb);
				}

				if(size == 16 || size == 32) {
					table->expand(reinterpret_cast<LPARAM>(&parent));
					snapshot(table->handle(),
						directory + (mode == dwt::Appearance::Mode::Dark ? "flags-dark-" : "flags-light-") + std::to_string(size) + ".bmp");
				}

				DestroyWindow(table->handle());
			}
		}

		auto appearance = new AppearancePage(window);
		appearance->resize(dwt::Rectangle(0, 0, 620, 620));
		appearance->layout();
		snapshot(appearance->handle(), directory + "appearance-dark.bmp");
		DestroyWindow(appearance->handle());
		for(auto mode : { dwt::Appearance::Mode::Light, dwt::Appearance::Mode::Dark }) {
			app.getAppearance().configure(mode, dwt::Appearance::defaultPalette());
			uint8_t id[CID::SIZE] {};
			id[0] = 123;
			PrivateSendDlg privateDialog(window, HintedUser(UserPtr(new User(CID(id))), "adcs://private-test.invalid:411"));
			privateDialog.create(dwt::ModalDialog::Seed(dwt::Point(740, 0)));
			privateDialog.resize(dwt::Rectangle(dwt::Point(-32000, -32000), privateDialog.getWindowRect().size));
			privateDialog.setVisible(true);
			for(unsigned i = 0; i < 200; ++i)
				app.dispatch();
			bool visibleReadiness = false;
			EnumChildWindows(privateDialog.handle(), [](HWND child, LPARAM value) -> BOOL {
				wchar_t text[512] {};
				GetWindowTextW(child, text, std::size(text));
				if(std::wstring(text).find(L"Private Send is disabled.") != std::wstring::npos)
					*reinterpret_cast<bool*>(value) = IsWindowVisible(child) != FALSE;
				return TRUE;
			}, reinterpret_cast<LPARAM>(&visibleReadiness));
			check(visibleReadiness, "Empty Private Send queue does not explain why sending is unavailable");
			bool testingNotice = false;
			EnumChildWindows(privateDialog.handle(), [](HWND child, LPARAM value) -> BOOL {
				wchar_t text[512] {};
				GetWindowTextW(child, text, std::size(text));
				if(std::wstring(text).find(L"Testing mode: contact verification is disabled.") == 0)
					*reinterpret_cast<bool*>(value) = IsWindowVisible(child) != FALSE;
				return TRUE;
			}, reinterpret_cast<LPARAM>(&testingNotice));
			check(testingNotice, "Private Send does not identify the unverified testing mode");
			bool statsColumns = false;
			EnumChildWindows(privateDialog.handle(), [](HWND child, LPARAM value) -> BOOL {
				if(dynamic_cast<dwt::Table*>(dwt::hwnd_cast<dwt::Widget*>(child))) {
					const auto header = ListView_GetHeader(child);
					check(Header_GetItemCount(header) == 3, "Private Send should have three compact columns");
					const wchar_t* expected[] = { L"File", L"Status", L"Progress" };
					for(int i = 0; i < 3; ++i) {
						wchar_t text[128] {};
						HDITEM item {};
						item.mask = HDI_TEXT;
						item.pszText = text;
						item.cchTextMax = std::size(text);
						check(Header_GetItem(header, i, &item) && std::wstring(text) == expected[i],
							"Private Send statistics column has the wrong heading");
					}
					RECT last {}, client {};
					Header_GetItemRect(header, 2, &last);
					GetClientRect(child, &client);
					check(last.right <= client.right, "Private Send statistics require horizontal scrolling");
					*reinterpret_cast<bool*>(value) = true;
				}
				return TRUE;
			}, reinterpret_cast<LPARAM>(&statsColumns));
			check(statsColumns, "Private Send transfer table is missing");
			HWND detailsButton = nullptr;
			EnumChildWindows(privateDialog.handle(), [](HWND child, LPARAM value) -> BOOL {
				wchar_t text[128] {};
				GetWindowTextW(child, text, std::size(text));
				if(std::wstring(text) == L"&Details >>")
					*reinterpret_cast<HWND*>(value) = child;
				if(std::wstring(text) == L"&Send file...")
					check(!IsWindowEnabled(child), "Unavailable Private Send button is enabled");
				if(std::wstring(text) == L"&Pause" || std::wstring(text) == L"&Cancel transfer")
					check(!IsWindowVisible(child), "Empty queue exposes transfer actions");
				if(std::wstring(text) == L"&Verify contact...")
					check(!IsWindowVisible(child), "Contact details should start collapsed");
				return TRUE;
			}, reinterpret_cast<LPARAM>(&detailsButton));
			check(detailsButton != nullptr, "Private Send details button is missing");
			const auto compactHeight = privateDialog.getWindowRect().size.y;
			SendMessage(detailsButton, BM_CLICK, 0, 0);
			check(privateDialog.getWindowRect().size.y > compactHeight, "Details did not expand the dialog");
			snapshot(privateDialog.handle(),
				directory + (mode == dwt::Appearance::Mode::Dark ? "private-send-details-dark.bmp" : "private-send-details-light.bmp"), true);
			SendMessage(detailsButton, BM_CLICK, 0, 0);
			check(privateDialog.getWindowRect().size.y == compactHeight, "Collapsing details changed the compact layout");

			bool visibleFooter = false;
			EnumChildWindows(
				privateDialog.handle(),
				[](HWND child, LPARAM value) -> BOOL {
					wchar_t text[512] {};
					GetWindowTextW(child, text, std::size(text));
					if(std::wstring(text).find(L"Default download folder:") == 0) {
						check(std::wstring(text).find(Text::toT(SETTING(DOWNLOAD_DIRECTORY))) != std::wstring::npos,
							"Private Send footer does not show the configured download folder");
						const auto dialog = GetAncestor(child, GA_ROOT);
						RECT row {}, client {};
						GetWindowRect(child, &row);
						MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&row), 2);
						GetClientRect(dialog, &client);
						*reinterpret_cast<bool*>(value) = row.top >= 0 && row.bottom <= client.bottom;
					}

					return TRUE;
				},
				reinterpret_cast<LPARAM>(&visibleFooter));
			check(visibleFooter, "Private Send footer is clipped by caption metrics");
			snapshot(privateDialog.handle(),
				directory + (mode == dwt::Appearance::Mode::Dark ? "private-send-dark.bmp" : "private-send-light.bmp"), true);
			DestroyWindow(privateDialog.handle());
		}

		auto page = new ExperimentalPage(window);
		page->resize(dwt::Rectangle(0, 0, 620, 620));
		page->layout();
		page->setVisible(true);
		for(unsigned i = 0; i < 200; ++i)
			app.dispatch();
		snapshot(page->handle(), directory + "sharing-dark.bmp");
		EnumChildWindows(
			page->handle(),
			[](HWND child, LPARAM) -> BOOL {
				if(auto tabs = dynamic_cast<dwt::TabView*>(dwt::hwnd_cast<dwt::Widget*>(child)))
					tabs->setActive(tabs->getChildren().back());
				return TRUE;
			},
			0);
		page->layout();
		HWND password = nullptr;
		EnumChildWindows(
			page->handle(),
			[](HWND child, LPARAM data) -> BOOL {
				if(dynamic_cast<dwt::TextBox*>(dwt::hwnd_cast<dwt::Widget*>(child)) && SendMessage(child, EM_GETPASSWORDCHAR, 0, 0))
					*reinterpret_cast<HWND*>(data) = child;
				return TRUE;
			},
			reinterpret_cast<LPARAM>(&password));
		check(password != nullptr, "MaxMind license key field is missing or unmasked");
		RECT bounds, client;
		GetWindowRect(password, &bounds);
		MapWindowPoints(nullptr, page->handle(), reinterpret_cast<POINT*>(&bounds), 2);
		GetClientRect(page->handle(), &client);
		check(bounds.left >= 0 && bounds.right <= client.right && bounds.top >= 0 && bounds.bottom <= client.bottom &&
				bounds.right > bounds.left,
			"MaxMind key field is clipped");
		SetWindowTextW(password, Text::toT(key).c_str());
		page->write();
		check(GeoIPUpdate::unprotectKey(SETTING(MAXMIND_LICENSE_KEY)) == key, "Experimental settings did not protect the license key");
		snapshot(page->handle(), directory + "settings-dark.bmp");
		EnumChildWindows(
			page->handle(),
			[](HWND child, LPARAM) -> BOOL {
				if(auto tabs = dynamic_cast<dwt::TabView*>(dwt::hwnd_cast<dwt::Widget*>(child)))
					tabs->setActive(tabs->getChildren().at(0));
				return TRUE;
			},
			0);
		for(int width : { 580, 780, 620 }) {
			page->resize(dwt::Rectangle(0, 0, width, 620));
			page->layout();
		}

		for(unsigned i = 0; i < 200; ++i)
			app.dispatch();
		snapshot(page->handle(), directory + "sharing-dark-resized.bmp");
		DestroyWindow(page->handle());
		DestroyWindow(window->handle());

		// Optional live check against a public synthetic fixture, never user settings.
		wchar_t testUrl[8192] {};
		if(GetEnvironmentVariableW(L"DCPLUSPLUS_GEOIP_TEST_URL", testUrl, std::size(testUrl))) {
			SettingsManager::getInstance()->set(SettingsManager::GEOIP_DOWNLOAD_URL, Text::fromT(testUrl));
			SettingsManager::getInstance()->set(SettingsManager::GEOIP_USE_MAXMIND, false);
			TransferEvents events;
			HttpManager::getInstance()->addListener(&events);
			GeoIPUpdate updater;
			updater.startUpdate(true);
			bool installed = false;
			for(unsigned i = 0; i < 450 && !installed && !events.failed; ++i) {
				Sleep(100);
				installed = updater.poll();
			}

			updater.stop();
			HttpManager::getInstance()->removeListener(&events);
			check(installed && events.added == 1 && events.bytes > 0 && events.completed == 1 && events.failed == 0,
				"Live hosted fixture download/HttpManager lifecycle failed");
			check(GeoManager::getInstance()->lookup("81.2.69.160").code == "GB", "Downloaded fixture failed lookup");
			std::puts("Live hosted MMDB installed with HttpManager progress and completion events.");
		}

		GeoManager::getInstance()->close();
		GeoManager::deleteInstance();
		WindowManager::deleteInstance();
		PrivateChatManager::deleteInstance();
		DirectTransferManager::deleteInstance();
		ConnectionManager::deleteInstance();
		CryptoManager::deleteInstance();
		ShareManager::deleteInstance();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		QueueManager::deleteInstance();
		UploadManager::deleteInstance();
		DownloadManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		ResourceManager::deleteInstance();
		std::puts("GeoIP credentials, country flags, and TransferView icons across all columns and tree expansion passed.");

		return 0;
	} catch(const std::exception& e) {
		std::fprintf(stderr, "%s\n", e.what());

		return 1;
	}
}
