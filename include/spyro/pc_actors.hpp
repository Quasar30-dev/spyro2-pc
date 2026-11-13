#pragma once
// .s2actor: PC-authored PS1-style animated actors (the Convexity demons).
// Little-endian layout, version 1:
//   "S2ACTOR1", u32 kind, vertex_count (<=256: retail 8-bit animated indices),
//   face_count, clip_count, texture width/height (16..128, powers of two),
//   palette_size (16), attachment_count (<=8), sphere_count (<=8), reserved 0;
//   palette u16[16] (PS1 BGR555 + STP; word 0 = transparent), 4bpp pixels
//   (low nibble first); faces[28]: u8 v[4], u8 flags (1 quad, 2 translucent,
//   blend<<2), u8 pad[3], u8 uv[4][2] texels, u8 rgb[4][3] (128 = 1.0);
//   clips: u32 frames, u32 VBlanks/frame, i16 xyz per vertex per frame and per
//   attachment per frame (retail units, 1/1024 world, +Y up, +Z forward);
//   spheres: u16 vertex, u16 radius; trailing CRC32 of every preceding byte.
#include "spyro/objects.hpp"

namespace spyro {
inline constexpr unsigned s2actor_max_vertices=256,s2actor_max_faces=600,s2actor_max_clips=8,s2actor_max_frames=64;
struct PcActor {
    unsigned kind{};
    ObjectModel model;       // face UVs hold texel coordinates until installed
    unsigned texture_width{},texture_height{};
    Bytes texture_rgba;
    std::vector<std::pair<unsigned,unsigned>> spheres;
};
PcActor decode_s2actor(std::span<const std::uint8_t> bytes);
Bytes encode_s2actor_checksum(std::span<const std::uint8_t> body); // body + CRC (tests/tools)
// Embedded demon actors (src/generated/pc_actor_data.inc).
unsigned embedded_pc_actor_count();
std::span<const std::uint8_t> embedded_pc_actor(unsigned index);
// Pack actor textures into ONE appended 256x256 atlas tile and register each
// model as type_base+kind. Existing face UVs are rescaled, as import does.
void install_pc_actors(ObjectModels& models,std::span<const PcActor> actors,std::uint16_t type_base);
void install_embedded_demons(ObjectModels& models);
} // namespace spyro
