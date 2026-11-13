#pragma once
#include "spyro/math.hpp"
#include <vector>
#include <string>
namespace spyro {
enum class ArenaKind : unsigned {Lava,Trapdoor,Vent,Cover,FireStation,FlyStation,ChargeStation,HealStation,CoolingValve,CoverSwitch,ShieldRelay,RepairRelay,OverdriveRelay,ExitGate,LaunchPad,UnderwaterJet,UnderwaterCrusher};
struct ArenaMesh {
    unsigned vertex{},vertices{},triangle{},triangles{},role{};
    std::vector<Vec3> base_positions,base_colors,base_collision;
};
struct ArenaPiece {
    ArenaKind kind{};Vec3 center,extent;float radius{},parameter{};std::string name;
    std::vector<ArenaMesh> meshes;
    unsigned phase{},timer{},cooldown{},hits{};
    float value=-999;bool destroyed=false;
};
struct CustomArena {
    std::vector<ArenaPiece> pieces;
    unsigned ticks{},cooling{},charge_ticks{},death_count{},last_lives{},damage_events{},switch_uses{},power_uses{},relay_breaks{},trap_opens{},launches{};
    bool initialized=false,covers=false,complete=false;
    bool ripto_encounter=false;
    // World devices expose these hooks for the separately authored boss later.
    bool boss_shield=true,boss_repair=true,boss_overdrive=true;
};
}
