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

/**
 * The PluginApiImpl class contains implementations of certain callback functions,
 * they are simply separated here.
 *
 * Notes:
 *	- dcpp may run HOOK_HUB_OFFLINE for hubs that never ran HOOK_HUB_ONLINE before (if socket creation fails in Client::connect)
 */

#include "stdinc.h"
#include <dcpp/plugins/PluginApiImpl.h>

#include <dcpp/protocol/AdcCommand.h>
#include <dcpp/hub/Client.h>
#include <dcpp/hub/ClientManager.h>
#include <dcpp/connection/ConnectionManager.h>
#include <dcpp/favorites/FavoriteManager.h>
#include <dcpp/system/File.h>
#include <dcpp/logging/LogManager.h>
#include <dcpp/plugins/PluginManager.h>
#include <dcpp/queue/QueueManager.h>
#include <dcpp/chat/Tagger.h>
#include <dcpp/connection/UserConnection.h>
#include <dcpp/version.h>
#include <dcpp/plugins/PluginHttp.h>
#include <mutex>
#include <type_traits>
#include <unordered_set>
#include <limits>
#include <filesystem>

namespace dcpp {

namespace {
template<auto Function> struct ApiBoundary;
template<class R, class... Args, R (DCAPI *Function)(Args...)>
struct ApiBoundary<Function> {
	static R DCAPI call(Args... args) noexcept {
		try { return Function(args...); }
		catch(...) { if constexpr(!std::is_void_v<R>) return R {}; }
	}
};
std::mutex udpMutex;
}

#define IMPL_HOOKS_COUNT 30

static const char* hookGuids[IMPL_HOOKS_COUNT] = {
	HOOK_CHAT_IN,
	HOOK_CHAT_OUT,
	HOOK_CHAT_PM_IN,
	HOOK_CHAT_PM_OUT,

	HOOK_TIMER_SECOND,
	HOOK_TIMER_MINUTE,

	HOOK_HUB_ONLINE,
	HOOK_HUB_OFFLINE,
	HOOK_USER_ONLINE,
	HOOK_USER_OFFLINE,

	HOOK_NETWORK_HUB_IN,
	HOOK_NETWORK_HUB_OUT,
	HOOK_NETWORK_CONN_IN,
	HOOK_NETWORK_CONN_OUT,
	HOOK_NETWORK_UDP_IN,
	HOOK_NETWORK_UDP_OUT,
	HOOK_NETWORK_UDP_IN2,
	HOOK_NETWORK_UDP_OUT2,

	HOOK_QUEUE_ADDED,
	HOOK_QUEUE_MOVED,
	HOOK_QUEUE_REMOVED,
	HOOK_QUEUE_FINISHED,

	HOOK_UI_CREATED,
	HOOK_UI_CHAT_TAGS,
	HOOK_UI_CHAT_DISPLAY,
	HOOK_UI_CHAT_COMMAND,
	HOOK_UI_CHAT_COMMAND_PM,

	HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION,
	HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION_FAILED,
	HOOK_DATAACESSOR_HTTP_RESOURCE_STREAM
};

static const char* hostName = APPNAME;

DCHooks PluginApiImpl::dcHooks = {
	DCINTF_HOOKS_VER,

	&ApiBoundary<&PluginApiImpl::createHook>::call,
	&ApiBoundary<&PluginApiImpl::destroyHook>::call,

	&ApiBoundary<&PluginApiImpl::bindHook>::call,
	&ApiBoundary<&PluginApiImpl::runHook>::call,
	&ApiBoundary<&PluginApiImpl::releaseHook>::call
};

DCConfig PluginApiImpl::dcConfig = {
	DCINTF_CONFIG_VER,

	&ApiBoundary<&PluginApiImpl::getPath>::call,
	&ApiBoundary<&PluginApiImpl::getInstallPath>::call,

	&ApiBoundary<&PluginApiImpl::setConfig>::call,
	&ApiBoundary<&PluginApiImpl::getConfig>::call,

	&ApiBoundary<&PluginApiImpl::getLanguage>::call,

	&ApiBoundary<static_cast<ConfigValuePtr (DCAPI *)(ConfigValuePtr)>(&PluginApiImpl::copyData)>::call,
	&ApiBoundary<static_cast<void (DCAPI *)(ConfigValuePtr)>(&PluginApiImpl::releaseData)>::call
};

DCLog PluginApiImpl::dcLog = {
	DCINTF_LOGGING_VER,

	&ApiBoundary<&PluginApiImpl::log>::call
};

DCConnection PluginApiImpl::dcConnection = {
	DCINTF_DCPP_CONNECTIONS_VER,

	&ApiBoundary<&PluginApiImpl::sendUdpData>::call,
	&ApiBoundary<static_cast<void (DCAPI *)(ConnectionDataPtr, const char*)>(&PluginApiImpl::sendProtocolCmd)>::call,
	&ApiBoundary<&PluginApiImpl::terminateConnection>::call,

	&ApiBoundary<&PluginApiImpl::getUserFromConn>::call
};

DCHub PluginApiImpl::dcHub = {
	DCINTF_DCPP_HUBS_VER,

	&ApiBoundary<&PluginApiImpl::addHub>::call,
	&ApiBoundary<&PluginApiImpl::findHub>::call,
	&ApiBoundary<&PluginApiImpl::removeHub>::call,

	&ApiBoundary<&PluginApiImpl::emulateProtocolCmd>::call,
	&ApiBoundary<static_cast<void (DCAPI *)(HubDataPtr, const char*)>(&PluginApiImpl::sendProtocolCmd)>::call,

	&ApiBoundary<&PluginApiImpl::sendHubMessage>::call,
	&ApiBoundary<&PluginApiImpl::sendLocalMessage>::call,
	&ApiBoundary<&PluginApiImpl::sendPrivateMessage>::call,

	&ApiBoundary<&PluginApiImpl::findUser>::call,
	&ApiBoundary<static_cast<UserDataPtr (DCAPI *)(UserDataPtr)>(&PluginApiImpl::copyData)>::call,
	&ApiBoundary<static_cast<void (DCAPI *)(UserDataPtr)>(&PluginApiImpl::releaseData)>::call,

	&ApiBoundary<static_cast<HubDataPtr (DCAPI *)(HubDataPtr)>(&PluginApiImpl::copyData)>::call,
	&ApiBoundary<static_cast<void (DCAPI *)(HubDataPtr)>(&PluginApiImpl::releaseData)>::call
};

DCQueue PluginApiImpl::dcQueue = {
	DCINTF_DCPP_QUEUE_VER,

	&ApiBoundary<&PluginApiImpl::addList>::call,
	&ApiBoundary<&PluginApiImpl::addDownload>::call,
	&ApiBoundary<&PluginApiImpl::findDownload>::call,
	&ApiBoundary<&PluginApiImpl::removeDownload>::call,

	&ApiBoundary<&PluginApiImpl::setPriority>::call,
	&ApiBoundary<&PluginApiImpl::pause>::call,

	&ApiBoundary<static_cast<QueueDataPtr (DCAPI *)(QueueDataPtr)>(&PluginApiImpl::copyData)>::call,
	&ApiBoundary<static_cast<void (DCAPI *)(QueueDataPtr)>(&PluginApiImpl::releaseData)>::call
};

DCUtils PluginApiImpl::dcUtils = {
	DCINTF_DCPP_UTILS_VER,

	&ApiBoundary<&PluginApiImpl::toUtf8>::call,
	&ApiBoundary<&PluginApiImpl::fromUtf8>::call,

	&ApiBoundary<&PluginApiImpl::Utf8toWide>::call,
	&ApiBoundary<&PluginApiImpl::WidetoUtf8>::call,

	&ApiBoundary<&PluginApiImpl::toBase32>::call,
	&ApiBoundary<&PluginApiImpl::fromBase32>::call
};

DCTagger PluginApiImpl::dcTagger = {
	DCINTF_DCPP_TAGGER_VER,

	&ApiBoundary<&PluginApiImpl::getText>::call,

	&ApiBoundary<&PluginApiImpl::addTag>::call,
	&ApiBoundary<&PluginApiImpl::replaceText>::call
};

DCDataAccess PluginApiImpl::dcDataAccess = {
	DCINTF_DCPP_DATAACCESSOR_VER,

	&ApiBoundary<&PluginApiImpl::getHTTPResource>::call,

	&ApiBoundary<static_cast<DataArrayPtr (DCAPI *)(DataArrayPtr)>(&PluginApiImpl::copyData)>::call,
	&ApiBoundary<static_cast<void (DCAPI *)(DataArrayPtr)>(&PluginApiImpl::releaseData)>::call
};

namespace {
template<class C, class F> size_t converted(C* dst, size_t capacity, F convert) noexcept {
	try {
		const auto value = convert();
		const auto required = value.size() + 1;
		if(dst && capacity >= required) memcpy(dst, value.c_str(), required * sizeof(C));
		return required;
	} catch(...) { return SIZE_MAX; }
}
size_t DCAPI utf8v2(char* dst, size_t capacity, const char* src) {
	return src ? converted(dst, capacity, [&] { return Text::toUtf8(src); }) : SIZE_MAX;
}
size_t DCAPI localv2(char* dst, size_t capacity, const char* src) {
	return src ? converted(dst, capacity, [&] { return Text::fromUtf8(src); }) : SIZE_MAX;
}
size_t DCAPI widev2(wchar_t* dst, size_t capacity, const char* src) {
	return src ? converted(dst, capacity, [&] { return Text::utf8ToWide(src); }) : SIZE_MAX;
}
size_t DCAPI narrowv2(char* dst, size_t capacity, const wchar_t* src) {
	return src ? converted(dst, capacity, [&] { return Text::wideToUtf8(src); }) : SIZE_MAX;
}
size_t DCAPI encodev2(char* dst, size_t capacity, const uint8_t* src, size_t length) {
	if((length && !src) || length > (SIZE_MAX - 4) / 8) return SIZE_MAX;
	return converted(dst, capacity, [&] { return Encoder::toBase32(src, length); });
}
size_t DCAPI decodev2(uint8_t* dst, size_t capacity, const char* src, size_t length) {
	if((length && !src) || length > SIZE_MAX / 5) return SIZE_MAX;
	const auto remainder = length % 8;
	if(remainder == 1 || remainder == 3 || remainder == 6) return SIZE_MAX;
	try {
		ByteVector bytes;
		bytes.reserve(length * 5 / 8);
		unsigned bits = 0, value = 0;
		for(size_t i = 0; i < length; ++i) {
			const unsigned char c = src[i];
			if(!((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'))) return SIZE_MAX;
			value = (value << 5) | (c >= 'A' ? c - 'A' : c - '2' + 26);
			bits += 5;
			if(bits >= 8) { bits -= 8; bytes.push_back(static_cast<uint8_t>(value >> bits)); }
		}
		if(bits && (value & ((1u << bits) - 1))) return SIZE_MAX;
		if(dst && capacity >= bytes.size() && !bytes.empty()) memcpy(dst, bytes.data(), bytes.size());
		return bytes.size();
	} catch(...) { return SIZE_MAX; }
}
Bool DCAPI drainSubscription(subsHandle subscription) {
	return PluginManager::getInstance()->drainHook(static_cast<HookSubscriber*>(subscription)) ? True : False;
}
DCHooks2 hooks2 { DCINTF_HOOKS2_VER, &ApiBoundary<&drainSubscription>::call };
DCUtils2 utils2 { DCINTF_DCPP_UTILS2_VER, utf8v2, localv2, widev2, narrowv2, encodev2, decodev2 };
}

Socket* PluginApiImpl::udpSocket = nullptr;
Socket& PluginApiImpl::getUdpSocket() {
	if(!udpSocket) {
		udpSocket = new Socket(Socket::TYPE_UDP);
	}
	return *udpSocket;
}

void PluginApiImpl::init() {
	auto& dcCore = *PluginManager::getInstance()->getCore();

	dcCore.apiVersion = DCAPI_CORE_VER;

	// Interface registry
	dcCore.register_interface = &ApiBoundary<&PluginApiImpl::registerInterface>::call;
	dcCore.query_interface = &ApiBoundary<&PluginApiImpl::queryInterface>::call;
	dcCore.release_interface = &ApiBoundary<&PluginApiImpl::releaseInterface>::call;

	// Core functions
	dcCore.has_plugin = &ApiBoundary<&PluginApiImpl::isLoaded>::call;
	dcCore.host_name = &ApiBoundary<&PluginApiImpl::hostName>::call;

	// Interfaces (since these outlast any plugin they don't need to be explictly released)
	dcCore.register_interface(DCINTF_HOOKS, &dcHooks);
	dcCore.register_interface(DCINTF_HOOKS2, &hooks2);
	static DCConfig2 config2 { DCINTF_CONFIG2_VER, &ApiBoundary<&PluginApiImpl::getConfig2>::call };
	dcCore.register_interface(DCINTF_CONFIG, &dcConfig);
	dcCore.register_interface(DCINTF_CONFIG2, &config2);
	dcCore.register_interface(DCINTF_LOGGING, &dcLog);

	dcCore.register_interface(DCINTF_DCPP_CONNECTIONS, &dcConnection);
	dcCore.register_interface(DCINTF_DCPP_HUBS, &dcHub);
	dcCore.register_interface(DCINTF_DCPP_QUEUE, &dcQueue);
	dcCore.register_interface(DCINTF_DCPP_UTILS, &dcUtils);
	dcCore.register_interface(DCINTF_DCPP_UTILS2, &utils2);
	dcCore.register_interface(DCINTF_DCPP_TAGGER, &dcTagger);
	dcCore.register_interface(DCINTF_DCPP_DATAACCESSOR, &dcDataAccess);

	// Create provided hooks (since these outlast any plugin they don't need to be explictly released)
	for(int i = 0; i < IMPL_HOOKS_COUNT; ++i)
		dcHooks.create_hook(hookGuids[i], NULL);
}

void PluginApiImpl::shutdown() {
	std::lock_guard<std::mutex> lock(udpMutex);
	if(udpSocket) {
		udpSocket->disconnect();
		delete udpSocket;
		udpSocket = nullptr;
	}
}

// Functions for DCCore
intfHandle PluginApiImpl::registerInterface(const char* guid, dcptr_t pInterface) {
	if(!guid || !pInterface) return nullptr;
	return PluginManager::getInstance()->registerInterface(guid, pInterface);
}

DCInterfacePtr PluginApiImpl::queryInterface(const char* guid, uint32_t version) {
	if(!guid) return nullptr;
	// we only return the registered interface if it is same or newer than requested
	DCInterface* dci = (DCInterface*)PluginManager::getInstance()->queryInterface(guid);
	return (!dci || dci->apiVersion >= version) ? dci : nullptr;
}

Bool PluginApiImpl::releaseInterface(intfHandle hInterface) {
	return PluginManager::getInstance()->releaseInterface(hInterface) ? True : False;
}

Bool PluginApiImpl::isLoaded(const char* guid) {
	if(!guid) return False;
	return PluginManager::getInstance()->isLoaded(guid) ? True : False;
}

const char* PluginApiImpl::hostName() {
	return dcpp::hostName;
}

// Functions for DCHook
hookHandle PluginApiImpl::createHook(const char* guid, DCHOOK defProc) {
	if(!guid) return nullptr;
	return PluginManager::getInstance()->createHook(guid, defProc);
}

Bool PluginApiImpl::destroyHook(hookHandle hHook) {
	Bool bRes = PluginManager::getInstance()->destroyHook(reinterpret_cast<PluginHook*>(hHook)) ? True : False;
	hHook = NULL;
	return bRes;
}

subsHandle PluginApiImpl::bindHook(const char* guid, DCHOOK hookProc, void* pCommon) {
	if(!guid || !hookProc) return nullptr;
	return PluginManager::getInstance()->bindHook(guid, hookProc, pCommon);
}

Bool PluginApiImpl::runHook(hookHandle hHook, dcptr_t pObject, dcptr_t pData) {
	return PluginManager::getInstance()->runHook(reinterpret_cast<PluginHook*>(hHook), pObject, pData) ? True : False;
}

size_t PluginApiImpl::releaseHook(subsHandle hHook) {
	return PluginManager::getInstance()->releaseHook(reinterpret_cast<HookSubscriber*>(hHook));
}

// Functions for DCConfig
ConfigStrPtr DCAPI PluginApiImpl::getPath(PathType type) {
	if(type < PATH_GLOBAL_CONFIG || type > PATH_LOCALE) return nullptr;
	auto str = Util::getPath(static_cast<Util::Paths>(type));
	ConfigStr value = { CFG_TYPE_STRING, str.c_str() };
	return reinterpret_cast<ConfigStrPtr>(copyData(reinterpret_cast<ConfigValuePtr>(&value)));
}

ConfigStrPtr DCAPI PluginApiImpl::getInstallPath(const char* guid) {
	if(!guid) return nullptr;
	auto str = PluginManager::getInstallPath(guid);
	const auto installed = PluginManager::getInstance()->getPlugin(guid);
	if(!installed.path.empty()) str = std::filesystem::u8path(installed.path).parent_path().u8string() + PATH_SEPARATOR_STR;
	ConfigStr value = { CFG_TYPE_STRING, str.c_str() };
	return reinterpret_cast<ConfigStrPtr>(copyData(reinterpret_cast<ConfigValuePtr>(&value)));
}

void PluginApiImpl::setConfig(const char* guid, const char* setting, ConfigValuePtr val) {
	if(!guid || !setting || !val) return;
	PluginManager* pm = PluginManager::getInstance();
	switch(val->type) {
		case CFG_TYPE_REMOVE:
			pm->removePluginSetting(guid, setting);
			break;
		case CFG_TYPE_STRING: {
			auto str = (ConfigStrPtr)val;
			pm->setPluginSetting(guid, setting, str->value ? str->value : "");
			break;
		}
		case CFG_TYPE_INT: {
			auto num = (ConfigIntPtr)val;
			pm->setPluginSetting(guid, setting, Util::toString(num->value));
			break;
		}
		case CFG_TYPE_BOOL: {
			auto num = (ConfigBoolPtr)val;
			pm->setPluginSetting(guid, setting, Util::toString(num->value));
			break;
		}
		case CFG_TYPE_INT64: {
			auto num = (ConfigInt64Ptr)val;
			pm->setPluginSetting(guid, setting, Util::toString(num->value));
			break;
		}
		default:
			break;
	}
}

ConfigValuePtr PluginApiImpl::getConfig(const char* guid, const char* setting, ConfigType type) {
	if(!guid || !setting) return nullptr;
	// Attempt to retrieve core setting...
	if(strcmp(guid, "CoreSetup") == 0) {
		int n;
		SettingsManager::Types coreType;
		if(SettingsManager::getInstance()->getType(setting, n, coreType)) {
			switch(coreType) {
				case SettingsManager::TYPE_STRING: {
					if(type != CFG_TYPE_UNKNOWN && type != CFG_TYPE_STRING) return nullptr;
					string settingValue = SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(n));
					ConfigStr value = { CFG_TYPE_STRING, settingValue.c_str() };

					return copyData((ConfigValuePtr)&value);
				}
				case SettingsManager::TYPE_INT: {
					if(type != CFG_TYPE_UNKNOWN && type != CFG_TYPE_INT) return nullptr;
					ConfigInt value = { CFG_TYPE_INT, SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(n)) };
					return copyData((ConfigValuePtr)&value);
				}
				case SettingsManager::TYPE_BOOL: {
					if(type != CFG_TYPE_UNKNOWN && type != CFG_TYPE_BOOL) return nullptr;
					ConfigBool value = { CFG_TYPE_BOOL, SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(n)) ? True : False };
					return copyData((ConfigValuePtr)&value);
				}
				case SettingsManager::TYPE_INT64: {
					if(type != CFG_TYPE_UNKNOWN && type != CFG_TYPE_INT64) return nullptr;
					ConfigInt64 value = { CFG_TYPE_INT64, SettingsManager::getInstance()->get(static_cast<SettingsManager::Int64Setting>(n)) };
					return copyData((ConfigValuePtr)&value);
				}
				default:
					break;
			}

			return NULL;
		}
		return nullptr;
	}

