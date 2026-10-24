/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef DCPLUSPLUS_WIN32_COUNTRY_FLAGS_H
#define DCPLUSPLUS_WIN32_COUNTRY_FLAGS_H
#include <dwt/resources/ImageList.h>
#include <string>

namespace CountryFlags {

/** Append a stable, ISO-code-sorted set. Returns its first image index. */
int append(dwt::ImageList& images);
/** Unknown/absent countries have no icon. Never infer codes from display text. */
int index(const std::string& code, int first);
}
#endif
