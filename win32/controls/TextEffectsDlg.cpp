// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdafx.h"
#include <dcpp/text/SimpleXML.h>
#include <dcpp/util/Util.h>
#include <win32/appearance/ControlSeeds.h>
#include <win32/appearance/UiFonts.h>
#include <win32/controls/DialogControls.h>
#include <win32/controls/TextEffectsDlg.h>
#include <win32/controls/HtmlToRtf.h>
#include <win32/controls/RichTextBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>
#include <cmath>
#include <iomanip>
#include <sstream>

using dwt::Grid;
using dwt::GridInfo;

namespace {
tstring points(int value, double scale) {
	std::ostringstream text;
	text.imbue(std::locale::classic());
	text << std::fixed << std::setprecision(2) << value / scale;
	return Text::toT(text.str());
}
}

TextEffectsDlg::TextEffectsDlg(dwt::Widget* parent, const tstring& styleName, const LOGFONT& font,
	const FontStyle& value, COLORREF foreground, COLORREF background) :
	GridDialog(parent, 650), fontValue(font), effects(value), foreground(foreground), background(background) {
	effects.normalize();
	onInitDialog([this, styleName] { return initDialog(styleName); });
}

bool TextEffectsDlg::initDialog(const tstring& styleName) {
	grid = addChild(Grid::Seed(6, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->setSpacing(10);
	grid->addChild(dwt::Label::Seed(styleName + _T("  /  ") + fontValue.lfFaceName));

	auto sample = grid->addChild(GroupBox::Seed(T_("Live preview")))->addChild(Grid::Seed(1, 1));
	sample->column(0).mode = GridInfo::FILL;
	sample->row(0).mode = GridInfo::STATIC;
	sample->row(0).size = sample->scale(105);
	sample->row(0).align = GridInfo::STRETCH;
	auto seed = ControlSeeds::Dialog::richTextBox;
	seed.style |= ES_READONLY;
	preview = sample->addChild(seed);
	preview->setAccessibleName(T_("Text effects preview"));
	preview->setTextLimit(32 * 1024);

	auto groups = grid->addChild(Grid::Seed(1, 2));
	groups->column(0).mode = groups->column(1).mode = GridInfo::FILL;
	groups->setSpacing(10);
	auto character = groups->addChild(GroupBox::Seed(T_("Character styles")))->addChild(Grid::Seed(4, 2));
	character->column(0).mode = character->column(1).mode = GridInfo::FILL;
	character->setSpacing(8);
	auto checkbox = [this](GridPtr parent, const tstring& label, bool refresh = true) {
		auto box = parent->addChild(CheckBox::Seed(label));
		if(refresh) box->onClicked([this] { updatePreview(); });
		return box;
	};
	bold = checkbox(character, T_("Bold"));
	italic = checkbox(character, T_("Italic"));
	smallCaps = checkbox(character, T_("Small capitals"), false);
	allCaps = checkbox(character, T_("All capitals"), false);
	subscript = checkbox(character, T_("Subscript"), false);
	superscript = checkbox(character, T_("Superscript"), false);
	outline = checkbox(character, T_("Outline"));
	shadow = checkbox(character, T_("Shadow"));
	for(auto pair : { std::make_pair(smallCaps, allCaps), std::make_pair(allCaps, smallCaps),
		std::make_pair(subscript, superscript), std::make_pair(superscript, subscript) }) {
		pair.first->onClicked([this, pair] {
			if(pair.first->getChecked()) pair.second->setChecked(false);
			updatePreview();
		});
	}

	auto lines = groups->addChild(GroupBox::Seed(T_("Underline and strikethrough")))->addChild(Grid::Seed(4, 1));
	lines->column(0).mode = GridInfo::FILL;
	lines->setSpacing(8);
	auto underlineRow = lines->addChild(Grid::Seed(1, 2));
	underlineRow->column(1).mode = GridInfo::FILL;
	underline = checkbox(underlineRow, T_("Underline"));
	underlineType = underlineRow->addChild(ControlSeeds::Dialog::comboBox);
	for(const auto& name : { T_("Single"), T_("Double"), T_("Dotted"), T_("Dash"), T_("Dash dot"),
		T_("Dash dot dot"), T_("Wave"), T_("Thick"), T_("Words only"), T_("Heavy wave"), T_("Double wave") }) underlineType->addValue(name);
	underlineType->onSelectionChanged([this] { updatePreview(); });
	strike = checkbox(lines, T_("Strikethrough"));
	doubleStrike = checkbox(lines, T_("Double strikethrough"));
	lines->addChild(dwt::Label::Seed(T_("Double takes precedence over single.")));

	auto metrics = grid->addChild(GroupBox::Seed(T_("Spacing and position")))->addChild(Grid::Seed(3, 3));
	metrics->setSpacing(8);
	for(int i = 0; i < 3; ++i) metrics->column(i).mode = GridInfo::FILL;
	metrics->addChild(dwt::Label::Seed(T_("Character spacing (pt)")));
	metrics->addChild(dwt::Label::Seed(T_("Baseline offset (pt)")));
	kerning = checkbox(metrics, T_("Kerning above (pt)"));
	spacing = metrics->addChild(ControlSeeds::Dialog::textBox);
	offset = metrics->addChild(ControlSeeds::Dialog::textBox);
	kerningSize = metrics->addChild(ControlSeeds::Dialog::textBox);
	spacing->setAccessibleName(T_("Character spacing in points"));
	offset->setAccessibleName(T_("Baseline offset in points"));
	kerningSize->setAccessibleName(T_("Kerning threshold in points"));
	metrics->addChild(dwt::Label::Seed(T_("-10 to 10; 0 = normal")));
	metrics->addChild(dwt::Label::Seed(T_("-50 to 50; positive = raised")));
	metrics->addChild(dwt::Label::Seed(T_("0 to 72 points")));
	for(auto box : { spacing, offset, kerningSize }) box->onUpdated([this] { updatePreview(); });

	status = grid->addChild(dwt::Label::Seed());
	auto footer = grid->addChild(Grid::Seed(1, 2));
	footer->column(0).mode = GridInfo::FILL;
	footer->column(0).align = GridInfo::TOP_LEFT;
	footer->addChild(Button::Seed(T_("Reset effects")))->onClicked([this] {
		effects = FontStyle();
		fontValue.lfWeight = FW_NORMAL;
		fontValue.lfItalic = false;
		loadControls();
	});
	ok = DialogControls::addDlgButtons(footer->addChild(Grid::Seed(1, 2)),
		[this] { if(ok->getEnabled()) endDialog(IDOK); },
		[this] { endDialog(IDCANCEL); }).first;
	loadControls();
	setText(T_("Text effects"));
	layout();
	centerWindow();
	bold->setFocus();
	return false;
}

void TextEffectsDlg::loadControls() {
	loading = true;
	bold->setChecked(fontValue.lfWeight >= FW_BOLD);
	italic->setChecked(fontValue.lfItalic != 0);
	smallCaps->setChecked((effects.effects & FontStyle::SmallCaps) != 0);
	allCaps->setChecked((effects.effects & FontStyle::AllCaps) != 0);
	subscript->setChecked(effects.script < 0);
	superscript->setChecked(effects.script > 0);
	outline->setChecked((effects.effects & FontStyle::Outline) != 0);
	shadow->setChecked((effects.effects & FontStyle::Shadow) != 0);
	underline->setChecked(effects.underline != 0);
	underlineType->setSelected(std::max(0, effects.underline - 1));
	strike->setChecked(effects.strike == 1);
	doubleStrike->setChecked(effects.strike == 2);
	kerning->setChecked(effects.kerning != 0);
	spacing->setText(points(effects.spacing, 20));
	offset->setText(points(effects.offset, 2));
	kerningSize->setText(points(effects.kerning, 20));
	loading = false;
	updatePreview();
}

bool TextEffectsDlg::readPoints(TextBoxPtr box, double minimum, double maximum, int scale, int& value) {
	auto text = Text::fromT(box->getText());
	std::replace(text.begin(), text.end(), ',', '.');
	std::istringstream input(text);
	input.imbue(std::locale::classic());
	double number;
	if(!(input >> number) || !(input >> std::ws).eof() || !std::isfinite(number) || number < minimum || number > maximum) return false;
	value = static_cast<int>(std::round(number * scale));
	return true;
}

void TextEffectsDlg::updatePreview() {
	if(loading) return;
	fontValue.lfWeight = bold->getChecked() ? std::max<LONG>(fontValue.lfWeight, FW_BOLD) : FW_NORMAL;
	fontValue.lfItalic = italic->getChecked();
	effects.underline = underline->getChecked() ? underlineType->getSelected() + 1 : 0;
	effects.strike = doubleStrike->getChecked() ? 2 : strike->getChecked() ? 1 : 0;
	effects.script = superscript->getChecked() ? 1 : subscript->getChecked() ? -1 : 0;
	effects.effects = (smallCaps->getChecked() ? FontStyle::SmallCaps : 0) | (allCaps->getChecked() ? FontStyle::AllCaps : 0) |
		(outline->getChecked() ? FontStyle::Outline : 0) | (shadow->getChecked() ? FontStyle::Shadow : 0);
	underlineType->setEnabled(underline->getChecked());
	strike->setEnabled(!doubleStrike->getChecked());
	kerningSize->setEnabled(kerning->getChecked());
	const bool validSpacing = readPoints(spacing, -10, 10, 20, effects.spacing);
	const bool validOffset = readPoints(offset, -50, 50, 2, effects.offset);
	const bool validKerning = !kerning->getChecked() || readPoints(kerningSize, 0, 72, 20, effects.kerning);
	if(!kerning->getChecked()) effects.kerning = 0;
	ok->setEnabled(validSpacing && validOffset && validKerning);
	status->setText(!validSpacing ? T_("Enter character spacing between -10 and 10 points.") :
		!validOffset ? T_("Enter a baseline offset between -50 and 50 points.") :
		!validKerning ? T_("Enter a kerning threshold between 0 and 72 points.") :
		T_("Preview uses Windows RichEdit; support for advanced effects varies."));
	fontValue.lfUnderline = effects.underline != 0;
	fontValue.lfStrikeOut = effects.strike != 0;
	const auto encoded = effects.encode(Text::fromT(UiFonts::encodeFont(fontValue)));
	const auto css = "font:" + Util::cssFont(encoded) + ";dc-font:" + encoded + ";color:#" + Util::cssColor(foreground) +
		";background-color:#" + Util::cssColor(background) + ";";
	string scratch;
	const auto html = "<span id=\"text\" style=\"" + SimpleXML::escape(css, scratch, true) +
		"\">The quick brown fox jumps over the lazy dog.<br/>Aa Bb Cc 0123456789<br/>This is how your chat text will look.</span>";
	preview->setText(L"");
	preview->setFont(dwt::FontPtr(new dwt::Font(fontValue)));
	preview->setColor(foreground, background);
	preview->addTextSteady(L"{\\urtf1" + HtmlToRtf::convert(html, preview) + L"}");
	preview->setSelection(0, 0);
	preview->sendMessage(WM_VSCROLL, SB_TOP);
	preview->redraw(true);
}
