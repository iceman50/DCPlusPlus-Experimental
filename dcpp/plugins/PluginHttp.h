// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#ifndef DCPLUSPLUS_PLUGIN_HTTP_H
#define DCPLUSPLUS_PLUGIN_HTTP_H

#include <string>

namespace dcpp {
/** Bounded plugin-only HTTPS transport. Call stop before plugin shutdown. */
class PluginHttp {
public:
	static bool validUrl(const std::string& url);
	static bool submit(const std::string& url, const std::string& destination);
	static void cancelOwner(const std::string& owner);
	static void stop();
};
}
#endif
