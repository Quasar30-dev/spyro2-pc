#include "spyro/game_build.hpp"
#include "spyro/level.hpp"
#include "spyro/coordinates.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace spyro {
namespace {
int signed9(std::uint32_t value) { const int low=static_cast<int>(value&511); return low>=256 ? low-512 : low; }
Vec3 pc_coordinate(int x, int y, int z) {
    return retail::world_position({x*16.0f,y*16.0f,z*16.0f});
}
// Extra gameplay headers repeat the parent's entry point. Until the native
// transition-specific entry records are mapped, select a supported floor in
// the requested area's actual collision and retain the authored header value.
void select_yotd_spawn(Level& level) {
    if(const auto floor=level.raycast(level.spawn+Vec3{0,.75f,0},{0,-1,0},2.75f);
       floor && floor->normal.y>.65f)return;
    const auto safe=[&](Vec3 p,bool dry) {
        if(dry && level.water_at(p+retail::origin_above_feet))return false;
        if(level.raycast(p+Vec3{0,.08f,0},{0,1,0},.8f))return false;
        constexpr std::array<Vec3,9> footprint={{{0,0,0},{.4f,0,0},{-.4f,0,0},{0,0,.4f},{0,0,-.4f},
            {.28f,0,.28f},{-.28f,0,.28f},{.28f,0,-.28f},{-.28f,0,-.28f}}};
        for(const auto offset:footprint) {
            const auto floor=level.raycast(p+offset+Vec3{0,.6f,0},{0,-1,0},1.2f);
            if(!floor || floor->normal.y<.65f || std::abs(floor->point.y-p.y)>.3f)return false;
        }
        return true;
    };
    struct Candidate {Vec3 point;float distance;};std::vector<Candidate> candidates;
    for(const auto& t:level.triangles) {
        if(t.disabled || t.water || t.portal!=UINT32_MAX)continue;
        const auto normal=cross(t.b-t.a,t.c-t.a);
        if(length(normal)<.08f || std::abs(normalized(normal).y)<.8f)continue;
        const auto point=(t.a+t.b+t.c)/3;
        candidates.push_back({point,length(point-level.authored_spawn)});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.distance<b.distance;});
    for(const bool dry:{true,false})for(const auto& candidate:candidates)if(safe(candidate.point,dry)) {
        const auto floor=level.raycast(candidate.point+Vec3{0,.6f,0},{0,-1,0},1.2f);
        if(!floor)continue;
        // Feet are sphere-center minus radius. On a slope the center must
        // rise by radius / normal.y, rather than starting inside the floor.
        const float clearance=retail::spyro_origin_height*(1/floor->normal.y-1)+.025f;
        level.spawn=floor->point+Vec3{0,clearance,0};level.spawn_projected=true;return;
    }
    throw FormatError("YOTD area has no supported provisional player spawn");
}
}
Triangle decode_collision_triangle(std::span<const std::uint8_t> record, std::uint32_t id) {
    const auto x=read_u32(record,0), y=read_u32(record,4), z=read_u32(record,8);
    const int bx=x&0x3fff, by=y&0x3fff, bz=z&0x3fff;
    // Verified against SCUS_944.25 function 0x8001c214. Use explicit sign
    // extension to avoid signed-left-shift UB and implementation-defined shifts.
    return {pc_coordinate(bx,by,bz),
        pc_coordinate(bx+signed9(x>>14),by+signed9(y>>14),bz+static_cast<int>((z>>16)&255)),
        pc_coordinate(bx+signed9(x>>23),by+signed9(y>>23),bz+static_cast<int>(z>>24)),id,(z&0x4000)!=0};
}
Level decode_level(std::span<const std::uint8_t> archive, std::uint32_t slot) {
    const auto children=parse_level_archive(archive);
    if (children.size()!=4 || children[1].size<0x40) throw FormatError("Not a verified four-section level archive");
    const auto& scene=children[1];
    std::size_t at=scene.offset, end=std::size_t(scene.offset)+scene.size;
    std::vector<unsigned> surfaces;
    std::vector<unsigned> portal_surfaces;
    Bytes sky_visibility;
    bool visibility_enabled=false;
    for (unsigned part=0; part<6; ++part) {
        const auto size=read_u32(archive,at);
        if (size<4 || size%4 || at>end || size>end-at) throw FormatError("Invalid level section chain");
        if(part==2)visibility_enabled=size>8;
        if(part==3 && visibility_enabled && size>8)
            sky_visibility.assign(archive.begin()+at+4,archive.begin()+at+size);
        // 8006fa98 relocates section 4's special-surface pointers;
        // 8002b078 resolves the collision flag's low six bits through them.
        if(part==4) {
            if(size<8)throw FormatError("Missing special-surface directory");
            const auto count=read_u32(archive,at+4);
            if(count>63 || 8+std::uint64_t(count)*4>size)throw FormatError("Invalid special-surface directory");
            for(unsigned i=0;i<count;++i) {
                const auto offset=std::uint64_t(read_u32(archive,at+8+i*4))+4;
                if(offset<8+count*4 || offset>=size)throw FormatError("Special surface outside its section");
                surfaces.push_back(archive[at+static_cast<std::size_t>(offset)]);
                portal_surfaces.push_back(surfaces.back()==6?read_u32(archive,at+static_cast<std::size_t>(offset)+4):UINT32_MAX);
            }
        }
        at+=size;
    }
    // Three 32-bit values precede the length-prefixed collision section.
    if (at>end || end-at<52) throw FormatError("Missing collision header");
    at+=12;
    const auto chunk_size=read_u32(archive,at);
    if (chunk_size<40 || chunk_size>end-at) throw FormatError("Collision chunk exceeds scene section");
    const auto data=archive.subspan(at+4,chunk_size-4);
    const auto count=read_u32(data,0), special=read_u32(data,4);
    const auto triangles=read_u32(data,20), flags=read_u32(data,24);
    if (!count || count>200000 || special>count || triangles<36 || triangles>data.size() ||
        std::uint64_t(count)*12>data.size()-triangles || std::uint64_t(triangles)+std::uint64_t(count)*12!=flags)
        throw FormatError("Invalid collision triangle table");
    if(flags>data.size() || special>data.size()-flags)throw FormatError("Truncated collision surface flags");
    Level level; level.wad_slot=slot; level.collision_offset=static_cast<std::uint32_t>(at+4);
    level.collision_bytes=chunk_size; level.special_count=special; level.triangles.reserve(count);
    for (std::uint32_t i=0; i<count; ++i) {
        auto t=decode_collision_triangle(data.subspan(triangles+std::size_t(i)*12,12),i);
        if(i<special) {
            const unsigned surface=data[flags+i]&63;
            // Some mounted retail scenes (25, 73, 77, 79, 93) retain
            // triangle tags with no matching surface record. Keep their
            // collision geometry, but do not invent a special response.
                t.surface_flags=data[flags+i];
                if(surface<surfaces.size()){t.surface_type=surfaces[surface];t.portal=portal_surfaces[surface];}
        }
        if(t.water)++level.water_triangles;
        level.bounds.add(t.a); level.bounds.add(t.b); level.bounds.add(t.c);
        level.triangles.push_back(t);
    }
    // 80070498 loads the language gameplay section into 8006b294;
    // 8001c734 copies its first XYZ triplet and byte +0xe heading into Spyro.
    const auto gameplay=archive.subspan(children[3].offset,children[3].size);
    if(gameplay.size()<16)throw FormatError("Missing retail player spawn record");
    level.spawn=retail::world_position({static_cast<float>(static_cast<std::int32_t>(read_u32(gameplay,0))),
        static_cast<float>(static_cast<std::int32_t>(read_u32(gameplay,4))),
        static_cast<float>(static_cast<std::int32_t>(read_u32(gameplay,8)))})-retail::origin_above_feet;
    level.spawn_yaw=retail::native_heading(gameplay[14]*16.0f);
    // Swim families are scene-local. Do not turn a non-swimming level's
    // flagged hazard plane into a swimmable pool by importing arbitrary clips.
    auto bank=static_cast<std::size_t>(read_u32(archive,level_model_directory(archive)));
    if(bank)for(unsigned guard=0;;++guard) {
        if(guard>62)throw FormatError("Unterminated level Spyro animation bank");
        const auto id=read_u32(archive,bank);if(id==0xffffffff)break;
        const auto size=read_u32(archive,bank+4);
        if(id>=62 || bank+8>archive.size() || size>archive.size()-bank-8)throw FormatError("Invalid level Spyro animation bank");
        if(id==44)level.surface_swimming=true;
        if(id==40)level.underwater_swimming=true;
        bank+=8+size;
    }
    // 8001c5ec installs this RGB value as the backdrop before sky geometry.
    const auto sky_at=at+chunk_size;
    if(sky_at>end || end-sky_at<8)throw FormatError("Missing retail sky header");
    const auto sky_bytes=read_u32(archive,sky_at);
    if(sky_bytes<12 || sky_bytes>end-sky_at)throw FormatError("Invalid retail sky section");
    decode_sky(archive.subspan(sky_at+4,sky_bytes-4),level);
    decode_sky_visibility(sky_visibility,level);
    decode_terrain(archive,level);
    level.objects=decode_object_instances(archive,slot);
    decode_ripto_arena(archive,level);
    level.npcs=decode_npcs(archive,slot);
    decode_level_systems(archive,level);
    decode_scene_morphs(archive,level);
    return level;
}
unsigned level_area_count(std::span<const std::uint8_t> archive,const GameProfile& profile) {
    if(!profile.yotd())return 1;
    (void)parse_level_archive(archive,profile);
    unsigned count=1;
    for(unsigned area=1;area<5;++area) {
        const auto at=0x20u+(area-1)*16;
        const auto scene=read_u32(archive,at),scene_bytes=read_u32(archive,at+4);
        const auto gameplay=read_u32(archive,at+8),gameplay_bytes=read_u32(archive,at+12);
        if(!scene_bytes && !gameplay_bytes)break;
        if(!scene_bytes || !gameplay_bytes || scene%4 || gameplay%4 ||
            std::uint64_t(scene)+scene_bytes>archive.size() || std::uint64_t(gameplay)+gameplay_bytes>archive.size())
            throw FormatError("Invalid YOTD subarea extents");
        ++count;
    }
    return count;
}
Level decode_level(std::span<const std::uint8_t> archive,std::uint32_t slot,const GameProfile& profile) {
    return decode_level(archive,slot,profile,0);
}
Level decode_level(std::span<const std::uint8_t> archive,std::uint32_t slot,const GameProfile& profile,unsigned area) {
    if(!profile.yotd()) {
        if(area)throw FormatError("Spyro 2 archives have no YOTD subareas");
        return decode_level(archive,slot);
    }
    if(area>=level_area_count(archive,profile))throw FormatError("YOTD subarea is not present");
    const auto children=parse_level_archive(archive,profile);
    const auto main_scene=archive.subspan(children[1].offset,children[1].size);
    const auto area_entry=0x20u+(area?area-1:0)*16;
    const auto scene_offset=area?read_u32(archive,area_entry):children[1].offset;
    const auto scene_bytes=area?read_u32(archive,area_entry+4):children[1].size;
    const auto scene=archive.subspan(scene_offset,scene_bytes);
    Level level;level.source_game=profile.build;level.source_area=area;level.wad_slot=slot;
    std::vector<unsigned> surfaces;
    std::size_t at=area?std::size_t(read_u32(scene,0))+0x180:0;
    // Main scenes start with textures/regions, extra areas with regions/
    // visibility. Both then carry special surfaces and an atmosphere chunk.
    for(unsigned part=0;part<4;++part) {
        const auto size=read_u32(scene,at);
        if(size<4 || size%4 || at>scene.size() || size>scene.size()-at)
            throw FormatError("Invalid YOTD scene section chain");
        if(part==2 && size>4) {
            if(size<8)throw FormatError("Missing YOTD special-surface directory");
            const auto count=read_u32(scene,at+4);
            if(count>63 || 8+std::uint64_t(count)*4>size)throw FormatError("Invalid YOTD special-surface directory");
            for(unsigned i=0;i<count;++i) {
                const auto offset=std::uint64_t(read_u32(scene,at+8+i*4))+4;
                if(offset<8+count*4 || offset>=size)throw FormatError("YOTD special surface outside its section");
                surfaces.push_back(scene[at+static_cast<std::size_t>(offset)]);
            }
        }
        at+=size;
    }
    // Only main scenes put three raw grid words before the collision length.
    if(at>scene.size() || scene.size()-at<(area?4u:16u))throw FormatError("Missing YOTD collision header");
    if(!area)at+=12;
    const auto collision_bytes=read_u32(scene,at);
    if(collision_bytes<4 || collision_bytes%4 || collision_bytes>scene.size()-at)
        throw FormatError("YOTD collision chunk exceeds its scene");
    level.collision_offset=scene_offset+static_cast<std::uint32_t>(at+4);level.collision_bytes=collision_bytes;
    if(collision_bytes>4) {
        if(collision_bytes<40)throw FormatError("Truncated YOTD collision header");
        const auto data=scene.subspan(at+4,collision_bytes-4);
        const auto count=read_u32(data,0),special=read_u32(data,4),triangles=read_u32(data,20),flags=read_u32(data,24);
        if(!count || count>200000 || special>count || triangles<36 || triangles>data.size() ||
            std::uint64_t(triangles)+std::uint64_t(count)*12!=flags || flags>data.size() || special>data.size()-flags)
            throw FormatError("Invalid YOTD collision triangle table");
        level.special_count=special;level.triangles.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            auto t=decode_collision_triangle(data.subspan(triangles+std::size_t(i)*12,12),i);
            if(i<special) {
                const auto surface=data[flags+i]&63;
                if(surface<surfaces.size())t.surface_type=surfaces[surface];
            }
            // YOTD surface IDs differ; retain their identity without routing
            // them through S2's portal IDs or class-specific responses.
            if(t.water)++level.water_triangles;
            level.bounds.add(t.a);level.bounds.add(t.b);level.bounds.add(t.c);level.triangles.push_back(t);
        }
    }
    at+=collision_bytes;
    if(area) {
        // Subareas share the parent sky. Their scene stream ends after its
        // collision data rather than carrying another sky/texture directory.
        at=0;
        for(unsigned part=0;part<4;++part) {
            const auto size=read_u32(main_scene,at);
            if(size<4 || size%4 || at>main_scene.size() || size>main_scene.size()-at)
                throw FormatError("Invalid YOTD parent scene section chain");
            at+=size;
        }
        if(at>main_scene.size() || main_scene.size()-at<16)throw FormatError("Missing YOTD parent collision chunk");
        at+=12;const auto size=read_u32(main_scene,at);
        if(size<4 || size>main_scene.size()-at)throw FormatError("Invalid YOTD parent collision extent");
        at+=size;
    }
    const auto sky_bytes=read_u32(main_scene,at);
    if(sky_bytes<12 || sky_bytes%4 || at>main_scene.size() || sky_bytes>main_scene.size()-at)
        throw FormatError("Invalid YOTD sky section");
    decode_sky(main_scene.subspan(at+4,sky_bytes-4),level);
    // The YOTD fourth scene chunk is not the S2 sky octree. Drawing all
    // authored sky regions avoids assigning an unverified culling format.
    decode_terrain(archive,level,profile,area);
    if(collision_bytes==4) {
        level.collision_from_terrain=true;
        for(std::size_t i=0;i+2<level.visual_vertices.size();i+=3) {
            const auto& a=level.visual_vertices[i];const auto& b=level.visual_vertices[i+1];const auto& c=level.visual_vertices[i+2];
            if(a.material<level.terrain_materials.size() && level.terrain_materials[a.material].translucent)continue;
            if(length(cross(b.position-a.position,c.position-a.position))<1e-8f)continue;
            Triangle t{a.position,b.position,c.position,static_cast<std::uint32_t>(level.triangles.size())};
            level.bounds.add(t.a);level.bounds.add(t.b);level.bounds.add(t.c);level.triangles.push_back(t);
        }
    }
    const auto gameplay_offset=area?read_u32(archive,area_entry+8):children[3].offset;
    const auto gameplay_bytes=area?read_u32(archive,area_entry+12):children[3].size;
    const auto gameplay=archive.subspan(gameplay_offset,gameplay_bytes);
    if(gameplay.size()<16)throw FormatError("Missing YOTD player spawn record");
    level.spawn=retail::world_position({static_cast<float>(static_cast<std::int32_t>(read_u32(gameplay,0))),
        static_cast<float>(static_cast<std::int32_t>(read_u32(gameplay,4))),static_cast<float>(static_cast<std::int32_t>(read_u32(gameplay,8)))})-retail::origin_above_feet;
    level.spawn_yaw=retail::native_heading(gameplay[14]*16.0f);
    level.authored_spawn=level.spawn;
    select_yotd_spawn(level);
    level.objects=decode_object_instances(archive,slot,profile,area);
    return level;
}
namespace {
// Buckets `count` items by XZ box centre; corners(i) gives an item's three
// corners. Moving items and items larger than a cell go to `always`. Items
// are reported as index*stride (visual faces report their first vertex).
template<class Corners> std::shared_ptr<CollisionGrid> build_grid(std::size_t count,std::uint32_t stride,Corners corners,const std::vector<std::uint8_t>& moving) {
    auto g=std::make_shared<CollisionGrid>();
    struct Item {std::uint32_t id;float cx,cz;};std::vector<Item> bucketed;bucketed.reserve(count);
    float x0=1e30f,z0=1e30f,x1=-1e30f,z1=-1e30f,pad=0;
    for(std::uint32_t i=0;i<count;++i) {
        if(i<moving.size() && moving[i]){g->always.push_back(i*stride);continue;}
        const auto [a,b,c]=corners(i);
        const float ax=std::min({a.x,b.x,c.x}),bx=std::max({a.x,b.x,c.x}),az=std::min({a.z,b.z,c.z}),bz=std::max({a.z,b.z,c.z});
        if(!std::isfinite(ax+bx+az+bz) || bx-ax>CollisionGrid::cell || bz-az>CollisionGrid::cell){g->always.push_back(i*stride);continue;}
        const float cx=(ax+bx)/2,cz=(az+bz)/2;bucketed.push_back({i*stride,cx,cz});
        pad=std::max({pad,(bx-ax)/2,(bz-az)/2});
        x0=std::min(x0,cx);x1=std::max(x1,cx);z0=std::min(z0,cz);z1=std::max(z1,cz);
    }
    if(bucketed.empty())return g;
    g->x0=x0;g->z0=z0;g->pad=pad+1e-3f;
    g->nx=std::clamp(int((x1-x0)/CollisionGrid::cell)+1,1,4096);g->nz=std::clamp(int((z1-z0)/CollisionGrid::cell)+1,1,4096);
    const auto cell_of=[&](const Item& it){
        const int ci=std::clamp(int((it.cx-g->x0)/CollisionGrid::cell),0,g->nx-1),cj=std::clamp(int((it.cz-g->z0)/CollisionGrid::cell),0,g->nz-1);
        return std::size_t(cj)*g->nx+ci;};
    g->start.assign(std::size_t(g->nx)*g->nz+1,0);
    for(const auto& it:bucketed)++g->start[cell_of(it)+1];
    for(std::size_t c=1;c<g->start.size();++c)g->start[c]+=g->start[c-1];
    g->items.resize(bucketed.size());auto fill=g->start;
    for(const auto& it:bucketed)g->items[fill[cell_of(it)]++]=it.id;
    return g;
}
}
const CollisionGrid& Level::collision_index() const {
    if(collision_grid && collision_grid->revision==collision_revision && collision_grid->built_size<=triangles.size() &&
       triangles.size()-collision_grid->built_size<=4096)return *collision_grid;
    std::vector<std::uint8_t> moving(triangles.size());
    for(const auto i:dynamic_triangles)if(i<moving.size())moving[i]=1;
    auto g=build_grid(triangles.size(),1,[&](std::uint32_t i){const auto& t=triangles[i];return std::array<Vec3,3>{t.a,t.b,t.c};},moving);
    // Moving triangles are reported through dynamic_triangles, not `always`.
    std::erase_if(g->always,[&](std::uint32_t i){return moving[i]!=0;});
    g->revision=collision_revision;g->built_size=triangles.size();
    collision_grid=std::move(g);return *collision_grid;
}
const std::vector<std::uint32_t>& Level::translation_vertices(const TerrainTranslation& t) const {
    if(t.vertices_for!=visual_vertices.size()) {
        t.vertices.clear();
        for(std::uint32_t i=0;i<visual_vertices.size();++i)
            if(std::find(t.regions.begin(),t.regions.end(),visual_vertices[i].source_region)!=t.regions.end())t.vertices.push_back(i);
        t.vertices_for=visual_vertices.size();
    }
    return t.vertices;
}
const CollisionGrid& Level::visual_index() const {
    const auto faces=visual_vertices.size()/3;
    if(visual_grid && visual_grid->revision==collision_revision && visual_grid->built_size<=faces*3 &&
       faces*3-visual_grid->built_size<=3*4096)return *visual_grid;
    std::vector<std::uint8_t> moving(faces);
    const auto mark=[&](std::size_t vertex){if(vertex/3<faces)moving[vertex/3]=1;};
    const auto meshes=[&](const auto& pieces){for(const auto& p:pieces)for(const auto& m:p.meshes)for(unsigned k=0;k<m.vertices;k+=3)mark(m.vertex+k);};
    meshes(homeworld_pieces);if(custom_arena)meshes(custom_arena->pieces);
    for(const auto& m:terrain_morphs)if(!m.collision)for(const auto& [output,input]:m.targets){(void)input;mark(output);}
    for(const auto& t:terrain_translations)for(const auto v:translation_vertices(t))mark(v);
    auto g=build_grid(faces,3,[&](std::uint32_t i){return std::array<Vec3,3>{visual_vertices[i*3].position,visual_vertices[i*3+1].position,visual_vertices[i*3+2].position};},moving);
    g->revision=collision_revision;g->built_size=faces*3;
    visual_grid=std::move(g);return *visual_grid;
}
std::vector<std::uint32_t> Level::visual_candidates(float x0,float z0,float x1,float z1) const {
    const auto& g=visual_index();std::vector<std::uint32_t> found;found.reserve(256);
    g.visit(x0,z0,x1,z1,[&](std::uint32_t i){found.push_back(i);});
    for(auto i=g.built_size;i+2<visual_vertices.size();i+=3)found.push_back(static_cast<std::uint32_t>(i));
    std::sort(found.begin(),found.end());found.erase(std::unique(found.begin(),found.end()),found.end());
    return found;
}
void Level::touch(std::vector<std::array<std::uint32_t,2>>& ranges,std::uint32_t first,std::uint32_t count) {
    if(!count)return;
    if(!ranges.empty()) {
        auto& last=ranges.back();
        if(first>=last[0] && first<=last[0]+last[1]){last[1]=std::max(last[1],first+count-last[0]);return;}
    }
    ranges.push_back({first,count});
    if(ranges.size()<=4096)return;
    std::sort(ranges.begin(),ranges.end());std::vector<std::array<std::uint32_t,2>> merged;
    for(const auto [f,c]:ranges) {
        if(!merged.empty() && f<=merged.back()[0]+merged.back()[1]+64)merged.back()[1]=std::max(merged.back()[0]+merged.back()[1],f+c)-merged.back()[0];
        else merged.push_back({f,c});
    }
    if(merged.size()>1024)merged={{merged.front()[0],merged.back()[0]+merged.back()[1]-merged.front()[0]}};
    ranges=std::move(merged);
}
std::vector<std::uint32_t> Level::collision_candidates(float x0,float z0,float x1,float z1) const {
    std::vector<std::uint32_t> found;found.reserve(128);
    near_triangles(x0,z0,x1,z1,[&](std::uint32_t i){found.push_back(i);});
    std::sort(found.begin(),found.end());found.erase(std::unique(found.begin(),found.end()),found.end());
    return found;
}
std::optional<RayHit> Level::raycast(Vec3 origin, Vec3 direction, float limit,bool include_water) const {
    if (!finite(origin) || !finite(direction) || !std::isfinite(limit) || limit<0) return {};
    direction=normalized(direction); if (length(direction)<0.5f) return {};
    std::optional<RayHit> result;
    const auto end=origin+direction*limit;
    const auto test=[&](const Triangle& t,std::uint32_t id) {
        const auto e1=t.b-t.a, e2=t.c-t.a, p=cross(direction,e2);
        const auto det=dot(e1,p); if (std::abs(det)<1e-8f) return;
        const auto inv=1.0f/det; const auto relative=origin-t.a;
        // Adjacent faces must both include their shared edge after float
        // rounding; otherwise a ray exactly on a tiled seam can miss both.
        constexpr float edge_epsilon=1e-6f;
        const auto u=dot(relative,p)*inv; if (u<-edge_epsilon || u>1+edge_epsilon) return;
        const auto q=cross(relative,e1); const auto v=dot(direction,q)*inv;
        if (v<-edge_epsilon || u+v>1+edge_epsilon) return;
        const auto distance=dot(e2,q)*inv; if (distance<0 || distance>limit) return;
        auto normal=normalized(cross(e1,e2)); if (dot(normal,direction)>0) normal=normal*-1.0f;
        result=RayHit{distance,origin+direction*distance,normal,id,t.surface_type}; limit=distance;
    };
    for(const auto index:collision_candidates(std::min(origin.x,end.x),std::min(origin.z,end.z),std::max(origin.x,end.x),std::max(origin.z,end.z))) {
        const auto& t=triangles[index];
        if(t.disabled || (t.water && !include_water) || t.portal!=UINT32_MAX)continue;
        test(t,t.source_id);
    }
    near_object_triangles({std::min(origin.x,end.x),std::min(origin.y,end.y),std::min(origin.z,end.z)},
        {std::max(origin.x,end.x),std::max(origin.y,end.y),std::max(origin.z,end.z)},[&](std::uint32_t k){test(object_triangles[k],object_triangle|k);});
    return result;
}
std::optional<WaterSurface> Level::water_at(Vec3 origin) const {
    if(!finite(origin))return {};
    if(dynamic_water.empty())return retail_water_at(origin);
    // Elemental water stands over retail water: the higher surface wins.
    std::optional<WaterSurface> best;
    for(const auto& w:dynamic_water) {
        const bool inside=w.cells?w.cells->contains(origin.x,origin.z) && origin.y>=w.floor-.4f:
            w.flood || (std::hypot(origin.x-w.center.x,origin.z-w.center.z)<=w.radius && origin.y>=w.floor-.4f);
        if(inside && (!best || w.height>best->height))best=WaterSurface{w.height,UINT32_MAX};
    }
    const auto retail=retail_water_at(origin);
    return best && (!retail || best->height>retail->height)?best:retail;
}
std::optional<WaterSurface> Level::retail_water_at(Vec3 origin) const {
    if(!water_volumes.empty()) {
        for(const auto& volume:water_volumes) {
            const auto& b=volume.bounds;
            if(origin.x>=b.min.x && origin.x<=b.max.x && origin.y>=b.min.y && origin.y<=b.max.y && origin.z>=b.min.z && origin.z<=b.max.z)
                return WaterSurface{volume.height,UINT32_MAX};
        }
        // Explicit volumes own this world's water; a visible pool must not
        // extend infinitely downwards beyond its sealed floor.
        return {};
    }
    std::optional<WaterSurface> result;float nearest=std::numeric_limits<float>::max();
    for(const auto index:collision_candidates(origin.x,origin.z,origin.x,origin.z)) {
        const auto& t=triangles[index];
        if(t.disabled || !t.water || origin.x<std::min({t.a.x,t.b.x,t.c.x})-1e-5f || origin.x>std::max({t.a.x,t.b.x,t.c.x})+1e-5f ||
            origin.z<std::min({t.a.z,t.b.z,t.c.z})-1e-5f || origin.z>std::max({t.a.z,t.b.z,t.c.z})+1e-5f)continue;
        const auto a=t.b-t.a,b=t.c-t.a,p=origin-t.a;
        const float det=a.x*b.z-a.z*b.x;if(std::abs(det)<1e-8f)continue;
        const float u=(p.x*b.z-p.z*b.x)/det,v=(a.x*p.z-a.z*p.x)/det;
        if(u<-1e-5f || v<-1e-5f || u+v>1.00001f)continue;
        const float height=t.a.y+u*a.y+v*b.y,distance=std::abs(origin.y-height);
        if(distance<nearest){result=WaterSurface{height,t.source_id};nearest=distance;}
    }
    // A bridge or an upper floor separates its player from a pool below it.
    if(result && origin.y>result->height+0.001f)
        if(const auto floor=raycast(origin,{0,-1,0},origin.y-result->height);floor)return {};
    return result;
}
Vec3 closest_point(Vec3 p, const Triangle& t) {
    const auto ab=t.b-t.a, ac=t.c-t.a, ap=p-t.a;
    if (dot(cross(ab,ac),cross(ab,ac))<1e-14f) {
        auto segment=[&](Vec3 a,Vec3 b) {const auto v=b-a; const auto len2=dot(v,v);
            return len2>1e-14f ? a+v*std::clamp(dot(p-a,v)/len2,0.0f,1.0f) : a;};
        auto best=segment(t.a,t.b);
        for (auto point : {segment(t.b,t.c),segment(t.c,t.a)})
            if (dot(p-point,p-point)<dot(p-best,p-best)) best=point;
        return best;
    }
    const auto d1=dot(ab,ap), d2=dot(ac,ap);
    if (d1<=0 && d2<=0) return t.a;
    const auto bp=p-t.b; const auto d3=dot(ab,bp), d4=dot(ac,bp);
    if (d3>=0 && d4<=d3) return t.b;
    const auto vc=d1*d4-d3*d2;
    if (vc<=0 && d1>=0 && d3<=0) return t.a+ab*(d1/(d1-d3));
    const auto cp=p-t.c; const auto d5=dot(ab,cp), d6=dot(ac,cp);
    if (d6>=0 && d5<=d6) return t.c;
    const auto vb=d5*d2-d1*d6;
    if (vb<=0 && d2>=0 && d6<=0) return t.a+ac*(d2/(d2-d6));
    const auto va=d3*d6-d5*d4;
    if (va<=0 && d4-d3>=0 && d5-d6>=0) return t.b+(t.c-t.b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const auto sum=va+vb+vc;
    if (std::abs(sum)<1e-12f) return t.a;
    return t.a+ab*(vb/sum)+ac*(vc/sum);
}
std::optional<RayHit> Level::ground_support(Vec3 center,float radius,float max_drop) const {
    if(!finite(center) || !std::isfinite(radius) || !std::isfinite(max_drop) || radius<=0 || max_drop<0)return {};
    std::optional<RayHit> result;
    const auto test=[&](const Triangle& t,std::uint32_t id) {
        if(center.x+radius<std::min({t.a.x,t.b.x,t.c.x}) || center.x-radius>std::max({t.a.x,t.b.x,t.c.x}) ||
            center.z+radius<std::min({t.a.z,t.b.z,t.c.z}) || center.z-radius>std::max({t.a.z,t.b.z,t.c.z}))return;
        auto normal=normalized(cross(t.b-t.a,t.c-t.a));if(normal.y<0)normal=normal*-1;
        if(normal.y<=0.55f)return;
        auto drop=(dot(center-t.a,normal)-radius)/normal.y;
        if(drop< -0.001f || drop>max_drop || (result && drop>=result->distance))return;
        auto contact=center-Vec3{0,drop,0}-normal*radius;
        if(length(contact-closest_point(contact,t))>0.001f) {
            // At a convex seam the sphere rests on the shared edge. Its
            // supporting point is outside either face's projected interior.
            const auto lowered=center-Vec3{0,max_drop,0};
            if(length(lowered-closest_point(lowered,t))>radius)return;
            float lo=0,hi=max_drop;
            for(unsigned i=0;i<16;++i) {
                const float mid=(lo+hi)*0.5f;const auto p=center-Vec3{0,mid,0};
                if(length(p-closest_point(p,t))>radius)lo=mid;else hi=mid;
            }
            drop=hi;const auto p=center-Vec3{0,drop,0};contact=closest_point(p,t);normal=normalized(p-contact);
            if(normal.y<=0.55f || (result && drop>=result->distance))return;
        }
        result=RayHit{drop,contact,normal,id,t.surface_type};
    };
    for(const auto index:collision_candidates(center.x-radius,center.z-radius,center.x+radius,center.z+radius)) {
        const auto& t=triangles[index];
        if(t.disabled || t.water || t.portal!=UINT32_MAX)continue;
        test(t,t.source_id);
    }
    near_object_triangles(center-Vec3{radius,radius+max_drop,radius},center+Vec3{radius,radius,radius},
        [&](std::uint32_t k){test(object_triangles[k],object_triangle|k);});
    return result;
}
Vec3 Level::resolve_sphere(Vec3 center, float radius, Vec3& velocity, bool& ground,TerrainContact* impact) const {
    if(!finite(center))return center;
    const float reach=radius*2+0.25f;
    const auto push=[&](const Triangle& t,std::uint32_t id) {
        // Cheap bounds rejection before the closest-point calculation.
        if (center.x+radius<std::min({t.a.x,t.b.x,t.c.x}) || center.x-radius>std::max({t.a.x,t.b.x,t.c.x}) ||
            center.y+radius<std::min({t.a.y,t.b.y,t.c.y}) || center.y-radius>std::max({t.a.y,t.b.y,t.c.y}) ||
            center.z+radius<std::min({t.a.z,t.b.z,t.c.z}) || center.z-radius>std::max({t.a.z,t.b.z,t.c.z})) return;
        const auto delta=center-closest_point(center,t); const auto distance=length(delta);
        if (distance>=radius) return;
        Vec3 n=distance>1e-6f ? delta/distance : normalized(cross(t.b-t.a,t.c-t.a));
        if (length(n)<0.5f) return;
        if (distance<1e-6f && dot(n,velocity)>0) n=n*-1;
        center+=n*(radius-distance+0.0001f);
        const auto incoming=dot(velocity,n);
        if(impact && std::abs(n.y)<.55f && -incoming>impact->incoming_speed)*impact={closest_point(center,t),n,id,-incoming};
        if (incoming<0) velocity=velocity-n*incoming;
        if (n.y>0.55f) ground=true;
    };
    for(const auto i:collision_candidates(center.x-reach,center.z-reach,center.x+reach,center.z+reach)) {
        const auto& t=triangles[i];
        // 8003ee84 exposes packed Z bit 14 as 8006aba4. It is a liquid
        // surface, not a solid floor or a camera obstruction.
        if(t.disabled || t.water || (t.portal!=UINT32_MAX && std::any_of(portals.begin(),portals.end(),[&](const auto& p){return p.index==t.portal && p.enabled;})))continue;
        push(t,i);
    }
    near_object_triangles(center-Vec3{reach,reach,reach},center+Vec3{reach,reach,reach},[&](std::uint32_t k){push(object_triangles[k],object_triangle|k);});
    return center;
}
} // namespace spyro
