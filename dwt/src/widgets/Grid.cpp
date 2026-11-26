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

#include <dwt/widgets/Grid.h>

#include <dwt/util/check.h>
#include <dwt/util/HoldResize.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace dwt {

namespace {

size_t saturatedAdd(size_t left, size_t right) {
	const auto maximum = (std::numeric_limits<size_t>::max)();
	return right > maximum - left ? maximum : left + right;
}

size_t saturatedMultiply(size_t left, size_t right) {
	if(left == 0 || right == 0) {
		return 0;
	}
	const auto maximum = (std::numeric_limits<size_t>::max)();
	return left > maximum / right ? maximum : left * right;
}

long boundedLong(size_t value) {
	return static_cast<long>(std::min(
		value, static_cast<size_t>((std::numeric_limits<long>::max)())));
}

/* A grid asks each child for its preferred size once while sizing rows, once while
sizing columns and once more while placing it, and a nested grid answers by asking
its own children the same way. A control N grids deep was measured about 3 * 2^N
times per layout, each measurement reading its text into a device context. Nothing
is resized while one layout or size query runs, so each answer is reused. */
std::unordered_map<Control*, Point>* preferredSizes = nullptr;

class PreferredSizeScope {
public:
	PreferredSizeScope() : owner(!preferredSizes) {
		if(owner) preferredSizes = &sizes;
	}
	~PreferredSizeScope() {
		if(owner) preferredSizes = nullptr;
	}
	PreferredSizeScope(const PreferredSizeScope&) = delete;
	PreferredSizeScope& operator=(const PreferredSizeScope&) = delete;

private:
	const bool owner;
	std::unordered_map<Control*, Point> sizes;
};

Point measurePreferredSize(Control* widget) {
	if(!preferredSizes) {
		return widget->getPreferredSize();
	}
	auto known = preferredSizes->find(widget);
	if(known != preferredSizes->end()) {
		return known->second;
	}
	const auto size = widget->getPreferredSize();
	preferredSizes->emplace(widget, size);
	return size;
}

}

Grid::Seed::Seed(size_t rows_, size_t cols_) :
BaseType::Seed(0, WS_EX_CONTROLPARENT),
rows(rows_),
cols(cols_)
{
}

void Grid::create( const Seed & cs )
{
	BaseType::create(cs);
	rows.resize(cs.rows);
	for(size_t i = 0; i < rows.size(); ++i) {
		rows[i].align = GridInfo::CENTER;	// Default to center for vertical alignment
	}
	columns.resize(cs.cols);
	for(size_t i = 0; i < columns.size(); ++i) {
		columns[i].align = GridInfo::STRETCH;	// Default to stretch for horizontal alignment
	}

	onEnabled([this](bool b) { handleEnabled(b); });
}

Grid::MeasureScope::MeasureScope() : scope(new PreferredSizeScope()) {
}

Grid::MeasureScope::~MeasureScope() {
	delete static_cast<PreferredSizeScope*>(scope);
}

Point Grid::getPreferredSize() {
	// Make sure we have WidgetInfo's for every child...
	auto children = getChildren<Control>();
	std::for_each(children.first, children.second, [this](Control* w) { getWidgetInfo(w); });

	PreferredSizeScope cache;
	std::vector<size_t> rowSize = calcSizes(rows, columns, 0, true, false);
	std::vector<size_t> colSize = calcSizes(columns, rows, 0, false, false);
	size_t width = 0;
	for(auto value: colSize) {
		width = saturatedAdd(width, value);
	}
	size_t height = 0;
	for(auto value: rowSize) {
		height = saturatedAdd(height, value);
	}
	const auto spacingSize = actualSpacing();
	width = saturatedAdd(width, spacingSize.x > 0 ?
		static_cast<size_t>(spacingSize.x) : 0);
	height = saturatedAdd(height, spacingSize.y > 0 ?
		static_cast<size_t>(spacingSize.y) : 0);
	return Point(boundedLong(width), boundedLong(height));
}

