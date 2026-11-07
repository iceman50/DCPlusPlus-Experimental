/* Copyright (C) 2026 iceman50. GPL-3.0-or-later. */
#include "stdafx.h"
#include <win32/services/SystemStats.h>
#include <dcpp/util/Util.h>
#include <dcpp/net/Socket.h>
#include <dcpp/version.h>
#include <psapi.h>
#include <dxgi.h>
#include <cstring>

namespace SystemStats {
namespace {
using namespace dcpp;

string clean(string value) {
	for(auto& c : value)
		if(static_cast<unsigned char>(c) < 32 || c == '|') c = ' ';
	const auto last = value.find_last_not_of(' ');
	if(last == string::npos) return "Unavailable";
	value.resize(last + 1);
	value.erase(0, value.find_first_not_of(' '));
	return value;
}

string bytes(uint64_t value) { return Util::formatBytes(static_cast<int64_t>(value)); }

string duration(uint64_t seconds) {
	return std::to_string(seconds / 86400) + "d " + Util::formatSeconds(seconds % 86400);
}

string architecture() {
	SYSTEM_INFO info {};
	GetNativeSystemInfo(&info);
	switch(info.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
	case PROCESSOR_ARCHITECTURE_ARM64: return "ARM64";
	case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
	default: return "Unknown architecture";
	}
}

string windowsVersion() {
	OSVERSIONINFOEXW info {};
	info.dwOSVersionInfoSize = sizeof(info);
	using VersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
	VersionFn version = nullptr;
	const auto module = GetModuleHandleW(L"ntdll.dll");
	const auto proc = module ? GetProcAddress(module, "RtlGetVersion") : nullptr;
	static_assert(sizeof(version) == sizeof(proc), "Function pointer size");
	std::memcpy(&version, &proc, sizeof(version));
	if(!version || version(reinterpret_cast<OSVERSIONINFOW*>(&info)) != 0)
		return "Windows / " + architecture();
	const string name = info.wProductType != VER_NT_WORKSTATION ? "Windows Server" :
		info.dwMajorVersion == 10 ? (info.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10") : "Windows";
	return name + " " + std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) +
		" (build " + std::to_string(info.dwBuildNumber) + ") / " + architecture();
}

string cpuInfo() {
	wchar_t name[256] {};
	DWORD size = sizeof(name);
	string cpu = "Unknown CPU";
	if(RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
		L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) == ERROR_SUCCESS)
		cpu = clean(Text::fromT(name));

	// One record per physical core, across all processor groups. Count masks
	// separately so SMT threads are never mislabeled as physical cores.
	DWORD length = 0;
	GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
	vector<uint8_t> data(length);
	unsigned cores = 0, threads = 0;
	if(length && GetLogicalProcessorInformationEx(RelationProcessorCore,
		reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(data.data()), &length)) {
		for(size_t offset = 0; offset + offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor) <= length;) {
			const auto record = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(data.data() + offset);
			if(!record->Size || record->Size > length - offset) break;
			if(record->Relationship == RelationProcessorCore) {
				++cores;
				for(WORD group = 0; group < record->Processor.GroupCount; ++group)
					for(auto mask = record->Processor.GroupMask[group].Mask; mask; mask &= mask - 1) ++threads;
			}
			offset += record->Size;
		}
	}
	if(!threads) threads = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	return cpu + " / " + (cores ? std::to_string(cores) : "unknown") + " cores / " + std::to_string(threads) + " threads";
}

