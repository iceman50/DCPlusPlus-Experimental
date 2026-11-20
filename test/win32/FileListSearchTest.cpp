// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
// Searching a file list window (Find / Find next) must find the right item, forwards
// and backwards, wrap around, and report when nothing matches, without filling the
// list pane for every directory it passes. Set DCPP_FILELIST_SEARCH_FILES to the number
// of files to generate (default 4000) to benchmark a large list.
#include "stdafx.h"
#include <win32/SingleInstance.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/IconManager.h>
#include <win32/util/UiServices.h>
#include <win32/util/AppAppearance.h>
#include <win32/util/ImageLists.h>
#include <win32/util/UiFonts.h>
#define private public
#define protected public
#include <win32/frames/filelist/DirectoryListingFrame.h>
#include <win32/components/TypedTable.h>
#include <win32/components/TypedTree.h>
#include <dwt/widgets/VirtualTree.h>
#undef protected
#undef private
#include <dcpp/DCPlusPlus.h>
#include <dcpp/core/io/File.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/user/CID.h>
#include <dwt/Application.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Window.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

const UINT SingleInstance::WMU_WHERE_ARE_YOU = WM_APP + 194;

namespace {

void check(bool value, const string& message) {
	if(!value) throw std::runtime_error(message);
}

/* Top directories, each with subdirectories holding the files. Two copies of "twin.dat"
(in the first and the middle top directory) and one "needle.dat" in the very last
subdirectory, the worst case for a forward search from the root. */
struct Layout { int tops, subs, files; };

Layout layoutFor(int totalFiles) {
	const int filesPerDir = 100;
	const int subs = 20;
	const int tops = std::max(3, totalFiles / (filesPerDir * subs));
	return { tops, subs, filesPerDir };
}

void writeList(const string& path, const Layout& layout) {
	const string tth = "LWPNACQDBZRYXW3VHJVCJ64QBZNGHOHHHZWCLNQ";
	string xml = "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\r\n"
		"<FileListing Version=\"1\" CID=\"" + CID::generate().toBase32() + "\" Base=\"/\" Generator=\"test\">\r\n";
	for(int t = 0; t < layout.tops; ++t) {
		xml += "<Directory Name=\"Top" + Util::toString(t) + "\">\r\n";
		for(int s = 0; s < layout.subs; ++s) {
			xml += "<Directory Name=\"Sub" + Util::toString(s) + "\">\r\n";
			for(int f = 0; f < layout.files; ++f) {
				string name = "file " + Util::toString(t) + "-" + Util::toString(s) + "-" + Util::toString(f) + ".bin";
				if(f == layout.files / 2 && s == 0 && (t == 0 || t == layout.tops / 2)) name = "twin.dat";
				if(f == layout.files - 1 && s == layout.subs - 1 && t == layout.tops - 1) name = "needle.dat";
				xml += "<File Name=\"" + name + "\" Size=\"1000\" TTH=\"" + tth + "\"/>\r\n";
			}
			xml += "</Directory>\r\n";
		}
		xml += "</Directory>\r\n";
	}
	xml += "</FileListing>\r\n";
	File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(xml);
}

struct Found {
	string dir;
	string file;
	double ms;
};

void pump(dwt::Application& app) {
	while(app.dispatch()) { }
}

}

