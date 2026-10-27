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
#include <win32/appearance/AppAppearance.h>
#include <win32/appearance/ControlSeeds.h>
#include <win32/appearance/IconManager.h>
#include <win32/controls/DialogControls.h>
#include <win32/services/HelpService.h>

#include <win32/resource.h>

#include <win32/settings/SettingsDialog.h>

#include <dcpp/settings/SettingsManager.h>

#include <dwt/util/GDI.h>
#include <dwt/util/HoldRedraw.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/ScrolledContainer.h>
#include <dwt/widgets/ToolTip.h>


#include <win32/settings/general/GeneralPage.h>

#include <win32/settings/connection/ConnectivityPage.h>
#include <win32/settings/connection/ConnectivityManualPage.h>
#include <win32/settings/connection/BandwidthLimitPage.h>
#include <win32/settings/connection/ProxyPage.h>

#include <win32/settings/transfers/DownloadPage.h>
#include <win32/settings/transfers/FavoriteDirsPage.h>
#include <win32/settings/transfers/QueuePage.h>

#include <win32/settings/transfers/UploadPage.h>
#include <win32/settings/transfers/UploadFilteringPage.h>

#include <win32/settings/appearance/AppearancePage.h>
#include <win32/settings/appearance/EmoticonsPage.h>
#include <win32/settings/appearance/ThemePage.h>
#include <win32/settings/appearance/TabsPage.h>
#include <win32/settings/appearance/WindowsPage.h>

#include <win32/settings/appearance/NotificationsPage.h>

#include <win32/settings/advanced/HistoryPage.h>
#include <win32/settings/advanced/LogPage.h>

#include <win32/settings/advanced/AdvancedPage.h>
#include <win32/settings/advanced/ExpertsPage.h>
#include <win32/settings/advanced/ExperimentalPage.h>
#include <win32/settings/advanced/UCPage.h>
#include <win32/settings/connection/CertificatesPage.h>
#include <win32/settings/advanced/SearchTypesPage.h>
#include <win32/settings/advanced/UserMatchPage.h>

#include <win32/settings/advanced/PluginPage.h>

using dwt::Grid;
using dwt::GridInfo;

using dwt::ToolTip;

const int SettingsDialog::pluginPagePos = 25; // remember to change when adding pages...

SettingsDialog::SettingsDialog(dwt::Widget* parent) :
dwt::ModalDialog(parent),
currentPage(0),
grid(0),
tree(0),
help(0),
tip(0)
{
	onInitDialog([this] { return initDialog(); });
	onHelp(&HelpService::help);
	onClosing([this] { return handleClosing(); });
}

int SettingsDialog::run() {
	auto sizeVal = [](SettingsManager::IntSetting setting) {
		return std::max(SettingsManager::getInstance()->get(setting), 200);
	};
	create(Seed(dwt::Point(sizeVal(SettingsManager::SETTINGS_WIDTH), sizeVal(SettingsManager::SETTINGS_HEIGHT)),
		WS_SIZEBOX | DS_CONTEXTHELP));
	return show();
}

SettingsDialog::~SettingsDialog() {
}

static LRESULT helpDlgCode(WPARAM wParam) {
	if(wParam == VK_TAB || wParam == VK_RETURN)
		return 0;
	return DLGC_WANTMESSAGE;
}

