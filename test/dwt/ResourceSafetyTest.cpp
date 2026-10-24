// Copyright (C) 2026 iceman50
// Noninteractive ownership and failure-path regression checks.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Button.h>
#include <dwt/resources/Bitmap.h>
#include <dwt/resources/Icon.h>
#include <dwt/util/RegKey.h>
#include <dwt/util/StringConversion.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <type_traits>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

template<typename F> void rejects(F action, const char* message) {
	bool caught = false;
	try { action(); } catch(const dwt::util::xCeptionSmartUtilities&) { caught = true; }
	check(caught, message);
}

struct RegistryFixture {
	std::wstring path = L"Software\\DwtResourceSafetyTest-" +
		std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(::GetTickCount64());
	HKEY handle = nullptr;
	RegistryFixture() {
		DWORD disposition = 0;
		check(::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
			REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr, &handle, &disposition) == ERROR_SUCCESS,
			"Cannot create temporary registry test key");
		if(disposition != REG_CREATED_NEW_KEY) {
			::RegCloseKey(handle);
			handle = nullptr;
			throw std::runtime_error("Temporary registry key already exists");
		}
	}
	~RegistryFixture() {
		if(handle) {
			::RegCloseKey(handle);
			::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
			::RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
		}
	}
	void value(const wchar_t* name, DWORD type, const void* data, DWORD size) {
		check(::RegSetValueExW(handle, name, 0, type, static_cast<const BYTE*>(data), size) == ERROR_SUCCESS,
			"Cannot write registry fixture");
	}
};

void registryTests() {
	static_assert(!std::is_copy_constructible<dwt::util::RegKey>::value, "Registry keys must have one owner");
	RegistryFixture fixture;
	dwt::util::RegKey key(HKEY_CURRENT_USER, fixture.path);
	std::array<BYTE, 16> source;
	source.fill(0x55);
	fixture.value(L"blob", REG_BINARY, source.data(), source.size());
	std::array<BYTE, 16> target;
	target.fill(0xaa);
	rejects([&] { key.readBinary(_T("blob"), target.data(), 4); }, "Oversized binary was accepted");
	check(std::all_of(target.begin() + 4, target.end(), [](BYTE b) { return b == 0xaa; }),
		"Binary read overwrote the caller's buffer boundary");
	key.readBinary(_T("blob"), target.data(), target.size());
	check(source == target, "Binary round trip failed");
	rejects([&] { key.readBinary(_T("blob"), target.data(), -1); }, "Negative buffer size was accepted");
	rejects([&] { key.readBinary(_T("blob"), nullptr, 0); }, "Missing buffer was accepted");
	rejects([&] { key.readDword(_T("blob")); }, "Binary data was accepted as a DWORD");
	fixture.value(L"badDword", REG_DWORD, source.data(), source.size());
	rejects([&] { key.readDword(_T("badDword")); }, "Oversized DWORD was accepted");
	check(key.writeDword(_T("number"), 0x12345678), "DWORD write failed");
	check(key.readDword(_T("number")) == 0x12345678, "DWORD round trip failed");
	rejects([&] { key.readString(_T("number")); }, "DWORD was accepted as a string");
	fixture.value(L"unterminated", REG_SZ, L"hello", 5 * sizeof(wchar_t));
	check(key.readString(_T("unterminated")) == L"hello", "Unterminated registry string was not bounded");
	fixture.value(L"empty", REG_SZ, nullptr, 0);
	check(key.readString(_T("empty")).empty(), "Empty registry string failed");
	const std::wstring large(600000, L'x');
	fixture.value(L"large", REG_SZ, large.c_str(), (large.size() + 1) * sizeof(wchar_t));
	check(key.readString(_T("large")) == large, "Large registry string failed");
	// Add names after opening: cached enumeration lengths/counts are now stale.
	const std::wstring name(1000, L'n');
	fixture.value(name.c_str(), REG_SZ, L"v", 2 * sizeof(wchar_t));
	auto values = key.getValues();
	check(std::find(values.begin(), values.end(), name) != values.end(), "Registry enumeration missed new long value");
	const std::wstring childName(255, L'c');
	HKEY child = nullptr;
	check(::RegCreateKeyExW(fixture.handle, childName.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
		KEY_ALL_ACCESS, nullptr, &child, nullptr) == ERROR_SUCCESS, "Cannot create test subkey");
	::RegCloseKey(child);
	const auto children = key.getSubkeys();
	check(std::find(children.begin(), children.end(), childName) != children.end(), "Registry enumeration missed new subkey");
	dwt::util::RegKey writeOnly(HKEY_CURRENT_USER, fixture.path, KEY_SET_VALUE);
	check(writeOnly.writeDword(_T("limitedAccess"), 42), "Write-only registry access failed");
	dwt::util::RegKey queryOnly(HKEY_CURRENT_USER, fixture.path, KEY_QUERY_VALUE);
	check(queryOnly.readDword(_T("limitedAccess")) == 42, "Query-only registry access failed");
	const auto queriedValues = queryOnly.getValues();
	check(std::find(queriedValues.begin(), queriedValues.end(), L"limitedAccess") != queriedValues.end(),
		"Query-only value enumeration failed");
	dwt::util::RegKey enumerateOnly(HKEY_CURRENT_USER, fixture.path, KEY_ENUMERATE_SUB_KEYS);
	const auto enumeratedChildren = enumerateOnly.getSubkeys();
	check(std::find(enumeratedChildren.begin(), enumeratedChildren.end(), childName) != enumeratedChildren.end(),
		"Subkey enumeration with limited access failed");
	std::puts("PASS: registry bounds, types, DWORD writes, strings, changing enumeration and limited access");
}

