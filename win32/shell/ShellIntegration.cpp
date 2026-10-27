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

#include <win32/shell/ShellIntegration.h>
#include <dcpp/text/format.h>
#include <win32/forward.h>
#include <dcpp/settings/SettingsManager.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/text/Text.h>
#include <dcpp/util/Util.h>
#include <dcpp/version.h>
#include <dwt/Application.h>

namespace ShellIntegration {

namespace {
bool urlDcADCRegistered = false, urlMagnetRegistered = false, dcextRegistered = false;
}
namespace {

void regChanged() {
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

tstring readRegistryString(HKEY key, LPCTSTR valueName) {
	DWORD type = 0;
	DWORD bytes = 0;
	if(::RegQueryValueEx(key, valueName, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
		(type != REG_SZ && type != REG_EXPAND_SZ))
	{
		return tstring();
	}

	tstring value(bytes / sizeof(TCHAR) + 1, _T('\0'));
	DWORD available = static_cast<DWORD>(value.size() * sizeof(TCHAR));
	if(::RegQueryValueEx(key, valueName, nullptr, &type,
		reinterpret_cast<LPBYTE>(&value[0]), &available) != ERROR_SUCCESS)
	{
		return tstring();
	}
	value.resize(value.find(_T('\0')));
	return value;
}

bool registerHandler_(const tstring& name, const tstring& descr, bool url, const tstring& prefix) {
	HKEY hk;
	tstring registeredApp;

	if(::RegOpenKeyEx(HKEY_CURRENT_USER, (_T("Software\\Classes\\") + name + _T("\\Shell\\Open\\Command")).c_str(),
		0, KEY_WRITE | KEY_READ, &hk) == ERROR_SUCCESS)
	{
		registeredApp = readRegistryString(hk, nullptr);
		::RegCloseKey(hk);
	}

	tstring app = _T("\"") + dwt::Application::instance().getModuleFileName() + _T("\" \"") + prefix + _T("%1\"");
	if(Util::stricmp(app, registeredApp) == 0) {
		// already registered to us
		return true;
	}

	if(::RegCreateKeyEx(HKEY_CURRENT_USER, (_T("Software\\Classes\\") + name).c_str(),
		0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hk, NULL) != ERROR_SUCCESS)
	{
		return false;
	}

	::RegSetValueEx(hk, NULL, 0, REG_SZ, (LPBYTE) descr.c_str(), static_cast<DWORD>(sizeof(TCHAR) * (descr.size() + 1)));
	if(url) {
		::RegSetValueEx(hk, _T("URL Protocol"), 0, REG_SZ, (LPBYTE) _T(""), sizeof(TCHAR));
	}
	::RegCloseKey(hk);

	if(::RegCreateKeyEx(HKEY_CURRENT_USER, (_T("Software\\Classes\\") + name + _T("\\Shell\\Open\\Command")).c_str(),
		0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hk, NULL) != ERROR_SUCCESS)
	{
		return false;
	}
	bool ret = ::RegSetValueEx(hk, _T(""), 0, REG_SZ, (LPBYTE) app.c_str(), static_cast<DWORD>(sizeof(TCHAR) * (app.length() + 1))) == ERROR_SUCCESS;
	::RegCloseKey(hk);

	if(::RegCreateKeyEx(HKEY_CURRENT_USER, (_T("Software\\Classes\\") + name + _T("\\DefaultIcon")).c_str(),
		0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hk, NULL) == ERROR_SUCCESS)
	{
		app = dwt::Application::instance().getModuleFileName();
		::RegSetValueEx(hk, _T(""), 0, REG_SZ, (LPBYTE) app.c_str(), static_cast<DWORD>(sizeof(TCHAR) * (app.length() + 1)));
		::RegCloseKey(hk);
	}

	return ret;
}

/// @todo var template
bool registerHandler(const tstring& name, const tstring& description, bool url, const tstring& prefix = Util::emptyStringT) {
	bool ret = registerHandler_(name, description, url, prefix);
	if(ret) {
		regChanged();
	} else {
		LogManager::getInstance()->message(str(F_("Error registering the %1% handler") % Text::fromT(name)),
			LogMessage::SEV_ERROR, _("Integration"));
	}
	return ret;
}

} // unnamed namespace

void registerHubHandlers() {
	if(SETTING(URL_HANDLER)) {
		if(!urlDcADCRegistered) {
			urlDcADCRegistered = registerHandler(_T("dchub"), _T("URL:Direct Connect Protocol"), true) &&
				registerHandler(_T("nmdcs"), _T("URL:Direct Connect Protocol"), true) &&
				registerHandler(_T("adc"), _T("URL:Direct Connect Protocol"), true) &&
				registerHandler(_T("adcs"), _T("URL:Direct Connect Protocol"), true);
		}
	} else if(urlDcADCRegistered) {
		urlDcADCRegistered =
			::SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\Classes\\dchub")) != ERROR_SUCCESS ||
			::SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\Classes\\nmdcs")) != ERROR_SUCCESS ||
			::SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\Classes\\adc")) == ERROR_SUCCESS ||
			::SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\Classes\\adcs")) == ERROR_SUCCESS;
		regChanged();
	}
}

void registerMagnetHandler() {
	if(SETTING(MAGNET_REGISTER)) {
		if(!urlMagnetRegistered) {
			urlMagnetRegistered = registerHandler(_T("magnet"), _T("URL:Magnet"), true);
		}
	} else if(urlMagnetRegistered) {
		urlMagnetRegistered = ::SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\Classes\\magnet")) != ERROR_SUCCESS;
		regChanged();
	}
}

void registerDcextHandler() {
	if(SETTING(DCEXT_REGISTER)) {
		if(!dcextRegistered) {
			dcextRegistered = registerHandler(_T(".dcext"), _T("DC plugin"), false, _T("dcext:"));
		}
	} else if(dcextRegistered) {
		dcextRegistered = ::SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\Classes\\.dcext")) != ERROR_SUCCESS;
		regChanged();
	}
}

void setApplicationStartupRegister()
{
	HKEY hk;
	DWORD ret = 0;

	if(::RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
		0, KEY_WRITE | KEY_READ, &hk) != ERROR_SUCCESS)
	{
		ret = ::RegCreateKeyEx(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
							   0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hk, NULL);

		if(ret != ERROR_SUCCESS)
		{
			LogManager::getInstance()->message(str(F_("Error registering DC++ for automatic startup (could not find or create key)")),
				LogMessage::SEV_ERROR, _("Integration"));
			return;
		}
	}

	tstring app = _T("\"") + dwt::Application::instance().getModuleFileName() + _T("\"");

	ret = ::RegSetValueEx(hk, _T("DC++"), 0, REG_SZ, (LPBYTE) app.c_str(), static_cast<DWORD>(sizeof(TCHAR) * (app.length() + 1)));
	if(ret != ERROR_SUCCESS)
	{
		LogManager::getInstance()->message(str(F_("Error registering DC++ for automatic startup (could not set key value)")),
			LogMessage::SEV_ERROR, _("Integration"));
	}

	::RegCloseKey(hk);
}

void setApplicationStartupUnregister()
{
	HKEY hk;
	DWORD ret = 0;

	ret = ::RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
						 0, KEY_WRITE | KEY_READ, &hk);
	if(ret != ERROR_SUCCESS)
	{
		// Could not find key, well, that's OK.
		return;
	}

	tstring app = _T("\"") + dwt::Application::instance().getModuleFileName() + _T("\"");
	const auto registeredApp = readRegistryString(hk, _T("DC++"));
	if(!registeredApp.empty())
	{
		bool bEqualApplications = Util::stricmp(app, registeredApp) == 0;
		if(bEqualApplications)
		{
			ret = ::RegDeleteValue(hk, _T("DC++"));
			if(ret != ERROR_SUCCESS)
			{
				LogManager::getInstance()->message(str(F_("Error unregistering DC++ for automatic startup (could not delete key value)")),
					LogMessage::SEV_ERROR, _("Integration"));
			}
		}
	}

	::RegCloseKey(hk);
}

void setApplicationStartup()
{
	if(SETTING(REGISTER_SYSTEM_STARTUP))
	{
		setApplicationStartupRegister();
	}
	else
	{
		setApplicationStartupUnregister();
	}
}

} // namespace ShellIntegration