bool SettingsDialog::initDialog() {
	/* set this to IDH_INDEX so that clicking in an empty space of the dialog generates a WM_HELP
	message with no error; then SettingsDialog::helpImpl will convert IDH_INDEX to the current
	page's help id. */
	setHelpId(IDH_INDEX);

	grid = addChild(Grid::Seed(1, 2));
	grid->row(0).mode = GridInfo::FILL;
	grid->row(0).align = GridInfo::STRETCH;
	grid->setSpacing(8);

	grid->column(0).size = 170;
	grid->column(0).mode = GridInfo::STATIC;
	grid->column(1).mode = GridInfo::FILL;
	HTREEITEM selectedPage = nullptr;

	{
		auto seed = Tree::Seed();
		seed.style |= WS_BORDER;
		seed.tvExStyle |= TVS_EX_DOUBLEBUFFER;
		tree = grid->addChild(seed);
		tree->setHelpId(IDH_SETTINGS_TREE);
		tree->setItemHeight(tree->getItemHeight() * 5 / 4);
		tree->onSelectionChanged([this] { handleSelectionChanged(); });
	}

	{
		const dwt::Point size(16, 16);
		dwt::ImageListPtr images(new dwt::ImageList(size));
		tree->setNormalImageList(images);

		auto cur = grid->addChild(Grid::Seed(3, 1));
		cur->row(0).mode = GridInfo::FILL;
		cur->row(0).align = GridInfo::STRETCH;
		cur->column(0).mode = GridInfo::FILL;
		cur->setSpacing(grid->getSpacing());

		// Compose the page and its controls together, including their background
		// erases. Buffer only this subtree so tree input and the footer stay responsive.
		auto container = cur->addChild(dwt::ScrolledContainer::Seed(WS_BORDER, WS_EX_COMPOSITED));
		pageHost = container;

		const size_t setting = SETTING(SETTINGS_PAGE);
		auto addPage = [&](const tstring& title, const std::type_info& type, std::function<PropPage* ()> create, unsigned icon, HTREEITEM parent) -> HTREEITEM {
			auto index = static_cast<int>(pages.size());
			images->add(*IconManager::load(icon, size.x));
			auto item = tree->insert(title, parent, TVI_LAST, 0, true, index);
			if(static_cast<size_t>(index) == setting) {
				selectedPage = item;
			}
			pages.emplace_back(type, item, icon, std::move(create));
			return item;
		};

		addPage(T_("Personal information"), typeid(GeneralPage), [container] { return new GeneralPage(container); }, IDI_USER, TVI_ROOT);

		{
			HTREEITEM item = addPage(T_("Connectivity"), typeid(ConnectivityPage), [container] { return new ConnectivityPage(container); }, IDI_CONN_BLUE, TVI_ROOT);
			addPage(T_("Manual configuration"), typeid(ConnectivityManualPage), [container] { return new ConnectivityManualPage(container); }, IDI_CONN_GREY, item);
			addPage(T_("Bandwidth limiting"), typeid(BandwidthLimitPage), [container] { return new BandwidthLimitPage(container); }, IDI_BW_LIMITER, item);
			addPage(T_("Proxy"), typeid(ProxyPage), [container] { return new ProxyPage(container); }, IDI_PROXY, item);
		}

		{
			HTREEITEM item = addPage(T_("Downloads"), typeid(DownloadPage), [container] { return new DownloadPage(container); }, IDI_DOWNLOAD, TVI_ROOT);
			addPage(T_("Favorites"), typeid(FavoriteDirsPage), [container] { return new FavoriteDirsPage(container); }, IDI_FAVORITE_DIRS, item);
			addPage(T_("Queue"), typeid(QueuePage), [container] { return new QueuePage(container); }, IDI_QUEUE, item);
		}

		{
			HTREEITEM item = addPage(T_("Sharing"), typeid(UploadPage), [container] { return new UploadPage(container); }, IDI_UPLOAD, TVI_ROOT);
			addPage(T_("Filtering"), typeid(UploadFilteringPage), [container] { return new UploadFilteringPage(container); }, IDI_UPLOAD_FILTERING, item);
		}

		{
			HTREEITEM item = addPage(T_("Appearance"), typeid(AppearancePage), [container] { return new AppearancePage(container); }, IDI_DCPP, TVI_ROOT);
			addPage(T_("Icons"), typeid(EmoticonsPage), [container] { return new EmoticonsPage(container); }, IDI_CHAT, item);
			addPage(T_("Themes"), typeid(ThemePage), [container] { return new ThemePage(container); }, IDI_STYLES, item);
			addPage(T_("Styles / User matching"), typeid(UserMatchPage), [container] { return new UserMatchPage(container); }, IDI_USERS, item);
			addPage(T_("Tabs"), typeid(TabsPage), [container] { return new TabsPage(container); }, IDI_TABS, item);
			addPage(T_("Windows"), typeid(WindowsPage), [container] { return new WindowsPage(container); }, IDI_WINDOWS, item);
		}

		addPage(T_("Notifications"), typeid(NotificationsPage), [container] { return new NotificationsPage(container); }, IDI_NOTIFICATIONS, TVI_ROOT);

		{
			HTREEITEM item = addPage(T_("History"), typeid(HistoryPage), [container] { return new HistoryPage(container); }, IDI_CLOCK, TVI_ROOT);
			addPage(T_("Logs"), typeid(LogPage), [container] { return new LogPage(container); }, IDI_LOGS, item);
		}

		{
			HTREEITEM item = addPage(T_("Advanced"), typeid(AdvancedPage), [container] { return new AdvancedPage(container); }, IDI_ADVANCED, TVI_ROOT);
			addPage(T_("Experts only"), typeid(ExpertsPage), [container] { return new ExpertsPage(container); }, IDI_EXPERT, item);
			addPage(T_("User commands"), typeid(UCPage), [container] { return new UCPage(container); }, IDI_USER_OP, item);
			addPage(T_("Security & certificates"), typeid(CertificatesPage), [container] { return new CertificatesPage(container); }, IDI_SECURE, item);
			addPage(T_("Search types"), typeid(SearchTypesPage), [container] { return new SearchTypesPage(container); }, IDI_SEARCH, item);
		}

		addPage(T_("Experimental"), typeid(ExperimentalPage), [container] { return new ExperimentalPage(container); }, IDI_EXPERT, TVI_ROOT);
		addPage(T_("Plugins"), typeid(PluginPage), [container] { return new PluginPage(container); }, IDI_PLUGINS, TVI_ROOT);
		// remember to change pluginPagePos accordingly...

		Grid::Seed gs(1, 1);
		gs.style |= WS_BORDER;
		auto helpGrid = cur->addChild(gs);
		helpGrid->column(0).mode = GridInfo::FILL;

		auto ts = ControlSeeds::Dialog::richTextBox;
		ts.style &= ~ES_SUNKEN;
		ts.exStyle &= ~WS_EX_CLIENTEDGE;
		ts.lines = 6;
		help = helpGrid->addChild(ts);
		AppAppearance::setColor(help);
		help->onRaw([this](WPARAM w, LPARAM) { return helpDlgCode(w); }, dwt::Message(WM_GETDLGCODE));

		cur = cur->addChild(Grid::Seed(1, 3));
		cur->column(0).mode = GridInfo::FILL;
		cur->column(0).align = GridInfo::BOTTOM_RIGHT;
		cur->setSpacing(grid->getSpacing());

		DialogControls::addDlgButtons(cur,
			[this] { handleClosing(); handleOKClicked(); },
			[this] { handleClosing(); endDialog(IDCANCEL); });

		DialogControls::addHelpButton(cur)->onClicked([this] { HelpService::help(this); });

		if(!selectedPage) {
			selectedPage = tree->getFirst();
		}
	}

	/* use a hidden tooltip to determine when to show the help tooltip, so we don't have to manage
	timers etc. */
	tip = addChild(ToolTip::Seed());

	// make tooltips last longer
	tip->setDelay(TTDT_AUTOPOP, tip->getDelay(TTDT_AUTOPOP) * 3);

	// wait more time before displaying tooltips
	tip->setDelay(TTDT_INITIAL, tip->getDelay(TTDT_INITIAL) + 1000);
	tip->setDelay(TTDT_RESHOW, tip->getDelay(TTDT_INITIAL));

	// on TTN_SHOW, hide the actual tooltip and display our rich one in its place.
	tip->onRaw([this](WPARAM, LPARAM lParam) -> LRESULT {
		auto pos = tip->getWindowRect().pos;
		::SetWindowPos(tip->handle(), 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOACTIVATE);
		auto& ttdi = *reinterpret_cast<LPNMTTDISPINFO>(lParam);
		auto widget = dwt::hwnd_cast<dwt::Control*>(reinterpret_cast<HWND>(ttdi.hdr.idFrom));
		if(widget) {
			HelpService::helpTooltip(widget, pos);
		}
		return TRUE;
	}, dwt::Message(WM_NOTIFY, TTN_SHOW));

	// kill our rich tooltip on TTN_POP.
	tip->onRaw([this](WPARAM, LPARAM) -> LRESULT {
		HelpService::killHelpTooltip();
		return 0;
	}, dwt::Message(WM_NOTIFY, TTN_POP));

	/*
	* catch WM_SETFOCUS messages (onFocus events) sent to every children of this dialog. the normal
	* way to do it would be to use an Application::Filter, but unfortunately these messages don't
	* go through there but instead are sent directly to the control's wndproc.
	*/
	/// @todo when dwt has better tracking of children, improve this
	registerHelp(handle());

	addAccel(FCONTROL, VK_TAB, [this] { handleCtrlTab(false); });
	addAccel(FCONTROL | FSHIFT, VK_TAB, [this] { handleCtrlTab(true); });
	initAccels();

	updateTitle();

	centerWindow();
	onSized([this](const dwt::SizedEvent&) { layout(); });
	layout();
	// Restore selection and scroll position while still hidden, after the tree
	// has its final size. Posting this work shows an unselected tree first.
	tree->setSelected(selectedPage);
	tree->ensureVisible(selectedPage);

	return false;
}

