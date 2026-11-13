#include "spyro/custom_level.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>
#include <iomanip>
#include <map>

namespace spyro {
namespace {
void require(bool ok,const char* text){if(!ok)throw FormatError(text);}
float scalar(std::istream& in,float limit=4096) {
    float n{};require(bool(in>>n) && std::isfinite(n) && std::abs(n)<=limit,"Invalid custom world numeric value");return n;
}
Vec3 point(std::istream& in){return {scalar(in),scalar(in),scalar(in)};}
unsigned count(std::istream& in,unsigned max) {
    unsigned n{};require(bool(in>>n) && n<=max,"Invalid custom world count/index");return n;
}
struct Tile {int slot{};unsigned tile{};bool water{};};
unsigned header(std::istream& in) {
    std::string magic;unsigned version{};
    require(bool(in>>magic>>version) && magic=="S2WORLD" && version>=1 && version<=4,"Unsupported custom world format");return version;
}
unsigned bank(std::istream& in,unsigned version) {
    const unsigned slot=version==1?17:count(in,95);
    require(slot==15 || slot==17,"Unsupported custom world actor bank");return slot;
}
std::string text(std::istream& in,std::size_t limit) {
    std::string value;require(bool(in>>std::quoted(value)) && !value.empty() && value.size()<=limit,"Invalid custom world text");return value;
}
// An original 64x64 RGBA tile shipped beside the world, named relative to it.
Bytes authored_tile(const std::filesystem::path& world,std::istream& in) {
    const auto file=std::filesystem::path(text(in,256));
    require(file.is_relative() && file.extension()==".rgba" &&
        std::none_of(file.begin(),file.end(),[](const auto& part){return part==".." || part==".";}),"Custom world texture path must stay beside the world");
    const auto full=world.parent_path()/file;
    require(std::filesystem::is_regular_file(full) && std::filesystem::file_size(full)==64*64*4,"Custom world texture must be a 64x64 RGBA file");
    std::ifstream image(full,std::ios::binary);Bytes pixels(64*64*4);
    require(bool(image.read(reinterpret_cast<char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()))),"Custom world texture could not be read");
    return pixels;
}
// Any retail gameplay scene may lend terrain tiles or actors (WAD 15-73, odd).
bool retail_scene(int slot){return slot>=15 && slot<=73 && (slot&1);}
// Version 4: the actor classes borrowed from other retail scenes, listed up
// front so the loader can import their models before the world is read.
std::vector<std::pair<unsigned,unsigned>> imports(std::istream& in,unsigned version) {
    std::vector<std::pair<unsigned,unsigned>> result;
    if(version<4)return result;
    std::string magic;require(bool(in>>magic) && magic=="IMPORTS","Missing custom world import list");
    const auto n=count(in,64);
    for(unsigned i=0;i<n;++i) {
        const auto slot=count(in,95),type=count(in,65535);
        require(retail_scene(int(slot)),"Custom world imports from an unsupported scene");result.push_back({slot,type});
    }
    return result;
}
}
#include "custom_arena.inl"
#include "custom_tree.inl"
#include "custom_homeworld.inl"
unsigned custom_level_bank(const std::filesystem::path& path) {
    std::ifstream in(path);const auto version=header(in);return bank(in,version);
}
std::vector<std::pair<unsigned,unsigned>> custom_level_imports(const std::filesystem::path& path) {
    std::ifstream in(path);const auto version=header(in);bank(in,version);return imports(in,version);
}
void import_custom_level_models(const std::filesystem::path& path,WadArchive& wad,std::span<const std::uint8_t> executable,ObjectModels& models) {
    std::map<unsigned,std::vector<unsigned>> wanted;
    for(const auto& [slot,type]:custom_level_imports(path))if(!models.models.contains(type))wanted[slot].push_back(type);
    for(const auto& [slot,types]:wanted) {
        const auto donor=decode_object_models(wad.read(slot),executable);
        for(const auto type:types)require(donor.models.contains(type),"Imported custom world actor has no model in its scene");
        import_object_models(models,donor,types);
    }
}
Level load_custom_level(const std::filesystem::path& path,WadArchive& wad,const Level& source,
                        const ObjectModels& models,const ReturnPortalAssets& portal) {
    require(std::filesystem::file_size(path)<=64*1024*1024,"Custom world exceeds the 64 MiB input limit");
    std::ifstream in(path);const auto version=header(in),source_slot=bank(in,version);
    require(source.wad_slot==source_slot,"Custom world loaded with the wrong actor bank");
    const auto imported=imports(in,version);
    Level level;level.wad_slot=source_slot;level.campaign_scene=source.campaign_scene;level.custom_path=path;
    require(bool(in>>std::quoted(level.custom_name)) && !level.custom_name.empty() && level.custom_name.size()<=128,"Invalid custom world title");
    level.spawn=point(in);level.spawn_yaw=scalar(in,7);level.sky_color=point(in);
    level.surface_swimming=level.underwater_swimming=true;
    if(version>=2){level.sky_vertices=source.sky_vertices;level.sky_regions=source.sky_regions;}
    const auto nm=count(in,version>=4?128:64);require(nm>0,"Custom world has no materials");
    std::vector<Tile> tiles;std::map<int,Level> sources;
    level.texture_width=1024;level.texture_height=((nm+15)/16)*64;
    level.texture_rgba.resize(level.texture_width*level.texture_height*4,255);
    for(unsigned m=0;m<nm;++m) {
        Tile t;require(bool(in>>t.slot) && (t.slot==-1 || t.slot==15 || t.slot==17 || t.slot==33 || t.slot==59 || t.slot==71 || t.slot==73 ||
            (version>=4 && (retail_scene(t.slot) || t.slot==-2))),"Unsupported custom world material bank");
        if(t.slot==-2) {
            // Version 4 authored tile: an original 64x64 RGBA texture shipped
            // beside the world (e.g. textures/blossom-meadow.rgba), painted with
            // the retail tiles as reference. Opaque, never water.
            const auto pixels=authored_tile(path,in);
            t.water=count(in,1)!=0;require(!t.water,"Custom world authored tiles cannot be water");
            tiles.push_back(t);level.terrain_materials.push_back({false,0});
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                const auto a=(y*64+x)*4,b=((m/16*64+y)*level.texture_width+m%16*64+x)*4;
                std::copy_n(pixels.data()+a,3,level.texture_rgba.data()+b);level.texture_rgba[b+3]=255;
            }
            continue;
        }
        t.tile=count(in,4095);t.water=count(in,1)!=0;tiles.push_back(t);level.terrain_materials.push_back({t.water,0});
        if(t.slot<0)continue;
        if(!sources.contains(t.slot))sources.emplace(t.slot,decode_level(wad.read(t.slot),t.slot));
        const auto& src=sources.at(t.slot);const unsigned sx=t.tile%16*64,sy=t.tile/16*64;
        if(version>=2 && t.water) {
            require(t.tile<src.terrain_materials.size() && src.terrain_materials[t.tile].translucent,"Custom water must reference a translucent retail material");
            level.terrain_materials[m]=src.terrain_materials[t.tile];
        }
        require(sx+64<=src.texture_width && sy+64<=src.texture_height,"Custom world texture tile is outside its retail atlas");
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            const auto a=((sy+y)*src.texture_width+sx+x)*4,b=((m/16*64+y)*level.texture_width+m%16*64+x)*4;
            std::copy_n(src.texture_rgba.data()+a,4,level.texture_rgba.data()+b);
            // Translucent water uses the renderer's PS1 semitransparency pass.
            if(version==1)level.texture_rgba[b+3]=t.water?128:255;
        }
    }
    const auto nt=count(in,200000);require(nt>0,"Custom world has no geometry");
    for(unsigned i=0;i<nt;++i) {
        const auto flags=count(in,7),mat=count(in,nm-1);std::array<TerrainVertex,3> v;
        for(auto& a:v) {
            a.position=point(in);a.color=point(in);const auto u=scalar(in,1),w=scalar(in,1);
            require(u>=0 && w>=0,"Custom world UV is outside its tile");
            // Retail vertex colours run to 2x (PS1 128 = neutral); authored
            // surfaces continuing retail ones need the same range.
            require(a.color.x>=0 && a.color.y>=0 && a.color.z>=0 && a.color.x<=2 && a.color.y<=2 && a.color.z<=2,"Custom world colour is out of range");
            a.u=(mat%16*64+u*64)/level.texture_width;
            a.v=(mat/16*64+(1-w)*64)/level.texture_height;a.material=mat;
            level.bounds.add(a.position);
        }
        require(length(cross(v[1].position-v[0].position,v[2].position-v[0].position))>1e-8f,"Degenerate custom world triangle");
        if(flags&1)level.visual_vertices.insert(level.visual_vertices.end(),v.begin(),v.end());
        if(flags&6) {
            level.triangles.push_back({v[0].position,v[1].position,v[2].position,static_cast<unsigned>(level.triangles.size()),bool(flags&4)});
            if(flags&4)++level.water_triangles;
        }
    }
    level.terrain_faces=static_cast<unsigned>(level.visual_vertices.size()/3);level.terrain_regions=1;
    const auto no=count(in,10000);
    for(unsigned i=0;i<no;++i) {
        // Version 4 names each actor's scene and may override its spirit
        // value (record +83, counted by power-up gates) and, for a gate, the
        // spirits it requires. 255 / 1000 keep the retail values.
        const auto slot=version>=4?count(in,95):source_slot;
        const auto type=count(in,65535),record=count(in,100000);const auto pos=point(in);const auto yaw=scalar(in,7);const auto gem=count(in,25);
        const auto spirit=version>=4?count(in,255):255u,requirement=version>=4?count(in,1000):1000u;
        const bool classic=type==1 || type==0xc8 || type==0x22b || type==0x1f0 || type==0x1aa;
        const bool home=version>=2 && source_slot==15 && (type==0xc9 || type==0x12c || type==0x110 || type==0x77 || type==0x1bc || type==0x16d || type==0x71 || type==0xd1);
        const bool borrowed=slot!=source_slot && std::find(imported.begin(),imported.end(),std::pair{slot,type})!=imported.end();
        require((slot==source_slot && (classic || home)) || borrowed,"Unsupported custom world actor class");
        if(slot!=source_slot && !sources.contains(int(slot)))sources.emplace(int(slot),decode_level(wad.read(slot),slot));
        const auto& donor=slot==source_slot?source.objects:sources.at(int(slot)).objects;
        const auto found=std::find_if(donor.begin(),donor.end(),[&](const auto& o){return o.id==record && o.type==type;});
        require(found!=donor.end() && models.models.contains(type),"Custom world actor reference is missing in the retail bank");
        auto o=*found;o.id=i;o.source_id=UINT32_MAX;o.record_offset=o.data_offset=0;
        if(spirit!=255){require(spirit<128,"Invalid custom spirit value");o.record[83]=static_cast<std::uint8_t>((o.record[83]&128)|spirit);}
        if(o.powerup_gate && requirement!=1000)o.powerup_gate->requirement=requirement;
        // The donor gate's spirit routes are waypoints in its own level; moved
        // into a custom world, its spirits fly straight to it instead.
        if(o.powerup_gate)o.powerup_gate->spirit_routes.clear();
        o.position=o.previous_position=pos;o.rotation=o.previous_rotation={0,0,retail::original_heading(yaw)/16};
        o.collected=o.runtime_spawned=o.audio_finished=o.hidden=o.scripted_contact=o.scripted_transient=false;
        o.state=0;o.attack_flags=0;o.interpolate_position=false;o.held_gem=static_cast<std::uint8_t>(gem);
        require(gem==1 || gem==2 || gem==5 || gem==10 || gem==25,"Invalid custom world gem denomination");
        o.gem_value=gem;o.record[60]=o.record[61]=o.record[62]=o.record[63]=0;
        if(models.models.at(type).animated)o.model=0;
        if(o.enemy){auto e=*o.enemy;o.enemy=EnemyState{};o.enemy->kind=e.kind;o.enemy->notice=e.notice;o.enemy->reach=e.reach;}
        if(o.container){o.container->rest_position=pos;o.container->rest_rotation=o.rotation;o.container->heat=o.container->wobble_age=0;}
        if(type==0x1aa) {
            o.state=2;o.draw_flags=0x20;o.material_color=0;o.power_target.reset();
            CampaignPortal p;p.index=static_cast<unsigned>(level.portals.size());p.object=i;p.destination=15;p.scene_id=10;
            p.enabled=true;p.return_home=true;p.two_sided=true;p.opening=p.rise=1;p.picture=portal.picture;
            for(unsigned k=0;k<5;++k)p.aperture[k]=pos+retail::rotate_model(portal.aperture[k],yaw);
            level.portals.push_back(std::move(p));
        }
        level.objects.push_back(std::move(o));
    }
    // The companion is driven by its existing runtime, not a static decoration.
    const auto sparx=std::find_if(source.objects.begin(),source.objects.end(),[](const auto& o){return o.type==0x78;});
    if(sparx!=source.objects.end()){auto o=*sparx;o.id=static_cast<unsigned>(level.objects.size());o.position=o.previous_position=level.spawn;o.source_id=UINT32_MAX;level.objects.push_back(o);}
    const auto nr=count(in,4096);for(unsigned i=0;i<nr;++i)level.custom_route.push_back(point(in));
    if(version>=2) {
        // Trees stay references to the mounted retail terrain. Their mesh and
        // textures are never copied into the authored interchange file.
        const auto np=count(in,256);
        for(unsigned i=0;i<np;++i) {
            const auto slot=count(in,95),region=count(in,1023);const auto pos=point(in);const auto yaw=scalar(in,7),scale=scalar(in,4);
            require(slot==15 && scale>=.25f,"Invalid retail tree placement");
            require(region==21 || region==25 || region==26 || region==32 || region==107,"Unsupported Summer Forest tree region");
            if(!sources.contains(slot))sources.emplace(slot,decode_level(wad.read(slot),slot));
            const auto& src=sources.at(slot);Bounds b;unsigned vertices=0;
            for(const auto& v:src.visual_vertices)if(v.source_region==region){b.add(v.position);++vertices;}
            require(vertices && vertices%3==0,"Missing retail tree region");
            const Vec3 origin{(b.min.x+b.max.x)/2,b.min.y,(b.min.z+b.max.z)/2};
            for(auto v:custom_tree::isolate(src,region)) {
                const auto found=std::find_if(tiles.begin(),tiles.end(),[&](auto t){return t.slot==int(slot) && t.tile==v.material;});
                require(found!=tiles.end(),"Retail tree material is missing from the custom atlas");
                const auto id=unsigned(found-tiles.begin());
                const float u=v.u*src.texture_width-(v.material%16)*64,w=v.v*src.texture_height-(v.material/16)*64;
                v.u=(id%16*64+u)/level.texture_width;v.v=(id/16*64+w)/level.texture_height;v.material=id;
                v.position=pos+retail::rotate_model((v.position-origin)*scale,yaw);
                v.source_region=v.source_vertex=v.source_color=UINT32_MAX;
                level.bounds.add(v.position);level.visual_vertices.push_back(v);
            }
        }
        const auto doors=count(in,32);
        for(unsigned i=0;i<doors;++i) {
            // A door borrows a homeworld portal's destination and picture.
            // Version 4 may instead give the aperture itself (five points,
            // bottom corner -> up one side -> top -> other side -> bottom
            // corner, wound toward the front) so the picture fills an arch
            // already in the referenced retail scenery.
            // Mode 2 (world-to-world, plan H1) has a mode-1 aperture, then the
            // world it leads to (relative to data/custom-levels, no ..), the
            // expansion emblems that open it and its presentation flags.
            const auto reference=count(in,255);
            const auto mode=version>=4?count(in,2):0u;
            const auto found=std::find_if(source.portals.begin(),source.portals.end(),[&](auto p){return p.index==reference;});
            require(found!=source.portals.end() && !found->return_home,"Missing homeworld portal reference");
            auto p=*found;
            bool two_sided=true;
            if(mode==0) {
                const auto pos=point(in);const auto yaw=scalar(in,7),scale=scalar(in,4);
                require(scale>=.5f,"Invalid custom portal scale");
                const auto origin=(p.aperture[0]+p.aperture[4])*.5f;
                auto right=normalized(Vec3{p.aperture[4].x-p.aperture[0].x,0,p.aperture[4].z-p.aperture[0].z});
                for(auto& v:p.aperture){const auto d=v-origin;v=pos+retail::rotate_model(Vec3{dot(d,right),d.y,0}*scale,yaw);}
            } else {
                two_sided=count(in,1)!=0;
                for(auto& v:p.aperture)v=point(in);
                const auto n=cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]);
                require(length(n)>0.05f && length(p.aperture[4]-p.aperture[0])>0.5f,"Invalid custom portal aperture");
            }
            if(mode==2) {
                const std::filesystem::path world=std::filesystem::u8path(text(in,240));
                require(!world.empty() && world.is_relative() && world.extension()==".s2world" &&
                    std::none_of(world.begin(),world.end(),[](const auto& part){return part=="..";}),"A world door needs a relative .s2world path without ..");
                p.world=world.generic_string();p.world_emblems=count(in,64);p.world_flags=count(in,3);
            }
            p.index=unsigned(level.portals.size());p.object=p.gate_object=p.gate_npc=p.purchase=UINT32_MAX;
            p.required_orbs=0;p.require_ripto=false;p.visibility=1;p.enabled=true;p.two_sided=two_sided;p.opening=p.rise=1;
            level.portals.push_back(std::move(p));
        }
        const auto conversations=count(in,32);
        for(unsigned i=0;i<conversations;++i) {
            const auto id=count(in,no?no-1:0);require(id<no,"Invalid custom conversation actor");
            const auto& o=level.objects[id];
            require(o.type==0x110 || o.type==0x77 || o.type==0x1bc || o.type==0x16d,"Custom conversation actor cannot speak");
            NpcDefinition npc;npc.object=id;npc.type=o.type;npc.name=text(in,64);
            for(unsigned j=0;j<2;++j){DialogueLine line;line.text=text(in,1024);line.formatted_text=line.text;npc.lines.push_back(std::move(line));}
            level.npcs.push_back(std::move(npc));
        }
        const auto goals=count(in,8);unsigned used=0;
        for(unsigned i=0;i<goals;++i) {
            CustomObjective goal;goal.bit=count(in,7);require(!(used&(1u<<goal.bit)),"Duplicate custom objective bit");used|=1u<<goal.bit;
            // Version 4: npc == actor count is a found orb with no speaker.
            goal.npc=count(in,version>=4?no:(no?no-1:0));goal.reward=count(in,no?no-1:0);goal.underwater=count(in,1)!=0;goal.title=text(in,128);
            require(goal.reward<no && level.objects[goal.reward].type==0x71,"Custom objective needs a retail orb reward");
            if(goal.npc==no)goal.npc=UINT32_MAX;
            if(version>=4)goal.radius=1.7f;
            else require(std::any_of(level.npcs.begin(),level.npcs.end(),[&](auto n){return n.object==goal.npc;}),"Custom objective needs a speaking character");
            const auto n=count(in,64);require(n>0,"Custom objective has no checkpoints");for(unsigned j=0;j<n;++j)goal.checkpoints.push_back(point(in));
            level.custom_objectives.push_back(std::move(goal));
        }
        level.terrain_faces=static_cast<unsigned>(level.visual_vertices.size()/3);
    }
    // Version 3 always carries an arena; from version 4 it is optional.
    if(version==3)load_custom_arena(in,level);
    else if(version>=4){in>>std::ws;if(in.peek()=='A')load_custom_arena(in,level);}
    in>>std::ws;
    if(in.peek()=='R') {
        // A retail scene's terrain, referenced from the mounted disc rather
        // than copied: its visual and collision triangles join the world,
        // minus any whose centroid lies in an authored exclusion box (for
        // example the intro's facade castle, replaced by an explorable one).
        std::string magic;unsigned retail_version{};
        // Version 2 also adopts the referenced scene's sky (dome and colour).
        require(bool(in>>magic>>retail_version) && magic=="RETAIL_TERRAIN" && retail_version>=1 && retail_version<=3,"Invalid retail terrain section");
        const auto slot=count(in,95);require(slot==73 || slot==15,"Unsupported retail terrain scene");
        // Version 2 always adopts the referenced scene's sky; version 3 says
        // whether to (0 keeps the actor bank's own sky, e.g. Summer Forest's).
        const bool adopt_sky=retail_version==2 || (retail_version>=3 && count(in,1)!=0);
        const auto boxes=count(in,64);std::vector<Bounds> excluded(boxes);
        for(auto& b:excluded){b.min=point(in);b.max=point(in);require(b.min.x<b.max.x && b.min.y<b.max.y && b.min.z<b.max.z,"Invalid retail exclusion box");}
        // Version 3: collision triangles whose centroid lies in these boxes
        // stop being solid (a pond surface the scene modelled as floor).
        std::vector<Bounds> liquid(retail_version>=3?count(in,64):0);
        for(auto& b:liquid){b.min=point(in);b.max=point(in);require(b.min.x<b.max.x && b.min.y<b.max.y && b.min.z<b.max.z,"Invalid retail liquid box");}
        const auto within=[](const std::vector<Bounds>& boxes,Vec3 p){return std::any_of(boxes.begin(),boxes.end(),[&](const Bounds& b){
            return p.x>=b.min.x && p.x<=b.max.x && p.y>=b.min.y && p.y<=b.max.y && p.z>=b.min.z && p.z<=b.max.z;});};
        const auto inside=[&](Vec3 p){return within(excluded,p);};
        if(!sources.contains(int(slot)))sources.emplace(int(slot),decode_level(wad.read(slot),slot));
        const auto& src=sources.at(int(slot));
        if(adopt_sky){level.sky_vertices=src.sky_vertices;level.sky_regions=src.sky_regions;level.sky_color=src.sky_color;}
        // Append the scene's tiles to the world atlas; earlier V coordinates
        // are rescaled to the taller atlas.
        std::map<unsigned,unsigned> remap;
        for(const auto& v:src.visual_vertices) {
            if(remap.contains(v.material))continue;
            const auto existing=std::find_if(tiles.begin(),tiles.end(),[&](auto t){return t.slot==int(slot) && t.tile==v.material;});
            if(existing!=tiles.end()){remap[v.material]=unsigned(existing-tiles.begin());continue;}
            remap[v.material]=unsigned(tiles.size());tiles.push_back({int(slot),v.material,false});
            level.terrain_materials.push_back(v.material<src.terrain_materials.size()?src.terrain_materials[v.material]:TerrainMaterial{});
        }
        require(tiles.size()<=256,"Retail terrain needs too many atlas tiles");
        const auto old_height=level.texture_height,new_height=static_cast<unsigned>((tiles.size()+15)/16*64);
        if(new_height!=old_height) {
            level.texture_rgba.resize(std::size_t(level.texture_width)*new_height*4,255);
            for(auto& v:level.visual_vertices)v.v*=float(old_height)/new_height;
            level.texture_height=new_height;
        }
        for(const auto& [tile,id]:remap) {
            const unsigned sx=tile%16*64,sy=tile/16*64;require(sx+64<=src.texture_width && sy+64<=src.texture_height,"Retail terrain tile outside its atlas");
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                const auto a=((sy+y)*src.texture_width+sx+x)*4,b=((id/16*64+y)*level.texture_width+id%16*64+x)*4;
                std::copy_n(src.texture_rgba.data()+a,4,level.texture_rgba.data()+b);
            }
        }
        for(std::size_t i=0;i+2<src.visual_vertices.size();i+=3) {
            if(inside((src.visual_vertices[i].position+src.visual_vertices[i+1].position+src.visual_vertices[i+2].position)/3))continue;
            for(unsigned k=0;k<3;++k) {
                auto v=src.visual_vertices[i+k];const auto id=remap.at(v.material);
                const float u=v.u*src.texture_width-(v.material%16)*64,w=v.v*src.texture_height-(v.material/16)*64;
                v.u=(id%16*64+u)/level.texture_width;v.v=(id/16*64+w)/level.texture_height;v.material=id;
                v.source_region=v.source_vertex=v.source_color=UINT32_MAX;
                level.bounds.add(v.position);level.visual_vertices.push_back(v);
            }
        }
        for(const auto& t:src.triangles) {
            if(inside((t.a+t.b+t.c)/3))continue;
            auto copy=t;copy.source_id=static_cast<std::uint32_t>(level.triangles.size());copy.portal=UINT32_MAX;
            if(within(liquid,(t.a+t.b+t.c)/3))copy.water=true;
            if(copy.water)++level.water_triangles;
            level.triangles.push_back(copy);
        }
        level.terrain_faces=static_cast<unsigned>(level.visual_vertices.size()/3);
        in>>std::ws;
    }
    if(in.peek()=='W') {
        std::string magic;unsigned water_version{};
        require(bool(in>>magic>>water_version) && magic=="WATER_VOLUMES" && water_version==1,"Invalid water volume section");
        const auto volumes=count(in,512);require(volumes>0,"Empty water volume section");
        for(unsigned i=0;i<volumes;++i) {
            WaterVolume v;v.bounds.min=point(in);v.bounds.max=point(in);v.height=scalar(in);
            const auto& lo=v.bounds.min;const auto& hi=v.bounds.max;
            require(lo.x<hi.x && lo.y<hi.y && lo.z<hi.z && v.height>lo.y,"Invalid water volume bounds");
            level.water_volumes.push_back(v);
        }
        in>>std::ws;
    }
    while(in.peek()=='S') {
        std::string section;unsigned section_version{};
        require(bool(in>>section>>section_version) && (section=="SPLATS" || section=="SCENERY" || section=="SLICEABLE") && section_version==1,"Invalid authored scenery section");
        if(section=="SLICEABLE") {
            // The only geometry the Convexity Void Beam may cut in this world.
            // A box with a fall direction is cut level and topples whole that
            // way (the Crownworks pylons falling across the molten ring).
            require(level.sliceable.empty(),"Duplicate sliceable section");
            const auto n=count(in,64);require(n>0,"Empty sliceable section");
            for(unsigned i=0;i<n;++i) {
                SliceableVolume v;v.bounds.min=point(in);v.bounds.max=point(in);v.fall=point(in);
                const auto& lo=v.bounds.min;const auto& hi=v.bounds.max;
                require(lo.x<hi.x && lo.y<hi.y && lo.z<hi.z && finite(v.fall) && length(v.fall)<=1.01f,"Invalid sliceable volume");
                if(length(v.fall)>.01f)v.fall=normalized(Vec3{v.fall.x,0,v.fall.z});
                level.sliceable.push_back(v);
            }
        } else {
            const auto filename=std::filesystem::path(text(in,240));
            require(!filename.is_absolute() && filename==filename.filename() && (filename.extension()==".s2gs" || filename.extension()==".s2scene"),"Authored scenery must be a sibling .s2gs or .s2scene file");
            level.splats=load_splats(path.parent_path()/filename,&level.splat_surface);
        }
        in>>std::ws;
    }
    if(in.peek()=='G'){homeworld::load(in,level);in>>std::ws;}
    if(in.peek()=='L') {
        // Whirlwind lifts (retail class 400 behaviour): base point, exit
        // heading, column height and catch radius in world units. Each one
        // is a hidden class-400 actor driving the existing lift runtime.
        std::string magic;unsigned lift_version{};
        require(bool(in>>magic>>lift_version) && magic=="LIFTS" && lift_version==1,"Invalid lift section");
        const auto lifts=count(in,64);
        for(unsigned i=0;i<lifts;++i) {
            const auto pos=point(in);const auto yaw=scalar(in,7),height=scalar(in,100),radius=scalar(in,16);
            require(height>=3 && radius>=.25f,"Invalid whirlwind lift dimensions");
            RetailObject o;o.id=static_cast<std::uint32_t>(level.objects.size());o.type=0x400;o.hidden=true;
            o.position=o.previous_position=pos;o.rotation=o.previous_rotation={0,0,retail::original_heading(yaw)/16};
            level.whirlwinds.push_back({o.id,0,0,0,0,height,radius,true});
            level.objects.push_back(std::move(o));
        }
        in>>std::ws;
    }
    if(in.peek()=='T') {
        // Retail tiles repainted for this world (e.g. the intro's autumn-yellow
        // canopies as spring green): every material using (scene, tile),
        // authored or merged from RETAIL_TERRAIN, shows the original file.
        std::string magic;unsigned override_version{};
        require(bool(in>>magic>>override_version) && magic=="TILE_OVERRIDES" && override_version==1,"Invalid tile override section");
        const auto overrides=count(in,64);
        for(unsigned i=0;i<overrides;++i) {
            int slot{};require(bool(in>>slot) && retail_scene(slot),"Tile override names an unsupported scene");
            const auto tile=count(in,4095);const auto pixels=authored_tile(path,in);bool used=false;
            for(unsigned m=0;m<tiles.size();++m)if(tiles[m].slot==slot && tiles[m].tile==tile) {
                used=true;
                for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
                    std::copy_n(pixels.data()+(y*64+x)*4,4,level.texture_rgba.data()+((m/16*64+y)*level.texture_width+m%16*64+x)*4);
            }
            require(used,"Tile override matches no material of this world");
        }
    }
    std::string trailing;require(!(in>>trailing),"Unexpected data at end of custom world");
    const auto floor=level.raycast(level.spawn+Vec3{0,1,0},{0,-1,0},4);
    require(floor && floor->normal.y>.8f,"Custom world has no safe spawn floor");
    initialize_retail_objects(level,models);
    for(const auto& npc:level.npcs) {
        auto& o=level.objects[npc.object];
        // Authored positions need an authored floor offset. The donor NPC's
        // offset describes its original perch, especially for floating Zoe.
        if(const auto floor=level.raycast(o.position+Vec3{0,.05f,0},{0,-1,0},3)) {
            const auto height=static_cast<std::int16_t>(std::lround((o.position.y-floor->point.y)*retail::units_per_world));
            const auto raw=std::bit_cast<std::uint16_t>(height);
            o.record[56]=raw&255;o.record[57]=raw>>8;
        }
    }
    for(auto& o:level.objects)if(o.enemy) {
        o.enemy->home=o.position;o.enemy->target=0;o.enemy->route.clear();
        // A small patrol stays within each authored encounter instead of
        // retaining the source level's distant route coordinates.
        for(float sign:{-1.0f,1.0f}) {
            auto p=o.position+Vec3{sign*.8f,2,0};const auto support=level.raycast(p,{0,-1,0},4);
            if(support && support->normal.y>.8f && std::abs(support->point.y-o.position.y)<.6f)o.enemy->route.push_back({support->point,0});
        }
    }
    return level;
}
void tick_custom_level(Game& g,const InputState& input) {
    if(g.custom_notice_ticks)--g.custom_notice_ticks;
    if(g.level.custom_name.empty())return;
    tick_custom_arena(g,input);
    homeworld::tick(g,input);
    for(auto& goal:g.level.custom_objectives) {
        auto& reward=g.retail_objects.at(goal.reward);
        if(g.guide_awards.orbs&(1u<<goal.bit)){reward.collected=true;continue;}
        reward.previous_rotation=reward.rotation;reward.rotation[2]+=1;
        if(g.dialogue.active() || g.portal_travel.active() || g.scene_locked || !homeworld::ready(g,goal.bit))continue;
        if(goal.progress<goal.checkpoints.size()) {
            const auto d=g.player.position-goal.checkpoints[goal.progress];
            if(length(d)<goal.radius && (!goal.underwater || g.player.swim.mode==WaterMode::Underwater))++goal.progress;
        }
        if(goal.progress==goal.checkpoints.size() && length(g.player.position+retail::origin_above_feet-reward.position)<.8f && g.award_orb(goal.bit)) {
            reward.collected=true;if(goal.npc!=UINT32_MAX)g.dialogue.complete_custom_objective(goal.npc);
            g.custom_notice=goal.title+" - ORB FOUND";g.custom_notice_ticks=240;
        }
    }
}
}
