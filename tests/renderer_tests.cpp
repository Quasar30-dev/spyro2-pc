#include "spyro/renderer.hpp"
#include "spyro/coordinates.hpp"
#include "spyro/runtime.hpp"
#include "spyro/spyro_dark.hpp"
#include "spyro/element_art.hpp"
#include "spyro/assets.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

using namespace spyro;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks=0;
void check(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
struct Capture {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("spyro-water-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bmp");
    ~Capture() {std::error_code error;std::filesystem::remove(path,error);}
};
void plane(Level& level,float z,unsigned material,float u=0.5f) {
    for(const auto p:std::array<Vec3,6>{{{-2,-2,z},{2,-2,z},{-2,2,z},{2,-2,z},{2,2,z},{-2,2,z}}})
        level.visual_vertices.push_back({p,{1,1,1},u,0.5f,material});
}
Level water(bool translucent,unsigned mode,std::array<std::uint8_t,4> pixel) {
    Level level;level.sky_color={51/255.0f,102/255.0f,153/255.0f};
    level.triangles.push_back({{-2,-2,2},{2,-2,2},{-2,2,2}});
    level.bounds.add({-2,-2,2});level.bounds.add({2,2,2});
    level.terrain_materials.push_back({translucent,mode});plane(level,2,0);
    level.texture_width=level.texture_height=1;level.texture_rgba.assign(pixel.begin(),pixel.end());return level;
}
void expect_pixel(Renderer& renderer,Level level,SpyroModel& model,SpyroPose pose,std::array<int,3> expected,const char* message) {
    renderer.upload_level(level);Scene scene;scene.spawn={0,-retail::spyro_origin_height,0};ObjectModels objects;
    renderer.draw(level,scene,{0,0,0},{0,0,1},nullptr,1,false,model,pose,0,false,false,1.0f,~0u,objects,{});
    Capture capture;renderer.save_bmp(capture.path);
    std::ifstream file(capture.path,std::ios::binary);file.seekg(54+(16*32+16)*4);
    std::array<unsigned char,4> pixel{};file.read(reinterpret_cast<char*>(pixel.data()),4);
    bool match=static_cast<bool>(file);
    for(unsigned c=0;c<3;++c)match=match && std::abs(static_cast<int>(pixel[2-c])-expected[c])<=1;
    if(!match)std::cerr<<message<<": got "<<int(pixel[2])<<','<<int(pixel[1])<<','<<int(pixel[0])<<'\n';
    check(match,message);
}
void object_blending(Renderer& renderer,SpyroModel& spyro) {
    Level level;level.sky_color={51/255.0f,102/255.0f,153/255.0f};
    // The renderer requires a level surface; keep it behind the camera so
    // the centre pixel measures only the object against the sky.
    level.triangles.push_back({{-2,-2,-2},{2,-2,-2},{-2,2,-2}});
    level.bounds.add({-2,-2,-2});level.bounds.add({2,2,-2});renderer.upload_level(level);
    ObjectModels models;models.texture_width=models.texture_height=1;
    ObjectModel bank;ObjectMesh mesh;mesh.radius=3;mesh.vertices={{-2,-2,2},{2,-2,2},{0,2,2}};
    ObjectFace face;face.vertices={0,1,2,2};face.colors.fill({1,1,1});face.uv.fill({0.5f,0.5f});
    face.textured=face.translucent=true;face.flags=1;mesh.faces.push_back(face);bank.meshes.push_back(mesh);models.models.emplace(1,bank);
    RetailObject instance;instance.type=1;
    instance.rotation[2]=instance.previous_rotation[2]=retail::original_heading(0)/16;Scene scene;
    const auto render=[&](std::array<std::uint8_t,4> pixel,unsigned mode,std::array<int,3> expected,const char* reason) {
        models.texture_rgba.assign(pixel.begin(),pixel.end());models.models[1].meshes[0].faces[0].blend_mode=mode;
        renderer.upload_objects(models);renderer.draw(level,scene,{0,0,0},{0,0,1},nullptr,1,false,spyro,{},0,false,false,1,~0u,models,std::span(&instance,1));
        Capture capture;renderer.save_bmp(capture.path);std::ifstream file(capture.path,std::ios::binary);file.seekg(54+(16*32+16)*4);
        std::array<unsigned char,4> result{};file.read(reinterpret_cast<char*>(result.data()),4);bool match=bool(file);
        for(unsigned c=0;c<3;++c)match=match && std::abs(int(result[2-c])-expected[c])<=1;
        if(!match)std::cerr<<reason<<": got "<<int(result[2])<<','<<int(result[1])<<','<<int(result[0])<<'\n';
        check(match,reason);
    };
    render({0,0,0,128},1,{51,102,153},"Black glow texel hid the scene instead of adding zero light");
    render({32,48,64,128},1,{83,150,217},"Original Moby additive glow equation failed");
    render({32,48,64,255},1,{32,48,64},"Opaque texel inside a translucent Moby lost its depth-writing pass");
    render({32,48,64,0},1,{51,102,153},"Transparent object texel was not discarded");
    render({32,48,64,128},0,{42,75,109},"Original Moby half blending equation failed");
    render({32,48,64,128},2,{19,54,89},"Original Moby subtractive blending equation failed");
    render({32,48,64,128},3,{59,114,169},"Original Moby quarter-additive blending equation failed");
}
void object_special_materials(Renderer& renderer,SpyroModel& spyro) {
    Level level;level.sky_color={0,0,0};level.triangles.push_back({{-2,-2,-2},{2,-2,-2},{0,2,-2}});
    level.bounds.add({-2,-2,-2});level.bounds.add({2,2,-2});renderer.upload_level(level);
    ObjectModels models;models.texture_width=models.texture_height=1;models.texture_rgba={32,64,96,255};models.environment_tile=0;
    ObjectModel bank;bank.animated=true;ObjectMesh mesh;mesh.vertices={{-2,-2,2},{2,-2,2},{0,2,2}};mesh.radius=4;
    ObjectFace face;face.animated=true;face.flags=0x28;face.palette_valid=false;face.shiny_normal={0,0,-100};face.vertices={0,1,2,2};
    face.colors.fill({128/255.0f,128/255.0f,128/255.0f});mesh.faces.push_back(face);bank.meshes.push_back(mesh);
    ObjectAnimation animation;animation.radius=4;ObjectFrame frame;frame.vertices=mesh.vertices;animation.frames.push_back(frame);
    bank.animations.push_back(animation);models.models.emplace(1,bank);RetailObject instance;instance.type=1;instance.material_color=0x2a505050;
    instance.animation=instance.previous_animation=start_object_animation(bank);instance.rotation[2]=instance.previous_rotation[2]=retail::original_heading(0)/16;
    renderer.upload_objects(models);Scene scene;
    const auto render=[&](std::array<int,3> expected,const char* reason) {
        renderer.draw(level,scene,{0,0,0},{0,0,1},nullptr,1,false,spyro,{},0,false,false,1,~0u,models,std::span(&instance,1));
        Capture capture;renderer.save_bmp(capture.path);std::ifstream file(capture.path,std::ios::binary);file.seekg(54+(16*32+16)*4);
        std::array<unsigned char,4> result{};file.read(reinterpret_cast<char*>(result.data()),4);bool match=bool(file);
        for(unsigned c=0;c<3;++c)match=match && std::abs(int(result[2-c])-expected[c])<=1;
        if(!match)std::cerr<<reason<<": got "<<int(result[2])<<','<<int(result[1])<<','<<int(result[0])<<'\n';check(match,reason);
    };
    render({156,156,156},"Normal payload was omitted or treated as palette indices");
    models.models[1].meshes[0].faces[0].palette_valid=true;instance.material_color=0x90000000;
    render({32,64,96},"Untextured reflection packet failed to sample the retail environment image");
    auto& test_bank=models.models[1];auto& test_mesh=test_bank.meshes[0];
    test_mesh.vertices={{-2,-2,3},{2,-2,3},{0,2,3},{-2,-2,2},{2,-2,2},{0,2,2}};
    face.palette_valid=true;face.flags=8;face.textured=face.translucent=true;face.blend_mode=0;face.colors.fill({1,1,1});face.uv.fill({0.25f,0.5f});
    test_mesh.faces={face};face.vertices={3,4,5,5};face.blend_mode=3;face.uv.fill({0.75f,0.5f});test_mesh.faces.push_back(face);
    test_bank.animations[0].frames[0].vertices=test_mesh.vertices;
    models.texture_width=2;models.texture_rgba={32,48,64,128,80,16,0,128};instance.material_color=0;
    level.sky_color={51/255.0f,102/255.0f,153/255.0f};renderer.upload_level(level);renderer.upload_objects(models);
    render({62,79,109},"Overlapping Moby blend modes did not sort in view depth");
    test_mesh.faces={face};level=water(true,0,{32,48,64,128});for(auto& v:level.visual_vertices)v.position.z=3;renderer.upload_level(level);
    render({62,79,109},"Foreground translucent object was incorrectly composited behind water");
    // Static class-104/229 front faces use material 2's second gold pair.
    // Exercise the actual GPU shiny pass rather than just the RGB helper.
    level.sky_color={0,0,0};level.visual_vertices.clear();level.triangles={{{-2,-2,-2},{2,-2,-2},{0,2,-2}}};renderer.upload_level(level);
    test_bank.animated=false;test_mesh.vertices={{-2,-2,6},{0,2,6},{2,-2,6}};test_mesh.radius=8;
    ObjectFace gold;gold.shiny=true;gold.flags=2;gold.vertices={0,1,2,2};gold.shiny_normal={0,0,-100};test_mesh.faces={gold};
    models.shiny_colors[2][2]={64/255.0f,64/255.0f,0};models.shiny_colors[2][3]={160/255.0f,160/255.0f,0};
    instance.material_color=2;renderer.upload_objects(models);
    render({111,111,0},"Gold number/letter front face was black on the GPU");
}
void destructive_terrain(Renderer& renderer,SpyroModel& spyro) {
    auto level=water(false,0,{20,40,60,255});level.wad_slot=17;level.spawn={-10,0,-10};
    for(unsigned i=0;i<level.visual_vertices.size();i+=3)
        level.triangles.push_back({level.visual_vertices[i].position,level.visual_vertices[i+1].position,level.visual_vertices[i+2].position});
    // A floor behind the wall makes this a real room, so a blast should open
    // a passage rather than seal an otherwise empty solid-terrain interior.
    level.triangles.push_back({{-10,-2,-10},{10,-2,10},{10,-2,-10}});
    level.triangles.push_back({{-10,-2,-10},{-10,-2,10},{10,-2,10}});
    Scene scene;scene.wad_slot=17;scene.spawn=level.spawn;Game game(level,scene);
    game.player.position={0,0,-2};game.destruction_settings.enabled=true;
    const auto center=[&]() {
        ObjectModels objects;renderer.draw(game.level,scene,{0,0,0},{0,0,1},nullptr,1,false,spyro,{},0,false,false,1,~0u,objects,{});
        Capture capture;renderer.save_bmp(capture.path);std::ifstream file(capture.path,std::ios::binary);file.seekg(54+(16*32+16)*4);
        std::array<unsigned char,4> p{};file.read(reinterpret_cast<char*>(p.data()),4);check(bool(file),"Could not read destruction render");
        return std::array<int,3>{p[2],p[1],p[0]};
    };
    renderer.upload_level(game.level);check(center()==std::array<int,3>{20,40,60},"Destruction render has no source wall");
    // Topology changes must retain the resident atlas rather than recreating
    // immutable textures and sky resources on an impact or restoration.
    game.level.texture_rgba.clear();
    check(game.destruction.impact(game,{0,0,2},{0,0,-1},TerrainAttack::Superflame,nullptr),"GPU fixture did not destroy terrain");
    check(center()==std::array<int,3>{51,102,153},"Blasted wall remains visible on the GPU");
    game.destruction.clear(game.level);check(center()==std::array<int,3>{20,40,60},"Restored wall did not return to the GPU");
    // A same-size geometry revision must also refresh interpolated UVs.
    game.level.texture_width=2;game.level.texture_rgba={20,40,60,255,90,110,130,255};
    for(auto& v:game.level.visual_vertices)v.u=.25f;renderer.upload_level(game.level);check(center()==std::array<int,3>{20,40,60},"UV refresh fixture did not start on the first atlas texel");
    for(auto& v:game.level.visual_vertices)v.u=.75f;++game.level.terrain_revision;
    check(center()==std::array<int,3>{90,110,130},"Dynamic terrain retained stale texture coordinates");
}
void moving_terrain(Renderer& renderer,SpyroModel& spyro) {
    Scene scene;ObjectModels objects;
    const auto pixel=[&](const Level& level,bool collision=false) {
        renderer.draw(level,scene,{0,0,0},{0,0,1},nullptr,1,false,spyro,{},0,false,collision,1,~0u,objects,{});
        Capture capture;renderer.save_bmp(capture.path);std::ifstream file(capture.path,std::ios::binary);file.seekg(54+(16*32+16)*4);
        std::array<unsigned char,4> result{};file.read(reinterpret_cast<char*>(result.data()),4);
        check(bool(file),"Could not read moving-terrain capture");
        return std::array<int,3>{result[2],result[1],result[0]};
    };
    auto level=water(false,0,{20,40,60,255});
    level.triangles={{{-3,-2,2},{3,-2,2},{0,4,2}}};
    const auto source=level;renderer.upload_level(level);
    const auto surface=pixel(level),collider=pixel(level,true);
    check(surface==std::array<int,3>{20,40,60},"Moving-terrain fixture has no visible surface");
    check(collider!=std::array<int,3>{51,102,153},"Moving-terrain fixture has no visible collider");
    for(auto& v:level.visual_vertices)v.position.z=-2;
    for(auto& t:level.triangles){t.a.z=t.b.z=t.c.z=-2;}
    ++level.terrain_revision;
    check(pixel(level)==std::array<int,3>{51,102,153},"Terrain vertex changes did not reach the GPU");
    check(pixel(level,true)==std::array<int,3>{51,102,153},"Terrain collider changes did not reach the GPU");
    check(pixel(source)==surface && pixel(source,true)==collider,"Returning to the source/editor retained moved terrain");
    check(pixel(level)==std::array<int,3>{51,102,153},"Returning to gameplay lost its moved terrain");

    level=water(true,0,{32,48,64,128});level.visual_vertices.clear();
    level.terrain_materials.push_back({true,3});level.texture_width=2;
    level.texture_rgba={32,48,64,128,80,16,0,128};
    plane(level,2,1,0.75f);plane(level,3,0,0.25f);renderer.upload_level(level);
    const auto before=pixel(level);
    for(unsigned i=0;i<6;++i)level.visual_vertices[i].position.z=4;
    ++level.terrain_revision;
    const auto after=pixel(level);
    check(std::abs(before[0]-62)<=1 && std::abs(before[1]-79)<=1 && std::abs(before[2]-109)<=1,
          "Moving transparent terrain fixture did not blend correctly");
    check(std::abs(after[0]-52)<=1 && std::abs(after[1]-77)<=1 && std::abs(after[2]-109)<=1,
          "Moved translucent terrain kept stale positions or sort depth");
}
void sky_and_quads(Renderer& renderer,SpyroModel& spyro) {
    auto level=water(false,0,{20,40,60,255});
    for(auto& v:level.visual_vertices)v.position.z=-2;
    for(const auto p:std::array<Vec3,6>{{{-2,-2,1},{2,-2,1},{-2,2,1},{2,-2,1},{2,2,1},{-2,2,1}}})
        level.sky_vertices.push_back({p,{0.8f,0.2f,0.1f}});
    level.sky_regions.push_back({0,6});renderer.upload_level(level);Scene scene;ObjectModels objects;
    const auto render=[&](Vec3 eye,Vec3 forward,std::array<int,3> expected,const char* why,bool studio=false) {
        renderer.draw(level,scene,eye,eye+forward,nullptr,1,false,spyro,{},0,studio,false,1,~0u,objects,{});
        Capture capture;renderer.save_bmp(capture.path);std::ifstream file(capture.path,std::ios::binary);file.seekg(54+(16*32+16)*4);
        std::array<unsigned char,4> result{};file.read(reinterpret_cast<char*>(result.data()),4);bool match=bool(file);
        for(unsigned c=0;c<3;++c)match=match && std::abs(int(result[2-c])-expected[c])<=1;
        check(match,why);
    };
    render({0,0,0},{0,0,1},{204,51,26},"Original sky colours did not render");
    render({100,80,300},{0,0,1},{204,51,26},"Sky translated with the player");
    render({100,80,300},{0,0,-1},{51,102,153},"Sky failed to rotate with the camera");
    for(auto& v:level.visual_vertices)v.position.z=20;
    // Make a distant world surface cover the centre. The sky is only one
    // unit away in its own direction space, but must never occlude the world.
    for(auto& v:level.visual_vertices){v.position.x*=20;v.position.y*=20;}
    renderer.upload_level(level);render({0,0,0},{0,0,1},{20,40,60},"Sky wrote foreground depth");
    render({0,2,0},{0,0,1},{8,10,17},"Sky leaked into the model studio",true);
    check(renderer.rendered_sky_triangles()==0,"Studio retained sky draw count");
    for(auto& v:level.visual_vertices)v.position.z=-20;
    level.sky_visibility={3,0,0};renderer.upload_level(level);
    render({0,0,0},{0,0,1},{51,102,153},"Sky ignored its authored visibility mask");
    level.sky_visibility.clear();level.sky_vertices.clear();level.sky_regions.clear();renderer.upload_level(level);
    ObjectModel bank;ObjectMesh mesh;mesh.radius=3;mesh.vertices={{-2,-2,2},{2,-2,2},{-2,2,2},{2,2,2}};
    ObjectFace face;face.quad=true;face.vertices={0,1,2,3};face.colors.fill({1,0,0});mesh.faces.push_back(face);bank.meshes.push_back(mesh);objects.models.emplace(0xc8,bank);
    RetailObject basket;basket.type=0xc8;basket.rotation[2]=basket.previous_rotation[2]=retail::original_heading(0)/16;
    renderer.upload_objects(objects);
    renderer.draw(level,scene,{0,0,0},{0,0,1},nullptr,1,false,spyro,{},0,false,false,1,~0u,objects,std::span(&basket,1));
    Capture capture;renderer.save_bmp(capture.path);std::ifstream file(capture.path,std::ios::binary);file.seekg(54+(16*32+16)*4);
    std::array<unsigned char,4> result{};file.read(reinterpret_cast<char*>(result.data()),4);
    check(result[2]==255 && result[1]==0 && result[0]==0,"Plain basket quad still renders inside out on the GPU");
}
}
std::vector<std::array<int,3>> frame_pixels(Renderer& renderer,unsigned w,unsigned h) {
    Capture capture;renderer.save_bmp(capture.path);std::ifstream file(capture.path,std::ios::binary);file.seekg(54);
    std::vector<std::array<int,3>> out(std::size_t(w)*h);std::vector<unsigned char> row(std::size_t(w)*4);
    for(unsigned y=0;y<h;++y){file.read(reinterpret_cast<char*>(row.data()),row.size());for(unsigned x=0;x<w;++x)out[y*w+x]={row[x*4+2],row[x*4+1],row[x*4]};}
    return out;
}
void dark_palette_rules() {
    // Every 15-bit colour: zero stays zero, STP survives, non-zero never becomes transparent.
    for(unsigned word=0;word<0x10000;++word) {
        const auto dark=dark_spyro_color(static_cast<std::uint16_t>(word));
        if(!word){check(!dark,"The transparent CLUT word must stay transparent");continue;}
        check((dark&0x8000)==(word&0x8000),"The STP bit must survive the Dark Spyro remap");
        check((dark&0x7fff) || (dark&0x8000),"An opaque texel became transparent");
    }
    const auto purple=dark_spyro_color(static_cast<std::uint16_t>((14)|(8<<5)|(22<<10)));
    const auto gold=dark_spyro_color(static_cast<std::uint16_t>((28)|(22<<5)|(8<<10)));
    check(((purple&31)+((purple>>5)&31)+((purple>>10)&31))<30,"Purple skin must become near-black violet");
    check(((gold&31)+((gold>>5)&31)+((gold>>10)&31))<30,"The gold belly must go black (kept parts use the retail atlas instead)");
    std::array<std::uint8_t,4> a{112,64,176,255},b=a;dark_spyro_texel(a.data());dark_spyro_texel(b.data());
    check(a==b && a[3]==255,"The texel remap must depend on colour alone and keep alpha");
    const auto sprites=element_sprite_atlas();check(sprites.rgba.size()==320*32*4,"Element sprite atlas size");
    unsigned levels=0;std::array<bool,256> seen{};for(std::size_t i=0;i<sprites.rgba.size();i+=4)if(sprites.rgba[i+3] && !seen[sprites.rgba[i]]){seen[sprites.rgba[i]]=true;++levels;}
    check(levels<=15,"Element sprites must keep a 4bpp-style 16-level palette");
}
void retail_eyes(const std::filesystem::path& data) {
    AssetStore assets(data);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");unsigned banks=0;
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto model=decode_spyro(wad.read(0),wad.read(slot),exe,wad.read(29),29);
        const auto cell=spyro_eye_cell(model);check(cell.valid() && cell.u1>cell.u0 && cell.v1>cell.v0,"Eye cell missing in a retail bank");
        const auto& rest=model.animations.at(0);const auto eyes=spyro_eye_vertices(model,rest,230);
        check(eyes[0].size()==7 && eyes[1].size()==7,"Eye landmarks");
        unsigned eye_faces=0;
        for(const auto& face:rest.faces){bool all=true;for(unsigned k=0;k<(face.quad?4u:3u);++k)all&=std::find(eyes[0].begin(),eyes[0].end(),face.vertices[k])!=eyes[0].end() ||
            std::find(eyes[1].begin(),eyes[1].end(),face.vertices[k])!=eyes[1].end();if(all){++eye_faces;check(face.atlas_tile==cell.tile,"Eye landmark face does not sample the eye cell");}}
        check(eye_faces>=6,"Each retail bank must expose both eyes' faces");
        SpyroPose pose;pose.vertices=rest.frames.front().vertices;const auto centres=spyro_eye_centers(model,pose);
        check(centres && (*centres)[0].x*(*centres)[1].x<0,"Eye centres must sit on opposite sides of the head");
        for(const auto appearance:{SuperchargeAppearance::Retail,SuperchargeAppearance::MainModel})
            if(model.animations.contains(61)) {
                const auto& clip=model.clip(61,appearance);if(clip.frames.empty())continue;
                SpyroPose charged;charged.vertices=clip.frames.front().vertices;
                check(spyro_eye_centers(model,charged,&clip).has_value(),"Supercharge meshes must still resolve both eyes");
            }
        const auto keep=dark_spyro_keep(model);
        if(keep.faces!=63)std::cerr<<"bank "<<slot<<" kept faces "<<keep.faces<<std::endl;
        check(keep.faces==63,"Horns, crest, wings and tail tip must be classified in every bank");
        for(const auto& face:rest.faces)if(dark_spyro_kept(keep,face,230)) {
            Vec3 c{};for(unsigned k=0;k<(face.quad?4u:3u);++k)c+=rest.frames.front().vertices[face.vertices[k]];c=c/float(face.quad?4:3);
            check(!(c.y<-.15f && c.z>-.5f),"The belly must not keep its gold");
        }
        for(unsigned v:{54u,60u,67u,204u,211u,194u})check(keep.vertices[v],"Wing/horn/crest landmark vertex missing from the kept set");
        const auto dark=dark_spyro_atlas(model);check(dark.size()==model.texture_rgba.size(),"Dark atlas size");
        if(slot==17) {
            // Measured: kept parts (wings, horns, crest, tail tip) sample the
            // retail atlas under the tint, so their hue survives; the rest of
            // the body samples the black atlas.
            const auto sample=[&](const Bytes& atlas,const SpyroFace& face){
                float u=0,v=0;const unsigned n=face.quad?4u:3u;for(unsigned k=0;k<n;++k){u+=face.uv[k][0];v+=face.uv[k][1];}
                const auto x=std::min(model.texture_width-1,unsigned(u/n*model.texture_width)),y=std::min(model.texture_height-1,unsigned(v/n*model.texture_height));
                const auto* t=&atlas[(std::size_t(y)*model.texture_width+x)*4];return Vec3{float(t[0]),float(t[1]),float(t[2])};};
            Vec3 kept_retail{},body_retail{},body_dark{};unsigned kept_n=0,body_n=0;
            for(const auto& face:rest.faces) {
                if(dark_spyro_kept(keep,face,230)){kept_retail+=sample(model.texture_rgba,face);++kept_n;}
                else {body_retail+=sample(model.texture_rgba,face);body_dark+=sample(dark,face);++body_n;}
            }
            kept_retail=kept_retail/float(kept_n);body_retail=body_retail/float(body_n);body_dark=body_dark/float(body_n);
            const Vec3 kept_dark{kept_retail.x*dark_spyro_keep_tint.x,kept_retail.y*dark_spyro_keep_tint.y,kept_retail.z*dark_spyro_keep_tint.z};
            const auto lum=[](Vec3 c){return c.x*.30f+c.y*.59f+c.z*.11f;};
            const auto hue=[](Vec3 c){return std::atan2(std::sqrt(3.0f)*(c.y-c.z),2*c.x-c.y-c.z)*180/std::numbers::pi_v<float>;};
            std::cerr<<"Dark Spyro (bank 17): kept wings/horns/crest/tail "<<kept_n<<" faces, retail avg "<<kept_retail.x<<","<<kept_retail.y<<","<<kept_retail.z
                     <<" -> dark "<<kept_dark.x<<","<<kept_dark.y<<","<<kept_dark.z<<" (hue "<<hue(kept_retail)<<" -> "<<hue(kept_dark)<<" deg, brightness "
                     <<lum(kept_dark)/lum(kept_retail)*100<<"%); body "<<body_n<<" faces "<<lum(body_retail)<<" -> "<<lum(body_dark)<<" luminance\n";
            check(std::abs(hue(kept_dark)-hue(kept_retail))<20,"Dark Spyro's wings, horns and spikes must keep their retail hue");
            check(lum(kept_dark)>lum(kept_retail)*.6f && lum(kept_dark)<lum(kept_retail)*.9f,"Kept parts must take a dark tint, not full brightness or black");
            check(lum(body_dark)<lum(kept_dark)*.6f,"The rest of Dark Spyro must be far darker than the kept parts");
        }
        for(std::size_t i=3;i<dark.size();i+=4)if(dark[i]!=model.texture_rgba[i])throw std::runtime_error("Dark atlas changed transparency");
        ++banks;
    }
    check(banks==41,"All 41 banks checked");
}
void element_passes(Renderer& renderer) {
    // A Void Beam over a grey sky: subtractive darkness plus an additive core.
    Level level;level.sky_color={.4f,.4f,.4f};level.wad_slot=17;level.bounds={{-50,-50,-50},{50,50,50}};
    level.triangles.push_back({{-50,-5,-50},{50,-5,50},{50,-5,-50}});
    Scene scene;scene.wad_slot=17;scene.spawn={0,0,0};Game g(level,scene);
    g.upgrades.enabled=true;unlock_all_elements(g);g.upgrades.elements.equipped=Element::Convexity;g.player.yaw=g.player.previous_yaw=std::numbers::pi_v<float>/2;
    InputState press;press.flame_pressed=press.flame_held=true;InputState hold;hold.flame_held=true;
    g.advance(FixedClock::step,press);for(unsigned i=0;i<10;++i)g.advance(FixedClock::step,hold);
    check(g.elements.beam().active,"Void Beam must be active");
    renderer.resize(64,64);renderer.upload_level(level);SpyroModel model;model.animations.emplace(0,SpyroAnimation{});
    model.texture_width=model.texture_height=1;model.texture_rgba={255,255,255,255};renderer.upload_spyro(model);
    renderer.element_game=&g;ObjectModels objects;
    const auto mid=(g.elements.beam().from+g.elements.beam().to)*.5f;
    renderer.draw(level,scene,mid+Vec3{0,0,-3},mid,&g.player,1,false,model,{},0,false,false,1.0f,~0u,objects,{});
    const auto pixels=frame_pixels(renderer,64,64);int darkest=255,brightest=0;
    for(const auto& p:pixels){darkest=std::min(darkest,p[0]+p[1]+p[2]);brightest=std::max(brightest,p[0]+p[1]+p[2]);}
    check(darkest<3*102-120,"The subtractive void ribbon must darken the scene");
    check(brightest>3*102+120,"The additive beam core must brighten the scene");
    check(renderer.rendered_element_effects()>0,"Element effects were not counted");
    // Electricity: bolts brighten, never darken.
    g.upgrades.elements.equipped=Element::Electric;g.advance(FixedClock::step,{});for(unsigned i=0;i<40;++i)g.advance(FixedClock::step,{});
    g.advance(FixedClock::step,press);
    const auto fresh=[&]{for(const auto& b:g.elements.bolts())if(b.age<=1)return &b;return static_cast<const ElementBolt*>(nullptr);};
    for(unsigned i=0;i<24 && !fresh();++i)g.advance(FixedClock::step,hold);
    check(fresh()!=nullptr,"Arc breath must create bolts");
    const auto& points=fresh()->points;const auto bolt=(points.front()+points.back())*.5f;
    const auto side=normalized(cross(points.back()-points.front(),Vec3{0,1,0}));
    renderer.draw(level,scene,bolt+side*2.5f,bolt,&g.player,0,false,model,{},0,false,false,1.0f,~0u,objects,{});
    const auto lit=frame_pixels(renderer,64,64);int brightest_bolt=0,dimmest=765;
    for(const auto& p:lit){brightest_bolt=std::max(brightest_bolt,p[0]+p[1]+p[2]);dimmest=std::min(dimmest,p[0]+p[1]+p[2]);}
    if(brightest_bolt<=3*102+150)std::cerr<<"bolt brightest "<<brightest_bolt<<" points "<<points.size()<<'\n';
    check(brightest_bolt>3*102+150,"Electric bolts must render additively");
    check(dimmest>=3*102-6,"Electric bolts must never darken the scene");
    renderer.element_game=nullptr;renderer.resize(32,32);
}
void dark_spyro_render(Renderer& renderer,const std::filesystem::path& data) {
    AssetStore assets(data);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
    const auto archive=wad.read(17);auto model=decode_spyro(wad.read(0),archive,exe,wad.read(29),29);
    Level level;level.sky_color={.2f,.2f,.25f};level.wad_slot=17;level.bounds={{-50,-50,-50},{50,50,50}};
    level.triangles.push_back({{-50,-5,-50},{50,-5,50},{50,-5,-50}});
    Scene scene;scene.wad_slot=17;scene.spawn={0,0,0};
    renderer.resize(128,128);renderer.upload_level(level);renderer.upload_spyro(model);ObjectModels objects;
    const auto pose=model.frame(0,0);
    const auto draw=[&](Game& g){renderer.element_game=&g;
        renderer.draw(level,scene,g.player.position+retail::origin_above_feet+Vec3{0,.12f,1.1f},g.player.position+retail::origin_above_feet+Vec3{0,.08f,0},&g.player,1,false,model,pose,0,false,false,.9f,~0u,objects,{});
        return frame_pixels(renderer,128,128);};
    Game normal(level,scene,{},nullptr,&model),dark(level,scene,{},nullptr,&model);dark.cheats.dark_spyro=true;
    for(auto* g:{&normal,&dark}){g->player.yaw=g->player.previous_yaw=0;for(unsigned i=0;i<8;++i)g->advance(FixedClock::step,{});}
    const auto a=draw(normal),b=draw(dark);
    check(renderer.rendered_dark_spyro() && renderer.rendered_eye_glows()==2,"Dark Spyro must render with both eye glows");
    long sum_a=0,sum_b=0;unsigned white=0;
    for(std::size_t i=0;i<a.size();++i){sum_a+=a[i][0]+a[i][1]+a[i][2];sum_b+=b[i][0]+b[i][1]+b[i][2];if(b[i][0]>225 && b[i][1]>225 && b[i][2]>225)++white;}
    check(white>=4,"Dark Spyro must have glowing white eyes");
    check(sum_b<sum_a,"Dark Spyro must be darker overall than the normal palette");
    renderer.element_game=nullptr;renderer.resize(32,32);
}
int main(int argc,char** argv) {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL feature;
        const auto result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context);
        check(SUCCEEDED(result),"Could not initialize D3D11 WARP");
        Renderer renderer;renderer.initialize(device.Get(),context.Get());renderer.resize(32,32);
        SpyroModel model;model.animations.emplace(0,SpyroAnimation{});
        model.texture_width=model.texture_height=1;model.texture_rgba={255,255,255,255};renderer.upload_spyro(model);
        expect_pixel(renderer,water(true,1,{0,0,0,128}),model,{}, {51,102,153},"Black STP water must preserve the background");
        expect_pixel(renderer,water(false,1,{0,0,0,128}),model,{}, {0,0,0},"Opaque primitives must ignore the CLUT STP flag");
        expect_pixel(renderer,water(true,1,{32,48,64,0}),model,{}, {51,102,153},"Color-zero texels must be discarded");
        expect_pixel(renderer,water(true,1,{32,48,64,255}),model,{}, {32,48,64},"Opaque texels in a translucent material need an opaque pass");
        constexpr std::array<std::array<int,3>,4> blended={{{42,75,109},{83,150,217},{19,54,89},{59,114,169}}};
        for(unsigned mode=0;mode<4;++mode)
            expect_pixel(renderer,water(true,mode,{32,48,64,128}),model,{},blended[mode],"PSX terrain blend equation mismatch");
        SpyroFace face;face.vertices={0,1,2,2};face.colors.fill({40/255.0f,80/255.0f,120/255.0f});
        face.uv.fill({0.5f,0.5f});model.animations[0].faces.push_back(face);
        SpyroPose submerged;submerged.vertices={{-2,-2,3},{2,-2,3},{0,2,3}};
        expect_pixel(renderer,water(true,1,{32,48,64,128}),model,submerged,{72,128,184},"Water must blend over submerged Spyro without hiding him");
        auto above=submerged;for(auto& v:above.vertices)v.z=1;
        expect_pixel(renderer,water(true,1,{32,48,64,128}),model,above,{40,80,120},"Water must depth-test behind foreground Spyro");
        model.animations[0].faces.clear();
        auto layered=water(true,0,{32,48,64,128});layered.visual_vertices.clear();
        layered.terrain_materials.push_back({true,3});layered.texture_width=2;
        layered.texture_rgba={32,48,64,128,80,16,0,128};
        plane(layered,2,1,0.75f);plane(layered,3,0,0.25f);
        expect_pixel(renderer,layered,model,{}, {62,79,109},"Transparent terrain must compose from back to front");
        check(renderer.rendered_transparent_terrain_triangles()==4,"Transparent terrain draw accounting mismatch");
        object_blending(renderer,model);
        object_special_materials(renderer,model);
        sky_and_quads(renderer,model);
        moving_terrain(renderer,model);
        destructive_terrain(renderer,model);
        dark_palette_rules();element_passes(renderer);
        if(argc>1 && std::filesystem::exists(std::filesystem::path(argv[1])/"SCUS_944.25")){retail_eyes(argv[1]);dark_spyro_render(renderer,argv[1]);}
        std::cout<<checks<<" D3D11 renderer checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
