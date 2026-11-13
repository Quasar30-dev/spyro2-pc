#include "spyro/element_art.hpp"
#include <cmath>
#include <numbers>

namespace spyro {
namespace {
#include "generated/element_icon_pixels.inc"
constexpr float pi=std::numbers::pi_v<float>;
float clamp01(float v){return std::clamp(v,0.0f,1.0f);}
float hash(unsigned x,unsigned y,unsigned s){unsigned h=x*374761393u+y*668265263u+s*2246822519u;h=(h^(h>>13))*1274126177u;return float(h&0xffff)/65535.0f;}
// Each returns intensity 0..1 at pixel centre (u,v in -1..1).
float droplet(float u,float v){const float body=1-std::hypot(u,(v-.18f)*1.15f)/.62f;const float tip=v<-.1f?1-std::abs(u)/(.62f*clamp01((v+.95f)/.85f)+.001f):-1;
    const float d=std::max(body,tip*clamp01((v+.95f)*1.2f));return d<=0?0:clamp01(.45f+d*.9f-std::max(0.0f,u+.25f)*.6f+(std::hypot(u+.2f,v-.05f)<.14f?.6f:0));}
float foam(float u,float v,unsigned){float best=0;const float c[5][3]{{0,0,.55f},{-.38f,.25f,.4f},{.4f,.22f,.38f},{-.2f,-.42f,.36f},{.3f,-.38f,.34f}};
    for(const auto& b:c){const float d=1-std::hypot(u-b[0],v-b[1])/b[2];best=std::max(best,d);}return best<=0?0:clamp01(.55f+best*.7f);}
float snowflake(float u,float v){const float r=std::hypot(u,v),a=std::atan2(v,u);if(r>.95f)return 0;
    const float arm=std::abs(std::sin(a*3));const float spoke=arm<.09f+.04f*(1-r)?1.0f:0;
    const float barb=(std::abs(r-.55f)<.06f || std::abs(r-.3f)<.05f) && arm<.35f?1.0f:0;
    return clamp01(std::max(spoke*(1.1f-r*.5f),barb*.9f)+(r<.12f?1:0));}
float spark(float u,float v){const float a=std::abs(u),b=std::abs(v);const float star=std::max(1-(a*6+b),1-(b*6+a))*1.2f;
    const float diag=std::max(1-(std::abs(u+v)*4+std::abs(u-v)),1-(std::abs(u-v)*4+std::abs(u+v)))*.6f;return clamp01(std::max({star,diag,1-std::hypot(u,v)*2.6f}));}
float mote(float u,float v){const float r=std::hypot(u,v);return clamp01((1-r)*1.4f)*clamp01((1-r)*1.4f);}
float steam(float u,float v,unsigned s){const float r=std::hypot(u,v);const float n=hash(unsigned((u+1)*5),unsigned((v+1)*5),s)*.35f;return clamp01((1-r-n*.6f)*1.6f);}
float ember(float u,float v){const float w=.55f*clamp01((v+1)/1.6f)*clamp01((1.05f-v)*1.4f);const float edge=1-std::abs(u+.12f*std::sin(v*4))/(w+.001f);
    return edge<=0?0:clamp01(edge*1.3f*(.5f+.5f*clamp01((v+1)/1.2f)));}
float flake(float u,float v){const float a=std::abs(u*1.4f+v*.4f)+std::abs(v*1.1f-u*.3f);return a<.85f?clamp01((.85f-a)*3+.35f):0;}
float ring(float u,float v){const float r=std::hypot(u,v);return clamp01(1-std::abs(r-.72f)/.16f);}
float crystal(float u,float v){const float d=std::abs(u)*1.6f+std::abs(v);if(d>.95f)return 0;return clamp01(.45f+(u<0?.45f:.15f)+(d>.8f?.4f:0));}
}
ElementIconAtlas element_icon_atlas() {
    ElementIconAtlas atlas;
    if(std::size(element_icon_pixels)!=std::size_t(atlas.width)*atlas.height)return atlas;
    atlas.rgba.resize(std::size(element_icon_pixels)*4);
    for(std::size_t i=0;i<std::size(element_icon_pixels);++i)for(unsigned c=0;c<4;++c)atlas.rgba[i*4+c]=static_cast<std::uint8_t>(element_icon_pixels[i]>>(c*8));
    return atlas;
}
Vec3 element_meter_outline(Element element) {
    switch(element){case Element::Fire:return {.80f,.26f,.06f};case Element::Water:return {.14f,.42f,.92f};case Element::Ice:return {.50f,.80f,.96f};
        case Element::Electric:return {.92f,.76f,.14f};case Element::Convexity:return {.56f,.22f,.84f};}
    return {1,1,1};
}
ElementHudAtlas element_hud_atlas(const HudAssets* ui,const std::array<HudSprite,5>* parts) {
    ElementHudAtlas out;const auto icons=element_icon_atlas();if(!icons.valid())return out;
    out.rgba.assign(std::size_t(out.width)*out.height*4,0);
    for(unsigned y=0;y<icons.height;++y)std::copy_n(&icons.rgba[std::size_t(y)*icons.width*4],icons.width*4,&out.rgba[std::size_t(y)*out.width*4]);
    for(unsigned k=0;k<element_count;++k) {
        out.icons[k]={k*32,0,32,32};
        // Emblem bounds inside its 32x16 cell (the packer centres it).
        unsigned x0=32,y0=16,x1=0,y1=0;
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x)if(out.rgba[((std::size_t(32+y))*out.width+k*32+x)*4+3]){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
        out.emblems[k]=x1>=x0?HudSprite{k*32+x0,32+y0,x1-x0+1,y1-y0+1}:HudSprite{};
    }
    if(!ui || !parts)return out;
    for(unsigned e=0;e<element_count;++e) {
        const auto outline=element_meter_outline(static_cast<Element>(e));
        for(unsigned p=0;p<5;++p) {
            const auto& s=(*parts)[p];if(!s.width || s.width>32 || s.height>16)continue;
            const unsigned ox=p*32,oy=48+e*16;
            const auto src=[&](int x,int y)->const std::uint8_t*{return &ui->rgba[(std::size_t(s.y+y)*ui->width+s.x+x)*4];};
            const auto clear=[&](int x,int y){return x<0 || y<0 || x>=int(s.width) || y>=int(s.height) || !src(x,y)[3];};
            for(int y=0;y<int(s.height);++y)for(int x=0;x<int(s.width);++x) {
                const auto* in=src(x,y);auto* o=&out.rgba[((std::size_t(oy)+y)*out.width+ox+x)*4];
                std::copy_n(in,4,o);if(!in[3] || p==4)continue;   // the orb keeps its own glow
                // A dark texel on the silhouette edge is outline: recolour it.
                const float lum=in[0]*.30f+in[1]*.59f+in[2]*.11f;
                if(lum<70 && (clear(x-1,y) || clear(x+1,y) || clear(x,y-1) || clear(x,y+1))) {
                    const float shade=.55f+lum/155;
                    o[0]=std::uint8_t(std::min(255.0f,outline.x*255*shade));o[1]=std::uint8_t(std::min(255.0f,outline.y*255*shade));o[2]=std::uint8_t(std::min(255.0f,outline.z*255*shade));
                }
            }
            out.parts[e][p]={ox,oy,s.width,s.height};
        }
    }
    out.meter=true;
    return out;
}
ElementSpriteAtlas element_sprite_atlas() {
    ElementSpriteAtlas atlas;atlas.rgba.assign(std::size_t(atlas.width)*atlas.height*4,0);
    for(unsigned s=0;s<ElementSpriteAtlas::count;++s)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
        const float u=(x+.5f)/16-1,v=1-(y+.5f)/16;float i=0;
        switch(static_cast<EffectSprite>(s)) {
        case EffectSprite::Droplet:i=droplet(u,v);break;
        case EffectSprite::Foam:i=foam(u,v,s);break;
        case EffectSprite::Snowflake:i=snowflake(u,v);break;
        case EffectSprite::Spark:i=spark(u,v);break;
        case EffectSprite::Mote:i=mote(u,v);break;
        case EffectSprite::Steam:i=steam(u,v,s);break;
        case EffectSprite::Ember:i=ember(u,v);break;
        case EffectSprite::Flake:i=flake(u,v);break;
        case EffectSprite::Ring:i=ring(u,v);break;
        case EffectSprite::Crystal:i=crystal(u,v);break;
        }
        // 16-level CLUT quantisation and 5-bit RGB, like a 4bpp PS1 texture.
        const unsigned level=static_cast<unsigned>(std::lround(clamp01(i)*15));
        auto* p=&atlas.rgba[(std::size_t(y)*atlas.width+s*32+x)*4];
        if(!level)continue;
        const unsigned c5=level*31/15;const auto c8=static_cast<std::uint8_t>((c5<<3)|(c5>>2));
        p[0]=p[1]=p[2]=c8;p[3]=255;
    }
    return atlas;
}
} // namespace spyro
