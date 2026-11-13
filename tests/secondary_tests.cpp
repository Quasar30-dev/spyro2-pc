#include "spyro/spyro_model.hpp"
#include "spyro/flame.hpp"
#include <iostream>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool good,const char* why) {++checks;if(!good)throw std::runtime_error(why);}
SpyroModel fixture() {
    SpyroModel m;m.transitions.fill(1);m.secondary_transitions.fill(6);
    m.secondary_transitions[1]=1;m.secondary_transitions[8]=4;m.secondary_transitions[9]=7;m.secondary_clips[1]=7;
    for(unsigned b=0;b<256;++b)m.blends[b]=static_cast<std::uint8_t>((b>>4)?(b&15)*256/((b>>4)+1):(b&15)*16);
    for(unsigned id:{0,2,7}) {
        SpyroAnimation clip;clip.id=id;clip.split_vertex=1;clip.loop_end=clip.transition_end=id==7?8:4;
        for(unsigned f=0;f<clip.loop_end;++f)clip.frames.push_back({{{static_cast<float>(id+f),0,0},{0,static_cast<float>(id+f),0}},4});
        m.animations.emplace(id,std::move(clip));
    }
    return m;
}
}
int main() {
    try {
        auto model=fixture();SpyroAnimator primary;primary.reset(&model,2);
        SpyroSecondaryAnimator head;head.reset(&model,primary.state());
        primary.tick(2);head.tick(0,primary.state());
        check(head.state().mode==6 && head.state().pose==primary.state(),"Head does not follow the body's endpoints");
        const auto before=head.sample();primary.tick(2);head.tick(1,primary.state());
        check(head.state().mode==1 && head.state().pose.next_clip==7 && head.state().pose.blend==0x71,
            "Flame did not enter through the retail transition table");
        check(length(head.sample(0).vertices[1]-before.vertices[1])<0.00001f,"Secondary transition snapped its presentation pose");
        for(unsigned i=0;i<7;++i) {primary.tick(2);head.tick(1,primary.state());}
        check(head.state().mode==7 && head.state().pose.clip==7 && head.state().pose.frame==0 && head.state().pose.next_frame==1,
            "Flame did not enter its one-shot mode after eight VBlanks");
        const auto body=primary.sample();const auto composite=model.secondary_track(2,body,head.sample());
        check(length(composite.vertices[0]-body.vertices[0])==0 && length(composite.vertices[1]-body.vertices[1])>1,
            "Independent head animation overwrote the body partition");
        bool saw_return=false;
        for(unsigned i=0;i<60;++i) {primary.tick(i<20?2:0);head.tick(1,primary.state());saw_return|=head.state().mode==8;}
        check(saw_return && head.state().mode==6 && head.state().pose==primary.state(),"One-shot head clip looped or failed to rejoin current movement");
        head.tick(0,primary.state());head.tick(1,primary.state());
        head.tick(0,primary.state(),true);
        check(head.state().mode==6 && head.state().action==0 && head.state().pose==primary.state(),"Forced action cancellation did not restore the primary track");
        head.reset_interpolation();check(length(head.sample(0).vertices[1]-head.sample(1).vertices[1])==0,"Pause rewound the head pose");
        auto charge_mesh=model.animations.at(2);charge_mesh.id=61;charge_mesh.split_vertex=2;
        for(auto& f:charge_mesh.frames)f.vertices.push_back({1,2,3});
        model.animations.emplace(61,std::move(charge_mesh));
        model.transitions[2*62+61]=model.transitions[61*62+2]=2;
        primary.reset(&model,2);head.reset(&model,primary.state());
        primary.tick(61);head.tick(0,primary.state(),true);
        check(primary.sample(0.5f).vertices.size()==3 && head.sample(0.5f).vertices.size()==3,
            "Direct transition failed to switch the supercharge vertex layout");
        primary.tick(2);head.tick(0,primary.state(),true);
        check(primary.sample(0.5f).vertices.size()==2 && head.sample(0.5f).vertices.size()==2,
            "Direct transition failed to restore the ordinary Spyro layout");
        FlameAction action;
        check(action.tick(true,false,true,false)==FlameEvent::None && action.action()==0,"Charge must reject a new ordinary flame");
        check(action.tick(true,true,false,true)==FlameEvent::None,"A living effect allowed a new flame");
        check(action.tick(true,true,false,false)==FlameEvent::Start && action.age()==1 && action.follows_attachment(),"Flame action did not initialize its VBlank clock");
        for(unsigned i=1;i<20;++i)action.tick(true,true,false,false);
        check(action.age()==20 && action.follows_attachment(),"Repeated fire input restarted or shortened the action");
        action.tick(false,false,true,false);
        check(action.action()==1 && action.age()==21 && !action.follows_attachment(),"Entering charge killed existing flame or kept updating its attachment");
        for(unsigned i=21;i<48;++i)action.tick(false,true,false,false);
        check(action.action()==1 && action.age()==48,"Flame recovery ended before the retail boundary");
        check(action.tick(false,true,false,false)==FlameEvent::Cancel && !action.action() && !action.force_follow(),"Normal flame recovery did not allow the animation exit");
        action.tick(true,true,false,false);
        check(action.tick(false,false,false,false)==FlameEvent::Cancel && action.force_follow(),"Disallowed movement did not force head-track recovery");
        action.reset();check(!action.action() && action.age()==0 && !action.follows_attachment(),"Respawn retained an action");
        std::cout<<checks<<" secondary animation checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"Secondary check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
