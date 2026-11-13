#include "spyro/ripto_autoplay.hpp"
#include <array>
#include <cmath>

namespace spyro {
namespace {
float flat(Vec3 v){return std::hypot(v.x,v.z);}
Vec3 level(Vec3 v){v.y=0;const float n=flat(v);return n>1e-4f?v/n:Vec3{};}
// The arrival is on the south crown walk; the duel island is reached by the
// south upper ascent, the terrace and the east lower ascent (design.json).
constexpr std::array<Vec3,7> route{{{0,7,-33},{0,3.5f,-26},{9,3.5f,-21.5f},{17,3.5f,-14},{22,3.5f,-4},{20,3.5f,0},{8,0,0}}};
}
InputState RiptoAutoplay::input(const Game& g) {
    InputState in;in.camera_relative=false;++tick_;
    const auto& b=g.ripto;
    if(!b.enabled || b.cinematic!=CrownCinematic::None || g.portal_travel.active() || g.dialogue.active())return in;
    const auto p=g.player.position;const auto c=b.center;
    const auto move=[&](Vec3 d){d=level(d);in.move_x=d.x;in.move_z=d.z;};
    // Off the island (the arrival, a respawn): follow the route down to it.
    const bool island=flat(p-c)<11.5f && p.y<c.y+1.2f;
    if(!island) {
        // Progress along the route only moves forward: within 2.5 of the
        // current waypoint, head for the next. (Re-picking "the one after the
        // nearest" every tick dithered at that radius on the south ramp.)
        // After a respawn, or when the waypoint is far off, restart from the
        // nearest one.
        if(route_>=route.size() || length(c+route[route_]-p)>12) {
            unsigned nearest=0;
            for(unsigned k=1;k<route.size();++k)if(length(c+route[k]-p)<length(c+route[nearest]-p))nearest=k;
            route_=nearest;
        }
        if(flat(c+route[route_]-p)<2.5f && route_+1<route.size())++route_;
        ++route_steps;move(c+route[route_]-p);return in;
    }
    route_=static_cast<unsigned>(route.size()-1);
    // The target: Ripto, then Mecha-Gulp (Crush once Gulp falls), then the
    // bird; at the crown core, Ripto again.
    const auto body=[&](unsigned i)->const RetailObject*{return i<g.retail_objects.size() && !g.retail_objects[i].hidden?&g.retail_objects[i]:nullptr;};
    const RetailObject* target=nullptr;
    if(b.action==RiptoAction::Finale)target=body(b.actor);
    else if(b.stage==RiptoStage::Foot)target=body(b.actor);
    else if(b.stage==RiptoStage::MechaGulp)target=b.health?body(b.gulp):b.crush_health?body(b.crush):nullptr;
    else if(b.stage==RiptoStage::Bird)target=body(b.bird);
    if(!target || b.defeated()){tracking_=false;return in;}
    const auto t=target->position;const auto to=level(t-p);const float distance=flat(t-p);
    // Track the target's ground velocity; a jump in position (a new target,
    // a teleport) restarts it.
    if(tracking_ && flat(t-target_last_)<1){auto v=(t-target_last_)*60.0f;v.y=0;target_velocity_=target_velocity_+(v-target_velocity_)*.3f;}
    else target_velocity_={};
    target_last_=t;tracking_=true;
    // Where a shot from here meets it: Ripto crosses about 5 units/s, so an
    // Ember (16 units/s) aimed at him passes behind. Two refinements suffice.
    auto lead=t;
    for(unsigned k=0;k<2;++k)lead=t+target_velocity_*(flat(lead-p)/16.0f);
    // Dangers first: jump an expanding ring as it reaches Spyro; leave marked circles.
    for(const auto& s:b.shots())if(s.kind==RiptoShotKind::Shockwave) {
        const float d=flat(s.visual.position-p);
        if(d>s.radius-.2f && d<s.radius+1.1f && g.player.grounded){in.jump_pressed=in.jump_held=true;++jumps;}
    }
    Vec3 escape{};
    for(const auto& m:b.marks())if(flat(m.position-p)<m.radius+.8f)escape+=level(p-m.position);
    // Powers from Hunter's orbs (and the bird phase's Superflame).
    const bool superflame=g.powers.fire.remaining>0 || b.green_bomb();
    const bool supercharge=b.supercharge() || g.powers.charge;
    // Otherwise fetch a resting orb when one is near and nothing is marked.
    if(!superflame && !supercharge && flat(escape)<.01f && b.stage!=RiptoStage::Bird) {
        const RiptoOrb* nearest=nullptr;float best=9;
        for(const auto& o:b.orbs())if(o.resting && flat(o.visual.position-c)<10.5f && flat(o.visual.position-p)<best){best=flat(o.visual.position-p);nearest=&o;}
        if(nearest){++orbs_sought;move(nearest->visual.position-p);return in;}
    }
    if(supercharge) {
        // Supercharge straight through the target.
        in.charge=true;move(to);return in;
    }
    // Circle the target at a stand-off distance, inside the island.
    const float want=b.stage==RiptoStage::Bird?3.0f:b.stage==RiptoStage::MechaGulp?7.5f:6.0f;
    if(++strafe_ticks_>170 || stuck_>25){strafe_=-strafe_;strafe_ticks_=0;stuck_=0;}
    stuck_=g.player.grounded && flat(p-last_)<.004f?stuck_+1:0;last_=p;
    const Vec3 tangent{-to.z*strafe_,0,to.x*strafe_};
    auto heading=tangent+to*std::clamp((distance-want)*.45f,-1.0f,1.0f);
    if(b.stage==RiptoStage::Bird)heading=level(c-p)*std::clamp((flat(p-c)-2.5f)*.5f,0.0f,1.0f)+tangent*.3f;
    const float r=flat(p-c);
    if(r>9.0f)heading=heading+level(c-p)*((r-9.0f)*1.5f);     // never toward the molten ring
    if(flat(escape)>.01f)heading=escape*2+heading*.3f;
    // Attack: turn to the target for a few ticks, then shoot or breathe.
    const bool ready=superflame || (g.upgrades.elements.energy>30 && g.elements.shot_ready());
    if(ready && flat(escape)<.01f && (aim_ || (tick_%45==0 && distance<8))) {
        ++aim_;move(lead-p);
        if(aim_>=8) {
            if(superflame){in.flame_pressed=true;++flames;}
            else {in.shot_pressed=true;++shots;}
            aim_=0;
        }
        return in;
    }
    aim_=0;move(heading);return in;
}
} // namespace spyro
