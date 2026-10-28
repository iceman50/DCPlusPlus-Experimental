/* Copyright (C) 2026 iceman50. Distributed under GPL-3.0-or-later. */
#ifndef DWT_ICON_INFO_H
#define DWT_ICON_INFO_H

#include "Bitmap.h"
#include "../Point.h"

namespace dwt {

/// Owns the bitmap copies returned by GetIconInfo, never the source icon.
class IconInfo {
public:
	explicit IconInfo(HICON icon) noexcept : valid(::GetIconInfo(icon, &info) != FALSE) { }
	IconInfo(const IconInfo&) = delete;
	IconInfo& operator=(const IconInfo&) = delete;
	~IconInfo() {
		if(valid) {
			if(info.hbmColor) ::DeleteObject(info.hbmColor);
			if(info.hbmMask) ::DeleteObject(info.hbmMask);
		}
	}

	explicit operator bool() const noexcept { return valid; }
	/// Borrowed handles: valid only during this object's lifetime.
	const ICONINFO& get() const noexcept { return info; }
	Point getSize() const {
		if(!valid) return Point(0, 0);
		auto size = Bitmap::getSize(info.hbmColor ? info.hbmColor : info.hbmMask);
		// A monochrome mask contains both AND and XOR planes vertically.
		if(!info.hbmColor) size.y /= 2;
		return size;
	}

private:
	ICONINFO info { };
	bool valid;
};

}
#endif
