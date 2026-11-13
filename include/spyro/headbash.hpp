#pragma once
#include "spyro/level.hpp"
#include "spyro/element_types.hpp"
#include <map>

namespace spyro {
class Game;
enum class HeadbashMode : unsigned { Normal,Crater,Lava };
const char* headbash_mode_name(HeadbashMode mode);
// `element`/`cooled` (S2SAVE16) record elemental shockwaves over a crater:
// Water/Ice cool lava to safe stone; Convexity tints it void-violet. They are
// recolour/flag changes only and never carve the terrain again.
struct HeadbashStamp {Vec3 center;HeadbashMode mode=HeadbashMode::Crater;Element element=Element::Fire;bool cooled=false;};
struct HeadbashLava {std::array<Vec3,3> vertices;unsigned stamp{};};
// A PC terrain layer. Retail source assets and special-surface IDs stay intact.
class HeadbashTerrain {
public:
    static constexpr float safe_radius=1.05f;
    static constexpr unsigned max_stamps=32;
    bool impact(Game& game,const ObjectModels* models);
    // Lava stamp at `feet` for the Fire Fury; obeys the same budget/protections.
    bool stamp(Game& game,Vec3 feet,HeadbashMode mode,const ObjectModels* models);
    void tick(Game& game,const ObjectModels* models);
    void restore(Game& game,std::span<const HeadbashStamp> stamps,const ObjectModels* models);
    // Elemental wave over existing craters: returns how many stamps changed.
    unsigned apply_element(Vec3 center,float radius,Element element);
    std::span<const HeadbashStamp> stamps()const{return stamps_;}
    std::span<const HeadbashLava> lava()const{return lava_;}
    std::span<const HeadbashLava> shock()const{return shock_;}
    bool burning(Vec3 feet)const;
    unsigned wave_ticks()const{return wave_ticks_;}
    float wave_radius(float alpha=1)const;
    unsigned wave_stamp()const{return wave_stamp_;}
    bool revealing()const{return revealing_;}
    unsigned changed_triangles{},protected_impacts{};
    static float radius(HeadbashMode mode){return mode==HeadbashMode::Lava?4.5f:3.6f;}
private:
    bool carve(Game& game,HeadbashStamp stamp,const ObjectModels* models,bool replay);
    void clear(Level& level);
    std::vector<HeadbashStamp> stamps_;
    std::vector<HeadbashLava> lava_;
    std::vector<HeadbashLava> shock_;
    std::map<unsigned,Triangle> original_triangles_;
    std::map<unsigned,std::array<TerrainVertex,3>> original_visual_;
    std::size_t initial_triangles_{},initial_visual_{};
    unsigned wave_ticks_{},wave_stamp_=UINT32_MAX;
    bool revealing_=false;
};
} // namespace spyro
