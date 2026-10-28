/* Copyright (C) 2026 iceman50. Distributed under GPL-3.0-or-later. */
#include <dwt/Application.h>
#include <dwt/CanvasClasses.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/DibBitmap.h>
#include <dwt/resources/IconInfo.h>
#include <dwt/resources/Pen.h>

#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

bool bitmapExists(HBITMAP bitmap) {
	BITMAP info { };
	// GetObjectType can still return OBJ_BITMAP for a deleted handle.
	return ::GetObject(bitmap, sizeof(info), &info) != 0;
}

void stateTests() {
	dwt::CompatibleCanvas canvas(nullptr);
	check(canvas.handle() != nullptr, "Memory DC creation failed");
	const auto dc = canvas.handle();
	dwt::Pen outerPen(RGB(20, 40, 60)), innerPen(RGB(80, 100, 120));
	const auto originalPen = ::GetCurrentObject(dc, OBJ_PEN);
	using Selection = decltype(canvas.select(outerPen));
	using Background = decltype(canvas.setBkMode(true));
	static_assert(!std::is_move_assignable<Selection>::value, "Selection assignment loses saved state");
	static_assert(!std::is_move_assignable<Background>::value, "Background assignment loses saved state");
	static_assert(!std::is_copy_constructible<dwt::CanvasState>::value, "DC state must have one owner");
	static_assert(!std::is_move_assignable<dwt::CanvasState>::value, "DC state assignment breaks nesting");
	{
		auto outer = canvas.select(outerPen);
		{
			auto inner = canvas.select(innerPen);
			auto moved = std::move(inner);
			check(::GetCurrentObject(dc, OBJ_PEN) == innerPen.handle(), "Moving selection changed selected pen");
		}
		check(::GetCurrentObject(dc, OBJ_PEN) == outerPen.handle(), "Nested selection did not restore pen");
	}
	check(::GetCurrentObject(dc, OBJ_PEN) == originalPen, "Selection did not restore original pen");
	::SetBkMode(dc, OPAQUE);
	{
		auto outer = canvas.setBkMode(true);
		{
			auto inner = canvas.setBkMode(false);
			auto moved = std::move(inner);
		}
		check(::GetBkMode(dc) == TRANSPARENT, "Nested background mode not restored");
	}
	check(::GetBkMode(dc) == OPAQUE, "Original background mode not restored");

	dwt::Region originalClip(dwt::Rectangle(1, 2, 30, 40));
	::SelectClipRgn(dc, originalClip.handle());
	const auto originalColor = ::SetTextColor(dc, RGB(1, 2, 3));
	try {
		auto outer = canvas.saveState();
		check(static_cast<bool>(outer), "SaveDC failed");
		::SelectObject(dc, outerPen.handle());
		::SetTextColor(dc, RGB(4, 5, 6));
		::SetBkMode(dc, TRANSPARENT);
		::SetViewportOrgEx(dc, 7, 8, nullptr);
		{
			auto inner = canvas.saveState();
			::SelectObject(dc, innerPen.handle());
			::SetTextColor(dc, RGB(7, 8, 9));
		}
		check(::GetCurrentObject(dc, OBJ_PEN) == outerPen.handle(), "Nested state lost outer pen");
		check(::GetTextColor(dc) == RGB(4, 5, 6), "Nested state lost outer color");
		::SelectClipRgn(dc, nullptr);
		// Restore the exact saved ID, even if a callee leaves another save behind.
		check(::SaveDC(dc) != 0, "Additional SaveDC failed");
		throw 7;
	} catch(int) { }
	check(::GetCurrentObject(dc, OBJ_PEN) == originalPen, "Exception did not restore pen");
	check(::GetTextColor(dc) == RGB(1, 2, 3), "Exception did not restore color");
	check(::GetBkMode(dc) == OPAQUE, "Exception did not restore background mode");
	POINT origin { };
	::GetViewportOrgEx(dc, &origin);
	check(origin.x == 0 && origin.y == 0, "Exception did not restore viewport");
	dwt::Region clip(dwt::Rectangle(0, 0, 0, 0));
	check(::GetClipRgn(dc, clip.handle()) == 1 && ::EqualRgn(clip.handle(), originalClip.handle()), "Exception did not restore clipping");
	{
		auto saved = canvas.saveState();
		auto moved = std::move(saved);
		check(!saved && static_cast<bool>(moved), "State move did not transfer ownership");
		::SetTextColor(dc, RGB(9, 9, 9));
		check(moved.restore(), "Explicit restoration failed");
		check(!moved.restore(), "State was restored twice");
		::SetTextColor(dc, originalColor);
	}
	check(::GetTextColor(dc) == originalColor, "Destructor restored consumed state");
	dwt::CanvasState invalid(nullptr);
	check(!invalid && !invalid.restore(), "Failed save was treated as valid");
	std::puts("PASS: nested/moved guards, explicit restoration, clipping and exception unwinding");
}

