/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ThemedFileDialog.h"

#include <dwt/util/GDI.h>
#include <dwt/widgets/MessageBox.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/util/win32/Path.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace dwt { namespace util { namespace win32 {

namespace {
FileDialogDefaults dialogDefaults;

tstring translate(const FileDialogDefaults& defaults, const char* text) {
	return defaults.translate ? defaults.translate(text) : tstring(text, text + std::strlen(text));
}

tstring errorText(DWORD error) {
	wchar_t text[1024] {};
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, error, 0, text, static_cast<DWORD>(std::size(text)), nullptr);
	tstring result(text);
	while(!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
	return result;
}

size_t rootLength(const tstring& path) {
	if(path.size() >= 7 && path.compare(0, 4, L"\\\\?\\") == 0 && path[5] == L':' && path[6] == L'\\') return 7;
	if(path.size() >= 3 && path[1] == L':' && path[2] == L'\\') return 3;
	if(path.compare(0, 2, L"\\\\") == 0) {
		const auto server = path.find(L'\\', path.compare(0, 8, L"\\\\?\\UNC\\") == 0 ? 8 : 2);
		if(server == tstring::npos) return path.size();
		const auto share = path.find(L'\\', server + 1);
		return share == tstring::npos ? path.size() : share + 1;
	}
	return 0;
}

tstring baseName(const tstring& path) { return path.substr(path.find_last_of(L"\\/") + 1); }

bool validName(const tstring& name) {
	if(name.empty() || name.size() > 255 || name.back() == L'.' || name.back() == L' ') return false;
	for(auto c : name) if(c < 32 || tstring(L"<>:\"/\\|?*").find(c) != tstring::npos) return false;
	auto stem = name.substr(0, name.find(L'.'));
	while(!stem.empty() && (stem.back() == L'.' || stem.back() == L' ')) stem.pop_back();
	for(const auto reserved : { L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$" })
		if(lstrcmpi(stem.c_str(), reserved) == 0) return false;
	if(stem.size() == 4 && (CompareStringOrdinal(stem.c_str(), 3, L"COM", 3, TRUE) == CSTR_EQUAL ||
		CompareStringOrdinal(stem.c_str(), 3, L"LPT", 3, TRUE) == CSTR_EQUAL))
	{
		const auto suffix = stem.back();
		if((suffix >= L'1' && suffix <= L'9') || suffix == L'\u00b9' || suffix == L'\u00b2' || suffix == L'\u00b3') return false;
	}
	return true;
}

bool matches(const tstring& name, const tstring& patterns) {
	for(size_t start = 0; start < patterns.size();) {
		const auto end = patterns.find(L';', start);
		const auto pattern = patterns.substr(start, end == tstring::npos ? end : end - start);
		if(PathMatchSpec(name.c_str(), pattern.c_str())) return true;
		if(end == tstring::npos) break;
		start = end + 1;
	}
	return patterns.empty();
}

tstring formatSize(uint64_t bytes) {
	wchar_t text[64] {};
	StrFormatByteSizeW(static_cast<LONGLONG>(std::min<uint64_t>(bytes, std::numeric_limits<LONGLONG>::max())), text, 64);
	return text;
}

void layoutDialog(ModalDialog& dialog, Grid* grid, int width) {
	for(unsigned pass = 0; pass < 2; ++pass) {
		const auto spacing = static_cast<long>(grid->getSpacing());
		const auto content = dialog.getContentRect();
		const auto height = grid->getPreferredSize().y;
		grid->resize(Rectangle(content.left() + spacing, content.top() + spacing, content.size.x - spacing * 2, height));
		RECT bounds { 0, 0, 0, height + spacing * 2 };
		dialog.adjustContentRect(bounds);
		dialog.resize(Rectangle(dialog.getWindowRect().pos, Point(dialog.scale(width), bounds.bottom - bounds.top)));
	}
}
dwt::Button::Seed buttonSeed(const tstring& caption, const FontPtr& font) {
	dwt::Button::Seed seed;
	seed.font = font;
	seed.caption = caption;
	seed.padding = dwt::Point(12, 5);
	return seed;
}
dwt::Label::Seed labelSeed(const tstring& caption, const FontPtr& font) {
	dwt::Label::Seed seed;
	seed.font = font;
	seed.caption = caption;
	seed.style &= ~SS_NOPREFIX;
	return seed;
}
tstring join(const tstring& folder, const tstring& name) {
	return folder + (folder.empty() || folder.back() == L'\\' ? L"" : L"\\") + name;
}
tstring parentFolder(tstring path) {
	const auto root = rootLength(path);
	while(path.size() > root && path.back() == L'\\') path.pop_back();
	const auto slash = path.find_last_of(L'\\');
	return slash == tstring::npos ? path : path.substr(0, std::max(root, slash));
}

// A borderless native single-line EDIT places its text at the top. Keep the
// native edit/caret one text line tall and center it inside a padded surface;
// this also preserves single-line Enter, selection and accessibility behavior.
class FileDialogEdit : public Container {
public:
	using ObjectType = FileDialogEdit*;
	struct Seed : Container::Seed {
		using WidgetType = FileDialogEdit;
		FontPtr font;
		explicit Seed(const FontPtr& font_) : Container::Seed(0, WS_EX_CONTROLPARENT), font(font_) { }
	};
	explicit FileDialogEdit(Widget* parent) : Container(parent) { }
	void create(const Seed& seed) {
		Container::create(seed);
		TextBox::Seed text;
		text.font = seed.font;
		text.style |= ES_AUTOHSCROLL;
		text.exStyle &= ~WS_EX_CLIENTEDGE;
		edit = addChild(text);
		onPainting([this](PaintCanvas& canvas) { paint(canvas); });
		onPrinting([this](Canvas& canvas) { paint(canvas); });
		onEraseBackground([this](Canvas& canvas) { paint(canvas); return true; });
		onLeftMouseDown([this](const MouseEvent&) { edit->setFocus(); return true; });
	}
	Point getPreferredSize() override {
		return edit->getTextSize(edit->getText().empty() ? L"M" : edit->getText()) + scale(Point(8, 10));
	}
	void layout() override {
		if(!edit) return;
		const auto size = getClientSize();
		const auto height = std::min(size.y, edit->getTextSize(L"M").y);
		const auto inset = scale(4);
		edit->resize(Rectangle(inset, (size.y - height) / 2,
			std::max(0L, size.x - inset * 2), height));
	}
	TextBox* edit = nullptr;
private:
	void paint(Canvas& canvas) {
		Brush brush(getAppearance().isManual() ? getAppearance().getPalette().surface : GetSysColor(COLOR_WINDOW));
		canvas.fill(Rectangle(getClientSize()), brush);
	}
};

class FolderNameDialog : public ModalDialog {
public:
	FolderNameDialog(Widget* parent, const FontPtr& font, const FileDialogDefaults& defaults) : ModalDialog(parent) {
		onInitDialog([this, font, defaults] {
			setText(translate(defaults, "New folder"));
			auto grid = addChild(Grid::Seed(3, 1));
			grid->setSpacing(8);
			grid->column(0).mode = GridInfo::FILL;
			grid->addChild(labelSeed(translate(defaults, "Folder name"), font));
			edit = grid->addChild(FileDialogEdit::Seed(font))->edit;
			auto buttons = grid->addChild(Grid::Seed(1, 2));
			auto button = buttonSeed(translate(defaults, "OK"), font);
			button.style |= BS_DEFPUSHBUTTON;
			button.menuHandle = reinterpret_cast<HMENU>(IDOK);
			buttons->addChild(button)->onClicked([this] { value = edit->getText(); endDialog(IDOK); });
			button.caption = translate(defaults, "Cancel");
			button.style &= ~BS_DEFPUSHBUTTON;
			button.menuHandle = reinterpret_cast<HMENU>(IDCANCEL);
			buttons->addChild(button)->onClicked([this] { endDialog(IDCANCEL); });
			layoutDialog(*this, grid, 365);
			centerWindow();
			edit->setFocus();
			return false;
		});
	}
	int run() { create(Seed(Point(365, 0))); return show(); }
	tstring value;
private:
	TextBoxPtr edit = nullptr;
};
}

void setFileDialogDefaults(const FileDialogDefaults& defaults) { dialogDefaults = defaults; }
tstring fileDialogText(const char* text) { return translate(dialogDefaults, text); }

struct ThemedFileDialog::Entry {
	tstring name;
	bool directory;
	uint64_t size;
};

struct ThemedFileDialog::Scan {
	std::mutex mutex;
	std::condition_variable wake;
	std::atomic<uint64_t> generation { 0 };
	std::atomic<bool> stopped { false };
	tstring path;
	bool pending = false, ready = false;
	DWORD error = ERROR_SUCCESS;
	std::vector<Entry> result;

	void run() noexcept {
		for(;;) {
			tstring directory;
			uint64_t request;
			{
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait(lock, [&] { return pending || stopped; });
				if(stopped) return;
				directory.swap(path);
				request = generation;
				pending = false;
			}
			std::vector<Entry> found;
			DWORD failure = ERROR_SUCCESS;
			HANDLE handle = INVALID_HANDLE_VALUE;
			try {
				WIN32_FIND_DATA data {};
				handle = FindFirstFileEx(toNativePath(join(directory, L"*")).c_str(),
					FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
				if(handle == INVALID_HANDLE_VALUE) {
					failure = GetLastError();
					if(failure == ERROR_FILE_NOT_FOUND) {
						const auto attributes = GetFileAttributes(toNativePath(directory).c_str());
						if(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
							failure = ERROR_SUCCESS;
					}
				} else {
					do {
						if(stopped || request != generation) break;
						if(lstrcmp(data.cFileName, L".") && lstrcmp(data.cFileName, L"..")) {
							found.push_back({ data.cFileName,
								(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
								(static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow });
						}
					} while(FindNextFile(handle, &data));
					failure = GetLastError();
					if(failure == ERROR_NO_MORE_FILES) failure = ERROR_SUCCESS;
				}
				if(!stopped && request == generation) {
					std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
						if(a.directory != b.directory) return a.directory;
						return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
					});
				}
			} catch(...) {
				failure = ERROR_NOT_ENOUGH_MEMORY;
			}
			if(handle != INVALID_HANDLE_VALUE) FindClose(handle);
			{
				std::lock_guard<std::mutex> lock(mutex);
				if(stopped) return;
				if(request != generation) continue;
				result = std::move(found);
				error = failure;
				ready = true;
			}
		}
	}
};

ThemedFileDialog::ThemedFileDialog(const FileDialogOptions& options_) :
	ModalDialog(hwnd_cast<Widget*>(options_.owner)), scan(std::make_shared<Scan>()),
	options(options_), defaults(dialogDefaults), font(defaults.font),
	title(options.title.empty() ? tr("Save As") : options.title), initialPath(options.initialFileName)
{
	if(!font) {
		NONCLIENTMETRICS metrics { sizeof(metrics) };
		font = SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) ?
			new Font(metrics.lfMessageFont) : new Font(Font::DefaultGui);
	}
	std::replace(initialPath.begin(), initialPath.end(), L'/', L'\\');
	if(!rootLength(initialPath) && initialPath.find(L'\\') != tstring::npos && options.initialDirectory.empty()) {
		const auto size = GetFullPathName(initialPath.c_str(), 0, nullptr, nullptr);
		if(size && size <= 32768) {
			std::vector<wchar_t> absolute(size);
			const auto length = GetFullPathName(initialPath.c_str(), size, absolute.data(), nullptr);
			if(length && length < size) initialPath.assign(absolute.data(), length);
		}
	}
	startFolder = options.initialDirectory;
	if(startFolder.empty() && rootLength(initialPath)) startFolder = parentFolder(initialPath);
	if(startFolder.empty()) {
		std::vector<wchar_t> current(GetCurrentDirectory(0, nullptr));
		if(!current.empty() && GetCurrentDirectory(static_cast<DWORD>(current.size()), current.data())) startFolder = current.data();
	}
	if(options.activeFilter >= options.filters.size()) options.activeFilter = 0;
	onInitDialog([this] { return init(); });
	onWindowPosChanged([this](const dwt::Rectangle&) { if(grid) relayout(); });
	onDestroy([this] { stopScan(); if(!done) finish(IDCANCEL); });
	onClosing([this] { finish(IDCANCEL); return true; });
}

ThemedFileDialog::~ThemedFileDialog() { stopScan(); }

tstring ThemedFileDialog::tr(const char* text) const { return translate(defaults, text); }

bool ThemedFileDialog::supports(const FileDialogOptions& options) {
	// Shell callbacks and namespace objects require a real IFileDialog. Preserve
	// that contract rather than dropping a callback or a root restriction.
	const auto supported = FOS_OVERWRITEPROMPT | FOS_STRICTFILETYPES | FOS_NOCHANGEDIR |
		FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | FOS_FORCESHOWHIDDEN;
	const DWORD legacy = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
		OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT | OFN_FORCESHOWHIDDEN | OFN_EXPLORER | OFN_LONGNAMES;
	return options.save && options.forceFilesystem && !options.pickFolders && !options.allowMultiple &&
		!options.events && !options.customize && !options.controls && !options.clientGuid &&
		!options.initialFolder && !options.initialItem && options.places.empty() && options.shellPlaces.empty() &&
		!(options.options & ~supported) && !(options.legacyFlags & ~legacy);
}

int ThemedFileDialog::run() {
	if(options.owner && !IsWindowEnabled(options.owner)) return IDCANCEL;
	create(Seed(Point(780, 0)));
	if(options.owner) {
		SetWindowLongPtr(handle(), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(options.owner));
		EnableWindow(options.owner, FALSE);
	}
	try {
		setVisible(true);
		auto& app = Application::instance();
		while(!done) if(!app.dispatch() && !app.sleep()) finish(IDCANCEL);
	} catch(...) {
		if(options.owner && IsWindow(options.owner)) EnableWindow(options.owner, TRUE);
		DestroyWindow(handle());
		throw;
	}
	if(options.owner && IsWindow(options.owner)) EnableWindow(options.owner, TRUE);
	DestroyWindow(handle());
	return result;
}

void ThemedFileDialog::finish(int value) { result = value; done = true; }

void ThemedFileDialog::stopScan() {
	{
		std::lock_guard<std::mutex> lock(scan->mutex);
		scan->stopped = true;
	}
	scan->wake.notify_one();
}

void ThemedFileDialog::relayout() {
	if(layingOut) return;
	layingOut = true;
	layoutDialog(*this, grid, 780);
	layingOut = false;
}

bool ThemedFileDialog::init() {
	setText(title);
	setFont(font);
	grid = addChild(Grid::Seed(6, 1));
	grid->setSpacing(8);
	grid->column(0).mode = GridInfo::FILL;
	auto toolbar = grid->addChild(Grid::Seed(1, 6));
	toolbar->column(4).mode = GridInfo::FILL;
	toolbar->column(5).mode = GridInfo::STATIC;
	toolbar->column(5).size = 85;
	back = toolbar->addChild(buttonSeed(tr("&Back"), font));
	back->onClicked([this] { if(!history.empty()) navigate(history.back(), true); });
	up = toolbar->addChild(buttonSeed(tr("&Up"), font));
	up->onClicked([this] { navigate(parentFolder(folder.empty() ? requestedFolder : folder)); });
	toolbar->addChild(buttonSeed(tr("Start &folder"), font))->onClicked([this] {
		navigate(startFolder);
	});
	newFolder = toolbar->addChild(buttonSeed(tr("New f&older..."), font));
	newFolder->onClicked([this] { createFolder(); });
	toolbar->addChild(dwt::Label::Seed());
	ComboBox::Seed comboSeed;
	comboSeed.style |= CBS_DROPDOWNLIST;
	comboSeed.font = font;
	drives = toolbar->addChild(comboSeed);
	const auto mask = GetLogicalDrives();
	for(unsigned drive = 0; drive < 26; ++drive) {
		const tstring root { static_cast<wchar_t>(L'A' + drive), L':', L'\\' };
		if(mask & (1u << drive))
			drives->addValue(root);
	}
	drives->onSelectionChanged([this] { navigate(drives->getText()); });
	back->setEnabled(false);

	auto address = grid->addChild(Grid::Seed(1, 3));
	address->column(1).mode = GridInfo::FILL;
	address->addChild(labelSeed(tr("&Folder:"), font));
	location = address->addChild(FileDialogEdit::Seed(font))->edit;
	address->addChild(buttonSeed(tr("&Go"), font))->onClicked([this] { navigate(location->getText()); });

	Table::Seed seed;
	seed.lvStyle |= LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
	seed.font = font;
	seed.style |= LVS_SINGLESEL | LVS_OWNERDATA;
	seed.exStyle &= ~WS_EX_CLIENTEDGE;
	files = grid->addChild(seed);
	files->addColumn(tr("Name"), scale(430));
	files->addColumn(tr("Type"), scale(130));
	files->addColumn(tr("Size"), scale(120), Column::RIGHT);
	grid->row(2).mode = GridInfo::STATIC;
	grid->row(2).size = 300;
	grid->row(2).align = GridInfo::STRETCH;
	files->onRaw([this](WPARAM, LPARAM value) -> LRESULT {
		auto& item = reinterpret_cast<NMLVDISPINFO*>(value)->item;
		if((item.mask & LVIF_TEXT) && item.iItem >= 0 && static_cast<size_t>(item.iItem) < entries.size()) {
			const auto& entry = entries[item.iItem];
			const auto text = item.iSubItem == 0 ? entry.name : item.iSubItem == 1 ?
				(entry.directory ? tr("Folder") : tr("File")) :
				(entry.directory ? tstring() : formatSize(entry.size));
			if(item.pszText && item.cchTextMax > 0) lstrcpyn(item.pszText, text.c_str(), item.cchTextMax);
		}
		return 0;
	}, dwt::Message(WM_NOTIFY, LVN_GETDISPINFO));
	files->onRaw([this](WPARAM, LPARAM value) -> LRESULT {
		const auto& find = *reinterpret_cast<NMLVFINDITEM*>(value);
		if(!(find.lvfi.flags & LVFI_STRING) || !find.lvfi.psz || entries.empty()) return -1;
		const auto length = lstrlen(find.lvfi.psz);
		for(size_t offset = 0; offset < entries.size(); ++offset) {
			const auto index = (std::max(0, find.iStart) + offset) % entries.size();
			const auto& name = entries[index].name;
			if(name.size() >= static_cast<size_t>(length) &&
				CompareStringOrdinal(name.c_str(), length, find.lvfi.psz, length, TRUE) == CSTR_EQUAL)
				return static_cast<LRESULT>(index);
		}
		return -1;
	}, dwt::Message(WM_NOTIFY, LVN_ODFINDITEM));
	files->onSelectionChanged([this] {
		const auto index = files->getSelected();
		if(index >= 0 && static_cast<size_t>(index) < entries.size() && !entries[index].directory)
			filename->setText(entries[index].name);
	});
	files->onDblClicked([this] { activate(); });
	files->onKeyDown([this](int key) {
		if(key == VK_RETURN) { activate(); return true; }
		if(key == VK_BACK) { navigate(parentFolder(folder)); return true; }
		if(key == VK_F5) { navigate(folder); return true; }
		return false;
	});
	files->onGetEmptyText([this] { return loading ? tr("Loading folder...") : tr("This folder is empty."); });
	status = grid->addChild(labelSeed(tr("Choose a folder and filename."), font));
	auto types = grid->addChild(Grid::Seed(1, 2));
	types->column(1).mode = GridInfo::FILL;
	types->addChild(labelSeed(tr("Save as &type:"), font));
	filters = types->addChild(comboSeed);
	if(options.filters.empty()) filters->addValue(tr("All files (*.*)"));
	else for(const auto& filter : options.filters) filters->addValue(filter.first);
	filters->setSelected(options.activeFilter);
	filters->onSelectionChanged([this] {
		options.activeFilter = static_cast<unsigned>(filters->getSelected());
		applyFilter();
	});

	auto footer = grid->addChild(Grid::Seed(1, 4));
	footer->column(1).mode = GridInfo::FILL;
	footer->column(2).mode = footer->column(3).mode = GridInfo::STATIC;
	footer->column(2).size = footer->column(3).size = 80;
	footer->addChild(labelSeed(tr("File &name:"), font));
	filename = footer->addChild(FileDialogEdit::Seed(font))->edit;
	filename->setText(options.initialDirectory.empty() ? baseName(initialPath) : initialPath);
	auto button = buttonSeed(tr("&Save"), font);
	button.style |= BS_DEFPUSHBUTTON;
	button.menuHandle = reinterpret_cast<HMENU>(IDOK);
	save = footer->addChild(button);
	save->onClicked([this] {
		if(GetFocus() == location->handle()) navigate(location->getText());
		else if(GetFocus() == files->handle()) activate();
		else accept();
	});
	button.caption = tr("Cancel");
	button.style &= ~BS_DEFPUSHBUTTON;
	button.menuHandle = reinterpret_cast<HMENU>(IDCANCEL);
	footer->addChild(button)->onClicked([this] { finish(IDCANCEL); });

	// A single worker owns only shared scan state, never a widget or application
	// singleton. Closing the picker cancels it without waiting on filesystem I/O.
	std::thread([state = scan] { state->run(); }).detach();
	navigate(startFolder);
	setTimer([this] { return poll(); }, 50);
	relayout();
	centerWindow();
	filename->setFocus();
	filename->setSelection(0, -1);
	return false;
}

void ThemedFileDialog::navigate(tstring path, bool fromHistory) {
	std::replace(path.begin(), path.end(), L'/', L'\\');
	if(path.empty()) return;
	if(!rootLength(path) && !folder.empty()) path = join(folder, path);
	const auto count = GetFullPathName(path.c_str(), 0, nullptr, nullptr);
	if(!count || count > 32768) { status->setText(tr("Invalid folder path.")); return; }
	std::vector<wchar_t> buffer(count);
	const auto length = GetFullPathName(path.c_str(), count, buffer.data(), nullptr);
	if(!length || length >= count) { status->setText(tr("Invalid folder path.")); return; }
	path.assign(buffer.data(), length);
	while(path.size() > rootLength(path) && path.back() == L'\\') path.pop_back();
	requestedFolder = path;
	goingBack = fromHistory;
	loading = true;
	location->setText(path);
	save->setEnabled(false);
	newFolder->setEnabled(false);
	status->setText(tr("Loading folder..."));
	{
		std::lock_guard<std::mutex> lock(scan->mutex);
		++scan->generation;
		scan->path = path;
		scan->pending = true;
		scan->ready = false;
	}
	scan->wake.notify_one();
}

bool ThemedFileDialog::poll() {
	DWORD error;
	std::vector<Entry> result;
	{
		std::lock_guard<std::mutex> lock(scan->mutex);
		if(!scan->ready) return true;
		scan->ready = false;
		error = scan->error;
		result = std::move(scan->result);
	}
	loading = false;
	if(error) {
		status->setText(tr("Cannot open this folder.") + L" " + errorText(error));
		if(!folder.empty()) location->setText(folder);
	} else {
		if(goingBack && !history.empty()) history.pop_back();
		else if(!folder.empty() && folder != requestedFolder) history.push_back(folder);
		folder = requestedFolder;
		drives->setSelected(drives->findString(folder.substr(0, 3)));
		allEntries = std::move(result);
		applyFilter();
		status->setText(tr("Choose a folder and filename."));
	}
	back->setEnabled(!history.empty());
	up->setEnabled(parentFolder(folder.empty() ? requestedFolder : folder) != (folder.empty() ? requestedFolder : folder));
	save->setEnabled(!folder.empty());
	newFolder->setEnabled(!folder.empty());
	return true;
}

void ThemedFileDialog::applyFilter() {
	files->clearSelection();
	files->resize(0u);
	entries.clear();
	for(const auto& entry : allEntries) {
		if(entry.directory || options.filters.empty() || matches(entry.name, options.filters[options.activeFilter].second))
			entries.push_back(entry);
	}
	files->resize(static_cast<unsigned>(entries.size()));
	files->redraw();
}

void ThemedFileDialog::activate() {
	if(loading) return;
	const auto index = files->getSelected();
	if(index >= 0 && static_cast<size_t>(index) < entries.size() && entries[index].directory)
		navigate(join(folder, entries[index].name));
	else accept();
}

void ThemedFileDialog::accept() {
	if(loading || folder.empty()) return;
	if(location->getText() != folder) { navigate(location->getText()); return; }
	auto name = filename->getText();
	std::replace(name.begin(), name.end(), L'/', L'\\');
	if(name.find(L'\\') != tstring::npos) {
		const auto leaf = baseName(name);
		if(!validName(leaf)) { status->setText(tr("Enter a valid filename.")); return; }
		const auto path = rootLength(name) ? name : join(folder, name);
		filename->setText(leaf);
		navigate(parentFolder(path));
		return;
	}
	auto extension = options.defaultExtension;
	if(!extension.empty() && extension.front() == L'.') extension.erase(0, 1);
	if(!options.filters.empty()) {
		const auto& pattern = options.filters[options.activeFilter].second;
		if(pattern.compare(0, 2, L"*.") == 0 && pattern.find_first_of(L"*?;", 2) == tstring::npos)
			extension = pattern.substr(2);
	}
	if(!name.empty() && name.find(L'.') == tstring::npos && !extension.empty()) name += L"." + extension;
	if(!validName(name)) {
		status->setText(tr("Enter a valid filename, without a folder path or reserved characters."));
		filename->setFocus();
		return;
	}
	if((options.options & FOS_STRICTFILETYPES) && !options.filters.empty() &&
		!matches(name, options.filters[options.activeFilter].second))
	{
		status->setText(tr("Choose a filename matching the selected file type."));
		return;
	}
	const auto path = join(folder, name);
	const auto attributes = GetFileAttributes(toNativePath(path).c_str());
	const auto error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
	if(attributes != INVALID_FILE_ATTRIBUTES) {
		if((attributes & FILE_ATTRIBUTE_DIRECTORY) || !options.allowOverwrite) {
			status->setText(tr("That name already exists. Choose another filename."));
			filename->setFocus();
			return;
		}
		if(MessageBox(this).show(tr("Replace the existing file?") + L"\n\n" + path,
			title, MessageBox::BOX_YESNO, MessageBox::BOX_ICONQUESTION) != MessageBox::RETBOX_YES) return;
	} else if(error != ERROR_FILE_NOT_FOUND) {
		status->setText(tr("Cannot use this destination.") + L" " + errorText(error));
		return;
	}
	// Selection never creates or truncates a file. The caller performs the save.
	selectedPath = path;
	finish(IDOK);
}

void ThemedFileDialog::createFolder() {
	if(loading || folder.empty()) return;
	FolderNameDialog dialog(this, font, defaults);
	if(dialog.run() != IDOK) return;
	const auto name = dialog.value;
	if(!validName(name)) {
		status->setText(tr("Enter a valid folder name, without a path or reserved characters."));
		return;
	}
	const auto path = join(folder, name);
	if(!CreateDirectory(toNativePath(path).c_str(), nullptr)) {
		status->setText(tr("Cannot create this folder.") + L" " + errorText(GetLastError()));
		return;
	}
	navigate(path);
}

} } }
