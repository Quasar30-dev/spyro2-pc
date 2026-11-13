#pragma once
#include "spyro/level.hpp"

namespace spyro {
class Game;
struct InputState;
unsigned custom_level_bank(const std::filesystem::path& path);
// Actor classes a version 4 world borrows from other retail scenes: (slot, class).
std::vector<std::pair<unsigned,unsigned>> custom_level_imports(const std::filesystem::path& path);
// Adds those classes' models (decoded from the mounted disc) to the bank's models.
void import_custom_level_models(const std::filesystem::path& path,WadArchive& wad,std::span<const std::uint8_t> executable,ObjectModels& models);
void tick_custom_level(Game& game,const InputState& input);
// S2WORLD stores authored geometry and references to assets in the mounted
// retail copy. It contains neither an executable overlay nor model/texture data.
Level load_custom_level(const std::filesystem::path& path,WadArchive& wad,
                        const Level& actor_source,const ObjectModels& models,
                        const ReturnPortalAssets& portal);
}
