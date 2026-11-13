#pragma once
// Original procedural PS1-style sprites for the elemental effects. Every
// sprite is drawn in code at 32x32, quantised to 16 grey levels (4bpp CLUT
// style) and 5-bit RGB, then tinted by vertex colour at draw time.
#include "spyro/assets.hpp"
#include "spyro/elements.hpp"
#include "spyro/hud.hpp"

namespace spyro {
struct ElementSpriteAtlas {
    static constexpr unsigned cell=32,count=10;
    unsigned width=cell*count,height=cell;
    Bytes rgba;
};
ElementSpriteAtlas element_sprite_atlas();
// Generated element HUD art (assets/ui/elements, tools/pack_element_icons.py):
// a 160x48 atlas. Row 0: 32x32 icons in Element order. Row 1 (y 32): the
// power-meter emblems that replace the retail superwing, one per 32x16 cell.
struct ElementIconAtlas {unsigned width=160,height=48;Bytes rgba;bool valid()const{return rgba.size()==std::size_t(width)*height*4;}};
ElementIconAtlas element_icon_atlas();
// Runtime HUD atlas: the icon atlas plus, per element, the mounted retail
// power-meter parts (resident 22..26) in their original colours with only
// their dark outline pixels recoloured. Built at level load from the decoded
// interface, never stored. parts[e][0..4] = top cap, empty cell, bottom cap,
// lit cell, orb.
struct ElementHudAtlas {
    unsigned width=160,height=128;Bytes rgba;
    std::array<HudSprite,5> icons,emblems;
    std::array<std::array<HudSprite,5>,5> parts;
    bool meter=false;
    bool valid()const{return rgba.size()==std::size_t(width)*height*4;}
};
Vec3 element_meter_outline(Element element);
ElementHudAtlas element_hud_atlas(const HudAssets* interface_atlas,const std::array<HudSprite,5>* resident_parts);
inline std::array<float,4> element_sprite_uv(EffectSprite sprite) {
    const float u0=static_cast<float>(static_cast<unsigned>(sprite))/ElementSpriteAtlas::count;
    return {u0+.5f/320,u0+1.0f/ElementSpriteAtlas::count-.5f/320,.5f/32,1-.5f/32};
}
} // namespace spyro
