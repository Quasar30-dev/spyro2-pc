#include "spyro/runtime.hpp"
#include <iostream>

using namespace spyro;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Level track(bool uneven,float end=100) {
    Level level;
    for(float z=-4;z<end;z+=0.5f) {
        const auto height=[&](float x){return uneven?0.12f*std::sin(x*1.7f):0.0f;};
        Vec3 a{-4,height(z),z},b{4,height(z),z},c{-4,height(z+0.5f),z+0.5f},d{4,height(z+0.5f),z+0.5f};
        level.triangles.push_back({a,c,b});level.triangles.push_back({b,c,d});
        level.bounds.add(a);level.bounds.add(d);
    }
    return level;
}
}
int main() {
    try {
        unsigned failures=0;
        for(bool uneven:{false,true})for(int mode=0;mode<3;++mode) {
            const auto level=track(uneven);Scene scene;scene.spawn={0,0.02f,0};Game game(level,scene);
            game.cheats.supercharge=mode==2;
            for(int i=0;i<30;++i)game.advance(FixedClock::step,{});
            InputState input;input.move_z=1;input.charge=mode!=0;
            const auto expected=mode==0?Motion::Run:mode==1?Motion::Charge:Motion::Supercharge;
            unsigned wrong=0;
            for(int i=0;i<360;++i) {game.advance(FixedClock::step,input);if(game.player.motion!=expected)++wrong;}
            std::cout<<(uneven?"uneven":"flat")<<" mode "<<mode<<": "<<wrong<<" interrupted ticks\n";
            failures+=wrong;
        }
        check(failures==0,"Floor contact interrupted continuous traversal animations");
        const auto level=track(false,2);Scene scene;Game edge(level,scene);
        for(int i=0;i<60;++i)edge.advance(FixedClock::step,{0,1});
        check(!edge.player.grounded && edge.player.motion==Motion::Fall && edge.player.position.y< -0.1f,"Ground support stuck to a real ledge");
        Game jump(level,scene);jump.advance(FixedClock::step,{});
        InputState input;input.jump_pressed=true;input.jump_held=true;jump.advance(FixedClock::step,input);
        check(!jump.player.grounded && jump.player.motion==Motion::Jump && jump.player.velocity.y>0,"Ground support swallowed an intentional jump");
        Vec3 reference{};
        const auto uneven=track(true);
        for(int rate:{30,60,144}) {
            Game game(uneven,scene);
            for(int i=0;i<rate*4;++i)game.advance(1.0/rate,{0,1});
            if(rate==30)reference=game.player.position;
            else check(length(game.player.position-reference)<0.0001f,"Presentation rate changed traversal");
        }
        std::cout<<"Traversal contact, real ledge, immediate jump and 30/60/144 Hz checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
