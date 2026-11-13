// Workbench front end. The title artwork/cinematic come exclusively from the
// mounted retail copy; PC dialogs and progression use the workbench's backends.
    void write_preferences() {
        if(preferences_path.empty())return;
        try{preferences.write(preferences_path);preferences_warning.clear();}
        catch(const std::exception& e){preferences_warning=std::string("Could not remember PC locations: ")+e.what();}
    }
    void refresh_saves() {
        recent_saves.clear();
        for(const auto& path:preferences.saves) {
            RecentSave save;save.path=path;
            try{save.data=GameSave::read(path);}catch(const std::exception& e){save.error=e.what();}
            recent_saves.push_back(std::move(save));
        }
        selected_save=std::min(selected_save,recent_saves.empty()?0u:static_cast<unsigned>(recent_saves.size()-1));
        save_page=selected_save/3;
    }
    void remember_save(const std::filesystem::path& path) {
        preferences.remember_save(path);selected_save=save_page=0;write_preferences();refresh_saves();
    }
    void ensure_title() {
        if(mounted_profile && mounted_profile->yotd())return;
        if(title || !wad || !wad->contains(95))return;
        const auto archive=wad->read(95);
        auto next=std::make_unique<TitleAssets>(decode_title(archive,wad->read(96),executable));
        title_renderer.upload_level(next->level);title_renderer.upload_objects(next->models);
        HudAssets sprites;sprites.width=next->width;sprites.height=next->height;sprites.rgba=next->rgba;title_renderer.upload_hud(sprites);
        title_empty_spyro.animations.try_emplace(0);
        title_audio.load(mounted_path,executable,archive,95);
        if(video.active())title_audio.begin_capture(capture_offline);
        title=std::move(next);title_ticks=840;
    }
    bool title_visible() const {return menu!=Menu::None && menu!=Menu::Pause && menu!=Menu::Guidebook && menu!=Menu::Sound && menu!=Menu::Upgrades && menu!=Menu::Elements && menu!=Menu::UpgradeHud && menu!=Menu::Destruction && !(menu==Menu::Options && options_back==Menu::Pause) && menu!=Menu::Quit && menu!=Menu::NewGame;}
    void open_menu(Menu page) {
        upgrade_display.hide();
        if(page==Menu::Options && (menu==Menu::Title || menu==Menu::Files))options_back=menu;
        if(game)game->set_paused(true);
        dialogue_input={};focus_play_viewport=false;menu=page;menu_focus=true;
        if((page==Menu::Pause || page==Menu::Guidebook) && game)refresh_guide();
        if(page==Menu::Title || page==Menu::Files)refresh_saves();
        if(title_visible())ensure_title();
    }
    void leave_menu() {
        menu=Menu::None;pending_new_game=false;pending_save.clear();dialogue_input={};
        if(playing && game)game->set_paused(false);
        suspend_frames=2;focus_play_viewport=playing;error.clear();
    }
    void menu_load(const std::filesystem::path& path) {
        GameSave::read(path); // Reject damaged files before asking for a game copy.
        if(!wad){pending_save=path;pending_new_game=false;menu_back=menu;open_menu(Menu::Source);return;}
        load_game(path);frontend=true;leave_menu();
    }
    void menu_new_game() {
        if(!wad){pending_new_game=true;pending_save.clear();menu_back=menu;open_menu(Menu::Source);return;}
        new_game();frontend=true;leave_menu();
    }
    std::optional<CampaignExit> menu_exit_destination() const {
        if(!playing || !level || level->universe || (online_status.connected && !online_status.hosting))return {};
        if(mounted_profile && mounted_profile->yotd()) {
            const auto home=yotd_home(level->wad_slot);
            if(home==level->wad_slot && !level->source_area)return {};
            return CampaignExit{home,0};
        }
        return campaign_exit(executable,level->wad_slot);
    }
    void menu_exit_level() {
        const auto exit=menu_exit_destination();if(!exit)return;
        if(mounted_profile->yotd()) {
            load_level(exit->destination);play();leave_menu();
            status="Returned to "+game_level_name(executable,exit->destination)+".";return;
        }
        if(exit_outro_due()) {   // 80071bd4 also plays the outro on a pause-menu exit (800698f8 = 1)
            leave_menu();exit_after_outro=*exit;++outros_before_exit;start_exit_cinematic();return;
        }
        load_level(exit->destination,true,{},false,exit->source_scene);
        playing=true;studio=object_studio=false;game->set_paused(false);
        remember_game();leave_menu();
        status="Returned to the home world. Level progress retained.";
    }
    void menu_source(const std::filesystem::path& path) {
        const bool live=playing;const auto previous_profile=mounted_profile;remember_game();const auto world=level && level->universe?std::optional(level->universe->address):std::nullopt;
        mount(path);ensure_title();
        if(!pending_save.empty()){const auto save=pending_save;menu_load(save);}
        else if(pending_new_game)menu_new_game();
        else {
            if(live){if(world && previous_profile==mounted_profile)load_universe(*world,false,true);else {load_level(campaign_ready?campaign.slot:mounted_profile->first_level(),false);play();}}
            open_menu(Menu::Title);
        }
        suspend_frames=2;
    }
    void choose_source(HWND window,bool folder) {
        if(folder) {
            ComPtr<IFileOpenDialog> picker;checked(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)),"Open folder picker");
            FILEOPENDIALOGOPTIONS flags{};checked(picker->GetOptions(&flags),"Read folder picker options");
            checked(picker->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR),"Configure folder picker");
            picker->SetTitle(spyro3_edition?L"Choose your extracted Spyro 3: Year of the Dragon folder":L"Choose your extracted Spyro 2 retail folder");
            const auto hr=picker->Show(window);suspend_frames=2;if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;checked(hr,"Choose retail folder");
            ComPtr<IShellItem> item;checked(picker->GetResult(&item),"Read retail folder");PWSTR path{};
            checked(item->GetDisplayName(SIGDN_FILESYSPATH,&path),"Read retail folder path");
            const std::filesystem::path chosen(path);CoTaskMemFree(path);set_source(chosen);menu_source(chosen);
        } else {
            wchar_t path[32768]{};OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=window;
            picker.lpstrTitle=spyro3_edition?L"Choose your Spyro 3: Year of the Dragon disc":L"Choose your Spyro 2 retail disc";picker.lpstrFile=path;picker.nMaxFile=32768;
            picker.lpstrFilter=L"Spyro disc images (*.cue;*.bin;*.iso;*.img)\0*.cue;*.bin;*.iso;*.img\0All files\0*.*\0\0";
            picker.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            const bool picked=GetOpenFileNameW(&picker)!=0;suspend_frames=2;
            if(picked){set_source(path);menu_source(path);}else if(CommDlgExtendedError())throw FormatError("Could not open the retail disc picker");
        }
    }
    void menu_return() {
        if(menu==Menu::UpgradeHud || menu==Menu::Destruction){open_menu(Menu::Options);return;}
        if(menu==Menu::Upgrades){open_menu(Menu::Pause);return;}
        if(menu==Menu::Elements){open_menu(Menu::Upgrades);return;}
        if(menu==Menu::Guidebook){if(guide_skills){guide_skills=false;guide_turn=0;menu_focus=true;}else if(guide_detail){guide_detail=false;guide_turn=0;menu_focus=true;}else open_menu(Menu::Pause);return;}
        if(menu==Menu::Sound){open_menu(Menu::Pause);return;}
        if(menu==Menu::Pause){leave_menu();return;}
        if(menu==Menu::Options){open_menu(options_back);return;}
        if(menu==Menu::Title){menu_back=menu;open_menu(Menu::Options);return;}
        if(menu==Menu::Levels){open_menu(menu_back==Menu::Pause?Menu::Pause:Menu::Title);return;}
        if(menu==Menu::Files){open_menu(Menu::Title);return;}
        pending_save.clear();pending_new_game=false;error.clear();open_menu(menu_back);
    }
    void menu_widgets(HWND window,double seconds) {
        menu_buttons.clear();
        const auto size=ImGui::GetIO().DisplaySize;
        capture_area={0,0,static_cast<unsigned>(std::max(1.0f,size.x)),static_cast<unsigned>(std::max(1.0f,size.y))};
        // A second renderer keeps the live level, player and GPU assets intact
        // while the original title cinematic runs in the main menu.
        if(title_visible() && title) {
            title_ticks+=std::min(seconds,0.1)*60;
            const auto frame=title->sample(static_cast<float>(title_ticks));
            const float width=std::min(size.x,size.y*4/3),height=std::min(size.y,size.x*3/4);
            const ImVec2 origin{(size.x-width)/2,(size.y-height)/2};
            title_renderer.resize(static_cast<unsigned>(width),static_cast<unsigned>(height));
            Scene scene;scene.wad_slot=95;
            title_renderer.draw(title->level,scene,frame.eye,frame.eye+frame.forward,nullptr,1,false,title_empty_spyro,{},0,false,false,
                camera_settings.vertical_fov(),UINT32_MAX,title->models,frame.actors,nullptr,nullptr,SuperchargeAppearance::Retail,
                nullptr,nullptr,{1,1,1},{},static_cast<float>(title_ticks),frame.poses,frame.up);
            ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},size,IM_COL32(0,0,0,255));
            ImGui::GetBackgroundDrawList()->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(title_renderer.image())),origin,{origin.x+width,origin.y+height});
        } else if(level && playing)viewport(0);
        else ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},size,IM_COL32(30,10,48,255));
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(size);
        if(menu_focus)ImGui::SetNextWindowFocus();
        bool focus_first=menu_focus;menu_focus=false;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        ImGui::Begin(spyro3()?"Spyro 3 main menu":"Spyro 2 main menu",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|
            ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground);
        const float sy=size.y/240,sx=std::min(size.x/512,sy*0.625f),left=(size.x-512*sx)/2;
        auto* draw=ImGui::GetWindowDrawList();
        const auto sprite=[&](unsigned id,float x,float y,ImU32 tint=IM_COL32_WHITE) {
            if(!title)return;const auto& s=title->sprites.at(id);
            draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(title_renderer.hud_image())),{left+x*sx,y*sy},{left+(x+s.width)*sx,(y+s.height)*sy},
                {float(s.x)/title->width,float(s.y)/title->height},{float(s.x+s.width)/title->width,float(s.y+s.height)/title->height},tint);
        };
        const auto text=[&](const std::string& label,float x,float y,float scale=1.0f,ImU32 color=IM_COL32(255,239,191,255)) {
            const float font=std::max(12.0f,sy*4.4f*scale);
            const auto width=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,label.c_str()).x;
            const ImVec2 p{left+x*sx-width/2,y*sy};
            draw->AddText(ImGui::GetFont(),font,{p.x+1,p.y+1},IM_COL32(26,8,39,255),label.c_str());
            draw->AddText(ImGui::GetFont(),font,p,color,label.c_str());
        };
        const auto hit=[&](const char* id,float x,float y,float width,float height,bool enabled=true) {
            ImGui::SetCursorScreenPos({left+x*sx,y*sy});ImGui::BeginDisabled(!enabled);
            const bool clicked=ImGui::InvisibleButton(id,{width*sx,height*sy},ImGuiButtonFlags_EnableNav);
            if(enabled && title && title_visible()) {
                if(clicked)title_audio.menu_sound(1);
                else if(ImGui::IsItemFocused() && menu_navigation!=ImGui::GetItemID())title_audio.menu_sound(0);
                if(ImGui::IsItemFocused())menu_navigation=ImGui::GetItemID();
            }
            menu_buttons[id]={left+(x+width/2)*sx,(y+height/2)*sy};
            if(focus_first && enabled){ImGui::SetKeyboardFocusHere(-1);focus_first=false;}
            ImGui::EndDisabled();return clicked;
        };
        const auto sprite_button=[&](const char* id,unsigned image,float x,float y,bool enabled=true) {
            const auto& s=title->sprites.at(image);const bool clicked=hit(id,x,y,float(s.width),float(s.height),enabled);
            const bool focus=enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
            sprite(image,x,y,!enabled?IM_COL32(140,140,140,255):focus?IM_COL32(255,255,38,255):IM_COL32_WHITE);return clicked;
        };
        const auto button=[&](const char* id,const char* label,float y,bool enabled=true) {
            const bool clicked=hit(id,150,y,212,11,enabled);
            const bool focus=enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
            if(focus)draw->AddRectFilled({left+150*sx,y*sy},{left+362*sx,(y+11)*sy},IM_COL32(74,37,99,215),3);
            text(label,256,y+2,1,enabled?(focus?IM_COL32(255,239,74,255):IM_COL32(255,235,198,255)):IM_COL32(145,132,146,255));return clicked;
        };
        const auto panel=[&] {
            draw->AddRectFilled({left+120*sx,12*sy},{left+392*sx,100*sy},IM_COL32(10,4,24,140));
            sprite(1,96,4);sprite(2,256,4);
        };
        const auto browse_save=[&] {if(const auto path=choose_game_save(window,false))menu_load(*path);};
        const Menu page=menu;
        if(page==Menu::Title && title) {
            sprite(0,128,10);
            if(hit("press-start",192,197,128,27) || ImGui::IsKeyPressed(ImGuiKey_Enter,false) || pad.pressed(pad::start))open_menu(Menu::Files);
            const auto phase=static_cast<unsigned>(title_ticks)&31;
            sprite(11,192,203,phase<21?IM_COL32(255,255,255,255):IM_COL32(145,145,145,255));
        } else if(page==Menu::Files && title) {
            panel();
            for(unsigned i=0;i<3;++i) {
                const unsigned index=save_page*3+i;const float x=133.0f+i*83;
                const auto* save=index<recent_saves.size()?&recent_saves[index]:nullptr;
                unsigned thumbnail=6;
                if(save && save->data)thumbnail=save->data->slot<39?3:save->data->slot<57?4:5;
                if(hit(("save-"+std::to_string(i)).c_str(),x,26,80,53,save!=nullptr))selected_save=index;
                sprite(thumbnail,x,31,save && !save->data?IM_COL32(120,120,120,255):IM_COL32_WHITE);
                if(index==selected_save && save)sprite(7,x,31);
                if(save) {
                    const auto label=save->data?std::to_string(save->data->gems)+" gems":"Unavailable";
                    text(label,x+40,18,0.85f);
                    text("FILE "+std::to_string(index+1),x+40,73,0.7f);
                }
            }
            if(sprite_button("new-game",72,128,85)) {
                if(playing || campaign_ready){menu_back=Menu::Files;open_menu(Menu::NewGame);}else action([&]{menu_new_game();});
            }
            if(sprite_button("load-game",73,256,85))action([&]{
                if(selected_save<recent_saves.size())menu_load(recent_saves[selected_save].path);else browse_save();
            });
            if(button("browse-save","Choose a PC save...",110))action(browse_save);
            if(recent_saves.size()>3 && button("more-saves","Next files",123)) {save_page=(save_page+1)%((static_cast<unsigned>(recent_saves.size())+2)/3);selected_save=save_page*3;}
            if(selected_save<recent_saves.size()) {
                const auto& saved=recent_saves[selected_save];text(utf8(saved.path.filename()),256,140,1);
                if(!saved.error.empty())text("File unavailable. Choose another PC save.",256,150,0.85f);
            }
        } else if((page==Menu::Title || page==Menu::Files) && wad && !title) {
            // Early copies do not share the retail title archive layout.
            // Keep source selection and PC save access usable while their
            // own title/cinematic banks are being mapped.
            if(mounted_profile->yotd()) {
                // Spyro 3: its own title and a world-by-world level select.
                draw->AddRectFilled({left+132*sx,16*sy},{left+380*sx,178*sy},IM_COL32(10,4,24,175),6);
                text("SPYRO 3",256,24,2.2f);
                text("YEAR OF THE DRAGON",256,46,1.2f,IM_COL32(255,214,92,255));
                text("PC preview - "+std::string(mounted_profile->name),256,60,0.75f);
                if(button("new-game","New game",79)) {
                    if(campaign_ready){menu_back=page;open_menu(Menu::NewGame);}
                    else action([&]{menu_new_game();});
                }
                if(button("choose-level","Choose level...",93)){menu_back=page;open_menu(Menu::Levels);}
                if(button("browse-save","Load a PC save...",107))action(browse_save);
                if(button("resume","Resume game",121,playing && level && level->wad_slot!=mounted_profile->first_level()))leave_menu();
                if(button("game-copy","Choose game copy...",135)){menu_back=page;open_menu(Menu::Source);}
                if(button("workbench","Workbench",149)){frontend=false;leave_menu();}
                if(button("quit","Quit game",163)){menu_back=page;open_menu(Menu::Quit);}
                if(!powerup_warning.empty())text(powerup_warning,256,200,0.7f,IM_COL32(255,190,150,255));
            } else {
            text("SPYRO 2",256,30,2);
            text(std::string(mounted_profile->name),256,53,0.85f);
            if(button("new-game","New game",79)) {
                if(playing || campaign_ready){menu_back=page;open_menu(Menu::NewGame);}
                else action([&]{menu_new_game();});
            }
            if(button("browse-save","Load a PC save...",98))action(browse_save);
            if(button("resume","Resume game",117,playing))leave_menu();
            if(button("game-copy","Choose game copy...",136)){menu_back=page;open_menu(Menu::Source);}
            if(button("workbench","Workbench",155)){frontend=false;leave_menu();}
            if(button("quit","Quit game",174)){menu_back=page;open_menu(Menu::Quit);}
            }
        } else if(page==Menu::Levels && wad && mounted_profile->yotd()) {
            // Every main scene, grouped under its home world; extra areas
            // are reached from the Inspector's YOTD area box.
            draw->AddRectFilled({left+8*sx,14*sy},{left+504*sx,216*sy},IM_COL32(10,4,24,170),6);
            text("CHOOSE A LEVEL",256,18,1.4f);
            const auto level_button=[&](unsigned slot,float x,float y,float width) {
                const auto name=game_level_name(executable,slot);
                const bool clicked=hit(("level-"+std::to_string(slot)).c_str(),x-width/2,y,width,11);
                const bool here=level && level->wad_slot==slot;
                const bool focus=ImGui::IsItemHovered() || ImGui::IsItemFocused();
                if(focus)draw->AddRectFilled({left+(x-width/2)*sx,y*sy},{left+(x+width/2)*sx,(y+11)*sy},IM_COL32(74,37,99,215),3);
                text(name,x,y+2,0.8f,focus?IM_COL32(255,239,74,255):here?IM_COL32(150,230,255,255):IM_COL32(255,235,198,255));
                if(clicked)action([&,slot]{load_level(slot);play();frontend=true;leave_menu();});
            };
            const std::array<unsigned,4> homes{97,115,133,151};
            for(unsigned w=0;w<homes.size();++w) {
                const float x=72.0f+w*123;
                auto world=game_level_name(executable,homes[w]);   // "Sunrise Spring Home" -> SUNRISE SPRING
                if(world.size()>5 && world.ends_with(" Home"))world.resize(world.size()-5);
                for(auto& c:world)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                text(world,x,34,0.95f,IM_COL32(255,214,92,255));
                for(unsigned slot=homes[w],row=0;slot<homes[w]+18 && slot<169;slot+=2,++row)level_button(slot,x,48.0f+row*15,118);
            }
            level_button(169,256,186,140);
            if(button("levels-back","Back",202))menu_return();
        } else if(page==Menu::Source || !wad) {
            text(spyro3_edition?"CHOOSE YOUR COPY OF SPYRO 3":"CHOOSE YOUR COPY OF SPYRO 2",256,35,1.5f);
            text(pending_save.empty()?"ISO, CUE/BIN or an extracted retail folder":"Choose your game copy to open the selected save",256,52);
            if(button("browse-disc","Choose disc image...",72))action([&]{choose_source(window,false);});
            if(button("browse-folder","Choose extracted folder...",86))action([&]{choose_source(window,true);});
            ImGui::SetCursorScreenPos({left+80*sx,108*sy});ImGui::SetNextItemWidth(352*sx);ImGui::InputText("##menu-source",source_path,sizeof(source_path));
            if(button("mount-source","Use this location",123,source_path[0]!=0))action([&]{menu_source(from_utf8(source_path));});
            if(wad){if(button("source-back","Back",142))menu_return();}
            else {
                if(button("workbench","Workbench",142)){frontend=false;leave_menu();}
                if(button("quit-empty","Quit",156))PostQuitMessage(0);
            }
        } else if(page==Menu::Pause || page==Menu::Guidebook || page==Menu::Sound || page==Menu::Upgrades || page==Menu::Elements || page==Menu::UpgradeHud || page==Menu::Destruction || (page==Menu::Options && options_back==Menu::Pause)) {
            retail_pause_widgets(window,seconds,focus_first);
        } else if(page==Menu::Options) {
            draw->AddRectFilled({left+128*sx,26*sy},{left+384*sx,221*sy},IM_COL32(30,12,45,232),8);
            text(page==Menu::Pause?"PAUSED":"PC OPTIONS",256,35,1.5f);
            if(button("resume","Resume game",55,playing))leave_menu();
            if(button("save","Save game",69,playing))action([&]{save_game(window,false);});
            if(button("save-as","Save game as...",83,playing))action([&]{save_game(window,true);});
            if(button("load-menu","Load game",97))open_menu(Menu::Files);
            if(button("main-menu","Main menu",111)){frontend=true;open_menu(Menu::Title);}
            if(button("game-copy","Game copy...",125)){menu_back=page;open_menu(Menu::Source);}
            if(button("workbench","Workbench",139)){frontend=false;leave_menu();}
            if(button("quit","Quit game",153)){menu_back=page;open_menu(Menu::Quit);}
            if(button("exit-level","Exit level",167,menu_exit_destination().has_value()))action([&]{menu_exit_level();});
            if(button("options-upgrade-hud","Upgrade HUD",181,game!=nullptr))open_menu(Menu::UpgradeHud);
            if(button("options-destruction","Terrain destruction",195,game!=nullptr))open_menu(Menu::Destruction);
            if(button("back","Back",209))menu_return();
        } else if(page==Menu::NewGame || page==Menu::Quit) {
            draw->AddRectFilled({left+115*sx,62*sy},{left+397*sx,176*sy},IM_COL32(30,12,45,238),8);
            const bool fresh=page==Menu::NewGame;
            text(fresh?"START A NEW GAME?":"QUIT SPYRO 2?",256,75,1.5f);
            text("Save first to keep your current progress.",256,94);
            const auto proceed=[&]{if(fresh)menu_new_game();else PostQuitMessage(0);};
            if(button("save-proceed",fresh?"Save, then start":"Save, then quit",111,playing))action([&]{if(save_game(window,false))proceed();});
            if(button("proceed",fresh?"Start without saving":"Quit without saving",127))action(proceed);
            if(button("cancel","Cancel",149))menu_return();
        }
        if(page==Menu::Title || page==Menu::Files) {
            if(hit("pc-options",360,225,145,12)){menu_back=page;open_menu(Menu::Options);}
            text("PC options / Workbench",428,228,0.8f);
        }
        if(!error.empty() || !preferences_warning.empty()) {
            ImGui::SetCursorScreenPos({std::max(15.0f,left+30*sx),164*sy});
            ImGui::BeginChild("Menu message",{std::min(size.x-30,452*sx),31*sy},ImGuiChildFlags_Borders);
            ImGui::TextWrapped("%s",(error.empty()?preferences_warning:error).c_str());ImGui::EndChild();
        } else if(page==Menu::Options && title_visible() && !status.empty())text(status,256,215,0.75f);
        ImGui::End();ImGui::PopStyleVar();
    }
