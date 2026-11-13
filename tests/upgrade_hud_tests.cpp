#include "spyro/upgrade_hud.hpp"
#include "spyro/runtime.hpp"
#include "spyro/savegame.hpp"
#include <chrono>
#include <iostream>
using namespace spyro;
unsigned checks=0;
void check(bool pass,const char* why){++checks;if(!pass)throw FormatError(why);}
void policy_checks() {
    UpgradeHudSnapshot s;s.enabled=true;for(auto& i:s.items)i.tier=1;
    UpgradeHudDisplay d;
    const auto tick=[&](unsigned count,bool recall=false,UpgradeHudSettings settings={}) {
        for(unsigned t=0;t<count;++t)d.update(s,1.0f/60,recall,settings);
    };
    const auto hidden=[&]{return d.points_opacity==0 && std::all_of(d.opacity.begin(),d.opacity.end(),[](float a){return a==0;});};
    tick(30,true);check(hidden() && !d.seen(),"Idle or Tab displayed an unused power in once mode");
    s.items[0].equipped=true;tick(15);check(d.opacity[0]==1 && d.opacity[1]==0 && d.seen()==1,"Newly equipped power did not show exactly its icon");
    s.items[0].contextual=true;tick(300);check(hidden(),"Holding an ability kept its once-only reminder on screen");
    s.points=100;tick(30,true);check(hidden(),"Points or Tab replayed the once-only strip");
    s.items[0].equipped=s.items[0].contextual=false;tick(20);s.items[0].equipped=true;tick(20);check(hidden(),"Re-equipping the same power replayed its reminder");
    d.hide();tick(20);check(hidden() && d.seen()==1,"Level/menu presentation reset erased session history");
    s.enabled=false;tick(20);s.enabled=true;tick(20);check(hidden(),"Toggling progression re-armed seen powers");
    s.suppressed=true;s.items[1].contextual=true;tick(30);check(hidden() && d.seen()==1,"A cutscene consumed an unseen reminder");
    s.suppressed=false;tick(15);check(d.opacity[1]==1 && d.seen()==3,"Unseen power did not get its reminder after cutscene");
    d.hide();tick(15);check(hidden(),"Closing a menu restarted an interrupted reminder");
    for(unsigned i=2;i<upgrade_icon_count;++i)s.items[i].contextual=true;
    tick(15);check(d.seen()==((1u<<upgrade_icon_count)-1) && d.opacity[5]==1 && d.opacity[7]==1 && d.opacity[0]==0,"New powers did not form a row without replaying old icons");
    tick(600,true);check(hidden(),"HUD returned after every power had been shown");
    s.items[0].tier=2;s.items[3].remaining=0;tick(30);check(hidden(),"Tier/counter changes replayed once-only reminders");
    UpgradeHudSettings manual{UpgradeHudMode::Manual,3};tick(20,false,manual);check(hidden(),"Manual mode automatically displayed icons");
    tick(15,true,manual);check(d.opacity[0]==1 && d.opacity[5]==1,"Manual mode failed to show icons on Tab");
    tick(25,false,manual);check(hidden(),"Manual mode did not hide after releasing Tab");
    UpgradeHudSettings off{UpgradeHudMode::Off,3};tick(20,true,off);check(hidden(),"Off ignored Tab or an active ability");
    UpgradeHudSettings always{UpgradeHudMode::Always,3};tick(20,false,always);check(d.opacity[0]==1,"Always mode is not visible");
    s.suppressed=true;tick(1,true,always);check(hidden(),"Always mode overlapped a cutscene");s.suppressed=false;
    tick(20);check(hidden(),"Switching back to Once erased session history");
    UpgradeHudSettings each{UpgradeHudMode::OnUse,1};tick(20,false,each);check(d.opacity[5]==1,"On-use setting failed to show an active ability");
    for(auto& i:s.items)i.contextual=i.equipped=false;tick(90,false,each);check(hidden(),"On-use timer did not expire");
    s.items[5].contextual=true;tick(20,false,each);check(d.opacity[5]==1,"On-use setting failed to repeat a later use");
    UpgradeHudDisplay fresh;s.items[0].equipped=true;
    fresh.update(s,1.0f/60,false,off);check(!fresh.seen(),"Off consumed session reminders");
    for(unsigned i=0;i<20;++i)fresh.update(s,1.0f/60,false);
    check(fresh.opacity[0]==1,"A fresh application session inherited another session's history");
}
void saved_settings(Game& game) {
    const auto path=std::filesystem::temp_directory_path()/("spyro-hud-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s2save");
    GameSave save;save.capture(game);
    for(unsigned mode=0;mode<5;++mode)for(unsigned seconds:{1u,3u,8u}) {
        game.upgrade_hud_settings={static_cast<UpgradeHudMode>(mode),seconds};save.capture(game);save.write(path);
        const auto loaded=GameSave::read(path);check(loaded.upgrade_hud_settings==game.upgrade_hud_settings,"Save lost HUD display policy/duration");
        Game restored(game.level,game.play_scene);loaded.apply(restored,nullptr);
        check(restored.upgrade_hud_settings==game.upgrade_hud_settings,"Loading did not apply HUD settings to gameplay");
    }
    // Cut back to the v11 layout (one level, no craters): v19 shards, v18 script memory, v17 cuts + cutting settings, v16 crater
    // tints + elements, v15 damage count + destruction settings, v14 roll, v13 crater count +
    // headbash tier, v12 HUD settings.
    std::ifstream file(path,std::ios::binary);std::string header;std::getline(file,header);
    std::string payload((std::istreambuf_iterator<char>(file)),{});file.close();
    for(unsigned line=0;line<12;++line)payload.erase(payload.rfind('\n',payload.size()-2)+1);
    {std::ofstream old(path,std::ios::binary);old<<"S2SAVE 11 "<<crc32({reinterpret_cast<const std::uint8_t*>(payload.data()),payload.size()})<<' '<<payload.size()<<'\n'<<payload;}
    check(GameSave::read(path).upgrade_hud_settings==UpgradeHudSettings{},"Older saves did not get the quiet default");
    for(auto setting:{UpgradeHudSettings{static_cast<UpgradeHudMode>(5),3},UpgradeHudSettings{UpgradeHudMode::Once,0},UpgradeHudSettings{UpgradeHudMode::Once,9}}) {
        save.upgrade_hud_settings=setting;bool rejected=false;try{save.validate();}catch(const FormatError&){rejected=true;}
        check(rejected,"Save accepted an invalid HUD policy/duration");
    }
    std::filesystem::remove(path);
}
int main() {try {
    Level level;level.wad_slot=17;level.spawn={0,10,0};level.bounds={{-100,0,-100},{100,100,100}};
    Scene scene;scene.wad_slot=17;scene.spawn=level.spawn;Game g(level,scene);
    g.upgrades.charge=g.upgrades.flame=g.upgrades.flight=g.upgrades.glide=2;g.upgrades.carry=g.upgrades.melee=true;
    auto s=upgrade_hud_snapshot(g);UpgradeHudDisplay d;
    for(unsigned i=0;i<60;++i)d.update(s,1.0f/60,true);
    check(d.points_opacity==0 && d.opacity[0]==0,"Disabled upgrade HUD is visible");
    g.upgrades.enabled=true;s=upgrade_hud_snapshot(g);
    check(s.items[0].status==UpgradeStatus::Dormant && s.items[1].value=="POWER" && s.items[2].status==UpgradeStatus::Dormant,"Locked base powers are shown as ready");
    check(s.items[4].value=="FLY" && s.items[5].value=="READY","Carry/melee availability is wrong");
    for(unsigned i=0;i<20;++i)d.update(s,1.0f/60,false);
    check(d.opacity[0]==0 && d.points_opacity==0,"Enabling progression advertised unused powers");
    for(unsigned i=0;i<300;++i)d.update(s,1.0f/60,false);
    check(d.opacity[0]==0 && d.points_opacity==0,"Idle HUD did not fade away");
    for(unsigned i=0;i<20;++i)d.update(s,1.0f/60,true);
    check(d.opacity[5]==0,"Tab bypassed the once-only policy");
    g.cheats.supercharge=g.cheats.superflame=g.cheats.superfly=true;
    g.player.motion=Motion::Supercharge;s=upgrade_hud_snapshot(g);
    check(s.items[0].status==UpgradeStatus::Active && s.items[0].value=="160%","Charge HUD does not match red tier");
    check(s.items[1].value=="SPLIT" && s.items[2].value=="150%","Flame/flight tier mapping failed");
    g.cheats.green_firebomb=true;s=upgrade_hud_snapshot(g);check(s.items[1].status==UpgradeStatus::Dormant,"Green bomb incorrectly advertises flame upgrades");
    g.powers.flying=true;g.abilities.carried=2;s=upgrade_hud_snapshot(g);
    check(s.items[4].status==UpgradeStatus::Active && s.items[4].value=="HELD","Held object is not reflected in HUD");
    check(s.items[3].status==UpgradeStatus::Dormant,"Glide boosts advertised during superfly");
    g.abilities.carried=UINT32_MAX;g.abilities.pickup_assist_ticks=48;s=upgrade_hud_snapshot(g);
    check(s.items[4].value=="READY" && !s.items[4].contextual,"Old pickup timer leaves SEEK stuck on screen");
    g.abilities.pickup_target=1;s=upgrade_hud_snapshot(g);check(s.items[4].value=="SEEK","Actual pickup target is not shown");
    g.powers.flying=false;g.player.air.gliding=true;g.player.air.boosts_used=1;s=upgrade_hud_snapshot(g);
    check(s.items[3].value=="1/2" && s.items[3].contextual,"Used glide boost was not deducted");
    g.player.air.boosts_used=2;s=upgrade_hud_snapshot(g);
    check(s.items[3].status==UpgradeStatus::Spent && s.items[3].value=="0/2" && s.items[3].contextual,"Exhausted glide counter should remain visible until landing");
    g.player.air={};s=upgrade_hud_snapshot(g);check(s.items[3].value=="2/2","Landing did not refill HUD counter");
    g.abilities.combo=2;g.abilities.combo_tick=12;s=upgrade_hud_snapshot(g);check(s.items[5].value=="3/3","Melee combo display has wrong index");
    g.scene_locked=true;s=upgrade_hud_snapshot(g);d.update(s,1.0f/60,true);
    check(s.suppressed && d.opacity[5]==0 && d.points_opacity==0,"Scene camera failed to suppress upgrade HUD");
    g.scene_locked=false;g.systems.mode=ChallengeMode::Trolley;s=upgrade_hud_snapshot(g);check(s.suppressed,"Upgrade HUD overlaps trolley HUD");
    g.systems.mode=ChallengeMode::None;g.upgrades.points=23;s=upgrade_hud_snapshot(g);
    g.cheats={};g.player.air={};g.abilities={};g.player.motion=Motion::Idle;s=upgrade_hud_snapshot(g);
    for(unsigned i=0;i<20;++i)d.update(s,1.0f/60,false);check(d.points_opacity==0,"Earning points replayed the HUD");
    policy_checks();saved_settings(g);
    const auto art=upgrade_sprite_assets();check(art.width==384 && art.height==64 && art.rgba.size()==98304,"Wrong embedded atlas size");
    for(unsigned i=0;i<6;++i) {
        const auto& r=art.sprites[i];unsigned visible=0,transparent=0;
        for(unsigned y=0;y<r.height;++y)for(unsigned x=0;x<r.width;++x){auto a=art.rgba[((r.y+y)*art.width+r.x+x)*4+3];visible+=a>128;transparent+=a==0;}
        check(visible>250 && transparent>1000,"Sprite is empty or has an opaque backdrop");
    }
    std::cout<<checks<<" upgrade HUD checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
