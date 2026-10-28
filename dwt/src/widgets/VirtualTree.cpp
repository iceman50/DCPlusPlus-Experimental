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

#include <dwt/widgets/VirtualTree.h>

#include <algorithm>

#include <dwt/util/check.h>

namespace dwt {

VirtualTree::Seed::Seed(const BaseType::Seed& seed) :
	BaseType::Seed(seed)
{
}

VirtualTree::VirtualTree(Widget* parent) :
	BaseType(parent),
	selected(nullptr),
	suppressSelectionSync(0),
	virtualMultiSelect(false)
{
	addRoot();
	configureAccessibility();
}

void VirtualTree::create(const Seed& seed) {
	virtualMultiSelect = (seed.tvExStyle & TVS_EX_MULTISELECT) != 0;
	BaseType::create(seed);
}

bool VirtualTree::handleMessage(const MSG& msg, LRESULT& retVal) {
	switch(msg.message) {
	case TVM_CREATEDRAGIMAGE:
		{
			auto item = reinterpret_cast<Item*>(msg.lParam);
			if(!validate(item)) {
				retVal = 0;
				return true;
			}
			display(*item);
			retVal = sendTreeMsg(TVM_CREATEDRAGIMAGE, 0,
				reinterpret_cast<LPARAM>(item->handle));
			return true;
		}
	case TVM_DELETEITEM:
		{
			retVal = handleDelete(msg.lParam);
			return true;
		}
	case TVM_ENSUREVISIBLE:
		{
			retVal = handleEnsureVisible(reinterpret_cast<Item*>(msg.lParam));
			return true;
		}
	case TVM_EDITLABEL:
		{
			auto item = reinterpret_cast<Item*>(msg.lParam);
			if(!validate(item)) {
				retVal = 0;
				return true;
			}
			display(*item);
			retVal = sendTreeMsg(TVM_EDITLABEL, 0,
				reinterpret_cast<LPARAM>(item->handle));
			return true;
		}
	case TVM_EXPAND:
		{
			retVal = handleExpand(msg.wParam, reinterpret_cast<Item*>(msg.lParam));
			return true;
		}
	case TVM_GETCOUNT:
		{
			retVal = items.size() - 1; // don't count the fake root
			return true;
		}
	case TVM_GETITEM:
		{
			retVal = handleGetItem(*reinterpret_cast<TVITEMEX*>(msg.lParam));
			return true;
		}
	case TVM_GETITEMRECT:
		{
			auto rect = reinterpret_cast<RECT*>(msg.lParam);
			auto item = rect ?
				reinterpret_cast<Item*>(*reinterpret_cast<HTREEITEM*>(rect)) :
				nullptr;
			if(!validate(item)) {
				retVal = FALSE;
				return true;
			}
			display(*item);
			RECT native = { 0 };
			*reinterpret_cast<HTREEITEM*>(&native) = item->handle;
			retVal = sendTreeMsg(TVM_GETITEMRECT, msg.wParam,
				reinterpret_cast<LPARAM>(&native));
			if(retVal) {
				*rect = native;
			}
			return true;
		}
	case TVM_GETITEMPARTRECT:
		{
			auto info = reinterpret_cast<TVGETITEMPARTRECTINFO*>(msg.lParam);
			auto item = info ? reinterpret_cast<Item*>(info->hti) : nullptr;
			if(!validate(item)) {
				retVal = FALSE;
				return true;
			}
			display(*item);
			auto native = *info;
			native.hti = item->handle;
			retVal = sendTreeMsg(TVM_GETITEMPARTRECT, 0,
				reinterpret_cast<LPARAM>(&native));
			return true;
		}
	case TVM_GETITEMSTATE:
		{
			retVal = handleGetItemState(static_cast<UINT>(msg.lParam), reinterpret_cast<Item*>(msg.wParam));
			return true;
		}
	case TVM_GETSELECTEDCOUNT:
		{
			retVal = selectedCount();
			return true;
		}
	case TVM_GETNEXTITEM:
		{
			auto ret = handleGetNextItem(msg.wParam, reinterpret_cast<Item*>(msg.lParam));
			retVal = ret == root ? 0 : reinterpret_cast<LRESULT>(ret);
			return true;
		}
	case TVM_HITTEST:
		{
			retVal = reinterpret_cast<LRESULT>(find(reinterpret_cast<HTREEITEM>(sendTreeMsg(TVM_HITTEST, 0, msg.lParam))));
			reinterpret_cast<TVHITTESTINFO*>(msg.lParam)->hItem = reinterpret_cast<HTREEITEM>(retVal);
			return true;
		}
	case TVM_INSERTITEM:
		{
			retVal = reinterpret_cast<LRESULT>(handleInsert(*reinterpret_cast<TVINSERTSTRUCT*>(msg.lParam)));
			return true;
		}
	case TVM_MAPACCIDTOHTREEITEM:
		{
			auto handle = reinterpret_cast<HTREEITEM>(
				sendTreeMsg(TVM_MAPACCIDTOHTREEITEM, msg.wParam, 0));
			auto item = find(handle);
			retVal = reinterpret_cast<LRESULT>(
				item ? item->ptr() : HTREEITEM());
			return true;
		}
	case TVM_MAPHTREEITEMTOACCID:
		{
			auto item = reinterpret_cast<Item*>(msg.wParam);
			if(!validate(item)) {
				retVal = 0;
				return true;
			}
			display(*item);
			retVal = sendTreeMsg(TVM_MAPHTREEITEMTOACCID,
				reinterpret_cast<WPARAM>(item->handle), 0);
			return true;
		}
	case TVM_SELECTITEM:
		{
			retVal = handleSelect(msg.wParam, reinterpret_cast<Item*>(msg.lParam));
			return true;
		}
	case TVM_SETINSERTMARK:
		{
			auto item = reinterpret_cast<Item*>(msg.lParam);
			if(item && !validate(item)) {
				retVal = FALSE;
				return true;
			}
			if(item) {
				display(*item);
			}
			retVal = sendTreeMsg(TVM_SETINSERTMARK, msg.wParam,
				reinterpret_cast<LPARAM>(item ? item->handle : nullptr));
			return true;
		}
	case TVM_SETITEM:
		{
			retVal = handleSetItem(*reinterpret_cast<TVITEMEX*>(msg.lParam));
			return true;
		}
	case TVM_SORTCHILDREN:
		{
			auto item = reinterpret_cast<Item*>(msg.lParam);
			if(!item || reinterpret_cast<HTREEITEM>(item) == TVI_ROOT) {
				item = root;
			}
			retVal = handleSortChildren(item, msg.wParam != FALSE);
			return true;
		}
	case WM_NOTIFY:
		{
			auto code = reinterpret_cast<NMHDR*>(msg.lParam)->code;
			switch(code) {
			case TVN_ITEMEXPANDED:
				{
					handleExpanded(*reinterpret_cast<NMTREEVIEW*>(msg.lParam));
					break;
				}
			case TVN_ITEMEXPANDING:
				{
					handleExpanding(*reinterpret_cast<NMTREEVIEW*>(msg.lParam));
					break;
				}
			case TVN_SELCHANGED:
				{
					if(!suppressSelectionSync) {
						handleSelected(find(reinterpret_cast<NMTREEVIEW*>(msg.lParam)->itemNew.hItem));
					}
					break;
				}
			case TVN_ITEMCHANGED:
				{
					handleItemChanged(*reinterpret_cast<NMTVITEMCHANGE*>(msg.lParam));
					break;
				}
			}

			auto virtualHandle = [this](HTREEITEM handle) {
				auto item = find(handle);
				return item ? item->ptr() : HTREEITEM();
			};
			switch(code) {
			case TVN_SELCHANGING:
			case TVN_SELCHANGED:
			case TVN_ITEMEXPANDING:
			case TVN_ITEMEXPANDED:
			case TVN_BEGINDRAG:
			case TVN_BEGINRDRAG:
			case TVN_DELETEITEM:
				{
					auto data = *reinterpret_cast<NMTREEVIEW*>(msg.lParam);
					data.itemOld.hItem = virtualHandle(data.itemOld.hItem);
					data.itemNew.hItem = virtualHandle(data.itemNew.hItem);
					auto translated = msg;
					translated.lParam = reinterpret_cast<LPARAM>(&data);
					return BaseType::handleMessage(translated, retVal);
				}
			case TVN_GETDISPINFO:
			case TVN_SETDISPINFO:
			case TVN_BEGINLABELEDIT:
			case TVN_ENDLABELEDIT:
				{
					auto original =
						reinterpret_cast<NMTVDISPINFO*>(msg.lParam);
					auto actualHandle = original->item.hItem;
					auto data = *original;
					data.item.hItem = virtualHandle(data.item.hItem);
					auto translated = msg;
					translated.lParam = reinterpret_cast<LPARAM>(&data);
					auto handled = BaseType::handleMessage(translated, retVal);
					original->item = data.item;
					original->item.hItem = actualHandle;
					return handled;
				}
			case TVN_GETINFOTIP:
				{
					auto data = *reinterpret_cast<NMTVGETINFOTIP*>(msg.lParam);
					data.hItem = virtualHandle(data.hItem);
					auto translated = msg;
					translated.lParam = reinterpret_cast<LPARAM>(&data);
					return BaseType::handleMessage(translated, retVal);
				}
			case TVN_ITEMCHANGING:
			case TVN_ITEMCHANGED:
				{
					auto data = *reinterpret_cast<NMTVITEMCHANGE*>(msg.lParam);
					data.hItem = virtualHandle(data.hItem);
					auto translated = msg;
					translated.lParam = reinterpret_cast<LPARAM>(&data);
					return BaseType::handleMessage(translated, retVal);
				}
			case TVN_ASYNCDRAW:
				{
					auto original = reinterpret_cast<NMTVASYNCDRAW*>(msg.lParam);
					auto data = *original;
					data.hItem = virtualHandle(data.hItem);
					auto translated = msg;
					translated.lParam = reinterpret_cast<LPARAM>(&data);
					auto handled = BaseType::handleMessage(translated, retVal);
					original->dwRetFlags = data.dwRetFlags;
					original->iRetImageIndex = data.iRetImageIndex;
					return handled;
				}
			default:
				break;
			}
			break;
		}
	}
	return BaseType::handleMessage(msg, retVal);
}

HTREEITEM VirtualTree::insert(const tstring& text, HTREEITEM parent, HTREEITEM insertAfter,
	LPARAM param, bool expanded, int iconIndex, int selectedIconIndex)
{
	TVINSERTSTRUCT item = { parent, insertAfter, { { TVIF_TEXT } } };
	auto& value = item.itemex;
	value.pszText = const_cast<TCHAR*>(text.c_str());
	if(param) {
		value.mask |= TVIF_PARAM;
		value.lParam = param;
	}
	if(expanded) {
		value.mask |= TVIF_STATE;
		value.state = TVIS_EXPANDED;
		value.stateMask = TVIS_EXPANDED;
	}
	value.mask |= TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	value.iImage = iconIndex;
	value.iSelectedImage = selectedIconIndex == -1 ? iconIndex : selectedIconIndex;
	return insert(item);
}

HTREEITEM VirtualTree::insert(TVINSERTSTRUCT& tvis) {
	return handleInsert(tvis)->ptr();
}

bool VirtualTree::isExpanded(HTREEITEM item) const {
	auto value = reinterpret_cast<Item*>(item);
	return validate(value) && value->expanded();
}

void VirtualTree::expand(HTREEITEM item) {
	handleExpand(TVE_EXPAND, reinterpret_cast<Item*>(item));
}

void VirtualTree::collapse(HTREEITEM item) {
	handleExpand(TVE_COLLAPSE, reinterpret_cast<Item*>(item));
}

void VirtualTree::setMultiSelect(bool value) {
	virtualMultiSelect = value;
	BaseType::setMultiSelect(value);
	if(!value && selected) {
		clearSelection(selected);
	}
}

Tree::CheckState VirtualTree::getCheckState(HTREEITEM item) const {
	auto value = reinterpret_cast<Item*>(item);
	return validate(value) ?
		static_cast<CheckState>((value->state & TVIS_STATEIMAGEMASK) >> 12) :
		NoCheckBox;
}

void VirtualTree::setCheckState(HTREEITEM item, CheckState state) {
	TVITEMEX value = { TVIF_HANDLE | TVIF_STATE, item };
	value.stateMask = TVIS_STATEIMAGEMASK;
	value.state = INDEXTOSTATEIMAGEMASK(static_cast<UINT>(state));
	handleSetItem(value);
}

HTREEITEM VirtualTree::getSelected() const {
	return selected ? selected->ptr() : HTREEITEM();
}

void VirtualTree::setSelected(HTREEITEM item) {
	handleSelect(TVGN_CARET, reinterpret_cast<Item*>(item));
}

size_t VirtualTree::countSelected() const {
	return selectedCount();
}

bool VirtualTree::getItemSelected(HTREEITEM item) const {
	auto value = reinterpret_cast<Item*>(item);
	return validate(value) && value->selected();
}

void VirtualTree::setItemSelected(HTREEITEM item, bool value) {
	auto selectedItem = reinterpret_cast<Item*>(item);
	if(!validate(selectedItem)) {
		return;
	}
	if(value && !multiSelect()) {
		clearSelection(selectedItem);
	}
	setSelectedState(selectedItem, value);
	if(value && !selected) {
		selected = selectedItem;
	}
}

std::vector<HTREEITEM> VirtualTree::getSelectedItems() const {
	std::vector<HTREEITEM> result;
	for(auto item = firstSelected(); item; item = nextSelected(item)) {
		result.push_back(item->ptr());
	}
	return result;
}

// root node
VirtualTree::Item::Item() :
	handle(nullptr),
	parent(nullptr),
	prev(nullptr),
	next(nullptr),
	firstChild(nullptr),
	lastChild(nullptr),
	state(TVIS_EXPANDED),
	image(0),
	selectedImage(0),
	lParam(0)
{
}

VirtualTree::Item::Item(TVINSERTSTRUCT& tvis) :
	handle(nullptr),
	parent(reinterpret_cast<Item*>(tvis.hParent)),
	prev(nullptr),
	next(nullptr),
	firstChild(nullptr),
	lastChild(nullptr),
	state(tvis.itemex.state & tvis.itemex.stateMask),
	image(tvis.itemex.iImage),
	selectedImage(tvis.itemex.iSelectedImage),
	lParam(tvis.itemex.lParam)
{
	if(tvis.hInsertAfter == TVI_FIRST) {
		insertBefore(parent->firstChild);
	} else if(tvis.hInsertAfter == TVI_LAST) {
		insertAfter(parent->lastChild);
	} else if(tvis.hInsertAfter == TVI_SORT) {
		dwtDebugFail("VirtualTree TVI_SORT not implemented");
	} else {
		insertAfter(reinterpret_cast<Item*>(tvis.hInsertAfter));
	}
	setText(tvis.itemex.pszText);
}

HTREEITEM VirtualTree::Item::ptr() const {
	return reinterpret_cast<HTREEITEM>(const_cast<Item*>(this));
}

size_t VirtualTree::Item::Hash::operator()(const Item& item) const {
	return reinterpret_cast<size_t>(&item) / sizeof(Item);
}

bool VirtualTree::Item::Equal::operator()(const Item& a, const Item& b) const {
	return &a == &b;
}

void VirtualTree::Item::insertBefore(Item* sibling) {
	if(sibling) {
		if(sibling->prev) {
			sibling->prev->next = this;
		}
		prev = sibling->prev;
		sibling->prev = this;
	}
	next = sibling;
	if(parent->firstChild == sibling) {
		parent->firstChild = this;
	}
	if(!parent->lastChild) {
		parent->lastChild = this;
	}
}

void VirtualTree::Item::insertAfter(Item* sibling) {
	if(sibling) {
		if(sibling->next) {
			sibling->next->prev = this;
		}
		next = sibling->next;
		sibling->next = this;
	}
	prev = sibling;
	if(parent->lastChild == sibling) {
		parent->lastChild = this;
	}
	if(!parent->firstChild) {
		parent->firstChild = this;
	}
}

void VirtualTree::Item::setText(LPTSTR newText) {
	if(newText && newText != LPSTR_TEXTCALLBACK) {
		this->text = newText;
	} else {
		this->text.reset();
	}
}

bool VirtualTree::Item::expanded() const {
	return state & TVIS_EXPANDED;
}

bool VirtualTree::Item::selected() const {
	return (state & TVIS_SELECTED) != 0;
}

VirtualTree::Item* VirtualTree::Item::lastExpandedChild() const {
	return lastChild && expanded() ? lastChild->lastExpandedChild() : const_cast<Item*>(this);
}

VirtualTree::Item* VirtualTree::Item::prevVisible() const {
	return prev ? prev->lastExpandedChild() : parent;
}

VirtualTree::Item* VirtualTree::Item::nextVisible() const {
	if(firstChild && expanded()) { return firstChild; }
	if(next) { return next; }
	auto item = this;
	while(item->parent) {
		item = item->parent;
		if(item->next) {
			return item->next;
		}
	}
	return nullptr;
}

VirtualTree::Item* VirtualTree::Item::nextItem() const {
	if(firstChild) { return firstChild; }
	if(next) { return next; }
	auto item = this;
	while(item->parent) {
		item = item->parent;
		if(item->next) {
			return item->next;
		}
	}
	return nullptr;
}

bool VirtualTree::handleDelete(LPARAM lParam) {
	if(!lParam || lParam == reinterpret_cast<LPARAM>(TVI_ROOT)) {
		// delete all items.
		beginSelectionSync();
		bool ret = sendTreeMsg(TVM_DELETEITEM, 0, lParam);
		endSelectionSync();
		items.clear();
		selected = nullptr;
		addRoot();
		raiseAccessibleStructureChanged();
		return ret;
	}

	// delete one item.
	auto item = reinterpret_cast<Item*>(lParam);
	if(!validate(item)) { return false; }
	hide(*item);
	remove(item);
	raiseAccessibleStructureChanged();
	return true;
}

bool VirtualTree::handleEnsureVisible(Item* item) {
	if(!validate(item)) { return false; }
	display(*item);
	return sendTreeMsg(TVM_ENSUREVISIBLE, 0, reinterpret_cast<LPARAM>(item->handle));
}

bool VirtualTree::handleExpand(WPARAM code, Item* item) {
	if(!validate(item)) { return false; }
	switch(code) {
	case TVE_COLLAPSE: if(item->expanded()) { item->state &= ~TVIS_EXPANDED; } break;
	case TVE_EXPAND: if(!item->expanded()) { item->state |= TVIS_EXPANDED; } break;
	default: dwtDebugFail("VirtualTree expand code not implemented"); break;
	}
	if(item->handle) {
		sendTreeMsg(TVM_EXPAND, code, reinterpret_cast<LPARAM>(item->handle));
	} else {
		raiseAccessibleStructureChanged();
	}
	return true;
}

void VirtualTree::handleExpanded(NMTREEVIEW& data) {
	auto item = find(data.itemNew.hItem);
	if(!validate(item)) { return; }
	switch(data.action) {
	case TVE_COLLAPSE:
		{
			if(item->expanded()) { item->state &= ~TVIS_EXPANDED; }
			// remove children of this item when collapsing it.
			auto child = item->firstChild;
			while(child) {
				hide(*child);
				child = child->next;
			}
			break;
		}
	case TVE_EXPAND:
		{
			if(!item->expanded()) { item->state |= TVIS_EXPANDED; }
			break;
		}
	default: dwtDebugFail("VirtualTree expand code not implemented"); break;
	}
	raiseAccessibleStructureChanged();
}

void VirtualTree::handleExpanding(NMTREEVIEW& data) {
	auto item = find(data.itemNew.hItem);
	if(!validate(item)) { return; }
	switch(data.action) {
	case TVE_COLLAPSE:
		{
			break;
		}
	case TVE_EXPAND:
		{
			// add direct children of this item before expanding it.
			auto child = item->firstChild;
			while(child) {
				display(*child);
				child = child->next;
			}
			break;
		}
	default: dwtDebugFail("VirtualTree expand code not implemented"); break;
	}
}

bool VirtualTree::handleGetItem(TVITEMEX& tv) {
	auto item = reinterpret_cast<Item*>(tv.hItem);
	if(!validate(item)) { return false; }
	if(tv.mask & TVIF_CHILDREN) { tv.cChildren = item->firstChild ? 1 : 0; }
	if(tv.mask & TVIF_HANDLE) { tv.hItem = item->ptr(); }
	if(tv.mask & TVIF_IMAGE) { tv.iImage = item->image; }
	tv.lParam = item->lParam; // doesn't depend on TVIF_PARAM
	if(tv.mask & TVIF_SELECTEDIMAGE) { tv.iSelectedImage = item->selectedImage; }
	tv.state = item->state; // doesn't depend on TVIF_STATE nor on stateMask
	if(tv.mask & TVIF_TEXT && item->text) { item->text->copy(tv.pszText, tv.cchTextMax); }
	return true;
}

UINT VirtualTree::handleGetItemState(UINT mask, Item* item) {
	if(!validate(item)) { return 0; }
	return item->state & mask;
}

VirtualTree::Item* VirtualTree::handleGetNextItem(WPARAM code, Item* item) {
	if(item && !validate(item)) { return nullptr; }
	switch(code) {
	case TVGN_CARET: return selected;
	case TVGN_CHILD: return (item ? item : root)->firstChild;
	case TVGN_DROPHILITE: return nullptr;
	case TVGN_FIRSTVISIBLE: return root->firstChild;
	case TVGN_LASTVISIBLE: return root->lastExpandedChild();
	case TVGN_NEXT: return item ? item->next : nullptr;
	case TVGN_NEXTSELECTED: return item ? nextSelected(item) : firstSelected();
	case TVGN_NEXTVISIBLE: return item ? item->nextVisible() : nullptr;
	case TVGN_PARENT: return item ? item->parent : nullptr;
	case TVGN_PREVIOUS: return item ? item->prev : nullptr;
	case TVGN_PREVIOUSVISIBLE: return item ? item->prevVisible() : nullptr;
	case TVGN_ROOT: return root->firstChild;
	default: return nullptr;
	}
}

VirtualTree::Item* VirtualTree::handleInsert(TVINSERTSTRUCT& tvis) {
	if(tvis.hParent == TVI_ROOT || !tvis.hParent) {
		tvis.hParent = root->ptr();
	}
	auto& item = const_cast<Item&>(*items.emplace(tvis).first);
	if(item.parent->firstChild == item.parent->lastChild) {
		updateChildDisplay(item.parent);
	}
	if(item.parent->expanded()) {
		display(item);
	}
	raiseAccessibleStructureChanged();
	return &item;
}

void VirtualTree::handleItemChanged(const NMTVITEMCHANGE& data) {
	if(suppressSelectionSync) {
		return;
	}

	auto item = find(data.hItem);
	if(!validate(item)) {
		return;
	}

	const auto changedState = data.uStateNew ^ data.uStateOld;
	if(changedState) {
		item->state &= ~changedState;
		item->state |= data.uStateNew & changedState;
	}

	if(changedState & TVIS_SELECTED) {
		if(item->selected()) {
			if(!multiSelect()) {
				clearSelection(item);
			}
			if(!selected) {
				selected = item;
			}
			raiseAccessibleItemEvent(
				reinterpret_cast<accessibility::ItemId>(item), 20012);
		} else if(selected == item) {
			selected = firstSelected();
		}
	}
}

bool VirtualTree::handleSelect(WPARAM code, Item* item) {
	if(code != TVGN_CARET) {
		if(!validate(item)) { return sendTreeMsg(TVM_SELECTITEM, code, 0); }
		display(*item);
		return sendTreeMsg(TVM_SELECTITEM, code, reinterpret_cast<LPARAM>(item->handle));
	}

	if(!validate(item)) {
		clearSelection();
		selected = nullptr;
		beginSelectionSync();
		auto result = sendTreeMsg(TVM_SELECTITEM, code, 0);
		endSelectionSync();
		return result != FALSE;
	}

	clearSelection(item);
	setSelectedState(item, true);
	selected = item;
	display(*item);
	beginSelectionSync();
	auto result = sendTreeMsg(TVM_SELECTITEM, code, reinterpret_cast<LPARAM>(item->handle));
	endSelectionSync();
	raiseAccessibleItemEvent(
		reinterpret_cast<accessibility::ItemId>(selected), 20012);
	return result != FALSE;
}

void VirtualTree::handleSelected(Item* item) {
	selected = validate(item) ? item : nullptr;
	if(selected) {
		if(!multiSelect()) {
			clearSelection(selected);
		}
		setSelectedState(selected, true, false);
		raiseAccessibleItemEvent(
			reinterpret_cast<accessibility::ItemId>(selected), 20012);
	}
}

bool VirtualTree::handleSetItem(TVITEMEX& tv) {
	auto item = reinterpret_cast<Item*>(tv.hItem);
	if(!validate(item)) { return false; }
	if(tv.mask & TVIF_IMAGE) { item->image = tv.iImage; }
	if(tv.mask & TVIF_PARAM) { item->lParam = tv.lParam; }
	if(tv.mask & TVIF_SELECTEDIMAGE) { item->selectedImage = tv.iSelectedImage; }
	if(tv.mask & TVIF_STATE) {
		if(tv.stateMask & TVIS_SELECTED) {
			setSelectedState(item, (tv.state & TVIS_SELECTED) != 0, false);
		}
		item->state &= ~tv.stateMask;
		item->state |= tv.state & tv.stateMask;
		if((tv.stateMask & TVIS_SELECTED) && item->selected()) {
			if(!multiSelect()) {
				clearSelection(item);
			}
			if(!selected) {
				selected = item;
			}
		} else if((tv.stateMask & TVIS_SELECTED) && selected == item) {
			selected = firstSelected();
		}
	}
	if(tv.mask & TVIF_TEXT) { item->setText(tv.pszText); }
	if(item->handle) {
		auto virtualHandle = tv.hItem;
		tv.hItem = item->handle;
		beginSelectionSync();
		sendTreeMsg(TVM_SETITEM, 0, reinterpret_cast<LPARAM>(&tv));
		endSelectionSync();
		tv.hItem = virtualHandle;
	}
	return true;
}

bool VirtualTree::handleSortChildren(Item* item, bool recursive) {
	if(!validate(item)) {
		return false;
	}

	std::vector<Item*> children;
	for(auto child = item->firstChild; child; child = child->next) {
		children.push_back(child);
	}
	std::stable_sort(children.begin(), children.end(),
		[](const Item* left, const Item* right) {
			const auto& leftText = left->text ? *left->text : tstring();
			const auto& rightText = right->text ? *right->text : tstring();
			return ::CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
				leftText.c_str(), -1, rightText.c_str(), -1) == CSTR_LESS_THAN;
		});

