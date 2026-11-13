#pragma once
#include <filesystem>
#include <vector>

namespace spyro {
// Only PC paths live here. Progress remains in the portable .s2save format.
struct FrontendPreferences {
    std::filesystem::path source;
    std::filesystem::path july_roll_source;
    std::filesystem::path retail_powerup_source;
    std::vector<std::filesystem::path> saves;
    bool show_fps=false;   // retail-font frame-rate counter, top right (S2MENU 4)
    // PC options (S2MENU 5): seconds after the right stick is let go before the
    // camera returns to Spyro's default angle (0 = never by itself, up to 10);
    // the PS1's own field of view and HUD size instead of the PC framing.
    unsigned camera_recentre_seconds=4;
    bool ps1_fov=false,ps1_hud=false;
    void remember_save(const std::filesystem::path& path);
    void write(const std::filesystem::path& path) const;
    static FrontendPreferences read(const std::filesystem::path& path);
};
} // namespace spyro
