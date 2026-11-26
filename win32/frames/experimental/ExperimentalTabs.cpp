/*
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
#include <win32/frames/experimental/ExperimentalTabs.h>

#include <algorithm>
#include <iterator>
#include <limits>

#include <dcpp/core/crypto/PasswordHash.h>
#include <dcpp/core/io/File.h>
#include <dcpp/core/version.h>
#include <dcpp/events/LogManager.h>
#include <dcpp/hash/HashManager.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/private_chat/MessageOutbox.h>
#include <dcpp/settings/HubSettings.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/share/ShareManager.h>
#include <dcpp/util/Util.h>

#include <dwt/util/StringUtils.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/MessageBox.h>
#include <dwt/widgets/RichTextBox.h>
#include <dwt/widgets/ScrolledContainer.h>
#include <dwt/widgets/Spinner.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Table.h>
#include <dwt/widgets/TextBox.h>

#include <win32/components/TableLayout.h>
#include <win32/dialog/ParamDlg.h>
#include <win32/resource.h>
#include <win32/settings/base/LiveSettings.h>
#include <win32/util/ControlSeeds.h>
#include <win32/util/Encodings.h>
#include <win32/util/GeoIPUpdate.h>
#include <win32/util/IconManager.h>
#include <win32/util/LinkActions.h>
#include <win32/util/UiFonts.h>

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dwt::Spinner;

namespace {

constexpr int BYTES_PER_KIB = 1024;
constexpr int MAX_KIB_SETTING = static_cast<int>(
	(static_cast<int64_t>(std::numeric_limits<int>::max()) + BYTES_PER_KIB - 1) / BYTES_PER_KIB);

const TableLayout::ColumnInfo tempColumns[] = {
	{ N_("File"), 130, false },
	{ N_("Size"), 75, true },
	{ N_("Route"), 140, false },
	{ N_("TTH"), 250, false },
	{ N_("Path"), 220, false }
};

/**
 * A grid with one FILL column reports only that column's fixed size as its preferred width, which
 * leaves out what its rows need; give such columns, down the tree, the width of their widest row.
 * Tables and rich text boxes fill whatever width they get, so they don't count.
 */