	item->firstChild = children.empty() ? nullptr : children.front();
	item->lastChild = children.empty() ? nullptr : children.back();
	for(size_t i = 0; i < children.size(); ++i) {
		children[i]->prev = i ? children[i - 1] : nullptr;
		children[i]->next = i + 1 < children.size() ? children[i + 1] : nullptr;
		if(recursive) {
			handleSortChildren(children[i], true);
		}
	}

	auto nativeParent = item == root ? TVI_ROOT : item->handle;
	if(item == root || nativeParent) {
		sendTreeMsg(TVM_SORTCHILDREN, recursive ? TRUE : FALSE,
			reinterpret_cast<LPARAM>(nativeParent));
	}
	raiseAccessibleStructureChanged();
	return true;
}

void VirtualTree::addRoot() {
	root = const_cast<Item*>(&*items.emplace().first);
}

bool VirtualTree::validate(Item* item) const {
	return item && items.find(*item) != items.cend();
}

VirtualTree::Item* VirtualTree::find(HTREEITEM handle) {
	if(!handle) { return nullptr; }
	for(auto& i: items) { if(i.handle == handle) { return const_cast<Item*>(&i); } }
	return nullptr;
}

void VirtualTree::display(Item& item) {
	// insert the item in the actual tree control.
	if(item.handle) { return; }

	if(item.parent != root && !item.parent->handle) { display(*item.parent); }

	// don't insert items as expanded; instead expand them manually if they were expanded before.
	auto expanded = item.expanded();
	if(expanded) { item.state &= ~TVIS_EXPANDED; }

	TVINSERTSTRUCT tvis = { item.parent->handle, item.prev ? item.prev->handle : item.next ? TVI_FIRST : TVI_LAST, { {
		TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_STATE | TVIF_TEXT } } };
	tvis.itemex.state = item.state;
	tvis.itemex.stateMask = item.state;
	tvis.itemex.pszText = item.text ? const_cast<TCHAR*>(item.text->c_str()) : LPSTR_TEXTCALLBACK;
	tvis.itemex.iImage = item.image;
	tvis.itemex.iSelectedImage = item.selectedImage;
	tvis.itemex.cChildren = item.firstChild ? 1 : 0;
	tvis.itemex.lParam = item.lParam;
	item.handle = reinterpret_cast<HTREEITEM>(sendTreeMsg(TVM_INSERTITEM, 0, reinterpret_cast<LPARAM>(&tvis)));

	if(item.selected()) {
		updateNativeSelectedState(&item);
	}
	if(&item == selected) {
		beginSelectionSync();
		sendTreeMsg(TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(item.handle));
		endSelectionSync();
	}
	if(expanded) { sendTreeMsg(TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(item.handle)); }
}

