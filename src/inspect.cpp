#include "spyro/assets.hpp"
#include "spyro/level.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/camera.hpp"
#include "spyro/game_build.hpp"
#include "spyro/runtime.hpp"
#include <fstream>
#include <map>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc<2) { std::cerr<<"Usage: spyro_inspect <ISO|CUE|BIN|directory> [--all-levels] [--level ID] [--obj output.obj]\n"; return 2; }
        spyro::AssetStore assets(std::filesystem::u8path(argv[1]));
        std::cout<<assets.description()<<"\nFiles: "<<assets.files().size()<<'\n';
        const auto executable=assets.read_all("SCUS_944.25",4*1024*1024);
        const auto exe=spyro::inspect_exe(executable);
        std::cout<<std::hex<<"SCUS_944.25 CRC32: "<<exe.crc<<"  load: "<<exe.load_address<<"  entry: "<<exe.pc<<std::dec<<'\n';
        spyro::WadArchive wad(assets);
        unsigned empty=0,nested=0;
        for (const auto& e:wad.entries()) {
            if (!e.size) ++empty;
            else try { auto index=spyro::parse_wad(wad.read(e.id,0,std::min<std::size_t>(e.size,0x10000)),e.size); ++nested; }
            catch (const spyro::FormatError&) {}
        }
        std::cout<<"WAD slots: "<<wad.entries().size()<<"  zero length: "<<empty<<"  nested candidates: "<<nested<<'\n';
        bool all=false,models=false,pickups=false,breakables=false,powerups=false; int slot=-1; std::filesystem::path output;
        for (int i=2;i<argc;++i) {
            const std::string arg=argv[i];
            if (arg=="--all-levels") all=true;
            else if(arg=="--spyro")models=true;
            else if(arg=="--powerups") {powerups=true;models=true;}
            else if(arg=="--verify-pickups")pickups=true;
            else if(arg=="--verify-breakables")breakables=true;
            else if (arg=="--level" && i+1<argc) slot=std::stoi(argv[++i]);
            else if (arg=="--obj" && i+1<argc) output=std::filesystem::u8path(argv[++i]);
            else throw spyro::FormatError("Unknown/incomplete option: "+arg);
        }
        if (!output.empty() && slot<0) throw spyro::FormatError("--obj requires --level");
        if ((all || models || slot>=0) && exe.crc!=0xcdc0917c) throw spyro::FormatError("Level decoding is currently verified only for this US executable CRC32 cdc0917c");
        if((models || pickups || breakables) && !all && slot<0)slot=15;
        if(all || slot>=0) {
            const auto camera=spyro::decode_camera_settings(executable);
            std::cout<<"Retail camera: radius "<<camera.follow[2]<<", elevation "<<camera.follow[1]
                <<"/4096 turn; vertical FOV "<<camera.vertical_fov()*180/std::numbers::pi_v<float><<'\n';
            std::cout<<"Shared world scale: 1/"<<spyro::retail::units_per_world<<"; Spyro origin/sphere radius "
                <<spyro::retail::spyro_origin_units<<" retail units\n";
        }
        std::uint64_t total=0,model_frames=0,total_objects=0,total_gems=0,verified_pickups=0,object_frames=0,object_clips=0;
        unsigned verified_containers=0,container_value=0;
        bool verified_flame_geometry=false;
        const auto global=models?wad.read(0):spyro::Bytes{};
        const auto powerup_archive=powerups?wad.read(29):spyro::Bytes{};
        for (const auto& e:wad.entries()) {
            if (static_cast<int>(e.id)!=slot && !(all && e.id>=15 && e.id<=95 && e.id%2)) continue;
            const auto archive=wad.read(e.id);
            auto level=spyro::decode_level(archive,e.id);
            total+=level.triangles.size();
            std::cout<<"  Sky: "<<level.sky_regions.size()<<" regions, "<<level.sky_vertices.size()/3<<" triangles, "
                <<level.sky_visibility.size()<<" visibility bytes\n";
            std::cout<<"  Terrain: "<<level.terrain_regions<<" regions, "<<level.terrain_faces<<" faces, "<<level.visual_vertices.size()/3<<" native triangles\n";
            const auto translucent=std::count_if(level.visual_vertices.begin(),level.visual_vertices.end(),[&](const auto& v){
                return level.terrain_materials.at(v.material).translucent;
            });
            std::cout<<"  Terrain materials: "<<level.terrain_materials.size()<<"; "
                <<std::count_if(level.terrain_materials.begin(),level.terrain_materials.end(),[](const auto& m){return m.translucent;})
                <<" translucent, "<<translucent/3<<" blended triangles\n";
            std::cout<<"Level WAD "<<e.id<<": "<<level.triangles.size()<<" collision triangles; retail spawn "
                <<level.spawn.x<<','<<level.spawn.y<<','<<level.spawn.z<<'\n';
            const auto object_models=spyro::decode_object_models(archive,executable);
            if(!object_models.flame.texture_rgba.empty()) {
                unsigned poses=0;
                if(!verified_flame_geometry) {
                    spyro::FlamePose flame;
                    for(unsigned curve=0;curve<8;++curve)for(unsigned cutoff=0;cutoff<=32;++cutoff)
                        for(unsigned half_age=2;half_age<(curve<4?60u:72u);++half_age) {
                            flame.streams={};flame.streams[curve]={half_age*0.5f,cutoff};
                            const auto geometry=spyro::flame_geometry(object_models.flame,flame,object_models.sine,object_models.cosine);
                            for(const auto& vertices:{std::span(geometry.tips),std::span(geometry.trails)})for(const auto& v:vertices)
                                if(!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z) ||
                                    v.u<0 || v.u>1 || v.v<0 || v.v>1 || v.color.x<0 || v.color.x>1)
                                    throw spyro::FormatError("Invalid interpolated flame geometry / material");
                            ++poses;
                        }
                    verified_flame_geometry=true;
                }
                std::cout<<"  Flame: eight retail curves, 32x128 additive material";
                if(poses)std::cout<<", "<<poses<<" age / impact poses verified";
                std::cout<<'\n';
            }
            const auto pickup_camera=spyro::decode_camera_settings(executable);
            spyro::initialize_retail_objects(level,object_models);
            unsigned static_models=0,meshes=0,faces=0,gems=0,gem_value=0;
            for(const auto& [id,m]:object_models.models)if(!m.animated) {
                ++static_models;meshes+=static_cast<unsigned>(m.meshes.size());
                for(const auto& mesh:m.meshes)faces+=static_cast<unsigned>(mesh.faces.size());
            }
            for(const auto& object:level.objects)if(object.type==1){++gems;gem_value+=object.held_gem;}
            total_objects+=level.objects.size();total_gems+=gems;
            {
                // 8006feb0: the scene's extra Spyro animation records (id, size, data).
                std::size_t at=spyro::read_u32(archive,spyro::level_model_directory(archive));
                std::cout<<"  Spyro scene clips:";
                for(unsigned guard=0;at && guard<64 && at+8<=archive.size();++guard) {
                    const auto id=spyro::read_u32(archive,at);if(id==0xffffffff)break;
                    const auto size=spyro::read_u32(archive,at+4);std::cout<<' '<<id;at+=8+size;
                }
                std::cout<<'\n';
            }
            {
                std::map<unsigned,unsigned> types;std::map<unsigned,spyro::Bounds> where;
                for(const auto& t:level.triangles)if(t.surface_type!=UINT32_MAX){++types[t.surface_type];where[t.surface_type].add(t.a);}
                if(!types.empty()){std::cout<<"  Surface types:";for(const auto& [type,n]:types)std::cout<<" "<<type<<"="<<n
                    <<"(y "<<where[type].min.y<<".."<<where[type].max.y<<")";std::cout<<'\n';}
            }
            for(const auto& object:level.objects)if(object.powerup_gate)
                std::cout<<"  Power-up gate: record "<<object.id<<" class 0x"<<std::hex<<object.type<<std::dec<<" kind "<<object.powerup_gate->kind
                    <<" duration "<<object.powerup_gate->duration<<" requirement "<<object.powerup_gate->requirement<<'\n';
            {
                std::map<unsigned,std::vector<unsigned>> spirits;
                for(const auto& object:level.objects)if(object.record[83]&127)spirits[object.type].push_back(object.id);
                for(const auto& [type,records]:spirits) {
                    std::cout<<"  Spirit carrier 0x"<<std::hex<<type<<std::dec<<":";
                    for(const auto id:records)std::cout<<' '<<id<<'='<<(level.objects[id].record[83]&127);
                    std::cout<<'\n';
                }
            }
            std::cout<<"  Objects: "<<level.objects.size()<<" records, "<<gems<<" loose gems (value "<<gem_value
                <<"); models "<<object_models.models.size()<<" / "<<static_models<<" static, "<<meshes
                <<" meshes, "<<faces<<" faces, "<<object_models.texture_tiles<<" texture palettes\n";
            unsigned animated_classes=0,clips=0,frames=0,special_faces=0;
            for(const auto& [id,m]:object_models.models)if(m.animated) {
                ++animated_classes;clips+=static_cast<unsigned>(m.animations.size());
                for(unsigned clip=0;clip<m.animations.size();++clip) {
                    frames+=static_cast<unsigned>(m.animations[clip].frames.size());
                    auto state=spyro::start_object_animation(m,clip);
                    for(unsigned tick=0;tick<600;++tick) {
                        spyro::tick_object_animation(m,state);
                        const auto pose=spyro::sample_object_animation(m,state,object_models.animation_blends);
                        if(pose.mesh>=m.meshes.size())throw spyro::FormatError("Animated object selects an invalid mesh");
                    }
                    spyro::object_animation_frame(m,clip,0.5f);
                }
                for(const auto& mesh:m.meshes)for(const auto& face:mesh.faces)if(!face.palette_valid)++special_faces;
            }
            object_frames+=frames;object_clips+=clips;
            std::cout<<"  Animated objects: "<<animated_classes<<" classes, "<<clips<<" clips, "<<frames
                <<" decoded frames; "<<special_faces<<" faces require special material shading\n";
            if(pickups)for(const auto& object:level.objects)if(object.gem_value) {
                spyro::Scene scene;scene.wad_slot=e.id;scene.spawn=object.position;
                spyro::Game game(level,scene,pickup_camera,&object_models);
                game.advance(spyro::FixedClock::step,{});
                if(!game.retail_objects.at(object.id).collected)
                    throw spyro::FormatError("Direct pickup failed at WAD "+std::to_string(e.id)+" record "+std::to_string(object.id));
                unsigned expected=0;for(const auto& item:game.retail_objects)if(item.collected)expected+=item.gem_value;
                if(game.player.gems!=expected || level.objects.at(object.id).collected)
                    throw spyro::FormatError("Gem accounting or authoring isolation failed");
                ++verified_pickups;
            }
            if(breakables)for(const auto& object:level.objects)if(spyro::is_breakable(object.type)) {
                auto instances=level.objects;spyro::ObjectInteractions interactions;unsigned value=0;
                const auto origin=object.position+spyro::retail::origin_above_feet;
                interactions.attack_sphere(instances,object_models,origin,spyro::retail::spyro_origin_height,spyro::charge_attack);
                interactions.tick(instances,&object_models,level,origin,{},0,{},true,value);
                if(instances.at(object.id).state>=0 || object.state<0)
                    throw spyro::FormatError("Retail container hit / authoring isolation failed at WAD "+std::to_string(e.id)+" record "+std::to_string(object.id));
                const auto fragments=std::count_if(instances.begin(),instances.end(),[&](const auto& fragment){
                    return fragment.source_id==object.id && spyro::is_container_fragment(fragment.type);});
                if(fragments!=12)throw spyro::FormatError("Container did not produce 12 retail fragments");
                for(const auto& fragment:instances)if(fragment.source_id==object.id && spyro::is_container_fragment(fragment.type)) {
                    const auto& bank=object_models.models.at(fragment.type);
                    if(fragment.model>=bank.meshes.size() || bank.meshes[fragment.model].vertices.empty())
                        throw spyro::FormatError("Container fragment has no retail geometry");
                }
                if(object.held_gem!=255) {
                    const auto gem=std::find_if(instances.begin(),instances.end(),[&](const auto& item){return item.source_id==object.id && item.type==1;});
                    if(gem==instances.end() || gem->gem_value!=object.held_gem || gem->state!=3)
                        throw spyro::FormatError("Container released an incorrect gem / flight state");
                    spyro::SparxCompanion courier;
                    for(unsigned tick=0;tick<40;++tick) {courier.tick(instances,&object_models,level,origin,0,{},true,false,0);courier.award_deliveries(value);}
                    unsigned expected=0;for(const auto& item:instances)if(item.collected)expected+=item.gem_value;
                    if(!gem->collected || value!=expected)throw spyro::FormatError("Container gem failed to deliver exactly once");
                    container_value+=object.held_gem;
                }
                ++verified_containers;
            }
            if(models) {
                const auto model=spyro::decode_spyro(global,archive,executable,powerup_archive,29);
                spyro::SpyroAnimator animator;animator.reset(&model);
                for(const unsigned clip:{0u,2u,5u,6u,0u,12u,16u,24u,16u,0u}) {
                    for(unsigned tick=0;tick<90;++tick) {
                        animator.tick(clip);
                        const auto pose=animator.sample(0.5f);
                        for(const auto vertex:pose.vertices)if(!spyro::finite(vertex))throw spyro::FormatError("Invalid retail movement transition pose");
                    }
                    if(animator.state().clip!=clip || animator.state().next_clip!=clip)
                        throw spyro::FormatError("Retail movement transition did not complete");
                }
                if(powerups) {
                    for(const unsigned clip:{33u,34u,33u,35u,33u,61u,25u,61u,0u}) {
                        for(unsigned tick=0;tick<90;++tick) {
                            animator.tick(clip);
                            const auto pose=animator.sample(0.5f);
                            for(const auto vertex:pose.vertices)if(!spyro::finite(vertex))throw spyro::FormatError("Invalid power-up transition pose");
                            const auto regular=animator.sample(0.5f,spyro::SuperchargeAppearance::MainModel);
                            for(const auto vertex:regular.vertices)if(!spyro::finite(vertex))throw spyro::FormatError("Invalid retargeted power-up pose");
                            const auto& faces=model.clip(animator.state().clip,spyro::SuperchargeAppearance::MainModel).faces;
                            for(const auto& face:faces)for(auto i:face.vertices)if(i>=regular.vertices.size())
                                throw spyro::FormatError("Retargeted face references another vertex layout");
                        }
                        if(animator.state().clip!=clip || animator.state().next_clip!=clip)
                            throw spyro::FormatError("Retail power-up animation transition did not complete");
                    }
                    std::cout<<"  Power-up animations: 25/33/34/35/61 available; imports";
                    for(const unsigned clip:{25u,33u,34u,35u,61u})if(model.animations.at(clip).imported_from_slot)
                        std::cout<<' '<<clip<<"@"<<model.animations.at(clip).imported_from_slot;
                    std::cout<<'\n';
                    const auto& regular=model.main_model_supercharge;const auto& original=model.animations.at(61);
                    if(regular.frames.size()!=original.frames.size() || model.supercharge_blur_vertices!=123 || regular.frames[0].vertices.size()!=353)
                        throw spyro::FormatError("Supercharge appearance lost retail frames or leg blur");
                    const auto& main_charge=model.animations.at(12);
                    const auto& head_bind=main_charge.frames[main_charge.loop_start].vertices;
                    const auto& wing_bind=main_charge.frames[0].vertices;
                    const auto head_width=spyro::length(head_bind[151]-head_bind[184]);
                    for(unsigned f=0;f<regular.frames.size();++f) {
                        const auto& frame=regular.frames[f];
                        if(frame.ticks!=original.frames[f].ticks || std::abs(spyro::length(frame.vertices[151]-frame.vertices[184])-head_width)>0.00001f)
                            throw spyro::FormatError("Retarget changed supercharge timing or main head proportions");
                        for(const auto indices:{std::vector<unsigned>{58,59,60,61,62,64,65,66},std::vector<unsigned>{63,67,68,69,70,71,72,73}})
                            for(auto a:indices)for(auto b:indices)
                                if(std::abs(spyro::length(frame.vertices[a]-frame.vertices[b])-spyro::length(wing_bind[a]-wing_bind[b]))>0.00001f)
                                    throw spyro::FormatError("Supercharge retarget shrank or stretched the main model's wings");
                        const auto socket=(frame.vertices[50]+frame.vertices[51]+frame.vertices[52]+frame.vertices[53])*0.25f;
                        const auto& native=original.frames[f].vertices;
                        const auto expected=(native[140]+native[138]+native[142]+native[144])*0.25f;
                        if(spyro::length(socket-expected)>0.00001f)throw spyro::FormatError("Supercharge neck moved away from its retail socket");
                    }
                    if(model.charge_smoke.texture_rgba.size()!=32*32*4)throw spyro::FormatError("Cross-level supercharge smoke is missing");
                    std::cout<<"  Retarget: 12 frames, 353 vertices, "<<model.supercharge_blur_faces<<" original blur faces; retail smoke 32x32\n";
                }
                if(level.surface_swimming) {
                    spyro::SpyroSecondaryAnimator head;head.reset(&model,animator.state());
                    std::vector<unsigned> sequence{44,43,44,5,44};
                    if(level.underwater_swimming)sequence.insert(sequence.end(),{40,41,40,42,40,41,44,0});
                    for(const auto clip:sequence) {
                        for(unsigned tick=0;tick<100;++tick) {
                            animator.tick(clip);head.tick(0,animator.state(),true);
                            const auto pose=model.secondary_track(animator.state().clip,animator.sample(0.5f),head.sample(0.5f));
                            for(const auto v:pose.vertices)if(!spyro::finite(v))throw spyro::FormatError("Invalid swimming transition pose");
                        }
                        if(animator.state().clip!=clip || animator.state().next_clip!=clip)throw spyro::FormatError("Swimming transition failed to finish");
                    }
                    std::cout<<"  Water: "<<level.water_triangles<<" triangles; surface 43/44"<<(level.underwater_swimming?"; underwater 40/41/42":"")<<'\n';
                }
                std::uint64_t frames=0;
                for(const auto& [id,animation]:model.animations) {
                    frames+=animation.frames.size();
                    for(const auto& frame:animation.frames)for(const auto p:frame.vertices)
                        if(!spyro::finite(p) || spyro::length(p)>64)throw spyro::FormatError("Implausible Spyro vertex position");
                    // Exercise intro, loop, static end poses, and fractional samples.
                    for(const double t:{0.0,0.0166666667,0.123,1.0,19.0,120.0})model.sample(id,t);
                }
                model_frames+=frames;
                std::cout<<"  Spyro: "<<model.animations.size()<<" clips, "<<frames<<" decoded frames, "<<model.texture_tiles<<" texture palettes\n";
                spyro::Bounds body;for(const auto p:model.animations.at(0).frames.front().vertices)body.add(p);
                const auto extent=(body.max-body.min)*spyro::retail::units_per_world;
                std::cout<<"  Idle frame 0 bounds (retail units): width "<<extent.x<<", height "<<extent.y<<", length "<<extent.z<<'\n';
            }
            if (!output.empty() && static_cast<int>(e.id)==slot) {
                if (output.extension()!=".obj") throw spyro::FormatError("Expected .obj export extension");
                std::ofstream obj(output); if (!obj) throw spyro::FormatError("Cannot write OBJ");
                obj<<"# Collision geometry from user-supplied retail WAD slot "<<e.id<<"; Y-up, scale 1/1024\n";
                for (auto& t:level.triangles) for (auto v:{t.a,t.b,t.c}) obj<<"v "<<v.x<<' '<<v.y<<' '<<v.z<<'\n';
                for (std::size_t i=0;i<level.triangles.size();++i) obj<<"f "<<i*3+1<<' '<<i*3+2<<' '<<i*3+3<<'\n';
                if (!obj) throw spyro::FormatError("OBJ write failed");
            }
        }
        if (total) std::cout<<"Decoded triangles total: "<<total<<'\n';
        if(model_frames)std::cout<<"Decoded Spyro frames across selected level banks: "<<model_frames<<'\n';
        if(object_frames)std::cout<<"Decoded object animations across selected level banks: "<<object_clips<<" clips / "<<object_frames<<" frames\n";
        if(total_objects)std::cout<<"Retail objects total: "<<total_objects<<"; loose gems "<<total_gems<<'\n';
        if(pickups)std::cout<<"Verified retail direct pickups: "<<verified_pickups<<'\n';
        if(breakables)std::cout<<"Verified retail containers: "<<verified_containers<<"; released gem value "<<container_value<<'\n';
        return 0;
    } catch (const std::exception& error) { std::cerr<<"error: "<<error.what()<<'\n';return 1; }
}