Point Grid::getPreferredSize(size_t row, size_t column) const {
	Point ret(0, 0);

	for(auto& i: widgetInfo) {
		if(i.inCell(row, column) && i.w->hasStyle(WS_VISIBLE)) {
			auto preferredSize = measurePreferredSize(i.w);
			// TODO consider fractions...
			if(i.colSpan > 1) {
				auto spanSpacing = saturatedMultiply(i.colSpan - 1, scaledSpacing());
				auto preferred = preferredSize.x > 0 ?
					static_cast<size_t>(preferredSize.x) : 0;
				if(preferred > spanSpacing) {
					preferredSize.x = boundedLong(
						(preferred - spanSpacing + i.colSpan - 1) / i.colSpan);
				} else {
					preferredSize.x = 0;
				}
			}
			if(i.rowSpan > 1) {
				auto spanSpacing = saturatedMultiply(i.rowSpan - 1, scaledSpacing());
				auto preferred = preferredSize.y > 0 ?
					static_cast<size_t>(preferredSize.y) : 0;
				if(preferred > spanSpacing) {
					preferredSize.y = boundedLong(
						(preferred - spanSpacing + i.rowSpan - 1) / i.rowSpan);
				} else {
					preferredSize.y = 0;
				}
			}
			ret.x = std::max(ret.x, preferredSize.x);
			ret.y = std::max(ret.y, preferredSize.y);
		}
	}
	return ret;
}

bool Grid::WidgetInfo::inCell(size_t r, size_t c) const {
	return rowSpan != 0 && colSpan != 0 &&
		r >= row && (r - row) < rowSpan &&
		c >= column && (c - column) < colSpan;
}

GridInfo& Grid::row(size_t i) {
	return rows[i];
}

GridInfo& Grid::column(size_t i) {
	return columns[i];
}

std::vector<size_t> Grid::calcSizes(const GridInfoList& x,
	const GridInfoList& y, size_t cur, bool isRow, bool constrain) const
{
	std::vector<size_t> ret(x.size());

	size_t total = 0;
	size_t fills = 0;
	for(size_t i = 0; i < ret.size(); ++i) {
		ret[i] = std::min(
			x[i].size, static_cast<size_t>((std::numeric_limits<int>::max)()));

		switch(x[i].mode) {
		case GridInfo::STATIC:
			ret[i] = static_cast<size_t>(std::max(
				0, scale(static_cast<int>(ret[i]))));
			break;
		case GridInfo::FILL:
			fills++;
			break;
		case GridInfo::AUTO:
			for(size_t j = 0; j < y.size(); ++j) {
				const auto preferredValue =
					isRow ? getPreferredSize(i, j).y : getPreferredSize(j, i).x;
				auto preferred = preferredValue > 0 ?
					static_cast<size_t>(preferredValue) : 0;
				ret[i] = ret[i] > preferred ? ret[i] : preferred;
			}
			break;
		}
		total = saturatedAdd(total, ret[i]);
	}

	if(total < cur && fills > 0) {
		size_t left = cur - total;
		for(size_t i = 0; i < ret.size(); ++i) {
			if(x[i].mode != GridInfo::FILL)
				continue;
			size_t take = left / fills;
			ret[i] += take;
			left -= take;
			fills--;
		}
	} else if(constrain && total > cur) {
		const auto original = ret;
		size_t assigned = 0;
		for(size_t i = 0; i < ret.size(); ++i) {
			const auto proportional = static_cast<long double>(original[i]) *
				static_cast<long double>(cur) / static_cast<long double>(total);
			ret[i] = static_cast<size_t>(proportional);
			assigned = saturatedAdd(assigned, ret[i]);
		}

		size_t remaining = cur > assigned ? cur - assigned : 0;
		while(remaining > 0) {
			bool assignedAny = false;
			for(size_t i = 0; i < ret.size() && remaining > 0; ++i) {
				if(original[i] != 0) {
					++ret[i];
					--remaining;
					assignedAny = true;
				}
			}
			if(!assignedAny) {
				break;
			}
		}
	}
	return ret;
}

size_t Grid::scaledSpacing() const {
	const auto logical = static_cast<int>(std::min(
		spacing, static_cast<size_t>((std::numeric_limits<int>::max)())));
	return static_cast<size_t>(std::max(0, scale(logical)));
}

Point Grid::actualSpacing() const {
	const auto actual = scaledSpacing();
	return Point(
		columns.empty() ? 0 : boundedLong(
			saturatedMultiply(columns.size() - 1, actual)),
		rows.empty() ? 0 : boundedLong(
			saturatedMultiply(rows.size() - 1, actual)));
}

bool Grid::cellAt(Point point, size_t& rowIndex, size_t& columnIndex, Rectangle& bounds) const {
	const auto size = getClientSize();
	if(point.x < 0 || point.y < 0 || point.x >= size.x || point.y >= size.y) return false;
	const auto gaps = actualSpacing();
	const size_t spacing = scaledSpacing();
	PreferredSizeScope cache;
	auto locate = [spacing](const std::vector<size_t>& sizes, size_t coordinate, size_t& index, long& start, long& length) {
		size_t offset = 0;
		for(size_t i = 0; i < sizes.size(); ++i) {
			if(coordinate < offset) return false;
			if(coordinate - offset < sizes[i]) {
				index = i;
				start = boundedLong(offset);
				length = boundedLong(sizes[i]);
				return true;
			}
			offset = saturatedAdd(offset, saturatedAdd(sizes[i], spacing));
		}
		return false;
	};
	long x, y, width, height;
	const auto widths = calcSizes(columns, rows, std::max(0L, size.x - gaps.x), false, true);
	const auto heights = calcSizes(rows, columns, std::max(0L, size.y - gaps.y), true, true);
	if(!locate(widths, point.x, columnIndex, x, width) || !locate(heights, point.y, rowIndex, y, height)) return false;
	bounds = Rectangle(x, y, width, height);
	return true;
}

