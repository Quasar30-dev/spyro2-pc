// --ripto-tells DIR: the Crownworks attack warnings (EXPANSION_LEVEL_PLANS
// 6.5.3/6.6) filmed from one fixed camera over the south side of the arena.
// Runs with the --ripto-check fixture; each attack is saved once, 24 ticks
// into its warning, as DIR/<phase>-<attack>.bmp.
#include <cctype>
#include <set>
struct RiptoTellCapture {
    std::filesystem::path folder;std::set<std::string> saved;
    static std::string slug(std::string s) {
        for(auto& c:s)c=std::isalnum(static_cast<unsigned char>(c))?static_cast<char>(std::tolower(static_cast<unsigned char>(c))):'-';
        while(s.find("--")!=std::string::npos)s.replace(s.find("--"),2,"-");
        if(const auto cut=s.find("-dodge");cut!=std::string::npos)s.resize(cut);
        if(const auto cut=s.find("-jump");cut!=std::string::npos)s.resize(cut);
        if(const auto cut=s.find("-leave");cut!=std::string::npos)s.resize(cut);
        while(!s.empty() && s.back()=='-')s.pop_back();
        return s;
    }
    void view(Workbench& app) {
        auto& g=*app.game;if(g.ripto.cinematic!=CrownCinematic::None)return;
        const auto c=g.ripto.center;
        g.camera.scripted_view=CameraView{c+Vec3{0,8.5f,-19},normalized(Vec3{0,-7.5f,19})};g.camera.scripted_exact=true;
    }
    void capture(Workbench& app) {
        const auto& b=app.game->ripto;
        if(!b.engaged || b.cinematic!=CrownCinematic::None || !b.telegraphing() || b.action_ticks!=24)return;
        const auto name=slug(b.phase_name())+"-"+slug(b.action_name());
        if(!saved.insert(name).second)return;
        std::filesystem::create_directories(folder);app.renderer.save_bmp(folder/(name+".bmp"));
    }
};
