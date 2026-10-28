/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka
  Copyright (C) 2026 iceman50

  SmartWin++

  Copyright (c) 2005 Thomas Hansen

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor SmartWin++ nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <dwt/util/RegKey.h>

#include <limits>

namespace dwt { namespace util {

RegKey::RegKey(void)
{
	m_hKeyHandle = NULL;
	m_dwDisposition = 0;
	m_pAccess = KEY_ALL_ACCESS;
}

RegKey::RegKey(HKEY hKey, tstring lpSubKey,REGSAM samDesired,bool createIfNotExists)
{
	m_hKeyHandle = NULL;
	m_dwDisposition = 0;
	m_pAccess = KEY_ALL_ACCESS;
	if(!open(hKey,lpSubKey,samDesired,createIfNotExists)) {
		xCeptionSmartUtilities x( "Couldn't open registry key" );
		throw x;
	}
}

RegKey::~RegKey(void)
{
	if(m_hKeyHandle != NULL) close();
}

bool RegKey::open(HKEY hKey, tstring lpSubKey,REGSAM samDesired,bool createIfNotExists)
{
	if(m_hKeyHandle != NULL) close();
	LONG lResult;
	m_pAccess = samDesired;
	if(createIfNotExists)
		lResult = RegCreateKeyEx(hKey,lpSubKey.c_str(),0,NULL,REG_OPTION_NON_VOLATILE, samDesired, NULL, &m_hKeyHandle, &m_dwDisposition);
	else {
		m_dwDisposition = REG_OPENED_EXISTING_KEY;
		lResult = RegOpenKeyEx(hKey, lpSubKey.c_str(), 0, samDesired, &m_hKeyHandle);
	}
	// Enumeration obtains current sizes directly; opening needs no metadata query
	// (which would require permissions unrelated to the requested operation).
	return lResult == ERROR_SUCCESS;
}

void RegKey::close(void)
{
	if(m_hKeyHandle != NULL) {
		RegCloseKey(m_hKeyHandle);
		m_hKeyHandle = NULL;
		m_dwDisposition = 0;
		m_pAccess = KEY_ALL_ACCESS;
	}
}

bool RegKey::deleteSubkey(tstring lpSubKey, bool recursiv)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x(  "No open reg key!" );
		throw x;
	}
	if(lpSubKey.length() == 0) {
		xCeptionSmartUtilities x(  "No subkey specified!" );
		throw x;
	}
	LONG lResult;
	if(recursiv)
		lResult = SHDeleteKey(m_hKeyHandle,lpSubKey.c_str());
	else
		lResult = SHDeleteEmptyKey(m_hKeyHandle,lpSubKey.c_str());
	return (lResult == ERROR_SUCCESS);
}

bool RegKey::deleteValue(tstring lpValue)
{
	return (ERROR_SUCCESS == RegDeleteValue(m_hKeyHandle,lpValue.c_str()));
}

std::vector<tstring> RegKey::getValues(void)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x(  "No open reg key!" );
		throw x;
	}
	if(!(m_pAccess & KEY_QUERY_VALUE)) {
		xCeptionSmartUtilities x("You need to have KEY_QUERY_VALUE access rights!" );
		throw x;
	}

	std::vector<tstring> v;
	std::vector<TCHAR> name(256);
	for(DWORD i = 0;;) {
		DWORD length = static_cast<DWORD>(name.size());
		const auto result = ::RegEnumValue(m_hKeyHandle, i, name.data(), &length,
			nullptr, nullptr, nullptr, nullptr);
		if(result == ERROR_NO_MORE_ITEMS) break;
		if(result == ERROR_MORE_DATA) {
			if(name.size() > std::numeric_limits<DWORD>::max() / 2)
				throw xCeptionSmartUtilities("Registry value name is too long");
			name.resize(name.size() * 2);
			continue;
		}
		if(result != ERROR_SUCCESS) throw xCeptionSmartUtilities("Couldn't enumerate registry values");
		v.emplace_back(name.data(), length);
		++i;
	}
	return v;
}

