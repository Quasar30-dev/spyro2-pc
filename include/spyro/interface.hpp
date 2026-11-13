#pragma once
#include "spyro/hud.hpp"
#include <map>
#include <optional>
#include <string_view>
namespace spyro {
struct InterfaceStamp {unsigned x{},y{},sprite{},type{};};
struct RetailGlyph {std::uint16_t descriptor{};std::array<HudSprite,14> colors;};
struct GuideLevel {unsigned scene{},gems{},spirits{},orbs{};std::string name;std::array<unsigned,4> orb_bits{};};
struct RiptoHudAssets {
    HudSprite orb_rim,empty_orb;
    std::array<HudSprite,6> portraits;
    // PS1 modulation can brighten above 1.0; bake the original 70..250
    // intensity range instead of clipping it through ImGui's colour tint.
    std::array<std::array<HudSprite,181>,3> orbs;
};
// Crush / Gulp health HUD (overlay 30 8006fe80, overlay 54 8006f198): the
// scene's portrait frame (80063660, size 21), eye frames (80063690+4i, size
// 18) and health pips (800635f8 + 4*sprite, size 7: 13/14/15 left/middle/
// right, 9/10/11 the empty ones drawn through palette 0x5960).
struct BossHudAssets {
    HudSprite frame;
    std::array<HudSprite,4> eyes;
    std::array<HudSprite,3> full,empty;
};
struct InterfaceAssets : HudAssets {
    std::array<RetailGlyph,128> font;
    std::array<std::uint8_t,46> escapes{};
    std::array<HudSprite,55> resident;
    std::array<HudSprite,26> pause;
    std::array<HudSprite,17> contents;
    std::array<std::array<HudSprite,22>,31> pages;
    std::array<GuideLevel,29> levels;
    std::array<unsigned,29> order{};
    std::array<std::vector<InterfaceStamp>,6> layouts;
    std::array<short,18> dialogue_enter{};
    std::array<short,12> dialogue_exit{};
    std::array<std::array<std::string,4>,4> flight_labels;
    std::array<short,320> page_sine{};
    std::array<std::uint8_t,12> page_angles{};
    std::string continue_arrow;
    std::array<std::string,20> skills;
    std::array<unsigned,20> skill_levels{};
    std::optional<RiptoHudAssets> ripto;
    std::optional<BossHudAssets> boss;
    // The level's interface VRAM before the guidebook uploads (scene-local
    // HUD sprites live there), and the sprites decoded from it on demand for
    // the level's own challenge HUD (RetailScripts::hud_draws). `changed`
    // asks the renderer to upload the grown atlas again.
    Bytes scene_vram;
    std::map<std::pair<std::uint64_t,std::uint32_t>,HudSprite> scene_sprites;
    unsigned atlas_x=1,atlas_y=1,atlas_row=0;bool changed=false;
};
// A sprite of texture page 0x38 (the HUD page) from the level's VRAM: (u, v),
// CLUT word, size, colour (0x80 per channel is neutral) and semi-transparency.
const HudSprite& scene_sprite(InterfaceAssets& assets,unsigned u,unsigned v,unsigned clut,unsigned width,unsigned height,unsigned color,bool half);
// Retail level child 0 holds the pause bank after its 0x80000-byte VRAM.
// WAD 4 is the 32 x 0xc000 guidebook page bank (US English).
InterfaceAssets decode_interface(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> exe,std::span<const std::uint8_t> book,bool ripto=false,bool boss=false);
struct TextGlyph {unsigned character{},color{},source_end{};float x{},y{};};
struct RetailText {std::vector<TextGlyph> glyphs;float width{},height=9;unsigned lines=1;};
RetailText layout_retail_text(const InterfaceAssets& assets,std::string_view text,unsigned color=0,float wrap=0,bool dense=false);
std::string retail_pc_controls(std::string_view text,bool controller=false);
unsigned guide_completion(const GuideLevel& level,unsigned gems,unsigned orbs,bool talisman);
std::array<float,2> project_book_point(const InterfaceAssets& assets,float x,float y,float angle);
} // namespace spyro
