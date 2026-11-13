#pragma once
#include "spyro/powerups.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/audio.hpp"
#include "spyro/interface.hpp"

namespace spyro {
inline constexpr std::array<unsigned,8> retail_power_types{0x2c2,0x146,0x1e2,0x21d,0x1e3,0x1f8,0x1e1,0x211};
// Decoded from a separately supplied retail copy. Prototype sources, music,
// level objects and save identities remain attached to their mounted build.
struct RetailPowerupBank {
    SpyroModel spyro;
    ObjectModels objects;
    PowerupAssets effects;
    SoundBank sounds;
};
RetailPowerupBank read_retail_powerups(const std::filesystem::path& source);
void import_retail_powerups(SpyroModel& spyro,ObjectModels& objects,PowerupAssets& effects,const RetailPowerupBank& donor);
// Optional PC effects/interface donor for YOTD. It never replaces the mounted
// game's scene, Spyro skin, object IDs or executable identity.
struct YotdDonorBank {
    RetailPowerupBank powers;
    ObjectModels common;
    HudAssets hud;
    InterfaceAssets ui;
    SoundBank sounds;
    Bytes ice_tile;
};
YotdDonorBank read_yotd_donor(const std::filesystem::path& source);
void import_yotd_pc_assets(SpyroModel& spyro,ObjectModels& objects,PowerupAssets& effects,const YotdDonorBank& donor);
} // namespace spyro
