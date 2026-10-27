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

#include <win32/appearance/ColorUtil.h>
#include <win32/forward.h>
#include <algorithm>

namespace ColorUtil {

HLSCOLOR RGB2HLS(COLORREF rgb) {
	unsigned char minval = std::min(GetRValue(rgb), std::min(GetGValue(rgb), GetBValue(rgb)));
	unsigned char maxval = std::max(GetRValue(rgb), std::max(GetGValue(rgb), GetBValue(rgb)));
	float mdiff = float(maxval) - float(minval);
	float msum = float(maxval) + float(minval);

	float luminance = msum / 510.0f;
	float saturation = 0.0f;
	float hue = 0.0f;

	if(maxval != minval) {
		float rnorm = (maxval - GetRValue(rgb)) / mdiff;
		float gnorm = (maxval - GetGValue(rgb)) / mdiff;
		float bnorm = (maxval - GetBValue(rgb)) / mdiff;

		saturation = (luminance <= 0.5f) ? (mdiff / msum) : (mdiff / (510.0f - msum));

		if(GetRValue(rgb) == maxval)
			hue = 60.0f * (6.0f + bnorm - gnorm);
		if(GetGValue(rgb) == maxval)
			hue = 60.0f * (2.0f + rnorm - bnorm);
		if(GetBValue(rgb) == maxval)
			hue = 60.0f * (4.0f + gnorm - rnorm);
		if(hue > 360.0f)
			hue = hue - 360.0f;
	}
	return HLS ((hue*255)/360, luminance*255, saturation*255);
}

static inline BYTE _ToRGB(float rm1, float rm2, float rh) {
	if(rh > 360.0f)
		rh -= 360.0f;
	else if(rh < 0.0f)
		rh += 360.0f;

	if(rh < 60.0f)
		rm1 = rm1 + (rm2 - rm1) * rh / 60.0f;
	else if(rh < 180.0f)
		rm1 = rm2;
	else if(rh < 240.0f)
		rm1 = rm1 + (rm2 - rm1) * (240.0f - rh) / 60.0f;

	return (BYTE) (rm1 * 255);
}

COLORREF HLS2RGB(HLSCOLOR hls) {
	float hue = ((int) HLS_H(hls) * 360) / 255.0f;
	float luminance = HLS_L(hls) / 255.0f;
	float saturation = HLS_S(hls) / 255.0f;

	if(saturation == 0.0f) {
		return RGB (HLS_L(hls), HLS_L(hls), HLS_L(hls));
	}
	float rm1, rm2;

	if(luminance <= 0.5f)
		rm2 = luminance + luminance * saturation;
	else
		rm2 = luminance + saturation - luminance * saturation;
	rm1 = 2.0f * luminance - rm2;
	BYTE red = _ToRGB(rm1, rm2, hue + 120.0f);
	BYTE green = _ToRGB(rm1, rm2, hue);
	BYTE blue = _ToRGB(rm1, rm2, hue - 120.0f);

	return RGB (red, green, blue);
}

COLORREF HLS_TRANSFORM(COLORREF rgb, int percent_L, int percent_S) {
	HLSCOLOR hls = RGB2HLS(rgb);
	BYTE h = HLS_H(hls);
	BYTE l = HLS_L(hls);
	BYTE s = HLS_S(hls);

	if(percent_L > 0) {
		l = BYTE(l + ((255 - l) * percent_L) / 100);
	} else if(percent_L < 0) {
		l = BYTE((l * (100 + percent_L)) / 100);
	}
	if(percent_S > 0) {
		s = BYTE(s + ((255 - s) * percent_S) / 100);
	} else if(percent_S < 0) {
		s = BYTE((s * (100 + percent_S)) / 100);
	}
	return HLS2RGB(HLS(h, l, s));
}

// check that col0 is within COLORREF bounds; adjust col1 & col2 otherwise.
namespace { void checkColor(int16_t& col0, int16_t& col1, int16_t& col2) {
	if(col0 < 0) {
		int16_t delta = -col0;
		col0 = 0;
		col1 += delta;
		col2 += delta;
		if(col1 > 255) { col1 = 255; }
		if(col2 > 255) { col2 = 255; }
	} else if(col0 > 255) {
		int16_t delta = col0 - 255;
		col0 = 255;
		col1 -= delta;
		col2 -= delta;
		if(col1 < 0) { col1 = 0; }
		if(col2 < 0) { col2 = 0; }
	}
} }

COLORREF modRed(COLORREF col, int16_t mod) {
	int16_t r = GetRValue(col) + mod, g = GetGValue(col), b = GetBValue(col);
	checkColor(r, g, b);
	return RGB(r, g, b);
}

COLORREF modGreen(COLORREF col, int16_t mod) {
	int16_t r = GetRValue(col), g = GetGValue(col) + mod, b = GetBValue(col);
	checkColor(g, r, b);
	return RGB(r, g, b);
}

COLORREF modBlue(COLORREF col, int16_t mod) {
	int16_t r = GetRValue(col), g = GetGValue(col), b = GetBValue(col) + mod;
	checkColor(b, r, g);
	return RGB(r, g, b);
}

} // namespace ColorUtil
