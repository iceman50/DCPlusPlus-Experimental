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

#include <dwt/widgets/MDIFrame.h>
#include <dwt/WidgetCreator.h>
#include <dwt/widgets/MDIParent.h>

namespace dwt {

// TODO Fix caption
MDIFrame::Seed::Seed(const tstring& caption) :
	BaseType::Seed(caption, WS_OVERLAPPEDWINDOW, 0),
	mdiIdFirstChild(0),
	mdiWindowMenu(NULL)
{
}

void MDIFrame::create( const Seed& cs )
{
	BaseType::create(cs);

	MDIParent::Seed mdiSeed;
	mdiSeed.idFirstChild = cs.mdiIdFirstChild;
	mdiSeed.windowMenu = cs.mdiWindowMenu;
	mdi = WidgetCreator<MDIParent>::create(this, mdiSeed);
	onSized([this](const SizedEvent&) { layout(); });
	layout();
}

void MDIFrame::layout()
{
	if(mdi && mdi->handle()) {
		mdi->resize(getContentRect());
	}
}

bool MDIFrame::handleMessage(const MSG& msg, LRESULT& retVal) {
	bool handled = BaseType::handleMessage(msg, retVal);

	if(!handled && msg.message == WM_COMMAND && getMDIParent() && getMDIParent()->getActive()) {
		// Forward commands to the active tab
		handled = getMDIParent()->getActive()->handleMessage(msg, retVal);
	}
	return handled;
}
}
