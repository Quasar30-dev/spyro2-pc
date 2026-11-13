// Frozen water: Ice turns a whole body of water into skateable ice.
//
// A body is found from the touched point: an elemental flood / pocket, the
// custom world's water volumes sharing that surface (connected boxes), or the
// connected retail water surface triangles. The water itself is kept; a solid
// sheet of retail surface type 4 caps it. Spyro skates there exactly as on
// Winter Tundra's frozen pond (overlay 56 state 9, clip 9). Swimmers caught at
// the surface are lifted onto the ice; divers stay under it. Ice lasts
// ice_life ticks, then thaws over ice_melt (sooner when fire touches it).
#include "spyro/elements.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

namespace spyro {
namespace {
constexpr float tile=1.6f,lift=0.012f;
// Clip a convex polygon (XZ) to a 1.6-unit cell grid; emit render triangles
// at height h (or on the source triangle's plane, lifted) with UVs inside each
// cell (the ice texture never wraps).
void grid_fill(FrozenWater& f,const std::vector<Vec3>& poly,float h,const Triangle* plane=nullptr) {
    const auto n=plane?cross(plane->b-plane->a,plane->c-plane->a):Vec3{};
    const auto height=[&](Vec3 v){return plane?plane->a.y-(n.x*(v.x-plane->a.x)+n.z*(v.z-plane->a.z))/n.y+lift:h;};
    float x0=1e30f,x1=-1e30f,z0=1e30f,z1=-1e30f;
    for(const auto& p:poly){x0=std::min(x0,p.x);x1=std::max(x1,p.x);z0=std::min(z0,p.z);z1=std::max(z1,p.z);}
    const auto clip=[](std::vector<Vec3> in,int axis,float k,bool above) {
        std::vector<Vec3> out;
        for(std::size_t i=0;i<in.size();++i) {
            const auto a=in[i],b=in[(i+1)%in.size()];
            const float va=axis?a.z:a.x,vb=axis?b.z:b.x;
            const bool ia=above?va>=k:va<=k,ib=above?vb>=k:vb<=k;
            if(ia)out.push_back(a);
            if(ia!=ib)out.push_back(lerp(a,b,(k-va)/(vb-va)));
        }
        return out;
    };
    for(int i=int(std::floor(x0/tile));i<=int(std::floor(x1/tile));++i)for(int j=int(std::floor(z0/tile));j<=int(std::floor(z1/tile));++j) {
        const float cx=i*tile,cz=j*tile;
        auto p=clip(poly,0,cx,true);if(p.size()>2)p=clip(p,0,cx+tile,false);
        if(p.size()>2)p=clip(p,1,cz,true);if(p.size()>2)p=clip(p,1,cz+tile,false);
        if(p.size()<3)continue;
        for(std::size_t k=1;k+1<p.size();++k)for(const auto& v:{p[0],p[k],p[k+1]}) {
            f.sheet.push_back({v.x,height(v),v.z});
            f.sheet_uv.push_back({std::clamp((v.x-cx)/tile,0.0f,1.0f),std::clamp((v.z-cz)/tile,0.0f,1.0f)});
        }
    }
}
float triangle_height(const Triangle& t,Vec3 p,bool& inside) {
    const auto a=t.b-t.a,b=t.c-t.a,q=p-t.a;
    const float det=a.x*b.z-a.z*b.x;inside=false;if(std::abs(det)<1e-8f)return 0;
    const float u=(q.x*b.z-q.z*b.x)/det,v=(a.x*q.z-a.z*q.x)/det;
    inside=u>=-1e-4f && v>=-1e-4f && u+v<=1.0001f;
    return t.a.y+u*a.y+v*b.y;
}
}
bool FrozenWater::contains(Vec3 p,float margin)const {
    return p.x>=bounds.min.x-margin && p.x<=bounds.max.x+margin && p.z>=bounds.min.z-margin && p.z<=bounds.max.z+margin &&
        p.y>bounds.min.y-1.6f-margin && p.y<bounds.max.y+1.6f+margin;
}
namespace {
// Water faces that can freeze over: pond, lake and river surfaces, not falls.
bool freezable(const Triangle& t) {
    const auto n=cross(t.b-t.a,t.c-t.a);const float l=length(n);
    return t.water && !t.disabled && l>1e-6f && std::abs(n.y)/l>=0.5f;
}
}
unsigned ElementSystem::ice_triangle(Game& g,Vec3 a,Vec3 b,Vec3 c) {
    Triangle t{a,b,c,0,false,4};
    if(!ice_free_.empty()) {
        const auto id=ice_free_.back();ice_free_.pop_back();t.source_id=id;g.level.triangles[id]=t;++g.level.collision_revision;return id;
    }
    t.source_id=static_cast<std::uint32_t>(g.level.triangles.size());g.level.triangles.push_back(t);return t.source_id;
}
void ElementSystem::finish_freeze(Game& g,FrozenWater f) {
    f.life=ice_life;f.center=f.bounds.center();f.center.y=f.height;
    // Surface swimmers are lifted onto the new ice (its local height: rivers
    // slope); divers stay beneath it.
    auto& p=g.player;
    if(p.swim.mode==WaterMode::Surface && f.contains(p.position,.5f)) {
        const auto hit=g.level.raycast(p.position+Vec3{0,1.2f,0},{0,-1,0},2.6f);
        if(hit && hit->surface_type==4 && std::abs(p.swim.height+lift-hit->point.y)<.3f &&
           std::find(f.collision.begin(),f.collision.end(),hit->triangle)!=f.collision.end()) {
            p.swim={};p.position.y=p.previous_position.y=hit->point.y+.01f;p.velocity.y=0;p.grounded=false;
        }
    }
    // A frost crust bursting across the surface.
    for(unsigned k=0;k<24;++k) {
        const float u=float(random_.next()%1000)/1000,v=float(random_.next()%1000)/1000;
        const Vec3 at{std::lerp(f.bounds.min.x,f.bounds.max.x,u),f.height+.05f,std::lerp(f.bounds.min.z,f.bounds.max.z,v)};
        if(length(at-g.player.position)<14)spray(at,{0,.8f,0},Element::Ice,2,.6f,.08f,EffectSprite::Crystal,36,1,1);
    }
    g.sound_events.push_back({SoundScope::Global,0x1d,0,{}});
    ice_.push_back(std::move(f));++frozen_bodies;
}
bool ElementSystem::freeze_at(Game& g,Vec3 p) {
    for(const auto& f:ice_)if(f.contains(p,.3f) && !f.melt)return false;
    auto& level=g.level;
    // 1. Elemental water: the Water Fury flood or a headbash pocket.
    for(auto& w:water_) {
        if(w.frozen || w.height<w.floor+.15f || std::abs(p.y-w.height)>1.4f)continue;
        if(!w.flood && std::hypot(p.x-w.center.x,p.z-w.center.z)>w.radius)continue;
        FrozenWater f;f.height=w.height+lift;f.elemental=w.id;w.frozen=true;
        if(w.flood) {
            const auto& b=level.bounds;
            const Vec3 a{b.min.x,f.height,b.min.z},c{b.max.x,f.height,b.max.z};
            f.collision.push_back(ice_triangle(g,a,{c.x,f.height,a.z},c));f.collision.push_back(ice_triangle(g,a,c,{a.x,f.height,c.z}));
            // Around Spyro the sheet renders; the frozen level beyond reads through its flood surface.
            const Vec3 o{g.player.position.x,f.height,g.player.position.z};
            const std::vector<Vec3> square{o+Vec3{-24,0,-24},o+Vec3{24,0,-24},o+Vec3{24,0,24},o+Vec3{-24,0,24}};
            grid_fill(f,square,f.height);f.bounds.add(a);f.bounds.add(c);
        } else {
            std::vector<Vec3> ring;
            for(unsigned k=0;k<20;++k){const float t=k*2*std::numbers::pi_v<float>/20;ring.push_back({w.center.x+std::sin(t)*w.radius,f.height,w.center.z+std::cos(t)*w.radius});}
            for(unsigned k=0;k<20;++k)f.collision.push_back(ice_triangle(g,{w.center.x,f.height,w.center.z},ring[k],ring[(k+1)%20]));
            grid_fill(f,ring,f.height);for(const auto& v:ring)f.bounds.add(v);
        }
        finish_freeze(g,std::move(f));return true;
    }
    // 2. Custom-world water volumes sharing this surface.
    const auto& vols=level.water_volumes;
    std::optional<unsigned> seed;
    for(unsigned i=0;i<vols.size();++i) {
        const auto& v=vols[i];const auto& b=v.bounds;
        if(p.x<b.min.x || p.x>b.max.x || p.z<b.min.z || p.z>b.max.z || b.max.y<v.height-.05f)continue;
        if(std::abs(p.y-v.height)>1.4f)continue;
        if(std::any_of(ice_.begin(),ice_.end(),[&](const auto& f){return std::find(f.volumes.begin(),f.volumes.end(),i)!=f.volumes.end();}))return false;
        seed=i;break;
    }
    if(seed) {
        FrozenWater f;const float h=vols[*seed].height;f.height=h+lift;
        std::vector<unsigned> open{*seed};std::vector<bool> seen(vols.size());seen[*seed]=true;
        while(!open.empty()) {
            const auto i=open.back();open.pop_back();f.volumes.push_back(i);
            for(unsigned j=0;j<vols.size();++j) {
                if(seen[j] || std::abs(vols[j].height-h)>.02f || vols[j].bounds.max.y<h-.05f)continue;
                const auto& a=vols[i].bounds;const auto& b=vols[j].bounds;
                if(a.min.x>b.max.x+.05f || b.min.x>a.max.x+.05f || a.min.z>b.max.z+.05f || b.min.z>a.max.z+.05f)continue;
                seen[j]=true;open.push_back(j);
            }
        }
        // One sheet over the union of the boxes' surfaces (no overlapping cells).
        for(const auto i:f.volumes) {
            const auto& b=vols[i].bounds;
            const Vec3 a{b.min.x,f.height,b.min.z},c{b.max.x,f.height,b.max.z};
            f.collision.push_back(ice_triangle(g,a,{c.x,f.height,a.z},c));f.collision.push_back(ice_triangle(g,a,c,{a.x,f.height,c.z}));
            f.bounds.add(a);f.bounds.add(c);
        }
        for(int i=int(std::floor(f.bounds.min.x/tile));i<=int(std::floor(f.bounds.max.x/tile));++i)
            for(int j=int(std::floor(f.bounds.min.z/tile));j<=int(std::floor(f.bounds.max.z/tile));++j) {
                const float cx=i*tile+tile/2,cz=j*tile+tile/2;
                const bool in=std::any_of(f.volumes.begin(),f.volumes.end(),[&](unsigned k){const auto& b=vols[k].bounds;
                    return cx>=b.min.x && cx<=b.max.x && cz>=b.min.z && cz<=b.max.z;});
                if(!in)continue;
                grid_fill(f,{{i*tile,0,j*tile},{(i+1)*tile,0,j*tile},{(i+1)*tile,0,(j+1)*tile},{i*tile,0,(j+1)*tile}},f.height);
            }
        finish_freeze(g,std::move(f));return true;
    }
    // 3. Retail water surface triangles connected to the touched one.
    std::optional<unsigned> start;
    for(const auto i:level.collision_candidates(p.x,p.z,p.x,p.z)) {      // Frost nodes probe every few ticks
        if(start)break;
        const auto& t=level.triangles[i];if(!freezable(t))continue;
        bool inside=false;const float y=triangle_height(t,p,inside);
        if(inside && std::abs(p.y-y)<1.4f)start=i;
    }
    if(!start)return false;
    for(const auto& f:ice_)if(std::find(f.surfaces.begin(),f.surfaces.end(),*start)!=f.surfaces.end())return false;
    std::map<std::tuple<int,int,int>,std::vector<unsigned>> by_vertex;
    const auto key=[](Vec3 v){return std::tuple<int,int,int>{int(std::lround(v.x*200)),int(std::lround(v.y*200)),int(std::lround(v.z*200))};};
    for(unsigned i=0;i<level.triangles.size();++i) {
        const auto& t=level.triangles[i];if(!freezable(t))continue;
        for(const auto v:{t.a,t.b,t.c})by_vertex[key(v)].push_back(i);
    }
    FrozenWater f;std::vector<unsigned> open{*start};std::vector<bool> seen(level.triangles.size());seen[*start]=true;
    while(!open.empty()) {
        const auto i=open.back();open.pop_back();f.surfaces.push_back(i);
        const auto t=level.triangles[i];
        for(const auto v:{t.a,t.b,t.c})for(const auto j:by_vertex[key(v)])if(!seen[j]){seen[j]=true;open.push_back(j);}
    }
    float sum=0;
    for(const auto i:f.surfaces) {
        const auto t=level.triangles[i];
        f.collision.push_back(ice_triangle(g,t.a+Vec3{0,lift,0},t.b+Vec3{0,lift,0},t.c+Vec3{0,lift,0}));
        grid_fill(f,{t.a,t.b,t.c},0,&t);        // the sheet follows each surface (rivers slope)
        sum+=(t.a.y+t.b.y+t.c.y)/3;
        for(const auto v:{t.a,t.b,t.c})f.bounds.add(v);
    }
    f.height=sum/f.surfaces.size()+lift;
    finish_freeze(g,std::move(f));return true;
}
unsigned ElementSystem::freeze_all(Game& g) {
    unsigned count=0;
    for(auto& w:water_)if(!w.frozen && freeze_at(g,{w.flood?g.player.position.x:w.center.x,w.height,w.flood?g.player.position.z:w.center.z}))++count;
    for(const auto& v:g.level.water_volumes)if(v.bounds.max.y>=v.height-.05f && freeze_at(g,{(v.bounds.min.x+v.bounds.max.x)/2,v.height,(v.bounds.min.z+v.bounds.max.z)/2}))++count;
    for(unsigned i=0;i<g.level.triangles.size();++i) {
        const auto& t=g.level.triangles[i];if(!freezable(t))continue;
        if(freeze_at(g,(t.a+t.b+t.c)/3))++count;
    }
    return count;
}
bool ElementSystem::thaw_at(Game& g,Vec3 p) {
    for(auto& f:ice_)if(!f.melt && f.contains(p,.2f) && p.y>f.bounds.min.y-1 && p.y<f.bounds.max.y+1) {
        f.melt=ice_melt;spray(p,{0,1.4f,0},Element::Water,6,.6f,.2f,EffectSprite::Steam,40,0,-1);
        g.sound_events.push_back({SoundScope::Global,8,0,{}});return true;
    }
    return false;
}
void ElementSystem::ice_tick(Game& g) {
    for(auto& f:ice_) {
        ++f.age;
        if(!f.melt && f.age>=f.life)f.melt=ice_melt;
        if(f.melt && --f.melt==0) {
            for(const auto id:f.collision){g.level.triangles[id].disabled=true;ice_free_.push_back(id);}
            for(auto& w:water_)if(w.id==f.elemental)w.frozen=false;
            ++thawed_bodies;f.collision.clear();
        }
    }
    std::erase_if(ice_,[](const auto& f){return f.collision.empty();});
    // Fire melts ice: the retail flame's collision points and Ember shots.
    for(const auto point:g.interactions.flame_points())thaw_at(g,point);
    for(const auto& s:shots_)if(s.element==Element::Fire)thaw_at(g,s.position);
}
} // namespace spyro
