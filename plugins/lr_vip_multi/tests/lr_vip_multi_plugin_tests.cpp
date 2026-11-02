// lr_vip_multi_plugin_tests — the raw-game-event / map-gate / teardown regression test of the
// pure-s2sdk lr_vip_multi plugin (the lr_weapons_plugin_tests / lr_namereward_plugin_tests
// pattern).
//
// The plugin TU is compiled into this test (this file includes ../src/plugin.cpp), so the real kill
// hook runs end to end: OnPluginStart() registers the levels_ranks *PreEvent* callback through
// lr_api.hpp, the LR trampoline hands the raw s2sdk event handle over as int64, and the fakes below
// answer the event / map / vip exports of the fake event object.
//
// Why this test exists (the bug class it pins):
//   player_death.attacker is a "player_controller_and_pawn" field (core.gameevents). s2sdk's
//   GetEventInt calls IGameEvent::GetInt, GetEventPlayerSlot calls IGameEvent::GetPlayerSlot - two
//   different engine APIs. GetInt answers the raw event value (the engine user id, the number the
//   engine's GetPlayerUserId(slot) returns), which is NOT a slot. The fake event below therefore
//   answers a deliberately different value for GetEventInt, and every case asserts that GetEventInt
//   is never called at all.
//
// Covered:
//   1. the attacker slot comes from GetEventPlayerSlot: valid slot 3 whose 3.5x LrBoost is applied
//      while the decoy int 7 names a different, non-VIP client; the reverse (slot 3 is not a VIP,
//      the decoy int 7 is) is not multiplied;
//   2. valid slot 0 - "0 is not the world" - with the decoy int 12 applies slot 0's own value;
//   3. -1 / world and the out-of-range 64: no multiplier and no VIP query, although the unused int
//      value names a fully usable VIP;
//   4. a missing player-slot binding fails soft (no multiplier, no crash, no GetEventInt fallback);
//   5. the guard order of PITFALLS B1/B2/A1: null event, no map yet, the OnMapEnd window (and an
//      OnPluginUpdate inside it), then the next OnMapStart re-opens the gate; a plugin restarted
//      mid-map opens the gate on its first OnPluginUpdate;
//   6. the feature registration: OnPluginStart registers "LrBoost" as VIP_BOOL / TOGGLABLE;
//   7. PITFALLS A7: OnPluginEnd unhooks the LR kill callback and the map listeners, closes the gate.
//
// No engine, no pisexcompat: every binding below is a recording fake. Run by ../test.sh
// (build/tests/lr_vip_multi_plugin_tests).

#include "../src/plugin.cpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, std::string_view what) {
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %.*s\n", static_cast<int>(what.size()), what.data());
	}
}

void CheckEqInt(long long actual, long long expected, std::string_view what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %.*s (got %lld, expected %lld)\n", static_cast<int>(what.size()),
					what.data(), actual, expected);
	}
}

std::string ToStd(const plg::string& text) {
	return std::string(std::string_view(text));
}

// --- the scenario ---------------------------------------------------------------------------------

// CS2's MAXPLAYERS: the plugin's own kMaxSlots (0..63) is asserted through the range cases below.
constexpr int32_t kSlots = 64;

// The fake raw event. |player_slot| is what GetEventPlayerSlot answers (the truth), |raw_int| is
// what GetEventInt would answer - the plugin must never ask for it.
struct FakeEvent {
	int32_t player_slot = -1;
	int32_t raw_int = 0;
};

FakeEvent g_event;

bool g_is_vip[kSlots];
float g_feature_value[kSlots];

int g_player_slot_calls = 0;
int g_event_int_calls = 0;
int g_event_int_total = 0; // survives ResetScenario(): "never called in the whole run"
int g_vip_queries = 0;
int32_t g_last_vip_slot = -1;
int g_feature_calls = 0;
int32_t g_last_feature_slot = -1;
std::string g_last_feature_name;
std::string g_last_player_slot_key;
std::string g_last_event_int_key;
int g_register_calls = 0;
std::string g_registered_feature;
int32_t g_registered_value_type = -1;
int32_t g_registered_feature_type = -1;