std::vector<tstring> RegKey::getSubkeys(void)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x(  "No open reg key!"  );
		throw x;
	}
	if(!(m_pAccess & KEY_ENUMERATE_SUB_KEYS)) {
		xCeptionSmartUtilities x( "You need to have KEY_ENUMERATE_SUB_KEYS access rights!" );
		throw x;
	}

	std::vector<tstring> v;
	std::vector<TCHAR> name(256);
	for(DWORD i = 0;;) {
		DWORD length = static_cast<DWORD>(name.size());
		const auto result = ::RegEnumKeyEx(m_hKeyHandle, i, name.data(), &length,
			nullptr, nullptr, nullptr, nullptr);
		if(result == ERROR_NO_MORE_ITEMS) break;
		if(result == ERROR_MORE_DATA) {
			if(name.size() > std::numeric_limits<DWORD>::max() / 2)
				throw xCeptionSmartUtilities("Registry subkey name is too long");
			name.resize(name.size() * 2);
			continue;
		}
		if(result != ERROR_SUCCESS) throw xCeptionSmartUtilities("Couldn't enumerate registry subkeys");
		v.emplace_back(name.data(), length);
		++i;
	}
	return v;
}

bool RegKey::writeDword(tstring lpValueName, DWORD dwValueDword)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x(  "No open reg key!"  );
		throw x;
	}
	return (RegSetValueEx(m_hKeyHandle,lpValueName.c_str(),0,REG_DWORD,reinterpret_cast<const BYTE*>(&dwValueDword),sizeof(DWORD)) == ERROR_SUCCESS);
}

bool RegKey::writeString(tstring lpValueName, tstring dwValueString)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x( "No open reg key!" );
		throw x;
	}
	return (RegSetValueEx(m_hKeyHandle,lpValueName.c_str(),0,REG_SZ,(LPBYTE)dwValueString.c_str(),static_cast<DWORD>((dwValueString.length() + 1) * sizeof(TCHAR))) == ERROR_SUCCESS);
}

bool RegKey::writeBinary(tstring lpValueName, void* pValueBinary, int size)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x(  "No open reg key!" );
		throw x;
	}
	return (RegSetValueEx(m_hKeyHandle,lpValueName.c_str(),0,REG_BINARY,(LPBYTE)pValueBinary,size) == ERROR_SUCCESS);
}

DWORD RegKey::readDword(tstring lpValueName)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x("No open reg key!");
		throw x;
	}
	DWORD dwLen = sizeof(DWORD);
	DWORD v = 0;
	const auto result = ::RegGetValue(m_hKeyHandle, nullptr, lpValueName.c_str(),
		RRF_RT_REG_DWORD, nullptr, &v, &dwLen);
	if(result != ERROR_SUCCESS || dwLen != sizeof(v)) {
		xCeptionSmartUtilities x( "Error while retreiving value!" );
		throw x;
	}
	return v;
}

tstring RegKey::readString(tstring lpValueName)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x( "No open reg key!" );
		throw x;
	}
	const DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
	DWORD bytes = 0;
	if(::RegGetValue(m_hKeyHandle, nullptr, lpValueName.c_str(), flags,
		nullptr, nullptr, &bytes) != ERROR_SUCCESS)
		throw xCeptionSmartUtilities("Error while retrieving string value");
	for(;;) {
		// Include an extra terminator, including for a stored empty value.
		std::vector<TCHAR> value(static_cast<size_t>(bytes) / sizeof(TCHAR) + 1, 0);
		const auto result = ::RegGetValue(m_hKeyHandle, nullptr, lpValueName.c_str(),
			flags, nullptr, value.data(), &bytes);
		if(result == ERROR_MORE_DATA) continue;
		if(result != ERROR_SUCCESS || bytes % sizeof(TCHAR))
			throw xCeptionSmartUtilities("Error while retrieving string value");
		return value.data();
	}
}

void RegKey::readBinary(tstring lpValueName,void* pValueData, int size)
{
	if(m_hKeyHandle == NULL) {
		xCeptionSmartUtilities x( "No open reg key!" );
		throw x;
	}
	if(size < 0 || (!pValueData && size != 0))
		throw xCeptionSmartUtilities("Invalid registry data buffer");
	DWORD dwLen = static_cast<DWORD>(size);
	const auto result = ::RegGetValue(m_hKeyHandle, nullptr, lpValueName.c_str(),
		RRF_RT_REG_BINARY, nullptr, pValueData, &dwLen);
	if(result != ERROR_SUCCESS || dwLen > static_cast<DWORD>(size)) {
		xCeptionSmartUtilities x( "Error while retreiving value!" );
		throw x;
	}
}

} }
