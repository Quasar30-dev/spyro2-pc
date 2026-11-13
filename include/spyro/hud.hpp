#pragma once
#include "spyro/assets.hpp"
#include "spyro/retail_math.hpp"
#include <array>
#include <optional>
namespace spyro {
struct HudSprite {unsigned x{},y{},width{},height{};};
struct HudAssets {
    unsigned width=512,height=320;
    Bytes rgba;
    std::array<HudSprite,36> sprites;
    std::array<std::uint8_t,24> enter_curve{},exit_curve{};
    std::array<std::uint8_t,4> eye_x{},eye_y{};
};
HudAssets decode_hud(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> exe);
enum class HudPart : unsigned { Gems,Spirits,Lives };
enum class HudPhase { Hidden,Entering,Counting,Holding,Exiting };
struct HudCounter {HudPhase phase=HudPhase::Hidden;unsigned age{},value{};};
class HudController {
public:
    void reset(unsigned gems,unsigned spirits,unsigned lives);
    void tick(unsigned gems,unsigned spirits,unsigned lives,bool recall=false,bool suppress=false);
    const HudCounter& counter(HudPart part) const {return current_[static_cast<unsigned>(part)];}
    bool visible(HudPart part) const;
    float offset(HudPart part,const HudAssets& assets,float alpha,int delay=0) const;
    unsigned eyes() const;
    // Crush / Gulp (hooks 80067154 update, 80067158 draw; both only while
    // 80066eec < 0): a forced counter of the boss's health at 80067674,
    // and a portrait whose eyes blink over `period` frames.
    void boss(std::optional<unsigned> health,unsigned period){boss_goal_=health;boss_period_=std::max(1u,period);}
    bool boss_visible()const{return boss_goal_.has_value();}
    const HudCounter& boss_counter()const{return boss_;}
    float boss_offset(const HudAssets& assets,float alpha)const;
    unsigned boss_eye()const{return boss_eye_;}
    void power_tick(float fill,bool suppress=false);
    bool power_visible() const {return power_age_ || previous_power_age_;}
    float power_x(const HudAssets& assets,float alpha) const;
    float power_fill() const {return power_fill_;}
private:
    std::array<HudCounter,3> current_{},previous_{};
    unsigned phase_{},recall_ticks_{},blink_=36;
    retail::Random random_;
    HudCounter boss_{},previous_boss_{};std::optional<unsigned> boss_goal_;
    unsigned boss_period_=5,boss_frames_{},boss_eye_{};bool boss_blinking_=false;
    unsigned power_age_{},previous_power_age_{};
    float power_fill_{};
    bool power_exiting_=false;
};
} // namespace spyro