void fitFillColumns(dwt::Widget* widget) {
	if(auto group = dynamic_cast<dwt::GroupBox*>(widget)) {
		// A group box isn't a container; its content is its one child grid.
		for(auto child = ::GetWindow(group->handle(), GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
			if(auto grid = dwt::hwnd_cast<dwt::Grid*>(child)) {
				fitFillColumns(grid);
			}
		}
		return;
	}
	auto container = dynamic_cast<dwt::Container*>(widget);
	if(!container) {
		return;
	}
	auto children = container->getChildren<dwt::Control>();
	LONG widest = 0;
	for(auto child = children.first; child != children.second; ++child) {
		fitFillColumns(*child);
		if(!dynamic_cast<dwt::Table*>(*child) && !dynamic_cast<dwt::RichTextBox*>(*child)) {
			widest = std::max<LONG>(widest, (*child)->getPreferredSize().x);
		}
	}
	auto grid = dynamic_cast<dwt::Grid*>(widget);
	if(grid && grid->columnCount() == 1 && grid->column(0).mode == GridInfo::FILL) {
		grid->column(0).size = static_cast<size_t>(widest);
	}
}

/**
 * Lay out a group's grids again from the top. A grid lays out its rows when its size changes, so
 * one whose size stays the same keeps rows measured with old fonts or contents.
 */
void layoutGrids(dwt::Widget* widget) {
	if(auto group = dynamic_cast<dwt::GroupBox*>(widget)) {
		for(auto child = ::GetWindow(group->handle(), GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
			if(auto grid = dwt::hwnd_cast<dwt::Grid*>(child)) {
				layoutGrids(grid);
			}
		}
		return;
	}
	auto grid = dynamic_cast<dwt::Grid*>(widget);
	if(!grid) {
		return;
	}
	grid->layout();
	auto children = grid->getChildren<dwt::Control>();
	for(auto child = children.first; child != children.second; ++child) {
		layoutGrids(*child);
	}
}

/** Check a password typed into a dialog against a stored hash, without keeping a UTF-8 copy around. */
bool matchesLockPassword(const tstring& password, const string& hash) {
	if(password.empty()) {
		return false;
	}
	auto utf8 = Text::fromT(password);
	const bool match = PasswordHash::verify(utf8, hash);
	::SecureZeroMemory(&utf8[0], utf8.size());
	return match;
}

/** The stored lock password hash, or nothing when none is set or it can't be checked. */
string lockHash() {
	const auto hash = SETTING(LOCK_PASSWORD);
	return PasswordHash::isValid(hash) ? hash : string();
}

}

/**
 * Lays out its groups in two columns sharing its width, or in one when it is too narrow for both
 * (the left column's groups first). It is the scroller's child, so its preferred size follows the
 * scroller's width.
 */
class ExperimentalTab::Columns : public dwt::Container {
public:
	explicit Columns(dwt::Widget* parent) : dwt::Container(parent) {
		Seed seed(0, WS_EX_CONTROLPARENT);
		create(seed);
		// Created without addChild, which would do this: without it the columns paint in the
		// system colours until a posted message gives them the application's.
		finalizeAppearance();
	}

	void add(dwt::Control* control, Column column, bool stretch) {
		items.push_back({ control, column, stretch });
		invalidate(true);
	}

	/** Measure again at the next layout; `grids` also lays out the groups' grids again. */
	void invalidate(bool grids) {
		measured = false;
		relayoutGrids = relayoutGrids || grids;
	}

	dwt::Point getPreferredSize() override {
		Grid::MeasureScope scope;
		RECT parent {};
		::GetClientRect(::GetParent(handle()), &parent);
		const auto measured = measure();
		auto fit = [&](LONG width) {
			return twoColumns(measured, width) ?
				dwt::Point(measured.width[LEFT] + gap() + measured.width[RIGHT],
					std::max(measured.height[LEFT], measured.height[RIGHT])) :
				dwt::Point(std::max(measured.width[LEFT], measured.width[RIGHT]), singleHeight(measured));
		};
		auto size = fit(parent.right);
		if(size.y > parent.bottom) {
			// A vertical scrollbar will take part of the width.
			size = fit(parent.right - getSystemMetric(SM_CXVSCROLL));
		}
		return size;
	}

	void layout() override {
		const auto size = getClientSize();
		const auto visible = visibleHeight();
		// Showing, hiding and resizing the tab or its scroller all lay the columns out; only a
		// change of size, visible height or contents moves anything. (The columns keep their size
		// while a column taller than the tab is scrolled.)
		if(measured && !relayoutGrids && size.x == laidOut.x && size.y == laidOut.y && visible == laidOutVisible) {
			return;
		}
		Grid::MeasureScope scope;
		const auto sizes = measure();
		if(twoColumns(sizes, size.x)) {
			// Share the width equally, each column at least as wide as its widest group.
			auto left = std::max<LONG>(sizes.width[LEFT], (size.x - gap()) / 2);
			auto right = size.x - gap() - left;
			if(right < sizes.width[RIGHT]) {
				right = sizes.width[RIGHT];
				left = size.x - gap() - right;
			}
			place({ LEFT }, 0, left, size.y, visible);
			place({ RIGHT }, left + gap(), right, size.y, visible);
		} else {
			place({ LEFT, RIGHT }, 0, size.x, size.y, visible);
		}
		laidOut = size;
		laidOutVisible = visible;
		relayoutGrids = false;
	}

private:
	struct Item {
		dwt::Control* control;
		Column column;
		bool stretch;
		/** Its preferred size when the columns were last measured. */
		dwt::Point preferred;
	};

	struct Measured {
		LONG width[2] = { 0, 0 };
		LONG height[2] = { 0, 0 };
		size_t count[2] = { 0, 0 };
	};

	std::vector<Item> items;
	/** The groups' sizes, measured again only when something changed. */
	Measured cache;
	bool measured = false;
	bool relayoutGrids = true;
	/** The size the columns were last laid out at. */
	dwt::Point laidOut;
	/** The height of the tab that was in view then. */
	LONG laidOutVisible = 0;

	LONG gap() { return scale(groupGap); }

	/** The height of the scroller's view: as much of the columns as the tab shows at a time. */
	LONG visibleHeight() {
		RECT view {};
		::GetClientRect(::GetParent(handle()), &view);
		return view.bottom;
	}

	Measured measure() {
		if(measured) {
			return cache;
		}
		Measured result;
		for(auto& item: items) {
			fitFillColumns(item.control);
			const auto size = item.control->getPreferredSize();
			item.preferred = size;
			// A grid's FILL columns report only their fixed size, so a group's preferred width
			// leaves out its labels; a column is never narrower than what its options need.
			result.width[item.column] = std::max<LONG>({ result.width[item.column], size.x, scale(minColumnWidth) });
			result.height[item.column] += (result.count[item.column] ? gap() : 0) + size.y;
			++result.count[item.column];
		}
		cache = result;
		measured = true;
		return result;
	}

	bool twoColumns(const Measured& measured, LONG width) {
		return measured.count[LEFT] && measured.count[RIGHT] &&
			width >= measured.width[LEFT] + gap() + measured.width[RIGHT];
	}

	LONG singleHeight(const Measured& measured) {
		return measured.height[LEFT] + measured.height[RIGHT] +
			(measured.count[LEFT] && measured.count[RIGHT] ? gap() : 0);
	}

	/** Stack the groups of these columns, in order, from the top; stretching ones share what is left
	 * of the visible height. The groups were just measured. */
	void place(std::initializer_list<Column> columns, LONG x, LONG width, LONG height, LONG visible) {
		std::vector<std::pair<const Item*, LONG>> placed;
		LONG total = 0;
		size_t stretching = 0;
		for(auto column: columns) {
			for(const auto& item: items) {
				if(item.column != column) continue;
				const auto preferred = item.preferred.y;
				total += (placed.empty() ? 0 : gap()) + preferred;
				stretching += item.stretch ? 1 : 0;
				placed.emplace_back(&item, preferred);
			}
		}

		// Not to the bottom of columns made taller than the tab by the other column: the stretching
		// group's last options would be out of view, below a list grown to fill the space. A column
		// that fits ends at the bottom of the tab; one that doesn't is scrolled, unstretched.
		const auto target = visible > 0 ? std::min(height, std::max(total, visible)) : height;
		auto extra = std::max<LONG>(0, target - total);
		LONG y = 0;
		for(auto& entry: placed) {
			auto itemHeight = entry.second;
			if(entry.first->stretch && stretching) {
				const auto share = extra / static_cast<LONG>(stretching--);
				itemHeight += share;
				extra -= share;
			}
			// A group whose size changes lays out its grids itself; one whose size stays the same
			// must be told to when its contents changed.
			const auto before = entry.first->control->getWindowSize();
			entry.first->control->resize(dwt::Rectangle(x, y, width, itemHeight));
			if(relayoutGrids && before.x == width && before.y == itemHeight) {
				layoutGrids(entry.first->control);
			}
			y += itemHeight + gap();
		}
	}
};

ExperimentalTab::ExperimentalTab(TabViewPtr parent, const tstring& title, unsigned helpId, LiveSettings& live_) :
	dwt::Container(parent),
	live(live_),
	scroller(nullptr),
	columns(nullptr)
{
	dwt::Container::Seed seed;
	seed.caption = title;
	seed.style &= ~WS_VISIBLE;
	seed.exStyle |= WS_EX_CONTROLPARENT;
	create(seed);
	// As addChild would: otherwise the tab paints in the system colours until a posted message
	// gives it the application's, and the first tab is painted before that.
	finalizeAppearance();
	setHelpId(helpId);

	scroller = addChild(dwt::ScrolledContainer::Seed(0, WS_EX_CONTROLPARENT));
	scroller->setHelpId(helpId);
	scroller->setAccessibleName(title);
	columns = new Columns(scroller);
	columns->setHelpId(helpId);

	onVisibilityChanged([this](bool visible) {
		if(!visible || (!built && !buildOnShow)) {
			return;
		}
		ensureBuilt();
		// Laid out here, not when shown: a page shown while its window holds its painting gets
		// no WM_WINDOWPOSCHANGED. When nothing changed, this does little.
		layout();
	});

	parent->add(this);
}

void ExperimentalTab::ensureBuilt() {
	if(built) {
		return;
	}
	built = true;
	build();
	columns->invalidate(true);
	if(builtCallback) {
		builtCallback();
	}
	// Controls made with addChild have the application's font and colours at once, so the tab is
	// measured right away; contents filled in later must not change a group's size (an empty
	// label is a line high only once it has text: start it with a space).
	// A tab built in the background is laid out when it is shown. The tab in front is laid out
	// now, also while its window holds its painting (and so counts as hidden).
	if(hasStyle(WS_VISIBLE)) {
		layout();
	}
}

void ExperimentalTab::appearanceChanged() {
	dwt::Container::appearanceChanged();
	queueRemeasure();
}

void ExperimentalTab::queueRemeasure() {
	if(!built || remeasureQueued) {
		return;
	}
	remeasureQueued = true;
	callAsync([this] {
		remeasureQueued = false;
		columns->invalidate(true);
		// Also while hidden, at the size it will be shown at, so showing it needs no layout.
		if(getClientSize().x > 0) {
			layout();
		}
	});
}

void ExperimentalTab::invalidateLayout(bool grids) {
	columns->invalidate(grids);
}

void ExperimentalTab::layout() {
	// A tab is laid out once it has its controls; until then there is nothing to place.
	if(!built) {
		return;
	}
	// A new application font or DPI changes the size of every option.
	const auto font = UiFonts::font ? UiFonts::font->handle() : nullptr;
	const auto dpi = getDpi();
	if(font != measuredFont || dpi != measuredDpi) {
		measuredFont = font;
		measuredDpi = dpi;
		columns->invalidate(true);
	}
	const auto size = getClientSize();
	const dwt::Rectangle area(7, 4, std::max<LONG>(0, size.x - 14), std::max<LONG>(0, size.y - 8));
	const auto current = scroller->getWindowSize();
	if(current.x != area.width() || current.y != area.height()) {
		// The scroller lays itself out, and its columns, when its size changes.
		scroller->resize(area);
	} else {
		// Content can change without the scroller moving (fonts, DPI, themes).
		scroller->layout();
	}
	// The columns are laid out again when their contents changed but their size didn't; when
	// nothing changed this does nothing.
	columns->layout();
}

GroupBoxPtr ExperimentalTab::addGroup(Column column, const tstring& title, unsigned helpId, bool stretch) {
	auto group = columns->addChild(GroupBox::Seed(title));
	if(helpId) {
		group->setHelpId(helpId);
	}
	columns->add(group, column, stretch);
	return group;
}

CheckBoxPtr ExperimentalTab::addCheck(GridPtr parent, const tstring& text, SettingsManager::BoolSetting setting, unsigned helpId) {
	auto box = parent->addChild(CheckBox::Seed(text));
	if(helpId) {
		box->setHelpId(helpId);
	}
	live.addBool(box, setting);
	return box;
}

TextBoxPtr ExperimentalTab::addInt(GridPtr parent, const tstring& text, SettingsManager::IntSetting setting,
	unsigned helpId, const tstring& unit, int minimum, int maximum, int multiplier)
{
	auto row = parent->addChild(Grid::Seed(1, 4));
	row->column(0).mode = GridInfo::FILL;
	row->column(1).size = 72;
	row->column(1).mode = GridInfo::STATIC;
	auto label = row->addChild(Label::Seed(text));
	label->setHelpId(helpId);
	// The label's column stretches; at its own width it still counts in the row's preferred width.
	row->column(0).size = static_cast<size_t>(label->getPreferredSize().x);
	auto box = row->addChild(ControlSeeds::Dialog::intTextBox);
	box->setHelpId(helpId);
	box->setAccessibleName(text);
	auto spin = row->addChild(Spinner::Seed(minimum, maximum == noLimit ? UD_MAXVAL : maximum, box));
	row->setWidget(spin);
	spin->setHelpId(helpId);
	row->addChild(Label::Seed(unit))->setHelpId(helpId);
	live.addInt(box, setting, minimum, maximum, multiplier);
	return box;
}

ChatSharingTab::ChatSharingTab(TabViewPtr parent, LiveSettings& live) :
	ExperimentalTab(parent, T_("Chat and sharing"), IDH_EXPERIMENTALPAGE, live),
	tempShares(nullptr),
	tempSummary(nullptr),
	removeTemp(nullptr),
	clearTemps(nullptr)
{
}

void ChatSharingTab::build() {
	{
		auto group = addGroup(LEFT, T_("Rich text and temporary shares"), IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARES);
		auto cur = group->addChild(Grid::Seed(5, 1));
		cur->column(0).mode = GridInfo::FILL;

		auto checks = cur->addChild(Grid::Seed(3, 1));
		checks->column(0).mode = GridInfo::FILL;
		addCheck(checks, T_("Enable ADC RTF0 CommonMark chat"), SettingsManager::ENABLE_RICH_TEXT,
			IDH_SETTINGS_EXPERIMENTAL_ENABLE_RICH_TEXT);
		addCheck(checks, T_("Enable temporary file sharing in chats"), SettingsManager::ENABLE_RTF_TEMP_SHARES,
			IDH_SETTINGS_EXPERIMENTAL_ENABLE_TEMP_SHARES);
		addCheck(checks, T_("Insert dropped image files as inline media"), SettingsManager::RTF_DROPPED_IMAGES_INLINE,
			IDH_SETTINGS_EXPERIMENTAL_DROPPED_IMAGES_INLINE);

		addInt(cur, T_("Rich text message size limit"), SettingsManager::RICH_TEXT_MAX_SIZE,
			IDH_SETTINGS_EXPERIMENTAL_RICH_TEXT_MAX_SIZE, T_("KiB"), 1, MAX_KIB_SETTING, BYTES_PER_KIB);
		addInt(cur, T_("Clickable chat link length limit"), SettingsManager::CHAT_LINK_MAX_LENGTH,
			IDH_SETTINGS_EXPERIMENTAL_CHAT_LINK_MAX_LENGTH, T_("characters"), 1, noLimit);
		addInt(cur, T_("Maximum temporary shares"), SettingsManager::RTF_TEMP_SHARE_LIMIT,
			IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARE_LIMIT, T_("files"), 1, 10000);
		addInt(cur, T_("Automatically remove temporary shares after"), SettingsManager::RTF_TEMP_SHARE_EXPIRATION,
			IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARE_EXPIRATION, T_("minutes (0 = never)"), 0, noLimit);
	}

	{
		auto group = addGroup(RIGHT, T_("Active temporary shares"), IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARES, true);
		auto cur = group->addChild(Grid::Seed(3, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->row(0).mode = GridInfo::FILL;
		cur->row(0).size = cur->scale(110);
		cur->row(0).align = GridInfo::STRETCH;
		// The group box supplies the themed frame.
		auto tableSeed = ControlSeeds::Dialog::table;
		tableSeed.exStyle &= ~WS_EX_CLIENTEDGE;
		tempShares = cur->addChild(tableSeed);
		tempShares->setHelpId(IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARES);
		TableLayout::makeColumns(tempShares, tempColumns, 5);
		tempShares->onGetEmptyText([] { return T_("No temporary attachment shares are active"); });
		tempShares->onSelectionChanged([this] { handleTempSelectionChanged(); });
		tempShares->onContextMenu([this](dwt::ScreenCoordinate pt) { return handleTempContextMenu(pt); });

		auto buttons = cur->addChild(Grid::Seed(1, 4));
		buttons->column(3).mode = GridInfo::FILL;
		removeTemp = buttons->addChild(Button::Seed(T_("&Remove selected")));
		removeTemp->onClicked([this] { handleRemoveTemps(); });
		clearTemps = buttons->addChild(Button::Seed(T_("&Clear all")));
		clearTemps->onClicked([this] { handleClearTemps(); });
		buttons->addChild(Button::Seed(T_("Re&fresh")))->onClicked([this] { refresh(); });
		tempSummary = cur->addChild(Label::Seed(tstring()));
	}

	refresh();
}

void ChatSharingTab::layout() {
	ExperimentalTab::layout();
	if(tempShares) {
		tempShares->setColumnWidth(4, std::max<LONG>(180, tempShares->getWindowSize().x - 610));
	}
}

void ChatSharingTab::refresh() {
	if(!tempShares) {
		return;
	}
	// Asked each time the window is shown: the list is filled again only when its rows changed, so
	// the selection and scroll position stay.
	std::vector<std::vector<tstring>> rows;
	std::vector<std::unique_ptr<TempShareRow>> rowData;
	const auto shares = ShareManager::getInstance()->getTempShares();
	for(const auto& share: shares) {
		bool available = false;
		try {
			File file(share.realPath, File::READ, File::OPEN | File::SHARED);
			available = file.getSize() == share.size && file.getLastModified() == share.timestamp;
		} catch(...) {
		}
		auto name = Text::toT(Util::getFileName(share.realPath));
		if(!available) name += T_(" (missing or changed)");
		auto data = std::make_unique<TempShareRow>(TempShareRow {
			share.realPath, share.hubUrl, share.tth.toBase32(), share.recipient
		});
		auto route = Text::toT(share.hubUrl);
		if(share.recipient) {
			// Shared in a private chat: only this user can download it.
			const auto nicks = ClientManager::getInstance()->getNicks(share.recipient);
			route += _T(" (") + (nicks.empty() ? T_("private chat") :
				str(TF_("private chat with %1%") % Text::toT(nicks.front()))) + _T(")");
		}
		rows.push_back({ name, Text::toT(Util::formatBytes(share.size)), route,
			Text::toT(data->tth), Text::toT(share.realPath) });
		rowData.push_back(std::move(data));
	}
	if(tempSharesListed && rows == listedTempShares) {
		return;
	}
	tempShares->clear();
	tempShareRows = std::move(rowData);
	for(size_t i = 0; i < rows.size(); ++i) {
		tempShares->insert(rows[i], reinterpret_cast<LPARAM>(tempShareRows[i].get()));
	}
	listedTempShares = std::move(rows);
	tempSharesListed = true;
	tempSummary->setText(T_("Active temporary shares: ") + Text::toT(std::to_string(shares.size())));
	handleTempSelectionChanged();
}

ChatSharingTab::TempShareRow* ChatSharingTab::getTempShareRow(int row) {
	return reinterpret_cast<TempShareRow*>(tempShares->getData(row));
}

void ChatSharingTab::handleTempSelectionChanged() {
	removeTemp->setEnabled(tempShares->hasSelected());
	clearTemps->setEnabled(tempShares->size() != 0);
}

bool ChatSharingTab::handleTempContextMenu(dwt::ScreenCoordinate pt) {
	if(pt.x() == -1 && pt.y() == -1) {
		pt = tempShares->getContextMenuPos();
	}

	const auto selected = tempShares->countSelected();
	auto menu = addChild(ControlSeeds::menu);
	menu->setTitle(selected == 1 ?
		dwt::util::escapeMenu(tempShares->getText(tempShares->getSelected(), 0)) :
		T_("Active temporary shares"), IconManager::menuIcon(IDI_MAGNET));
	menu->appendItem(T_("Copy magnet link to clipboard"), [this] { handleCopyTempMagnet(); },
		IconManager::menuIcon(IDI_MAGNET), selected == 1);
	menu->appendItem(T_("&Remove"), [this] { handleRemoveTemps(); }, dwt::IconPtr(), selected != 0);
	menu->open(pt);
	return true;
}

void ChatSharingTab::handleCopyTempMagnet() {
	const auto row = tempShares->getSelected();
	if(row < 0) return;

	try {
		const auto data = getTempShareRow(row);
		if(!data) return;
		const TTHValue tth(data->tth);
		const auto shares = ShareManager::getInstance()->getTempShares();
		const auto share = std::find_if(shares.begin(), shares.end(), [&](const ShareManager::TempShareInfo& item) {
			return item.tth == tth && Util::stricmp(item.realPath, data->path) == 0 &&
				Util::stricmp(item.hubUrl, data->route) == 0 && item.recipient == data->recipient;
		});
		if(share != shares.end()) {
			LinkActions::copyMagnet(share->tth, Text::toT(Util::getFileName(share->realPath)), share->size);
		}
	} catch(...) {
	}
}

void ChatSharingTab::handleRemoveTemps() {
	for(int row = tempShares->getNext(-1, LVNI_SELECTED); row != -1;
		row = tempShares->getNext(row, LVNI_SELECTED))
	{
		try {
			const auto data = getTempShareRow(row);
			if(data) {
				ShareManager::getInstance()->removeTempShare(data->path,
					TTHValue(data->tth), data->route, data->recipient);
			}
		} catch(...) {
		}
	}
	refresh();
}

void ChatSharingTab::handleClearTemps() {
	if(dwt::MessageBox(this).show(T_("Remove all active temporary attachment shares and cancel pending preparations?"),
		_T(APPNAME) _T(" ") _T(VERSIONSTRING), dwt::MessageBox::BOX_YESNO,
		dwt::MessageBox::BOX_ICONQUESTION) != IDYES) return;
	ShareManager::getInstance()->clearTempShares();
	refresh();
}

TransfersTab::TransfersTab(TabViewPtr parent, LiveSettings& live) :
	ExperimentalTab(parent, T_("Transfers and hashing"), IDH_EXPERIMENTALPAGE, live),
	hashDbStatus(nullptr),
	hashDbCallbackToken(std::make_shared<int>(0)),
	hashDbMaintenanceRunning(false)
{
}

void TransfersTab::build() {
	{
		auto group = addGroup(LEFT, T_("File lists"));
		auto cur = group->addChild(Grid::Seed(7, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Largest file list to download"), SettingsManager::MAX_FILELIST_DOWNLOAD_SIZE,
			IDH_SETTINGS_EXPERIMENTAL_MAX_FILELIST_DOWNLOAD_SIZE, T_("MiB (0 = no limit)"), 0, noLimit);
		addInt(cur, T_("Largest file list to open (XML)"), SettingsManager::MAX_FILELIST_SIZE,
			IDH_SETTINGS_EXPERT_MAX_FILELIST_SIZE, T_("MiB (0 = no limit)"), 0, noLimit);
		addInt(cur, T_("Generated partial file list limit"), SettingsManager::MAX_PARTIAL_LIST_BYTES,
			IDH_SETTINGS_EXPERIMENTAL_MAX_PARTIAL_LIST_BYTES, T_("KiB"), 1, MAX_KIB_SETTING, BYTES_PER_KIB);
		addInt(cur, T_("Received partial file list limit"), SettingsManager::MAX_RECEIVED_PARTIAL_LIST_BYTES,
			IDH_SETTINGS_EXPERIMENTAL_MAX_RECEIVED_PARTIAL_LIST_BYTES, T_("KiB"), 1, MAX_KIB_SETTING, BYTES_PER_KIB);
		addCheck(cur, T_("Prefer Zstandard over zlib when the peer supports both"), SettingsManager::PREFER_ZSTD);
		addCheck(cur, T_("Cache file lists on disk and load file entries on demand"), SettingsManager::FILELIST_CACHE);
		cur->addChild(Label::Seed(T_("The XML limit is also under Settings > Advanced > Experts only.")))
			->setHelpId(IDH_SETTINGS_EXPERT_MAX_FILELIST_SIZE);
	}

	{
		auto group = addGroup(LEFT, T_("Multi-connection transfers (MCN)"));
		auto cur = group->addChild(Grid::Seed(2, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Maximum regular download connections per user"), SettingsManager::MAX_MCN_DOWNLOADS,
			IDH_SETTINGS_EXPERIMENTAL_MCN_DOWNLOADS, T_("connections"), 1, 100);
		addInt(cur, T_("Maximum regular upload connections per user"), SettingsManager::MAX_MCN_UPLOADS,
			IDH_SETTINGS_EXPERIMENTAL_MCN_UPLOADS, T_("connections"), 1, 100);
	}

	{
		auto group = addGroup(RIGHT, T_("Hashing and share database"));
		auto cur = group->addChild(Grid::Seed(7, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Maximum hash speed"), SettingsManager::MAX_HASH_SPEED,
			IDH_SETTINGS_EXPERIMENTAL_MAX_HASH_SPEED, T_("MiB/s per drive (0 = unlimited)"), 0, noLimit);
		addInt(cur, T_("Drives hashed at the same time"), SettingsManager::MAX_HASH_VOLUMES,
			IDH_SETTINGS_EXPERIMENTAL_MAX_HASH_VOLUMES, T_("drives"),
			SettingsManager::MAX_HASH_VOLUMES_MIN, SettingsManager::MAX_HASH_VOLUMES_MAX);
		addInt(cur, T_("Hashing worker threads"), SettingsManager::HASHING_THREADS,
			IDH_SETTINGS_EXPERIMENTAL_HASHING_THREADS, T_("threads"),
			SettingsManager::HASHING_THREADS_MIN, SettingsManager::HASHING_THREADS_MAX);
		addInt(cur, T_("Hash database write batch size"), SettingsManager::HASH_DB_WRITE_BATCH_SIZE,
			IDH_SETTINGS_EXPERIMENTAL_HASH_BATCH_SIZE, T_("statements"), 1, noLimit);

		auto checks = cur->addChild(Grid::Seed(4, 1));
		checks->column(0).mode = GridInfo::FILL;
		addCheck(checks, T_("Report each hashed file in the system log"), SettingsManager::LOG_HASHED_FILES,
			IDH_SETTINGS_ADVANCED_LOG_HASHED_FILES);
		addCheck(checks, T_("Verify hash database on startup"), SettingsManager::HASH_DB_VERIFY_STARTUP,
			IDH_SETTINGS_EXPERIMENTAL_HASH_VERIFY_STARTUP);
		addCheck(checks, T_("Compact hash database after rebuild"), SettingsManager::HASH_DB_COMPACT_ON_REBUILD,
			IDH_SETTINGS_EXPERIMENTAL_HASH_COMPACT_REBUILD);
		addCheck(checks, T_("Use cached share tree on startup"), SettingsManager::SHARE_CACHE,
			IDH_SETTINGS_EXPERIMENTAL_SHARE_CACHE);

		// The empty last column takes the spare width, so the buttons keep their own.
		auto buttons = cur->addChild(Grid::Seed(2, 5));
		buttons->column(4).mode = GridInfo::FILL;
		buttons->addChild(Button::Seed(T_("&Verify")))->onClicked([this] { handleVerifyHashDb(false); });
		buttons->addChild(Button::Seed(T_("Full &check")))->onClicked([this] { handleVerifyHashDb(true); });
		buttons->addChild(Button::Seed(T_("&Optimize")))->onClicked([this] { handleOptimizeHashDb(); });
		buttons->addChild(Button::Seed(T_("&Compact")))->onClicked([this] { handleCompactHashDb(); });
		hashDbStatus = buttons->addChild(Label::Seed(_T(" ")));
		buttons->setWidget(hashDbStatus, 1, 0, 1, 5);
	}
}

TransfersTab::~TransfersTab() {
	hashDbCallbackToken.reset();
}

void TransfersTab::handleVerifyHashDb(bool fullCheck) {
	if(hashDbMaintenanceRunning) return;
	setHashDbMaintenanceRunning(true, fullCheck ?
		T_("Checking the hash database...") : T_("Verifying the hash database..."));
	HashManager::getInstance()->verifyHashStoreAsync(fullCheck,
		hashDbCompletion(T_("Hash database check passed"),
			T_("Hash database check failed; see the system log for details")));
}

void TransfersTab::handleOptimizeHashDb() {
	if(hashDbMaintenanceRunning) return;
	setHashDbMaintenanceRunning(true, T_("Optimizing the hash database..."));
	HashManager::getInstance()->optimizeHashStoreAsync(hashDbCompletion(T_("Hash database optimized")));
}

void TransfersTab::handleCompactHashDb() {
	if(hashDbMaintenanceRunning) return;
	if(dwt::MessageBox(this).show(T_("Compact the hash database now?"), _T(APPNAME) _T(" ") _T(VERSIONSTRING),
		dwt::MessageBox::BOX_YESNO, dwt::MessageBox::BOX_ICONQUESTION) != IDYES) return;
	setHashDbMaintenanceRunning(true, T_("Compacting the hash database..."));
	HashManager::getInstance()->compactHashStoreAsync(hashDbCompletion(T_("Hash database compacted")));
}

void TransfersTab::setHashDbMaintenanceRunning(bool running, const tstring& status) {
	// The buttons stay enabled: disabling the one with the focus can make dialog navigation
	// (IsDialogMessage) spin in WM_GETDLGCODE. The flag stops a second operation instead.
	hashDbMaintenanceRunning = running;
	hashDbStatus->setText(status.empty() ? _T(" ") : status);
}

std::function<void (bool, const string&)> TransfersTab::hashDbCompletion(
	const tstring& successMessage, const tstring& failureMessage)
{
	const auto tabHandle = handle();
	std::weak_ptr<int> callbackToken(hashDbCallbackToken);
	return [tabHandle, callbackToken, successMessage, failureMessage](bool success, const string& error) {
		dwt::Application::instance().callAsync([tabHandle, callbackToken, successMessage, failureMessage, success, error] {
			if(callbackToken.expired() || !::IsWindow(tabHandle)) return;
			auto tab = dwt::hwnd_cast<TransfersTab*>(tabHandle);
			if(!tab) return;

			tab->setHashDbMaintenanceRunning(false);
			const auto failed = !error.empty() || !success;
			const auto message = !error.empty() ? Text::toT(error) :
				(success ? successMessage : failureMessage);
			dwt::MessageBox(tab).show(message, _T(APPNAME) _T(" ") _T(VERSIONSTRING),
				dwt::MessageBox::BOX_OK, !error.empty() ? dwt::MessageBox::BOX_ICONSTOP :
					(failed ? dwt::MessageBox::BOX_ICONEXCLAMATION : dwt::MessageBox::BOX_ICONINFORMATION));
		});
	};
}

PrivateSendTab::PrivateSendTab(TabViewPtr parent, LiveSettings& live) :
	ExperimentalTab(parent, T_("Private Send"), IDH_EXPERIMENTALPAGE, live)
{
}

void PrivateSendTab::build() {
	{
		auto group = addGroup(LEFT, T_("Private Send (experimental PFT1)"));
		auto cur = group->addChild(Grid::Seed(4, 1));
		cur->column(0).mode = GridInfo::FILL;
		addCheck(cur, T_("Enable Private Send (requires TLS 1.2+)"), SettingsManager::ENABLE_PRIVATE_SEND);
		addCheck(cur, T_("Require contact verification (off for testing)"), SettingsManager::PFT_REQUIRE_VERIFICATION);
		cur->addChild(Label::Seed(T_("Use Private Send in a private chat's menu.\r\nTesting mode skips contact verification on this client.")));
		cur->addChild(Label::Seed(
			T_("Disabling revokes transfers. Received files stay in a protected, unshared folder;\r\nfiles never open automatically.")));
	}

	{
		auto group = addGroup(RIGHT, T_("Time limits"));
		auto cur = group->addChild(Grid::Seed(2, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Offer expires after"), SettingsManager::PFT_OFFER_MINUTES, IDH_EXPERIMENTALPAGE, T_("minutes"), 1, 1440);
		addInt(cur, T_("Accepted transfer expires after"), SettingsManager::PFT_TRANSFER_MINUTES, IDH_EXPERIMENTALPAGE,
			T_("minutes"), 1, 10080);
	}
}

ProtocolLimitsTab::ProtocolLimitsTab(TabViewPtr parent, LiveSettings& live) :
	ExperimentalTab(parent, T_("Protocol limits"), IDH_EXPERIMENTALPAGE, live)
{
}

void ProtocolLimitsTab::build() {
	{
		auto group = addGroup(LEFT, T_("Direct private-message channels (CCPM)"), IDH_SETTINGS_EXPERIMENTAL_CCPM_RECONNECT_BASE_DELAY);
		auto cur = group->addChild(Grid::Seed(4, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Initial automatic reconnect delay"), SettingsManager::CCPM_RECONNECT_BASE_DELAY,
			IDH_SETTINGS_EXPERIMENTAL_CCPM_RECONNECT_BASE_DELAY, T_("seconds"),
			SettingsManager::CCPM_RECONNECT_DELAY_MIN, SettingsManager::CCPM_RECONNECT_DELAY_MAX);
		addInt(cur, T_("Maximum automatic reconnect delay"), SettingsManager::CCPM_RECONNECT_MAX_DELAY,
			IDH_SETTINGS_EXPERIMENTAL_CCPM_RECONNECT_MAX_DELAY, T_("seconds"),
			SettingsManager::CCPM_RECONNECT_DELAY_MIN, SettingsManager::CCPM_RECONNECT_DELAY_MAX);
		addInt(cur, T_("Stable connection reset time"), SettingsManager::CCPM_STABLE_CONNECTION_TIME,
			IDH_SETTINGS_EXPERIMENTAL_CCPM_STABLE_CONNECTION_TIME, T_("seconds"),
			SettingsManager::CCPM_STABLE_CONNECTION_TIME_MIN, SettingsManager::CCPM_STABLE_CONNECTION_TIME_MAX);
		addInt(cur, T_("Maximum automatic reconnect attempts"), SettingsManager::CCPM_MAX_AUTOMATIC_ATTEMPTS,
			IDH_SETTINGS_EXPERIMENTAL_CCPM_MAX_AUTOMATIC_ATTEMPTS, T_("attempts"),
			SettingsManager::CCPM_AUTOMATIC_ATTEMPTS_MIN, SettingsManager::CCPM_AUTOMATIC_ATTEMPTS_MAX);
	}

	{
		auto group = addGroup(LEFT, T_("Hub authentication"), IDH_SETTINGS_EXPERIMENTAL_ADC_MIN_SALT_LENGTH);
		auto cur = group->addChild(Grid::Seed(1, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Minimum ADC password challenge length"), SettingsManager::ADC_MIN_SALT_LENGTH,
			IDH_SETTINGS_EXPERIMENTAL_ADC_MIN_SALT_LENGTH, T_("characters"),
			SettingsManager::ADC_MIN_SALT_LENGTH_MIN, SettingsManager::ADC_MIN_SALT_LENGTH_MAX);
	}

	{
		auto group = addGroup(LEFT, T_("NMDC hubs"), IDH_SETTINGS_EXPERIMENTAL_NMDC_ENCODING);
		auto cur = group->addChild(Grid::Seed(2, 2));
		cur->column(0).mode = GridInfo::FILL;
		cur->addChild(Label::Seed(T_("Default encoding for NMDC hubs")))->setHelpId(IDH_SETTINGS_EXPERIMENTAL_NMDC_ENCODING);
		auto encoding = cur->addChild(ControlSeeds::Dialog::comboBox);
		encoding->setHelpId(IDH_SETTINGS_EXPERIMENTAL_NMDC_ENCODING);
		encoding->setAccessibleName(T_("Default encoding for NMDC hubs"));
		live.addCombo(encoding, SettingsManager::NMDC_ENCODING, [encoding] {
			const auto stored = SETTING(NMDC_ENCODING);
			const auto shown = Encodings::selected(encoding);
			if(encoding->size() == 0 || shown.empty() != stored.empty() ||
				(!stored.empty() && Text::getCodePage(shown) != Text::getCodePage(stored)))
			{
				encoding->clear();
				Encodings::fill(encoding, T_("Windows default"), stored);
			}
		}, [encoding] {
			const auto value = Encodings::selected(encoding);
			if(value == SETTING(NMDC_ENCODING)) return false;
			SettingsManager::getInstance()->set(SettingsManager::NMDC_ENCODING, value);
			return true;
		});
		auto note = cur->addChild(Label::Seed(T_("Favorite hubs can choose their own encoding.\r\nChoose 65001 (UTF-8) for hubs that use UTF-8.")));
		note->setHelpId(IDH_SETTINGS_EXPERIMENTAL_NMDC_ENCODING);
		cur->setWidget(note, 1, 0, 1, 2);
	}

	{
		auto group = addGroup(RIGHT, T_("Protocol resource limits"));
		auto cur = group->addChild(Grid::Seed(11, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Queued protocol data limit"), SettingsManager::MAX_QUEUED_PROTOCOL_DATA,
			IDH_SETTINGS_EXPERIMENTAL_MAX_QUEUED_PROTOCOL_DATA, T_("KiB"), 1, MAX_KIB_SETTING, BYTES_PER_KIB);
		addInt(cur, T_("Concurrent peer connection limit"), SettingsManager::MAX_CONCURRENT_CONNECTIONS,
			IDH_SETTINGS_EXPERIMENTAL_MAX_CONCURRENT_CONNECTIONS, T_("connections"), 1, noLimit);
		addInt(cur, T_("Incoming connection flood window"), SettingsManager::FLOOD_WINDOW,
			IDH_SETTINGS_EXPERIMENTAL_FLOOD_WINDOW, T_("milliseconds"), 1, noLimit);
		addInt(cur, T_("Global UDP rate window"), SettingsManager::GLOBAL_WINDOW,
			IDH_SETTINGS_EXPERIMENTAL_GLOBAL_WINDOW, T_("milliseconds"), 1, noLimit);
		addInt(cur, T_("Global UDP packet limit"), SettingsManager::GLOBAL_LIMIT,
			IDH_SETTINGS_EXPERIMENTAL_GLOBAL_LIMIT, T_("packets per window"), 1, noLimit);
		addInt(cur, T_("Per-peer UDP packet limit"), SettingsManager::PEER_LIMIT,
			IDH_SETTINGS_EXPERIMENTAL_PEER_LIMIT, T_("packets per window"), 1, noLimit);
		addInt(cur, T_("Per-peer UDP rate window"), SettingsManager::PEER_WINDOW,
			IDH_SETTINGS_EXPERIMENTAL_PEER_WINDOW, T_("milliseconds"), 1, noLimit);
		addInt(cur, T_("Tracked UDP peer limit"), SettingsManager::MAX_TRACKED_PEERS,
			IDH_SETTINGS_EXPERIMENTAL_MAX_TRACKED_PEERS, T_("peers"), 1, noLimit);
		addInt(cur, T_("Encrypted UDP packet limit"), SettingsManager::MAX_SUDP_PACKET,
			IDH_SETTINGS_EXPERIMENTAL_MAX_SUDP_PACKET, T_("bytes"), 32, noLimit);
		addInt(cur, T_("Stored encrypted UDP key limit"), SettingsManager::MAX_SUDP_KEYS,
			IDH_SETTINGS_EXPERIMENTAL_MAX_SUDP_KEYS, T_("keys"), 1, noLimit);
		addInt(cur, T_("BBS boards per hub limit"), SettingsManager::BBS_MAX_BOARDS,
			IDH_SETTINGS_EXPERIMENTAL_BBS_MAX_BOARDS, T_("boards"),
			SettingsManager::BBS_MAX_BOARDS_MIN, SettingsManager::BBS_MAX_BOARDS_MAX);
	}
}

OtherTab::OtherTab(TabViewPtr parent, LiveSettings& live) :
	ExperimentalTab(parent, T_("Other"), IDH_EXPERIMENTALPAGE, live),
	lockState(nullptr),
	lockSet(nullptr),
	lockRemove(nullptr)
{
}

void OtherTab::build() {
	{
		auto group = addGroup(LEFT, T_("Offline message outbox"));
		auto cur = group->addChild(Grid::Seed(5, 1));
		cur->column(0).mode = GridInfo::FILL;
		addCheck(cur, T_("Keep messages for offline users and send them later"), SettingsManager::ENABLE_MESSAGE_OUTBOX,
			IDH_SETTINGS_EXPERIMENTAL_ENABLE_OUTBOX);
		addInt(cur, T_("Maximum retained messages"), SettingsManager::OUTBOX_MAX_ENTRIES,
			IDH_EXPERIMENTALPAGE, T_("messages"), MessageOutbox::entryLimitMin, MessageOutbox::entryLimitMax);
		addInt(cur, T_("Maximum message size"), SettingsManager::OUTBOX_MAX_MESSAGE_BYTES,
			IDH_EXPERIMENTALPAGE, T_("KiB"), MessageOutbox::messageLimitMin / BYTES_PER_KIB,
			MessageOutbox::messageLimitMax / BYTES_PER_KIB, BYTES_PER_KIB);
		addInt(cur, T_("Pending messages expire after"), SettingsManager::OUTBOX_EXPIRY_DAYS,
			IDH_EXPERIMENTALPAGE, T_("days"), MessageOutbox::expiryDaysMin, MessageOutbox::expiryDaysMax);
		cur->addChild(Label::Seed(T_("Applies to new messages and retries.\r\nExisting messages keep their deadlines.")));
	}

	{
		auto group = addGroup(LEFT, T_("Hub user lists"));
		auto cur = group->addChild(Grid::Seed(2, 2));
		cur->column(0).mode = GridInfo::FILL;
		cur->addChild(Label::Seed(T_("Default user icon size")));
		auto iconSize = cur->addChild(ControlSeeds::Dialog::comboBox);
		iconSize->setAccessibleName(T_("Default hub user icon size"));
		for(auto size: HubSettings::userIconSizes) {
			iconSize->addValue(Text::toT(std::to_string(size)) + _T(" px"));
		}
		live.addCombo(iconSize, SettingsManager::HUB_USER_ICON_SIZE, [iconSize] {
			const auto& sizes = HubSettings::userIconSizes;
			const auto found = std::find(std::begin(sizes), std::end(sizes), SETTING(HUB_USER_ICON_SIZE));
			iconSize->setSelected(found == std::end(sizes) ? 0 : static_cast<int>(found - std::begin(sizes)));
		}, [iconSize] {
			const auto index = iconSize->getSelected();
			const auto size = index >= 0 && static_cast<size_t>(index) < std::size(HubSettings::userIconSizes) ?
				HubSettings::userIconSizes[index] : 16;
			if(size == SETTING(HUB_USER_ICON_SIZE)) return false;
			SettingsManager::getInstance()->set(SettingsManager::HUB_USER_ICON_SIZE, size);
			return true;
		});
		auto note = cur->addChild(Label::Seed(T_("Individual favorite hubs can override this size.\r\nSizes scale with display DPI.")));
		cur->setWidget(note, 1, 0, 1, 2);
	}

	{
		auto group = addGroup(LEFT, T_("System Log"), IDH_SETTINGS_EXPERIMENTAL_MAX_SYSTEM_LOG_ITEMS);
		auto cur = group->addChild(Grid::Seed(1, 1));
		cur->column(0).mode = GridInfo::FILL;
		addInt(cur, T_("Maximum System Log items"), SettingsManager::MAX_SYSTEM_LOG_ITEMS,
			IDH_SETTINGS_EXPERIMENTAL_MAX_SYSTEM_LOG_ITEMS, T_("messages"),
			LogManager::MIN_HISTORY_ITEMS, LogManager::MAX_HISTORY_ITEMS);
	}

	{
		auto group = addGroup(RIGHT, T_("Country database"));
		auto fields = group->addChild(Grid::Seed(6, 2));
		fields->column(1).mode = GridInfo::FILL;

		fields->addChild(Label::Seed(T_("Download URL")))->setHelpId(IDH_SETTINGS_EXPERIMENTAL_GEOIP_DOWNLOAD_URL);
		auto url = fields->addChild(ControlSeeds::Dialog::textBox);
		url->setHelpId(IDH_SETTINGS_EXPERIMENTAL_GEOIP_DOWNLOAD_URL);
		url->setAccessibleName(T_("Country database download URL"));
		live.addText(url, SettingsManager::GEOIP_DOWNLOAD_URL, [] {
			return Text::toT(SETTING(GEOIP_DOWNLOAD_URL));
		}, [](const tstring& text) {
			auto value = Text::fromT(text);
			Util::trim(value);
			if(value == SETTING(GEOIP_DOWNLOAD_URL)) return false;
			SettingsManager::getInstance()->set(SettingsManager::GEOIP_DOWNLOAD_URL, value);
			return true;
		});
		auto urlNote = fields->addChild(Label::Seed(T_("HTTPS link to a Country .mmdb or .tar.gz file.\r\nGitHub release assets are supported.")));
		urlNote->setHelpId(IDH_SETTINGS_EXPERIMENTAL_GEOIP_DOWNLOAD_URL);
		fields->setWidget(urlNote, 1, 0, 1, 2);

		auto enabled = fields->addChild(CheckBox::Seed(T_("Download directly from MaxMind (testing override)")));
		fields->setWidget(enabled, 2, 0, 1, 2);
		live.addBool(enabled, SettingsManager::GEOIP_USE_MAXMIND);

		fields->addChild(Label::Seed(T_("MaxMind account ID")));
		auto account = fields->addChild(ControlSeeds::Dialog::textBox);
		account->setAccessibleName(T_("MaxMind account ID"));
		live.addText(account, SettingsManager::MAXMIND_ACCOUNT_ID, [] {
			return Text::toT(SETTING(MAXMIND_ACCOUNT_ID));
		}, [](const tstring& text) {
			auto value = Text::fromT(text);
			Util::trim(value);
			if(value == SETTING(MAXMIND_ACCOUNT_ID)) return false;
			SettingsManager::getInstance()->set(SettingsManager::MAXMIND_ACCOUNT_ID, value);
			return true;
		});

		fields->addChild(Label::Seed(T_("License key")));
		auto key = fields->addChild(ControlSeeds::Dialog::textBox);
		key->setPassword();
		key->setAccessibleName(T_("MaxMind license key"));
		live.addText(key, SettingsManager::MAXMIND_LICENSE_KEY, [] {
			return Text::toT(GeoIPUpdate::unprotectKey(SETTING(MAXMIND_LICENSE_KEY)));
		}, [](const tstring& text) {
			auto value = Text::fromT(text);
			Util::trim(value);
			if(value == GeoIPUpdate::unprotectKey(SETTING(MAXMIND_LICENSE_KEY))) return false;
			// Stored encrypted for this Windows account.
			SettingsManager::getInstance()->set(SettingsManager::MAXMIND_LICENSE_KEY, GeoIPUpdate::protectKey(value));
			return true;
		});

		auto note = fields->addChild(Label::Seed(T_("The MaxMind login overrides the URL.\r\nThe key is protected for this Windows account.")));
		fields->setWidget(note, 5, 0, 1, 2);
	}

	{
		auto group = addGroup(RIGHT, T_("Password lock"), IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);
		auto cur = group->addChild(Grid::Seed(5, 1));
		cur->column(0).mode = GridInfo::FILL;

		auto row = cur->addChild(Grid::Seed(1, 3));
		row->column(0).mode = GridInfo::FILL;
		row->row(0).align = GridInfo::CENTER;
		lockState = row->addChild(Label::Seed(Util::emptyStringT));
		lockState->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);
		lockSet = row->addChild(Button::Seed(T_("Set password...")));
		lockSet->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);
		lockSet->onClicked([this] { handleSetLockPassword(); });
		lockRemove = row->addChild(Button::Seed(T_("Remove password...")));
		lockRemove->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);
		lockRemove->onClicked([this] { handleRemoveLockPassword(); });

		lockOptions.push_back(addCheck(cur, T_("Lock DC++ when it is minimized"), SettingsManager::LOCK_ON_MINIMIZE,
			IDH_SETTINGS_WINDOWS_LOCK_ON_MINIMIZE));
		lockOptions.push_back(addCheck(cur, T_("Lock DC++ when it starts"), SettingsManager::LOCK_ON_STARTUP,
			IDH_SETTINGS_WINDOWS_LOCK_ON_STARTUP));
		lockOptions.push_back(addCheck(cur, T_("Ask for the lock password before exiting"), SettingsManager::LOCK_PASSWORD_TO_EXIT,
			IDH_SETTINGS_WINDOWS_LOCK_PASSWORD_TO_EXIT));
		cur->addChild(Label::Seed(T_("Type /lock in any chat to lock DC++ straight away.")))
			->setHelpId(IDH_SETTINGS_WINDOWS_LOCK_PASSWORD);
	}

	updateLockControls();
}

void OtherTab::refresh() {
	updateLockControls();
}

void OtherTab::handleSetLockPassword() {
	const auto current = lockHash();
	const bool change = !current.empty();

	ParamDlg dlg(this, change ? T_("Change the lock password") : T_("Set a lock password"));
	if(change) {
		dlg.addTextBox(T_("Current password"), Util::emptyStringT, true);
	}
	dlg.addTextBox(T_("New password"), Util::emptyStringT, true);
	dlg.addTextBox(T_("Type the new password again"), Util::emptyStringT, true);
	if(dlg.run() != IDOK) {
		return;
	}

	const auto& values = dlg.getValues();
	size_t i = 0;
	if(change && !matchesLockPassword(values[i++], current)) {
		showLockError(T_("The current password is wrong."));
		return;
	}
	const auto& password = values[i];
	if(password.empty()) {
		showLockError(T_("The new password can't be empty."));
		return;
	}
	if(password != values[i + 1]) {
		showLockError(T_("The two new passwords don't match."));
		return;
	}

	auto utf8 = Text::fromT(password);
	auto hash = PasswordHash::create(utf8);
	::SecureZeroMemory(&utf8[0], utf8.size());
	if(hash.empty()) {
		showLockError(T_("The password could not be stored."));
		return;
	}

	SettingsManager::getInstance()->set(SettingsManager::LOCK_PASSWORD, hash);
	live.notify(SettingsManager::LOCK_PASSWORD);
	updateLockControls();
}

void OtherTab::handleRemoveLockPassword() {
	const auto current = lockHash();
	if(current.empty()) {
		return;
	}

	ParamDlg dlg(this, T_("Remove the lock password"), T_("Current password"), Util::emptyStringT, true);
	if(dlg.run() != IDOK) {
		return;
	}
	if(!matchesLockPassword(dlg.getValue(), current)) {
		showLockError(T_("The current password is wrong."));
		return;
	}

	SettingsManager::getInstance()->set(SettingsManager::LOCK_PASSWORD, string());
	live.notify(SettingsManager::LOCK_PASSWORD);
	updateLockControls();
}

void OtherTab::showLockError(const tstring& message) {
	dwt::MessageBox(this).show(message, T_("Password lock"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONEXCLAMATION);
}

void OtherTab::updateLockControls() {
	if(!lockState) {
		return;
	}
	const bool set = !lockHash().empty();
	lockState->setText(set ? T_("A lock password is set.") : T_("No lock password is set."));
	lockSet->setText(set ? T_("Change password...") : T_("Set password..."));
	lockRemove->setEnabled(set);

	auto settings = SettingsManager::getInstance();
	const SettingsManager::BoolSetting options[] = { SettingsManager::LOCK_ON_MINIMIZE,
		SettingsManager::LOCK_ON_STARTUP, SettingsManager::LOCK_PASSWORD_TO_EXIT };
	for(size_t i = 0; i < lockOptions.size(); ++i) {
		lockOptions[i]->setEnabled(set);
		// Without a password (or with a damaged one) the options are off, as the lock can't work.
		if(!set && settings->get(options[i])) {
			settings->set(options[i], false);
			lockOptions[i]->setChecked(false);
			live.notify(options[i]);
		}
	}
	if(!set && !SETTING(LOCK_PASSWORD).empty()) {
		settings->set(SettingsManager::LOCK_PASSWORD, string());
		live.notify(SettingsManager::LOCK_PASSWORD);
	}
}
