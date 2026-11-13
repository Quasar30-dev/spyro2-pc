#include "spyro/runtime.hpp"
#include <iostream>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
Level world() {
    Level level;level.bounds.add({-5,0,-5});level.bounds.add({5,0,5});
    level.triangles={{{-5,0,-5},{-5,0,5},{5,0,5}},{{-5,0,-5},{5,0,5},{5,0,-5}}};
    RetailObject hunter;hunter.type=0x1bc;hunter.position={0,0,1.2f};level.objects.push_back(hunter);
    NpcDefinition npc;npc.object=0;npc.lines.resize(3);npc.lines[0].text="First line";npc.lines[0].repeat=1;
    npc.lines[0].speech=XaClip{10,150,2};npc.lines[1].text="Repeat line";npc.lines[2].text="Choice response";
    level.npcs.push_back(npc);return level;
}
void ticks(Game& game,unsigned n,InputState input={}) {for(unsigned i=0;i<n;++i)game.advance(FixedClock::step,input);}
}
int main() {
    try {
        auto level=world();Scene scene;Game game(level,scene);ticks(game,2);
        check(game.dialogue.nearby() && !game.dialogue.active(),"Nearby Hunter prompt missing or interaction forced");
        InputState speak;speak.dialogue.interact=true;ticks(game,1,speak);
        check(game.dialogue.active() && game.dialogue.line()->text=="First line" && game.dialogue.conversations()==1,"E did not start retail dialogue");
        const auto at=game.player.position;InputState move;move.move_z=1;move.charge=true;move.jump_pressed=true;move.flame_pressed=true;ticks(game,30,move);
        check(length(game.player.position-at)<0.001f && !game.flame_action.follows_attachment(),"Movement or attacks escaped dialogue lock");
        InputState next;next.dialogue.next=true;ticks(game,1,next);
        check(!game.dialogue.active(),"Continue did not close dialogue");ticks(game,1,speak);
        check(game.dialogue.line() && game.dialogue.line()->text=="Repeat line","Retail repeat pointer was ignored");
        game.respawn();check(!game.dialogue.active(),"Respawn left a dialogue lock active");
        auto choice=world();choice.npcs[0].lines[0].yes=2;choice.npcs[0].lines[0].no=1;
        Game yes(choice,scene);ticks(yes,2);ticks(yes,1,speak);ticks(yes,12);
        InputState answer;answer.dialogue.choice=0;ticks(yes,1,answer);
        check(yes.dialogue.active() && yes.dialogue.line()->text=="Choice response" && yes.dialogue.elapsed()==0,"Yes branch did not select its retail response");
        answer.dialogue={};answer.dialogue.cancel=true;ticks(yes,1,answer);
        check(!yes.dialogue.active(),"Escape did not release conversation");
        auto camera=world();auto& camera_npc=camera.npcs[0];camera_npc.subtype=6;camera_npc.lines.resize(7);
        camera_npc.lines[3].text="Change to passive?";camera_npc.lines[3].yes=4;camera_npc.lines[3].no=5;
        camera_npc.lines[4].text="Passive response";camera_npc.lines[0].yes=1;camera_npc.lines[0].no=2;
        Game modes(camera,scene);ticks(modes,2);ticks(modes,1,speak);ticks(modes,12);
        check(modes.dialogue.line()->text=="Change to passive?","Camera Hunter ignored the current camera preference");
        InputState choose;choose.dialogue.choice=0;ticks(modes,1,choose);ticks(modes,12);ticks(modes,1,next);
        check(!modes.dialogue.active_camera && modes.camera.passive,"Hunter's passive reply did not change the native camera");
        ticks(modes,1,speak);ticks(modes,12);ticks(modes,1,choose);ticks(modes,12);ticks(modes,1,next);
        check(modes.dialogue.active_camera && !modes.camera.passive,"Hunter's active reply did not restore the native camera");
        auto automatic=world();automatic.npcs[0].automatic=true;Game auto_game(automatic,scene);ticks(auto_game,4);
        check(auto_game.dialogue.active(),"Authored automatic greeting was ignored");ticks(auto_game,1,answer);ticks(auto_game,20);
        check(!auto_game.dialogue.active() && auto_game.dialogue.conversations()==1,"Automatic greeting retriggered every tick");
        auto wall=world();wall.triangles.push_back({{-3,-1,0.6f},{3,-1,0.6f},{3,3,0.6f}});wall.triangles.push_back({{-3,-1,0.6f},{3,3,0.6f},{-3,3,0.6f}});
        Game blocked(wall,scene);ticks(blocked,2);ticks(blocked,1,speak);check(!blocked.dialogue.active(),"Conversation triggered through a wall");
        Scene high;high.spawn={0,2,0};Game above(level,high);ticks(above,1,speak);check(!above.dialogue.active(),"Airborne player triggered a ground-only conversation");
        for(int rate:{30,60,144}) {
            Game paced(level,scene);ticks(paced,2);
            for(int f=0;f<rate;++f){InputState in;if(f==0)in.dialogue.interact=true;paced.advance(1.0/rate,in);}
            check(paced.dialogue.conversations()==1 && paced.dialogue.active(),"Display rate duplicated or dropped an interaction edge");
        }
        std::cout<<checks<<" NPC interaction checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
