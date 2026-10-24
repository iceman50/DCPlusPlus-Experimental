/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "stdafx.h"

#include "CountryFlags.h"

#include "IconManager.h"

#include <algorithm>

namespace CountryFlags {
namespace {

struct Flag {
	const char* code;
	unsigned resource;
};

const Flag flags[] = {
#include "CountryFlags.inc"
};
}

int append(dwt::ImageList& images) {
	const auto first = static_cast<int>(images.size());
	for(const auto& flag : flags)
		images.add(*IconManager::load(flag.resource, images.getImageSize().x));
	return first;
}

int index(const std::string& code, int first) {
	if(code.size() != 2)
		return -1;

	const auto i = std::lower_bound(
		std::begin(flags), std::end(flags), code, [](const Flag& flag, const std::string& code) { return flag.code < code; });
	return i != std::end(flags) && code == i->code ? first + static_cast<int>(i - std::begin(flags)) : -1;
}
}
