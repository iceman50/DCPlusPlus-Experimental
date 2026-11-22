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

#ifndef DCPLUSPLUS_WIN32_LIVE_SETTINGS_H
#define DCPLUSPLUS_WIN32_LIVE_SETTINGS_H

#include <functional>
#include <memory>
#include <vector>

#include <dcpp/settings/SettingsManager.h>

#include <win32/forward.h>

/**
 * Binds controls to settings and applies each change at once, without an OK button:
 * - a check box or drop-down when it changes;
 * - a number or text field after a pause in typing, on Enter, or when it loses the focus.
 *
 * A half-typed number never takes effect: after a pause, only a complete number within the
 * field's range is applied. Leaving the field (or Enter) applies the nearest value in range, or
 * shows the stored value again when the text is not a number.
 */
class LiveSettings {
public:
	/** Called after a setting changed, with its SettingsManager index. */
	typedef std::function<void (int setting)> ChangedF;

	explicit LiveSettings(ChangedF changed);
	~LiveSettings();

	LiveSettings(const LiveSettings&) = delete;
	LiveSettings& operator=(const LiveSettings&) = delete;

	void addBool(CheckBoxPtr box, SettingsManager::BoolSetting setting);

	/** A number field limited to [minimum, maximum], shown in units of `multiplier`: a setting
	 * stored in bytes and edited in KiB has a multiplier of 1024. */
	void addInt(TextBoxPtr box, SettingsManager::IntSetting setting, int minimum, int maximum, int multiplier = 1);

	/** A text field: `read` returns the text to show, `write` stores the text and returns whether
	 * the setting changed. */
	void addText(TextBoxPtr box, int setting, std::function<tstring ()> read,
		std::function<bool (const tstring&)> write);

	/** A drop-down: `read` selects the stored value, `write` stores the selection and returns
	 * whether the setting changed. */
	void addCombo(ComboBoxPtr box, int setting, std::function<void ()> read, std::function<bool ()> write);

	/** Show the stored values again after something else changed them. A field with an edit
	 * still waiting to be applied keeps its text. */
	void reload();

	/** Apply every field with an edit still waiting for its typing pause. */
	void commitAll();

	/** Report a setting that a control stored itself, without a binding here. */
	void notify(int setting);

	/** How long typing must pause before a field applies. */
	static constexpr unsigned typingPause = 700;

private:
	struct Field;

	void watchEdits(Field& field);
	void commit(Field& field, bool final);
	void read(Field& field);

	ChangedF changed;
	std::vector<std::unique_ptr<Field>> fields;
	bool loading = false;
	/** Cleared on destruction, so a control's late event can't reach a destroyed binding. */
	std::shared_ptr<bool> alive;
};

#endif // !defined(DCPLUSPLUS_WIN32_LIVE_SETTINGS_H)
