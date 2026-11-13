// In-game orb challenges in the real workbench (--challenge-check NAME,
// hidden, bounded). The level loads as in play; the challenge's autopilot
// (tools/challenge_pilot.hpp, the same one spyro_challenges runs headless)
// drives the real input path each frame: stick, X, Square, Circle, Triangle.
// Its only other actions are its declared fixtures (the first approach to the
// giver is a teleport before him). Photos at the pilot's moments and at the
// end go beside --capture; the report's "challenge" block has the result.
struct ChallengeCheck {
    std::optional<challenge_pilot::Pilot> pilot;
    unsigned assertions{},photos_taken{},after{};bool finished=false,awarded=false;
    std::filesystem::path folder;std::string stem;std::vector<std::string> photos;
    void require(bool ok,const std::string& why){++assertions;if(!ok)throw FormatError("Challenge check ("+(pilot?pilot->name:std::string("?"))+"): "+why);}
    void photo(Workbench& app,const std::string& name) {
        if(photos.size()>=24)return;
        // The composited game view (3D, the HUD and the dialogue box), saved by
        // the frame loop from the back buffer before it is presented.
        const auto path=folder/(stem+"-"+std::to_string(photos.size())+"-"+name+".bmp");
        app.photo_request=path;photos.push_back(path.filename().string());
        if(app.game) {
            const auto& g=*app.game;
            std::printf("photo %s: tick %llu spyro %.2f %.2f %.2f motion %s orbs %02x hud %zu step %u\n",name.c_str(),static_cast<unsigned long long>(g.clock.ticks()),
                g.player.position.x,g.player.position.y,g.player.position.z,motion_name(g.player.motion),g.guide_awards.orbs,g.overlay_scripts.hud_draws().size(),pilot->index);
            std::fflush(stdout);
        }
    }
    void setup(Workbench& app,const std::string& name,const std::filesystem::path& capture) {
        pilot=challenge_pilot::make(name);
        require(pilot.has_value(),"unknown challenge (see spyro_challenges list)");
        folder=capture.parent_path();stem=capture.stem().string();
        app.automatic_cinematics=false;app.load_level(pilot->slot,false);app.play();
        auto& g=*app.game;g.player.health=3;g.player.lives=std::max(g.player.lives,4u);
        pilot->trace=true;
        app.scripted_input=[this,&app](InputState& in){if(app.game && pilot && !pilot->finished())in=pilot->next(*app.game);};
    }
    void tick(Workbench& app) {
        if(finished || !app.game || !pilot)return;
        auto& g=*app.game;g.player.health=std::max(g.player.health,1u);g.player.lives=std::max(g.player.lives,3u);
        if(!pilot->photo.empty()){photo(app,pilot->photo);pilot->photo.clear();}
        if(pilot->finished()) {
            if(!after++){awarded=pilot->awarded(g);photo(app,"end");}
            if(after>2)finished=true;
        }
    }
};