	return makeConfig(PluginManager::getInstance()->getPluginSetting(guid, setting), type);
}

ConfigValuePtr PluginApiImpl::getConfig2(const char* guid, const char* setting, ConfigType type) {
	if(!guid || !setting) return nullptr;
	if(strcmp(guid, "CoreSetup") == 0) return getConfig(guid, setting, type);
	string stored;
	if(!PluginManager::getInstance()->tryGetPluginSetting(guid, setting, stored)) return nullptr;
	return makeConfig(stored, type);
}

ConfigValuePtr PluginApiImpl::makeConfig(const string& stored, ConfigType type) {
	switch(type) {
		case CFG_TYPE_STRING: {
			ConfigStr value = { type, stored.c_str() };
			return copyData((ConfigValuePtr)&value);
		}
		case CFG_TYPE_INT: {
			ConfigInt value = { type, Util::toInt(stored) };
			return copyData((ConfigValuePtr)&value);
		}
		case CFG_TYPE_BOOL: {
			ConfigBool value = { type, Util::toInt(stored) != 0 ? True : False };
			return copyData((ConfigValuePtr)&value);
		}
		case CFG_TYPE_INT64: {
			ConfigInt64 value = { type, Util::toInt64(stored) };
			return copyData((ConfigValuePtr)&value);
		}
		default:
			break;
	}

	return NULL;
}

ConfigStrPtr DCAPI PluginApiImpl::getLanguage() {
	auto str = Util::getIETFLang();
	ConfigStr value = { CFG_TYPE_STRING, str.c_str() };
	return reinterpret_cast<ConfigStrPtr>(copyData(reinterpret_cast<ConfigValuePtr>(&value)));
}

ConfigValuePtr PluginApiImpl::copyData(const ConfigValuePtr val) {
	if(!val) return nullptr;
	switch(val->type) {
		case CFG_TYPE_STRING: {
			auto str = (ConfigStrPtr)val;
			if(!str->value) return nullptr;
			auto copy = (ConfigStrPtr)malloc(sizeof(ConfigStr));
			if(!copy) return nullptr;
			memcpy(copy, str, sizeof(ConfigStr));

			size_t bufLen = strlen(str->value) + 1;
			copy->value = (char*)malloc(bufLen);
			if(!copy->value) { free(copy); return nullptr; }
			strncpy((char*)copy->value, str->value, bufLen);

			return (ConfigValuePtr)copy;
		}
		case CFG_TYPE_INT: {
			auto num = (ConfigIntPtr)val;
			auto copy = (ConfigIntPtr)malloc(sizeof(ConfigInt));
			if(!copy) return nullptr;
			memcpy(copy, num, sizeof(ConfigInt));

			return (ConfigValuePtr)copy;
		}
		case CFG_TYPE_BOOL: {
			auto num = (ConfigBoolPtr)val;
			auto copy = (ConfigBoolPtr)malloc(sizeof(ConfigBool));
			if(!copy) return nullptr;
			memcpy(copy, num, sizeof(ConfigBool));
			
			return (ConfigValuePtr)copy;
		}
		case CFG_TYPE_INT64: {
			auto num = (ConfigInt64Ptr)val;
			auto copy = (ConfigInt64Ptr)malloc(sizeof(ConfigInt64));
			if(!copy) return nullptr;
			memcpy(copy, num, sizeof(ConfigInt64));

			return (ConfigValuePtr)copy;
		}
		default:
			break;
	}

	return NULL;
}

void PluginApiImpl::releaseData(ConfigValuePtr val) {
	if(!val) return;
	switch(val->type) {
		case CFG_TYPE_STRING: {
			auto str = (ConfigStrPtr)val;
			free((char*)str->value);
			free(str);
			break;
		}
		case CFG_TYPE_INT: {
			auto num = (ConfigIntPtr)val;
			free(num);
			break;
		}
		case CFG_TYPE_BOOL: {
			auto num = (ConfigBoolPtr)val;
			free(num);
			break;
		}
		case CFG_TYPE_INT64: {
			auto num = (ConfigInt64Ptr)val;
			free(num);
			break;
		}
		default:
			break;
	}
}

// Functions for DCLog
void PluginApiImpl::log(const char* msg) {
	if(!msg) return;
	LogManager::getInstance()->message(msg, LogMessage::SEV_INFO, _("Plugins"));
}

// Functions for DCConnection
void PluginApiImpl::sendProtocolCmd(ConnectionDataPtr conn, const char* cmd) {
	if(!PluginManager::isCallbackObject(conn) || !conn->object || !cmd) return;
	reinterpret_cast<UserConnection*>(conn->object)->sendRaw(cmd);
}

void PluginApiImpl::terminateConnection(ConnectionDataPtr conn, Bool graceless) {
	if(!PluginManager::isCallbackObject(conn) || !conn->object) return;
	reinterpret_cast<UserConnection*>(conn->object)->disconnect(graceless != False);
}

void PluginApiImpl::sendUdpData(const char* ip, uint32_t port, dcptr_t data, size_t n) {
	if(!ip || !data || !n || port > 65535) return;
	std::lock_guard<std::mutex> lock(udpMutex);
	try { getUdpSocket().writeTo(ip, Util::toString(port), data, n); } catch (const Exception&) { /* ... */ }
}

UserDataPtr PluginApiImpl::getUserFromConn(ConnectionDataPtr conn) {
	if(!PluginManager::isCallbackObject(conn) || !conn->object) return nullptr;
	auto user = reinterpret_cast<UserConnection*>(conn->object)->getHintedUser();
	if(!user.user) { return nullptr; }

	auto lock = ClientManager::getInstance()->lock();
	auto ou = ClientManager::getInstance()->findOnlineUser(user);
	if(!ou) { return nullptr; }
	return ou->copyPluginObject();
}

// Functions for DCUtils
size_t PluginApiImpl::toUtf8(char* dst, const char* src, size_t n) {
	if(!src) return 0;
	string str(Text::toUtf8(src));
	if(dst && n >= str.size()) {
		memcpy(dst, str.data(), str.size());
		if(n > str.size()) dst[str.size()] = 0;
	}
	return str.size();
}

size_t PluginApiImpl::fromUtf8(char* dst, const char* src, size_t n) {
	if(!src) return 0;
	string str(Text::fromUtf8(src));
	if(dst && n >= str.size()) {
		memcpy(dst, str.data(), str.size());
		if(n > str.size()) dst[str.size()] = 0;
	}
	return str.size();
}

size_t PluginApiImpl::Utf8toWide(wchar_t* dst, const char* src, size_t n) {
	if(!src) return 0;
	wstring str(Text::utf8ToWide(src));
	if(dst && n >= str.size()) {
		memcpy(dst, str.data(), str.size() * sizeof(wchar_t));
		if(n > str.size()) dst[str.size()] = 0;
	}
	return str.size();
}

size_t PluginApiImpl::WidetoUtf8(char* dst, const wchar_t* src, size_t n) {
	if(!src) return 0;
	string str(Text::wideToUtf8(src));
	if(dst && n >= str.size()) {
		memcpy(dst, str.data(), str.size());
		if(n > str.size()) dst[str.size()] = 0;
	}
	return str.size();
}

size_t PluginApiImpl::toBase32(char* dst, const uint8_t* src, size_t n) {
	if((n && !src) || n > (std::numeric_limits<size_t>::max() - 4) / 8) return 0;
	const auto encoded = Encoder::toBase32(src, n);
	if(dst) memcpy(dst, encoded.data(), encoded.size());
	return encoded.size();
}

size_t PluginApiImpl::fromBase32(uint8_t* dst, const char* src, size_t n) {
	if(!src || !dst || !n) return 0;
	const auto count = std::min(n, strlen(src) * 5 / 8);
	Encoder::fromBase32(src, dst, count);
	return count;
}

// Functions for DCTagger
const char* PluginApiImpl::getText(TagDataPtr hTags) {
	if(!hTags || !hTags->object) return nullptr;
	return reinterpret_cast<Tagger*>(hTags->object)->getText().c_str();
}

void PluginApiImpl::addTag(TagDataPtr hTags, size_t start, size_t end, const char* id, const char* attributes) {
	if(!hTags || !hTags->object || !id || !attributes) return;
	reinterpret_cast<Tagger*>(hTags->object)->addTag(start, end, id, attributes);
}

void PluginApiImpl::replaceText(TagDataPtr hTags, size_t start, size_t end, const char* replacement) {
	if(!hTags || !hTags->object || !replacement) return;
	reinterpret_cast<Tagger*>(hTags->object)->replaceText(start, end, replacement);
}

// Functions for DCQueue
QueueDataPtr PluginApiImpl::addList(UserDataPtr user, Bool silent) {
	if(!user || !user->cid || !user->hubHint) return nullptr;
	auto u = ClientManager::getInstance()->findUser(CID(user->cid));
	if(!u) return NULL;

	QueueData* data = NULL;
	try {
		QueueManager::getInstance()->addList(HintedUser(u, user->hubHint), silent ? 0 : QueueItem::FLAG_CLIENT_VIEW);
		data = findDownload(QueueManager::getInstance()->getListPath(HintedUser(u, user->hubHint)).c_str());
	} catch(const Exception& e) {
		LogManager::getInstance()->message(e.getError(), LogMessage::SEV_ERROR, _("Queue"));
	}

	return data;
}

QueueDataPtr PluginApiImpl::addDownload(const char* hash, uint64_t size, const char* target) {
	if(!hash || strlen(hash) != 39 || !target || size > INT64_MAX) return nullptr;
	QueueData* data = NULL;
	try {
		string sTarget = File::isAbsolute(target) ? target : SETTING(DOWNLOAD_DIRECTORY) + target;
		QueueManager::getInstance()->add(sTarget, size, TTHValue(hash), HintedUser(UserPtr(), Util::emptyString));
		data = findDownload(sTarget.c_str());
	} catch(const Exception& e) {
		LogManager::getInstance()->message(e.getError(), LogMessage::SEV_ERROR, _("Queue"));
	}

	return data;
}

QueueDataPtr PluginApiImpl::findDownload(const char* target) {
	if(!target) return nullptr;
	QueueData* data = NULL;
	QueueManager::getInstance()->lockedOperation([&data, target](const QueueItem::StringMap& lst) {
		string sTarget = target;
		auto i = lst.find(&sTarget);
		if (i != lst.end()) {
			data = i->second->copyPluginObject();
		}
	});

	return data;
}

void PluginApiImpl::removeDownload(QueueDataPtr qi) {
	if(!qi || !qi->target) return;
	QueueManager::getInstance()->remove(qi->target);
}

void PluginApiImpl::setPriority(QueueDataPtr qi, QueuePrio priority) {
	if(!qi || !qi->target || static_cast<int>(priority) < QueueItem::DEFAULT || static_cast<int>(priority) >= QueueItem::LAST) return;
	QueueManager::getInstance()->setPriority(qi->target, static_cast<int>(priority) == QueueItem::DEFAULT ? QueueItem::NORMAL : static_cast<QueueItem::Priority>(priority));
}

Bool PluginApiImpl::pause(QueueDataPtr qi) {
	if(!qi || !qi->target) return False;
	return QueueManager::getInstance()->togglePause(qi->target) ? True : False;
}

QueueDataPtr PluginApiImpl::copyData(const QueueDataPtr qi) {
	if(!qi || !qi->target || !qi->location || !qi->hash) return nullptr;
	auto copy = static_cast<QueueDataPtr>(malloc(sizeof(QueueData)));
	if(!copy) return nullptr;
	*copy = *qi;
	copy->target = nullptr;
	copy->location = nullptr;
	copy->hash = nullptr;
	copy->isManaged = False;
	copy->target = static_cast<char*>(malloc(strlen(qi->target) + 1));
	if(!copy->target) { releaseData(copy); return nullptr; }
	memcpy(const_cast<char*>(copy->target), qi->target, strlen(qi->target) + 1);
	copy->location = static_cast<char*>(malloc(strlen(qi->location) + 1));
	if(!copy->location) { releaseData(copy); return nullptr; }
	memcpy(const_cast<char*>(copy->location), qi->location, strlen(qi->location) + 1);
	copy->hash = static_cast<char*>(malloc(strlen(qi->hash) + 1));
	if(!copy->hash) { releaseData(copy); return nullptr; }
	memcpy(const_cast<char*>(copy->hash), qi->hash, strlen(qi->hash) + 1);
	return copy;
}

void PluginApiImpl::releaseData(QueueDataPtr qi) {
	if(!qi) return;
	if(qi->isManaged) {
		LogManager::getInstance()->message("Plugin trying to free a managed object !", LogMessage::SEV_WARNING, _("Plugins"));
		return;
	}

	free((char*)qi->target);
	free((char*)qi->location);
	free((char*)qi->hash);

	free(qi);
}

// Functions for DCHub
HubDataPtr PluginApiImpl::addHub(const char* url, const char* nick, const char* password) {
	if(!url || !*url) return nullptr;
	auto lock = ClientManager::getInstance()->lock();
	if(auto existing = findHub(url)) return existing;
	try {
		auto client = ClientManager::getInstance()->getClient(url);
		client->connectWithCredentials(nick ? nick : "", password ? password : "");
		return client->copyPluginObject(); // accepted asynchronous connection
	} catch(...) { return nullptr; }
}

namespace {
Client* currentHub(HubDataPtr hub) {
	if(!hub || !hub->url) return nullptr;
	for(auto client: ClientManager::getInstance()->getClients()) {
		if(hubHintsEqual(client->getHubUrl(), hub->url)) return client;
	}
	return nullptr;
}
}

HubDataPtr PluginApiImpl::findHub(const char* url) {
	if(!url) return nullptr;
	auto lock = ClientManager::getInstance()->lock();
	const auto& list = ClientManager::getInstance()->getClients();

	for(auto i = list.begin(); i != list.end(); ++i) {
		if(hubHintsEqual((*i)->getHubUrl(), url))
			 return (*i)->copyPluginObject();
	}

	return NULL;
}

void PluginApiImpl::removeHub(HubDataPtr hub) {
	auto lock = ClientManager::getInstance()->lock();
	if(auto client = currentHub(hub)) ClientManager::getInstance()->putClient(client);
}

void PluginApiImpl::emulateProtocolCmd(HubDataPtr hub, const char* cmd) {
	if(!cmd) return;
	auto lock = ClientManager::getInstance()->lock();
	if(auto client = currentHub(hub)) client->emulateCommand(cmd);
}

void PluginApiImpl::sendProtocolCmd(HubDataPtr hub, const char* cmd) {
	if(!cmd) return;
	auto lock = ClientManager::getInstance()->lock();
	auto client = currentHub(hub);
	if(!client) return;
	if(hub->protocol != PROTOCOL_ADC) {
		client->send(cmd);
		return;
	}

	string line(cmd);
	while(!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
		line.pop_back();
	}
	try {
		client->send(AdcCommand(line));
	} catch(const ParseException&) {
		dcdebug("Refusing malformed plugin ADC hub command: %.50s\n", line.c_str());
	}
}

void PluginApiImpl::sendHubMessage(HubDataPtr hub, const char* message, Bool thirdPerson) {
	if(!message) return;
	auto lock = ClientManager::getInstance()->lock();
	if(auto client = currentHub(hub)) {

		// Lets make plugins life easier...
		ParamMap params;
		client->getMyIdentity().getParams(params, "my", true);
		client->getHubIdentity().getParams(params, "hub", false);

		client->hubMessage(Util::formatParams(message, params), thirdPerson != False);
	}
}

void PluginApiImpl::sendLocalMessage(HubDataPtr hub, const char* msg, MsgType type) {
	if(!msg) return;
	auto lock = ClientManager::getInstance()->lock();
	auto client = currentHub(hub);
	if(!client) return;
	client->fire(ClientListener::ClientLine(), client, msg, type);
}

Bool PluginApiImpl::sendPrivateMessage(UserDataPtr user, const char* message, Bool thirdPerson) {
	if(user && user->cid && user->hubHint && message) {
		auto lock = ClientManager::getInstance()->lock();
		OnlineUser* ou = ClientManager::getInstance()->findOnlineUser(CID(user->cid), user->hubHint);
		if(!ou) return False;

		// Lets make plugins life easier...
		ParamMap params;
		ou->getIdentity().getParams(params, "user", true);
		ou->getClient().getMyIdentity().getParams(params, "my", true);
		ou->getClient().getHubIdentity().getParams(params, "hub", false);

		return ClientManager::getInstance()->privateMessage(HintedUser(ou->getUser(), user->hubHint),
			Util::formatParams(message, params), thirdPerson != False) ? True : False;
	}
	return False;
}

HubDataPtr PluginApiImpl::copyData(const HubDataPtr hub) {
	if(!hub || !hub->url || !hub->ip) return nullptr;
	HubDataPtr copy = (HubDataPtr)malloc(sizeof(HubData));
	if(!copy) return nullptr;
	memcpy(copy, hub, sizeof(HubData));

	size_t bufLen = strlen(hub->url) + 1;
	copy->url = (char*)malloc(bufLen);
	if(!copy->url) { free(copy); return nullptr; }
	strncpy((char*)copy->url, hub->url, bufLen);

	bufLen = strlen(hub->ip) + 1;
	copy->ip = (char*)malloc(bufLen);
	if(!copy->ip) { free(const_cast<char*>(copy->url)); free(copy); return nullptr; }
	memcpy((char*)copy->ip, hub->ip, bufLen);

	copy->isManaged = False;
	return copy;
}

void PluginApiImpl::releaseData(HubDataPtr hub) {
	if(!hub) return;
	if(hub->isManaged) {
		LogManager::getInstance()->message("Plugin trying to free a managed object !", LogMessage::SEV_WARNING, _("Plugins"));
		return;
	}

	free((char*)hub->url);
	free((char*)hub->ip);

	free(hub);
}

UserDataPtr PluginApiImpl::findUser(const char* cid, const char* hubUrl) {
	if(!cid || strlen(cid) != 39 || !hubUrl) return nullptr;
	auto lock = ClientManager::getInstance()->lock();
	OnlineUser* ou = ClientManager::getInstance()->findOnlineUser(CID(cid), hubUrl);
	if(!ou) return NULL;
	return ou->copyPluginObject();
}

UserDataPtr PluginApiImpl::copyData(const UserDataPtr user) {
	if(!user || !user->nick || !user->hubHint || !user->cid) return nullptr;
	auto copy = static_cast<UserDataPtr>(malloc(sizeof(UserData)));
	if(!copy) return nullptr;
	*copy = *user;
	copy->nick = nullptr;
	copy->hubHint = nullptr;
	copy->cid = nullptr;
	copy->isManaged = False;
	copy->nick = static_cast<char*>(malloc(strlen(user->nick) + 1));
	if(!copy->nick) { releaseData(copy); return nullptr; }
	memcpy(const_cast<char*>(copy->nick), user->nick, strlen(user->nick) + 1);
	copy->hubHint = static_cast<char*>(malloc(strlen(user->hubHint) + 1));
	if(!copy->hubHint) { releaseData(copy); return nullptr; }
	memcpy(const_cast<char*>(copy->hubHint), user->hubHint, strlen(user->hubHint) + 1);
	copy->cid = static_cast<char*>(malloc(strlen(user->cid) + 1));
	if(!copy->cid) { releaseData(copy); return nullptr; }
	memcpy(const_cast<char*>(copy->cid), user->cid, strlen(user->cid) + 1);
	return copy;
}

void PluginApiImpl::releaseData(UserDataPtr user) {
	if(!user) return;
	if(user->isManaged) {
		LogManager::getInstance()->message("Plugin trying to free a managed object !", LogMessage::SEV_WARNING, _("Plugins"));
		return;
	}

	free((char*)user->nick);
	free((char*)user->hubHint);
	free((char*)user->cid);

	free(user);
}

/* Functions for DataAccess */
void PluginApiImpl::getHTTPResource(const char* uri, const char* localPath) {
	if(!uri) return;
	try {
		if(PluginHttp::submit(uri, localPath ? localPath : "")) return;
	} catch(...) { }
	PluginManager::getInstance()->runHook(HOOK_DATAACESSOR_HTTP_RESOURCE_NOTIFICATION_FAILED, const_cast<char*>(uri));
}

namespace {
std::mutex dataCopiesMutex;
std::unordered_set<DataArrayPtr> dataCopies;
}

DataArrayPtr PluginApiImpl::copyData(const DataArrayPtr val)
{
	if(!val || (val->size && !val->pData) || val->size > std::numeric_limits<size_t>::max()) return nullptr;
	DataArrayPtr copy = (DataArrayPtr)malloc(sizeof(DataArray));
	if(!copy) return nullptr;
	copy->size = val->size;
	copy->pData = nullptr;
	if(val->size) {
		auto data = malloc(static_cast<size_t>(val->size));
		if(!data) { free(copy); return nullptr; }
		memcpy(data, val->pData, static_cast<size_t>(val->size));
		copy->pData = data;
	}

	try { std::lock_guard<std::mutex> lock(dataCopiesMutex); dataCopies.insert(copy); }
	catch(...) { free(const_cast<void*>(copy->pData)); free(copy); return nullptr; }
	return copy;
}

void PluginApiImpl::releaseData(DataArrayPtr val)
{
	if(!val) return;
	{ std::lock_guard<std::mutex> lock(dataCopiesMutex); if(!dataCopies.erase(val)) return; }
	free(const_cast<void*>(val->pData));

	free(val);
}

} // namespace dcpp
