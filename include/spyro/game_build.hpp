#pragma once
#include "spyro/assets.hpp"
#include <array>
#include <optional>
#include <string_view>

namespace spyro {
enum class GameBuild { RetailUS, July1999, August1999, WinterJampack1999, YearOfTheDragonUS };
struct GameProfile {
    GameBuild build;
    std::uint32_t crc;
    std::string_view name;
    bool yotd()const{return build==GameBuild::YearOfTheDragonUS;}
    bool prerelease()const{return build!=GameBuild::RetailUS && !yotd();}
    bool demo()const{return build==GameBuild::WinterJampack1999;}
    std::optional<unsigned> physical_slot(unsigned logical)const;
    std::vector<unsigned> levels()const;
    unsigned first_level()const{return yotd()?97:demo()?27:15;}

};
const GameProfile& game_profile(std::uint32_t crc);
std::string game_level_name(std::span<const std::uint8_t> executable,unsigned slot);
const GameProfile& game_profile(std::span<const std::uint8_t> executable);
// Compilation discs boot a menu. Select Spyro's executable inside its own
// directory, keeping the ISO paths and original executable bytes intact.
std::string find_game_executable(const AssetStore& source);
std::string game_asset_prefix(const AssetStore& source);

// A table identity is named by its original retail VA; the returned file
// offset belongs to the mounted executable. No CRC spoofing or copied tables.
class ExeTables {
public:
    explicit ExeTables(std::span<const std::uint8_t> exe):profile(game_profile(exe)),bytes_(exe){}
    std::size_t operator()(unsigned retail_address)const;
    std::size_t native(unsigned address)const;
    const GameProfile& profile;
private:
    std::span<const std::uint8_t> bytes_;
};
// Level archives have four extents. Early builds place the model directory
// immediately at +20; retail reserves +20..3f before its directory at +40.
std::vector<WadEntry> parse_level_archive(std::span<const std::uint8_t> archive);
std::vector<WadEntry> parse_level_archive(std::span<const std::uint8_t> archive,const GameProfile& profile);
unsigned level_model_directory(std::span<const std::uint8_t> archive);
unsigned level_model_directory(std::span<const std::uint8_t> archive,const GameProfile& profile);
// The Jampack scene omits both the speech directory and portal pictures.
// Validate the record table at the seventh gameplay section before using it.
bool compact_demo_layout(std::span<const std::uint8_t> archive);
} // namespace spyro
