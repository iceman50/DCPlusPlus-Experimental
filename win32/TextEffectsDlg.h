// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#ifndef DCPLUSPLUS_WIN32_TEXT_EFFECTS_DLG_H
#define DCPLUSPLUS_WIN32_TEXT_EFFECTS_DLG_H

#include "GridDialog.h"
#include <dcpp/FontStyle.h>

class TextEffectsDlg : public GridDialog {
public:
	TextEffectsDlg(dwt::Widget* parent, const dwt::tstring& styleName, const LOGFONT& font,
		const dcpp::FontStyle& effects, COLORREF foreground, COLORREF background);
	const LOGFONT& getFontValue() const { return fontValue; }
	const dcpp::FontStyle& getEffects() const { return effects; }

private:
	LOGFONT fontValue;
	dcpp::FontStyle effects;
	COLORREF foreground, background;
	RichTextBoxPtr preview = nullptr;
	CheckBoxPtr bold, italic, smallCaps, allCaps, subscript, superscript, outline, shadow;
	CheckBoxPtr underline, strike, doubleStrike, kerning;
	ComboBoxPtr underlineType;
	TextBoxPtr spacing, offset, kerningSize;
	LabelPtr status;
	ButtonPtr ok;
	bool loading = true;

	bool initDialog(const dwt::tstring& styleName);
	void loadControls();
	void updatePreview();
	bool readPoints(TextBoxPtr box, double minimum, double maximum, int scale, int& value);
};

#endif
