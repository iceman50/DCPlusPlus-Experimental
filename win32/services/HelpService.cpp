/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdafx.h"

#include <win32/services/HelpService.h>
#include <dwt/util/GDI.h>
#include <win32/appearance/ControlSeeds.h>
#include <win32/forward.h>
#include <dcpp/system/File.h>
#include <dcpp/text/StringTokenizer.h>
#include <dcpp/util/Util.h>
#include <dcpp/text/Text.h>
#include <win32/controls/RichTextBox.h>
#include <win32/resource.h>

#ifdef HAVE_HTMLHELP_H
#include <htmlhelp.h>

#ifndef _MSC_VER
// HTML Help libs from recent MS SDKs are compiled with /GS, so link in the following.
// See https://bugs.launchpad.net/dcplusplus/+bug/2028276 for __security_cookie
#ifdef _WIN64
#define HH_GS_CALL __cdecl
#else
#define HH_GS_CALL __fastcall
#endif
extern "C" {
	void HH_GS_CALL __GSHandlerCheck() { }
	void HH_GS_CALL __security_check_cookie(uintptr_t) { }
#if __MINGW64_VERSION_MAJOR > 7
	uintptr_t __security_cookie;
#endif
}
#undef HH_GS_CALL
#endif

#endif

namespace HelpService {

namespace {
DWORD helpCookie = 0;
tstring helpPath;
StringList helpTexts;

void initHelpPath() {
	// find the current locale
	auto lang = Util::getIETFLang();

	// find the path to the help file
	string path;
	if(lang != "en" && lang != "en-US") {
		while(true) {
			path = Util::getPath(Util::PATH_LOCALE) + lang
				+ PATH_SEPARATOR_STR "help" PATH_SEPARATOR_STR "DCPlusPlus.chm";
			if(File::getSize(path) != -1)
				break;
			// if the lang has extra information, try to remove it
			string::size_type pos = lang.rfind('-');
			if(pos == string::npos)
				break;
			lang = lang.substr(0, pos);
		}
	}
	if(path.empty() || File::getSize(path) == -1) {
		path = Util::getPath(Util::PATH_RESOURCES) + "DCPlusPlus.chm";
		if(File::getSize(path) == -1) {
			/// @todo also check that the file is up-to-date
			/// @todo alert the user that the help file isn't found/up-to-date
			return;
		}
	}
	helpPath = Text::toT(path);
}

dwt::Control* helpPopup = nullptr; // only have 1 help popup at any given time.

/** Display a help popup.
Help popups display RTF text within a Rich Edit control. They visually look similar to a regular
tooltip, but with a yellow background and thicker borders.
@tparam tooltip Whether this help popup should act as an inactive tooltip that doesn't receive
input but closes itself once its timer runs out. */
template<bool tooltip>
class HelpPopup : private RichTextBox {
	typedef HelpPopup<tooltip> ThisType;
	typedef RichTextBox BaseType;

public:
	HelpPopup(dwt::Widget* parent, const tstring& text, const dwt::Point& pos = dwt::Point(), bool multiline = false) :
		BaseType(parent)
	{
		// create the box as an invisible popup window.
		auto seed = ControlSeeds::richTextBox;
		seed.style = WS_POPUP | ES_READONLY;
		if(multiline)
			seed.style |= ES_MULTILINE;
		seed.exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_CLIENTEDGE;
		seed.location.size.x = std::min(getParent()->getDesktopSize().width(), static_cast<long>(maxWidth * dwt::util::dpiFactor()));
		seed.location.size.y = 1; // so that Rich Edit 8 sends EN_REQUESTRESIZE
		seed.events |= ENM_REQUESTRESIZE;
		create(seed);

		const auto margins = sendMessage(EM_GETMARGINS);
		sendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(LOWORD(margins) + margin, HIWORD(margins) + margin));

