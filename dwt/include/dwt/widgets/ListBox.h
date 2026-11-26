#ifndef DWT_LISTBOX_H
#define DWT_LISTBOX_H

#include "Control.h"
#include "../aspects/Collection.h"
#include "../aspects/Selection.h"

namespace dwt {

/// A native, single-selection list of strings. Indices are zero based.
class ListBox : public Control, public aspects::Collection<ListBox, int>,
	public aspects::Selection<ListBox, int> {
	friend class WidgetCreator<ListBox>;
	friend class ChainingDispatcher;
	friend class aspects::Collection<ListBox, int>;
	friend class aspects::Selection<ListBox, int>;

public:
	using ObjectType = ListBox*;
	struct Seed : Control::Seed {
		using WidgetType = ListBox;
		Seed() : Control::Seed(WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY |
			LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE) {}
	};

	int addValue(const tstring& text) {
		return static_cast<int>(sendMessage(LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
	}

	tstring getValue(int index) const {
		const auto length = sendMessage(LB_GETTEXTLEN, index);
		if(length == LB_ERR) return {};
		tstring value(static_cast<size_t>(length) + 1, _T('\0'));
		const auto copied = sendMessage(LB_GETTEXT, index, reinterpret_cast<LPARAM>(&value[0]));
		if(copied == LB_ERR) return {};
		value.resize(static_cast<size_t>(copied));
		return value;
	}

	Point getPreferredSize() override { return scale(Point(140, 90)); }

protected:
	explicit ListBox(Widget* parent) : Control(parent, ChainingDispatcher::superClass<ListBox>()) {}
	void create(const Seed& seed) {
		Control::create(seed);
		setFont(nullptr);
	}

private:
	inline static const TCHAR windowClass[] = _T("LISTBOX");
	static Message getSelectionChangedMessage() { return Message(WM_COMMAND, LBN_SELCHANGE); }
	int getSelectedImpl() const { return static_cast<int>(sendMessage(LB_GETCURSEL)); }
	void setSelectedImpl(int index) { sendMessage(LB_SETCURSEL, index); }
	size_t countSelectedImpl() const { return getSelectedImpl() == LB_ERR ? 0 : 1; }
	void eraseImpl(int index) { sendMessage(LB_DELETESTRING, index); }
	void clearImpl() { sendMessage(LB_RESETCONTENT); }
	size_t sizeImpl() const { return static_cast<size_t>(sendMessage(LB_GETCOUNT)); }
};

} // namespace dwt

#endif
