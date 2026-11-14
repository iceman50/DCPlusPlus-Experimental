/* Copyright (C) 2026 iceman50. GPL-3.0-or-later. */
#ifndef DCPLUSPLUS_SYSTEM_STATS_H
#define DCPLUSPLUS_SYSTEM_STATS_H

#include <string>

namespace SystemStats {
/** A chat-ready snapshot. Does not send anything or include host/user identifiers. */
std::string report();
}

#endif
