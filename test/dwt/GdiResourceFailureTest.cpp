/* Copyright (C) 2026 iceman50. Distributed under GPL-3.0-or-later. */
// Compile uniquely named copies of the header-only wrappers against injected
// failures. Production types and linked DWT code retain the real Win32 calls.
#include <dwt/Application.h>
#include <dwt/resources/Bitmap.h>
#include <dwt/Point.h>
#include <cstdio>
#include <stdexcept>

namespace {
enum class Failure { None, Allocation, MissingStorage };
Failure failure = Failure::None;
int allocations = 0;
HBITMAP incompleteBitmap = nullptr;
BITMAPINFOHEADER requestedFormat { };

void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

bool bitmapExists(HBITMAP bitmap) {
	BITMAP info { };
	return ::GetObject(bitmap, sizeof(info), &info) != 0;
}
}

HBITMAP WINAPI auditCreateDIBSection(HDC dc, const BITMAPINFO* info, UINT usage,
	void** pixels, HANDLE section, DWORD offset) {
	++allocations;
	requestedFormat = info->bmiHeader;
	if(failure == Failure::Allocation) {
		*pixels = nullptr;
		return nullptr;
	}
	if(failure == Failure::MissingStorage) {
		*pixels = nullptr;
		incompleteBitmap = ::CreateBitmap(1, 1, 1, 1, nullptr);
		check(incompleteBitmap != nullptr, "Failure fixture allocation failed");
		return incompleteBitmap;
	}
	return ::CreateDIBSection(dc, info, usage, pixels, section, offset);
}

#define CreateDIBSection auditCreateDIBSection
#define DibBitmap FailureDibBitmap
#define DibBitmapPtr FailureDibBitmapPtr
#include <dwt/resources/DibBitmap.h>
#undef DibBitmapPtr
#undef DibBitmap
#undef CreateDIBSection

int dwtMain(dwt::Application&) {
	try {
		using Bitmap = dwt::FailureDibBitmap;
		for(auto mode : { Failure::Allocation, Failure::MissingStorage }) {
			failure = mode;
			Bitmap bitmap(nullptr, dwt::Point(3, 4), Bitmap::RowOrder::TopDown);
			check(!bitmap.handle() && !bitmap.pixels() && bitmap.stride() == 0 && bitmap.getSize() == dwt::Point(0, 0),
				"Failed allocation exposed partial bitmap state");
			check(requestedFormat.biWidth == 3 && requestedFormat.biHeight == -4 &&
				requestedFormat.biPlanes == 1 && requestedFormat.biBitCount == 32 && requestedFormat.biCompression == BI_RGB,
				"Top-down allocation format changed");
			if(mode == Failure::MissingStorage)
				check(!bitmapExists(incompleteBitmap), "Incomplete bitmap was not released");
		}
		const int beforeInvalid = allocations;
		Bitmap invalid(nullptr, dwt::Point(-1, 4), Bitmap::RowOrder::TopDown);
		Bitmap overflow(nullptr, dwt::Point(0x7fffffff, 0x7fffffff), Bitmap::RowOrder::BottomUp);
		check(allocations == beforeInvalid, "Invalid sizes reached GDI allocation");
		failure = Failure::None;
		HBITMAP handle;
		{
			Bitmap bitmap(nullptr, dwt::Point(3, 4), Bitmap::RowOrder::BottomUp);
			handle = bitmap.handle();
			check(handle && bitmap.pixels() && bitmap.stride() == 12 && requestedFormat.biHeight == 4,
				"Allocation did not recover after failures");
		}
		check(!bitmapExists(handle), "Recovered allocation leaked");
		std::puts("PASS: allocation failure, incomplete-result cleanup, invalid-size rejection and recovery");
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