bool InRange(int32_t slot) {
	return slot >= 0 && slot < kSlots;
}

// Only |slot| is a VIP with the given LrBoost value; every other slot is a plain client with the
// neutral 1.0 (what VIP_GetClientFeatureFloat answers for a missing feature).
void SetOnlyVip(int32_t slot, float value) {
	for (bool& vip : g_is_vip) {
		vip = false;
	}
	for (float& feature : g_feature_value) {
		feature = 1.0f;
	}
	if (InRange(slot)) {
		g_is_vip[slot] = true;
		g_feature_value[slot] = value;
	}
}

// --- s2sdk fakes ----------------------------------------------------------------------------------

void FakeLog(std::string_view message, plg::Severity /*severity*/,
			 const plg::source_location& /*location*/) {
	std::printf("  log: %.*s\n", static_cast<int>(message.size()), message.data());
}

int32_t FakeGetEventPlayerSlot(void* event, const plg::string& key) {
	++g_player_slot_calls;
	g_last_player_slot_key = ToStd(key);
	if (event != &g_event) {
		return -1;
	}
	return ToStd(key) == "attacker" ? g_event.player_slot : -1;
}

// GetEventInt is the wrong API for a player field: the test records every call and fails when one
// happens. It still answers |raw_int|, so a regression would produce a wrong slot instead of a crash.
int32_t FakeGetEventInt(void* event, const plg::string& key) {
	++g_event_int_calls;
	++g_event_int_total;
	g_last_event_int_key = ToStd(key);
	if (event != &g_event) {
		return -1;
	}
	return ToStd(key) == "attacker" ? g_event.raw_int : -1;
}

s2sdk::OnMapStartCallback g_on_map_start = nullptr;
s2sdk::OnMapEndCallback g_on_map_end = nullptr;

void FakeOnMapStartRegister(s2sdk::OnMapStartCallback callback) {
	g_on_map_start = callback;
}

void FakeOnMapStartUnregister(s2sdk::OnMapStartCallback callback) {
	if (g_on_map_start == callback) {
		g_on_map_start = nullptr;
	}
}

void FakeOnMapEndRegister(s2sdk::OnMapEndCallback callback) {
	g_on_map_end = callback;
}

void FakeOnMapEndUnregister(s2sdk::OnMapEndCallback callback) {
	if (g_on_map_end == callback) {
		g_on_map_end = nullptr;
	}
}

// --- levels_ranks fakes ---------------------------------------------------------------------------

levels_ranks::PlayerKilledPreCallback g_killed_pre = nullptr;

bool FakeIsCoreLoaded() {
	return true;
}

void FakeOnCoreIsReadyRegister(levels_ranks::CoreReadyCallback callback) {
	(void)callback;
}

void FakeOnCoreIsReadyUnregister(levels_ranks::CoreReadyCallback callback) {
	(void)callback;
}

void FakeOnPlayerKilledPreRegister(levels_ranks::PlayerKilledPreCallback callback) {
	g_killed_pre = callback;
}

void FakeOnPlayerKilledPreUnregister(levels_ranks::PlayerKilledPreCallback callback) {
	if (g_killed_pre == callback) {
		g_killed_pre = nullptr;
	}
}

// --- vip fakes -------------------------------------------------------------------------------------

bool FakeVIP_IsVIPLoaded() {
	return true;
}

void FakeVIP_RegisterFeature(const plg::string& feature, ::vip::ValueType value_type,
							 ::vip::FeatureType feature_type,
							 ::vip::ItemSelectableDelegate /*select_callback*/,
							 ::vip::ItemTogglableDelegate /*toggle_callback*/,
							 ::vip::ItemDisplayDelegate /*display_callback*/) {
	++g_register_calls;
	g_registered_feature = ToStd(feature);
	g_registered_value_type = static_cast<int32_t>(value_type);
	g_registered_feature_type = static_cast<int32_t>(feature_type);
}

bool FakeVIP_IsClientVIP(int32_t slot) {
	++g_vip_queries;
	g_last_vip_slot = slot;
	return InRange(slot) && g_is_vip[slot];
}