PropPage* SettingsDialog::ensurePage(PageInfo& info) {
	if(info.page) {
		return info.page;
	}

	info.page = info.create();
	const auto icon = info.icon;
	info.page->onVisibilityChanged([=](bool b) { if(b) {
		setSmallIcon(IconManager::load(icon, 16));
		setLargeIcon(IconManager::load(icon, 32));
	} });

	if(tip) {
		registerHelp(info.page->handle());
	}

	return info.page;
}

void SettingsDialog::registerHelp(HWND root) {
	::EnumChildWindows(root, EnumChildProc, reinterpret_cast<LPARAM>(this));
}

BOOL CALLBACK SettingsDialog::EnumChildProc(HWND hwnd, LPARAM lParam) {
	SettingsDialog* dialog = reinterpret_cast<SettingsDialog*>(lParam);
	dwt::Control* widget = dwt::hwnd_cast<dwt::Control*>(hwnd);

	if(widget && widget != dialog->help) {
		// update the bottom help box on focus / sel change.
		widget->onFocus([dialog, widget] { dialog->handleChildHelp(widget); });

		TablePtr table = dynamic_cast<TablePtr>(widget);
		if(table)
			table->onSelectionChanged([dialog, widget] { dialog->handleChildHelp(widget); });

		/* associate a tooltip callback with every widget; a tooltip will be shown for those that
		provide a valid cshelp id; the tooltip will disappear when hovering others (to be as
		discreet as possible). the tooltip is provided a random text to make it believe in its
		usefulness (we will actually hide it and show our own rich one on top of it). */
		dialog->tip->addTool(widget, const_cast<LPTSTR>(_T("M")));

		// special refresh logic for tables as they may have different help ids for each item.
		if(table) {
			table->onMouseMove([dialog, table](const dwt::MouseEvent&) -> bool {
				const auto id = table->getHelpId();
				static int prevId = -1;
				if(static_cast<int>(id) != prevId) {
					prevId = static_cast<int>(id);
					dialog->tip->refresh();
				}
				return false;
			});
		}
	}
	return TRUE;
}

