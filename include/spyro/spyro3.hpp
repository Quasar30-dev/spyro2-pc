#pragma once
#include "spyro/objects.hpp"

namespace spyro {
// Native provisional reward flight uses the existing gem controller and the
// mounted YOTD denomination/mesh tables; source IDs remain stable for saves.
RetailObject make_yotd_reward_gem(const RetailObject& source,const ObjectModels& models,
    Vec3 origin,bool charging,retail::Random& random);
} // namespace spyro
