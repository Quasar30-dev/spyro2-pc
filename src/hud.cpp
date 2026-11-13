#include "spyro/game_build.hpp"
#include "spyro/hud.hpp"
#include <algorithm>
namespace spyro {
HudAssets decode_hud(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> exe) {
    const ExeTables tables(exe);
    const auto children=parse_level_archive(archive);
    if(children.empty() || children[0].size<0x80000)throw FormatError("Missing HUD VRAM");
    const auto vram=archive.subspan(children[0].offset,0x80000);
    const auto word=[&](unsigned x,unsigned y){if(x<512 || x>=1024 || y>=512)throw FormatError("HUD texture outside VRAM");return read_u16(vram,((y*512)+(x-512))*2);};
    HudAssets result;result.rgba.resize(result.width*result.height*4);
    std::copy_n(exe.begin()+tables(0x80063430),24,result.enter_curve.begin());
    std::copy_n(exe.begin()+tables(0x80063448),24,result.exit_curve.begin());
    std::copy_n(exe.begin()+tables(0x800634e8),4,result.eye_x.begin());
    std::copy_n(exe.begin()+tables(0x800634f4),4,result.eye_y.begin());
    for(unsigned i=0;i<result.sprites.size();++i) {
        unsigned type=0;
        if(i>=11 && i<=21)type=1;else if(i==31)type=9;else if(i>=32)type=10;else if(i>=10)continue;
        // 800520cc: four-byte UV/CLUT descriptor; TPAGE 38 from 80053e78.
        const auto p=tables(0x8006351c)+i*4;const unsigned u=exe[p],v=exe[p+1];
        const unsigned clut=0x4020+(exe[p+2]&3)+exe[p+3]*64;
        auto& sprite=result.sprites[i];sprite={i%8*64,i/8*64,exe[tables(0x800634b8)+type],exe[tables(0x800634d0)+type]};
        if(u+sprite.width>256 || v+sprite.height>256 || sprite.width>64 || sprite.height>64)throw FormatError("Invalid HUD sprite extent");
        for(unsigned y=0;y<sprite.height;++y)for(unsigned x=0;x<sprite.width;++x) {
            const auto index=(word(512+(u+x)/4,256+v+y)>>(((u+x)&3)*4))&15;
            const auto color=word((clut&63)*16+index,clut>>6);
            const auto at=((sprite.y+y)*result.width+sprite.x+x)*4;
            for(unsigned c=0;c<3;++c){const auto n=(color>>(c*5))&31;result.rgba[at+c]=static_cast<std::uint8_t>((n<<3)|(n>>2));}
            result.rgba[at+3]=color?255:0;
        }
    }
    return result;
}
namespace {
void counter_tick(HudCounter& c,unsigned goal,bool forced,bool suppress) {
    // 8005199c, candidate_ticks=2. The three standard counters hold for
    // 0x78 VBlanks; entry/exit state durations are 0x30 VBlanks.
    const auto change=[&](HudPhase phase){c.phase=phase;c.age=0;};
    switch(c.phase) {
        case HudPhase::Hidden:
            if(suppress)c.value=goal;
            else if(forced || c.value!=goal)change(HudPhase::Entering);
            break;
        case HudPhase::Entering:
            c.age+=2;
            if(suppress){c.phase=HudPhase::Exiting;c.age=48-std::min(c.age,48u);}
            else if(c.age>=48)change(c.value==goal?HudPhase::Holding:HudPhase::Counting);
            break;
        case HudPhase::Counting: {
            if(suppress){change(HudPhase::Exiting);c.value=goal;break;}
            c.age+=2;const auto difference=c.value>goal?c.value-goal:goal-c.value;
            if(c.age>5 || (difference>9 && c.age>3) || difference>39) {
                const unsigned step=difference>39?3:1;
                if(c.value<goal)c.value+=std::min(step,goal-c.value);
                else if(c.value>goal)c.value-=std::min(step,c.value-goal);
                c.age=0;if(c.value==goal)c.phase=HudPhase::Holding;
            }
            break;
        }
        case HudPhase::Holding:
            if(suppress){change(HudPhase::Exiting);c.value=goal;}
            else if(c.value!=goal)change(HudPhase::Counting);
            else if(c.age<120)c.age+=2;
            else if(!forced)change(HudPhase::Exiting);
            break;
        case HudPhase::Exiting:
            if(!suppress && c.age>=28 && (c.value!=goal || forced))change(HudPhase::Entering);
            else {c.age+=2;if(c.age>=48)c.phase=HudPhase::Hidden;}
            break;
    }
}
float counter_offset(const HudCounter& c,const HudAssets& assets,int delay) {
    if(c.phase==HudPhase::Hidden)return 0;
    if(c.phase!=HudPhase::Entering && c.phase!=HudPhase::Exiting)return 40;
    const auto index=std::clamp(static_cast<int>(c.age)+delay,0,47)/2;
    return (c.phase==HudPhase::Entering?assets.enter_curve:assets.exit_curve)[index];
}
}
void HudController::reset(unsigned gems,unsigned spirits,unsigned lives) {
    current_={HudCounter{HudPhase::Hidden,0,gems},HudCounter{HudPhase::Hidden,0,spirits},HudCounter{HudPhase::Hidden,0,lives}};
    previous_=current_;phase_=recall_ticks_=0;blink_=36;
    boss_=previous_boss_={};boss_goal_.reset();boss_frames_=boss_eye_=0;boss_blinking_=false;
    power_age_=previous_power_age_=0;power_fill_=0;power_exiting_=false;
}
void HudController::tick(unsigned gems,unsigned spirits,unsigned lives,bool recall,bool suppress) {
    if(recall)recall_ticks_=90; // 800159c8: input bit 0x100 recalls all counters.
    if((phase_++&1)!=0)return;
    previous_=current_;const std::array<unsigned,3> values{gems,spirits,lives};
    for(unsigned i=0;i<3;++i)counter_tick(current_[i],values[i],recall_ticks_!=0,suppress);
    if(recall_ticks_)recall_ticks_=recall_ticks_>2?recall_ticks_-2:0;
    previous_boss_=boss_;
    if(boss_goal_) {
        // Overlay 30 8006fdd8 / overlay 54 8006f0f0: +1 = 3 keeps the counter
        // up, then 8005199c eases it to the boss's health (80066fec).
        counter_tick(boss_,*boss_goal_,true,suppress);
        // 8006fe80 / 8006f198: every `period` frames a blink ends, or starts
        // one time in 16; its frame runs 1,2,3,2,1 (Crush) or 1,2,1 (Gulp).
        if(boss_frames_%boss_period_==0)boss_blinking_=boss_blinking_?false:(random_.next()&15)==0;
        unsigned eye=boss_blinking_?boss_frames_%boss_period_+1:0;
        if(eye>=(boss_period_+3)/2)eye=boss_period_+1-eye;
        boss_eye_=eye;++boss_frames_;
    }
    if(current_[2].phase!=HudPhase::Hidden) {
        if(blink_)--blink_;
        if(!blink_)blink_=(random_.next()&63)+36;
    }
}
bool HudController::visible(HudPart part) const {
    const auto i=static_cast<unsigned>(part);
    return current_[i].phase!=HudPhase::Hidden || previous_[i].phase!=HudPhase::Hidden;
}
float HudController::offset(HudPart part,const HudAssets& assets,float alpha,int delay) const {
    const auto i=static_cast<unsigned>(part);const float blend=((phase_&1)?0.0f:0.5f)+std::clamp(alpha,0.0f,1.0f)*0.5f;
    return std::lerp(counter_offset(previous_[i],assets,delay),counter_offset(current_[i],assets,delay),blend);
}
float HudController::boss_offset(const HudAssets& assets,float alpha) const {
    const float blend=((phase_&1)?0.0f:0.5f)+std::clamp(alpha,0.0f,1.0f)*0.5f;
    return std::lerp(counter_offset(previous_boss_,assets,0),counter_offset(boss_,assets,0),blend);
}
unsigned HudController::eyes() const {return blink_>=6?1:blink_>3?7-blink_:blink_;}
void HudController::power_tick(float fill,bool suppress) {
    if(suppress)return; // Resident 80051d94 freezes the meter during dialogue.
    previous_power_age_=power_age_;power_fill_=std::clamp(fill,0.0f,1.0f);
    power_exiting_=fill<=0;
    power_age_=power_exiting_?(power_age_?power_age_-1:0):std::min(47u,power_age_+1);
}
float HudController::power_x(const HudAssets& assets,float alpha)const {
    const auto x=[&](unsigned age){return 512-1.5f*(power_exiting_?assets.exit_curve[(47-age)/2]:assets.enter_curve[age/2]);};
    return std::lerp(x(previous_power_age_),x(power_age_),std::clamp(alpha,0.0f,1.0f));
}
} // namespace spyro
