#include "spyro/upgrade_hud.hpp"
namespace spyro {
namespace {
#include "generated/upgrade_pixels.inc"
}
HudAssets upgrade_sprite_assets() {
    HudAssets a;a.width=384;a.height=64;a.rgba.resize(a.width*a.height*4);
    static_assert(std::size(upgrade_pixels)==384*64);
    for(unsigned i=0;i<std::size(upgrade_pixels);++i)for(unsigned c=0;c<4;++c)a.rgba[i*4+c]=static_cast<std::uint8_t>(upgrade_pixels[i]>>(c*8));
    for(unsigned i=0;i<6;++i)a.sprites[i]={i*64,0,64,64};
    return a;
}
} // namespace spyro
