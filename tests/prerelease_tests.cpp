#include "spyro/game_build.hpp"
#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include "spyro/interface.hpp"
#include <iostream>

using namespace spyro;
namespace {
void check(bool value,const char* why){if(!value)throw FormatError(why);}
Level floor_level(unsigned slot=15) {
    Level level;level.wad_slot=slot;level.bounds={{-100,0,-100},{100,30,100}};
    level.triangles={{{-100,0,-100},{100,0,100},{100,0,-100}},{{-100,0,-100},{-100,0,100},{100,0,100}}};
    level.spawn={0,.02f,0};return level;
}
Scene scene(const Level& level,unsigned crc=0xcdc0917c){Scene s;s.wad_slot=level.wad_slot;s.retail_crc=crc;s.spawn=level.spawn;return s;}
Vec3 roll(const SpyroModel& model,bool left,unsigned rate,bool enabled=true,unsigned crc=0xcdc0917c) {
    auto floor=floor_level();Game game(floor,scene(floor,crc),{},nullptr,&model);
    game.upgrades.enabled=enabled;game.upgrades.roll=true;
    for(unsigned i=0;i<rate;++i)game.advance(1.0/rate,{});
    game.sound_events.clear();
    const auto before=game.player.position;bool seen=false;auto previous=game.animation.state();unsigned wraps=0;
    for(unsigned i=0;i<rate*2;++i) {
        InputState input;if(i==0){input.roll_left_pressed=left;input.roll_right_pressed=!left;}
        game.advance(1.0/rate,input);
        const auto& pose=game.animation.state();
        if(pose.clip==(left?10u:11u) && previous.clip==pose.clip && pose.frame<previous.frame)++wraps;
        previous=pose;
        seen|=game.player.motion==Motion::RollLeft || game.player.motion==Motion::RollRight;
        for(auto v:game.animation.sample(.5f).vertices)check(finite(v),"Roll emitted nonfinite geometry");
    }
    check(seen==(enabled || crc==0x2c8235e7),"Roll activation ignored upgrade/build rules");
    check(!game.player.dodge.active,"A single press never finished rolling");
    check(!wraps,"A single tap played more than one authored roll");
    const auto sounds=std::count_if(game.sound_events.begin(),game.sound_events.end(),[](const auto& e){return e.scope==SoundScope::Global && e.id==july_roll_sound_id;});
    check(sounds==unsigned(enabled || crc==0x2c8235e7),"Tap roll did not sound once at its native frame event");
    return game.player.position-before;
}
void held_roll(const SpyroModel& model,bool left,unsigned held_ticks,unsigned crc=0xcdc0917c) {
    auto floor=floor_level();Game game(floor,scene(floor,crc),{},nullptr,&model);
    game.upgrades.enabled=true;game.upgrades.roll=true;
    for(unsigned i=0;i<60;++i)game.advance(FixedClock::step,{});
    const unsigned clip=left?10:11;unsigned wraps=0,release_wraps=0,stopped=0;
    game.sound_events.clear();
    auto previous=game.animation.state();
    for(unsigned i=0;i<held_ticks+90;++i) {
        InputState input;input.roll_left_pressed=left && i==0;input.roll_right_pressed=!left && i==0;
        input.roll_left_held=left && i<held_ticks;input.roll_right_held=!left && i<held_ticks;
        game.advance(FixedClock::step,input);
        if(i<held_ticks)check(game.player.dodge.active,"Holding roll stopped or restarted the movement");
        const auto& pose=game.animation.state();
        if(pose.clip==clip && previous.clip==clip && pose.frame<previous.frame) {
            ++wraps;if(i>=held_ticks)++release_wraps;
        }
        if(i>=held_ticks && !game.player.dodge.active && !stopped)stopped=i-held_ticks+1;
        previous=pose;
    }
    check(wraps>=1,"Holding the button failed to repeat the authored roll");
    check(!release_wraps,"Releasing roll started an unwanted extra revolution");
    check(stopped && stopped<=40 && game.animation.requested()!=clip,"Released roll did not finish its current cycle");
    const auto sounds=std::count_if(game.sound_events.begin(),game.sound_events.end(),[](const auto& e){return e.scope==SoundScope::Global && e.id==july_roll_sound_id;});
    check(sounds==wraps+1,"Held roll sound did not follow each completed animation revolution");
}
void spyro_events(const SpyroModel& model,unsigned crc) {
    auto floor=floor_level();Game game(floor,scene(floor,crc),{},nullptr,&model);
    for(unsigned i=0;i<60;++i)game.advance(FixedClock::step,{});
    game.sound_events.clear();
    for(unsigned i=0;i<90;++i){InputState in;in.move_z=1;game.advance(FixedClock::step,in);}
    check(std::count_if(game.sound_events.begin(),game.sound_events.end(),[](auto& e){return e.scope==SoundScope::Surface;})>4,
          "Native walking/running animation stopped emitting footstep sounds");
    game.sound_events.clear();InputState jump;jump.jump_pressed=jump.jump_held=true;
    game.advance(FixedClock::step,jump);
    check(std::count_if(game.sound_events.begin(),game.sound_events.end(),[](auto& e){return e.scope==SoundScope::Global && e.id==0x22;})==1,
          "Jump did not emit exactly one semantic jump event");
    for(unsigned i=0;i<100;++i)game.advance(FixedClock::step,{});
    game.sound_events.clear();InputState flame;flame.flame_pressed=true;game.advance(FixedClock::step,flame);
    check(std::count_if(game.sound_events.begin(),game.sound_events.end(),[](auto& e){return e.scope==SoundScope::Global && e.id==3;})==1,
          "Flaming did not emit exactly one semantic flame event");
}
}
int main(int argc,char** argv)try {
    const auto root=std::filesystem::path(argc>1?argv[1]:".");
    if(!std::filesystem::exists(root/"data/builds/july-1999/disc/SPYRO2.EXE") || !std::filesystem::exists(root/"data/disc/SCUS_944.25"))return 77;
    AssetStore july(root/"data/builds/july-1999/disc"),retail(root/"data/disc");
    const auto donor_exe=july.read_all(find_game_executable(july)),exe=retail.read_all(find_game_executable(retail));
    WadArchive donor_wad(july),wad(retail);const auto bank=decode_july_rolls(donor_wad.read(0),donor_exe);
    auto model=decode_spyro(wad.read(0),wad.read(15),exe);const auto neutral=model.animations.at(0);
    import_july_rolls(model,bank);
    spyro_events(model,0xcdc0917c);
    for(unsigned id:{10u,11u}) {
        const auto& clip=model.animations.at(id);check(clip.frames.size()==20,"Cut roll frame count changed");
        for(const auto& frame:clip.frames)check(frame.ticks==2 && frame.vertices.size()==230,"Cut roll timing/topology changed");
        for(unsigned f=0;f<clip.faces.size();++f)check(clip.faces[f].vertices==neutral.faces[f].vertices && clip.faces[f].uv==neutral.faces[f].uv && clip.faces[f].atlas_tile==neutral.faces[f].atlas_tile,"Imported roll replaced the mounted model's topology or texture coordinates");
    }
    const auto left=roll(model,true,60),right=roll(model,false,60);
    std::cout<<"Roll displacement: left "<<left.x<<','<<left.y<<','<<left.z<<"; right "<<right.x<<','<<right.y<<','<<right.z<<'\n';
    check(left.x < -2 && left.x > -2.8f && right.x > 2 && right.x < 2.8f && dot(left,right)<0,"Roll did not move in opposite lateral directions at the recovered speed");
    check(std::abs(left.z)<.001f && std::abs(right.z)<.001f,"Exiting the side roll introduced forward motion");
    for(unsigned rate:{30u,144u})check(length(roll(model,true,rate)-left)<.0001f,"Render rate changed 60 Hz roll movement");
    check(length(roll(model,true,60,false))<.001f,"Disabled PC roll still moved the player");
    check(length(roll(model,true,60,false,0x2c8235e7))>2,"July's native roll incorrectly needs a PC upgrade");
    // Sweep releases across every phase, including the final pose after the
    // queued endpoint has wrapped. This catches a second visible revolution
    // even if the movement state has already reported the roll as finished.
    for(unsigned phase=0;phase<38;++phase)for(bool left_side:{false,true})held_roll(model,left_side,90+phase);
    auto floor=floor_level();Game game(floor,scene(floor),{},nullptr,&model);
    game.upgrades.enabled=true;game.upgrades.points=30;
    check(game.upgrades.buy(Upgrade::Roll) && game.upgrades.roll && !game.upgrades.points,"Roll purchase failed");
    for(unsigned i=0;i<60;++i)game.advance(FixedClock::step,{});
    GameSave saved;saved.capture(game);
    const auto file=std::filesystem::temp_directory_path()/"spyro-prerelease-roll-test.s2save";
    saved.write(file);const auto restored=GameSave::read(file);std::filesystem::remove(file);
    Game loaded(floor,scene(floor),{},nullptr,&model);restored.apply(loaded,nullptr);
    check(loaded.upgrades.roll && loaded.upgrades.enabled,"Saved roll upgrade was lost");
    loaded.player.dodge.active=true;loaded.respawn();check(!loaded.player.dodge.active,"Respawn retained an active roll");
    for(const auto name:{"july-1999","august-1999","winter-jampack-1999"}) {
        AssetStore assets(root/"data/builds"/name/"disc");const auto executable=assets.read_all(find_game_executable(assets));
        const auto& profile=game_profile(executable);WadArchive source(assets,&profile);
        const auto archive=source.read(profile.first_level());
        const auto optional=[&](unsigned slot){return source.contains(slot)?source.read(slot):Bytes{};};
        const auto native=decode_spyro(source.read(0),archive,executable,optional(29),29,optional(15),optional(45),optional(59));
        const auto interface=decode_interface(archive,executable,{});
        check(!interface.rgba.empty() && interface.font['A'].colors[0].width,"Prerelease font is missing");
        check(!native.animations.empty(),"Empty native Spyro bank");
        spyro_events(native,profile.crc);
        if(native.animations.contains(10)) {
            roll(native,true,60,true,profile.crc);roll(native,false,60,true,profile.crc);
            held_roll(native,true,120,profile.crc);held_roll(native,false,127,profile.crc);
        }
        check(source.contains(profile.first_level()),"First available scene is missing");
        if(profile.demo())check(!source.contains(15) && source.contains(27) && source.contains(35),"Compilation demo borrowed unavailable retail levels");
        std::cout<<name<<": native model, supplemental movement, fonts and source capabilities OK\n";
    }
    std::cout<<"Cut roll: geometry, opposing movement, 30/60/144 Hz, upgrade gating, native July controls, save/load and respawn OK\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
