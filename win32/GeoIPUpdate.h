/* Copyright (C) 2026 iceman50
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef DCPLUSPLUS_WIN32_GEOIP_UPDATE_H
#define DCPLUSPLUS_WIN32_GEOIP_UPDATE_H

#include <memory>
#include <string>

class GeoIPTransfer;

/** UI-thread scheduling for the HttpManager-owned background transfer. */
class GeoIPUpdate {
public:
	~GeoIPUpdate();
	void startUpdate(bool force);
	bool poll();
	void stop();

	static std::string protectKey(const std::string& key);
	static std::string unprotectKey(const std::string& stored);
	static bool validHostedUrl(const std::string& url);

private:
	std::shared_ptr<GeoIPTransfer> transfer;
	bool pending = false;
};

#endif