vector<string> gpuInfo() {
	vector<string> result;
	const auto module = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if(!module) return result;
	using FactoryFn = HRESULT (WINAPI*)(REFIID, void**);
	FactoryFn create = nullptr;
	const auto proc = GetProcAddress(module, "CreateDXGIFactory");
	static_assert(sizeof(create) == sizeof(proc), "Function pointer size");
	std::memcpy(&create, &proc, sizeof(create));
	IDXGIFactory* factory = nullptr;
	if(create && SUCCEEDED(create(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)))) {
		for(UINT index = 0; index < 16; ++index) {
			IDXGIAdapter* adapter = nullptr;
			if(factory->EnumAdapters(index, &adapter) != S_OK) break;
			DXGI_ADAPTER_DESC desc {};
			if(SUCCEEDED(adapter->GetDesc(&desc))) {
				// Microsoft's software renderer isn't a physical GPU.
				if(!(desc.VendorId == 0x1414 && desc.DeviceId == 0x8c))
					result.push_back(clean(Text::fromT(desc.Description)) + " / " + bytes(desc.DedicatedVideoMemory) +
						" dedicated VRAM / " + bytes(desc.SharedSystemMemory) + " shared memory limit");
			}
			adapter->Release();
		}
		factory->Release();
	}
	FreeLibrary(module);
	return result;
}

uint64_t fileTime(const FILETIME& value) {
	return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
}

std::string report() {
	// BDC++-style sections use ASCII decoration so the report also survives
	// legacy NMDC encodings. No right border depends on the chat font's width.
	string result = "\n_[ " FULL_APPNAME " " VERSIONSTRING " :: System Stats ]_";
	auto section = [&](const string& title) { result += "\n |\n |---[ " + title + " ]"; };
	auto line = [&](const string& label, const string& value) { result += "\n |   [" + label + "]  " + clean(value); };
	section("Client");
	line("APP", FULL_APPNAME " " VERSIONSTRING " / " + buildLabel +
#ifdef _WIN64
		" / 64-bit");
#else
		" / 32-bit");
#endif
	PROCESS_MEMORY_COUNTERS_EX process {};
	process.cb = sizeof(process);
	if(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process)))
		line("MEM", bytes(process.WorkingSetSize) + " working set / " + bytes(process.PrivateUsage) + " private bytes");
	else line("MEM", "Unavailable");
	section("System");
	line("OS ", windowsVersion());
	line("CPU", cpuInfo());
	section("Memory");
	MEMORYSTATUSEX memory {};
	memory.dwLength = sizeof(memory);
	if(GlobalMemoryStatusEx(&memory))
		line("RAM", bytes(memory.ullTotalPhys - memory.ullAvailPhys) + " used / " + bytes(memory.ullTotalPhys) +
			" total (" + std::to_string(memory.dwMemoryLoad) + "%)");
	else line("RAM", "Unavailable");
	section("Graphics");
	const auto gpus = gpuInfo();
	if(gpus.empty()) line("GPU", "Unavailable");
	for(const auto& gpu : gpus) line("GPU", gpu);
	section("Storage");
	// Query ready local volumes only: no network paths, labels, or serial numbers.
	const auto drives = GetLogicalDrives();
	unsigned count = 0;
	for(unsigned index = 0; index < 26; ++index) {
		if(!(drives & (1UL << index))) continue;
		wchar_t root[] = L"A:\\";
		root[0] += static_cast<wchar_t>(index);
		if(GetDriveTypeW(root) != DRIVE_FIXED) continue;
		ULARGE_INTEGER available {}, total {}, free {};
		if(GetDiskFreeSpaceExW(root, &available, &total, &free)) {
			line(string(1, static_cast<char>('A' + index)) + ": ", bytes(total.QuadPart) +
				" total / " + bytes(free.QuadPart) + " free");
			++count;
		}
	}
	if(!count) line("DSK", "Unavailable");
	section("Session");
	FILETIME created {}, exited {}, kernel {}, user {}, now {};
	GetSystemTimeAsFileTime(&now);
	if(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) && fileTime(now) >= fileTime(created))
		line("CLI", duration((fileTime(now) - fileTime(created)) / 10000000) + " client uptime");
	line("SYS", duration(GetTickCount64() / 1000) + " system uptime");
	line("NET", bytes(Socket::getTotalDown()) + " downloaded / " + bytes(Socket::getTotalUp()) + " uploaded");
	return result + "\n |\n `---[ End of System Stats ]---";
}
}
