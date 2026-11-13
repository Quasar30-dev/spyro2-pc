#pragma once
#include "spyro/math.hpp"
#include <string>
#include <vector>
namespace spyro {
enum class HomeworldKind : unsigned { Shuttle=1, PressureLift=2, SinkingSlab=3, FlameRelay=4 };
struct HomeworldMesh {
    unsigned vertex{},vertices{},triangle{},triangles{};
    std::vector<Vec3> positions,colors,collision;
};
struct HomeworldPiece {
    HomeworldKind kind{};unsigned objective=7;
    std::string name;Vec3 center,extent,axis;
    float travel{},period{},offset{};unsigned age{};bool active=false;
    std::vector<HomeworldMesh> meshes;
};
struct HomeworldTrial {
    unsigned objective{},limit{},remaining{};Vec3 start;
    std::string name;bool active=false,complete=false;
};
}
