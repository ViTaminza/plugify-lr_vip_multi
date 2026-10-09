// lr_vip_multi — Plugify C++ port of the Pisex LR module "[LR][VIP] Multi"
// (ref/src/cs2-lvl_ranks_modules/LR_VIP_Multi/lr_vip_multi.cpp, 110 lines).
//
// What it does (identical to the original): registers the VIP feature "LrBoost"
// (VIP_BOOL, TOGGLABLE - the per-group value is a decimal like "3" or "2.5" in groups.ini) and,
// when a kill's experience is computed, multiplies it by that feature value for VIPs:
//
//     float fValue = g_pVIPCore->VIP_GetClientFeatureFloat(iClient, "LrBoost");
//     if (g_pVIPCore->VIP_IsClientVIP(iClient)) iExpCaused = int(iExpCaused * fValue);
//
// Port notes (the rest is parity):
//   * both cores are Plugify plugins now: the LR core through lr_api.hpp, the VIP core through
//     vip_api.hpp (the original fetched them with MetaFactory in AllPluginsLoaded and unloaded
//     itself when one was missing; a Plugify plugin declares them as dependencies instead).
//   * VIP_RegisterFeature runs in OnPluginStart: both dependencies are loaded before this plugin,
//     which is the moment the original registered it (AllPluginsLoaded).
//   * The original's `if CoreIsLoaded() ... else HookOnCoreIsReady(...)` check is kept (the
//     original lr_vip_multi only hooked; the check makes a plugin reload after the core finished
//     work as well).
//   * No config of its own; the feature values live in the vips' groups.ini.

#include <pisex/pisex.hpp>

#include <plg/plugin.hpp>

#include <lr_api.hpp>
#include <vip_api.hpp>

#include <chrono>
#include <exception>

#include "vip_multi_core.hpp"

namespace {

// GetLogTag() of the original.
constexpr char kLogTag[] = "[LR-VIP-MULTI]";

// The original's g_pLRCore / g_pVIPCore, moved to the consumer wrappers.
levels_ranks::LrApi* g_pLRCore = &levels_ranks::LrApi::Get();
vip::IVIPApi* g_pVIPCore = &vip::IVIPApi::Get();

// OnPlayerKilledPreHook() of the original.
void OnPlayerKilledPreHook(pisex::IGameEvent* hEvent, int& iExpCaused, int /*iExpVictim*/, int /*iExpAttacker*/) {
	if (hEvent == nullptr) {
		return;
	}
	const int iClient = hEvent->GetInt("attacker");
	if (iClient < 0 || iClient >= 64) {
		return; // the original fed the raw event value to the VIP core; the port guards the slot
	}
	const float fValue = g_pVIPCore->VIP_GetClientFeatureFloat(iClient, lr_vip_multi::kFeature.data());
	if (g_pVIPCore->VIP_IsClientVIP(iClient)) {
		iExpCaused = lr_vip_multi::ApplyMultiplier(iExpCaused, fValue);
	}
}

// OnCoreIsReadyHook() of the original.
void OnCoreIsReadyHook() {
	g_pLRCore->HookOnPlayerKilledPre(OnPlayerKilledPreHook);
}

class LrVipMultiPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;
};

LrVipMultiPlugin g_plugin;

plg::PluginResult LrVipMultiPlugin::OnPluginStart() {
	try {
		// <configs>/levels_ranks: the shared LR directory (the module has no own config file, but
		// the path resolver of pisexcompat is initialised the same way as in the sibling modules).
		pisex::Init("levels_ranks");

		// AllPluginsLoaded() of the original (VIP part first - a Plugify dependency is loaded
		// before this plugin, so the VIP core is already available here).
		g_pVIPCore->VIP_RegisterFeature(lr_vip_multi::kFeature.data(), vip::VIP_BOOL, vip::TOGGLABLE);

		if (g_pLRCore->CoreIsLoaded()) {
			OnCoreIsReadyHook();
		} else {
			g_pLRCore->HookOnCoreIsReady(OnCoreIsReadyHook);
		}
		return plg::PluginResult{};
	} catch (const std::exception& ex) {
		pisex::ErrorLog("%s OnPluginStart failed: %s", kLogTag, ex.what());
	} catch (...) {
		pisex::ErrorLog("%s OnPluginStart failed with an unknown exception", kLogTag);
	}
	return plg::PluginResult{}; // never Failed: that stops every plugify plugin
}

plg::PluginResult LrVipMultiPlugin::OnPluginUpdate(std::chrono::milliseconds /*dt*/) {
	try {
		pisex::Poll();
	} catch (const std::exception& ex) {
		pisex::ErrorLog("%s OnPluginUpdate failed: %s", kLogTag, ex.what());
	} catch (...) {
		pisex::ErrorLog("%s OnPluginUpdate failed with an unknown exception", kLogTag);
	}
	return plg::PluginResult{};
}

plg::PluginResult LrVipMultiPlugin::OnPluginEnd() {
	try {
		// The Unhook* are no-ops when levels_ranks is not loaded (lr_api.hpp Unregister checks the
		// null binding slot); the Available() guard only keeps that intent visible here.
		if (g_pLRCore->Available()) {
			g_pLRCore->UnhookOnPlayerKilledPre();
			g_pLRCore->UnhookOnCoreIsReady();
		}
		pisex::Shutdown();
	} catch (...) {
		// nothing left to do at unload
	}
	return plg::PluginResult{};
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
