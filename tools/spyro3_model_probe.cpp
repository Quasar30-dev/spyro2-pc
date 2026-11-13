#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include "spyro/objects.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/audio.hpp"
#include "spyro/yotd_assets.hpp"
#include <iostream>
int main(int argc,char** argv) {
    try {
        spyro::AssetStore source(argc>1?argv[1]:"data-yotd");
        const auto exe=source.read_all(spyro::find_game_executable(source));
        const auto& profile=spyro::game_profile(exe);spyro::WadArchive wad(source,&profile);
        const auto global=wad.read(0);unsigned failures=0,total_objects=0,total_models=0,total_clips=0,total_frames=0,vram_patches=0;
        std::uint64_t vram_changes=0;
        std::optional<spyro::SpyroModel> donor;std::optional<spyro::SpyroRollBank> rolls;
        if(argc>2) {
            spyro::AssetStore original(argv[2]);const auto donor_exe=original.read_all(spyro::find_game_executable(original));
            const auto& donor_profile=spyro::game_profile(donor_exe);spyro::WadArchive donor_wad(original,&donor_profile);
            donor=spyro::decode_spyro(donor_wad.read(0),donor_wad.read(15),donor_exe,donor_wad.read(29),29,
                donor_wad.read(15),donor_wad.read(45),donor_wad.read(59),donor_wad.read(57));
        }
        if(argc>3) {
            spyro::AssetStore source(argv[3]);const auto roll_exe=source.read_all(spyro::find_game_executable(source));
            const auto& roll_profile=spyro::game_profile(roll_exe);spyro::WadArchive roll_wad(source,&roll_profile);
            rolls=spyro::decode_july_rolls(roll_wad.read(0),roll_exe);
        }
        for(unsigned slot:profile.levels()) {
            const auto archive=wad.read(slot);
            for(unsigned area=0;area<=(profile.yotd()?4u:0u);++area) {
            if(area && (!spyro::read_u32(archive,0x20+(area-1)*16) || !spyro::read_u32(archive,0x24+(area-1)*16)))continue;
            try {
                if(profile.yotd()) {
                    const auto image=spyro::decode_yotd_vram(archive,profile,area);
                    const auto children=spyro::parse_level_archive(archive,profile);
                    const auto original=std::span<const std::uint8_t>(archive).subspan(children.at(0).offset,0x80000);
                    if(image.pixels.size()!=original.size())throw spyro::FormatError("Native VRAM image dimensions changed");
                    for(unsigned byte=0;byte<original.size();++byte) {
                        const auto x=512+(byte/2)%512,y=(byte/2)/512;auto expected=original[byte];
                        if(image.patch) {
                            const auto& p=*image.patch;
                            if(x>=p.x && x<p.x+p.width && y>=p.y && y<p.y+p.height)
                                expected=archive.at(p.source_offset+((y-p.y)*p.width+x-p.x)*2+(byte&1));
                        }
                        if(image.pixels[byte]!=expected)throw spyro::FormatError("Native area VRAM changed a pixel outside its exact source rectangle");
                        vram_changes+=image.pixels[byte]!=original[byte];
                    }
                    vram_patches+=image.patch.has_value();
                }
                const auto objects=spyro::decode_object_instances(archive,slot,profile,area);
                const auto models=spyro::decode_object_models(archive,exe,area);
                const auto sounds=spyro::decode_sound_bank(archive,exe,slot,area);
                auto player=spyro::decode_spyro(global,archive,exe,{},0,{},{},{},{},area);
                if(donor) {
                    const auto original=player.animations.at(0);
                    spyro::import_retail_power_animations(player,*donor);
                    const auto& remapped=player.animations.at(0);
                    if(profile.yotd()) {
                        if(player.source_vertices.size()!=230)throw spyro::FormatError("Incomplete canonical source vertex map");
                        for(unsigned frame=0;frame<original.frames.size();++frame)for(unsigned v=0;v<230;++v) {
                            const auto source=player.source_vertices[v];
                            if(source>=230 || ((source<115)!=(v<115)))throw spyro::FormatError("Canonical source map crossed a partition");
                            const auto delta=remapped.frames[frame].vertices[v]-original.frames[frame].vertices[source];
                            if(spyro::dot(delta,delta)!=0)throw spyro::FormatError("Canonical import changed mounted neutral geometry");
                        }
                    }
                    const auto texture_crc=spyro::crc32(player.texture_rgba),tiles=player.texture_tiles;
                    spyro::import_retail_power_animations(player,*donor);
                    if(texture_crc!=spyro::crc32(player.texture_rgba) || tiles!=player.texture_tiles)
                        throw spyro::FormatError("Repeated PC motion import changed its texture atlas");
                    if(player.main_model_supercharge.frames.empty() || player.main_model_supercharge_charge_wings.frames.empty())
                        throw spyro::FormatError("Missing canonical main-model charge variants");
                    if(profile.yotd())for(const auto id:{0u,2u,5u,6u,7u,9u,12u,13u,16u,17u,18u,21u,24u,25u,33u,34u,35u,40u,41u,42u,43u,44u,46u,49u,57u,61u})
                        if(!player.animations.contains(id) || player.animations.at(id).frames.empty())
                            throw spyro::FormatError("Missing required PC controller motion "+std::to_string(id));
                }
                if(rolls) {
                    const auto pixels=spyro::crc32(player.texture_rgba);const auto original=player.animations.at(0).frames.at(0).vertices;
                    spyro::import_july_rolls(player,*rolls);
                    if(!player.cut_rolls_imported || pixels!=spyro::crc32(player.texture_rgba))
                        throw spyro::FormatError("Verified July roll import changed the native skin or omitted its identity");
                    for(unsigned v=0;v<original.size();++v) {
                        const auto delta=original[v]-player.animations.at(0).frames.at(0).vertices.at(v);
                        if(spyro::dot(delta,delta)!=0)throw spyro::FormatError("July roll import changed the mounted neutral geometry");
                    }
                    for(const auto id:{10u,11u})if(player.animations.at(id).faces.size()!=player.animations.at(0).faces.size())
                        throw spyro::FormatError("July roll import lost mounted face topology");
                } else if(player.cut_rolls_imported)throw spyro::FormatError("Unverified native clips advertised cut rolls");
                for(const auto& [id,clip]:player.animations)for(const auto& frame:clip.frames)for(const auto vertex:frame.vertices)
                    if(!spyro::finite(vertex) || spyro::length(vertex)>16)throw spyro::FormatError("Implausible Spyro pose after decoding/import");
                unsigned clips=0,frames=0,faces=0;for(const auto& [type,model]:models.models) {
                    clips+=model.animations.size();for(const auto& c:model.animations)frames+=c.frames.size();
                    for(const auto& m:model.meshes)faces+=m.faces.size();
                }
                unsigned missing=0;for(const auto& object:objects)if(!models.models.contains(object.type))++missing;
                std::cout<<"slot "<<slot<<" area "<<area<<": "<<objects.size()<<" objects, "<<models.models.size()<<" models, "<<clips<<" object clips / "<<frames<<" frames / "<<faces<<" faces, "<<missing<<" missing, "<<player.animations.size()<<" Spyro clips, "<<models.texture_tiles<<" texture tiles, "<<player.source_vertices.size()<<" canonical vertices, "<<sounds.sounds.size()<<" native sounds\n";
                total_objects+=objects.size();total_models+=models.models.size();total_clips+=clips;total_frames+=frames;
            } catch(const std::exception& error) {std::cout<<"slot "<<slot<<" area "<<area<<": ERROR "<<error.what()<<'\n';++failures;}
            }
        }
        std::cout<<"TOTAL objects="<<total_objects<<" models="<<total_models<<" clips="<<total_clips<<" frames="<<total_frames<<" failures="<<failures<<" vram_patches="<<vram_patches<<" changed_vram_bytes="<<vram_changes<<'\n';
        return failures?1:0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
