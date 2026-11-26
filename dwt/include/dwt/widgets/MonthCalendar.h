/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, Jacek Sieka

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor SmartWin++ nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DWT_MonthCalendar_h
#define DWT_MonthCalendar_h

#include "Control.h"

#include <array>
#include <optional>
#include <utility>

namespace dwt {

class MonthCalendar :
	public CommonControl
{
	typedef CommonControl BaseType;
	friend class WidgetCreator<MonthCalendar>;

public:
	typedef MonthCalendar ThisType;
	typedef ThisType* ObjectType;

	struct Seed : public BaseType::Seed {
		typedef ThisType WidgetType;

		FontPtr font;
		SYSTEMTIME initialDate;
		COLORREF backgroundColor;
		COLORREF monthBackgroundColor;
		COLORREF textColor;
		COLORREF titleBackgroundColor;
		COLORREF titleTextColor;
		COLORREF trailingTextColor;

		Seed();
	};

	void create(const Seed& cs = Seed());

	std::optional<SYSTEMTIME> getValue() const;
	bool setValue(const SYSTEMTIME& value);
	std::optional<std::pair<SYSTEMTIME, SYSTEMTIME>> getSelectionRange() const;
	bool setSelectionRange(const SYSTEMTIME& first, const SYSTEMTIME& last);
	bool setMaxSelectionCount(int days);
	int getMaxSelectionCount() const;
	void setRange(const SYSTEMTIME* minimum, const SYSTEMTIME* maximum);
	void getRange(std::optional<SYSTEMTIME>& minimum,
		std::optional<SYSTEMTIME>& maximum) const;
	std::optional<SYSTEMTIME> getToday() const;
	void setToday(const SYSTEMTIME* today);
	int getMonthRange(DWORD flags, SYSTEMTIME& first, SYSTEMTIME& last) const;
	Rectangle getMinRequiredRect() const;
	bool sizeRectToMin(Rectangle& rect) const;
	int getMaxTodayWidth() const;
	int getCalendarCount() const;
	bool getCalendarGridInfo(MCGRIDINFO& info) const;
	DWORD getCurrentView() const;
	bool setCurrentView(DWORD view);
	int getMonthDelta() const;
	int setMonthDelta(int months);
	COLORREF getColor(int part) const;
	COLORREF setColor(int part, COLORREF color);
	bool getUnicodeFormat() const;
	bool setUnicodeFormat(bool unicode);
	DWORD hitTest(MCHITTESTINFO& info) const;

	void onSelectionChanged(
		std::function<void (const SYSTEMTIME&, const SYSTEMTIME&)> f);
	void onSelection(
		std::function<void (const SYSTEMTIME&, const SYSTEMTIME&)> f);
	void onViewChanged(std::function<void (DWORD, DWORD)> f);
	virtual Point getPreferredSize();

protected:
	void appearanceChanged() override;
	explicit MonthCalendar(Widget* parent);
	virtual ~MonthCalendar() { }

private:
	// Indexed by the six MCSC_* calendar parts. NaC follows the active palette.
	std::array<COLORREF, MCSC_TRAILINGTEXT + 1> explicitColors{NaC, NaC, NaC, NaC, NaC, NaC};
	bool manualColors = false;
	COLORREF partColor(int part) const;
	friend class ChainingDispatcher;
	static const TCHAR windowClass[];
};

inline std::optional<SYSTEMTIME> MonthCalendar::getValue() const {
	SYSTEMTIME value = { };
	return MonthCal_GetCurSel(handle(), &value) ?
		std::optional<SYSTEMTIME>(value) : std::nullopt;
}

inline bool MonthCalendar::setValue(const SYSTEMTIME& value) {
	return MonthCal_SetCurSel(handle(), &value) == TRUE;
}

inline std::optional<std::pair<SYSTEMTIME, SYSTEMTIME>>
MonthCalendar::getSelectionRange() const
{
	SYSTEMTIME range[2] = { };
	return MonthCal_GetSelRange(handle(), range) ?
		std::optional<std::pair<SYSTEMTIME, SYSTEMTIME>>(
			std::make_pair(range[0], range[1])) :
		std::nullopt;
}

inline bool MonthCalendar::setSelectionRange(const SYSTEMTIME& first,
	const SYSTEMTIME& last)
{
	SYSTEMTIME range[2] = { first, last };
	return MonthCal_SetSelRange(handle(), range) == TRUE;
}

inline bool MonthCalendar::setMaxSelectionCount(int days) {
	return MonthCal_SetMaxSelCount(handle(), days) == TRUE;
}

inline int MonthCalendar::getMaxSelectionCount() const {
	return static_cast<int>(sendMessage(MCM_GETMAXSELCOUNT));
}

inline void MonthCalendar::setRange(const SYSTEMTIME* minimum,
	const SYSTEMTIME* maximum)
{
	SYSTEMTIME range[2] = { };
	DWORD flags = 0;
	if(minimum) {
		range[0] = *minimum;
		flags |= GDTR_MIN;
	}
	if(maximum) {
		range[1] = *maximum;
		flags |= GDTR_MAX;
	}
	MonthCal_SetRange(handle(), flags, range);
}

inline void MonthCalendar::getRange(std::optional<SYSTEMTIME>& minimum,
	std::optional<SYSTEMTIME>& maximum) const
{
	SYSTEMTIME range[2] = { };
	auto flags = MonthCal_GetRange(handle(), range);
	minimum = (flags & GDTR_MIN) ? std::optional<SYSTEMTIME>(range[0]) : std::nullopt;
	maximum = (flags & GDTR_MAX) ? std::optional<SYSTEMTIME>(range[1]) : std::nullopt;
}

inline std::optional<SYSTEMTIME> MonthCalendar::getToday() const {
	SYSTEMTIME today = { };
	return MonthCal_GetToday(handle(), &today) ?
		std::optional<SYSTEMTIME>(today) : std::nullopt;
}

inline void MonthCalendar::setToday(const SYSTEMTIME* today) {
	MonthCal_SetToday(handle(), today);
}

inline int MonthCalendar::getMonthRange(DWORD flags, SYSTEMTIME& first,
	SYSTEMTIME& last) const
{
	SYSTEMTIME range[2] = { };
	auto months = MonthCal_GetMonthRange(handle(), flags, range);
	first = range[0];
	last = range[1];
	return months;
}

inline Rectangle MonthCalendar::getMinRequiredRect() const {
	RECT rect = { };
	return MonthCal_GetMinReqRect(handle(), &rect) ?
		Rectangle(rect) : Rectangle();
}

inline bool MonthCalendar::sizeRectToMin(Rectangle& rect) const {
	RECT value = rect;
	if(!sendMessage(MCM_SIZERECTTOMIN, 0, reinterpret_cast<LPARAM>(&value))) {
		return false;
	}
	rect = Rectangle(value);
	return true;
}

inline int MonthCalendar::getMaxTodayWidth() const {
	return static_cast<int>(sendMessage(MCM_GETMAXTODAYWIDTH));
}

inline int MonthCalendar::getCalendarCount() const {
	return static_cast<int>(sendMessage(MCM_GETCALENDARCOUNT));
}

inline bool MonthCalendar::getCalendarGridInfo(MCGRIDINFO& info) const {
	info.cbSize = sizeof(MCGRIDINFO);
	return sendMessage(MCM_GETCALENDARGRIDINFO, 0,
		reinterpret_cast<LPARAM>(&info)) == TRUE;
}

inline DWORD MonthCalendar::getCurrentView() const {
	return static_cast<DWORD>(sendMessage(MCM_GETCURRENTVIEW));
}

inline bool MonthCalendar::setCurrentView(DWORD view) {
	return sendMessage(MCM_SETCURRENTVIEW, 0, view) == TRUE;
}

inline int MonthCalendar::getMonthDelta() const {
	return static_cast<int>(sendMessage(MCM_GETMONTHDELTA));
}

inline int MonthCalendar::setMonthDelta(int months) {
	return static_cast<int>(sendMessage(MCM_SETMONTHDELTA, months));
}

inline COLORREF MonthCalendar::getColor(int part) const {
	return static_cast<COLORREF>(MonthCal_GetColor(handle(), part));
}

inline bool MonthCalendar::getUnicodeFormat() const {
	return sendMessage(MCM_GETUNICODEFORMAT) != 0;
}

inline bool MonthCalendar::setUnicodeFormat(bool unicode) {
	return sendMessage(MCM_SETUNICODEFORMAT, unicode ? TRUE : FALSE) != 0;
}

inline DWORD MonthCalendar::hitTest(MCHITTESTINFO& info) const {
	info.cbSize = sizeof(MCHITTESTINFO);
	return static_cast<DWORD>(sendMessage(MCM_HITTEST, 0,
		reinterpret_cast<LPARAM>(&info)));
}

inline void MonthCalendar::onSelectionChanged(
	std::function<void (const SYSTEMTIME&, const SYSTEMTIME&)> f)
{
	addCallback(Message(WM_NOTIFY, MCN_SELCHANGE),
		[f](const MSG& msg, LRESULT&) -> bool {
			auto data = reinterpret_cast<NMSELCHANGE*>(msg.lParam);
			if(!data) {
				return false;
			}
			f(data->stSelStart, data->stSelEnd);
			return true;
		});
}

inline void MonthCalendar::onSelection(
	std::function<void (const SYSTEMTIME&, const SYSTEMTIME&)> f)
{
	addCallback(Message(WM_NOTIFY, MCN_SELECT),
		[f](const MSG& msg, LRESULT&) -> bool {
			auto data = reinterpret_cast<NMSELCHANGE*>(msg.lParam);
			if(!data) {
				return false;
			}
			f(data->stSelStart, data->stSelEnd);
			return true;
		});
}

inline void MonthCalendar::onViewChanged(std::function<void (DWORD, DWORD)> f) {
	addCallback(Message(WM_NOTIFY, MCN_VIEWCHANGE),
		[f](const MSG& msg, LRESULT&) -> bool {
			auto data = reinterpret_cast<NMVIEWCHANGE*>(msg.lParam);
			if(!data) {
				return false;
			}
			f(data->dwOldView, data->dwNewView);
			return true;
		});
}

inline MonthCalendar::MonthCalendar(Widget* parent) :
	BaseType(parent, ChainingDispatcher::superClass<MonthCalendar>())
{
}

}

#endif
