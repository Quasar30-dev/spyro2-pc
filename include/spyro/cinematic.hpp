#pragma once
#include "spyro/level.hpp"
#include "spyro/objects.hpp"
#include "spyro/audio.hpp"

namespace spyro {
struct CinematicFrame {
    Vec3 eye,forward,up;
    std::vector<RetailObject> actors;
    std::vector<ObjectPose> poses;
};
struct CinematicAssets {
    Level level;
    ObjectModels models;
    Bytes cinematic;
    std::vector<unsigned> chunks;
    unsigned duration{};
    PcmAudio sound;
    CinematicFrame sample(float ticks,bool title_camera=false) const;
};
// The executable maps scenery slots to movie pairs. Glimmer has no intro;
// its index zero is also used by the title sequence.
std::optional<unsigned> cinematic_slot(std::span<const std::uint8_t> exe,unsigned scene,bool outro=false);
CinematicAssets decode_cinematic(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> movie,
                                 std::span<const std::uint8_t> exe,unsigned scene);
} // namespace spyro