void SettingsDialog::handleChildHelp(dwt::Control* widget) {
	const auto text = Text::toT(HelpService::getHelpText(widget->getHelpId()).second);
	if(help->getText() != text) help->setText(text);
}

bool SettingsDialog::handleClosing() {
	dwt::Point pt = getWindowSize();
	SettingsManager::getInstance()->set(SettingsManager::SETTINGS_WIDTH,
		static_cast<int>(static_cast<float>(pt.x) / dwt::util::dpiFactor()));
	SettingsManager::getInstance()->set(SettingsManager::SETTINGS_HEIGHT,
		static_cast<int>(static_cast<float>(pt.y) / dwt::util::dpiFactor()));

	if(currentPage) {
		auto pageInfo = find_if(pages.begin(), pages.end(), [this](const PageInfo& info) { return info.page == currentPage; });
		if(pageInfo != pages.end()) {
			SettingsManager::getInstance()->set(SettingsManager::SETTINGS_PAGE,
				static_cast<int>(pageInfo - pages.begin()));
		}
	}

	return true;
}

void SettingsDialog::handleSelectionChanged() {
	auto sel = tree->getSelected();
	if(sel) {
		auto pageInfo = find_if(pages.begin(), pages.end(), [sel](const PageInfo& info) { return info.item == sel; });
		if(pageInfo == pages.end()) {
			return;
		}

		if(pageInfo->page && pageInfo->page == currentPage) {
			return;
		}

		{
			/* Suppress intermediate paints during page creation and layout.
			 * Keep the new page hidden until the host's redraw is restored. */
			dwt::util::HoldRedraw hold(pageHost);
			auto page = ensurePage(*pageInfo);
			if(currentPage) {
				currentPage->setVisible(false);
			}

			// The ScrolledContainer treats its first child in Z order as active.
			page->setZOrder(HWND_TOP);
			currentPage = page;
			pageHost->layout();
			updateTitle();
		}
		// WM_SETREDRAW temporarily removes WS_VISIBLE from the host. Showing
		// the page under that hidden ancestor can leave native list views with
		// stale contents from the previous page. Deliver the show transition
		// only once the host is visible again.
		currentPage->setVisible(true);
		// Paint the completed subtree together after the tree notification returns.
		// Synchronous painting here can run inside the native tree's drawing cycle.
		pageHost->redrawWindow(RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
			RDW_ALLCHILDREN);
	}
}