float FakeVIP_GetClientFeatureFloat(int32_t slot, const plg::string& feature) {
	++g_feature_calls;
	g_last_feature_slot = slot;
	g_last_feature_name = ToStd(feature);
	return InRange(slot) ? g_feature_value[slot] : 1.0f;
}

// --- fixtures --------------------------------------------------------------------------------------

void InstallFakes() {
	plg::Log = &FakeLog;

	__s2sdk_OnMapStart_Register = &FakeOnMapStartRegister;
	__s2sdk_OnMapStart_Unregister = &FakeOnMapStartUnregister;
	__s2sdk_OnMapEnd_Register = &FakeOnMapEndRegister;
	__s2sdk_OnMapEnd_Unregister = &FakeOnMapEndUnregister;

	__s2sdk_GetEventPlayerSlot = &FakeGetEventPlayerSlot;
	__s2sdk_GetEventInt = &FakeGetEventInt;

	__levels_ranks_IsCoreLoaded = &FakeIsCoreLoaded;
	__levels_ranks_OnCoreIsReady_Register = &FakeOnCoreIsReadyRegister;
	__levels_ranks_OnCoreIsReady_Unregister = &FakeOnCoreIsReadyUnregister;
	__levels_ranks_OnPlayerKilledPre_Register = &FakeOnPlayerKilledPreRegister;
	__levels_ranks_OnPlayerKilledPre_Unregister = &FakeOnPlayerKilledPreUnregister;

	__vip_VIP_IsVIPLoaded = &FakeVIP_IsVIPLoaded;
	__vip_VIP_RegisterFeature = &FakeVIP_RegisterFeature;
	__vip_VIP_IsClientVIP = &FakeVIP_IsClientVIP;
	__vip_VIP_GetClientFeatureFloat = &FakeVIP_GetClientFeatureFloat;
}

void ResetScenario() {
	g_event = FakeEvent{};
	for (bool& vip : g_is_vip) {
		vip = false;
	}
	for (float& feature : g_feature_value) {
		feature = 1.0f;
	}
	g_player_slot_calls = 0;
	g_event_int_calls = 0;
	g_vip_queries = 0;
	g_last_vip_slot = -1;
	g_feature_calls = 0;
	g_last_feature_slot = -1;
	g_last_feature_name.clear();
	g_last_player_slot_key.clear();
	g_last_event_int_key.clear();
	g_register_calls = 0;
	g_registered_feature.clear();
	g_registered_value_type = -1;
	g_registered_feature_type = -1;
}

void OpenMap() {
	if (g_on_map_start != nullptr) {
		g_on_map_start();
		return;
	}
	Check(false, "the OnMapStart listener is registered");
}

// The kill the LR core reports: the raw s2sdk event handle travels as int64 through lr_api's
// trampoline (levels_ranks does `static_cast<int64_t>(reinterpret_cast<intptr_t>(event))`).
// Returns the (possibly rewritten) experience the module caused.
int32_t FireKill(int32_t exp, bool null_event = false) {
	if (g_killed_pre == nullptr) {
		Check(false, "the LR kill callback is not registered");
		return exp;
	}
	int32_t caused = exp;
	const int64_t event_id =
		null_event ? 0 : static_cast<int64_t>(reinterpret_cast<intptr_t>(&g_event));
	g_killed_pre(event_id, caused, /*exp_victim=*/50, /*exp_attacker=*/100);
	return caused;
}

void StartPlugin() {
	InstallFakes();
	ResetScenario();
	g_plugin.OnPluginStart();
	Check(g_on_map_start != nullptr, "the OnMapStart listener is registered");
	Check(g_on_map_end != nullptr, "the OnMapEnd listener is registered");
	Check(g_killed_pre != nullptr, "the LR OnPlayerKilledPre callback is registered");
}

// --- cases -----------------------------------------------------------------------------------------

void TestNoMapYet() {
	std::printf("-- no map yet\n");
	SetOnlyVip(3, 3.5f);
	g_event = FakeEvent{3, 7};
	CheckEqInt(FireKill(100), 100, "no multiplier is applied before the first map (B1)");
	CheckEqInt(g_player_slot_calls, 0, "no event field is read before the first map");
	CheckEqInt(g_feature_calls, 0, "the VIP core is not queried before the first map");
	CheckEqInt(g_vip_queries, 0, "the VIP core is not queried before the first map");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");
}

