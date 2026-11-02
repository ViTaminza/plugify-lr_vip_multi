// Tests for plugins/lr_vip_multi/src/vip_multi_core.hpp (the engine-free core of the ported
// "[LR][VIP] Multi" module) and for the VIP groups.ini it relies on.
//
// Pure s2sdk / no pisexcompat (docs/PLUGIN_STANDARD.md §7): this executable links only
// lr_vip_multi_core - no plugify_sdk, no engine bindings, no KeyValues of the compat library. The
// shipped groups.ini is read with the small quoted-KeyValues reader below, i.e. the subset of the
// format the file uses ("key" "value" pairs and "block" { ... }).
//
//   * the multiplier body: int(exp * value) with the C-style cast of the original (truncation
//     towards zero for both signs, 1.0 leaves the experience alone, 0 zeroes it),
//   * the shipped groups.ini: every group that carries "LrBoost" (the feature this module
//     registers) must hold a parseable decimal > 1 - the value the module multiplies experience
//     by - and the production groups keep their exact values, so a silent fixture change (or a
//     rename of the feature) fails here.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vip_multi_core.hpp"

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
	// Negative experience (the pre-hook can see one from another module) keeps the sign and is
	// still truncated towards zero: -24.5 -> -24 (not -25, which rounding down would give).
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(-10, 2.5f), -25);
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(-7, 3.5f), -24);
	// Larger values stay in the int range the hook carries.
	CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(1000000, 2.5f), 2500000);
}

// --- a tiny reader for the quoted KeyValues subset of groups.ini ---------------------------------

struct KvNode {
	std::string name;
	std::string value;
	std::vector<KvNode> children;
	bool block = false; // true: "name" { ... }, false: "name" "value"
};

class KvReader {
public:
	explicit KvReader(std::string_view text) : m_text(text) {}

	// Parses the whole file into its top-level nodes ("VIP" { ... } in groups.ini).
	bool ReadAll(std::vector<KvNode>& out) { return ReadBlock(out, /*topLevel=*/true); }

private:
	enum class TokenKind { End, String, OpenBrace, CloseBrace };

	struct Token {
		TokenKind kind = TokenKind::End;
		std::string text;
	};

	void SkipSpaceAndComments() {
		while (m_pos < m_text.size()) {
			const unsigned char c = static_cast<unsigned char>(m_text[m_pos]);
			if (std::isspace(c) != 0) {
				++m_pos;
				continue;
			}
			// "// ..." to the end of the line (the shipped file has none, but configs may).
			if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
				m_pos += 2;
				while (m_pos < m_text.size() && m_text[m_pos] != '\n') {
					++m_pos;
				}
				continue;
			}
			break;
		}
	}

	Token Next() {
		SkipSpaceAndComments();
		if (m_pos >= m_text.size()) {
			return {};
		}
		const char c = m_text[m_pos];
		if (c == '{') {
			++m_pos;
			return {TokenKind::OpenBrace, "{"};
		}
		if (c == '}') {
			++m_pos;
			return {TokenKind::CloseBrace, "}"};
		}
		if (c != '"') {
			// A bare (unquoted) token, as KeyValues allows for simple names/values.
			const std::size_t start = m_pos;
			while (m_pos < m_text.size()) {
				const unsigned char t = static_cast<unsigned char>(m_text[m_pos]);
				if (std::isspace(t) != 0 || m_text[m_pos] == '{' || m_text[m_pos] == '}') {
					break;
				}
				++m_pos;
			}
			return {TokenKind::String, std::string(m_text.substr(start, m_pos - start))};
		}
		++m_pos; // opening quote
		std::string text;
		while (m_pos < m_text.size()) {
			const char ch = m_text[m_pos++];
			if (ch == '\\' && m_pos < m_text.size()) {
				const char escaped = m_text[m_pos++];
				switch (escaped) {
				case 'n': text += '\n'; break;
				case 't': text += '\t'; break;
				default: text += escaped; break;
				}
				continue;
			}
			if (ch == '"') {
				return {TokenKind::String, std::move(text)};
			}
			text += ch;
		}
		return {}; // unterminated string
	}

	bool ReadBlock(std::vector<KvNode>& out, bool topLevel) {
		while (true) {
			const Token key = Next();
			if (key.kind == TokenKind::End) {
				return topLevel; // a nested block must close before the file ends
			}
			if (key.kind == TokenKind::CloseBrace) {
				return !topLevel;
			}
			if (key.kind != TokenKind::String) {
				return false;
			}
			const Token next = Next();
			if (next.kind == TokenKind::OpenBrace) {
				KvNode node;
				node.name = key.text;
				node.block = true;
				if (!ReadBlock(node.children, /*topLevel=*/false)) {
					return false;
				}
				out.push_back(std::move(node));
			} else if (next.kind == TokenKind::String) {
				KvNode node;
				node.name = key.text;
				node.value = next.text;
				out.push_back(std::move(node));
			} else {
				return false;
			}
		}
	}

	std::string_view m_text;
	std::size_t m_pos = 0;
};

