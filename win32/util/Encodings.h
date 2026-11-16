/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_ENCODINGS_H
#define DCPLUSPLUS_WIN32_ENCODINGS_H

#include <string>

#include <dwt/forward.h>
#include <dwt/tstring.h>

/** Choosing an NMDC hub encoding from the Windows code pages installed on this system. */
namespace Encodings {

/** Fill the combo box with defaultLabel followed by every installed code page, and select the
entry for current (or defaultLabel when current is empty or not installed). */
void fill(dwt::ComboBoxPtr box, const dwt::tstring& defaultLabel, const std::string& current);

/** The selected code page name, such as "1251 (ANSI - Cyrillic)", or an empty string when the
default entry is selected. */
std::string selected(dwt::ComboBoxPtr box);

}

#endif
