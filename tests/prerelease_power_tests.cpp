#include "spyro/powerup_import.hpp"
#include "spyro/game_build.hpp"
#include "spyro/travel.hpp"
#include "spyro/savegame.hpp"
#include <iostream>

using namespace spyro;
namespace {
unsigned checks{};
std::string context;
void check(bool ok,const char* why){++checks;if(!ok)throw FormatError(context+": "+why);}
Level floor_level() {
    Level l;l.wad_slot=15;l.spawn={0,.02f,0};l.bounds={{-200,0,-200},{200,200,200}};
    l.triangles={{{-200,0,-200},{200,0,200},{200,0,-200}},{{-200,0,-200},{-200,0,200},{200,0,200}}};return l;
}
void tick(Game& g,const SpyroModel& model,InputState in={}) {
    g.advance(FixedClock::step,in);
    // Exercise the same primary/secondary pose composition as the renderer,
    // including interpolation while switching between the two mesh families.
    for(float alpha:{0.0f,.5f,1.0f}) {
        const auto pose=model.secondary_track(g.animation.state().clip,g.animation.sample(alpha),g.secondary.sample(alpha));
        check(std::all_of(pose.vertices.begin(),pose.vertices.end(),[](Vec3 v){return finite(v);}),"Nonfinite composed power animation");
        const auto& clip=model.clip(g.animation.state().clip);
        for(const auto& f:clip.faces)for(unsigned k=0;k<(f.quad?4u:3u);++k)
            if(f.vertices[k]>=pose.vertices.size())throw FormatError(context+": composed pose/face topology mismatch");
    }
}
void settle(Game& g,const SpyroModel& model){for(unsigned i=0;i<65;++i)tick(g,model);}
void movement(const SpyroModel& model,const ObjectModels& objects,unsigned crc) {
    const auto l=floor_level();Scene s;s.wad_slot=15;s.retail_crc=crc;s.spawn=l.spawn;
    Game charge(l,s,{},&objects,&model);charge.cheats.supercharge=true;settle(charge,model);
    check(charge.powers.charge_enabled(charge),"Supercharge unavailable");
    unsigned ground=0,jump=0;float peak=0;
    for(unsigned i=0;i<125;++i) {
        InputState in;in.charge=i<100;in.jump_pressed=i==35;in.jump_held=i>=35 && i<40;
        tick(charge,model,in);ground+=charge.animation.state().clip==61;jump+=charge.animation.state().clip==25;
        peak=std::max(peak,charge.player.position.y);
    }
    check(ground>30 && jump>10 && peak>2 && charge.player.position.z>15,"Supercharge failed movement, mesh swap or powered jump");
    check(charge.charge_smoke.emitted()>5,"Supercharge omitted its smoke");
    check(charge.animation.state().clip!=61 && charge.animation.state().clip!=25,"Releasing charge did not restore normal motion");
    if(model.animations.contains(10)) {
        charge.upgrades.enabled=true;charge.upgrades.roll=true;
        InputState in;in.roll_left_pressed=true;tick(charge,model,in);
        for(unsigned i=0;i<65;++i)tick(charge,model);
        check(!charge.player.dodge.active,"Imported power transitions broke the July roll exit");
    }
    Game fly(l,s,{},&objects,&model);fly.cheats.superfly=true;settle(fly,model);
    InputState in;in.jump_pressed=in.jump_held=true;tick(fly,model,in);
    check(!fly.powers.flying && fly.player.velocity.y>0,"Superfly bypassed the normal first jump");
    in.jump_pressed=false;for(unsigned i=0;i<10;++i)tick(fly,model,in);
    in.jump_pressed=true;tick(fly,model,in);check(fly.powers.flying,"Second airborne tap did not start Superfly");
    for(unsigned i=0;i<150;++i) {
        in={};in.move_z=i<50?-1.0f:i<100?0.0f:1.0f;in.move_x=i>=40 && i<90?.3f:0;
        tick(fly,model,in);check(fly.powers.flying,"Flight unexpectedly stopped");
        const auto& wings=fly.powers.wings();
        check(wings[0].size()>=2 && wings[1].size()>=2,"A wing trail is missing");
        check(length(wings[0].front()-wings[1].front())>.35f,"Wing streaks overlap or are attached to the root");
        const auto pose=fly.animation.sample(1);
        for(unsigned wing=0;wing<2;++wing) {
            const auto expected=fly.player.position+retail::origin_above_feet+
                retail::rotate_model(pose.vertices.at(model.wing_tips[wing]),fly.player.yaw,fly.player.pitch,fly.player.roll);
            check(length(wings[wing].front()-expected)<.00001f,"Trail detached from its animated wing tip");
        }
    }
    fly.cheats.superfly=false;for(unsigned i=0;i<12;++i)tick(fly,model);
    check(!fly.powers.flying && fly.powers.wings()[0].empty() && fly.powers.wings()[1].empty(),"Ending flight left stale trails");
    auto wall=l;wall.triangles.push_back({{-10,0,4},{10,8,4},{10,0,4}});wall.triangles.push_back({{-10,0,4},{-10,8,4},{10,8,4}});
    Game bonk(wall,s,{},&objects,&model);bonk.cheats.supercharge=true;bonk.destruction_settings.enabled=true;settle(bonk,model);
    in={};in.charge=true;unsigned guard=0;
    while(!bonk.player.charge_impacts && ++guard<120)tick(bonk,model,in);
    check(bonk.player.charge_impacts==1 && bonk.player.motion==Motion::ChargeRecoil && bonk.player.velocity.z<0 && bonk.destruction.damage().size()==1,"Imported supercharge cannot deform a wall and enter retail recoil");
    bool visible_recoil=false;for(unsigned i=0;i<60;++i){tick(bonk,model);visible_recoil|=bonk.animation.state().clip==13;}
    check(visible_recoil && !bonk.player.recoil_ticks,"Demo/prototype wall recoil was absent or failed to recover");
}
void fire(const SpyroModel& model,const ObjectModels& objects,unsigned crc,bool bomb) {
    auto l=floor_level();
    l.triangles.push_back({{-10,0,3},{10,10,3},{10,0,3}});l.triangles.push_back({{-10,0,3},{-10,10,3},{10,10,3}});
    Scene s;s.wad_slot=15;s.retail_crc=crc;s.spawn=l.spawn;Game g(l,s,{},&objects,&model);
    g.cheats.superflame=true;g.cheats.green_firebomb=bomb;settle(g,model);
    InputState in;in.flame_pressed=true;tick(g,model,in);
    check(g.powers.secondary_action()==6,"Powered shot failed to start its windup");
    for(unsigned i=0;i<3;++i)tick(g,model);
    check(!g.powers.shots_fired,"Powered shot bypassed its windup");
    unsigned guard=0;while(!g.powers.bombs_exploded && !g.powers.flame_explosions && ++guard<120)tick(g,model);
    check(g.powers.shots_fired==1 && (bomb?g.powers.bombs_exploded:g.powers.flame_explosions)==1,"Projectile never launched or detonated on the wall");
    check(!g.powers.visuals().empty() && !g.powers.particles().empty() && !g.powers.sparks().empty(),"Impact omitted meshes, particles or spark streaks");
    if(bomb)check(length(g.powers.tint(g)-Vec3{1,1,1})<.001f,"Green-bomb cheat tinted normal Spyro");
    if(!bomb)check(std::count_if(g.powers.visuals().begin(),g.powers.visuals().end(),[](const auto& v){return v.kind==PowerVisualKind::FireSpoke;})==5,"Superflame omitted its five radial spokes");
    for(const auto& v:g.powers.visuals()) {
        const auto& bank=objects.models.at(v.object.type);const auto pose=sample_object_animation(bank,v.object.animation,objects.animation_blends);
        check(!pose.vertices.empty() && pose.mesh<bank.meshes.size(),"Effect has no renderable mesh");
    }
    const auto event=std::find_if(g.sound_events.begin(),g.sound_events.end(),[&](const auto& e){return e.scope==SoundScope::Object && e.object_type==(bomb?0x2c2u:0x1e3u);});
    check(event!=g.sound_events.end(),"Powered shot emitted no object sound event");
    for(unsigned i=0;i<150;++i)tick(g,model);
    check(g.powers.visuals().empty() && g.powers.projectiles().empty(),"Explosion failed to clean up");
}
void sounds(SoundBank bank,const RetailPowerupBank& donor) {
    const auto globals=bank.globals;import_object_sounds(bank,donor.sounds,retail_power_types,true);
    check(bank.globals==globals,"Retail import changed native Spyro sounds");
    for(unsigned type:{0x2c2u,0x1e3u,0x1f8u}) {
        const auto& expected=donor.sounds.object_sounds.at(type);const auto& ids=bank.object_sounds.at(type);
        check(!ids.empty() && ids.size()==expected.size(),"Imported effect sounds missing");
        for(unsigned i=0;i<ids.size();++i)if(expected[i]!=255) {
            check(ids[i]<bank.sounds.size(),"Imported effect sample index invalid");
            check(bank.sounds[ids[i]].pcm.samples==donor.sounds.sounds.at(expected[i]).pcm.samples,"Effect sample PCM changed during import");
        }
    }
}
}
int main(int argc,char** argv)try {
    const auto root=std::filesystem::path(argc>1?argv[1]:".");
    if(!std::filesystem::exists(root/"data/disc/SCUS_944.25") || !std::filesystem::exists(root/"data/builds/july-1999/disc/SPYRO2.EXE"))return 77;
    const auto donor=read_retail_powerups(root/"data/disc");
    bool rejected=false;try{read_retail_powerups(root/"data/builds/july-1999/disc");}catch(const FormatError&){rejected=true;}
    check(rejected,"Prototype was accepted as a retail donor");unsigned levels=0,hidden=0,returns=0;
    for(const auto name:{"july-1999","august-1999","winter-jampack-1999"}) {
        AssetStore store(root/"data/builds"/name/"disc");const auto exe=store.read_all(find_game_executable(store));
        const auto& profile=game_profile(exe);WadArchive wad(store,&profile);const auto global=wad.read(0);
        const auto optional=[&](unsigned slot){return wad.contains(slot)?wad.read(slot):Bytes{};};
        const auto ocean=optional(29),forest=optional(15),scorch=optional(45),marsh=optional(59);
        for(unsigned slot:profile.levels()) {
            if(slot>71)continue;context=std::string(name)+"/"+std::to_string(slot);
            const auto archive=wad.read(slot);auto model=decode_spyro(global,archive,exe,ocean,29,forest,scorch,marsh);
            auto objects=decode_object_models(archive,exe);auto effects=decode_powerup_assets(archive);
            const auto skin=model.texture_rgba;const auto height=model.texture_height;
            const auto native=model.animations.at(0);const auto local_wind=effects.whirlwind_particle.texture_rgba;
            import_retail_powerups(model,objects,effects,donor);
            check(std::equal(skin.begin(),skin.end(),model.texture_rgba.begin()),"Import overwrote mounted Spyro textures");
            check(model.animations.at(0).frames.size()==native.frames.size(),"Import replaced native idle animation");
            for(unsigned i=0;i<native.faces.size();++i)for(unsigned k=0;k<4;++k)
                check(std::abs(model.animations.at(0).faces[i].uv[k][1]*model.texture_height-native.faces[i].uv[k][1]*height)<.001f,"Import changed native atlas pixel coordinates");
            check(model.wing_tips==(profile.build==GameBuild::August1999?std::array<unsigned,2>{63,62}:std::array<unsigned,2>{58,57}),"Incorrect native wing anchors");
            for(unsigned clip:{25u,33u,34u,35u,61u})check(!model.animations.at(clip).frames.empty(),"Power motion missing");
            check(!model.charge_smoke.texture_rgba.empty() && !effects.wing_trail.texture_rgba.empty() && !effects.green_particle.texture_rgba.empty() && !effects.fire_particle.texture_rgba.empty(),"Power sprites missing");
            check(effects.whirlwind_particle.texture_rgba==local_wind,"Import replaced local whirlwind texture");
            for(unsigned type:retail_power_types)for(const auto& mesh:objects.models.at(type).meshes)for(const auto& f:mesh.faces)if(f.textured)
                for(auto uv:f.uv)check(uv[0]>=0 && uv[0]<=1 && uv[1]>=0 && uv[1]<=1,"Imported effect UV escaped atlas");
            auto level=decode_level(archive,slot);decode_travel(archive,exe,level);initialize_retail_objects(level,objects);
            for(const auto& o:level.objects)if(o.type==0x1aa) {
                const bool bound=std::any_of(level.portals.begin(),level.portals.end(),[&](const auto& p){return p.return_home && p.object==o.id;});
                check(o.hidden!=bound,"Unbound Return Home actor visible, or valid exit hidden");hidden+=o.hidden;returns+=bound;
                if(level.campaign_scene%10==0)check(o.hidden,"Home world contains a Return Home actor");
            }
            for(const auto& p:level.portals)if(p.return_home)check(level.campaign_scene%10!=0,"Home world contains an active Return Home portal");
            sounds(decode_sound_bank(archive,exe,slot),donor);
            movement(model,objects,profile.crc);fire(model,objects,profile.crc,false);fire(model,objects,profile.crc,true);
            ++levels;std::cout<<context<<": power motion, firing, impacts, sounds, wing trails and portals OK\n";
        }
    }
    check(levels==59,"Not all demo/prototype gameplay banks were exercised");
    check(hidden>0 && returns>0,"Portal audit missed orphan actors or legitimate exits");
    std::cout<<checks<<" checks across "<<levels<<" levels; "<<hidden<<" orphan actors hidden, "<<returns<<" legitimate exits retained\n";
    return 0;
}catch(const std::exception& e){std::cerr<<context<<": "<<e.what()<<'\n';return 1;}
