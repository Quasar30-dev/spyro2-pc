#pragma once
#include "spyro/math.hpp"
#include <cstdint>

namespace spyro {
// Legend-of-Spyro-style elements (PC extension). Values are serialized by saves
// and online snapshots; keep them stable. Earth is reserved for later.
enum class Element : std::uint8_t { Fire=0,Water=1,Ice=2,Electric=3,Convexity=4 };
inline constexpr unsigned element_count=5;
// Per-object status requests carried by an ElementHit.
namespace element_status {
inline constexpr unsigned soak=1,freeze=2,stun=4,void_mark=8,push=16,pull=32,lift=64,burn=128;
}
// What dealt a hit. Bosses read it for their damage table (Crownworks: breath
// by element, shot 3, headbash wave 4, Fury 4 per hit within its cap).
enum class HitSource : std::uint8_t { Breath,Shot,Wave,Fury };
// PC mailbox beside the retail attack mask. The source drives knockback, so a
// demon or shockwave throws enemies away from itself rather than from Spyro.
struct ElementHit {
    Element element=Element::Fire;
    unsigned strength{};  // 1 breath/minor, 2 shot/wave core, 3 fury finisher
    Vec3 source;
    unsigned status{};
    bool demon=false;     // dealt by a summoned ally (never hurts Spyro/NPCs)
    float push=0;         // knockback/pull impulse in world units per second
    HitSource from=HitSource::Breath;
};
} // namespace spyro
