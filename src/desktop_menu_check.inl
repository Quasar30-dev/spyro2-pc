// Opt-in integration replay. It drives the actual ImGui widgets and replaces
// only native file-picker results so it can run in a hidden window.
struct MenuIntegrationCheck {
    unsigned assertions=0,writes=0,reads=0;
    Game* live=nullptr;
    Vec3 frozen;
    std::uint64_t ticks=0;
    unsigned departed_gem=UINT32_MAX;
    std::filesystem::path first,second,world,invalid;
    void require(bool value,const char* why){++assertions;if(!value)throw FormatError(std::string("Menu integration: ")+why);}
    void setup(Workbench& app) {
        const auto root=app.preferences_path.parent_path();std::filesystem::create_directories(root);
        first=root/"First.s2save";second=root/std::filesystem::path(u8"Second - Élodie.s2save");world=root/"Universe.s2save";invalid=root/"Damaged.s2save";
        std::ofstream(invalid)<<"damaged save";
        app.test_save_picker=[&](bool writing)->std::optional<std::filesystem::path> {
            if(!writing){++reads;return invalid;}
            switch(++writes){case 1:return first;case 2:return second;case 3:return {};case 4:return world;default:throw FormatError("Unexpected scripted file picker");}
        };
    }
    void input(unsigned frame,Workbench& app,ImGuiIO& io) {
        using Menu=Workbench::Menu;
        io.AddFocusEvent(true);
        const auto key=[&](ImGuiKey key,std::initializer_list<unsigned> frames){io.AddKeyEvent(key,std::find(frames.begin(),frames.end(),frame)!=frames.end());};
        key(ImGuiKey_Enter,{8,70,148,188});key(ImGuiKey_Escape,{24,86,104,124});
        key(ImGuiKey_J,{30});key(ImGuiKey_R,{30});key(ImGuiKey_W,{30});
        const auto click=[&](unsigned when,const char* button) {
            if(frame!=when && frame!=when+1)return;
            const auto found=app.menu_buttons.find(button);require(found!=app.menu_buttons.end(),("Missing button: "+std::string(button)).c_str());
            io.AddMousePosEvent(found->second.x,found->second.y);io.AddMouseButtonEvent(0,frame==when);
        };
        click(16,"new-game");click(40,"save");click(48,"save-as");click(56,"save-as");click(64,"main-menu");
        click(76,"load-game");click(92,"load-menu");click(96,"browse-save");click(108,"pc-options");click(116,"resume");
        click(128,"game-copy");click(136,"source-back");click(140,"main-menu");click(154,"new-game");click(158,"cancel");
        click(174,"save-as");click(180,"main-menu");click(194,"load-game");
        click(210,"exit-level");click(222,"exit-level");
        if(frame==12)require(app.menu==Menu::Files,"Press Start did not open the retail file menu");
        if(frame==22) {
            require(app.playing && app.menu==Menu::None && app.level->wad_slot==17,"New Game did not start Glimmer");
            app.game->player.gems=37;app.game->upgrades.enabled=true;app.game->upgrades.glide=2;app.game->upgrades.melee=true;
        }
        if(frame==28){require(app.menu==Menu::Pause && app.game->paused,"Escape did not pause");live=app.game.get();frozen=live->player.position;ticks=live->clock.ticks();}
        if(frame==38)require(app.game.get()==live && length(live->player.position-frozen)==0 && live->clock.ticks()==ticks && !live->abilities.melee_swings,
            "Menu navigation moved, respawned or attacked with the paused player");
        if(frame==44) {
            require(writes==1 && GameSave::read(first).gems==37 && app.current_save_path()==first,"First Save did not use the PC location");
            app.photo_request=app.preferences_path.parent_path()/"pause-saved.bmp";
        }
        if(frame==52)require(writes==2 && GameSave::read(second).upgrades.glide==2 && app.current_save_path()==second,"Save As lost upgrades or selected location");
        if(frame==60)require(writes==3 && app.current_save_path()==second && app.game.get()==live,"Cancelling Save As changed the live game or path");
        if(frame==62)app.game->player.gems=99;
        if(frame==74){require(app.menu==Menu::Files && app.recent_saves.size()==2,"Saved files are absent from the original selector");app.photo_request=app.preferences_path.parent_path()/"retail-save-selector.bmp";}
        if(frame==82) {
            require(app.menu==Menu::None && app.playing && app.game->player.gems==37 && app.game->upgrades.glide==2 && app.level->wad_slot==17,"Load Game failed to restore PC progress");
            live=app.game.get();
        }
        if(frame==102) {
            require(reads==1 && !app.error.empty() && app.game.get()==live && app.game->player.gems==37 && app.game->paused,"Damaged save replaced live progress");app.error.clear();
        }
        if(frame==120)require(app.menu==Menu::None && app.game.get()==live && !live->paused,"Resume recreated or left the game paused");
        if(frame==132) {
            require(app.menu==Menu::Source,"Pause menu cannot choose another game source");
            const auto source=app.mounted_path;app.action([&]{app.menu_source(invalid);});
            require(!app.error.empty() && app.game.get()==live && source==app.mounted_path,"Invalid game source discarded the live game");app.error.clear();
        }
        if(frame==162) {
            require(app.menu==Menu::Files && app.game.get()==live && app.game->player.gems==37,"Cancelling New Game discarded progress");
            require(GameSave::read(first).gems==37 && GameSave::read(second).gems==37,"New Game touched an existing save");
        }
        if(frame==168) {
            app.load_universe({2026,7,false});app.game->player.gems=53;app.open_menu(Menu::Pause);
        }
        if(frame==178)require(writes==4 && GameSave::read(world).universe->world==7,"Save As lost the universe address");
        if(frame==200) {
            require(app.menu==Menu::None && app.level->universe && app.level->universe->address.world==7 && app.game->player.gems==53,"Menu could not reload a universe save");
            require(GameSave::read(second).gems==37,"Universe save overwrote the retail campaign");
            const auto preferences=FrontendPreferences::read(app.preferences_path);
            require(preferences.source==app.mounted_path && preferences.saves.front()==world,"Restart preferences did not remember the mounted copy/last save");
            app.open_menu(Menu::Title);
        }
        if(frame==206) {
            app.load_level(17);app.play();app.game->player.gems=137;app.game->purchases=8;
            const auto gem=std::find_if(app.game->retail_objects.begin(),app.game->retail_objects.end(),[](const auto& object){return object.type==1 && !object.runtime_spawned;});
            require(gem!=app.game->retail_objects.end(),"Exit test has no authored gem");
            departed_gem=gem->id;gem->collected=true;gem->state=-1;
            app.open_menu(Menu::Pause);
        }
        if(frame==214) {
            require(app.menu==Menu::None && app.playing && app.level->wad_slot==15 && !app.game->paused,"Exit Level did not resume in Summer Forest");
            require(app.game->player.gems==137 && app.game->purchases==8 && app.campaign.levels.contains(17),"Exit Level lost campaign progress");
            const auto arrival=campaign_arrival(app.wad->read(15),11,*app.level);
            require(arrival.doorway && length(app.game->player.position-arrival.position)<0.3f,"Exit Level ignored the Glimmer doorway arrival");
            require(!app.menu_exit_destination(),"Summer Forest offered an exit to itself");
            app.photo_request=app.preferences_path.parent_path()/"campaign-return.bmp";
        }
        if(frame==216) {
            app.load_level(17);app.play();
            require(app.game->player.gems==137 && app.game->purchases==8 && app.game->retail_objects.at(departed_gem).collected,
                    "Revisiting Glimmer reset inventory, purchase or collected gem");
            require(!app.game->scene_locked && app.game->level.terrain_morphs.front().node==59,"Revisiting Glimmer did not restore the paid bridge");
            app.online_status.connected=true;app.online_status.hosting=false;
            require(!app.menu_exit_destination(),"Online guest could override the host's destination");
            app.online_status.connected=false;app.open_menu(Menu::Pause);
        }
        if(frame==226)require(app.level->wad_slot==15 && app.game->player.gems==137 && app.game->purchases==8,"Repeated return duplicated or lost progress");
        if(frame==238)require(assertions>=63 && writes==4 && reads==1,"Integration replay did not complete");
    }
};
