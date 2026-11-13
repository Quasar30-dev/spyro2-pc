struct InterfaceIntegrationCheck {
    unsigned assertions=0;std::filesystem::path root;Game* live=nullptr;Vec3 position;
    void require(bool ok,const char* why){++assertions;if(!ok)throw FormatError(std::string("Interface integration: ")+why);}
    void setup(Workbench& app,const std::filesystem::path& capture) {
        root=capture.parent_path();app.load_level(17);app.play_beside_object(9);app.capture_fullscreen=true;app.game->player.gems=150;
        live=app.game.get();position=live->player.position;
    }
    void input(unsigned frame,Workbench& app,ImGuiIO& io) {
        using Menu=Workbench::Menu;io.AddFocusEvent(true);
        const auto key=[&](ImGuiKey k,std::initializer_list<unsigned> frames){io.AddKeyEvent(k,std::find(frames.begin(),frames.end(),frame)!=frames.end());};
        key(ImGuiKey_E,{18});key(ImGuiKey_Enter,{90,170});key(ImGuiKey_Escape,{205,220,301,365,380});
        key(ImGuiKey_RightArrow,{320,342});key(ImGuiKey_LeftArrow,{354});
        const auto click=[&](unsigned when,const char* id){if(frame!=when && frame!=when+1)return;const auto p=app.menu_buttons.find(id);require(p!=app.menu_buttons.end(),("Missing widget "+std::string(id)).c_str());io.AddMousePosEvent(p->second.x,p->second.y);io.AddMouseButtonEvent(0,frame==when);};
        click(250,"guidebook");click(270,"guide-level-1");
        if(frame==80)require(app.game->dialogue.active() && app.dialogue_visible,"E did not open the retail dialogue");
        if(frame==94){require(app.dialogue_revealed && app.game->dialogue.active(),"Continue did not reveal the passage before choosing");app.photo_request=root/"moneybags-dialogue.bmp";}
        if(frame==178)app.photo_request=root/"moneybags-choice.bmp";
        if(frame==212)require(!app.game->dialogue.active() && app.menu==Menu::None,"Escape failed to dismiss dialogue without pausing");
        if(frame==228){require(app.menu==Menu::Pause && app.game->paused,"Escape failed to open retail pause");position=live->player.position;app.photo_request=root/"retail-pause.bmp";}
        if(frame==258){require(app.menu==Menu::Guidebook && !app.guide_detail,"Guidebook entry did not open the contents");app.photo_request=root/"guidebook-summer.bmp";}
        if(frame==281){require(app.guide_detail && app.guide_selected==1,"Level selection did not open Glimmer");app.photo_request=root/"guidebook-glimmer.bmp";}
        if(frame==307)require(!app.guide_detail && app.menu==Menu::Guidebook,"Escape did not return from level page to contents");
        if(frame==330){require(app.guide_world==1,"Right arrow did not turn to Autumn Plains");app.photo_request=root/"guidebook-autumn.bmp";}
        if(frame==349){require(app.guide_world==2,"Right arrow did not turn to Winter Tundra");app.photo_request=root/"guidebook-winter.bmp";}
        if(frame==359)require(app.guide_world==1,"Left arrow did not turn back");
        if(frame==373)require(app.menu==Menu::Pause,"Contents Escape did not return to pause");
        if(frame==388)require(app.menu==Menu::None && !app.game->paused && app.game.get()==live,"Resume recreated the game or left it paused");
        if(frame==400){app.open_menu(Menu::Guidebook);app.guide_detail=true;}
        if(frame>=401 && frame<1271 && (frame-401)%30==0){app.guide_selected=(frame-401)/30;app.guide_detail=true;}
        if(frame>=429 && frame<1299 && (frame-429)%30==0) {
            require(app.menu==Menu::Guidebook && app.game->paused && app.game.get()==live,"Rendering a retail level page lost the paused game");
            if(app.guide_selected==9 || app.guide_selected==22 || app.guide_selected==28)app.photo_request=root/("guidebook-level-"+std::to_string(app.guide_selected)+".bmp");
        }
        if(frame==1295){require(length(app.game->player.position-position)<0.05f,"Guidebook controls moved Spyro");app.leave_menu();}
    }
};
