// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#ifndef DCPLUSPLUS_FONT_STYLE_H
#define DCPLUSPLUS_FONT_STYLE_H

#include <string>

namespace dcpp {
/** Backward-compatible character effects appended to the five legacy font fields.
 * Units: spacing and kerning in twips, baseline offset in half-points. */
struct FontStyle {
	int underline = 0;
	int strike = 0;
	int effects = 0;
	int script = 0;
	int spacing = 0;
	int offset = 0;
	int kerning = 0;
	enum { Outline = 1, Shadow = 2, SmallCaps = 4, AllCaps = 8 };
	static constexpr int underlineCount = 12;
	static FontStyle decode(const std::string& font);
	std::string encode(const std::string& font) const;
	std::string rtf() const;
	void normalize();
};
}
#endif
