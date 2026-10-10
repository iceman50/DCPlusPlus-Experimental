// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include "stdinc.h"
#include "FontStyle.h"
#include "StringTokenizer.h"
#include "Util.h"

namespace dcpp {
void FontStyle::normalize() {
	underline = std::clamp(underline, 0, underlineCount - 1);
	strike = std::clamp(strike, 0, 2);
	effects &= Outline | Shadow | SmallCaps | AllCaps;
	script = std::clamp(script, -1, 1);
	spacing = std::clamp(spacing, -200, 200);
	offset = std::clamp(offset, -100, 100);
	kerning = std::clamp(kerning, 0, 1440);
}

FontStyle FontStyle::decode(const std::string& font) {
	StringTokenizer<std::string> tokens(font, ',');
	const auto& fields = tokens.getTokens();
	FontStyle result;
	int* values[] = { &result.underline, &result.strike, &result.effects, &result.script,
		&result.spacing, &result.offset, &result.kerning };
	for(size_t i = 0; i < std::size(values) && i + 5 < fields.size(); ++i)
		*values[i] = Util::toInt(fields[i + 5]);
	result.normalize();
	return result;
}

std::string FontStyle::encode(const std::string& font) const {
	StringTokenizer<std::string> tokens(font, ',');
	const auto& fields = tokens.getTokens();
	if(fields.size() < 5) return font;
	std::string result;
	for(size_t i = 0; i < 5; ++i) {
		if(i) result += ',';
		result += fields[i];
	}
	auto value = *this;
	value.normalize();
	for(int field : { value.underline, value.strike, value.effects, value.script,
		value.spacing, value.offset, value.kerning }) result += ',' + std::to_string(field);
	return result;
}

std::string FontStyle::rtf() const {
	auto value = *this;
	value.normalize();
	static const char* underlines[] = { "\\ulnone", "\\ul", "\\uldb", "\\uld", "\\uldash",
		"\\uldashd", "\\uldashdd", "\\ulwave", "\\ulth", "\\ulw", "\\ulhwave", "\\ululdbwave" };
	std::string result = underlines[value.underline];
	result += value.strike == 1 ? "\\strike\\striked0" : value.strike == 2 ? "\\strike0\\striked1" : "\\strike0\\striked0";
	result += value.effects & Outline ? "\\outl" : "\\outl0";
	result += value.effects & Shadow ? "\\shad" : "\\shad0";
	result += value.effects & SmallCaps ? "\\scaps" : "\\scaps0";
	result += value.effects & AllCaps ? "\\caps" : "\\caps0";
	result += value.script < 0 ? "\\sub" : value.script > 0 ? "\\super" : "\\nosupersub";
	result += "\\expndtw" + std::to_string(value.spacing);
	result += "\\up" + std::to_string(value.offset);
	result += "\\kerning" + std::to_string(value.kerning / 10);
	return result;
}
}
