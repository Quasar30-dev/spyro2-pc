struct UpgradeIntegrationCheck {
    unsigned assertions=0;std::filesystem::path root;Game* live=nullptr;std::uint64_t paused_tick{};
    void require(bool ok,const char* why){++assertions;if(!ok)throw FormatError(std::string("Upgrade integration: ")+why);}
    void setup(Workbench& app,const std::filesystem::path& capture) {
        root=capture.parent_path();app.load_level(17);app.play();app.capture_fullscreen=true;
        app.game->upgrades={};app.game->upgrades.points=100;app.remember_settings();live=app.game.get();
    }
    void input(unsigned frame,Workbench& app,ImGuiIO& io) {
        using Menu=Workbench::Menu;io.AddFocusEvent(true);
        io.AddKeyEvent(ImGuiKey_Escape,frame==10 || frame==110 || frame==120 || frame==670 || frame==900 || frame==1066 || frame==1072 || frame==1078);
        io.AddKeyEvent(ImGuiKey_Tab,(frame>=390 && frame<412) || (frame>=680 && frame<700) || (frame>=800 && frame<825));
        io.AddKeyEvent(ImGuiKey_J,frame==425 || frame==540 || frame==810);
        const bool charge=(frame>=450 && frame<480) || (frame>=520 && frame<560) || (frame>=805 && frame<825);
        io.AddKeyEvent(ImGuiKey_LeftShift,charge);
        io.AddKeyEvent(ImGuiMod_Shift,charge);
        const auto click=[&](unsigned when,const char* id) {
            if(frame!=when && frame!=when+1)return;
            const auto p=app.menu_buttons.find(id);require(p!=app.menu_buttons.end(),("Missing widget "+std::string(id)).c_str());
            io.AddMousePosEvent(p->second.x,p->second.y);io.AddMouseButtonEvent(0,frame==when);
        };
        click(20,"pause-options");click(30,"options-upgrades");click(45,"upgrade-enable");
        click(55,"upgrade-buy-3");click(65,"upgrade-buy-3");click(75,"upgrade-buy-5");
        if(frame==8)require(app.upgrade_display.points_opacity==0,"Disabled HUD is visible");
        if(frame==40) {
            require(app.menu==Menu::Upgrades && live->paused,"Pause options did not open upgrades");
            paused_tick=live->clock.ticks();app.photo_request=root/"upgrade-page-locked.bmp";
        }
        if(frame==85) {
            require(live->upgrades.enabled && live->upgrades.glide==2 && live->upgrades.points==25,"Glide purchase did not spend 25 then 50 points");
            require(!live->upgrades.melee,"Unaffordable melee was purchased");app.photo_request=root/"upgrade-page-purchased.bmp";
        }
        if(frame==90)app.unlock_upgrades(false);
        click(94,"headbash-mode");click(101,"headbash-mode");click(106,"headbash-mode");
        if(frame==98)require(live->upgrades.headbash_mode==HeadbashMode::Normal,"Headbash mode card cannot select Normal");
        if(frame==104)require(live->upgrades.headbash_mode==HeadbashMode::Crater,"Headbash mode card cannot select Crater");
        if(frame==109)require(live->upgrades.headbash_mode==HeadbashMode::Lava,"Headbash mode card cannot select Lava");
        if(frame==100) {
            require(live==app.game.get() && live->clock.ticks()==paused_tick,"Upgrade menu advanced or replaced the game");
            app.photo_request=root/"upgrade-page-complete.bmp";
        }
        if(frame==115)require(app.menu==Menu::Pause,"Escape did not return to pause");
        if(frame==135) {
            require(app.menu==Menu::None && !live->paused && app.upgrade_display.points_opacity==0,"Resume advertised unused upgrades");
        }
        if(frame==385)require(app.upgrade_display.points_opacity==0 && app.upgrade_display.opacity[5]==0,"Idle upgrade strip did not fade away");
        if(frame==407)require(app.upgrade_display.opacity[5]==0,"Tab bypassed once-per-power reminders");
        if(frame==420){app.cheats.supercharge=live->cheats.supercharge=true;}
        if(frame==435) {
            require(live->abilities.attacking() && app.upgrade_display.opacity[5]>0.9f,"J did not activate melee HUD");
            app.photo_request=root/"upgrade-hud-melee.bmp";
        }
        if(frame==467) {
            require(live->player.motion==Motion::Supercharge && app.upgrade_display.opacity[0]>0.9f,"Charge HUD did not follow actual input");
            app.photo_request=root/"upgrade-hud-charge.bmp";
        }
        if(frame==490){live->upgrades.enabled=false;live->abilities.reset(*live);app.remember_settings();}
        if(frame==495) {
            require(std::all_of(app.upgrade_display.opacity.begin(),app.upgrade_display.opacity.end(),[](float a){return a==0;}),"Disabling progression did not hide every icon");
            app.photo_request=root/"upgrade-hud-disabled.bmp";
        }
        if(frame==500){live->upgrades.enabled=true;live->upgrades.points+=10;app.remember_settings();}
        const auto hidden=[&] {return app.upgrade_display.points_opacity==0 && std::all_of(app.upgrade_display.opacity.begin(),app.upgrade_display.opacity.end(),[](float a){return a==0;});};
        if(frame==555)require(hidden() && (app.upgrade_display.seen()&33)==33,"Repeat use or earned points replayed a reminder");
        if(frame==575){app.load_level(39);app.play();live=app.game.get();}
        if(frame==595)require(hidden() && (app.upgrade_display.seen()&33)==33,"Level travel reset once-only history");
        if(frame==600){app.options_back=Menu::Pause;app.open_menu(Menu::Options);}
        click(610,"options-upgrade-hud");click(620,"upgrade-hud-mode");click(630,"upgrade-hud-mode");
        if(frame==642)require(app.menu==Menu::UpgradeHud && live->upgrade_hud_settings.mode==UpgradeHudMode::Manual,"Options cannot select manual upgrade HUD");
        click(650,"upgrade-hud-back");click(660,"options-done");
        if(frame==695)require(app.upgrade_display.opacity[5]==1 && (app.upgrade_display.seen()&127)==127,"Manual mode failed to recall the full owned row");
        if(frame==730)require(hidden(),"Manual HUD did not hide after releasing Tab");
        if(frame==735)app.open_menu(Menu::Options);
        click(740,"options-upgrade-hud");click(750,"upgrade-hud-mode");click(760,"upgrade-hud-mode");
        if(frame==770) {
            require(live->upgrade_hud_settings.mode==UpgradeHudMode::Off,"Options cannot disable upgrade HUD");
            app.photo_request=root/"upgrade-hud-settings-off.bmp";
            app.save_game_to(root/"upgrade-hud-settings.s2save");
        }
        if(frame==780) {
            live->upgrade_hud_settings={};app.load_game(root/"upgrade-hud-settings.s2save");live=app.game.get();app.leave_menu();
            require(live->upgrade_hud_settings.mode==UpgradeHudMode::Off && (app.upgrade_display.seen()&127)==127,"Load lost Off setting or reset session history");
        }
        if(frame==830)require(hidden(),"Off displayed upgrade HUD on input");
        if(frame==835)app.open_menu(Menu::Options);
        click(840,"options-upgrade-hud");click(850,"upgrade-hud-mode");click(860,"upgrade-hud-duration");
        if(frame==870) {
            require(live->upgrade_hud_settings.mode==UpgradeHudMode::Once && live->upgrade_hud_settings.seconds==4,"Options cannot restore once mode or change duration");
            app.photo_request=root/"upgrade-hud-settings-once.bmp";
            app.save_game_to(root/"upgrade-hud-settings.s2save");
            require(GameSave::read(root/"upgrade-hud-settings.s2save").upgrade_hud_settings==live->upgrade_hud_settings,"Save lost edited HUD settings");
        }
        click(880,"upgrade-hud-back");click(890,"options-done");
        if(frame==920){app.cheats.supercharge=app.cheats.superflame=app.cheats.superfly=true;live->cheats=app.cheats;}
        if(frame==940)require(hidden() && (app.upgrade_display.seen()&127)==127,"All-seen HUD reappeared after mode change/equipping powers");
        if(frame==945){app.load_level(17);app.play();live=app.game.get();}
        if(frame==975)require(hidden() && live->upgrade_hud_settings.seconds==4,"Another level lost settings or re-showed all-seen HUD");
        // Elements page: buy tiers, equip, Convexity lock, Dark Spyro and smoke toggles.
        if(frame==980) {
            live->upgrades.elements={};live->upgrades.points=300;app.cheats.dark_spyro=false;app.cheats.dark_smoke=true;live->cheats=app.cheats;
            app.remember_settings();app.open_menu(Menu::Pause);app.open_menu(Menu::Upgrades);
        }
        click(990,"upgrade-elements");
        if(frame==996)require(app.menu==Menu::Elements && live->paused,"Upgrades page did not open the Elements page");
        click(1000,"element-buy-1");click(1006,"element-buy-1");click(1012,"element-buy-1");click(1018,"element-buy-2");
        if(frame==1024) {
            const auto& e=live->upgrades.elements;
            require(e.tier(Element::Water)==3 && e.tier(Element::Ice)==1 && live->upgrades.points==300-30-50-80-30,"Element tiers did not cost 30/50/80");
            require(e.equipped==Element::Ice,"A first purchase must equip the element");
        }
        click(1026,"element-card-1");
        if(frame==1030)require(live->upgrades.elements.equipped==Element::Water,"Card did not equip an owned element");
        click(1032,"element-buy-4");
        if(frame==1036)require(!live->upgrades.elements.owns(Element::Convexity),"Convexity bought before its prerequisites");
        click(1038,"element-dark");
        if(frame==1042)require(app.cheats.dark_spyro && live->cheats.dark_spyro && live->cheats.dark_smoke,"Dark Spyro toggle did not reach the game");
        click(1044,"element-smoke");
        if(frame==1048) {
            require(!live->cheats.dark_smoke,"Smoke toggle did not reach the game");
            require(live->paused,"The Elements page must keep the game paused");
            app.photo_request=root/"upgrade-elements-page.bmp";
        }
        click(1054,"elements-back");
        if(frame==1060)require(app.menu==Menu::Upgrades,"Elements BACK did not return to Upgrades");
        click(1062,"upgrade-elements");
        if(frame==1070)require(app.menu==Menu::Upgrades,"Escape on Elements did not return to Upgrades");
        if(frame==1090) {
            require(app.menu==Menu::None && !live->paused,"Escape chain did not resume play");
            require(live->elements.dark(*live) && !live->elements.smoke.emitted(),"Dark Spyro without smoke");
        }
        if(frame==1092){app.cheats.dark_smoke=true;live->cheats=app.cheats;}
        if(frame==1110) {
            require(live->elements.smoke.emitted()>0 && app.renderer.rendered_dark_spyro(),"Dark Spyro must render and smoke once re-enabled");
            app.photo_request=root/"upgrade-dark-spyro.bmp";
        }
        if(frame==1115){app.cheats.dark_spyro=false;live->cheats=app.cheats;app.remember_settings();}
    }
};
