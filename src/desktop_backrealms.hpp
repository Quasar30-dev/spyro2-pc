#pragma once
// Optional pre-production presentation. Retail levels and saves are untouched.
inline void backrealms_night(spyro::Level& level) {
    using namespace spyro;
    for(auto& v:level.visual_vertices) {
        const float l=(v.color.x+v.color.y+v.color.z)/3;
        v.color={v.color.x*.24f+l*.08f,v.color.y*.34f+l*.08f,v.color.z*.53f+l*.10f};
    }
    for(auto& morph:level.terrain_morphs)if(morph.color)for(auto& frame:morph.frames)for(auto& c:frame) {
        const float l=(c.x+c.y+c.z)/3;c={c.x*.24f+l*.08f,c.y*.34f+l*.08f,c.z*.53f+l*.10f};
    }
    level.sky_color={.012f,.017f,.044f};
    for(auto& v:level.sky_vertices) {
        const float h=std::clamp(normalized(v.direction).y*2,0.0f,1.0f);
        v.color=lerp(Vec3{.055f,.065f,.13f},Vec3{.008f,.012f,.034f},h);
    }
    // A small cold moon, authored as sky geometry, follows the camera exactly
    // like the retail sky. Never alters collision or portal visibility.
    const unsigned first=static_cast<unsigned>(level.sky_vertices.size());
    const Vec3 center=normalized(Vec3{-.5f,.55f,.7f})*4;
    const auto right=normalized(cross(Vec3{0,1,0},center))*.055f;
    const auto up=normalized(cross(center,right))*.055f;
    for(unsigned i=0;i<16;++i) {
        const float a=i*6.283185307f/16,b=(i+1)*6.283185307f/16;
        level.sky_vertices.push_back({center,{.63f,.70f,.83f}});
        level.sky_vertices.push_back({center+right*std::cos(a)+up*std::sin(a),{.43f,.49f,.65f}});
        level.sky_vertices.push_back({center+right*std::cos(b)+up*std::sin(b),{.43f,.49f,.65f}});
    }
    if(level.sky_regions.size()<64) {level.sky_regions.push_back({first,48});level.sky_visibility.clear();}
    else level.sky_vertices.resize(first);
}
