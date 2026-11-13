// Swimmable elemental water: the Water Fury flood and Water headbash pockets.
// Surfaces are mirrored into Level::dynamic_water, so the retail swimming code
// (Game::update_water / move_in_water) treats them exactly like level water.
#include "spyro/elements.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>
#include <numbers>

namespace spyro {
namespace {
float ease(float t){t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t);}
float floor_below(const Game& g,Vec3 at,float reach,float fallback) {
    if(const auto f=g.level.raycast(at+Vec3{0,.5f,0},{0,-1,0},reach))return f->point.y;
    return fallback;
}
}
// The Crownworks (plan 6.4): a Water Fury floods only the middle terrace, its
// pools included; the lower ring and the crown walk stay dry. The terrace is
// every column whose floor lies in its band (3.1..3.8), plus pool water.
static std::shared_ptr<const WaterCells> crownworks_terrace(const Level& level) {
    constexpr float terrace=3.5f,cell=.5f;
    auto cells=std::make_shared<WaterCells>();const auto& b=level.bounds;
    cells->origin={b.min.x,terrace,b.min.z};cells->cell=cell;
    cells->columns=static_cast<unsigned>(std::ceil((b.max.x-b.min.x)/cell));cells->rows=static_cast<unsigned>(std::ceil((b.max.z-b.min.z)/cell));
    cells->wet.assign(std::size_t(cells->columns)*cells->rows,0);
    for(unsigned j=0;j<cells->rows;++j)for(unsigned i=0;i<cells->columns;++i) {
        const float x=b.min.x+(i+.5f)*cell,z=b.min.z+(j+.5f)*cell;
        const auto f=level.raycast({x,terrace+2.5f,z},{0,-1,0},12);
        const bool terrace_floor=f && f->normal.y>.6f && f->point.y>terrace-.4f && f->point.y<terrace+.3f;
        const auto pool=level.retail_water_at({x,terrace-.3f,z});
        if(terrace_floor || (pool && pool->height>terrace-1.5f))cells->wet[std::size_t(j)*cells->columns+i]=1;
    }
    return cells;
}
void ElementSystem::flood(Game& g) {
    // One flood at a time: a new Water Fury restarts it from the current level.
    std::erase_if(water_,[](const auto& w){return w.flood && !w.frozen;});
    ElementWater w;w.flood=true;w.center=g.player.position;w.id=++water_ids_;
    if(g.level.custom_arena && g.level.custom_arena->ripto_encounter) {
        if(!terrace_ || terrace_level_!=&g.level){terrace_=crownworks_terrace(g.level);terrace_level_=&g.level;}
        w.cells=terrace_;w.floor=3.25f;w.height=w.previous_height=w.floor;w.peak=4.9f;
    } else {
        const float base=floor_below(g,g.player.position,3,g.player.position.y);
        w.floor=base-.6f;w.height=w.previous_height=w.floor;w.peak=base+3.4f;
    }
    w.rise=180;w.hold=720;w.drain=2700;   // 3 s rise, 12 s full, 45 s slow drain
    water_.push_back(w);++floods;
}
void ElementSystem::pocket(Game& g,Vec3 feet) {
    ElementWater w;w.center=feet;w.radius=2.7f;w.floor=floor_below(g,feet,2,feet.y);w.id=++water_ids_;
    w.height=w.previous_height=w.floor;w.peak=w.floor+1.6f;w.rise=24;w.hold=600;w.drain=360;
    // Beyond the cap a new pocket replaces the oldest pocket (never the flood).
    if(std::count_if(water_.begin(),water_.end(),[](const auto& x){return !x.flood;})>=static_cast<long long>(max_pockets))
        for(auto it=water_.begin();it!=water_.end();++it)if(!it->flood && !it->frozen){water_.erase(it);break;}
    water_.push_back(w);++pockets;
}
void ElementSystem::water_tick(Game& g,const SpyroModel* spyro) {
    for(auto& w:water_) {
        w.previous_height=w.height;
        if(w.frozen)continue;          // ice holds the surface until it thaws
        ++w.age;
        if(w.age<=w.rise)w.height=w.floor+(w.peak-w.floor)*ease(float(w.age)/float(w.rise));
        else if(w.age<=w.rise+w.hold)w.height=w.peak;
        else w.height=w.peak-(w.peak-w.floor)*std::min(1.0f,float(w.age-w.rise-w.hold)/float(w.drain));
        const auto rnd=[&]{return float(random_.next()%2001)/1000.0f-1;};
        if(w.flood) {
            // The flood surges in around Spyro, then bubbles while it drains.
            if(w.age<=w.rise && phase_%2==0) {
                const float a=float(random_.next()%6283)/1000.0f,r=2+float(random_.next()%700)/100.0f;
                const auto at=Vec3{g.player.position.x+std::sin(a)*r,w.height,g.player.position.z+std::cos(a)*r};
                spray(at,{0,2.2f,0},Element::Water,2,1.2f,.06f,EffectSprite::Droplet,28,0,9);
                spray(at,{0,.3f,0},Element::Water,1,.4f,.3f,EffectSprite::Foam,24,0);
            } else if(phase_%7==0) {
                const auto at=g.player.position+Vec3{rnd()*5,0,rnd()*5};
                if(at.y<w.height-.3f)spray(Vec3{at.x,std::min(w.height-.2f,at.y+.4f),at.z},{0,1.1f,0},Element::Water,1,.2f,.05f,EffectSprite::Foam,40,0,-.5f);
            }
        } else if(phase_%4==0 && w.height>w.floor+.05f) {
            const float a=float(random_.next()%6283)/1000.0f;
            spray(w.center+Vec3{std::sin(a)*w.radius,w.height-w.center.y,std::cos(a)*w.radius},{0,.5f,0},Element::Water,1,.4f,.16f,EffectSprite::Foam,20,0);
        }
    }
    std::erase_if(water_,[](const auto& w){return !w.frozen && w.age>=w.rise+w.hold+w.drain;});
    g.level.dynamic_water.clear();
    for(const auto& w:water_)g.level.dynamic_water.push_back({w.center,w.flood?0.0f:w.radius,w.floor,w.height,w.flood,w.cells});
    // Elemental water is swimmable in every level whose Spyro bank carries the
    // swim clips (the workbench imports them); retail flags return afterwards.
    if(!water_.empty()) {
        if(!swim_flags_saved_){saved_surface_=g.level.surface_swimming;saved_underwater_=g.level.underwater_swimming;swim_flags_saved_=true;}
        if(spyro && spyro->animations.contains(44))g.level.surface_swimming=true;
        if(spyro && spyro->animations.contains(40))g.level.underwater_swimming=true;
    } else if(swim_flags_saved_) {
        g.level.surface_swimming=saved_surface_;g.level.underwater_swimming=saved_underwater_;swim_flags_saved_=false;
    }
    if(water_.empty())return;
    // Dry out: once a draining surface sinks to Spyro's floor he stands again.
    auto& swim=g.player.swim;
    if(swim.mode!=WaterMode::Dry) {
        const auto origin=g.player.position+retail::origin_above_feet;
        if(const auto f=g.level.raycast(origin+Vec3{0,.2f,0},{0,-1,0},4.0f);f && f->point.y>swim.height-.25f) {
            swim.mode=WaterMode::Dry;swim.submerged=false;g.player.grounded=false;g.player.velocity.y=0;
            g.player.position.y=std::max(g.player.position.y,f->point.y);
        }
    }
    // Enemies standing in elemental water stay soaked (Arc chains twice as far).
    if(phase_%30==0)
        for(auto& o:g.retail_objects)if(o.enemy && !o.enemy->defeated && element_target(o))
            if(const auto surface=g.level.water_at(o.position+Vec3{0,.3f,0});surface && surface->height>o.position.y+.3f)
                queue_element_hit(o,{Element::Water,0,o.position,element_status::soak,false,0});
}
} // namespace spyro
