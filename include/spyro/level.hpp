#pragma once
#include <memory>
#include <array>
#include <cmath>
#include <algorithm>
#include "spyro/custom_gameplay.hpp"
#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include "spyro/math.hpp"
#include "spyro/objects.hpp"
#include "spyro/npc.hpp"
#include "spyro/universe_types.hpp"
#include "spyro/travel_types.hpp"
#include "spyro/level_systems.hpp"
#include "spyro/custom_arena_types.hpp"
#include "spyro/ripto.hpp"
#include "spyro/splats.hpp"
#include <optional>

namespace spyro {
struct Triangle { Vec3 a,b,c; std::uint32_t source_id{}; bool water=false; unsigned surface_type=UINT32_MAX; unsigned portal=UINT32_MAX; bool disabled=false;
    std::uint8_t surface_flags=0xff; };   // surface_flags: the retail flag byte of a special triangle (index & 3F into 800673e8), FF otherwise
struct RayHit { float distance{}; Vec3 point, normal; std::uint32_t triangle{}; unsigned surface_type=UINT32_MAX; };
struct TerrainContact {Vec3 point,normal;unsigned triangle=UINT32_MAX;float incoming_speed{};};
struct WaterSurface { float height{}; std::uint32_t triangle{}; };
// Authored flooded rooms can sit below dry floors. Their surface need not be
// directly overhead (a submerged corridor may connect to a distant shaft).
struct WaterVolume { Bounds bounds; float height{}; };
// A flood held to part of a level (the Crownworks terrace): an XZ grid of
// cells starting at `origin`, `cell` wide; water stands only over set cells.
struct WaterCells {
    Vec3 origin;float cell=1;unsigned columns{},rows{};std::vector<std::uint8_t> wet;
    bool contains(float x,float z)const {
        const float fx=(x-origin.x)/cell,fz=(z-origin.z)/cell;
        if(fx<0 || fz<0 || fx>=float(columns) || fz>=float(rows))return false;
        return wet[static_cast<std::size_t>(fz)*columns+static_cast<std::size_t>(fx)]!=0;
    }
};
// PC elemental water (Water Fury flood, Water headbash pockets): transient
// swimmable surfaces. A flood covers the whole level below `height` (or only
// its `cells`); a pocket is a cylinder of `radius` standing on `floor`.
struct DynamicWater { Vec3 center; float radius{},floor{},height{}; bool flood=false; std::shared_ptr<const WaterCells> cells; };
// Query acceleration for collision triangles and visual faces: static items
// bucketed by bounding-box centre into 4-unit XZ cells (each in exactly one
// cell; queries widen by the largest half extent), plus items always tested:
// those larger than a cell and those the level rewrites every tick (moving
// platforms, morphs). Items appended after the build are the caller's tail.
// Rebuilt lazily when Level::collision_revision changes.
struct CollisionGrid {
    static constexpr float cell=4.0f;
    float x0{},z0{},pad{};int nx{},nz{};
    std::vector<std::uint32_t> start,items,always;
    std::size_t built_size{};unsigned revision{};
    template<class F> void visit(float ax,float az,float bx,float bz,F&& f) const {
        if(nx>0 && ax<=bx && az<=bz) {
            const int i0=std::max(0,int(std::floor((ax-pad-x0)/cell))),i1=std::min(nx-1,int(std::floor((bx+pad-x0)/cell)));
            const int j0=std::max(0,int(std::floor((az-pad-z0)/cell))),j1=std::min(nz-1,int(std::floor((bz+pad-z0)/cell)));
            for(int j=j0;j<=j1;++j)for(int i=i0;i<=i1;++i) {
                const auto c=std::size_t(j)*nx+i;
                for(auto k=start[c];k<start[c+1];++k)f(items[k]);
            }
        }
        for(const auto i:always)f(i);
    }
};
struct TerrainMaterial {
    bool translucent=false;
    unsigned blend_mode{};
};
struct TerrainVertex { Vec3 position,color; float u{},v{}; unsigned material{}; unsigned source_region=UINT32_MAX,source_vertex=UINT32_MAX,source_color=UINT32_MAX; };
struct TerrainStep {unsigned flags{},duration{},forward{},reverse{},weight{},first{},second{};bool hidden=false;};
struct TerrainMorph {
    bool collision=false,color=false;
    unsigned group{},region{},first_vertex{},first_triangle{},node{},flags{},remaining{};
    // Directory (8001c734's seven at 800681d4) and record index. `scripted`
    // morphs follow the level scripts' RAM timeline (RetailScripts runs
    // 800225f8); Glimmer's bridge and Breeze Harbor stay native.
    unsigned kind{},record{};bool scripted=false;
    std::vector<TerrainStep> steps;
    std::vector<std::vector<Vec3>> frames;
    // Pairs map expanded render vertices back to their authored mesh index.
    std::vector<std::pair<unsigned,unsigned>> targets;
};
struct SkyVertex {Vec3 direction,color;};
struct SkyRegion {unsigned first{},count{};};
struct TerrainTranslation {unsigned group{};std::vector<unsigned> regions,triangles;Vec3 offset;
    // Cache of the visual vertices in `regions` (Level::translation_vertices).
    mutable std::vector<std::uint32_t> vertices;mutable std::size_t vertices_for=SIZE_MAX;};
struct CustomObjective {
    unsigned bit{},npc{},reward{},progress{};bool underwater=false;
    float radius=.85f;          // checkpoint reach (format 4 worlds use 1.7)
    std::string title;
    std::vector<Vec3> checkpoints;
};
// A custom world's SLICEABLE volume: the Void Beam cuts only inside these; a
// volume with a fall direction (horizontal, unit) is cut level and topples.
struct SliceableVolume {Bounds bounds;Vec3 fall;};
struct RetailScriptImage;struct YotdScriptImage;
struct Level {
    GameBuild source_game=GameBuild::RetailUS;
    unsigned source_area{};
    // Explicit provisional fallback for YOTD scenes whose authored packed
    // collision section is empty. Triangles then come from visible terrain.
    bool collision_from_terrain=false;
    Vec3 authored_spawn;
    bool spawn_projected=false;
    std::vector<GaussianSplat> splats;
    SplatSurface splat_surface;
    std::string custom_name;
    std::vector<Vec3> custom_route;
    std::vector<CustomObjective> custom_objectives;
    std::vector<HomeworldPiece> homeworld_pieces;
    std::vector<HomeworldTrial> homeworld_trials;
    std::optional<CustomArena> custom_arena;
    std::optional<RiptoArena> ripto_arena;
    std::filesystem::path custom_path;          // the .s2world this level was loaded from
    std::vector<SliceableVolume> sliceable;     // custom worlds only; empty: cut anywhere
    // The toppling volume (one with a fall direction) holding p, if any.
    const SliceableVolume* toppling_at(Vec3 p,float margin=.3f)const {
        for(const auto& v:sliceable)if(p.x>=v.bounds.min.x-margin && p.x<=v.bounds.max.x+margin && p.y>=v.bounds.min.y-margin &&
            p.y<=v.bounds.max.y+margin && p.z>=v.bounds.min.z-margin && p.z<=v.bounds.max.z+margin && length(v.fall)>.5f)return &v;
        return nullptr;
    }
    std::uint32_t wad_slot{}, collision_offset{}, collision_bytes{}, special_count{};
    std::vector<Triangle> triangles;
    Bounds bounds;
    Vec3 spawn;
    float spawn_yaw{};
    Vec3 sky_color;
    std::vector<SkyVertex> sky_vertices;
    std::vector<SkyRegion> sky_regions;
    Bytes sky_visibility;
    std::vector<RetailObject> objects;
    // Retail level runtime (executable, overlays, archive) for RetailScripts; empty for custom/generated scenes.
    std::shared_ptr<const RetailScriptImage> script_image;
    // Independently mapped original YOTD runtime; never uses the S2 ABI.
    std::shared_ptr<const YotdScriptImage> yotd_script_image;
    std::vector<ScriptDefinition> scripts;
    std::vector<NpcDefinition> npcs;
    std::vector<TerrainVertex> visual_vertices;
    std::vector<TerrainMaterial> terrain_materials;
    Bytes texture_rgba;
    unsigned texture_width{},texture_height{},terrain_regions{},terrain_faces{};
    unsigned water_triangles{};
    std::vector<WaterVolume> water_volumes;
    std::vector<DynamicWater> dynamic_water;
    std::vector<TerrainMorph> terrain_morphs;
    unsigned skipped_morphs{};   // animation records the decoder could not map (scripted scenes only)
    std::vector<TerrainTranslation> terrain_translations;
    unsigned terrain_revision{};
    bool dynamic_terrain=false;
    // Structural collision edits (craters, destruction, slices, new ice)
    // bump this; per-tick movers register in dynamic_triangles instead.
    unsigned collision_revision{};
    std::vector<std::uint32_t> dynamic_triangles;
    // Ranges rewritten since the renderer's last upload ([first, count)):
    // moving platforms, arena parts and scene morphs record what they touch,
    // so presentation streams those ranges instead of the whole level.
    mutable std::vector<std::array<std::uint32_t,2>> dirty_vertices,dirty_triangles;
    void touch_vertices(std::uint32_t first,std::uint32_t count){touch(dirty_vertices,first,count);}
    void touch_triangles(std::uint32_t first,std::uint32_t count){touch(dirty_triangles,first,count);}
    // Appends a range, extending the previous one when contiguous; bounded
    // even when nothing presents (headless runs), by collapsing to a span.
    static void touch(std::vector<std::array<std::uint32_t,2>>& ranges,std::uint32_t first,std::uint32_t count);
    mutable std::shared_ptr<const CollisionGrid> collision_grid;
    const CollisionGrid& collision_index() const;
    // Sorted, unique indices of the triangles that may overlap an XZ box
    // (ascending, so callers resolve contacts in the same order a full scan
    // would; disabled/water filtering is the caller's).
    std::vector<std::uint32_t> collision_candidates(float x0,float z0,float x1,float z1) const;
    template<class F> void near_triangles(float x0,float z0,float x1,float z1,F&& f) const {
        const auto& g=collision_index();g.visit(x0,z0,x1,z1,f);
        for(const auto i:dynamic_triangles)f(i);
        for(auto i=g.built_size;i<triangles.size();++i)f(static_cast<std::uint32_t>(i));
    }
    // Solid script mobys (doors, gates, lifts, switch plates): world-space
    // triangles of each moby's collision bank (+08), rebuilt every script
    // tick by RetailScripts. The retail terrain test 8003dc64 and the floor
    // rays 8003ce38/8003ee84 collide with them as with the scene's own.
    // Hits report `object_triangle | index` into object_triangles.
    struct ObjectCollider {unsigned object=UINT32_MAX;std::uint32_t moby{};Bounds bounds;std::uint32_t first{},count{};};
    static constexpr std::uint32_t object_triangle=0x80000000u;
    std::vector<Triangle> object_triangles;
    std::vector<ObjectCollider> object_colliders;
    template<class F> void near_object_triangles(Vec3 lo,Vec3 hi,F&& f) const {
        for(const auto& c:object_colliders) {
            if(hi.x<c.bounds.min.x || lo.x>c.bounds.max.x || hi.y<c.bounds.min.y || lo.y>c.bounds.max.y || hi.z<c.bounds.min.z || lo.z>c.bounds.max.z)continue;
            for(auto k=c.first;k<c.first+c.count;++k)f(k);
        }
    }
    const ObjectCollider* object_collider(std::uint32_t hit) const {
        if(!(hit&object_triangle))return nullptr;
        const auto k=hit&~object_triangle;
        for(const auto& c:object_colliders)if(k>=c.first && k<c.first+c.count)return &c;
        return nullptr;
    }
    // The same for visual faces: sorted first-vertex indices (multiples of 3).
    // Faces that move every tick (homeworld/arena meshes, morph targets,
    // translated regions) are always returned.
    mutable std::shared_ptr<const CollisionGrid> visual_grid;
    const CollisionGrid& visual_index() const;
    std::vector<std::uint32_t> visual_candidates(float x0,float z0,float x1,float z1) const;
    // Visual vertices of a translated scene group (cached per mesh layout).
    const std::vector<std::uint32_t>& translation_vertices(const TerrainTranslation& t) const;
    std::optional<std::array<Vec3,2>> bridge_camera;
    bool surface_swimming=false,underwater_swimming=false;
    std::vector<CampaignPortal> portals;
    std::vector<HomeTransport> home_transports;
    std::vector<Whirlwind> whirlwinds;
    unsigned campaign_scene{};
    std::optional<UniverseLayout> universe;
    std::optional<RayHit> raycast(Vec3 origin, Vec3 direction, float max_distance,bool include_water=false) const;
    std::optional<WaterSurface> water_at(Vec3 origin) const;
    std::optional<WaterSurface> retail_water_at(Vec3 origin) const;
    Vec3 resolve_sphere(Vec3 center, float radius, Vec3& velocity, bool& on_ground,TerrainContact* impact=nullptr) const;
    std::optional<RayHit> ground_support(Vec3 center,float radius,float max_drop) const;
};
void decode_terrain(std::span<const std::uint8_t> archive,Level& level);
void decode_terrain(std::span<const std::uint8_t> archive,Level& level,const GameProfile& profile,unsigned area=0);
void decode_scene_morphs(std::span<const std::uint8_t> archive,Level& level);
void advance_scene_morphs(Level& level);
// Adopt a scripted morph's RAM timeline state (flags, node, frames left) and
// pose its geometry; returns whether anything changed.
bool pose_scene_morph(Level& level,TerrainMorph& morph,unsigned flags,unsigned node,unsigned remaining);
void set_scene_morphs(Level& level,unsigned group,bool finished);
void translate_scene_group(Level& level,unsigned group,Vec3 delta);
// Sky payload excludes its length word; visibility is the scene's fourth section.
void decode_sky(std::span<const std::uint8_t> payload,Level& level);
void decode_sky_visibility(std::span<const std::uint8_t> payload,Level& level);
std::array<bool,64> visible_sky_regions(const Level& level,Vec3 eye);
// Profile currently verified against SCUS_944.25 CRC32 cdc0917c.
Level decode_level(std::span<const std::uint8_t> archive, std::uint32_t wad_slot);
Level decode_level(std::span<const std::uint8_t> archive, std::uint32_t wad_slot,const GameProfile& profile);
Level decode_level(std::span<const std::uint8_t> archive, std::uint32_t wad_slot,const GameProfile& profile,unsigned area);
unsigned level_area_count(std::span<const std::uint8_t> archive,const GameProfile& profile);
Triangle decode_collision_triangle(std::span<const std::uint8_t> record, std::uint32_t id);
Vec3 closest_point(Vec3 p, const Triangle& triangle);
} // namespace spyro
