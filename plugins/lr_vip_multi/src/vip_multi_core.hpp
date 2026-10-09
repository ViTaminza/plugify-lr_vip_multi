#pragma once
// Engine-free core of the lr_vip_multi port: the body of the original's OnPlayerKilledPreHook
// (ref/src/cs2-lvl_ranks_modules/LR_VIP_Multi/lr_vip_multi.cpp, 110 lines).
//
// The original registered the VIP feature "LrBoost" as VIP_BOOL/TOGGLABLE and, for every kill,
// multiplied the experience by the value of that feature for the attacker's group:
//
//     float fValue = g_pVIPCore->VIP_GetClientFeatureFloat(iClient, "LrBoost");
//     if (g_pVIPCore->VIP_IsClientVIP(iClient))
//         iExpCaused = int(iExpCaused * fValue);
//
// The module has no config of its own (the per-group values live in the VIP core's groups.ini).

#include <string_view>

namespace lr_vip_multi {

// The feature name of VIP_RegisterFeature("LrBoost", VIP_BOOL, TOGGLABLE).
inline constexpr std::string_view kFeature = "LrBoost";

// int(iExpCaused * fValue) of the original: a C-style cast, truncating towards zero.
int ApplyMultiplier(int exp_caused, float value);

} // namespace lr_vip_multi
