/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka
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
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <dwt/resources/Region.h>

#include <algorithm>
#include <limits>

#include <dwt/DWTException.h>
#include <dwt/Point.h>

namespace dwt {

Region::Region(HRGN h, bool own) : ResourceType(h, own) { }

Region::Region(const Rectangle& rect) : Region(::CreateRectRgn(rect.left(), rect.top(), rect.right(), rect.bottom()), true) { }

Region::Region(const Rectangle& rect, const Point& ellipseSize) :
	Region(::CreateRoundRectRgn(rect.left(), rect.top(), rect.right(), rect.bottom(), ellipseSize.x, ellipseSize.y), true) { }

int Region::combineInPlace(const Region& other, CombineMode mode) noexcept {
	return ::CombineRgn(handle(), handle(), other.handle(), static_cast<int>(mode));
}

Region::Region(const std::vector<Point>& points, PolyFillMode mode)
{
	if(points.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		throw DWTException("Too many points in polygon region");
	}
	std::vector<POINT> tmp(points.size());
	std::transform(points.begin(), points.end(), tmp.begin(),
		[](const Point& pt) { return pt.toPOINT(); });
	const auto region = tmp.empty() ?
		::CreateRectRgn(0, 0, 0, 0) :
		::CreatePolygonRgn(tmp.data(), static_cast<int>(tmp.size()), mode);
	if(!region) {
		throw Win32Exception("Unable to create polygon region");
	}
	init(region, true);
}

RegionPtr Region::transform(const XFORM* transform) const {
	DWORD bytes = ::GetRegionData(handle(), 0, NULL);
	if(!bytes)
		throw Win32Exception("1st GetRegionData in Region::transform failed");

	std::vector<char> data(bytes);
	if(!::GetRegionData(handle(), bytes, reinterpret_cast<PRGNDATA>(data.data())))
		throw Win32Exception("2nd GetRegionData in Region::transform failed");

	HRGN transformed = ::ExtCreateRegion(
		transform, bytes, reinterpret_cast<PRGNDATA>(data.data()));
	if(!transformed)
		throw Win32Exception("ExtCreateRegion in Region::transform failed");

	return RegionPtr(new Region(transformed));
}

RegionPtr Region::transform(const XFORM& transform) const {
	return this->transform(&transform);
}

int Region::getType() const {
	RECT bounds = { 0 };
	const auto result = ::GetRgnBox(handle(), &bounds);
	if(result == ERROR) {
		throw Win32Exception("Unable to query region bounds");
	}
	return result;
}

Rectangle Region::getBounds() const {
	RECT bounds = { 0 };
	if(::GetRgnBox(handle(), &bounds) == ERROR) {
		throw Win32Exception("Unable to query region bounds");
	}
	return Rectangle(bounds);
}

bool Region::empty() const {
	return getType() == NULLREGION;
}

bool Region::contains(const Point& point) const {
	return ::PtInRegion(handle(), point.x, point.y) != FALSE;
}

bool Region::intersects(const Rectangle& rectangle) const {
	auto bounds = rectangle.normalized().toRECT();
	return ::RectInRegion(handle(), &bounds) != FALSE;
}

bool Region::equals(const Region& other) const {
	return ::EqualRgn(handle(), other.handle()) != FALSE;
}

int Region::offset(const Point& amount) {
	const auto result = ::OffsetRgn(handle(), amount.x, amount.y);
	if(result == ERROR) {
		throw Win32Exception("Unable to offset region");
	}
	return result;
}

RegionPtr Region::combine(const Region& other, CombineMode mode) const {
	auto destination = ::CreateRectRgn(0, 0, 0, 0);
	if(!destination) {
		throw Win32Exception("Unable to create combined region");
	}
	RegionPtr result(new Region(destination));
	if(::CombineRgn(result->handle(), handle(), other.handle(), mode) == ERROR) {
		throw Win32Exception("Unable to combine regions");
	}
	return result;
}

}
