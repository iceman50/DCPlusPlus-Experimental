# GDI resource and drawing-state helpers

`dwt/include/dwt/resources` contains owned GDI objects. Drawing-state guards
live alongside `Canvas` in `dwt/include/dwt/CanvasClasses.h`.

## DC state

`Canvas::saveState()` or `CanvasState(HDC)` saves the complete DC state. The
guard restores the exact successful save identifier on destruction, including
during exception unwinding. `restore()` supports an earlier restoration boundary
and consumes the guard. A failed save produces an inactive guard; callers that
require restoration before changing clipping should check its boolean value.

The DC and saved GDI resources must outlive the guard. Nested guards must unwind
in reverse order. Do not independently restore a guard's state with Win32 calls.
Move construction transfers ownership; move assignment is prohibited. The
existing object-selection and background-mode guards also prohibit assignment,
which previously discarded their destination's restoration state.

Client-frame painting and statistics plotting restore at their existing
boundaries. The tree control's SaveDC/RestoreDC pair intentionally remains raw:
it spans separate prepaint and postpaint callbacks, so a local guard would
restore too soon.

## DIB storage

`DibBitmap` derives from `Bitmap`, so existing `BitmapPtr` owners and canvas
selection work unchanged. It allocates 32-bit `BI_RGB` storage, with an explicit
`TopDown` or `BottomUp` row order. Width and height must be positive. Invalid
dimensions, size overflow, images larger than a DWORD byte count, or GDI
allocation failure produce a null handle, null pixels and zero size/stride.

Pixel storage is B,G,R,unused/alpha bytes. The wrapper does not initialize pixels
or convert alpha. Callers supplying per-pixel alpha must retain their existing
premultiplication and initialization. Call GdiFlush on the drawing thread before
CPU access after GDI writes. Deselect the bitmap before releasing its last owner.

Menu icons and the splash bitmap remain top-down; taskbar captures and statistics
buffers remain bottom-up. Menu alpha recovery and its flush are unchanged.
Statistics retain their allocation budget, cache and fallback. Splash allocation
failure now skips that draw instead of dereferencing missing pixel storage.

## Icon information and regions

`IconInfo` owns only the bitmap copies returned by GetIconInfo. It borrows the
source icon; the handles exposed by `get()` are valid only while the helper lives.
Its `getSize()` handles monochrome AND/XOR mask height. Existing `Icon::getSize`
and button layout retain their previous size calculations in this refactor;
changing their monochrome behavior is a separate compatibility decision.

`Region::combine` updates the destination with intersection, union, difference,
or exclusive-or. It returns the Win32 region type; `NULLREGION` is a successful
empty result, and `ERROR` is failure. The rounded-rectangle constructor accepts
the full corner-ellipse dimensions and follows CreateRoundRectRgn bounds.
Non-client invalidation uses owned regions while retaining its original
combination and redraw behavior.

## Regression coverage

`test/dwt/GdiResourcesTest.cpp` checks nesting, move construction, explicit
restoration, exception unwinding, clipping, invalid handles, DIB overflow,
byte-for-byte pixels against direct CreateDIBSection allocation in both row
orders, intrusive ownership, icon bitmap cleanup, monochrome metadata, native
region equivalence, and repeated-scope GDI handle counts.

`GdiResourceFailureTest.cpp` injects DIB allocation failure and an incomplete
allocation result, verifies cleanup and empty metadata, checks invalid sizes
never reach GDI, and confirms successful allocation still works afterward.

Existing IconControlsTest checks native-menu transparency and premultiplied
colors for alpha and mask icons. Existing DWT and Win32 integration suites
exercise frame painting, appearance, toolbars and application theme handling.

Validated on 2026-10-27 with MinGW x64:

- SCons Release application build and all 15 automated DWT suites passed.
- CMake Debug application build with PCH and all 20 DWT/Win32 suites passed.
- New ownership, pixel-equivalence and allocation-failure suites passed in both
  configurations. No manual visual inspection was performed.

Build/test logs are in the ignored `build/gdi-wrappers-release.log`,
`build/gdi-wrappers-release-tests.log` and `build/gdi-wrappers-debug-final.log`.

API contracts: [SaveDC](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-savedc),
[CreateDIBSection](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection),
[ICONINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-iconinfo),
and [CombineRgn](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-combinergn).