void VirtualTree::hide(Item& item) {
	// remove the item and its children from the actual tree control.
	if(!item.handle) { return; }
	auto child = item.firstChild;
	while(child) {
		hide(*child);
		child = child->next;
	}
	beginSelectionSync();
	sendTreeMsg(TVM_DELETEITEM, 0, reinterpret_cast<LPARAM>(item.handle));
	endSelectionSync();
	item.handle = nullptr;
}

void VirtualTree::remove(Item* item) {
	while(item->firstChild) {
		remove(item->firstChild);
	}
	if(item->parent) {
		if(item->parent->firstChild == item) {
			item->parent->firstChild = item->next;
			if(!item->parent->firstChild) {
				updateChildDisplay(item->parent);
			}
		}
		if(item->parent->lastChild == item) {
			item->parent->lastChild = item->prev;
		}
	}
	if(item->prev) { item->prev->next = item->next; }
	if(item->next) { item->next->prev = item->prev; }
	if(item == selected) {
		item->state &= ~TVIS_SELECTED;
		selected = firstSelected();
	}
	items.erase(*item);
}

bool VirtualTree::multiSelect() const {
	return virtualMultiSelect || (getExtendedStyle() & TVS_EX_MULTISELECT) != 0;
}

size_t VirtualTree::selectedCount() const {
	size_t result = 0;
	for(const auto& item: items) {
		if(&item != root && item.selected()) {
			++result;
		}
	}
	return result;
}

