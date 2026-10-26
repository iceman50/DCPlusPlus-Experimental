/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef DCPLUSPLUS_WIN32_COUNTRY_FLAGS_H
#define DCPLUSPLUS_WIN32_COUNTRY_FLAGS_H
#include <dwt/resources/ImageList.h>
#include <cstddef>
#include <string>

namespace CountryFlags {

std::size_t count() noexcept;
/** Icon-pack path for a flag resource, or nullptr for an unknown resource. */
const char* fileName(unsigned resource) noexcept;
/** Append a stable, ISO-code-sorted set. Returns its first image index. */
int append(dwt::ImageList& images);
/** Unknown/absent countries have no icon. Never infer codes from display text. */
int index(const std::string& code, int first);
}
#endif
