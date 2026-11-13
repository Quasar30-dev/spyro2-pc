#pragma once
#include "spyro/runtime.hpp"
#include <map>

namespace spyro {
struct RemovedObject {unsigned id{},type{};};
struct SavedGem {unsigned source{},value{};Vec3 position;};
struct SavedButterfly {unsigned source{},type{},age{},heading{};Vec3 position,home;};
struct LevelProgress {
    Vec3 position,spawn;float yaw{};
    std::vector<Entity> entities;
    std::vector<RemovedObject> removed;
    std::vector<SavedGem> pending_gems;
    std::vector<SavedButterfly> butterflies;
    std::vector<NpcProgress> conversations;
    std::vector<CarryCheckpoint> bodies;
    PowerCheckpoint powers;
    Vec3 velocity;float pitch{},roll{};
    GuideAwards awards;
    std::vector<ScriptProgress> scripts;
    std::vector<HeadbashStamp> craters;
    std::vector<TerrainDamage> terrain_damage;
    // Original scripts' persistent state (8004c214 records): quest flags,
    // conversation line pointers, one-shot triggers.
    std::vector<RetailMemory> retail_memory;
};
// Portable progression checkpoint, independent of the mounted copy's path.
// Transient particles, attacks and unfinished conversations restart on load.
struct GameSave {
    unsigned retail_crc=0xcdc0917c,slot=15,gems=0,lives=3,health=3,orbs=0,talismans=0;
    bool active_camera=true;
    Cheats cheats;
    UpgradeProgress upgrades;
    UpgradeHudSettings upgrade_hud_settings;
    DestructionSettings destruction_settings;
    bool permanent_superflame=false;
    unsigned butterfly_drops{};
    std::uint32_t purchases{};
    unsigned skill_points{};
    std::map<unsigned,LevelProgress> levels;
    std::optional<UniverseAddress> universe;
    unsigned generator_version=universe_generator_version;
    std::map<std::uint64_t,LevelProgress> universe_worlds;
    void capture(const Game& game);
    void apply(Game& game,const ObjectModels* models) const;
    void validate() const;
    void write(const std::filesystem::path& path) const;
    static GameSave read(const std::filesystem::path& path);
};
RetailObject make_saved_gem(const SavedGem& saved,const ObjectModels& models,unsigned id);
} // namespace spyro
