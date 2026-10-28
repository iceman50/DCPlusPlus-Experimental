/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka
  Copyright (C) 2026 iceman50

  SmartWin++

  Copyright (c) 2005 Thomas Hansen

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
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DWT_MDIParent_h
#define DWT_MDIParent_h

#include "Control.h"
#include "../util/check.h"
#include "MDIFrame.h"

namespace dwt {

// Forward declaring friends
template< class WidgetType >
class WidgetCreator;
class Menu;

/** sideeffect= \par Side Effects :
  */
/// MDI Parent Control class
/** \ingroup WidgetControls
  * \WidgetUsageInfo
  * \image html mdi.PNG
  * Class for creating a MDI Parent Widget. <br>
  * An MDI Parent is a Widget which can contain several "inner Widgets" which must be
  * of type MDIChild, think of it as a special "container" Widget for only
  * MDIChild widgets. <br>
  * Note! <br>
  * You can either inherit your own MDIChild Widgets or use the factory method
  * createMDIChild to create MDI Children Widgets. <br>
  * Related class : <br>
  * MDIChild
  */
class MDIParent :
	public Control

{
	typedef Control BaseType;
	friend class WidgetCreator< MDIParent >;
public:
	/// Class type
	typedef MDIParent ThisType;

	/// Object type
	typedef ThisType * ObjectType;

	/// Seed class
	/** This class contains all of the values needed to create the widget. It also
	  * knows the type of the class whose seed values it contains. Every widget
	  * should define one of these.
	  */
	struct Seed : public BaseType::Seed {
		/**
		 * First child id for mdi menu, must be different from any other main menu id.
		 * Also, the menuHandle parameter of cs should point to the menu that will receive
		 *
		 **/
		UINT idFirstChild;

		HMENU windowMenu;

		Seed();
	};

	/// Actually creates the MDI EventHandlerClass Control
	/** You should call WidgetFactory::createMDIParent if you instantiate class
	  * directly. <br>
	  * Only if you DERIVE from class you should call this function directly.
	  */
	void create( const Seed & cs = Seed() );

	void cascade() {
		this->sendMessage(WM_MDICASCADE);
	}

	void tile(bool horizontal) {
		this->sendMessage(WM_MDITILE, horizontal ? MDITILE_HORIZONTAL : MDITILE_VERTICAL);
	}

	void arrange() {
		this->sendMessage(WM_MDIICONARRANGE);
	}

	void closeActive() {
		if(auto active = getActive()) {
			this->sendMessage(WM_MDIDESTROY, reinterpret_cast<WPARAM>(active->handle()));
		}
	}

	void closeAll() {
		for(HWND child = ::GetWindow(handle(), GW_CHILD); child; ) {
			HWND nextChild = ::GetWindow(child, GW_HWNDNEXT);
			if(hwnd_cast<Widget*>(child)) {
				this->sendMessage(WM_MDIDESTROY, reinterpret_cast<WPARAM>(child));
			}
			child = nextChild;
		}
	}

	void minimizeAll() {
		for(HWND child = ::GetWindow(handle(), GW_CHILD); child; ) {
			HWND nextChild = ::GetWindow(child, GW_HWNDNEXT);
			if(hwnd_cast<Widget*>(child)) {
				::ShowWindow(child, SW_MINIMIZE);
			}
			child = nextChild;
		}
	}

	Widget* getActive() {
		return hwnd_cast<Widget*>(reinterpret_cast<HWND>(this->sendMessage(WM_MDIGETACTIVE)));
	}

	bool isActiveMaximized() {
		BOOL max = FALSE;
		this->sendMessage(WM_MDIGETACTIVE, 0, reinterpret_cast<LPARAM>(&max));
		return (max > 0);
	}

	void setActive(dwt::Widget* widget) {
		if(widget && ::GetParent(widget->handle()) == handle()) {
			this->sendMessage(WM_MDIACTIVATE, reinterpret_cast<WPARAM>(widget->handle()));
		}
	}

	void maximizeActive() {
		if(auto active = getActive()) {
			this->sendMessage(WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(active->handle()));
		}
	}

	void restoreActive() {
		if(auto active = getActive()) {
			this->sendMessage(WM_MDIRESTORE, reinterpret_cast<WPARAM>(active->handle()));
		}
	}

	void next() {
		this->sendMessage(WM_MDINEXT, reinterpret_cast<WPARAM>(
			getActive() ? getActive()->handle() : NULL), FALSE);
	}

	void previous() {
		this->sendMessage(WM_MDINEXT, reinterpret_cast<WPARAM>(
			getActive() ? getActive()->handle() : NULL), TRUE);
	}

	void setMenu(HMENU frameMenu, HMENU windowMenu);
	void setMenu(Menu* frameMenu, Menu* windowMenu);
	void setBackgroundColor(COLORREF color);
	COLORREF getBackgroundColor() const { return backgroundColor; }
	void setBackgroundImage(const BitmapPtr& bitmap);
	void setBackgroundImage(const IconPtr& icon);
	void clearBackgroundImage();

	MDIFrame* getParent() { return static_cast<MDIFrame*>(BaseType::getParent()); }
protected:
	/// Constructor Taking pointer to parent
	explicit MDIParent( Widget * parent );

	// Protected to avoid direct instantiation, you can inherit and use WidgetFactory class which is friend
	virtual ~MDIParent();
private:
	bool handlePaintBackground();
	bool handleEraseBackground(HDC hdc);
	void redrawBackground();

	Application::FilterIter acceleratorFilter;
	bool hasAcceleratorFilter;
	COLORREF backgroundColor;
	BitmapPtr backgroundImage;
	IconPtr backgroundIcon;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation of class
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline MDIParent::MDIParent( Widget * parent )
	: BaseType(parent, MDIClientDispatcher::getDefault()),
	acceleratorFilter(),
	hasAcceleratorFilter(false),
	backgroundColor(::GetSysColor(COLOR_APPWORKSPACE)),
	backgroundImage(),
	backgroundIcon()
{
	dwtassert(parent, "Can't have a MDIParent without a parent...");
}

// end namespace dwt
}

#endif