void Grid::layout() {
	auto size = getClientSize();
	auto children = getChildren<Control>();

	// Make sure we have WidgetInfo's for every child...
	std::for_each(children.first, children.second, [this](Control* w) { getWidgetInfo(w); });

	Point as = actualSpacing();
	const size_t availableWidth = size.x > as.x ?
		static_cast<size_t>(size.x - as.x) : 0;
	const size_t availableHeight = size.y > as.y ?
		static_cast<size_t>(size.y - as.y) : 0;

	// Children are moved when hr goes away, after the cache: a resized child grid
	// lays out again and must measure its children at their new sizes.
	util::HoldResize hr(this, std::distance(children.first, children.second));
	PreferredSizeScope cache;

	std::vector<size_t> rowSize =
		calcSizes(rows, columns, availableHeight, true, true);
	std::vector<size_t> colSize =
		calcSizes(columns, rows, availableWidth, false, true);
	const auto actual = scaledSpacing();

	for(auto i = children.first; i != children.second; ++i) {
		WidgetInfo* wi = getWidgetInfo(*i);
		if(!wi || wi->noResize) {
			continue;
		}

		size_t r = wi->row;
		size_t rs = wi->rowSpan;

		size_t c = wi->column;
		size_t cs = wi->colSpan;

		if(rs == 0 || cs == 0 || r >= rowSize.size() ||
			c >= colSize.size() || rs > rowSize.size() - r ||
			cs > colSize.size() - c)
		{
			continue;
		}

		size_t x = std::accumulate(colSize.begin(), colSize.begin() + c, static_cast<size_t>(0));
		x = saturatedAdd(x, saturatedMultiply(c, actual));

		size_t y = std::accumulate(rowSize.begin(), rowSize.begin() + r, static_cast<size_t>(0));
		y = saturatedAdd(y, saturatedMultiply(r, actual));

		size_t w = std::accumulate(colSize.begin() + c, colSize.begin() + c + cs, static_cast<size_t>(0));
		w = saturatedAdd(w, saturatedMultiply(cs - 1, actual));

		size_t h = std::accumulate(rowSize.begin() + r, rowSize.begin() + r + rs, static_cast<size_t>(0));
		h = saturatedAdd(h, saturatedMultiply(rs - 1, actual));

		Point ps = measurePreferredSize(wi->w);
		const size_t preferredWidth = ps.x > 0 ?
			static_cast<size_t>(ps.x) : 0;
		const size_t preferredHeight = ps.y > 0 ?
			static_cast<size_t>(ps.y) : 0;
		const size_t alignedWidth = std::min(w, preferredWidth);
		const size_t alignedHeight = std::min(h, preferredHeight);

		switch(columns[wi->column].align) {
		case GridInfo::TOP_LEFT: w = alignedWidth; break;
		case GridInfo::BOTTOM_RIGHT: x += w - alignedWidth; w = alignedWidth; break;
		case GridInfo::CENTER: x += (w - alignedWidth) / 2; w = alignedWidth; break;
		case GridInfo::STRETCH: break; // Do nothing
		}

		switch(rows[wi->row].align) {
		case GridInfo::TOP_LEFT: h = alignedHeight; break;
		case GridInfo::BOTTOM_RIGHT: y += h - alignedHeight; h = alignedHeight; break;
		case GridInfo::CENTER: y += (h - alignedHeight) / 2; h = alignedHeight; break;
		case GridInfo::STRETCH: break; // Do nothing
		}

		hr.resize(wi->w, Rectangle(
			boundedLong(x),
			boundedLong(y),
			boundedLong(w),
			boundedLong(h)));
	}
}