void imageTests(dwt::Window* window) {
	auto bitmapSeed = dwt::Button::Seed();
	bitmapSeed.style |= BS_BITMAP;
	auto button = window->addChild(bitmapSeed);
	const auto bitmap = ::CreateBitmap(8, 8, 1, 1, nullptr);
	check(bitmap != nullptr, "CreateBitmap failed");
	button->setImage(dwt::BitmapPtr(new dwt::Bitmap(bitmap)));
	BITMAP info {};
	check(::GetObject(bitmap, sizeof(info), &info) != 0, "Button destroyed its installed bitmap");
	check(reinterpret_cast<HBITMAP>(button->sendMessage(BM_GETIMAGE, IMAGE_BITMAP)) == bitmap,
		"Button did not install the bitmap");
	button->setImage(dwt::BitmapPtr());
	check(!::GetObject(bitmap, sizeof(info), &info), "Clearing button image leaked bitmap");
	check(!button->sendMessage(BM_GETIMAGE, IMAGE_BITMAP), "Clearing button image retained handle");

	auto iconSeed = dwt::Button::Seed();
	iconSeed.style |= BS_ICON;
	auto iconButton = window->addChild(iconSeed);
	const auto icon = ::CopyIcon(::LoadIcon(nullptr, IDI_APPLICATION));
	check(icon != nullptr, "CopyIcon failed");
	iconButton->setImage(dwt::IconPtr(new dwt::Icon(icon)));
	ICONINFO iconInfo {};
	check(::GetIconInfo(icon, &iconInfo), "Button destroyed its installed icon");
	::DeleteObject(iconInfo.hbmMask);
	::DeleteObject(iconInfo.hbmColor);
	::DestroyWindow(iconButton->handle());
	check(!::GetIconInfo(icon, &iconInfo), "Destroying button leaked icon");
	std::puts("PASS: button bitmap/icon lifetime and null image clearing");
}

void conversionTests() {
	using Narrow = dwt::util::UnicodeConverter<wchar_t, char>;
	using Wide = dwt::util::UnicodeConverter<char, wchar_t>;
	using Codepage = dwt::util::ConversionCodepage;
	check(Narrow::doConvert(L"hello", Codepage::UTF8) == "hello", "UTF-8 encoding failed");
	check(Wide::doConvert("hello", Codepage::UTF8) == L"hello", "UTF-8 decoding failed");
	check(Wide::canConvert(static_cast<const char*>(nullptr), Codepage::UTF8), "Null conversion input failed");
	rejects([] { Narrow::doConvert(L"hello", static_cast<Codepage::Codepage>(0xffffffff)); },
		"Unsupported encoding was accepted");
	rejects([] { Wide::doConvert("hello", static_cast<Codepage::Codepage>(0xffffffff)); },
		"Unsupported decoding was accepted");
	std::puts("PASS: conversion errors and null input");
}
}

int dwtMain(dwt::Application& app) {
	dwt::Window* window = nullptr;
	int result = 0;
	try {
		conversionTests();
		window = new dwt::Window;
		auto seed = dwt::Window::Seed();
		seed.style &= ~WS_VISIBLE;
		window->create(seed);
		imageTests(window);
		if(app.getCommandLine().getParamsRaw().find(_T("--registry")) != dwt::tstring::npos)
			registryTests();
		else std::puts("Registry checks require --registry (creates and removes one volatile HKCU test key)");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	if(window && window->handle()) ::DestroyWindow(window->handle());
	return result;
}
