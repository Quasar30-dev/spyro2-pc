#pragma once
#include "spyro/runtime.hpp"
// Repeatable input-only traversal for native footage and collision validation.
// The caller chooses the initial spawn; this never moves or heals the player.
struct CrownworksSwimRoute {
    unsigned stage{},waypoint{};
    std::vector<spyro::Vec3> route{{-26,-8.5f,-12},{-18,-8.5f,-12},{-2,-8.5f,-12},
        {-2,-8.5f,0},{-2,-8.5f,12},{10,-8.5f,12},{26,-8.5f,12},{26,2.9f,12}};
    spyro::InputState input(const spyro::Game& g) {
        using namespace spyro;InputState out;
        if(stage==0) {
            if(g.player.swim.mode==WaterMode::Surface){stage=1;return out;}
            out.move_z=1;return out;
        }
        if(stage==1) {
            if(g.player.swim.mode==WaterMode::Underwater){stage=2;return out;}
            out.charge=g.player.swim.surface_ticks>25;return out;
        }
        if(stage==2) {
            if(waypoint+1==route.size() && g.player.swim.mode==WaterMode::Surface){stage=3;return out;}
            if(waypoint>=route.size()){stage=3;return out;}
            const auto d=route[waypoint]-g.player.position;const float flat=std::hypot(d.x,d.z);
            if(length(d)<.65f && waypoint+1<route.size()){++waypoint;return out;}
            const float yaw=flat>.5f?std::atan2(d.x,d.z):g.player.yaw;
            const float pitch=std::clamp(std::atan2(d.y,std::max(.01f,flat)),-1.20f,1.20f);
            const float heading=std::remainder(yaw-g.player.yaw,6.283185307f);
            out.move_x=std::clamp(heading*3,-1.f,1.f);
            out.move_z=std::clamp((g.player.pitch-pitch)*4,-1.f,1.f);
            out.jump_held=std::abs(heading)<.65f && std::abs(g.player.pitch-pitch)<.5f;
            return out;
        }
        if(stage==3) {
            out.move_z=1;out.jump_pressed=g.player.swim.mode==WaterMode::Surface;out.jump_held=true;
            if(g.player.swim.mode==WaterMode::Dry && g.player.grounded && g.player.position.z>14.5f)stage=4;
        }
        return out;
    }
};