void bitmapTests() {
	using Order = dwt::DibBitmap::RowOrder;
	static_assert(!std::is_copy_constructible<dwt::DibBitmap>::value, "Bitmap must not duplicate ownership");
	for(auto order : { Order::TopDown, Order::BottomUp }) {
		dwt::DibBitmap bitmap(nullptr, dwt::Point(3, 2), order);
		check(bitmap.handle() && bitmap.pixels(), "DIB creation failed");
		check(bitmap.stride() == 12 && bitmap.getSize() == dwt::Point(3, 2) && bitmap.rowOrder() == order, "DIB metadata incorrect");
		DIBSECTION section { };
		check(::GetObject(bitmap.handle(), sizeof(section), &section) == sizeof(section), "DIB metadata inaccessible");
		check(section.dsBm.bmBits == bitmap.pixels() && section.dsBm.bmBitsPixel == 32, "DIB storage/format mismatch");
		// Compare against the former direct allocation path, byte for byte.
		BITMAPINFO format { { sizeof(BITMAPINFOHEADER), 3, order == Order::TopDown ? -2L : 2L, 1, 32, BI_RGB } };
		void* referencePixels = nullptr;
		dwt::Bitmap reference(::CreateDIBSection(nullptr, &format, DIB_RGB_COLORS, &referencePixels, nullptr, 0));
		check(reference.handle() && referencePixels, "Reference DIB creation failed");
		DIBSECTION referenceSection { };
		check(::GetObject(reference.handle(), sizeof(referenceSection), &referenceSection) == sizeof(referenceSection), "Reference metadata inaccessible");
		// GetObject can report an absolute height; verify orientation through the
		// actual pixel rows below, and metadata against the native allocation.
		check(section.dsBmih.biHeight == referenceSection.dsBmih.biHeight &&
			section.dsBm.bmWidthBytes == referenceSection.dsBm.bmWidthBytes, "DIB metadata differs from legacy allocation");
		std::memset(bitmap.pixels(), 0x55, 24);
		std::memset(referencePixels, 0x55, 24);
		dwt::CompatibleCanvas actual(nullptr), expected(nullptr);
		auto selected = actual.select(bitmap);
		auto referenceSelected = expected.select(reference);
		for(auto dc : { actual.handle(), expected.handle() }) {
			::SetPixelV(dc, 0, 0, RGB(255, 0, 0));
			::SetPixelV(dc, 2, 1, RGB(0, 0, 255));
		}
		::GdiFlush();
		check(std::memcmp(bitmap.pixels(), referencePixels, 24) == 0, "DIB pixels differ from legacy allocation");
		const auto pixels = static_cast<const DWORD*>(bitmap.pixels());
		check((pixels[order == Order::TopDown ? 0 : 3] & 0xffffff) == 0xff0000, "CPU pixel row orientation incorrect");
		check(::GetPixel(actual.handle(), 2, 1) == RGB(0, 0, 255), "GDI color interpretation changed");
	}
	for(const auto size : { dwt::Point(0, 1), dwt::Point(1, 0), dwt::Point(-1, 2), dwt::Point(1, LONG_MIN), dwt::Point(LONG_MAX, LONG_MAX), dwt::Point(1, LONG_MAX) }) {
		dwt::DibBitmap bitmap(nullptr, size, Order::TopDown);
		check(!bitmap.handle() && !bitmap.pixels() && bitmap.stride() == 0 && bitmap.getSize() == dwt::Point(0, 0), "Invalid/overflowing DIB was accepted");
	}
	HBITMAP handle;
	{
		dwt::DibBitmapPtr derived = new dwt::DibBitmap(nullptr, dwt::Point(2, 2), Order::TopDown);
		dwt::BitmapPtr base = derived;
		handle = base->handle();
		derived.reset();
		check(bitmapExists(handle), "Base BitmapPtr lost derived bitmap");
	}
	check(!bitmapExists(handle), "BitmapPtr leaked derived bitmap");
	std::puts("PASS: DIB pixel equivalence, both orientations, bounds checks and shared ownership");
}

