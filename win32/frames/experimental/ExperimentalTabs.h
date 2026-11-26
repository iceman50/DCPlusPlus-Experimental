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

#ifndef DCPLUSPLUS_WIN32_EXPERIMENTAL_TABS_H
#define DCPLUSPLUS_WIN32_EXPERIMENTAL_TABS_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <dcpp/settings/SettingsManager.h>
#include <dcpp/user/CID.h>

#include <dwt/widgets/Container.h>

#include <win32/forward.h>

class LiveSettings;

/**
 * A page of the Experimental window. Its options are in groups, laid out in two columns that
 * share the window's width; a window too narrow for both shows the left column's groups above
 * the right column's. The groups scroll when the window is too small for them.
 */
class ExperimentalTab : public dwt::Container {
public:
	enum Column { LEFT, RIGHT };

	ExperimentalTab(TabViewPtr parent, const tstring& title, unsigned helpId, LiveSettings& live);

	void layout() override;
	/** Fonts and colors changed: measure the groups again once every control has the new ones. */
	void appearanceChanged() override;

	/** Show again what may have changed elsewhere: settings not bound to LiveSettings, lists and
	 * statuses. Only called once the tab is built. */
	virtual void refresh() { }

	/** Create the tab's controls if they don't exist yet. A tab creates them when it is first shown,
	 * so the window opens with only the tab in front built. */
	void ensureBuilt();
	bool isBuilt() const { return built; }
	/** Build the tab when it is first shown from now on. Not from the constructor: adding the first
	 * tab to its tab view shows it before the derived tab exists. */
	void buildWhenShown() { buildOnShow = true; }
	/** Called once, when the controls have been created. */
	void onBuilt(std::function<void ()> f) { builtCallback = std::move(f); }
	/** Measure the groups again at the next layout, as their contents or fonts changed; `grids` also
	 * lays out every grid inside them again. */
	void invalidateLayout(bool grids = true);

	/** Space between groups and columns, before DPI scaling. */
	static constexpr int groupGap = 10;
	/** The narrowest a column gets, before DPI scaling: room for a label, a number field and its
	 * unit. A tab narrower than two of these shows one column. */
	static constexpr int minColumnWidth = 500;

protected:
	LiveSettings& live;

	/** Create the controls; see ensureBuilt. */
	virtual void build() = 0;

	/** A group in a column. A stretching group takes the height its column has left over. */
	GroupBoxPtr addGroup(Column column, const tstring& title, unsigned helpId = 0, bool stretch = false);
	CheckBoxPtr addCheck(GridPtr parent, const tstring& text, SettingsManager::BoolSetting setting, unsigned helpId = 0);
	/** A labelled number field with a spinner and a unit; see LiveSettings::addInt. */
	TextBoxPtr addInt(GridPtr parent, const tstring& text, SettingsManager::IntSetting setting, unsigned helpId,
		const tstring& unit, int minimum, int maximum, int multiplier = 1);

	/** A field without an upper limit; its spinner stops at the largest value it can show. */
	static constexpr int noLimit = 0x7fffffff;

private:
	class Columns;

	dwt::ScrolledContainerPtr scroller;
	Columns* columns;
	bool built = false;
	bool buildOnShow = false;
	/** The application font and DPI the groups were last measured with. */
	HFONT measuredFont = nullptr;
	unsigned measuredDpi = 0;
	bool remeasureQueued = false;

	/** Measure and lay out the groups again once no message waits: after a font change, controls
	 * receive the new application font one after another, after the tab is told. */
	void queueRemeasure();
	std::function<void ()> builtCallback;
};

/** Rich text chat and temporary shares, with the list of active temporary shares. */
class ChatSharingTab : public ExperimentalTab {
public:
	ChatSharingTab(TabViewPtr parent, LiveSettings& live);
	void layout() override;
	void refresh() override;

private:
	struct TempShareRow {
		string path;
		string route;
		string tth;
		CID recipient;
	};

	TablePtr tempShares;
	std::vector<std::unique_ptr<TempShareRow>> tempShareRows;
	/** The text of each row in the list, once it has been filled. */
	std::vector<std::vector<tstring>> listedTempShares;
	bool tempSharesListed = false;
	LabelPtr tempSummary;
	ButtonPtr removeTemp;
	ButtonPtr clearTemps;

	void build() override;
	TempShareRow* getTempShareRow(int row);
	void handleTempSelectionChanged();
	bool handleTempContextMenu(dwt::ScreenCoordinate pt);
	void handleCopyTempMagnet();
	void handleRemoveTemps();
	void handleClearTemps();
};

/** File list limits, compression, multi-connection transfers, hashing and the hash database. */
class TransfersTab : public ExperimentalTab {
public:
	TransfersTab(TabViewPtr parent, LiveSettings& live);
	virtual ~TransfersTab();

private:
	LabelPtr hashDbStatus;
	std::shared_ptr<int> hashDbCallbackToken;
	bool hashDbMaintenanceRunning;

	void build() override;
	void handleVerifyHashDb(bool fullCheck);
	void handleOptimizeHashDb();
	void handleCompactHashDb();
	void setHashDbMaintenanceRunning(bool running, const tstring& status = tstring());
	std::function<void (bool, const string&)> hashDbCompletion(const tstring& successMessage,
		const tstring& failureMessage = tstring());
};

/** Private Send (PFT1). */
class PrivateSendTab : public ExperimentalTab {
public:
	PrivateSendTab(TabViewPtr parent, LiveSettings& live);

private:
	void build() override;
};

/** Direct private-message channels, hub authentication, NMDC encoding and defensive limits. */
class ProtocolLimitsTab : public ExperimentalTab {
public:
	ProtocolLimitsTab(TabViewPtr parent, LiveSettings& live);

private:
	void build() override;
};

/** The offline message outbox, hub user icons, the System Log, the country database and the
 * password lock. */
class OtherTab : public ExperimentalTab {
public:
	OtherTab(TabViewPtr parent, LiveSettings& live);
	void refresh() override;

private:
	LabelPtr lockState;
	ButtonPtr lockSet;
	ButtonPtr lockRemove;
	std::vector<CheckBoxPtr> lockOptions;

	void build() override;
	void handleSetLockPassword();
	void handleRemoveLockPassword();
	void showLockError(const tstring& message);
	/** The lock options do nothing without a password, so they are offered only once one is set. */
	void updateLockControls();
};

#endif // !defined(DCPLUSPLUS_WIN32_EXPERIMENTAL_TABS_H)