void TestPlayerSlotSource() {
	std::printf("-- the attacker slot comes from GetEventPlayerSlot\n");
	// Valid slot 3 with the decoy int 7: slot 3 is the only VIP, so a GetEventInt port would not
	// multiply at all (slot 7 is a plain client).
	SetOnlyVip(3, 3.5f);
	g_event = FakeEvent{3, 7};
	int player_slot_calls = g_player_slot_calls;
	CheckEqInt(FireKill(100), 350, "slot 3: the LrBoost 3.5 of slot 3 is applied");
	CheckEqInt(g_player_slot_calls, player_slot_calls + 1, "the attacker field is read once");
	Check(g_last_player_slot_key == "attacker", "the player slot is read from the \"attacker\" key");
	CheckEqInt(g_last_vip_slot, 3, "the VIP query uses the resolved slot, not the decoy int");
	CheckEqInt(g_last_feature_slot, 3, "the feature value is read for the resolved slot");
	Check(g_last_feature_name == std::string(lr_vip_multi::kFeature),
		  "the feature value is read from \"LrBoost\"");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");

	// Valid slot 0 - "0 is not the world" - with the decoy int 12 (a non-VIP client).
	SetOnlyVip(0, 2.0f);
	g_event = FakeEvent{0, 12};
	CheckEqInt(FireKill(7), 14, "slot 0 is a real player: 7 * 2.0 = 14");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");

	// The reverse decoy: the resolved slot 3 is not a VIP, while the decoy int 7 is a 5x VIP. Only
	// the player-slot answer may decide, so the kill stays untouched.
	SetOnlyVip(7, 5.0f);
	g_event = FakeEvent{3, 7};
	CheckEqInt(FireKill(100), 100, "the non-VIP slot 3 is not boosted by the VIP decoy int 7");
	CheckEqInt(g_last_vip_slot, 3, "the VIP query still uses the resolved slot 3");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");
}

void TestWorldAttacker() {
	std::printf("-- the world is not a player\n");
	// -1 is INVALID_PLAYER_SLOT_INDEX: no player at all, although the unused int value names a fully
	// usable VIP (a GetEventInt-based port would multiply slot 2 here).
	SetOnlyVip(2, 4.0f);
	g_event = FakeEvent{-1, 2};
	const int player_slot_calls = g_player_slot_calls;
	const int vip_queries = g_vip_queries;
	const int feature_calls = g_feature_calls;
	CheckEqInt(FireKill(100), 100, "a world attacker causes no multiplier");
	CheckEqInt(g_player_slot_calls, player_slot_calls + 1, "the attacker field is read once");
	CheckEqInt(g_vip_queries, vip_queries, "the VIP core is not queried for a non-player attacker");
	CheckEqInt(g_feature_calls, feature_calls, "no feature value is read for a non-player attacker");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");

	// The slot range guard still holds for a malformed answer.
	g_event = FakeEvent{kSlots, kSlots};
	CheckEqInt(FireKill(100), 100, "a slot above the range causes no multiplier");
	CheckEqInt(g_vip_queries, vip_queries, "the VIP core is not queried for an out-of-range slot");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");
}

void TestMissingPlayerSlotBinding() {
	std::printf("-- no player-slot binding\n");
	SetOnlyVip(3, 3.5f);
	g_event = FakeEvent{3, 7};
	__s2sdk_GetEventPlayerSlot = nullptr;
	const int player_slot_calls = g_player_slot_calls;
	const int vip_queries = g_vip_queries;
	CheckEqInt(FireKill(100), 100, "without the binding the kill is dropped (fail soft)");
	CheckEqInt(g_player_slot_calls, player_slot_calls, "the missing binding is not called");
	CheckEqInt(g_vip_queries, vip_queries, "the VIP core is not queried without a slot");
	CheckEqInt(g_event_int_calls, 0, "there is no GetEventInt fallback");
	__s2sdk_GetEventPlayerSlot = &FakeGetEventPlayerSlot;
	CheckEqInt(FireKill(100), 350, "the binding restores the multiplier");
}

