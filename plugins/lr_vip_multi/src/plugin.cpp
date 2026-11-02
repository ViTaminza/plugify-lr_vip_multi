// lr_vip_multi — Plugify C++ port of the Pisex LR module "[LR][VIP] Multi"
// (the original's lr_vip_multi.cpp).
//
// What it does (identical to the original): registers the VIP feature "LrBoost"
// (VIP_BOOL, TOGGLABLE - the per-group value is a decimal like "3" or "2.5" in groups.ini) and,
// when a kill's experience is computed, multiplies it by that feature value for VIPs.
//
// Pure s2sdk (docs/PLUGIN_STANDARD.md §7): no pisexcompat and no guard library. The safety rules of
// handoff/weaponskins/PITFALLS.md therefore live right here:
//   * B1/B2 map gate: OnMapStart/OnMapEnd listeners are registered in OnPluginStart; the gate
//     g_mapReady is opened by OnMapStart (or by the first OnPluginUpdate when the plugin was loaded
//     into a running map) and dropped by OnMapEnd. The kill hook runs only while the gate is open:
//     the VIP core resolves the attacker's identity through its own client lookups, so a kill that
//     arrives in the OnMapEnd window (C13: events keep arriving during a changelevel) must not be
//     forwarded.
//   * player_death.attacker is a player-typed field ("player_controller_and_pawn"), so its slot is
//     read with s2sdk::GetEventPlayerSlot - the engine's IGameEvent::GetPlayerSlot - and never with
//     GetEventInt. GetEventInt is a different engine call and answers the raw event value (the
//     engine user id), which is not a slot; there is deliberately no fallback to it. The read is
//     guarded by the binding slot, wrapped in try/catch and the answer is bounds-checked to 0..63;
//     -1 is INVALID_PLAYER_SLOT_INDEX (the world / no player behind the field).
//   * A7 unload: OnPluginEnd unhooks the LR callbacks, removes the map listeners and closes the
//     gate, so `plg reload` leaves nothing behind that points into the unloaded library.
//   * There is no other per-frame work: OnPluginUpdate only latches the map gate for a plugin that
//     was loaded into a running map and never saw an OnMapStart.
//
// Port notes (the rest is parity):
//   * both cores are Plugify plugins now: the LR core through lr_api.hpp, the VIP core through
//     vip_api.hpp (the original fetched them with MetaFactory in AllPluginsLoaded and unloaded
//     itself when one was missing; a Plugify plugin declares them as dependencies instead).
//   * VIP_RegisterFeature runs in OnPluginStart: both dependencies are loaded before this plugin,
//     which is the moment the original registered it (AllPluginsLoaded).
//   * The original's "if the core is loaded run now, else hook" check is kept (the original
//     lr_vip_multi only hooked; the check makes a plugin reload after the core finished work as
//     well).
//   * No config of its own; the feature values live in the vips' groups.ini.
//   * The original fed the event's raw "attacker" value (the engine user id) straight to the
//     slot-based VIP API. The port resolves the player field with the engine's own player accessor:
//     -1 (the world / no player at all) is dropped instead of becoming an index, and a user id can
//     no longer be mistaken for a slot. Deliberate divergence.
//   * Errors go through plg::Log (with the null check every pure-s2sdk plugin uses), never
//     pisex::ErrorLog.

#include <plg/plugin.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include <lr_api.hpp>
#include <vip_api.hpp>

#include <chrono>
#include <exception>
#include <string>
#include <string_view>

#include "vip_multi_core.hpp"