VirtualTree::Item* VirtualTree::firstSelected() const {
	for(auto item = root ? root->firstChild : nullptr; item; item = item->nextItem()) {
		if(item->selected()) {
			return item;
		}
	}
	return nullptr;
}

VirtualTree::Item* VirtualTree::nextSelected(Item* item) const {
	if(!validate(item)) {
		return nullptr;
	}
	for(auto next = item->nextItem(); next; next = next->nextItem()) {
		if(next->selected()) {
			return next;
		}
	}
	return nullptr;
}

void VirtualTree::clearSelection(Item* except) {
	for(auto& value: items) {
		auto item = const_cast<Item*>(&value);
		if(item == root || item == except || !item->selected()) {
			continue;
		}
		setSelectedState(item, false);
	}
}

void VirtualTree::setSelectedState(Item* item, bool value, bool updateNative) {
	if(!validate(item) || item == root) {
		return;
	}

	if(value) {
		item->state |= TVIS_SELECTED;
	} else {
		item->state &= ~TVIS_SELECTED;
	}
	if(!value && selected == item) {
		selected = firstSelected();
	}
	if(updateNative) {
		updateNativeSelectedState(item);
	}
}

void VirtualTree::updateNativeSelectedState(Item* item) {
	if(!validate(item) || !item->handle) {
		return;
	}

	TVITEMEX tv = { TVIF_HANDLE | TVIF_STATE, item->handle };
	tv.stateMask = TVIS_SELECTED;
	tv.state = item->selected() ? TVIS_SELECTED : 0;
	beginSelectionSync();
	sendTreeMsg(TVM_SETITEM, 0, reinterpret_cast<LPARAM>(&tv));
	endSelectionSync();
}