void iconTests() {
	static_assert(!std::is_copy_constructible<dwt::IconInfo>::value, "Icon info must have one owner");
	const auto shared = ::LoadIcon(nullptr, IDI_APPLICATION);
	check(shared != nullptr, "Shared icon unavailable");
	HBITMAP color, mask;
	{
		dwt::IconInfo info(shared);
		check(static_cast<bool>(info), "GetIconInfo failed");
		color = info.get().hbmColor;
		mask = info.get().hbmMask;
		check(info.getSize() == dwt::Bitmap::getSize(color), "Color icon size changed");
	}
	check(!bitmapExists(color) && !bitmapExists(mask), "IconInfo leaked bitmap copies");
	dwt::IconInfo stillValid(shared);
	check(static_cast<bool>(stillValid), "IconInfo destroyed borrowed icon");
	dwt::IconInfo invalid(nullptr);
	check(!invalid && invalid.getSize() == dwt::Point(0, 0), "Invalid icon reported a size");

	BYTE andBits[16] { }, xorBits[16] { };
	dwt::Icon monochrome(::CreateIcon(::GetModuleHandle(nullptr), 8, 8, 1, 1, andBits, xorBits));
	check(monochrome.handle() != nullptr, "Monochrome icon creation failed");
	dwt::IconInfo monoInfo(monochrome.handle());
	check(monoInfo && !monoInfo.get().hbmColor && monoInfo.getSize() == dwt::Point(8, 8), "Monochrome icon mask height incorrect");
	// Existing Icon::getSize behavior is deliberately preserved by this refactor.
	check(monochrome.getSize() == dwt::Point(0, 0), "Legacy icon sizing changed");
	std::puts("PASS: icon bitmap ownership, invalid handles and monochrome metadata");
}

void regionTests() {
	for(auto mode : { dwt::Region::CombineMode::Intersection, dwt::Region::CombineMode::Union,
		dwt::Region::CombineMode::Difference, dwt::Region::CombineMode::ExclusiveOr }) {
		dwt::Region actual(dwt::Rectangle(0, 0, 10, 10)), other(dwt::Rectangle(5, 5, 10, 10));
		dwt::Region expected(dwt::Rectangle(0, 0, 10, 10));
		const int result = ::CombineRgn(expected.handle(), expected.handle(), other.handle(), static_cast<int>(mode));
		check(actual.combine(other, mode) == result && ::EqualRgn(actual.handle(), expected.handle()), "Region operation differs from Win32");
		check(::PtInRegion(other.handle(), 14, 14), "Region operation modified source");
	}
	dwt::Region empty(dwt::Rectangle(0, 0, 10, 10));
	check(empty.combine(empty, dwt::Region::CombineMode::Difference) == NULLREGION, "Empty result treated as failure");
	dwt::Region invalid(static_cast<HRGN>(nullptr));
	check(empty.combine(invalid, dwt::Region::CombineMode::Union) == ERROR, "Invalid region not reported");
	dwt::Region rounded(dwt::Rectangle(2, 3, 30, 40), dwt::Point(8, 10));
	dwt::Region reference(::CreateRoundRectRgn(2, 3, 32, 43, 8, 10));
	check(rounded.handle() && ::EqualRgn(rounded.handle(), reference.handle()), "Rounded rectangle bounds changed");
	std::puts("PASS: region operations, empty/error results and rounded rectangles");
}

void lifetimeTests() {
	// Warm up the APIs before comparing process GDI handle counts.
	for(int pass = 0; pass != 2; ++pass) {
		const auto before = ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
		for(int i = 0; i < 300; ++i) {
			dwt::DibBitmap bitmap(nullptr, dwt::Point(8, 8), dwt::DibBitmap::RowOrder::TopDown);
			dwt::IconInfo info(::LoadIcon(nullptr, IDI_APPLICATION));
			dwt::Region region(dwt::Rectangle(0, 0, 8, 8), dwt::Point(2, 2));
			dwt::CompatibleCanvas canvas(nullptr);
			auto selected = canvas.select(bitmap);
			auto saved = canvas.saveState();
			check(bitmap.handle() && info && region.handle() && saved, "Lifetime test allocation failed");
		}
		::GdiFlush();
		if(pass) check(::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS) == before, "GDI handles leaked across repeated scopes");
	}
	std::puts("PASS: 300 repeated scopes leave GDI object count unchanged");
}
}

int dwtMain(dwt::Application&) {
	try {
		stateTests();
		bitmapTests();
		iconTests();
		regionTests();
		lifetimeTests();
		return 0;
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
