#pragma once
#include "spyro/math.hpp"
#include <filesystem>
#include <vector>

namespace spyro {
// Unlit sRGB degree-0 3D Gaussian; covariance includes rotation and scale.
// Explicit 16-byte groups match the D3D11 structured-buffer ABI.
struct GaussianSplat {
    Vec3 position; float opacity{};
    Vec3 color; float reserved0{};
    Vec3 diagonal; float reserved1{};
    Vec3 off_diagonal; float reserved2{}; // xy, xz, yz
};
static_assert(sizeof(GaussianSplat)==64);
struct SplatSurfaceVertex {Vec3 position;float u{},v{};Vec3 color{1,1,1};};
static_assert(sizeof(SplatSurfaceVertex)==32);
struct SplatSurface {
    std::vector<SplatSurfaceVertex> vertices;
    std::vector<std::uint8_t> rgba;
    unsigned width{},height{};
};
std::vector<GaussianSplat> load_splats(const std::filesystem::path& path,SplatSurface* surface=nullptr);
}
