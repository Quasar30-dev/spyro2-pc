#pragma once
#include "spyro/level.hpp"
#include <map>

namespace spyro {
class Game;
// VoidSlice: the Convexity Void Beam cuts a bounded block of terrain off
// along a plane; the block falls as a rigid body and lands as level geometry.
enum class TerrainAttack : unsigned { Supercharge,Superflame,GreenBomb,VoidSlice };
struct DestructionSettings {
    bool enabled=false,charge=true,flame=true,bomb=true,protect_gameplay=true;
    unsigned strength=1;
    // Convexity breath terrain cutting. Independent of `enabled` (the retail
    // power blasts); `slice_size` 1..3 picks a 2 / 3 / 4.2 unit cut radius.
    bool void_slice=true,void_erode=true;
    unsigned slice_size=2;
    bool valid()const{return strength>=1 && strength<=3 && slice_size>=1 && slice_size<=3;}
    bool operator==(const DestructionSettings&)const=default;
};
struct TerrainDamage {
    Vec3 center,normal,safe_feet;
    TerrainAttack attack=TerrainAttack::Supercharge;
    unsigned strength=1,seed{};
    bool protect_gameplay=true,fragment=false;
    // VoidSlice only: cut radius and the block's final rigid transform about
    // its pivot (rotation vector, radians), recorded when it lands.
    float slice_radius{};
    Vec3 slice_offset,slice_rotation;
    bool slice_landed=false;
    // Shattered cuts (since save version 19): the block breaks into 4-8 shards;
    // each pose is {offset, rotation vector} about that shard's pivot. Zero
    // shards is the original single-slab cut, replayed as before.
    unsigned shards{};
    std::vector<std::array<Vec3,2>> shard_poses;
    bool valid()const;
};
float slice_radius(unsigned size);
// A falling cut block. Geometry is stored relative to `pivot`; `basis` holds
// the orientation as three column vectors.
struct TerrainSlab {
    std::vector<TerrainVertex> visual;
    std::vector<Triangle> collision;
    std::vector<std::pair<unsigned,Vec3>> riders;
    std::vector<Vec3> samples;      // contact points (relative), lowest first
    Vec3 pivot,offset,previous_offset,velocity,spin;
    std::array<Vec3,3> basis{{{1,0,0},{0,1,0},{0,0,1}}},previous_basis{{{1,0,0},{0,1,0},{0,0,1}}};
    unsigned record{},age{},rest{};
    unsigned shard=UINT32_MAX;   // index into the record's shard poses (UINT32_MAX: single slab)
    unsigned hold{};             // ticks before it breaks away (shards crumble off one by one)
    float radius{};              // bounding radius about the pivot
    // A toppling piece (a sliceable volume with a fall direction) turns about
    // the stump edge on its fall side, under gravity, until it meets the floor.
    bool toppling=false;
    Vec3 hinge,hinge_axis;
    float hinge_angle{},hinge_speed{},hinge_length{};
    Vec3 transform(Vec3 local,float alpha=1)const;
};
struct TerrainDebris {
    std::vector<TerrainVertex> vertices;
    Vec3 previous,position,velocity,previous_rotation,rotation,spin;
    float radius{};
    unsigned age{},lifetime=300;
};
struct TerrainDust {Vec3 previous,position,velocity;float radius{};unsigned age{},lifetime=36;};
class TerrainDestruction {
public:
    static constexpr unsigned max_impacts=64,max_debris=128,min_shards=4,max_shards=8;
    bool impact(Game& game,Vec3 point,Vec3 normal,TerrainAttack attack,const ObjectModels* models,bool fragment=false);
    // Demon eruptions: textured floor chips and dust that keep the floor's atlas
    // UVs and colours. No terrain is removed; works with destruction disabled.
    unsigned emit_chips(Game& game,Vec3 center,float radius,unsigned count,unsigned seed);
    // Convexity: cut along the plane through `hit` (horizontal, tilted away
    // from the beam, or through the beam and its sweep). Ground struck below
    // the mouth (`surface` = its normal) is lifted out as a disc, cut level just
    // under the surface. Returns false when disabled, protected or empty.
    bool slice(Game& game,Vec3 mouth,Vec3 hit,Vec3 beam,Vec3 sweep,const ObjectModels* models,Vec3 surface={});
    // Convexity erosion: a small superflame-style scar (void_erode setting).
    bool erode(Game& game,Vec3 point,Vec3 normal,const ObjectModels* models);
    std::span<const TerrainSlab> slabs()const{return slabs_;}
    void tick(Game& game);
    void clear(Level& level);
    void clear(Game& game);
    void restore(Game& game,std::span<const TerrainDamage> damage,const ObjectModels* models);
    // Headbash edits the underlying floor first. Reapply the wall/blast layer
    // afterwards, so both live impacts and save reconstruction use one order.
    void suspend(Level& level);
    void resume(Game& game,const ObjectModels* models);
    std::span<const TerrainDamage> damage()const{return damage_;}
    std::span<const TerrainDebris> debris()const{return debris_;}
    std::span<const TerrainDust> dust()const{return dust_;}
    std::span<const std::array<Vec3,3>> cracks()const{return cracks_;}
    unsigned changed_triangles{},chunks_emitted{},protected_impacts{},slices{},slabs_landed{};
    unsigned shards_made{},infill_faces{};   // Convexity shards created; packed-ground infill faces added
    static float radius(const TerrainDamage& damage);
private:
    bool carve(Game& game,const TerrainDamage& damage,const ObjectModels* models,bool replay);
    bool carve_slice(Game& game,const TerrainDamage& damage,const ObjectModels* models,bool replay);
    void land(Game& game,TerrainSlab& slab);
    void land_geometry(Level& level,TerrainSlab& slab);
    Vec3 slice_push_{0,0,1};
    void tick_slabs(Game& game);
    std::vector<TerrainSlab> slabs_;
    void refresh_supports(Game& game);
    std::vector<TerrainDamage> damage_;
    std::vector<TerrainDebris> debris_;
    std::vector<TerrainDust> dust_;
    std::vector<std::array<Vec3,3>> cracks_;
    std::map<unsigned,Triangle> original_triangles_;
    std::map<unsigned,std::array<TerrainVertex,3>> original_visual_;
    struct Support {float height{},offset{};};
    std::map<unsigned,Support> original_supports_;
    std::size_t initial_triangles_{},initial_visual_{};
    bool suspended_=false;
};
} // namespace spyro
