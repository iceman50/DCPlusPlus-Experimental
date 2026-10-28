/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, Jacek Sieka
  Copyright (C) 2026 iceman50

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
*/

#ifndef DWT_UTIL_WIN32_FILEDIALOG_H
#define DWT_UTIL_WIN32_FILEDIALOG_H

#include "../../WindowsHeaders.h"
#include "../../tstring.h"
#include "../../resources/Font.h"

#include <functional>
#include <utility>
#include <vector>

namespace dwt { namespace util { namespace win32 {

template<typename T>
class ComPtr {
public:
	ComPtr() : value(nullptr) { }

	explicit ComPtr(T* ptr, bool addRef = true) : value(ptr) {
		if(value && addRef) {
			value->AddRef();
		}
	}

	ComPtr(const ComPtr& rhs) : value(rhs.value) {
		if(value) {
			value->AddRef();
		}
	}

	ComPtr(ComPtr&& rhs) noexcept : value(rhs.value) {
		rhs.value = nullptr;
	}

	~ComPtr() {
		reset();
	}

	ComPtr& operator=(const ComPtr& rhs) {
		if(this != &rhs) {
			reset(rhs.value);
		}
		return *this;
	}

	ComPtr& operator=(ComPtr&& rhs) noexcept {
		if(this != &rhs) {
			reset();
			value = rhs.value;
			rhs.value = nullptr;
		}
		return *this;
	}

	T* get() const { return value; }
	T** put() {
		reset();
		return &value;
	}
	T* operator->() const { return value; }
	explicit operator bool() const { return value != nullptr; }

	void reset(T* ptr = nullptr, bool addRef = true) {
		if(ptr && addRef) {
			ptr->AddRef();
		}
		if(value) {
			value->Release();
		}
		value = ptr;
	}

	T* detach() {
		auto result = value;
		value = nullptr;
		return result;
	}

private:
	T* value;
};

typedef ComPtr<IShellItem> ShellItemPtr;

/** Optional application font/localization. Rendering and dark-mode selection
 * remain owned by DWT; unconfigured applications use system fonts and English. */
struct FileDialogDefaults {
	FontPtr font;
	std::function<tstring (const char*)> translate;
};
void setFileDialogDefaults(const FileDialogDefaults& defaults);

struct FileDialogControlItem {
	FileDialogControlItem(DWORD id_, const tstring& label_) :
		id(id_), label(label_) { }

	DWORD id;
	tstring label;
};

class FileDialogControls {
public:
	explicit FileDialogControls(IFileDialogCustomize& customize);
	explicit FileDialogControls(IFileDialog& dialog);

	IFileDialogCustomize& raw() const;

	void startVisualGroup(DWORD id, const tstring& label);
	void endVisualGroup();
	void makeProminent(DWORD id);

	void addLabel(DWORD id, const tstring& text);
	void addText(DWORD id, const tstring& text);
	void addPushButton(DWORD id, const tstring& label);
	void addCheckButton(DWORD id, const tstring& label, bool checked = false);
	void addRadioButtonList(DWORD id,
		const std::vector<FileDialogControlItem>& items = std::vector<FileDialogControlItem>());
	void addComboBox(DWORD id,
		const std::vector<FileDialogControlItem>& items = std::vector<FileDialogControlItem>());
	void addMenu(DWORD id, const tstring& label);
	void addControlItem(DWORD controlId, DWORD itemId, const tstring& label);
	void removeControlItem(DWORD controlId, DWORD itemId);
	void removeAllControlItems(DWORD controlId);

	void setControlLabel(DWORD id, const tstring& label);
	CDCONTROLSTATEF getControlState(DWORD id) const;
	void setControlState(DWORD id, CDCONTROLSTATEF state);
	bool getControlVisible(DWORD id) const;
	bool getControlEnabled(DWORD id) const;
	void setControlVisible(DWORD id, bool visible = true);
	void setControlEnabled(DWORD id, bool enabled = true);

	bool getCheckButtonChecked(DWORD id) const;
	void setCheckButtonChecked(DWORD id, bool checked = true);
	DWORD getSelectedControlItem(DWORD controlId) const;
	void setSelectedControlItem(DWORD controlId, DWORD itemId);

	CDCONTROLSTATEF getControlItemState(DWORD controlId, DWORD itemId) const;
	void setControlItemState(DWORD controlId, DWORD itemId, CDCONTROLSTATEF state);
	bool getControlItemVisible(DWORD controlId, DWORD itemId) const;
	bool getControlItemEnabled(DWORD controlId, DWORD itemId) const;
	void setControlItemVisible(DWORD controlId, DWORD itemId, bool visible = true);
	void setControlItemEnabled(DWORD controlId, DWORD itemId, bool enabled = true);

private:
	ComPtr<IFileDialogCustomize> customize;
};

struct FileDialogResult {
	ShellItemPtr item;
	tstring path;

	bool hasPath() const { return !path.empty(); }
	bool getDisplayName(SIGDN sigdn, tstring& value) const;
};

struct FileDialogEvents {
	typedef std::function<HRESULT (IFileDialog*)> FileOkCallback;
	typedef std::function<HRESULT (IFileDialog*, IShellItem*)> FolderChangingCallback;
	typedef std::function<void (IFileDialog*)> DialogCallback;
	typedef std::function<void (IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE&)> ShareViolationCallback;
	typedef std::function<void (IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE&)> OverwriteCallback;

	FileOkCallback fileOk;
	FolderChangingCallback folderChanging;
	DialogCallback folderChanged;
	DialogCallback selectionChanged;
	ShareViolationCallback shareViolation;
	DialogCallback typeChanged;
	OverwriteCallback overwrite;

	bool empty() const {
		return !fileOk && !folderChanging && !folderChanged && !selectionChanged &&
			!shareViolation && !typeChanged && !overwrite;
	}
};

typedef std::function<void (IFileDialogCustomize&)> FileDialogCustomizeCallback;
typedef std::function<void (FileDialogControls&)> FileDialogControlsCallback;

struct FileDialogOptions {
	HWND owner = nullptr;
	bool save = false;
	bool pickFolders = false;
	bool allowMultiple = false;
	bool forceFilesystem = true;
	bool allowOverwrite = true;
	DWORD legacyFlags = 0;
	FILEOPENDIALOGOPTIONS options = 0;
	unsigned activeFilter = 0;
	tstring title;
	tstring initialDirectory;
	tstring defaultExtension;
	tstring initialFileName;
	std::vector<std::pair<tstring, tstring>> filters;
	std::vector<std::pair<tstring, FDAP>> places;
	std::vector<std::pair<ShellItemPtr, FDAP>> shellPlaces;
	PCIDLIST_ABSOLUTE initialItem = nullptr;
	ShellItemPtr initialFolder;
	const GUID* clientGuid = nullptr;
	const FileDialogEvents* events = nullptr;
	FileDialogCustomizeCallback customize;
	FileDialogControlsCallback controls;
};

bool showFileDialog(const FileDialogOptions& options, std::vector<tstring>& paths);
bool showFileDialogItems(const FileDialogOptions& options, std::vector<FileDialogResult>& results);

} } }

#endif
