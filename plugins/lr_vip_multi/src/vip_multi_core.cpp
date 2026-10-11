#include "vip_multi_core.hpp"

namespace lr_vip_multi {

int ApplyMultiplier(int exp_caused, float value) {
	return static_cast<int>(static_cast<float>(exp_caused) * value);
}

} // namespace lr_vip_multi
