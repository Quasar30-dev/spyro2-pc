#include "spyro/runtime.hpp"
#include <iostream>
#include <limits>

using namespace spyro;
namespace {
unsigned checks=0;
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
bool near(float a,float b,float epsilon=0.0001f) {return std::abs(a-b)<epsilon;}
void plane(Level& level,float y,float extent,bool water) {
    const auto id=static_cast<unsigned>(level.triangles.size());
    level.triangles.push_back({{-extent,y,-extent},{extent,y,extent},{extent,y,-extent},id,water});
    level.triangles.push_back({{-extent,y,-extent},{-extent,y,extent},{extent,y,extent},id+1,water});
    level.bounds.add({-extent,y,-extent});level.bounds.add({extent,y,extent});
}
void tick(Game& game,unsigned count,InputState input={}) {for(unsigned i=0;i<count;++i)game.advance(FixedClock::step,input);}
}
int main() {
    try {
        Level land;plane(land,0,500,false);Scene scene;scene.spawn={0,0.001f,0};
        for(int rate:{30,60,144}) {
            // Isolate Q6 movement integration from the floating-point contact
            // solver. Ground contacts have their own tests below and in core.
            Game run(land,scene);run.player.position.y=20;run.tuning.gravity=0;tick(run,1);
            for(int f=0;f<rate;++f)run.advance(1.0/rate,{0,1});
            float expected=0;for(int t=1;t<=60;++t)expected+=std::min(t*0x140,0x1200)/(64.0f*1024);
            if(!near(run.player.velocity.z,4.21875f) || !near(run.player.position.z,expected))
                std::cerr<<"rate "<<rate<<", run velocity "<<run.player.velocity.z<<", distance "<<run.player.position.z<<", expected "<<expected<<'\n';
            check(near(run.player.velocity.z,4.21875f) && near(run.player.position.z,expected),"Run speed or acceleration differs from retail Q6 integration");
            Game diagonal(land,scene);tick(diagonal,1);
            for(int f=0;f<rate;++f)diagonal.advance(1.0/rate,{1,1});
            check(near(length(diagonal.player.velocity),4.21875f),"Diagonal running gained speed");
            Game charge(land,scene);tick(charge,1);InputState input;input.charge=true;
            for(int f=0;f<rate;++f)charge.advance(1.0/rate,input);
            check(near(charge.player.velocity.z,7.1484375f) && charge.player.motion==Motion::Charge,"Charge needs forward input or uses a provisional speed");
            charge.cheats.supercharge=true;tick(charge,60,input);
            check(near(charge.player.velocity.z,13.0078125f),"Supercharge still scales a provisional charge speed");
        }
        Level pool;plane(pool,-15,500,false);plane(pool,0,40,true);
        pool.surface_swimming=pool.underwater_swimming=true;
        auto water=pool.water_at({0,-5,0});check(water && near(water->height,0),"Underwater query missed the surface above Spyro");
        check(!pool.water_at({41,0,0}) && !pool.water_at({std::numeric_limits<float>::quiet_NaN(),0,0}),"Water query escaped its polygon or accepted NaN");
        const auto hit=pool.raycast({0,2,0},{0,-1,0},30);
        check(hit && near(hit->point.y,-15),"Water still blocks terrain/camera rays");
        const auto liquid_hit=pool.raycast({0,2,0},{0,-1,0},30,true);
        check(liquid_hit && near(liquid_hit->point.y,0),"Editor cannot select a water surface");
        Vec3 velocity{0,-1,0};bool grounded=false;
        check(near(pool.resolve_sphere({0,0.1f,0},0.35f,velocity,grounded).y,0.1f) && !grounded,"Water still resolves as a solid sphere floor");
        auto bridge=pool;plane(bridge,2,5,false);
        check(!bridge.water_at({0,2.4f,0}),"A bridge incorrectly immersed the player in the pool beneath it");
        scene.spawn={0,0.1f,0};Game surface(pool,scene);tick(surface,100);
        check(surface.player.swim.mode==WaterMode::Surface && surface.player.motion==Motion::SurfaceIdle && !surface.player.grounded,
            "Falling onto water failed to enter floating idle");
        check(motion_animation(surface.player.motion)==44,"Surface idle selected the wrong clip");
        const auto global_sound=[](const Game& game,unsigned id){return std::any_of(game.sound_events.begin(),game.sound_events.end(),
            [&](const SoundEvent& e){return e.scope==SoundScope::Global && e.id==id;});};
        // Overlay 14 state entry: landing on the surface (2C) plays global 7,
        // only diving under (28/29/2A) plays global 8.
        check(global_sound(surface,7) && !global_sound(surface,8),"Landing on water played the dive sound instead of the splash");
        tick(surface,60,{0,1});
        check(surface.player.motion==Motion::SurfaceSwim && near(length(surface.player.velocity),2.2265625f),"Surface paddling speed / animation mismatch");
        check(motion_animation(surface.player.motion)==43,"Surface movement selected the wrong clip");
        InputState jump;jump.jump_pressed=true;surface.advance(FixedClock::step,jump);
        check(surface.player.swim.mode==WaterMode::Dry && surface.player.velocity.y>0,"Surface jump failed to leave buoyancy");
        Game dive(pool,scene);tick(dive,120);InputState button;button.charge=true;tick(dive,1,button);tick(dive,65);
        check(dive.player.swim.mode==WaterMode::Underwater && dive.player.swim.submerged && dive.player.swim.entry_ticks==0,
            "Dive stroke failed to cross the surface after releasing its button");
        check(global_sound(dive,8),"Diving under the surface did not play the dive sound");
        tick(dive,120);
        check(dive.player.motion==Motion::UnderwaterIdle && length(dive.player.velocity)==0 && motion_animation(dive.player.motion)==41,
            "Underwater idle must stop without gravity or a walking animation");
        {
            // 8002b0b8 selects state 39 underwater: full-strength push from the
            // attacker with no lift, the 0x18 cue and clip 57 (not a land hurt).
            auto hurt=dive;hurt.sound_events.clear();
            check(hurt.hurt(hurt.player.position+Vec3{1,0,0}),"Underwater hit was ignored");
            check(near(hurt.player.velocity.x,-9) && near(hurt.player.velocity.y,0) && global_sound(hurt,0x18),"Underwater knockback lost its retail push or cue");
            tick(hurt,1);check(hurt.player.motion==Motion::UnderwaterHurt && motion_animation(hurt.player.motion)==57,"Underwater hit used a land hurt animation");
            tick(hurt,30);check(hurt.player.swim.mode==WaterMode::Underwater && hurt.player.motion!=Motion::Fall,"Underwater knockback left the water state");
            // No surface triangle above for a moment (overhang / seam): stay underwater.
            auto gap=dive;gap.player.position.x=41;tick(gap,5);
            check(gap.player.swim.mode==WaterMode::Underwater && gap.player.motion==Motion::UnderwaterIdle,"A brief water-surface gap dropped Spyro into land animations");
            tick(gap,40);check(gap.player.swim.mode==WaterMode::Dry,"Leaving the water entirely kept the underwater state");
        }
        const auto idle_position=dive.player.position;tick(dive,60);
        check(length(dive.player.position-idle_position)<0.00001f,"Underwater idle sinks or drifts");
        InputState swim;swim.jump_held=true;tick(dive,60,swim);
        check(dive.player.motion==Motion::UnderwaterSwim && near(length(dive.player.velocity),3.28125f) && motion_animation(dive.player.motion)==40,
            "Swimming propulsion / animation mismatch");
        InputState charge;charge.charge=true;tick(dive,30,charge);
        check(dive.player.motion==Motion::UnderwaterCharge && near(length(dive.player.velocity),6.5625f) && motion_animation(dive.player.motion)==42,
            "Underwater charge speed / animation mismatch");
        tick(dive,120);check(dive.player.motion==Motion::UnderwaterIdle,"Underwater charge release did not coast to idle");
        swim.move_z=-1;tick(dive,130,swim);
        check(dive.player.swim.mode==WaterMode::Surface,"Swimming upwards did not resurface");
        scene.spawn={0,-5,0};Game breach(pool,scene);tick(breach,1);charge.move_z=-1;
        bool breached=false;
        for(unsigned i=0;i<180;++i){tick(breach,1,charge);if(breach.player.motion==Motion::ChargeJump){breached=true;break;}}
        check(breached,"Charging upwards did not breach with the charge-jump clip");
        auto shallow=pool;shallow.triangles.clear();plane(shallow,-0.8f,40,false);plane(shallow,0,40,true);
        scene.spawn={0,0,0};Game wading(shallow,scene);tick(wading,120);tick(wading,1,button);
        check(wading.player.swim.mode==WaterMode::Surface,"Dive ignored the retail bottom-clearance check");
        Vec3 cadence_position;float cadence_pitch=0;
        for(int rate:{30,60,144}) {
            scene.spawn={0,-5,0};Game swimming(pool,scene);tick(swimming,1);
            swim.move_x=0.35f;swim.move_z=0.25f;
            for(int frame=0;frame<rate*2;++frame)swimming.advance(1.0/rate,swim);
            if(rate==30){cadence_position=swimming.player.position;cadence_pitch=swimming.player.pitch;}
            else check(length(swimming.player.position-cadence_position)<0.00001f && near(swimming.player.pitch,cadence_pitch),
                "Swimming depends on presentation cadence");
            const auto before=swimming.player.position;swimming.set_paused(true);swimming.advance(1,charge);
            check(length(swimming.player.position-before)==0 && swimming.player.previous_pitch==swimming.player.pitch,
                "Pause advanced swimming or retained stale pitch interpolation");
            swimming.respawn();check(swimming.player.swim.mode==WaterMode::Dry && swimming.player.pitch==0,"Respawn retained swimming state");
        }
        const auto nose=retail::rotate_model({0,0,1},0,0.5f,0.3f);
        check(near(length(nose),1) && near(nose.y,std::sin(0.5f)),"Body rendering pitch disagrees with swimming heading");
        std::cout<<checks<<" swimming / retail speed checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
