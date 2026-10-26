/* Copyright (C) 2026 iceman50. GPL-3.0-or-later. */
#ifndef DWT_THEMED_FILE_DIALOG_H
#define DWT_THEMED_FILE_DIALOG_H

#include <dwt/widgets/ModalDialog.h>
#include <dwt/util/win32/FileDialog.h>
#include <memory>
#include <vector>

namespace dwt { namespace util { namespace win32 {

tstring fileDialogText(const char* text);

// Internal implementation; applications use SaveDialog's normal API.
class ThemedFileDialog : public ModalDialog {
public:
	explicit ThemedFileDialog(const FileDialogOptions& options);
	~ThemedFileDialog();
	static bool supports(const FileDialogOptions& options);
	int run();
	const tstring& getPath() const { return selectedPath; }

private:
	struct Entry;
	struct Scan;
	std::shared_ptr<Scan> scan;
	FileDialogOptions options;
	FileDialogDefaults defaults;
	FontPtr font;
	std::vector<Entry> entries, allEntries;
	std::vector<tstring> history;
	tstring title, initialPath, startFolder, folder, requestedFolder, selectedPath;
	bool goingBack = false, loading = false, layingOut = false, done = false;
	int result = IDCANCEL;
	GridPtr grid = nullptr;
	TablePtr files = nullptr;
	TextBoxPtr location = nullptr, filename = nullptr;
	LabelPtr status = nullptr;
	ComboBoxPtr drives = nullptr, filters = nullptr;
	ButtonPtr back = nullptr, up = nullptr, save = nullptr, newFolder = nullptr;

	tstring tr(const char* text) const;
	bool init();
	void relayout();
	void navigate(tstring path, bool fromHistory = false);
	bool poll();
	void applyFilter();
	void activate();
	void accept();
	void createFolder();
	void stopScan();
	void finish(int value);
};

} } }
#endif