		// let the control figure out what the best size is.
		onRaw([this, text, pos](WPARAM, LPARAM l) { return this->resize(l, text, pos); }, dwt::Message(WM_NOTIFY, EN_REQUESTRESIZE));
		setText(text);
	}

private:
	LRESULT resize(LPARAM lParam, const tstring& text, dwt::Point pos) {
		dwt::Rectangle rect { reinterpret_cast<REQRESIZE*>(lParam)->rc };

		if(rect.width() > getWindowRect().width() && !hasStyle(ES_MULTILINE)) {
			// can't add ES_MULTILINE at run time, so create the control again.
			new ThisType(getParent(), text, pos, true);
			close();
			return 0;
		}

		// ok, ready to show the popup! make sure there's only 1 at any given time.
		if(helpPopup) {
			helpPopup->close();
		}
		helpPopup = this;
		onDestroy([this] { if(this == helpPopup) helpPopup = nullptr; });

		// where to position the popup.
		if(!tooltip) {
			if(isAnyKeyPressed()) {
				auto rect = getParent()->getWindowRect();
				pos.x = rect.left() + rect.width() / 2;
				pos.y = rect.bottom() + margin;
			} else {
				pos = dwt::Point::fromLParam(::GetMessagePos());
			}
		}

		// adjust the size to account for borders and margins.
		rect.pos = pos;
		rect.size.x += ::GetSystemMetrics(SM_CXEDGE) * 2;
		rect.size.y += ::GetSystemMetrics(SM_CYEDGE) * 2 + margin;

		// make sure the window fits in within the desktop of the parent widget.
		rect.ensureVisibility(getParent());

		setColor(dwt::Color::predefined(COLOR_INFOTEXT), dwt::Color::predefined(COLOR_INFOBK));

		if(tooltip) {
			// this help popup acts as a tooltip; it will close by itself.
			onMouseMove([this](const dwt::MouseEvent&) { return this->close(); });

		} else {
			// capture the mouse.
			onLeftMouseDown([this](const dwt::MouseEvent&) { return this->close(); });
			::SetCapture(handle());
			onDestroy([] { ::ReleaseCapture(); });

			// capture the keyboard.
			auto focus = dwt::hwnd_cast<dwt::Widget*>(::GetFocus());
			if(focus) {
				auto cb1 = focus->addCallback(dwt::Message(WM_KEYDOWN), [this](const MSG&, LRESULT&) { return this->close(); });
				auto cb2 = focus->addCallback(dwt::Message(WM_HELP), [this](const MSG&, LRESULT&) { return this->close(); });
				onDestroy([this, focus, cb1, cb2] {
					auto cb1_ = cb1; focus->clearCallback(dwt::Message(WM_KEYDOWN), cb1_);
					auto cb2_ = cb2; focus->clearCallback(dwt::Message(WM_HELP), cb2_);
				});
			}
		}

		// go live!
		::SetWindowPos(handle(), 0, rect.left(), rect.top(), rect.width(), rect.height(),
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_SHOWWINDOW);

		return 0;
	}

	bool close() {
		BaseType::close(true);
		return true;
	}

	static const long margin = 6;
	static const long maxWidth = 400;
};

} // namespace

void help(dwt::Control* widget) {
	helpId(widget, widget->getHelpId());
}

void helpId(dwt::Control* widget, unsigned id) {
	if(id >= IDH_CSHELP_BEGIN && id <= IDH_CSHELP_END) {
		// context-sensitive help
		new HelpPopup<false>(widget, Text::toT(getHelpText(id).second));

	} else if (!helpPath.empty()) {
#ifdef HAVE_HTMLHELP_H
		if(id < IDH_BEGIN || id > IDH_END)
			id = IDH_INDEX;
		const auto nativeHelpPath = File::toNativePath(Text::fromT(helpPath));
		::HtmlHelp(widget->handle(), nativeHelpPath.c_str(), HH_HELP_CONTEXT, id);
#endif
	}
}

void helpTooltip(dwt::Control* widget, const dwt::Point& pos) {
	killHelpTooltip();

	auto id = widget->getHelpId();
	if(id >= IDH_CSHELP_BEGIN && id <= IDH_CSHELP_END) {
		// context-sensitive help
		auto text = getHelpText(id);
		if(text.first) {
			new HelpPopup<true>(widget, Text::toT(text.second), pos);
		}
	}
}

void killHelpTooltip() {
	if(helpPopup) {
		helpPopup->close();
		helpPopup = nullptr;
	}
}

pair<bool, string> getHelpText(unsigned id) {
	if(id >= IDH_CSHELP_BEGIN) {
		id -= IDH_CSHELP_BEGIN;

		if(id < helpTexts.size()) {
			const auto& ret = helpTexts[id];
			if(!ret.empty()) {
				return std::make_pair(true, ret);
			}
		}
	}

	return std::make_pair(false, _("No help information available"));
}
void init() {
	initHelpPath();

	if(!helpPath.empty()) {
		// load up context-sensitive help texts
		try {
			helpTexts = StringTokenizer<string> (File(Util::getFilePath(Text::fromT(helpPath)) + "cshelp.rtf",
				File::READ, File::OPEN).read(), "\r\n").getTokens();
		} catch (const FileException&) {
		}
	}

#ifdef HAVE_HTMLHELP_H
	::HtmlHelp(NULL, NULL, HH_INITIALIZE, reinterpret_cast<DWORD_PTR> (&helpCookie));
#endif
}
void uninit() {
#ifdef HAVE_HTMLHELP_H
	::HtmlHelp(NULL, NULL, HH_UNINITIALIZE, helpCookie);
#endif
}

} // namespace HelpService
