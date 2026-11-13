#pragma once
#include "spyro/extensions.hpp"
#include "spyro/hud.hpp"
#include <string>

namespace spyro {
enum class UpgradeIcon : unsigned { Charge,Flame,Flight,Glide,Carry,Melee,Headbash,Roll,Count };
inline constexpr unsigned upgrade_icon_count=static_cast<unsigned>(UpgradeIcon::Count);
enum class UpgradeStatus { Locked,Dormant,Ready,Active,Spent };
struct UpgradeHudItem {
    Upgrade upgrade{};
    unsigned tier{},maximum=2,remaining{};
    UpgradeStatus status=UpgradeStatus::Locked;
    bool contextual=false,equipped=false,asset_ready=true;
    std::string value;
};
struct UpgradeHudSnapshot {
    bool enabled=false,suppressed=false;
    unsigned points{};
    std::array<UpgradeHudItem,upgrade_icon_count> items;
};
UpgradeHudSnapshot upgrade_hud_snapshot(const Game& game);
const char* upgrade_name(unsigned icon);
const char* upgrade_description(unsigned icon,unsigned tier);
const char* upgrade_hud_mode_name(UpgradeHudMode mode);
const char* upgrade_hud_mode_description(UpgradeHudMode mode);
// Owned by the workbench session, not a Game/level. Saved policy is separate
// from session reminder history; hiding, respawning and loading never re-arm it.
class UpgradeHudDisplay {
public:
    void update(const UpgradeHudSnapshot& snapshot,float seconds,bool recall,const UpgradeHudSettings& settings={});
    void hide();
    unsigned seen()const{return seen_;}
    std::array<float,upgrade_icon_count> opacity{};
    float points_opacity{};
private:
    unsigned seen_{};
    UpgradeHudSettings settings_;
    std::array<bool,upgrade_icon_count> equipped_{};
    std::array<float,upgrade_icon_count> linger_{};
};
HudAssets upgrade_sprite_assets();
} // namespace spyro
