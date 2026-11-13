// Controller integration (--pad-check): scripted XInput samples go through
// the real workbench loop (Workbench::poll_pad) while a workbench panel, not
// the viewport, has focus. The controller must play the game, pause and back
// out of the game's menu, and never reach ImGui's navigation or a workbench
// setting.
struct PadIntegrationCheck {
    unsigned assertions{};
    Vec3 start;bool flamed=false,jumped=false,charged=false,finished=false;
    int azimuth{};std::filesystem::path folder;   // photos
    Vec3 ground;   // where Spyro stood at the start
    ImGuiID menu_item{};
    std::string settings;
    void require(bool ok,const char* why){++assertions;if(!ok)throw FormatError(std::string("Controller check: ")+why);}
    static std::string workbench_settings(const Workbench& app) {
        // Every workbench toggle a stray activation could flip.
        std::string s;for(const bool b:{app.show_hud,app.preferences.show_fps,app.studio,app.object_studio,app.frontend,app.cheats.superfly,app.cheats.superflame,
            app.cheats.supercharge,app.cheats.green_firebomb,app.cheats.dark_spyro,app.game->upgrades.enabled})s+=b?'1':'0';
        return s;
    }
    PadSample sample(unsigned frame) const {
        PadSample s;
        const auto in=[&](unsigned from,unsigned to){return frame>=from && frame<to;};
        if(in(12,72))s.left_y=32767;                      // run forward
        if(in(80,96))s.buttons|=pad::a;                    // jump
        if(in(140,146))s.buttons|=pad::b;                  // flame: must not pause
        if(in(170,200))s.buttons|=pad::x;                  // charge
        if(in(200,212) || in(232,236) || in(264,268))s.buttons|=pad::start;
        if(in(216,220))s.buttons|=pad::down;               // menu navigation
        if(in(224,228))s.buttons|=pad::b;                  // back out of the pause menu
        if(in(244,262))s.buttons|=pad::a;                  // held through opening the menu
        if(in(248,252))s.buttons|=pad::start;
        // In the air (lifted at 286): Cross glides, Triangle hovers, then
        // Triangle again must headbash.
        if(in(292,296))s.buttons|=pad::a;
        if(in(304,308) || in(324,328))s.buttons|=pad::y;
        // Standing again: hold Triangle to look, steer right, let go; then
        // the right stick turns the camera.
        if(in(400,470))s.buttons|=pad::y;
        if(in(430,450))s.left_x=32767;
        if(in(490,510))s.right_x=32767;
        return s;
    }
    void tick(unsigned frame,Workbench& app) {
        using Menu=Workbench::Menu;
        require(app.game && app.playing && !app.frontend,"needs a level playing in the workbench (--workbench --play)");
        auto& g=*app.game;
        if(app.menu==Menu::None)require(!(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NavEnableGamepad),"ImGui gamepad navigation is on outside the game's menus");
        if(frame==8){settings=workbench_settings(app);ImGui::SetWindowFocus("Session");}
        if(frame==10) {
            const auto* focused=ImGui::GetCurrentContext()->NavWindow;
            require(focused && std::string(focused->Name)=="Session","could not focus a workbench panel");
            start=ground=g.player.position;
        }
        if(frame==72) {
            require(length(Vec3{g.player.position.x-start.x,0,g.player.position.z-start.z})>2,"the left stick did not move Spyro with a panel focused");
            require(ImGui::GetCurrentContext()->NavWindow && std::string(ImGui::GetCurrentContext()->NavWindow->Name)=="Session","the controller moved ImGui focus");
        }
        if(frame>80 && frame<110)jumped|=!g.player.grounded;
        if(frame==110)require(jumped,"Cross did not jump");
        if(frame>140 && frame<168)flamed|=g.flame_action.action()!=0;
        if(frame==168)require(flamed && app.menu==Menu::None,"Circle did not flame, or paused the game");
        if(frame>176 && frame<200)charged|=g.player.motion==Motion::Charge;
        if(frame==200)require(charged,"Square did not charge");
        if(frame==214){require(app.menu==Menu::Pause && g.paused,"Start did not pause");menu_item=app.menu_navigation;}
        if(frame==222)require(app.menu==Menu::Pause && app.menu_navigation!=menu_item,"D-pad did not move through the pause menu");
        if(frame==230)require(app.menu==Menu::None && !g.paused,"Circle did not back out of the pause menu");
        if(frame==240)require(app.menu==Menu::Pause,"Start did not pause again");
        if(frame==242){require(app.menu==Menu::Pause,"Start held across pausing resumed at once");app.action([&]{app.menu_return();});}
        if(frame==256)require(app.menu==Menu::Pause,"Cross held while pausing activated a menu item");
        if(frame==272)require(app.menu==Menu::None && !g.paused,"Start did not resume");
        if(frame==280)require(workbench_settings(app)==settings,"the controller changed a workbench setting");
        if(frame==286) {
            g.purchases|=1u<<6;   // headbash learned (Autumn Plains' Moneybags)
            g.player.position.y+=12;g.player.previous_position=g.player.position;g.player.grounded=false;g.player.velocity={};
        }
        const auto air=[&]{return " (motion "+std::to_string(int(g.player.motion))+", gliding "+std::to_string(g.player.air.gliding)+", hover "+
            std::to_string(g.player.air.hover_ticks)+", headbash "+std::to_string(g.player.air.headbash_ticks)+", grounded "+std::to_string(g.player.grounded)+")";};
        if(frame==302)require(g.player.air.gliding,("Cross did not glide"+air()).c_str());
        if(frame==310)require(g.player.motion==Motion::Hover,("Triangle did not hover out of the glide"+air()).c_str());
        if(frame==330)require(g.player.air.headbash_ticks>0,("Triangle after a hover did not headbash"+air()).c_str());
        if(frame==380){g.player.position=g.player.previous_position=ground;g.player.velocity={};g.player.air={};g.player.grounded=true;}
        if(frame==398)require(g.player.grounded && g.player.motion==Motion::Idle,("Spyro is not standing before the look test"+air()).c_str());
        if(frame==428) {
            require(g.player.look.active && g.camera.state()==7,"Holding Triangle while standing did not enter look mode");
            start=g.player.position;app.photo_request=folder/"look-mode-centre.bmp";
        }
        if(frame==460) {
            require(g.player.look.yaw>0x200 && g.player.head.yaw< -0x100,"The stick did not turn the look view and Spyro's head");
            {const auto d=g.player.position-start;require(std::hypot(d.x,d.z)<.02f,("Spyro walked while looking: moved "+std::to_string(d.x)+","+std::to_string(d.y)+","+std::to_string(d.z)).c_str());}
            app.photo_request=folder/"look-mode-right.bmp";
        }
        if(frame==478)require(!g.player.look.active && g.camera.state()!=7,"Releasing Triangle did not leave look mode");
        if(frame==488)azimuth=g.camera.parameters()[0];
        if(frame==512) {
            int d=(g.camera.parameters()[0]-azimuth)&4095;if(d>2048)d-=4096;
            require(std::abs(d)>400,"The right stick did not turn the camera");
            app.photo_request=folder/"right-stick-turned.bmp";
            finished=true;
        }
    }
};
