/* Copyright (C) 2026 iceman50. Distributed under GPL-3.0-or-later. */
#ifndef DWT_DIB_BITMAP_H
#define DWT_DIB_BITMAP_H

#include "Bitmap.h"
#include "../Point.h"
#include <cstddef>
#include <limits>

namespace dwt {

/// Owned 32-bit BI_RGB DIB. Storage is B,G,R,unused/alpha bytes, with no
/// implicit alpha conversion or initialization. AlphaBlend callers must supply
/// premultiplied colors themselves. Deselect before destroying the bitmap.
class DibBitmap : public Bitmap {
public:
	enum class RowOrder { TopDown, BottomUp };

	/// Failure leaves a null handle/pixels and zero dimensions/stride. Positive
	/// dimensions and an explicit row order avoid signed-height ambiguity.
	DibBitmap(HDC dc, const Point& size, RowOrder order) :
		Bitmap(static_cast<HBITMAP>(nullptr)), order(order) {
		if(size.x <= 0 || size.y <= 0) return;
		const auto width = static_cast<size_t>(size.x);
		const auto height = static_cast<size_t>(size.y);
		const auto limit = (std::numeric_limits<size_t>::max)();
		if(width > limit / sizeof(DWORD)) return;
		const auto rowBytes = width * sizeof(DWORD);
		if(height > limit / rowBytes) return;
		// BITMAPINFOHEADER image sizes are DWORDs, even on 64-bit Windows.
		if(rowBytes * height > (std::numeric_limits<DWORD>::max)()) return;

		BITMAPINFO format { };
		format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		format.bmiHeader.biWidth = size.x;
		format.bmiHeader.biHeight = order == RowOrder::TopDown ? -size.y : size.y;
		format.bmiHeader.biPlanes = 1;
		format.bmiHeader.biBitCount = 32;
		format.bmiHeader.biCompression = BI_RGB;
		void* storage = nullptr;
		const auto bitmap = ::CreateDIBSection(dc, &format, DIB_RGB_COLORS, &storage, nullptr, 0);
		if(!bitmap) return;
		if(!storage) {
			::DeleteObject(bitmap);
			return;
		}
		init(bitmap, true);
		pixels_ = storage;
		size_ = size;
		stride_ = rowBytes;
	}

	/// Call GdiFlush on the drawing thread before CPU access following GDI writes.
	/// Storage starts at the top or bottom row according to rowOrder().
	void* pixels() noexcept { return pixels_; }
	const void* pixels() const noexcept { return pixels_; }
	Point getSize() const { return size_; }
	size_t stride() const noexcept { return stride_; }
	RowOrder rowOrder() const noexcept { return order; }

private:
	void* pixels_ = nullptr;
	Point size_ { 0, 0 };
	size_t stride_ = 0;
	RowOrder order;
};

using DibBitmapPtr = util::intrusive_ptr<DibBitmap>;

}
#endif