const KvNode* FindChild(const std::vector<KvNode>& nodes, std::string_view name) {
	for (const KvNode& node : nodes) {
		if (node.name == name) {
			return &node;
		}
	}
	return nullptr;
}

bool LoadFile(const char* path, std::vector<KvNode>& root) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	KvReader reader(text);
	return reader.ReadAll(root);
}

// --- the shipped groups.ini ----------------------------------------------------------------------

struct GroupBoost {
	std::string group;
	std::string raw;  // the value as written in groups.ini
	float value = 0;  // atof() of the value: what VIP_GetClientFeatureFloat returns
};

struct ExpectedBoost {
	const char* group;
	const char* raw;
	float value;
	int exp_100; // ApplyMultiplier(100, value): pins the multiplication of the shipped value
};

const GroupBoost* FindBoost(const std::vector<GroupBoost>& boosts, std::string_view group) {
	for (const GroupBoost& boost : boosts) {
		if (boost.group == group) {
			return &boost;
		}
	}
	return nullptr;
}

void TestProductionGroups() {
	Section("production groups.ini");
	const std::string path = std::string(LR_VIP_MULTI_TEST_DATA_DIR) + "/vip/groups.ini";
	std::vector<KvNode> root;
	const bool loaded = LoadFile(path.c_str(), root);
	CHECK(loaded);
	if (!loaded) {
		return;
	}

	const KvNode* vip = FindChild(root, "VIP");
	CHECK(vip != nullptr);
	if (vip == nullptr) {
		return;
	}

	int groups = 0;
	std::vector<GroupBoost> boosts;
	for (const KvNode& group : vip->children) {
		if (!group.block) {
			continue; // "server_id" "1" is a value of the VIP block, not a group
		}
		++groups;
		const KvNode* entry = FindChild(group.children, lr_vip_multi::kFeature);
		if (entry == nullptr) {
			continue;
		}
		// The module reads the feature with VIP_GetClientFeatureFloat (atof of the group value);
		// every configured value must be a decimal > 1, otherwise "Multi" would not multiply.
		boosts.push_back({group.name, entry->value, std::strtof(entry->value.c_str(), nullptr)});
	}
	CHECK(groups > 0);
	CHECK(!boosts.empty());

	for (const GroupBoost& boost : boosts) {
		CHECK(boost.value > 1.0f);
		std::printf("   group '%s': %s = %s (%.2f)\n", boost.group.c_str(),
					std::string(lr_vip_multi::kFeature).c_str(), boost.raw.c_str(),
					static_cast<double>(boost.value));
	}

	// The production values of the VIP package: the multiplier is only correct when these are
	// the numbers (and when the feature name still matches the file).
	const ExpectedBoost expected[] = {
		{"SKILL", "2", 2.0f, 200},
		{"XPBOOST", "3.5", 3.5f, 350},
		{"LITE", "2", 2.0f, 200},
		{"PREMIUM", "2.5", 2.5f, 250},
		{"PROVIP", "3", 3.0f, 300},
		{"LEGEND", "3", 3.0f, 300},
	};
	CHECK_EQ_INT(boosts.size(), sizeof(expected) / sizeof(expected[0]));
	for (const ExpectedBoost& want : expected) {
		const GroupBoost* boost = FindBoost(boosts, want.group);
		++g_checks;
		if (boost == nullptr) {
			++g_failures;
			std::printf("FAIL [%.*s] missing group '%s'\n", static_cast<int>(g_section.size()),
						g_section.data(), want.group);
			continue;
		}
		if (boost->raw != want.raw || boost->value != want.value) {
			++g_failures;
			std::printf("FAIL [%.*s] group '%s': got '%s' (%.2f), expected '%s' (%.2f)\n",
						static_cast<int>(g_section.size()), g_section.data(), want.group,
						boost->raw.c_str(), static_cast<double>(boost->value), want.raw,
						static_cast<double>(want.value));
			continue;
		}
		CHECK_EQ_INT(lr_vip_multi::ApplyMultiplier(100, boost->value), want.exp_100);
	}

	std::printf("   %d groups, %d with %s\n", groups, static_cast<int>(boosts.size()),
				std::string(lr_vip_multi::kFeature).c_str());
}

} // namespace

int main() {
	std::printf("lr_vip_multi_tests\n");
	TestMultiplier();
	TestProductionGroups();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