Grid::WidgetInfo* Grid::getWidgetInfo(Control* w) {
	for(auto& i: widgetInfo) {
		if(i.w == w) {
			return &i;
		}
	}

	if(columns.empty() || rows.empty()) {
		return nullptr;
	}

	const size_t maximumCells = rows.size() >
		(std::numeric_limits<size_t>::max)() / columns.size() ?
		(std::numeric_limits<size_t>::max)() :
		rows.size() * columns.size();
	size_t pos = 0;
	for(; pos < maximumCells; ++pos) {
		bool taken = false;
		const size_t r = pos / columns.size();
		const size_t c = pos % columns.size();
		for(auto& i: widgetInfo) {
			if(i.inCell(r, c)) {
				taken = true;
				break;
			}
		}
		if(!taken) {
			break;
		}
	}

	if(pos >= maximumCells) {
		return nullptr;
	}
	size_t r = pos / columns.size();
	size_t c = pos % columns.size();

	if(r >= rows.size()) {
		return 0;
	}

	widgetInfo.emplace_back(w, r, c, 1, 1);
	return &widgetInfo.back();
}

size_t Grid::addRow(const GridInfo& gp) {
	rows.push_back(gp);
	raiseAccessibleStructureChanged();
	return rows.size() - 1;
}

size_t Grid::addColumn(const GridInfo& gp) {
	columns.push_back(gp);
	raiseAccessibleStructureChanged();
	return columns.size() - 1;
}

void Grid::removeRow(Control* w) {
	for(auto& i: widgetInfo) {
		if(i.w == w) {
			removeRow(i.row);
			break;
		}
	}
}

void Grid::removeColumn(Control* w) {
	for(auto& i: widgetInfo) {
		if(i.w == w) {
			removeColumn(i.column);
			break;
		}
	}
}

void Grid::removeRow(size_t row) {
	if(row >= rows.size()) {
		return;
	}
	rows.erase(rows.begin() + row);

	for(auto i = widgetInfo.begin(); i != widgetInfo.end();) {

		if(i->row == row) {
			auto w = i->w;
			i = widgetInfo.erase(i);
			w->close();

		} else {
			if(i->row > row) {
				--i->row;
			}

			++i;
		}
	}
	raiseAccessibleStructureChanged();
}

void Grid::removeColumn(size_t column) {
	if(column >= columns.size()) {
		return;
	}
	columns.erase(columns.begin() + column);

	for(auto i = widgetInfo.begin(); i != widgetInfo.end();) {

		if(i->column == column) {
			auto w = i->w;
			i = widgetInfo.erase(i);
			w->close();

		} else {
			if(i->column > column) {
				--i->column;
			}

			++i;
		}
	}
	raiseAccessibleStructureChanged();
}

void Grid::clearRows() {
	rows.clear();
	raiseAccessibleStructureChanged();
}

void Grid::clearColumns() {
	columns.clear();
	raiseAccessibleStructureChanged();
}

void Grid::setWidget(Control* w, size_t row, size_t column, size_t rowSpan, size_t colSpan) {
	if(!w) {
		return;
	}
	dwtassert(w->getParent() == this, "Control must be a child of the grid");
	if(w->getParent() != this) {
		return;
	}
	rowSpan = std::max<size_t>(1, rowSpan);
	colSpan = std::max<size_t>(1, colSpan);
	if(row < rows.size()) {
		rowSpan = std::min(rowSpan, rows.size() - row);
	}
	if(column < columns.size()) {
		colSpan = std::min(colSpan, columns.size() - column);
	}

	for(auto& i: widgetInfo) {
		if(i.w == w) {
			i.row = row;
			i.column = column;
			i.rowSpan = rowSpan;
			i.colSpan = colSpan;
			raiseAccessibleStructureChanged();
			return;
		}
	}

	widgetInfo.push_back(WidgetInfo(w, row, column, rowSpan, colSpan));
	raiseAccessibleStructureChanged();
}

void Grid::setWidget(Control* w) {
	for(auto& i: widgetInfo) {
		if(i.w == w) {
			i.noResize = true;
			return;
		}
	}

	widgetInfo.push_back(WidgetInfo(w));
}

void Grid::handleEnabled(bool enabled) {
	// TODO find better way of keeping track of children
	for(HWND wnd = ::FindWindowEx(handle(), NULL, NULL, NULL); wnd; wnd = ::FindWindowEx(handle(), wnd, NULL, NULL)) {
		::EnableWindow(wnd, enabled ? TRUE : FALSE);
	}
}

bool Grid::handleMessage(const MSG &msg, LRESULT &retVal) {
	if(msg.message == WM_PARENTNOTIFY) {
		if(LOWORD(msg.wParam) == WM_DESTROY) {
			auto wnd = (HWND)msg.lParam;
			if(::GetParent(wnd) == handle()) {
				for(auto i = widgetInfo.begin(); i != widgetInfo.end(); ++i) {
					if(i->w->handle() == wnd) {
						widgetInfo.erase(i);
						break;
					}
				}
			}
		}
	}

	return BaseType::handleMessage(msg, retVal);
}

}
