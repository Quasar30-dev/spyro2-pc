#pragma once
// Dark Spyro presentation helpers: a PS1-authentic alternate palette built
// from the mounted CLUT colours, eye landmarks and the melee tail tip.
#include "spyro/spyro_model.hpp"
#include <optional>

namespace spyro {
// Landmarks on the verified 230-vertex main mesh (probe: faces 2-5 / 284-286
// sample the white-sclera eye cell of the (page,CLUT) tile 2 in WAD 15).
inline constexpr std::array<unsigned,7> spyro_left_eye{138,139,140,176,177,178,190};
inline constexpr std::array<unsigned,7> spyro_right_eye{126,134,141,142,143,144,145};
// Remap one PS1 15-bit colour word (STP bit kept) to the Dark Spyro ramp.
// Zero stays zero (transparent); luminance order is preserved.
std::uint16_t dark_spyro_color(std::uint16_t bgr555);
// Same remap applied to an already expanded RGBA texel (5-bit exact).
void dark_spyro_texel(std::uint8_t* rgba);
Vec3 dark_spyro_vertex(Vec3 color);
// Atlas rectangle (normalised) of the eye texture cell, from the landmark faces.
struct EyeCell {float u0{},v0{},u1{},v1{};unsigned tile=UINT32_MAX;bool valid()const{return tile!=UINT32_MAX;}};
EyeCell spyro_eye_cell(const SpyroModel& model);
// Dark atlas: CLUT remap of every tile plus flat near-white eye texels.
Bytes dark_spyro_atlas(const SpyroModel& model);
// Eye faces of a clip: landmark vertices on the 230-vertex mesh, otherwise any
// face sampling the eye cell (other meshes such as the retail supercharge).
std::array<std::vector<unsigned>,2> spyro_eye_vertices(const SpyroModel& model,const SpyroAnimation& clip,std::size_t vertex_count);
// Model-space eye centres for a pose (slightly in front of the eye surface).
std::optional<std::array<Vec3,2>> spyro_eye_centers(const SpyroModel& model,const SpyroPose& pose,const SpyroAnimation* clip=nullptr);
// Dark Spyro keeps his horns, gold crest, wings and tail tip in their retail
// colours under a dark tint (gpt-image concept ratio ~0.75/0.76/0.86); the
// rest of the body uses the black atlas. Faces are matched by fixed vertex
// landmarks on the 230-vertex main mesh (valid for every retail bank).
struct DarkSpyroKeep {std::array<bool,230> vertices{};unsigned faces{};bool valid()const{return faces>0;}};
inline constexpr Vec3 dark_spyro_keep_tint{.72f,.72f,.84f};
DarkSpyroKeep dark_spyro_keep(const SpyroModel& model);
bool dark_spyro_kept(const DarkSpyroKeep& keep,const SpyroFace& face,std::size_t vertex_count);
// Tail tip vertex on the main mesh (farthest tail vertex from its root).
unsigned spyro_tail_tip(const SpyroModel& model);
// Snout landmarks on the main mesh: the frontmost head vertices (115..229) of
// the rest pose. Empty for other meshes.
std::vector<unsigned> spyro_snout_vertices(const SpyroModel& model);
// Model-space mouth point (just ahead of and below the snout tip) for a
// 230-vertex pose, so breaths leave the mouth through every head gesture.
std::optional<Vec3> spyro_mouth(const SpyroModel& model,const SpyroPose& pose);
} // namespace spyro
