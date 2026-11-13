#include "spyro/powerup_import.hpp"
#include "spyro/game_build.hpp"
#include "spyro/level.hpp"

namespace spyro {
RetailPowerupBank read_retail_powerups(const std::filesystem::path& path) {
    AssetStore source(path);const auto exe=source.read_all(find_game_executable(source));
    if(game_profile(exe).build!=GameBuild::RetailUS)throw FormatError("Power import requires your US retail Spyro 2 copy");
    WadArchive wad(source);const auto forest=wad.read(15),ripto=wad.read(71);
    RetailPowerupBank bank;
    bank.spyro=decode_spyro(wad.read(0),forest,exe,wad.read(29),29);
    bank.objects=decode_object_models(ripto,exe);
    const auto forest_models=decode_object_models(forest,exe);
    const unsigned water[]{0x1e3,0x1f8,0x1e1,0x211};import_object_models(bank.objects,forest_models,water);
    for(unsigned type:retail_power_types)if(!bank.objects.models.contains(type))throw FormatError("Retail power donor is incomplete");
    std::erase_if(bank.objects.models,[](const auto& entry){return std::find(retail_power_types.begin(),retail_power_types.end(),entry.first)==retail_power_types.end();});
    bank.effects=decode_powerup_assets(ripto);
    bank.effects.bubble_particle=decode_powerup_assets(forest).bubble_particle;
    bank.sounds=decode_sound_bank(ripto,exe,71);
    import_object_sounds(bank.sounds,decode_sound_bank(forest,exe,15),water);
    return bank;
}
void import_retail_powerups(SpyroModel& spyro,ObjectModels& objects,PowerupAssets& effects,const RetailPowerupBank& donor) {
    import_retail_power_animations(spyro,donor.spyro);
    // Retail effect scripts consume these animation families. Replace early
    // variants as a complete family, including their referenced texture tiles.
    for(unsigned type:retail_power_types)objects.models.erase(type);
    import_object_models(objects,donor.objects,retail_power_types);
    objects.powered_fire_allowed=donor.objects.powered_fire_allowed;
    effects.green_particle=donor.effects.green_particle;effects.fire_particle=donor.effects.fire_particle;
    effects.bubble_particle=donor.effects.bubble_particle;
    if(effects.wing_trail.texture_rgba.empty())effects.wing_trail=donor.effects.wing_trail;
}
YotdDonorBank read_yotd_donor(const std::filesystem::path& path) {
    YotdDonorBank out;out.powers=read_retail_powerups(path);
    AssetStore source(path);const auto exe=source.read_all(find_game_executable(source));WadArchive wad(source);
    const auto forest=wad.read(15),temples=wad.read(61);
    out.powers.spyro=decode_spyro(wad.read(0),forest,exe,wad.read(29),29,forest,wad.read(45),wad.read(59),wad.read(57));
    out.common=decode_object_models(forest,exe);
    const unsigned shell[]{0x271};import_object_models(out.common,decode_object_models(temples,exe),shell);
    out.hud=decode_hud(forest,exe);out.ui=decode_interface(forest,exe,wad.read(4));
    out.sounds=decode_sound_bank(forest,exe,15);import_object_sounds(out.sounds,out.powers.sounds,retail_power_types,true);
    const auto tundra=decode_level(wad.read(57),57);
    out.ice_tile.resize(64*64*4);
    for(unsigned y=0;y<64;++y)std::copy_n(tundra.texture_rgba.data()+((std::size_t(2*64+y))*tundra.texture_width+3*64)*4,64*4,out.ice_tile.data()+y*64*4);
    for(std::size_t i=3;i<out.ice_tile.size();i+=4)out.ice_tile[i]=255;
    return out;
}
void import_yotd_pc_assets(SpyroModel& spyro,ObjectModels& objects,PowerupAssets& effects,const YotdDonorBank& donor) {
    if(objects.source_game!=GameBuild::YearOfTheDragonUS)throw FormatError("YOTD PC donor applied to another game profile");
    import_retail_power_animations(spyro,donor.powers.spyro);
    auto power_models=donor.powers.objects;std::vector<unsigned> types;
    for(unsigned type:retail_power_types) {
        auto node=power_models.models.extract(type);node.key()=objects.effect_type(type);power_models.models.insert(std::move(node));types.push_back(objects.effect_type(type));
    }
    import_object_models(objects,power_models,types);
    auto shell=donor.common;auto node=shell.models.extract(0x271);node.key()=objects.effect_type(0x271);shell.models.insert(std::move(node));
    const unsigned shell_type[]{objects.effect_type(0x271)};import_object_models(objects,shell,shell_type);
    // The existing PC elemental and powered controllers consume S2's effect
    // curves/materials. Their models live in a reserved 16-bit namespace so a
    // YOTD enemy with the same raw class ID keeps its original mesh.
    if(objects.flame.texture_rgba.empty())objects.flame=donor.common.flame;
    objects.powered_fire_allowed=donor.powers.objects.powered_fire_allowed;
    effects=donor.powers.effects;
}
} // namespace spyro
