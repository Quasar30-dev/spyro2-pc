#pragma once
#include "spyro/coordinates.hpp"
#include <array>
#include <cstdint>

namespace spyro::retail {
// Original X/Y/Z-up integer coordinates, used by reconstructed object scripts.
struct IVec3 {
    std::int32_t x{},y{},z{};
    IVec3 operator+(IVec3 b) const {return {x+b.x,y+b.y,z+b.z};}
    IVec3 operator-(IVec3 b) const {return {x-b.x,y-b.y,z-b.z};}
    IVec3 operator/(int n) const {return {x/n,y/n,z/n};}
    bool operator==(const IVec3&) const =default;
};
inline IVec3 from_world(Vec3 p) {
    return {static_cast<int>(std::lround(p.x*units_per_world)),static_cast<int>(std::lround(p.z*units_per_world)),
            static_cast<int>(std::lround(p.y*units_per_world))};
}
inline Vec3 to_world(IVec3 p) {return world_position({static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.z)});}
struct MathTables {
    std::array<std::uint16_t,129> atan{};
    std::array<std::uint16_t,192> square_root{};
    MathTables();
    unsigned angle(int x,int y,unsigned mode=0) const;
    int length(IVec3 v,bool include_z=true) const;
};
int horizontal_distance(IVec3 delta);
IVec3 rescale(IVec3 v,int length,int wanted);
unsigned approach_angle(unsigned current,unsigned target,unsigned step);
class Random {
public:
    explicit Random(std::uint32_t seed=1):state_(seed) {}
    unsigned next() {state_=state_*0x41c64e6du+0x3039u;return (state_>>16)&0x7fff;}
private:
    std::uint32_t state_;
};
} // namespace spyro::retail
