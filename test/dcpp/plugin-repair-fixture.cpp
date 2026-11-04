// Copyright (C) 2026 iceman50. GPL-3.0-or-later.
#include <dcpp/plugins/PluginDefs.h>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
const char* id = "{15503cbe-b840-4ab8-bc95-2ebf2646afe7}";
struct Counters { uint32_t apiVersion, unloads; } table { 1, 0 };
dcBool DCAPI tick(dcptr_t, dcptr_t, dcptr_t, Bool*) { return True; }
dcBool DCAPI mainCallback(PluginState state, DCCorePtr core, dcptr_t) {
	if(state == ON_UNLOAD || state == ON_UNINSTALL) { ++table.unloads; return False; }
	if(state == ON_CONFIGURE) return True;
	auto config = reinterpret_cast<DCConfigPtr>(core->query_interface(DCINTF_CONFIG, 1));
	ConfigStr setting { CFG_TYPE_STRING, "saved-during-initialization" };
	config->set_cfg(id, "fixture", reinterpret_cast<ConfigValuePtr>(&setting));
	core->register_interface("fixture.interface", &table);
	auto hooks = reinterpret_cast<DCHooksPtr>(core->query_interface(DCINTF_HOOKS, DCINTF_HOOKS_VER));
	hooks->bind_hook(HOOK_TIMER_SECOND, tick, nullptr);
#ifdef _WIN32
	char fail[4] {};
	if(GetEnvironmentVariableA("DCPP_PLUGIN_FIXTURE_FAIL", fail, sizeof(fail))) return False;
#endif
	return True;
}
}

extern "C" DCEXP DCMAIN DCAPI pluginInit(MetaDataPtr info) {
	table = { 1, 0 };
	*info = {};
	info->name = "Plugin repair regression fixture";
	info->guid = id;
	info->version = 1.0;
	info->apiVersion = DCAPI_CORE_VER;
	return mainCallback;
}