void TestEventGuards() {
	std::printf("-- event guards\n");
	SetOnlyVip(6, 2.0f);
	g_event = FakeEvent{6, 6};
	CheckEqInt(FireKill(100), 200, "the baseline kill is multiplied");
	CheckEqInt(FireKill(100, /*null_event=*/true), 100, "a null event handle is a no-op");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");
}

void TestFeatureRegistration() {
	std::printf("-- the VIP feature registration\n");
	CheckEqInt(g_register_calls, 1, "OnPluginStart registers exactly one VIP feature");
	Check(g_registered_feature == std::string(lr_vip_multi::kFeature),
		  "the registered feature is \"LrBoost\"");
	CheckEqInt(g_registered_value_type, static_cast<int32_t>(::vip::ValueType::VIP_BOOL),
			   "the feature is registered as VIP_BOOL");
	CheckEqInt(g_registered_feature_type, static_cast<int32_t>(::vip::FeatureType::TOGGLABLE),
			   "the feature is registered as TOGGLABLE");
}

void TestMapEndWindow() {
	std::printf("-- the OnMapEnd window (B2)\n");
	SetOnlyVip(3, 3.5f);
	g_event = FakeEvent{3, 7};
	Check(g_on_map_end != nullptr, "the OnMapEnd listener is available");
	if (g_on_map_end != nullptr) {
		g_on_map_end();
	}
	const int player_slot_calls = g_player_slot_calls;
	const int vip_queries = g_vip_queries;
	CheckEqInt(FireKill(100), 100, "no kill is multiplied in the OnMapEnd window");
	CheckEqInt(g_player_slot_calls, player_slot_calls, "no event field is read in the OnMapEnd window");
	CheckEqInt(g_vip_queries, vip_queries, "the VIP core is not queried in the OnMapEnd window");
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	CheckEqInt(FireKill(100), 100, "OnPluginUpdate does not re-open the gate after OnMapEnd");
	CheckEqInt(g_player_slot_calls, player_slot_calls, "still no event field is read");
	OpenMap();
	CheckEqInt(FireKill(100), 350, "the next OnMapStart re-opens the gate");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");
}

void TestReloadMidMap() {
	std::printf("-- a plugin loaded into a running map (B1 latch)\n");
	g_plugin.OnPluginEnd();
	ResetScenario();
	g_plugin.OnPluginStart();
	Check(g_killed_pre != nullptr, "the kill hook is registered again");
	Check(!g_mapReady, "a plugin loaded into a running map starts with the gate closed");
	SetOnlyVip(3, 3.5f);
	g_event = FakeEvent{3, 7};
	CheckEqInt(FireKill(100), 100, "no multiplier before the first frame after a reload");
	CheckEqInt(g_player_slot_calls, 0, "no event field is read before the first frame");
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(0));
	CheckEqInt(FireKill(100), 350,
			   "the first OnPluginUpdate opens the gate for a plugin loaded mid-map");
	CheckEqInt(g_event_int_calls, 0, "GetEventInt is never called");
}

void TestUnload() {
	std::printf("-- unload (A7)\n");
	g_plugin.OnPluginEnd();
	Check(g_killed_pre == nullptr, "the LR kill callback is unhooked");
	Check(g_on_map_start == nullptr, "the OnMapStart listener is removed");
	Check(g_on_map_end == nullptr, "the OnMapEnd listener is removed");
	Check(!g_mapReady, "the map gate is closed on unload");
	Check(!g_mapListenersRegistered, "the map listeners are marked unregistered");
	CheckEqInt(g_event_int_total, 0, "GetEventInt was never called in the whole run");
}

} // namespace

int main() {
	std::printf("lr_vip_multi_plugin_tests\n");
	StartPlugin();
	TestNoMapYet();
	// The OnMapStart callback opens the gate for the rest of the run (PITFALLS B1).
	OpenMap();
	TestPlayerSlotSource();
	TestWorldAttacker();
	TestMissingPlayerSlotBinding();
	TestEventGuards();
	TestFeatureRegistration();
	TestMapEndWindow();
	TestReloadMidMap();
	TestUnload();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
