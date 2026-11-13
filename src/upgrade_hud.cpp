#include "spyro/upgrade_hud.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>

namespace spyro {
const char* upgrade_hud_mode_name(UpgradeHudMode mode) {
    switch(mode) {
    case UpgradeHudMode::Once:return "Once per power";
    case UpgradeHudMode::OnUse:return "On each use";
    case UpgradeHudMode::Manual:return "Manual - hold Tab";
    case UpgradeHudMode::Always:return "Always";
    case UpgradeHudMode::Off:return "Off";
    }
    return "Off";
}
const char* upgrade_hud_mode_description(UpgradeHudMode mode) {
    switch(mode) {
    case UpgradeHudMode::Once:return "Each power appears once this session, across all levels.";
    case UpgradeHudMode::OnUse:return "Show equipped powers briefly and icons while using them.";
    case UpgradeHudMode::Manual:return "Show your upgrade icons only while holding Tab.";
    case UpgradeHudMode::Always:return "Keep your unlocked upgrade icons on screen.";
    case UpgradeHudMode::Off:return "Hide all upgrade HUD icons and the points display.";
    }
    return "";
}
const char* upgrade_name(unsigned icon) {
    constexpr const char* names[]{"SUPERCHARGE","SUPERFLAME","SUPERFLY","GLIDE BOOST","AERIAL CARRY","MELEE","HEADBASH","DODGE ROLL"};
    return icon<upgrade_icon_count?names[icon]:"";
}
const char* upgrade_description(unsigned icon,unsigned tier) {
    switch(icon) {
    case 0:return tier<1?"Yellow flames. Supercharge at 130% speed.":"Red flames. Supercharge at 160% speed.";
    case 1:return tier<1?"Superflame seeks nearby enemies.":"Superflame splits into three on impact.";
    case 2:return tier<1?"Superfly at 125% speed.":"Superfly at 150% speed.";
    case 3:return tier<1?"One wing boost per glide. Land to refill.":"Two wing boosts per glide. Land to refill.";
    case 4:return "G: grab or drop. Shift+G: throw while flying.";
    case 6:return tier<1?"Headbash digs a crater around a safe central island.":"A lava wave fills the crater. The central island stays safe.";
    case 7:return "Z / V or L1 / R1: the recovered July dodge roll.";
    default:return "J: strike. Press again to chain three hits.";
    }
}
UpgradeHudSnapshot upgrade_hud_snapshot(const Game& g) {
    UpgradeHudSnapshot s;s.enabled=g.upgrades.enabled;s.points=g.upgrades.points;
    s.suppressed=g.dialogue.active() || g.scene_locked || g.camera.scripted_view.has_value() || g.portal_travel.active() ||
        g.systems.results() || (g.systems.mode!=ChallengeMode::None && g.systems.mode!=ChallengeMode::FreeFlight) || !g.systems.prompt.empty();
    constexpr Upgrade kinds[]{Upgrade::Supercharge,Upgrade::Superflame,Upgrade::Superfly,Upgrade::Glide,Upgrade::Carry,Upgrade::Melee,Upgrade::Headbash,Upgrade::Roll};
    const unsigned tiers[]{g.upgrades.charge,g.upgrades.flame,g.upgrades.flight,g.upgrades.glide,unsigned(g.upgrades.carry),unsigned(g.upgrades.melee),g.upgrades.headbash,unsigned(g.upgrades.roll)};
    const bool flying=g.powers.flying,gliding=g.player.air.gliding || g.player.air.boost_ticks;
    const bool available[]{g.powers.charge_enabled(g),g.powers.flame_enabled(g) && !g.cheats.green_firebomb,
        g.powers.fly_enabled(g),!flying && g.player.swim.mode==WaterMode::Dry,flying,true,g.upgrades.headbash_mode!=HeadbashMode::Normal && !flying && g.player.swim.mode==WaterMode::Dry,
        g.roll_available() && g.player.grounded && !flying && g.player.swim.mode==WaterMode::Dry};
    const bool fire=g.powers.secondary_action()!=0 || std::any_of(g.powers.projectiles().begin(),g.powers.projectiles().end(),[](const auto& p){return p.upgrade && !p.exploding;});
    const bool active[]{g.player.motion==Motion::Supercharge || g.player.motion==Motion::SuperchargeJump,fire,flying,
        g.player.air.boost_ticks!=0,g.abilities.carried!=UINT32_MAX || g.abilities.pickup_target!=UINT32_MAX,g.abilities.attacking(),g.player.air.headbash_ticks!=0 || g.player.air.recovery_ticks!=0,g.player.dodge.active};
    for(unsigned i=0;i<upgrade_icon_count;++i) {
        auto& item=s.items[i];item.upgrade=kinds[i];item.tier=tiers[i];item.maximum=(i==4 || i==5 || i==7)?1:2;
        item.asset_ready=i!=7 || g.roll_available();
        item.remaining=i==3?tiers[i]-std::min(tiers[i],g.player.air.boosts_used):tiers[i];
        item.status=!s.enabled || !tiers[i]?UpgradeStatus::Locked:!available[i]?UpgradeStatus::Dormant:active[i]?UpgradeStatus::Active:UpgradeStatus::Ready;
        if(i==3 && item.status==UpgradeStatus::Ready && !item.remaining)item.status=UpgradeStatus::Spent;
        item.contextual=s.enabled && tiers[i] && available[i] && (active[i] || (i==3 && gliding));
        item.equipped=s.enabled && tiers[i] && i<3 && available[i];
        if(item.status==UpgradeStatus::Locked)item.value="LOCKED";
        else if(item.status==UpgradeStatus::Dormant)item.value=i<3?"POWER":i==3?"LAND":i==6?"NORMAL":i==7?(item.asset_ready?"LAND":"IMPORT"):"FLY";
        else switch(i) {
            case 0:item.value=tiers[i]==1?"130%":"160%";break;
            case 1:item.value=tiers[i]==1?"SEEK":"SPLIT";break;
            case 2:item.value=tiers[i]==1?"125%":"150%";break;
            case 3:item.value=std::to_string(item.remaining)+"/"+std::to_string(tiers[i]);break;
            case 4:item.value=g.abilities.carried!=UINT32_MAX?"HELD":g.abilities.pickup_target!=UINT32_MAX?"SEEK":"READY";break;
            case 5:item.value=g.abilities.attacking()?std::to_string(std::min(g.abilities.combo+1,3u))+"/3":"READY";break;
            case 6:item.value=g.upgrades.headbash_mode==HeadbashMode::Lava && tiers[i]>=2?"LAVA":"CRATER";break;
            case 7:item.value=g.player.dodge.active?"ROLL":"READY";break;
        }
    }
    return s;
}
void UpgradeHudDisplay::hide() {
    opacity={};points_opacity=0;linger_={};equipped_={};
}
void UpgradeHudDisplay::update(const UpgradeHudSnapshot& s,float seconds,bool recall,const UpgradeHudSettings& settings) {
    const float dt=std::clamp(seconds,0.0f,0.1f);
    if(settings_!=settings){hide();settings_=settings;}
    if(!s.enabled || s.suppressed || settings.mode==UpgradeHudMode::Off) {hide();return;}
    const auto fade=[&](float& a,bool wanted){a=std::clamp(a+(wanted?dt*8:-dt*3),0.0f,1.0f);};
    bool any=false;
    for(unsigned i=0;i<upgrade_icon_count;++i) {
        const auto& item=s.items[i];const bool owned=item.tier!=0;
        linger_[i]=std::max(0.0f,linger_[i]-dt);
        bool wanted=false;
        switch(settings.mode) {
        case UpgradeHudMode::Once:
            if(owned && dt>0 && !(seen_&(1u<<i)) && (item.equipped || item.contextual))linger_[i]=float(settings.seconds);
            wanted=owned && linger_[i]>0;break;
        case UpgradeHudMode::OnUse:
            if(owned && (item.contextual || (item.equipped && !equipped_[i])))linger_[i]=float(settings.seconds);
            wanted=owned && (linger_[i]>0 || recall);break;
        case UpgradeHudMode::Manual:wanted=owned && recall;break;
        case UpgradeHudMode::Always:wanted=owned;break;
        case UpgradeHudMode::Off:break;
        }
        equipped_[i]=item.equipped;
        if(wanted && dt>0)seen_|=1u<<i;
        fade(opacity[i],wanted);any|=wanted;
    }
    fade(points_opacity,any);
}
} // namespace spyro
