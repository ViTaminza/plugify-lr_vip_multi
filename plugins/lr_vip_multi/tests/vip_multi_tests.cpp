// Tests for plugins/lr_vip_multi/src/vip_multi_core.hpp (engine-free core of the ported
// "[LR][VIP] Multi" module).
//
//   * the multiplier body: int(exp * value) with the C-style cast of the original,
//   * the shipped groups.ini: every group that carries "LrBoost" (the feature this module
//     registers) must hold a parseable decimal > 1 - the value the module multiplies experience
//     by. This pins the assumption the module makes about the VIP config.

#include <plg/plugin.hpp>

#include <pisex/keyvalues.hpp>
#include <pisex/paths.hpp>

#include <cstdio>
#include <string>
#include <string_view>

#include "vip_multi_core.hpp"

// A real plugin gets these from PLUGIFY_PLUGIN; the test leaves them null. The path resolver is
// driven through pisex::SetPluginName/SetConfigDir instead.
namespace plg {
GetBaseDirFn GetBaseDir{nullptr};
GetExtensionsDirFn GetExtensionsDir{nullptr};
GetConfigsDirFn GetConfigsDir{nullptr};
GetDataDirFn GetDataDir{nullptr};
GetLogsDirFn GetLogsDir{nullptr};
GetCacheDirFn GetCacheDir{nullptr};
IsLoadedFn IsLoaded{nullptr};
LogFn Log{nullptr};
BeginZoneFn BeginZone{nullptr};
EndZoneFn EndZone{nullptr};
} // namespace plg

namespace {

int g_checks = 0;
int g_failures = 0;
std::string_view g_section = "?";

void Section(std::string_view name) {
	g_section = name;
	std::printf("-- %.*s\n", static_cast<int>(name.size()), name.data());
}

#define CHECK(expr)                                                                            \
	do {                                                                                       \
		++g_checks;                                                                            \
		if (!(expr)) {                                                                         \
			++g_failures;                                                                      \
			std::printf("FAIL [%.*s] %s:%d: %s\n", static_cast<int>(g_section.size()),         \
						g_section.data(), __FILE__, __LINE__, #expr);                          \
		}                                                                                      \
	} while (false)

#define CHECK_EQ_INT(actual, expected)                                                         \
	do {                                                                                       \
		++g_checks;                                                                            \
		const long long a_ = static_cast<long long>(actual);                                   \
		const long long e_ = static_cast<long long>(expected);                                 \
		if (a_ != e_) {                                                                        \
			++g_failures;                                                                      \
			std::printf("FAIL [%.*s] %s:%d: got %lld, expected %lld\n",                        \
						static_cast<int>(g_section.size()), g_section.data(), __FILE__,       \
						__LINE__, a_, e_);                                                     \
		}                                                                                      \
	} while (false)

// --- the multiplier -----------------------------------------------------------------------------

void TestMultiplier() {
	Section("ApplyMultiplier");
	// The shipped groups.ini values: 2 / 2.5 / 3 / 3.5 (see TestProductionGroups).
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(10, 2.0f), 20);
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(10, 2.5f), 25);
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(7, 3.5f), 24); // 24.5 -> 24 (truncation, not round)
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(9, 3.0f), 27);
	// The original's C-style cast for the degenerate cases: a 0 value zeroes the reward, a
	// missing feature (the wrapper returns 1.0) leaves it alone.
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(10, 0.0f), 0);
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(10, 1.0f), 10);
	// Negative experience (the pre-hook can see one) keeps the sign.
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(-10, 2.5f), -25);
}

// --- the shipped groups.ini ----------------------------------------------------------------------

void TestProductionGroups() {
	Section("production groups.ini");
	pisex::SetPluginName("vip");
	pisex::SetConfigDir(std::string(LR_VIP_MULTI_TEST_DATA_DIR) + "/vip");

	pisex::KeyValues kv("Groups");
	CHECK(kv.LoadFromFile("addons/configs/vip/groups.ini"));

	const std::string feature(lr_vip_multi::kFeature);
	int groups_with_feature = 0;
	int groups = 0;
	for (const pisex::KeyValues* group = kv.GetFirstTrueSubKey(); group != nullptr;
		 group = group->GetNextTrueSubKey()) {
		++groups;
		const pisex::KeyValues* entry = group->FindKey(feature.c_str());
		if (entry == nullptr) {
			continue;
		}
		++groups_with_feature;
		const std::string value(entry->GetString());
		const float parsed = entry->GetFloat();
		// The module reads the feature with VIP_GetClientFeatureFloat (atof of the group value);
		// every configured value must be a decimal > 1, otherwise "Multi" would not multiply.
		CHECK(parsed > 1.0f);
		std::printf("   group '%s': LrBoost = %s (%.2f)\n", group->GetName(), value.c_str(),
					static_cast<double>(parsed));
	}
	CHECK(groups > 0);
	CHECK(groups_with_feature > 0);
	std::printf("   %d groups, %d with LrBoost\n", groups, groups_with_feature);
}

} // namespace

int main() {
	std::printf("lr_vip_multi_tests\n");
	TestMultiplier();
	TestProductionGroups();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
