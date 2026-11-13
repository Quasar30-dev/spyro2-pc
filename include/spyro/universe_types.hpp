#pragma once
#include "spyro/math.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace spyro {
inline constexpr unsigned universe_generator_version=1;
struct UniverseAddress {
    std::uint64_t seed=2026,world=0;
    bool space=false;
    bool operator==(const UniverseAddress&)const=default;
};
struct UniversePortal {
    Vec3 center;
    float radius=1.8f;
    UniverseAddress destination;
    std::string name;
    Vec3 color{0.4f,0.7f,1};
    unsigned object=UINT32_MAX;
    float scale=1;
    std::array<Vec3,5> aperture{};
};
struct PortalVertex {Vec3 position,color;};
struct ReturnPortalAssets {
    std::array<Vec3,5> aperture{};
    std::vector<PortalVertex> picture;
};
struct UniverseFootprint {
    Bounds bounds;
    unsigned object=UINT32_MAX;
    bool structure=false;
};
struct UniverseStar {Vec3 position,color;float radius{};};
struct UniversePlanet {Vec3 center;float radius{};unsigned portal{};};
struct UniverseLayout {
    UniverseAddress address;
    std::string name,biome;
    Vec3 landing;
    float landing_radius=5,route_clearance=2.5f;
    std::vector<Vec3> route;
    std::vector<UniversePortal> portals;
    std::vector<UniverseFootprint> footprints;
    std::vector<UniverseStar> stars;
    std::vector<UniversePlanet> planets;
    std::vector<unsigned> objective_gems;
    std::array<unsigned,3> materials{};
    unsigned generation_attempt{},enemies{},structures{};
    bool validated=false;
    ReturnPortalAssets return_portal;
};
} // namespace spyro