void SettingsDialog::handleOKClicked() {
	write();
	endDialog(IDOK);
}

void SettingsDialog::handleCtrlTab(bool shift) {
	HTREEITEM sel = tree->getSelected();
	HTREEITEM next = 0;
	if(!sel)
		next = tree->getFirst();
	else if(shift) {
		if(sel == tree->getFirst())
			next = tree->getLast();
	} else if(sel == tree->getLast())
		next = tree->getFirst();
	if(!next)
		next = tree->getNext(sel, shift ? TVGN_PREVIOUSVISIBLE : TVGN_NEXTVISIBLE);
	tree->setSelected(next);
}

void SettingsDialog::updateTitle() {
	tstring title;
	auto item = tree->getSelected();
	while(item) {
		title = _T(" > ") + tree->getText(item) + title;
		item = tree->getParent(item);
	}
	setText(T_("Settings") + title);
}

void SettingsDialog::write() {
	for(auto& i: pages) {
		if(i.page) {
			i.page->write();
		}
	}
}

void SettingsDialog::layout() {
	const auto content = getContentRect();
	grid->resize(dwt::Rectangle(content.left() + 8, content.top() + 8, content.width() - 16, content.height() - 16));

	if(currentPage) {
		currentPage->getParent()->layout();
	}
}

void SettingsDialog::helpImpl(unsigned& id) {
	if(id == IDH_INDEX && currentPage) {

		/* when a control has no help id, it asks its parent and so on. here we go back to children
		from the top parent, so there is a possibility of an infinite loop if a page has no help
		id. */
		static bool recursion = false;
		if(recursion)
			return;
		recursion = true;

		id = currentPage->getHelpId();

		recursion = false;
	}
}
