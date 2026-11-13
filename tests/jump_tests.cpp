#include "spyro/runtime.hpp"
#include <iostream>
using namespace spyro;
namespace {
unsigned checks{};
void expect(bool ok,const char* message){++checks;if(!ok)throw FormatError(message);}
Level floor_level(){Level l;l.wad_slot=15;l.spawn={0,0,0};l.triangles={{{-100,0,-100},{100,0,100},{100,0,-100}},{{-100,0,-100},{-100,0,100},{100,0,100}}};for(auto& t:l.triangles){l.bounds.add(t.a);l.bounds.add(t.b);l.bounds.add(t.c);}return l;}
struct Flight {float peak{},distance{};unsigned charge_ticks{};};
Flight jump(const Level& l,int delay,bool held,bool super=false) {
    Scene s;s.spawn=l.spawn;Game g(l,s);g.cheats.supercharge=super;for(int i=0;i<6;++i)g.advance(FixedClock::step,{});
    Flight result;
    for(unsigned t=0;t<180;++t) {
        InputState in;in.move_z=1;in.jump_pressed=t==0;in.jump_held=held || t==0;in.charge=delay>=0 && t>=static_cast<unsigned>(delay);
        g.advance(FixedClock::step,in);result.peak=std::max(result.peak,g.player.position.y);
        if(g.player.motion==Motion::ChargeJump)++result.charge_ticks;
        if(t>3 && g.player.grounded){result.distance=g.player.position.z;break;}
    }
    return result;
}
void retail_animation(const std::filesystem::path& path) {
    AssetStore assets(path);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
    const auto model=decode_spyro(wad.read(0),wad.read(17),exe,wad.read(29),29);
    const auto level=floor_level();Scene scene;scene.spawn=level.spawn;
    // Exercise the full retail animator and sampled mesh, not just Motion.
    for(bool delayed:{false,true})for(bool fly_cheat:{false,true})for(unsigned rate:{30u,60u,144u}) {
        Game g(level,scene,{},nullptr,&model);g.cheats.superfly=fly_cheat;
        for(unsigned i=0;i<rate;++i)g.advance(1.0/rate,{});
        unsigned charge_frames=0;float peak=0,pitch_up=0,pitch_down=0;bool saw_jump=false;
        for(unsigned frame=0;frame<rate*2;++frame) {
            const float time=float(frame)/rate;InputState in;
            in.charge=delayed?time>=0.5f+10.0f/60:time>=0.2f;
            in.jump_pressed=frame==rate/2;in.jump_held=frame>=rate/2;
            g.advance(1.0/rate,in);peak=std::max(peak,g.player.position.y);
            const auto& a=g.animation.state();
            if(g.player.motion==Motion::ChargeJump) {
                if(g.player.velocity.y>0)pitch_up=std::max(pitch_up,g.player.pitch);
                else pitch_down=std::min(pitch_down,g.player.pitch);
                expect(a.next_clip==21,"Ordinary charge jump did not request retail clip 21");
                expect(a.next_clip!=61 && !g.cheats.supercharge,"Charge jump incorrectly depends on supercharge");
                if(a.clip==21 && a.next_clip==21) {
                    const auto rendered=g.animation.sample(1);const auto expected=model.pose(a);
                    expect(rendered.vertices.size()==expected.vertices.size() && std::equal(rendered.vertices.begin(),rendered.vertices.end(),expected.vertices.begin(),
                        [](Vec3 a,Vec3 b){return length(a-b)<1e-6f;}),"Rendered charge-jump pose differs from the retail animation");
                    ++charge_frames;
                }
            }
            saw_jump|=g.player.motion==Motion::Jump;
            expect(!g.powers.flying,"First charge jump incorrectly activated Superfly");
        }
        expect(charge_frames>rate/5,"Charge-jump animation was missing or only flashed for a frame");
        expect(saw_jump==delayed,"Ground charge jump used the ordinary jump motion");
        expect(pitch_up>0.3f && pitch_down< -0.1f,"Charge jump must visibly pitch up during ascent and down during descent");
        expect(g.player.grounded && std::abs(g.player.pitch)<0.001f,"Landing did not return the charge jump to a level body pose");
        std::cout<<"retail animation: "<<(delayed?"jump then charge":"charge then jump")<<" display="<<rate
                 <<" superfly-enabled="<<fly_cheat<<" clip21-frames="<<charge_frames<<" peak="<<peak
                 <<" pitch="<<pitch_up*180/std::numbers::pi_v<float><<".."<<pitch_down*180/std::numbers::pi_v<float><<'\n';
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--oracle") {
            int v,c,h,r;unsigned t;while(std::cin>>v>>t>>c>>h>>r)std::cout<<retail_jump_velocity(v,t,c!=0,h!=0,r!=0)<<'\n';return 0;
        }
        const auto l=floor_level();const auto normal=jump(l,-1,true),tap=jump(l,-1,false),ground=jump(l,0,true),delayed=jump(l,10,true),powered=jump(l,0,true,true);
        expect(normal.peak>tap.peak*1.5f,"Holding jump must provide the retail extra lift");
        expect(delayed.peak>normal.peak && delayed.peak>ground.peak,"Jump-then-charge must gain height through velocity conservation");
        expect(ground.charge_ticks>10 && delayed.charge_ticks>10,"Air charge must select its own motion/animation");
        expect(ground.distance>normal.distance,"Ground charge jump should cover more distance");
        expect(powered.peak>2.3f && powered.peak<2.6f,"Requested supercharge height adjustment left its intended range");
        Scene s;s.spawn={0,0,0};Game g(l,s);g.cheats.superfly=true;for(unsigned i=0;i<6;++i)g.advance(FixedClock::step,{});
        expect(!g.powers.flying && g.player.grounded,"Enabling superfly must not launch Spyro");
        InputState in;in.jump_pressed=in.jump_held=true;g.advance(FixedClock::step,in);
        expect(!g.powers.flying && g.player.motion==Motion::Jump && g.player.velocity.y>0,"First superfly tap must jump normally");
        in.jump_pressed=false;for(unsigned i=0;i<10;++i)g.advance(FixedClock::step,in);
        expect(!g.powers.flying,"Holding jump must not start superfly");
        in.jump_pressed=true;g.advance(FixedClock::step,in);expect(g.powers.flying,"Second airborne tap must start superfly");
        if(argc>1)retail_animation(std::filesystem::u8path(argv[1]));
        std::cout<<checks<<" checks; heights normal="<<normal.peak<<" tap="<<tap.peak<<" ground charge="<<ground.peak
            <<" delayed charge="<<delayed.peak<<" supercharge="<<powered.peak<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
