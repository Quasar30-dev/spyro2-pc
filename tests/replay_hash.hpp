#pragma once
// Deterministic retail replay fingerprint. The golden values in
// element_tests.cpp were recorded from the build immediately BEFORE the
// elemental powers were added (9 October 2026). Elements/Dark Spyro disabled
// must reproduce them exactly: the new systems may not perturb retail play.
#include "spyro/runtime.hpp"
#include <bit>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace spyro::replay {
struct Hash {
    std::uint64_t value=1469598103934665603ull;
    void bytes(const void* data,std::size_t n){const auto* p=static_cast<const unsigned char*>(data);for(std::size_t i=0;i<n;++i){value^=p[i];value*=1099511628211ull;}}
    void f(float v){bytes(&v,sizeof v);}
    void u(std::uint64_t v){bytes(&v,sizeof v);}
    void v3(Vec3 v){f(v.x);f(v.y);f(v.z);}
};
inline InputState scripted(unsigned tick) {
    InputState in;const auto t=tick%600;
    in.move_z=t>=20 && t<420?1.0f:0.0f;in.move_x=t>=150 && t<220?0.6f:t>=300 && t<340?-0.8f:0.0f;
    in.charge=t>=90 && t<140;
    in.jump_pressed=t==240 || t==252 || t==460;in.jump_held=(t>=240 && t<290) || (t>=460 && t<480);
    in.flame_pressed=t==60 || t==400 || t==520;
    in.headbash_pressed=t==470;
    return in;
}
// Advances `ticks` simulation steps and folds the observable game state.
inline std::uint64_t run(Game& g,unsigned ticks) {
    Hash h;
    for(unsigned i=0;i<ticks;++i) {
        if(std::getenv("SPYRO_REPLAY_TRACE"))std::fprintf(stderr,"tick %u\n",i);
        g.advance(FixedClock::step,scripted(i));
        h.v3(g.player.position);h.v3(g.player.velocity);h.f(g.player.yaw);h.f(g.player.pitch);
        h.u(static_cast<unsigned>(g.player.motion));h.u(g.player.gems);h.u(g.player.health);h.u(g.player.lives);
        h.u(g.animation.state().clip);h.u(g.animation.state().frame);
        if(i%30==29)for(const auto& o:g.retail_objects) {
            h.v3(o.position);h.u(static_cast<std::uint64_t>(o.state)+128);h.u(o.collected);h.u(o.attack_flags);h.u(o.frozen_ticks);
            if(o.enemy){h.u(o.enemy->defeated);h.u(static_cast<unsigned>(o.enemy->phase));}
        }
    }
    h.u(g.retail_objects.size());h.u(g.level.triangles.size());h.u(g.level.visual_vertices.size());
    return h.value;
}
} // namespace spyro::replay
