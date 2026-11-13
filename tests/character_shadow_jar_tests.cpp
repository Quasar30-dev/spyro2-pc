#include "spyro/renderer.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/travel.hpp"
#include <fstream>
#include <iostream>

using namespace spyro;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks{};
void check(bool ok,const std::string& why){++checks;if(!ok)throw FormatError(why);}
void ticks(Game& g,unsigned n){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,{});}
Bytes pixels(const std::filesystem::path& file) {
    std::ifstream f(file,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});
}
void pixel_checks(const Bytes& off,const Bytes& on,const char* label) {
    check(off.size()==on.size() && off.size()>54,"Missing paired GPU captures");
    unsigned darker=0,leaks=0,too_dark=0;
    for(std::size_t i=54;i+3<off.size();i+=4) {
        if(on[i]+5<off[i] && on[i+1]+5<off[i+1] && on[i+2]+5<off[i+2]) {
            ++darker;
            // White terrain is the only receiver. Sky and caster colours
            // must not change when the terrain shadow switch is toggled.
            if(off[i]!=255 || off[i+1]!=255 || off[i+2]!=255)++leaks;
            if(on[i]<132 || on[i+1]<132 || on[i+2]<132)++too_dark;
        }
    }
    std::cout<<label<<": darker="<<darker<<" receiver_leaks="<<leaks<<" double_dark="<<too_dark<<'\n';
    check(darker>80,"No visible character shadow on GPU terrain");check(!leaks,"Character shadow leaked onto sky/caster");
    check(!too_dark,"Overlapping silhouette triangles darkened a receiver repeatedly");
}
void gpu_checks(const std::filesystem::path& out) {
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL feature;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&feature,&c)),"D3D11 WARP unavailable");
    Renderer r;r.initialize(d.Get(),c.Get());r.resize(512,512);
    SpyroModel model;model.texture_width=model.texture_height=1;model.texture_rgba={255,255,255,255};
    auto& clip=model.animations[0];
    // An L silhouette with an open corner. A generic ellipse cannot pass
    // the contour-mask check below. Repeated faces exercise depth union.
    SpyroPose pose;pose.vertices={{-1,1,-1},{0,1,-1},{-1,1,1},{0,1,1},{1,1,0},{1,1,1}};
    for(auto indices:{std::array<std::uint16_t,4>{0,1,2,3},{3,4,5,5}}) {
        SpyroFace f;f.vertices=indices;f.quad=indices[2]!=indices[3];f.colors.fill({.8f,.2f,.1f});clip.faces.push_back(f);
    }
    clip.faces.push_back(clip.faces.front());r.upload_spyro(model);
    Scene scene;scene.spawn={0,0,0};ObjectModels objects;
    const auto floor=[&](bool slope,bool gap) {
        Level l;l.spawn=scene.spawn;l.sky_color={.1f,.2f,.3f};l.texture_width=l.texture_height=1;l.texture_rgba={255,255,255,255};
        l.terrain_materials.push_back({});
        const float right=gap?0.1f:4.0f;
        const std::array<Vec3,6> vertices{{{-4,0,-4},{right,0,4},{right,0,-4},{-4,0,-4},{-4,0,4},{right,0,4}}};
        for(auto p:vertices){if(slope)p.y=p.x*.25f;l.visual_vertices.push_back({p,{1,1,1},.5f,.5f,0});l.bounds.add(p);}
        for(unsigned i=0;i<6;i+=3)l.triangles.push_back({l.visual_vertices[i].position,l.visual_vertices[i+1].position,l.visual_vertices[i+2].position});
        return l;
    };
    for(unsigned kind=0;kind<3;++kind) {
        const auto level=floor(kind==1,kind==2);r.upload_level(level);
        const auto name=kind==0?"flat":kind==1?"slope":"edge-gap";
        Bytes before;
        for(bool enabled:{false,true}) {
            r.character_shadows=enabled;
            r.draw(level,scene,{4,6,-7},{0,0,0},nullptr,1,false,model,pose,0,false,false,.9f,~0u,objects,{});
            const auto file=out/(std::string("gpu-")+name+(enabled?"-on.bmp":"-off.bmp"));r.save_bmp(file);
            if(enabled){check(r.rendered_character_shadows()==1,"Spyro caster missing");pixel_checks(before,pixels(file),name);}
            else before=pixels(file);
        }
    }
    // Animated NPC, original shadow flag and renderer-hidden exclusion.
    auto& bank=objects.models[0x110];bank.animated=true;bank.animation_count=1;ObjectMesh mesh;
    mesh.vertices=pose.vertices;mesh.radius=3;
    for(const auto& f:clip.faces){ObjectFace face;face.vertices={f.vertices[0],f.vertices[1],f.vertices[2],f.vertices[3]};face.quad=f.quad;face.colors=f.colors;mesh.faces.push_back(face);}
    clip.faces.clear(); // This fixture measures only the NPC, with no player mesh.
    bank.meshes.push_back(mesh);ObjectAnimation a;ObjectFrame f;f.vertices=pose.vertices;f.ticks=4;a.radius=3;a.frames.push_back(f);bank.animations.push_back(a);
    RetailObject npc;npc.type=0x110;npc.record[0x1f]=0x80;npc.position=npc.previous_position={0,.3f,0};npc.size=32;
    npc.animation=npc.previous_animation=start_object_animation(bank);
    npc.rotation[2]=npc.previous_rotation[2]=retail::original_heading(0)/16;
    r.upload_objects(objects);auto level=floor(false,false);r.upload_level(level);
    r.draw(level,scene,{4,6,-7},{0,0,0},nullptr,1,false,model,{},0,false,false,.9f,~0u,objects,std::span(&npc,1));
    check(r.rendered_character_shadows()==1 && r.rendered_shadow_triangles()==5,"Animated NPC silhouette missing");
    r.save_bmp(out/"gpu-npc-on.bmp");npc.hidden=true;
    r.draw(level,scene,{4,6,-7},{0,0,0},nullptr,1,false,model,{},0,false,false,.9f,~0u,objects,std::span(&npc,1));
    check(r.rendered_character_shadows()==0,"Hidden NPC continued casting");
    std::cout<<"GPU shadow projection PASS: Spyro/NPC, animated silhouette, slope, edge/gap, union and hidden exclusion\n";
}
void retail_jars(const std::filesystem::path& root,const std::filesystem::path& out) {
    AssetStore store(root/"data/disc");WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");unsigned jars=0;
    for(unsigned slot=15;slot<=69;slot+=2) {
        const auto archive=wad.read(slot);auto level=decode_level(archive,slot);
        if(std::none_of(level.objects.begin(),level.objects.end(),[](const auto& o){return o.type==0x233;}))continue;
        auto models=decode_object_models(archive,exe);initialize_retail_objects(level,models);
        level.script_image=make_retail_script_image(wad,exe,slot);
        for(const auto& jar:level.objects)if(jar.type==0x233) {
            Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;Game g(level,scene,{},&models);
            // Keep Spyro near enough for the actual jar case to run, without
            // touching its damage spheres. No camera or dialogue changes.
            std::optional<RayHit> floor;
            for(unsigned side=0;side<16 && !floor;++side) {
                const float angle=side*2*3.14159265f/16;
                floor=g.level.raycast(jar.position+Vec3{std::sin(angle)*1.4f,2,-std::cos(angle)*1.4f},{0,-1,0},8);
                if(floor && std::abs(floor->point.y-jar.position.y)>1.5f)floor.reset();
            }
            check(floor.has_value(),"No support beside retail jar");
            g.player.position=g.player.previous_position=floor->point+Vec3{0,.02f,0};g.player.health=0;g.player.invulnerable_ticks=10000;
            ticks(g,8);check(g.overlay_scripts.faults==0,"Jar activation interpreter fault");
            auto fly=std::find_if(g.retail_objects.begin(),g.retail_objects.end(),[&](const auto& o){return o.type==0xd3 && o.state==2 && !o.collected && length(o.position-jar.position)<.3f;});
            check(fly!=g.retail_objects.end(),"Retail jar did not retain its state-2 life butterfly");
            const unsigned id=fly->id;const Vec3 start=fly->position;
            ticks(g,1900);fly=std::find_if(g.retail_objects.begin(),g.retail_objects.end(),[&](const auto& o){return o.id==id;});
            check(fly!=g.retail_objects.end() && fly->state==2 && !fly->collected && length(fly->position-start)<.002f,"Intact jar butterfly escaped/expired");
            // Other live level scripts can release unrelated food during a
            // 32-second wait (notably Magma Cone's neighbouring jars). The
            // invariant above checks this exact jar's child, not all food.
            g.player.health=0;const auto lives=g.player.lives;const auto eaten=g.sparx.butterflies_eaten();
            // Attack reaches the retail collision query, as ordinary charged
            // movement does. The jar's original code performs the release.
            // Some jars sit on lava ledges: hold the probe player beside the
            // target during the award check so unrelated falling deaths do
            // not cancel its life counter. Containment above runs normally.
            const auto hold=[&](unsigned count){for(unsigned t=0;t<count;++t){
                const auto& target=g.retail_objects[id];
                const auto nearby_position=target.collected?jar.position+Vec3{0,.1f,-.7f}:
                    target.position-retail::origin_above_feet+Vec3{0,0,-.15f};
                g.player.position=g.player.previous_position=nearby_position;
                g.player.velocity={};g.player.invulnerable_ticks=10000;g.advance(FixedClock::step,{});
            }};
            g.overlay_scripts.attack_query(jar.position+Vec3{0,.2f,0},.5f,charge_attack);hold(8);
            check(g.retail_objects[jar.id].state<0,"Normal attack did not break original jar");
            hold(240);
            if(g.sparx.butterflies_eaten()!=eaten+1 || g.player.health!=3 || g.player.lives!=lives+1) {
                std::cerr<<"Release diagnostic slot="<<slot<<" jar="<<jar.id<<" health="<<g.player.health<<" lives="<<g.player.lives<<" before="<<lives<<" eaten="<<g.sparx.butterflies_eaten()<<" starts="<<g.sparx.feeding_starts()<<" player="<<g.player.position.x<<','<<g.player.position.y<<','<<g.player.position.z<<'\n';
                for(const auto& o:g.retail_objects)if(o.type==0xd3)std::cerr<<" D3 id="<<o.id<<" state="<<int(o.state)<<" collected="<<o.collected<<" script="<<o.retail_script<<" source="<<o.source_id<<" pos="<<o.position.x<<','<<o.position.y<<','<<o.position.z<<'\n';
            }
            check(g.sparx.butterflies_eaten()==eaten+1 && g.player.health==3 && g.player.lives==lives+1,"Released life butterfly did not award exactly once");
            check(g.retail_objects[id].collected && !g.retail_objects[id].retail_script,"Released jar child was not consumed by the native feeding controller");
            check(g.overlay_scripts.faults==0,"Jar release interpreter fault");
            const auto after=g.retail_objects.size();hold(120);
            check(g.sparx.butterflies_eaten()==eaten+1 && g.player.lives==lives+1,"Released jar duplicated its award");
            (void)after;++jars;
            std::cout<<"Retail jar PASS slot="<<slot<<" record="<<jar.id<<" intact_vblanks=1908 release=charge health=3 life_awards=1 faults=0\n";
        }
    }
    check(jars==23,"Retail life-jar coverage changed");
    (void)out;
}
}
int main(int argc,char** argv) {
    try {
        const auto root=std::filesystem::path(argc>1?argv[1]:".");const auto out=root/"build/shadows-jars-20261105/evidence";
        std::filesystem::create_directories(out);gpu_checks(out);retail_jars(root,out);
        std::cout<<checks<<" character shadow / life jar checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
