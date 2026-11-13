#pragma once
#include "spyro/cinematic.hpp"
#include "spyro/objects.hpp"
#include "spyro/hud.hpp"

namespace spyro {
using TitleFrame=CinematicFrame;
struct TitleAssets : CinematicAssets {
    unsigned width=1024,height=1152;
    Bytes rgba;
    std::array<HudSprite,75> sprites{};
    TitleFrame sample(float ticks) const;
};
TitleAssets decode_title(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> cinematic,
                        std::span<const std::uint8_t> executable);
} // namespace spyro
