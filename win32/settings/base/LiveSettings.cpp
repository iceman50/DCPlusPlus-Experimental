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
#include <win32/settings/base/LiveSettings.h>

#include <algorithm>
#include <limits>

#include <dcpp/util/Util.h>
#include <dcpp/util/text/Text.h>

#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/TextBox.h>

struct LiveSettings::Field {
	enum Kind { BOOL, INT, TEXT, COMBO };

	Field(Kind kind_, int setting_, dwt::Control* control_) : kind(kind_), setting(setting_), control(control_) { }

	Kind kind;
	int setting;
	dwt::Control* control;

	int minimum = 0;
	int maximum = 0;
	int multiplier = 1;

	std::function<tstring ()> readText;
	std::function<bool (const tstring&)> writeText;
	std::function<void ()> readCombo;
	std::function<bool ()> writeCombo;

	/** The field was edited and the edit has not been applied yet. */
	bool pending = false;
};

namespace {

/** A stored value in the units a field shows, rounded up so that showing and applying a value
 * never lowers a limit (1000 bytes shows as 1 KiB, not 0). */
int64_t shownValue(int64_t stored, int multiplier) {
	return stored > 0 ? (stored + multiplier - 1) / multiplier : 0;
}

bool isNumber(const string& text) {
	// Twelve digits are far beyond any int setting, and well within int64_t.
	return !text.empty() && text.size() <= 12 &&
		std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

}

LiveSettings::LiveSettings(ChangedF changed_) : changed(std::move(changed_)), alive(std::make_shared<bool>(true)) {
}

LiveSettings::~LiveSettings() {
}

void LiveSettings::addBool(CheckBoxPtr box, SettingsManager::BoolSetting setting) {
	fields.push_back(std::make_unique<Field>(Field::BOOL, setting, box));
	auto& field = *fields.back();
	read(field);

	std::weak_ptr<bool> guard(alive);
	box->onClicked([this, guard, box, setting] {
		if(guard.expired() || loading) return;
		auto settings = SettingsManager::getInstance();
		const auto checked = box->getChecked();
		if(settings->get(setting) != checked) {
			settings->set(setting, checked);
			notify(setting);
		}
	});
}

void LiveSettings::addInt(TextBoxPtr box, SettingsManager::IntSetting setting, int minimum, int maximum, int multiplier) {
	fields.push_back(std::make_unique<Field>(Field::INT, setting, box));
	auto& field = *fields.back();
	field.minimum = minimum;
	field.maximum = maximum;
	field.multiplier = std::max(1, multiplier);
	read(field);
	watchEdits(field);
}

void LiveSettings::addText(TextBoxPtr box, int setting, std::function<tstring ()> read_,
	std::function<bool (const tstring&)> write_)
{
	fields.push_back(std::make_unique<Field>(Field::TEXT, setting, box));
	auto& field = *fields.back();
	field.readText = std::move(read_);
	field.writeText = std::move(write_);
	read(field);
	watchEdits(field);
}

void LiveSettings::addCombo(ComboBoxPtr box, int setting, std::function<void ()> read_, std::function<bool ()> write_) {
	fields.push_back(std::make_unique<Field>(Field::COMBO, setting, box));
	auto& field = *fields.back();
	field.readCombo = std::move(read_);
	field.writeCombo = std::move(write_);
	read(field);

	std::weak_ptr<bool> guard(alive);
	auto f = &field;
	box->onSelectionChanged([this, guard, f] {
		if(guard.expired() || loading) return;
		if(f->writeCombo()) notify(f->setting);
	});
}

void LiveSettings::watchEdits(Field& field) {
	auto box = static_cast<TextBoxPtr>(field.control);
	std::weak_ptr<bool> guard(alive);
	auto f = &field;

	box->onUpdated([this, guard, f, box] {
		if(guard.expired() || loading) return;
		f->pending = true;
		// Restarting the timer on every edit applies the field once typing pauses.
		box->setTimer([this, guard, f] {
			if(!guard.expired()) commit(*f, false);
			return false;
		}, typingPause);
	});

	auto finish = [this, guard, f, box] {
		if(guard.expired()) return;
		box->setTimer(std::function<bool ()>(), 0);
		commit(*f, true);
	};
	box->onKillFocus([finish](dwt::Widget*) { finish(); });

	// Enter applies the field. A single-line edit doesn't ask for Enter, so the frame's dialog
	// navigation would take it; ask for it on top of what an edit normally wants.
	box->onRaw([](WPARAM key, LPARAM) -> LRESULT {
		return DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS | (key == VK_RETURN ? DLGC_WANTMESSAGE : 0);
	}, dwt::Message(WM_GETDLGCODE));
	box->onKeyDown([finish](int key) {
		if(key != VK_RETURN) return false;
		finish();
		return true;
	});
	// Without this, a single-line edit beeps at the Enter character.
	box->onChar([](int c) { return c == VK_RETURN; });
}

void LiveSettings::commit(Field& field, bool final) {
	if(!field.pending) {
		// Leaving a field that wasn't edited changes nothing (a value stored in bytes that isn't
		// a whole number of KiB keeps its exact value).
		return;
	}

	switch(field.kind) {
	case Field::INT:
		{
			auto box = static_cast<TextBoxPtr>(field.control);
			auto text = Text::fromT(box->getText());
			Util::trim(text);
			if(!isNumber(text)) {
				if(final) {
					field.pending = false;
					read(field);
				}
				return;
			}

			auto value = Util::toInt64(text);
			if(!final && (value < field.minimum || value > field.maximum)) {
				// Perhaps still being typed ("1" on the way to "1024"); leaving the field decides.
				return;
			}

			field.pending = false;
			value = std::clamp<int64_t>(value, field.minimum, field.maximum);
			if(final && text != std::to_string(value)) {
				loading = true;
				box->setText(Text::toT(std::to_string(value)));
				loading = false;
			}

			const auto stored = static_cast<int>(std::min<int64_t>(value * field.multiplier,
				std::numeric_limits<int>::max()));
			auto settings = SettingsManager::getInstance();
			const auto setting = static_cast<SettingsManager::IntSetting>(field.setting);
			if(settings->get(setting) != stored) {
				settings->set(setting, stored);
				notify(field.setting);
			}
			break;
		}
	case Field::TEXT:
		{
			field.pending = false;
			if(field.writeText(static_cast<TextBoxPtr>(field.control)->getText())) {
				notify(field.setting);
			}
			break;
		}
	case Field::BOOL:
	case Field::COMBO:
		field.pending = false;
		break;
	}
}

void LiveSettings::read(Field& field) {
	loading = true;
	auto settings = SettingsManager::getInstance();
	// Controls that already show the stored value are left alone, so showing the window again
	// doesn't repaint every field.
	auto showText = [](TextBoxPtr box, const tstring& text) {
		if(box->getText() != text) {
			box->setText(text);
		}
	};
	switch(field.kind) {
	case Field::BOOL:
		{
			auto box = static_cast<CheckBoxPtr>(field.control);
			const bool checked = settings->get(static_cast<SettingsManager::BoolSetting>(field.setting));
			if(box->getChecked() != checked) {
				box->setChecked(checked);
			}
			break;
		}
	case Field::INT:
		showText(static_cast<TextBoxPtr>(field.control), Text::toT(std::to_string(shownValue(
			settings->get(static_cast<SettingsManager::IntSetting>(field.setting)), field.multiplier))));
		break;
	case Field::TEXT:
		showText(static_cast<TextBoxPtr>(field.control), field.readText());
		break;
	case Field::COMBO:
		field.readCombo();
		break;
	}
	loading = false;
}

void LiveSettings::reload() {
	for(auto& field: fields) {
		if(!field->pending) {
			read(*field);
		}
	}
}

void LiveSettings::commitAll() {
	for(auto& field: fields) {
		if(field->pending) {
			if(field->kind == Field::INT || field->kind == Field::TEXT) {
				static_cast<TextBoxPtr>(field->control)->setTimer(std::function<bool ()>(), 0);
			}
			commit(*field, true);
		}
	}
}

void LiveSettings::notify(int setting) {
	if(changed) {
		changed(setting);
	}
}
