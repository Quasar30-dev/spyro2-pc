#include "spyro/runtime.hpp"
#include "spyro/assets.hpp"
#include "spyro/spyro_model.hpp"
#include <cmath>
#include <iostream>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw FormatError(why);}
void ticks(Game& g,unsigned n,InputState in={}){for(unsigned i=0;i<n;++i)g.advance(FixedClock::step,in);}
Level floor_level() {
    Level l;l.wad_slot=15;l.spawn={0,8,0};l.bounds={{-200,0,-200},{200,100,200}};
    l.triangles={{{-200,8,-200},{200,8,200},{200,8,-200}},{{-200,8,-200},{-200,8,200},{200,8,200}}};return l;
}
Scene scene(const Level& l){Scene s;s.wad_slot=l.wad_slot;s.spawn={0,8,0};return s;}
int angle_error(int a,int b){int d=(a-b)&4095;return d>2048?d-4096:d;}
}
int main(int argc,char** argv) {
    try {
        const auto level=floor_level();
        // PC right-stick camera: the stick's angle is held recentre_delay ticks
        // after it is let go (the PC option, 4 s by default), then the follow
        // spring eases the camera back to Spyro's default angle (no cut).
        for(const unsigned seconds:{4u,2u}) {
            Game g(level,scene(level));ticks(g,30);g.camera.recentre_delay=seconds*60;
            const int rest=g.camera.parameters()[0];
            InputState stick;stick.camera_x=1;ticks(g,30,stick);
            const int turned=g.camera.parameters()[0];
            check(std::abs(angle_error(turned,rest))>500,"The right stick did not turn the camera");
            unsigned held=0,first_move=0,largest_step=0;int previous=turned;
            for(unsigned t=1;t<=seconds*60+240;++t) {
                ticks(g,1);const int now=g.camera.parameters()[0];
                if(g.camera.steered())held=t;
                const auto step=static_cast<unsigned>(std::abs(angle_error(now,previous)));
                if(step>2 && !first_move)first_move=t;
                largest_step=std::max(largest_step,step);previous=now;
            }
            const int left=angle_error(g.camera.parameters()[0],rest);
            std::cout<<"camera recentre "<<seconds<<" s: stick turned the azimuth "<<angle_error(turned,rest)<<"; held "<<held<<" ticks after release ("
                     <<held/60.0<<" s); returning from tick "<<first_move<<"; largest step "<<largest_step<<" per tick; "<<left<<" from the default angle 4 s later\n";
            check(held+1==seconds*60,"The stick's camera angle was not held until the recentre delay");   // released at tick 0, dropped at tick 60 x seconds
            check(first_move>=seconds*60 && first_move<=seconds*60+2,"The camera did not start back at the recentre delay");
            check(largest_step<400,"The camera cut back instead of easing");
            check(std::abs(left)<32,"The camera did not return to Spyro's default angle");
        }
        {   // 0 = never by itself: a standing Spyro keeps the stick's angle.
            Game g(level,scene(level));ticks(g,30);g.camera.recentre_delay=0;
            InputState stick;stick.camera_x=1;ticks(g,30,stick);const int turned=g.camera.parameters()[0];
            ticks(g,900);check(g.camera.steered() && std::abs(angle_error(g.camera.parameters()[0],turned))<8,"Recentre delay 0 still recentred");
        }
        // Look mode: hold Triangle while standing. The close camera draws in,
        // Spyro stays put whatever the stick does, and the view turns.
        {
            Game g(level,scene(level));ticks(g,30);
            check(g.player.grounded && g.player.motion==Motion::Idle,"Spyro did not settle standing");
            const auto start=g.player.position;
            InputState look;look.look_held=true;ticks(g,40,look);
            check(g.player.look.active && g.camera.look.active && g.camera.state()==7,"Holding Triangle while standing did not enter look mode");
            check(g.camera.look_ready() && std::abs(g.camera.parameters()[2]-768)<0x200,"The look camera did not draw in to its 768 radius");
            InputState right=look;right.move_x=1;right.move_z=0;ticks(g,60,right);
            check(length(g.player.position-start)<1e-4f,"Spyro walked while looking");
            check(g.player.look.yaw==0x400,"Looking right did not reach the 0x400 limit");
            check(g.player.head.yaw_target==-0x300 && g.player.head.yaw<-0x200,"The head did not turn right with the view");
            InputState up=look;up.move_z=1;ticks(g,80,up);
            check(g.player.look.pitch==0x500 && g.player.head.pitch_target==-0x200,"Up did not raise the camera to its limit and bow the head");
            // Retail: the steering value falls back by 0x20 a tick, so a
            // released stick stops turning within four ticks.
            ticks(g,6,look);const auto held=g.player.look.yaw;ticks(g,20,look);
            check(g.player.look.yaw==held,"The view kept turning after the stick was released");
            ticks(g,1);
            check(!g.player.look.active && !g.camera.look.active && g.player.head.yaw_target==0,"Releasing Triangle did not leave look mode");
            ticks(g,60);check(std::abs(g.player.head.yaw)<8 && g.camera.state()!=7,"The head and camera did not return");
            // Jumping ends it.
            ticks(g,30,look);check(g.player.look.active,"Look mode did not resume");
            InputState jump=look;jump.jump_pressed=jump.jump_held=true;ticks(g,1,jump);ticks(g,2,look);
            check(!g.player.look.active,"Jumping did not end look mode");
        }
        // After a headbash Triangle has to be let go before looking (80030c94).
        {
            Game g(level,scene(level));ticks(g,30);g.player.air.recovery_ticks=12;
            InputState look;look.look_held=true;ticks(g,40,look);
            check(!g.player.look.active,"Triangle held from a headbash entered look mode");
            ticks(g,1);ticks(g,10,look);check(g.player.look.active,"Look mode refused after Triangle was released");
        }
        // PC right stick: the camera turns at once, stays while Spyro stands,
        // and swings back behind him once he runs.
        {
            Game g(level,scene(level));ticks(g,60);
            const int behind=g.camera.parameters()[0];
            InputState turn;turn.camera_x=1;ticks(g,30,turn);
            const int turned=g.camera.parameters()[0];
            check(std::abs(angle_error(turned,behind))>700,"The right stick did not turn the camera");
            ticks(g,240);check(std::abs(angle_error(g.camera.parameters()[0],turned))<8,"The turned camera drifted while Spyro stood still");
            InputState run;run.move_z=1;run.camera_relative=true;ticks(g,240,run);
            const int heading=static_cast<int>(std::lround(retail::original_heading(g.player.yaw)));
            check(std::abs(angle_error(g.camera.parameters()[0],2048+heading))<160,"The camera did not return behind Spyro once he ran");
            InputState tilt;tilt.camera_y=1;ticks(g,40,tilt);ticks(g,60);
            check(g.camera.parameters()[1]<g.camera.settings.follow[1]-100,"Up on the right stick did not lower the camera to look up");
            InputState centre;centre.center_camera=true;ticks(g,1,centre);ticks(g,120);
            check(std::abs(g.camera.parameters()[1]-g.camera.settings.follow[1])<16,"Centring did not restore the camera height");
        }
        // The head part turns about the neck: a left turn of 90 degrees takes a
        // point ahead of the neck to its left (-x).
        {
            SpyroModel model;SpyroAnimation clip;clip.split_vertex=1;model.animations[0]=clip;
            SpyroPose pose;pose.attachment={0,1,0};pose.vertices={{0,0,0},{0,1,1}};
            model.turn_head(pose,0,std::numbers::pi_v<float>/2,0);
            check(length(pose.vertices[0])<1e-6f && length(pose.vertices[1]-Vec3{-1,1,0})<1e-5f,"Head yaw turned the wrong part or direction");
            pose.vertices={{0,0,0},{0,1,1}};model.turn_head(pose,0,0,std::numbers::pi_v<float>/2);
            check(length(pose.vertices[1]-Vec3{0,2,0})<1e-5f,"Head pitch up did not raise the muzzle");
        }
        if(argc>1) {
            // The retail model: how many faces join head and body vertices
            // (those stretch when the head turns, in the original as here).
            AssetStore store(argv[1]);WadArchive wad(store);const auto exe=store.read_all("SCUS_944.25");
            const auto model=decode_spyro(wad.read(0),wad.read(15),exe);
            for(const unsigned id:{0u,2u}) {
                const auto& c=model.clip(id);unsigned spanning=0;
                for(const auto& f:c.faces){bool head=false,body=false;for(unsigned k=0;k<(f.quad?4u:3u);++k)(f.vertices[k]<c.split_vertex?body:head)=true;spanning+=head && body;}
                std::cout<<"clip "<<id<<": split "<<c.split_vertex<<", "<<c.faces.size()<<" faces, "<<spanning<<" join head and body\n";
            }
        }
        std::cout<<checks<<" look-mode and camera checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