int dwtMain(dwt::Application& app) {
	int result = 0;
	try {
		wchar_t cwd[32768];
		GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
		const auto directory = Text::fromT(cwd) + "\\build\\filelist-search-test\\";
		Util::PathsMap paths;
		for(int path = 0; path < Util::PATH_LAST; ++path)
			paths[static_cast<Util::Paths>(path)] = directory;
		Util::initialize(paths);
		File::ensureDirectory(directory);
		dcpp::startup();
		IconManager::initialize();
		// UiServices::init without its shell handler registration.
		AppAppearance::init();
		UiFonts::initDefault();
		UiServices::initFont();
		UiFonts::updateUploadFont();
		UiFonts::updateDownloadFont();
		UiFonts::initUserMatching();
		AppAppearance::initStyleDefaults();
		ImageLists::init();

		const auto requested = std::getenv("DCPP_FILELIST_SEARCH_FILES");
		const auto layout = layoutFor(requested ? std::max(1000, std::atoi(requested)) : 4000);
		const auto total = layout.tops * layout.subs * layout.files;
		const auto listPath = directory + "bench.xml";
		writeList(listPath, layout);

		auto window = new dwt::Window();
		dwt::Window::Seed seed;
		seed.location = dwt::Rectangle(80, 80, 900, 650);
		window->create(seed);
		auto tabs = window->addChild(dwt::TabView::Seed());
		tabs->resize(dwt::Rectangle(0, 0, 880, 620));

		const auto user = ClientManager::getInstance()->getUser(CID::generate());
		DirectoryListingFrame::openWindow(tabs, Text::toT(listPath), Util::emptyStringT,
			HintedUser(user, "adc://filelist.example.invalid"), 0, DirectoryListingFrame::FORCE_ACTIVE);
		pump(app);
		auto i = DirectoryListingFrame::lists.find(user);
		check(i != DirectoryListingFrame::lists.end(), "The file list window did not open");
		auto frame = i->second;
		const auto loadStart = std::chrono::steady_clock::now();
		while(!(frame->loaded && !frame->loader)) {
			check(std::chrono::steady_clock::now() - loadStart < std::chrono::minutes(5), "The file list did not load");
			pump(app);
			Sleep(10);
		}
		std::printf("%d files in %d directories\n", total, layout.tops * (layout.subs + 1));

		auto find = [&](const string& text, bool reverse) {
			frame->searchBox->setText(Text::toT(text));
			const auto start = std::chrono::steady_clock::now();
			frame->findFile(reverse);
			const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			pump(app);
			Found found { string(), string(), ms };
			if(auto sel = frame->dirs->getSelected()) {
				if(auto data = frame->dirs->getData(sel)) found.dir = frame->dl->getPath(data->dir);
			}
			const auto row = frame->files->getSelected();
			if(row != -1) {
				const auto& item = *frame->files->getData(row);
				found.file = item.type == DirectoryListingFrame::ItemInfo::FILE ? item.file->getName() : item.dir->getName();
			}
			return found;
		};
		auto goRoot = [&] {
			frame->dirs->setSelected(frame->treeRoot);
			frame->files->clearSelection();
			pump(app);
		};
		auto report = [](const char* what, const Found& found) {
			std::printf("  %-34s %9.1f ms  -> %s%s\n", what, found.ms, found.dir.c_str(), found.file.c_str());
		};

		const auto lastDir = "Top" + Util::toString(layout.tops - 1) + "\\Sub" + Util::toString(layout.subs - 1) + "\\";
		goRoot();
		auto found = find("needle", false);
		report("forward, match in the last folder", found);
		check(found.file == "needle.dat" && found.dir.find(lastDir) != string::npos, "Forward search found the wrong item: " + found.dir + found.file);

		goRoot();
		found = find("needle", true);
		report("backward, wrapping to the last", found);
		check(found.file == "needle.dat" && found.dir.find(lastDir) != string::npos, "Backward search found the wrong item: " + found.dir + found.file);

		goRoot();
		found = find("no such file anywhere", false);
		report("no match", found);
		check(found.file.empty() && frame->dirs->getSelected() == frame->treeRoot, "A search without matches changed the view");

		goRoot();
		found = find("twin", false);
		report("find twin (first)", found);
		check(found.file == "twin.dat" && found.dir.find("Top0\\Sub0\\") != string::npos, "First twin not found first: " + found.dir);
		found = find("twin", false);
		report("find next twin (second)", found);
		check(found.file == "twin.dat" && found.dir.find("Top" + Util::toString(layout.tops / 2) + "\\Sub0\\") != string::npos,
			"Second twin not found next: " + found.dir);
		found = find("twin", false);
		report("find next twin (wraps to first)", found);
		check(found.file == "twin.dat" && found.dir.find("Top0\\Sub0\\") != string::npos, "Find next did not wrap to the first twin: " + found.dir);
		found = find("twin", true);
		report("find previous twin (wraps back)", found);
		check(found.file == "twin.dat" && found.dir.find("Top" + Util::toString(layout.tops / 2) + "\\Sub0\\") != string::npos,
			"Find previous did not wrap back to the second twin: " + found.dir);

		frame->close();
		pump(app);
		DestroyWindow(window->handle());
		std::puts("PASS: file list search finds items forwards, backwards and across the wrap, and reports no matches");
	} catch(const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		result = 1;
	}
	dcpp::shutdown();
	return result;
}
