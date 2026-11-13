#pragma once
#include "spyro/game_build.hpp"

namespace spyro {
struct YotdVramPatch {
    unsigned x{},y{},width{},height{};
    unsigned rectangle_offset{},source_offset{};
};
struct YotdVramImage {
    // 512x512 16-bit pixels covering original PS1 VRAM x512..1023.
    Bytes pixels;
    std::optional<YotdVramPatch> patch;
};
YotdVramImage decode_yotd_vram(std::span<const std::uint8_t> archive,const GameProfile& profile,unsigned area=0);
} // namespace spyro