void VirtualTree::beginSelectionSync() {
	++suppressSelectionSync;
}

void VirtualTree::endSelectionSync() {
	if(suppressSelectionSync) {
		--suppressSelectionSync;
	}
}

void VirtualTree::updateChildDisplay(Item* item) {
	// this determines whether the item has a +/- sign next to it.
	if(!item->handle) { return; }
	TVITEMEX tv = { TVIF_CHILDREN, item->handle };
	tv.cChildren = item->firstChild ? 1 : 0;
	sendTreeMsg(TVM_SETITEM, 0, reinterpret_cast<LPARAM>(&tv));
}

void VirtualTree::configureAccessibility() {
	setAccessibleControlType(accessibility::Tree);
	if(!handle() || getAccessibleName().empty()) {
		setAccessibleName(_T("Virtual tree"));
	}

	auto toId = [](Item* item) {
		return reinterpret_cast<accessibility::ItemId>(item);
	};
	auto fromId = [](accessibility::ItemId id) {
		return reinterpret_cast<Item*>(id);
	};

	accessibility::ItemProvider provider;
	provider.exists = [this, fromId](accessibility::ItemId id) {
		return validate(fromId(id));
	};
	provider.children = [this, toId, fromId](accessibility::ItemId parent) {
		std::vector<accessibility::ItemId> result;
		auto item = parent ? fromId(parent) : root;
		if(!validate(item)) {
			return result;
		}
		for(auto child = item->firstChild; child; child = child->next) {
			result.push_back(toId(child));
		}
		return result;
	};
	provider.parent = [this, toId, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		return validate(item) && item->parent != root ?
			toId(item->parent) : accessibility::ItemId();
	};
	provider.name = [this, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		if(!validate(item)) {
			return tstring();
		}
		if(item->text) {
			return *item->text;
		}
		if(item->handle) {
			TCHAR buffer[1024] = { 0 };
			TVITEMEX value = { TVIF_TEXT, item->handle };
			value.pszText = buffer;
			value.cchTextMax =
				static_cast<int>(sizeof(buffer) / sizeof(buffer[0]));
			if(sendTreeMsg(TVM_GETITEM, 0,
				reinterpret_cast<LPARAM>(&value))) {
				return tstring(buffer);
			}
		}
		return tstring();
	};
	provider.bounds = [this, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		if(!validate(item) || !item->handle) {
			return Rectangle();
		}
		RECT rect = { 0 };
		if(!TreeView_GetItemRect(treeHandle(), item->handle, &rect, TRUE)) {
			return Rectangle();
		}
		::MapWindowPoints(treeHandle(), handle(),
			reinterpret_cast<POINT*>(&rect), 2);
		return Rectangle(rect);
	};
	provider.controlType = [](accessibility::ItemId) {
		return static_cast<long>(accessibility::TreeItem);
	};
	provider.selection = [this, toId] {
		std::vector<accessibility::ItemId> result;
		if(selected) {
			result.push_back(toId(selected));
		}
		return result;
	};
	provider.selected = [this, fromId](accessibility::ItemId id) {
		return selected == fromId(id);
	};
	provider.select = [this, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		if(validate(item)) {
			setSelected(item->ptr());
		}
	};
	provider.selectionRequired = false;
	provider.expandState = [this, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		if(!validate(item) || !item->firstChild) {
			return accessibility::LeafNode;
		}
		return item->expanded() ?
			accessibility::Expanded : accessibility::Collapsed;
	};
	provider.expand = [this, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		if(validate(item)) {
			expand(item->ptr());
		}
	};
	provider.collapse = [this, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		if(validate(item)) {
			collapse(item->ptr());
		}
	};
	provider.invoke = [this, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		if(!validate(item)) {
			return;
		}
		if(item->firstChild) {
			item->expanded() ? collapse(item->ptr()) : expand(item->ptr());
		} else {
			setSelected(item->ptr());
		}
	};
	provider.focused = [this, toId] {
		return selected ? toId(selected) : accessibility::ItemId();
	};
	provider.setFocus = [this, fromId](accessibility::ItemId id) {
		auto item = fromId(id);
		if(validate(item)) {
			::SetFocus(treeHandle());
			setSelected(item->ptr());
		}
	};
	setAccessibleItems(provider);
}

LRESULT VirtualTree::sendTreeMsg(UINT msg, WPARAM wParam, LPARAM lParam) {
	// send with a direct dispatcher call to avoid loops (since we catch tree messages).
	MSG treeMsg { treeHandle(), msg, wParam, lParam };
	return tree->getDispatcher().chain(treeMsg);
}

}
