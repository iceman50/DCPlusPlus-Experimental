/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka

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
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <dwt/CanvasClasses.h>
#include <dwt/util/check.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Region.h>

#include <iterator>

namespace dwt {

COLORREF Color::darken(COLORREF color, double factor) {
	if ( factor > 0.0 && factor <= 1.0 )
	{
		BYTE red = GetRValue( color );
		BYTE green = GetGValue( color );
		BYTE blue = GetBValue( color );

		BYTE lightred = ( BYTE )( red - ( factor * red ) );
		BYTE lightgreen = ( BYTE )( green - ( factor * green ) );
		BYTE lightblue = ( BYTE )( blue - ( factor * blue ) );

		color = RGB( lightred, lightgreen, lightblue );
	}
	return color;
}

COLORREF Color::lighten(COLORREF color, double factor) {
	if ( factor > 0.0 && factor <= 1.0 )
	{
		BYTE red = GetRValue( color );
		BYTE green = GetGValue( color );
		BYTE blue = GetBValue( color );

		BYTE lightred = ( BYTE )( ( factor * ( 255 - red ) ) + red );
		BYTE lightgreen = ( BYTE )( ( factor * ( 255 - green ) ) + green );
		BYTE lightblue = ( BYTE )( ( factor * ( 255 - blue ) ) + blue );

		color = RGB( lightred, lightgreen, lightblue );
	}
	return color;
}

COLORREF Color::alphaBlend(COLORREF color, COLORREF factor) {
	float aRed = GetRValue( factor );
	float aGreen = GetGValue( factor );
	float aBlue = GetBValue( factor );
	float red = GetRValue( color );
	float green = GetGValue( color );
	float blue = GetBValue( color );

	BYTE lightred = ( BYTE )( ( aRed / 255.0 ) * red );
	BYTE lightgreen = ( BYTE )( ( aGreen / 255.0 ) * green );
	BYTE lightblue = ( BYTE )( ( aBlue / 255.0 ) * blue );

	color = RGB( lightred, lightgreen, lightblue );
	return color;
}

COLORREF Color::predefined(int index) {
	return ::GetSysColor(index);
}

int Canvas::getDeviceCaps( int nIndex )
{
	return( ::GetDeviceCaps( itsHdc, nIndex ) );
}

Canvas::State::State(Canvas& canvas_) :
	canvas(&canvas_), savedState(::SaveDC(canvas_.handle()))
{
	if(!savedState) {
		canvas = nullptr;
		throw Win32Exception("Unable to save canvas state");
	}
}

Canvas::State::State(State&& other) noexcept :
	canvas(other.canvas), savedState(other.savedState)
{
	other.canvas = nullptr;
	other.savedState = 0;
}

Canvas::State& Canvas::State::operator=(State&& other) noexcept {
	if(this != &other) {
		restore();
		canvas = other.canvas;
		savedState = other.savedState;
		other.canvas = nullptr;
		other.savedState = 0;
	}
	return *this;
}

Canvas::State::~State() {
	restore();
}

void Canvas::State::restore() noexcept {
	if(canvas && savedState) {
		::RestoreDC(canvas->handle(), savedState);
	}
	canvas = nullptr;
	savedState = 0;
}

Canvas::State Canvas::save() {
	return State(*this);
}

Point Canvas::getCurrentPosition() const {
	POINT point = { 0 };
	if(!::GetCurrentPositionEx(handle(), &point)) {
		throw Win32Exception("Unable to retrieve canvas position");
	}
	return Point(point);
}

void Canvas::moveTo( int x, int y )
{
	if ( !::MoveToEx( itsHdc, x, y, 0 ) ) {
		dwtWin32DebugFail("Error in CanvasClasses moveTo");
	}
}

void Canvas::moveTo( const Point & coord )
{
	moveTo( coord.x, coord.y );
}

void Canvas::lineTo( int x, int y )
{
	if ( !::LineTo( itsHdc, x, y ) ) {
		dwtWin32DebugFail("Error in CanvasClasses lineTo");
	}
}

void Canvas::lineTo( const Point & coord ) {
	lineTo( coord.x, coord.y );
}

void Canvas::line( int xStart, int yStart, int xEnd, int yEnd )
{
	moveTo( xStart, yStart );
	lineTo( xEnd, yEnd );
}

void Canvas::line( const Point & start, const Point & end )
{
	moveTo( start.x, start.y );
	lineTo( end.x, end.y );
}

// Draw the outline of a rectangle.
void Canvas::line( const dwt::Rectangle & rect )
{
	moveTo( rect.pos );
	dwt::Point lr { rect.right(), rect.bottom() };
	lineTo( lr.x, rect.pos.y );
	lineTo( lr.x, lr.y );
	lineTo( rect.x(), lr.y );
	lineTo( rect.pos );
}

void Canvas::polygon( const Point points[], unsigned count )
{
	if(!points || count < 2) {
		return;
	}
	if ( !::Polygon( itsHdc, reinterpret_cast< POINT * >( const_cast < Point * >(points) ), count ) ) {
		dwtWin32DebugFail("Error in CanvasClasses polygon");
	}
}

void Canvas::polygon( POINT points[], unsigned count )
{
	if(!points || count < 2) {
		return;
	}
	if ( !::Polygon( itsHdc, points, count ) ) {
		dwtWin32DebugFail("Error in CanvasClasses polygon" );
	}
}

namespace {

std::vector<POINT> toNativePoints(const std::vector<Point>& points) {
	if(points.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		throw DWTException("Too many canvas points");
	}
	std::vector<POINT> result;
	result.reserve(points.size());
	std::transform(points.begin(), points.end(), std::back_inserter(result),
		[](const Point& point) { return point.toPOINT(); });
	return result;
}

}

void Canvas::polyline(const std::vector<Point>& points) {
	if(points.size() < 2) {
		return;
	}
	auto native = toNativePoints(points);
	if(!::Polyline(handle(), native.data(), static_cast<int>(native.size()))) {
		dwtWin32DebugFail("Error in CanvasClasses polyline");
	}
}

void Canvas::polyBezier(const std::vector<Point>& points) {
	if(points.size() < 4) {
		return;
	}
	if((points.size() - 1) % 3 != 0) {
		throw DWTException("Bezier point count must be one plus a multiple of three");
	}
	auto native = toNativePoints(points);
	if(!::PolyBezier(handle(), native.data(), static_cast<DWORD>(native.size()))) {
		dwtWin32DebugFail("Error in CanvasClasses polyBezier");
	}
}

void Canvas::ellipse( int left, int top, int right, int bottom )
{
	if ( ! ::Ellipse( itsHdc, left, top, right, bottom ) ) {
		dwtWin32DebugFail("Error in CanvasClasses ellipse");
	}
}

void Canvas::rectangle( int left, int top, int right, int bottom )
{
	if ( ! ::Rectangle( itsHdc, left, top, right, bottom ) ) {
		dwtWin32DebugFail("Error in CanvasClasses Rectangle");
	}
}

void Canvas::rectangle( const dwt::Rectangle & rect )
{
	rectangle( rect.left(),
			   rect.top(),
			   rect.right(),
			   rect.bottom() );
}

void Canvas::roundRectangle(const dwt::Rectangle& rect, const Point& ellipseSize) {
	if(!::RoundRect(handle(), rect.left(), rect.top(), rect.right(), rect.bottom(),
		ellipseSize.x, ellipseSize.y))
	{
		dwtWin32DebugFail("Error in CanvasClasses roundRectangle");
	}
}

void Canvas::ellipse( const dwt::Rectangle & rect )
{
	if ( ! ::Ellipse( itsHdc, rect.left(), rect.top(), rect.right(), rect.bottom() ) ) {
		dwtWin32DebugFail("Error in CanvasClasses ellipse");
	}
}

void Canvas::getTextMetrics(TEXTMETRIC& tm) {
	::GetTextMetrics(handle(), &tm);
}

Point Canvas::getTextExtent(const tstring& str) {
	SIZE sz = { 0 };
	if(str.empty()) {
		return Point(sz.cx, sz.cy);
	}
	::GetTextExtentPoint32(handle(), str.data(), static_cast<int>(str.size()), &sz);
	return Point(sz.cx, sz.cy);
}

void Canvas::fill(int left, int top, int right, int bottom, const Brush& brush) {
	RECT rc;
	rc.bottom = bottom;
	rc.left = left;
	rc.right = right;
	rc.top = top;

	if(!::FillRect(itsHdc, &rc, brush.handle())) {
		dwtWin32DebugFail("Error in CanvasClasses fill from coordinates");
	}
}

void Canvas::fill(const Rectangle& rect, const Brush& brush) {
	RECT rc = rect;
	if(!::FillRect(itsHdc, &rc, brush.handle())) {
		dwtWin32DebugFail("Error in CanvasClasses fill from rectangle");
	}
}

void Canvas::fill(const Region& region, const Brush& brush) {
	if(!::FillRgn(itsHdc, region, brush)) {
		dwtWin32DebugFail("Error in CanvasClasses fill from region");
	}
}

void Canvas::frame(const Rectangle& rect, const Brush& brush) {
	auto native = rect.toRECT();
	if(!::FrameRect(handle(), &native, brush.handle())) {
		dwtWin32DebugFail("Error in CanvasClasses frame");
	}
}

COLORREF Canvas::setPixel( int x, int y, COLORREF pixcolor )
{
	return ::SetPixel( itsHdc, x, y, pixcolor );
}

COLORREF Canvas::getPixel( int x, int y )
{
	return ::GetPixel( itsHdc, x, y );
}

COLORREF Canvas::getPixel( const Point & coord )
{
	return ::GetPixel( itsHdc, coord.x, coord.y );
}

bool Canvas::extFloodFill( int x, int y, COLORREF color, bool fillTilColorFound )
{
	// Can't check for errors here since if no filling was done (which obviously is no error) it'll also  return 0
	return ::ExtFloodFill( itsHdc, x, y, color, fillTilColorFound ? FLOODFILLBORDER : FLOODFILLSURFACE ) != FALSE;
}

void Canvas::invert(const Region& region) {
	::InvertRgn(handle(), region.handle());
}

void Canvas::invert(const Rectangle& rectangle) {
	auto rect = rectangle.toRECT();
	if(!::InvertRect(handle(), &rect)) {
		dwtWin32DebugFail("Error in CanvasClasses invert rectangle");
	}
}

int Canvas::selectClip(const Region* region) {
	const auto result = ::SelectClipRgn(handle(), region ? region->handle() : nullptr);
	if(result == ERROR) {
		throw Win32Exception("Unable to select canvas clipping region");
	}
	return result;
}

int Canvas::intersectClip(const Rectangle& rectangle) {
	const auto rect = rectangle.normalized();
	const auto result = ::IntersectClipRect(handle(), rect.left(), rect.top(),
		rect.right(), rect.bottom());
	if(result == ERROR) {
		throw Win32Exception("Unable to intersect canvas clipping region");
	}
	return result;
}

int Canvas::excludeClip(const Rectangle& rectangle) {
	const auto rect = rectangle.normalized();
	const auto result = ::ExcludeClipRect(handle(), rect.left(), rect.top(),
		rect.right(), rect.bottom());
	if(result == ERROR) {
		throw Win32Exception("Unable to exclude canvas clipping region");
	}
	return result;
}

Rectangle Canvas::getClipBounds(int* complexity) const {
	RECT rect = { 0 };
	const auto result = ::GetClipBox(handle(), &rect);
	if(result == ERROR) {
		throw Win32Exception("Unable to retrieve canvas clipping bounds");
	}
	if(complexity) {
		*complexity = result;
	}
	return Rectangle(rect);
}

bool Canvas::bitBlt(const Rectangle& destination, const Canvas& source,
	const Point& sourcePosition, DWORD operation)
{
	return ::BitBlt(handle(), destination.x(), destination.y(),
		destination.width(), destination.height(), source.handle(),
		sourcePosition.x, sourcePosition.y, operation) != FALSE;
}

bool Canvas::stretchBlt(const Rectangle& destination, const Canvas& source,
	const Rectangle& sourceRectangle, DWORD operation)
{
	return ::StretchBlt(handle(), destination.x(), destination.y(),
		destination.width(), destination.height(), source.handle(),
		sourceRectangle.x(), sourceRectangle.y(), sourceRectangle.width(),
		sourceRectangle.height(), operation) != FALSE;
}

void Canvas::drawIcon(const IconPtr& icon, const Rectangle& rectangle) {
	if(!::DrawIconEx(itsHdc, rectangle.left(), rectangle.top(), icon->handle(), rectangle.width(), rectangle.height(), 0, 0, DI_NORMAL))
		dwtWin32DebugFail("DrawIconEx failed in Canvas::drawIcon");
}

int Canvas::drawText(const tstring& text, Rectangle& rect, unsigned format) {
	RECT rc = rect;
	int retVal = ::DrawText( itsHdc, text.c_str(), ( int ) text.length(), & rc, format );
	if ( 0 == retVal && !text.empty() )
	{
		dwtWin32DebugFail("Error while trying to draw text to canvas");
	}
	if((format & DT_CALCRECT) == DT_CALCRECT)
		rect = Rectangle(rc);
	return retVal;
}

void Canvas::extTextOut( const tstring & text, unsigned x, unsigned y )
{
	if(text.empty()) {
		return;
	}
	if ( 0 == ::ExtTextOut( itsHdc, x, y, 0, NULL, text.c_str(), ( unsigned ) text.length(), 0 ) ) {
		dwtWin32DebugFail("Error while trying to do TextOut operation");
	}
}

COLORREF Canvas::setTextColor( COLORREF crColor )
{
	return ::SetTextColor( itsHdc, crColor );
}

COLORREF Canvas::setBkColor( COLORREF crColor )
{
	return ::SetBkColor( itsHdc, crColor );
}

Canvas::BkMode::BkMode(Canvas& canvas_, int mode) :
canvas(&canvas_), prevMode(::SetBkMode(canvas->handle(), mode))
{
	if(prevMode == 0) {
		canvas = nullptr;
		throw Win32Exception("Unable to set canvas background mode");
	}
}

Canvas::BkMode::~BkMode() {
	restore();
}

void Canvas::BkMode::restore() noexcept {
	if(canvas && prevMode) {
		::SetBkMode(canvas->handle(), prevMode);
	}
	canvas = nullptr;
	prevMode = 0;
}

Canvas::BkMode Canvas::setBkMode(bool transparent) {
	return BkMode(*this, transparent ? TRANSPARENT : OPAQUE);
}

COLORREF Canvas::getBkColor()
{
	return ::GetBkColor( itsHdc );
}

unsigned Canvas::setTextAlign( unsigned fMode )
{
	return ::SetTextAlign( itsHdc, fMode );
}

BoundCanvas::BoundCanvas(HWND hWnd) :
Canvas(),
itsHandle(hWnd)
{
}

BoundCanvas::BoundCanvas(Widget* widget) :
Canvas(),
itsHandle(widget ? widget->handle() : nullptr)
{
	if(!itsHandle) {
		throw DWTException("Cannot create a canvas for an invalid widget");
	}
}

PaintCanvas::~PaintCanvas()
{
	::EndPaint( itsHandle, & itsPaint );
}

Rectangle PaintCanvas::getPaintRect()
{
	return Rectangle(itsPaint.rcPaint);
}

void PaintCanvas::initialize()
{
	itsHdc = ::BeginPaint( itsHandle, & itsPaint );
	if(!itsHdc) {
		throw Win32Exception("Unable to begin painting");
	}
}

FreeCanvas::FreeCanvas(HDC hdc) :
Canvas()
{
	itsHdc = hdc;
}

CompatibleCanvas::CompatibleCanvas(HDC hdc) :
FreeCanvas(::CreateCompatibleDC(hdc))
{
	if(!itsHdc) {
		throw Win32Exception("Unable to create a compatible canvas");
	}
}

CompatibleCanvas::~CompatibleCanvas() {
	if(itsHdc) {
		::DeleteDC(itsHdc);
	}
}

}
