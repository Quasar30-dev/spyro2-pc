#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

using namespace spyro;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void ticks(Game& game,unsigned count,InputState input={}){for(unsigned i=0;i<count;++i)game.advance(FixedClock::step,input);}
Level floor_level() {
    Level l;l.wad_slot=15;l.spawn={0,8,0};l.bounds={{-200,0,-200},{200,100,200}};
    l.triangles={{{-200,8,-200},{200,8,200},{200,8,-200}},{{-200,8,-200},{-200,8,200},{200,8,200}}};return l;
}
Scene scene(const Level& l,float y=8){Scene s;s.wad_slot=l.wad_slot;s.spawn={0,y,0};return s;}
void basic() {
    const auto level=floor_level();InputState jump;jump.jump_pressed=true;
    Game tap(level,scene(level));ticks(tap,2);tap.advance(FixedClock::step,jump);ticks(tap,7);tap.advance(FixedClock::step,jump);
    ticks(tap,16);check(tap.player.air.gliding && tap.player.motion==Motion::Glide,"Second tap did not queue glide through ascent / release");
    // The retail tap jump is much shorter than the former placeholder launch.
    // Check forward travel before it reaches the floor, not after landing.
    const auto start=tap.player.position;ticks(tap,8);check(tap.player.position.z>start.z+0.2f && tap.player.air.gliding,"Glide still needs a held button or forward input");
    const float ceiling=tap.player.air.glide_ceiling;InputState special;special.special_pressed=true;tap.advance(FixedClock::step,special);
    check(tap.player.motion==Motion::Hover && tap.player.velocity.y>0 && !tap.player.air.gliding,"Hover did not lift out of glide");
    float high=tap.player.position.y;for(unsigned i=0;i<45;++i){tap.advance(FixedClock::step,{});high=std::max(high,tap.player.position.y);}
    check(high<=ceiling+0.001f,"Repeated hover can gain unlimited height");
    // Triangle again after a hover headbashes: during its lift, mid-hover
    // and once it has ended (a controller has no separate headbash key).
    for(const unsigned wait:{2u,20u,140u}) {
        Game after(level,scene(level,40));ticks(after,2);after.advance(FixedClock::step,jump);ticks(after,7);after.advance(FixedClock::step,jump);ticks(after,24);
        after.advance(FixedClock::step,special);check(after.player.motion==Motion::Hover,"Hover did not start before the headbash check");
        ticks(after,wait);after.advance(FixedClock::step,special);
        if(after.player.motion!=Motion::Headbash)std::cout<<"after hover +"<<wait<<": motion "<<int(after.player.motion)<<" grounded "<<after.player.grounded<<" hover "<<after.player.air.hover_ticks
            <<" gliding "<<after.player.air.gliding<<" headbash "<<after.player.air.headbash_ticks<<" y "<<after.player.position.y<<"\n";
        check(after.player.motion==Motion::Headbash && after.player.air.headbash_ticks,"Triangle after a hover did not headbash");
    }
    Game baseline(level,scene(level,40));baseline.advance(FixedClock::step,jump);ticks(baseline,150);
    for(unsigned tier:{0u,1u,2u}) {
        Game boosted(level,scene(level,40));boosted.upgrades.enabled=true;boosted.upgrades.glide=tier;boosted.advance(FixedClock::step,jump);
        for(unsigned i=0;i<150;++i){InputState press;press.jump_pressed=i==10 || i==60 || i==110;boosted.advance(FixedClock::step,press);}
        check(boosted.player.glide_boosts==tier && boosted.player.air.boosts_used==tier,"Glide boost limit is not enforced");
        check(boosted.player.air.gliding && !boosted.powers.flying,"A wing boost became infinite Superfly or cancelled glide");
        if(tier)check(boosted.player.position.y>baseline.player.position.y,"Wing boosts did not extend the glide");
        boosted.upgrades.enabled=false;boosted.advance(FixedClock::step,jump);
        check(!boosted.player.air.boost_ticks && boosted.player.glide_boosts==tier,"Disabled upgrades accepted a wing boost");
        boosted.player.position={0,8,0};boosted.player.velocity={};boosted.player.grounded=true;boosted.advance(FixedClock::step,{});
        check(boosted.player.air.boosts_used==0,"Landing did not recharge wing boosts");
    }
    Game held(level,scene(level,40));held.upgrades={true,0,0,0,0,false,false,2};held.advance(FixedClock::step,jump);
    InputState button;button.jump_held=true;ticks(held,120,button);
    check(held.player.glide_boosts==0,"Holding jump consumed boosts without a new press");
    Game bash(level,scene(level));ticks(bash,2);bash.advance(FixedClock::step,jump);ticks(bash,10);
    InputState down;down.headbash_pressed=true;bash.advance(FixedClock::step,down);
    check(bash.player.motion==Motion::Headbash && bash.player.velocity.y>0,"Headbash flip did not start in the air");
    ticks(bash,43);check(bash.player.velocity.y<0 && bash.player.air.headbash_ticks>42,"Headbash never entered its downward strike");
    ticks(bash,120);check(bash.player.headbash_impacts==1 && bash.player.grounded && std::abs(bash.player.position.y-8)<0.01f,"Headbash missed the floor, repeated impact or failed to recover");
    auto ramp=level;ramp.triangles={{{-10,18,-10},{10,8,10},{10,18,-10},0,false,10},{{-10,18,-10},{-10,8,10},{10,8,10},1,false,10}};
    auto spawn=scene(ramp,13.05f);Game slide(ramp,spawn);ticks(slide,90);
    check(slide.player.slide_ticks>60 && slide.player.position.z>3 && slide.player.position.y<12,"Authored sand did not slide downhill without input");
    slide.advance(FixedClock::step,jump);check(!slide.player.slide.active && slide.player.velocity.y>0,"Jump cannot exit the sand slide");
    for(auto& t:ramp.triangles)t.surface_type=UINT32_MAX;
    Game stone(ramp,spawn);ticks(stone,90);check(!stone.player.slide_ticks && std::abs(stone.player.position.z)<1,"Ordinary sloping floors became slippery sand");
    GameSave save;save.capture(held);save.upgrades.enabled=true;save.upgrades.glide=2;
    const auto file=std::filesystem::temp_directory_path()/"spyro-aerial-save.s2save";save.write(file);
    check(GameSave::read(file).upgrades.glide==2,"Save lost the glide upgrade");
    // An actual v4 payload, independent of the current writer's extra records.
    std::ostringstream fixture;fixture<<0xcdc0917cu<<" 15 0 3 3 0 0 1 0 0 0 0 0 0\n0 1 0 0 0\n1\n15 0 8 0 0 0 8 0\n0\n0\n0\n0\n0\n1 0 0 0 0 0 0\n";
    const auto payload=fixture.str();
    {std::ofstream legacy(file,std::ios::binary);legacy<<"S2SAVE 4 "<<crc32({reinterpret_cast<const std::uint8_t*>(payload.data()),payload.size()})<<' '<<payload.size()<<'\n'<<payload;}
    const auto old=GameSave::read(file);check(old.upgrades.enabled && old.upgrades.glide==0,"Version 4 saves did not load with glide boosts locked");
    std::filesystem::remove(file);
    UpgradeProgress upgrades;upgrades.enabled=true;upgrades.points=75;
    check(upgrades.buy(Upgrade::Glide) && upgrades.buy(Upgrade::Glide) && !upgrades.buy(Upgrade::Glide) && upgrades.glide==2 && !upgrades.points,"Glide upgrade purchase/cap is wrong");
}
void check_retail(const std::filesystem::path& path) {
    AssetStore assets(path);WadArchive wad(assets);const auto global=wad.read(0),exe=assets.read_all("SCUS_944.25"),power=wad.read(29);
    unsigned sand_count=0;
    for(unsigned slot=15;slot<=95;slot+=2) {
        const auto archive=wad.read(slot);const auto level=decode_level(archive,slot);const auto model=decode_spyro(global,archive,exe,power,29);
        check(model.animations.contains(17) && model.animations.contains(46) && model.animations.contains(34),"A retail bank lacks an aerial ability clip");
        if(slot==45) {
            sand_count=static_cast<unsigned>(std::count_if(level.triangles.begin(),level.triangles.end(),[](const auto& t){return t.surface_type==10;}));
            check(sand_count==1276 && model.animations.contains(49),"Scorch sand classification or native slide clip is missing");
        }
        const bool slippery=std::any_of(level.triangles.begin(),level.triangles.end(),[](const auto& t){return t.surface_type==10;});
        check(!slippery || model.animations.contains(49),"A slippery retail scene lacks its slide animation");
        SpyroAnimator animator;animator.reset(&model,0);
        const std::vector<unsigned> motions=slippery?std::vector<unsigned>{2,49,0,5,16,34,16,17,46,0}:std::vector<unsigned>{5,16,34,16,17,46,0};
        for(auto clip:motions)for(unsigned i=0;i<90;++i) {animator.tick(clip);const auto pose=animator.sample(0.37f);
            check(pose.vertices.size()==230 && std::all_of(pose.vertices.begin(),pose.vertices.end(),[](Vec3 v){return finite(v);}),"Aerial animation transition corrupted the retail mesh");}
    }
    const auto archive=wad.read(17);const auto source=decode_level(archive,17),arena_template=floor_level();
    const auto models=decode_object_models(archive,exe);const auto model=decode_spyro(global,archive,exe,power,29);
    for(unsigned type:{0xc8u,0xc9u,0xd1u}) {
        const auto found=std::find_if(source.objects.begin(),source.objects.end(),[&](const auto& o){return o.type==type;});
        if(found==source.objects.end())continue;
        auto arena=arena_template;auto object=*found;object.id=0;object.position=object.previous_position={0,8,0};
        object.container->rest_position=object.position;arena.objects={object};
        Game strike(arena,scene(arena,12),{},&models,&model);InputState bash;bash.headbash_pressed=true;strike.advance(FixedClock::step,bash);ticks(strike,150);
        check(strike.retail_objects[0].state<0 && strike.player.headbash_impacts==1,"Headbash passed through a retail container without breaking it");
    }
    std::cout<<"41 retail banks checked; Scorch sliding triangles: "<<sand_count<<'\n';
}
}
int main(int argc,char** argv){try{basic();if(argc>1)check_retail(argv[1]);std::cout<<checks<<" aerial / sand checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<"Aerial check "<<checks<<": "<<e.what()<<'\n';return 1;}}