namespace {

// GetLogTag() of the original.
constexpr char kLogTag[] = "[LR-VIP-MULTI]";

// s2sdk player slots are 0..63 (CS2's MAXPLAYERS); the original fed the raw event value to the VIP
// core, the port bounds-checks the resolved slot.
constexpr int kMaxSlots = 64;

// The original's g_pLRCore / g_pVIPCore, moved to the consumer wrappers.
levels_ranks::LrApi* g_pLRCore = &levels_ranks::LrApi::Get();
vip::IVIPApi* g_pVIPCore = &vip::IVIPApi::Get();

// --- map gate (PITFALLS B1/B2) ---------------------------------------------------------------

bool g_mapReady = false;               // a map is up: the VIP core's client lookups are safe
bool g_mapStartSeen = false;           // an OnMapStart arrived since OnPluginStart / the last OnMapEnd
bool g_mapEndSeen = false;             // an OnMapEnd arrived since OnPluginStart
bool g_mapListenersRegistered = false; // OnMapStart/OnMapEnd are registered with s2sdk

// plg::Log is filled by the plugify module right before OnPluginStart; the null check keeps the
// plugin silent (not crashing) in an unusual host that never provides it.
void LogMessage(std::string_view message, plg::Severity severity) {
	if (plg::Log) {
		plg::Log(message, severity, plg::source_location::current());
	}
}

void LogInfo(std::string_view message) {
	LogMessage(message, plg::Severity::Info);
}

void LogError(std::string_view message) {
	LogMessage(message, plg::Severity::Error);
}

// s2sdk listener callbacks are plain function pointers called on the game thread; nothing here may
// throw, and OnMapEnd must not touch a client (PITFALLS B2).

void OnMapStartListener() {
	// The engine calls this once the new map and its client list exist: readiness is granted here
	// and by the next OnPluginUpdate (the PITFALLS B1 work is only legal from this point on).
	g_mapReady = true;
	g_mapStartSeen = true;
}

void OnMapEndListener() {
	// From here until the next OnMapStart s2sdk's client list is torn down and any client query
	// crashes - so this handler only drops state.
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = true;
}

void RegisterMapListeners() {
	if (g_mapListenersRegistered) {
		return;
	}
	if (__s2sdk_OnMapStart_Register == nullptr || __s2sdk_OnMapEnd_Register == nullptr) {
		LogError(std::string(kLogTag) + " the s2sdk map listeners are unavailable - the LrBoost multiplier stays off");
		return;
	}
	s2sdk::OnMapStart_Register(&OnMapStartListener);
	s2sdk::OnMapEnd_Register(&OnMapEndListener);
	g_mapListenersRegistered = true;
}

void UnregisterMapListeners() {
	if (!g_mapListenersRegistered) {
		return;
	}
	g_mapListenersRegistered = false;
	if (__s2sdk_OnMapStart_Unregister != nullptr) {
		s2sdk::OnMapStart_Unregister(&OnMapStartListener);
	}
	if (__s2sdk_OnMapEnd_Unregister != nullptr) {
		s2sdk::OnMapEnd_Unregister(&OnMapEndListener);
	}
}

// OnPlayerKilledPreHook() of the original, registered as the LR core's *PreEvent* variant so the
// callback receives the raw s2sdk event handle (the original's pisex::IGameEvent*).
void OnPlayerKilledPreHook(void* hEvent, int& iExpCaused, int /*iExpVictim*/, int /*iExpAttacker*/) {
	if (hEvent == nullptr || !g_mapReady) {
		// No event handle or no map: the experience stays as it is. Without a map there are no kill
		// events to scale anyway, and the guard keeps the VIP core from looking a slot up while the
		// client list is torn down.
		return;
	}
	// player_death.attacker is a "player_controller_and_pawn" field (core.gameevents): the slot comes
	// from IGameEvent::GetPlayerSlot, not from IGameEvent::GetInt. The two are different engine calls
	// and GetInt answers the raw event value (the engine user id, the number GetPlayerUserId(slot)
	// returns) - a number that is not a slot. Without the binding the field cannot be resolved at
	// all, so the hook fails soft instead of falling back to the raw int (PITFALLS A1: no crash, no
	// wrong slot).
	if (__s2sdk_GetEventPlayerSlot == nullptr) {
		return;
	}
	int iClient = -1;
	try {
		iClient = static_cast<int>(s2sdk::GetEventPlayerSlot(hEvent, "attacker"));
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " OnPlayerKilledPre: reading the \"attacker\" slot failed: " + ex.what());
		return;
	} catch (...) {
		LogError(std::string(kLogTag) + " OnPlayerKilledPre: reading the \"attacker\" slot failed with an unknown exception");
		return;
	}
	if (!(0 <= iClient && iClient < kMaxSlots)) {
		return; // -1 is INVALID_PLAYER_SLOT_INDEX: the world / no player behind the field
	}
	const float fValue = g_pVIPCore->VIP_GetClientFeatureFloat(iClient, lr_vip_multi::kFeature.data());
	if (g_pVIPCore->VIP_IsClientVIP(iClient)) {
		iExpCaused = lr_vip_multi::ApplyMultiplier(iExpCaused, fValue);
	}
}

// OnCoreIsReadyHook() of the original.
void OnCoreIsReadyHook() {
	g_pLRCore->HookOnPlayerKilledPreEvent(OnPlayerKilledPreHook);
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
		// Only the map listeners - no client call happens here (PITFALLS B1: none exists before the
		// first map).
		RegisterMapListeners();

		// AllPluginsLoaded() of the original (VIP part first - a Plugify dependency is loaded
		// before this plugin, so the VIP core is already available here).
		g_pVIPCore->VIP_RegisterFeature(lr_vip_multi::kFeature.data(), vip::VIP_BOOL, vip::TOGGLABLE);

		if (g_pLRCore->CoreIsLoaded()) {
			OnCoreIsReadyHook();
		} else {
			g_pLRCore->HookOnCoreIsReady(OnCoreIsReadyHook);
		}
		LogInfo(std::string(kLogTag) + " ready");
		return plg::PluginResult{};
	} catch (const std::exception& ex) {
		LogError(std::string(kLogTag) + " OnPluginStart failed: " + ex.what());
	} catch (...) {
		LogError(std::string(kLogTag) + " OnPluginStart failed with an unknown exception");
	}
	return plg::PluginResult{}; // never Failed: that stops every plugify plugin
}

plg::PluginResult LrVipMultiPlugin::OnPluginUpdate(std::chrono::milliseconds /*dt*/) {
	try {
		// PITFALLS B1/B2: the host pumps this from the server frame, so the first update proves a
		// live map for a plugin loaded into a running map (it never saw an OnMapStart). An OnMapEnd
		// seen since OnPluginStart means the server is between maps, so the latch waits for the next
		// OnMapStart. Without the map listeners nothing could ever close the gate again, so it is
		// not opened at all then (fail safe).
		if (!g_mapReady && g_mapListenersRegistered && (g_mapStartSeen || !g_mapEndSeen)) {
			g_mapReady = true;
		}
	} catch (...) {
		LogError(std::string(kLogTag) + " OnPluginUpdate failed");
	}
	return plg::PluginResult{};
}

plg::PluginResult LrVipMultiPlugin::OnPluginEnd() {
	try {
		// PITFALLS A7: nothing may point into this library after the unload.
		g_mapReady = false;
		// The Unhook* are no-ops when levels_ranks is not loaded (lr_api.hpp Unregister checks the
		// null binding slot); the Available() guard only keeps that intent visible here.
		if (g_pLRCore->Available()) {
			g_pLRCore->UnhookOnPlayerKilledPre();
			g_pLRCore->UnhookOnCoreIsReady();
		}
		UnregisterMapListeners();
		g_mapStartSeen = false;
		g_mapEndSeen = false;
	} catch (...) {
		// nothing left to do at unload
	}
	return plg::PluginResult{};
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_plugin)
