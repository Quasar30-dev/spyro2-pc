    void sync_dialogue() {
        if(!game || !game->dialogue.active())return;
        const auto& d=game->dialogue;
        if(dialogue_revision==d.revision())return;
        dialogue_revision=d.revision();dialogue_page=dialogue_selection=0;dialogue_revealed=dialogue_answers_page=false;
        dialogue_scroll=0;dialogue_delay=dialogue_visible?0:60;if(!dialogue_visible)dialogue_entry_tick=game->clock.ticks();
        dialogue_name=d.name();dialogue_side=d.camera_side();dialogue_purchase=d.line()->purchase;
        dialogue_choices=d.line()->yes>=0 && d.line()->no>=0 && (d.line()->supported || d.retail_driven());
        const auto formatted=retail_pc_controls(d.display_text(true),pad.connected());const auto question=formatted.rfind('&');
        dialogue_layout=layout_retail_text(interface_assets,dialogue_choices && question!=std::string::npos?formatted.substr(0,question):formatted,0,434);
        dialogue_question=layout_retail_text(interface_assets,question!=std::string::npos?formatted.substr(question+1):formatted,0,425);
        for(unsigned i=0;i<2;++i)dialogue_answers[i]=retail_pc_controls(d.line()->formatted_answers[i].empty()?d.line()->answers[i]:d.line()->formatted_answers[i],pad.connected());
    }
    unsigned dialogue_letters()const {return dialogue_revealed?UINT32_MAX:game->dialogue.elapsed()>dialogue_delay?(game->dialogue.elapsed()-dialogue_delay)*2:0;}
    bool dialogue_page_ready()const {
        if(dialogue_revealed)return true;
        return dialogue_letters()>=dialogue_layout.glyphs.size();
    }
    void filter_dialogue_input(DialogueInput& input) {
        if(!game || !game->dialogue.active())return;
        sync_dialogue();
        const auto pressed=[](ImGuiKey k){return ImGui::IsKeyPressed(k,false);};
        // Controller: D-pad or stick picks an answer, Cross (A) continues,
        // Triangle (Y, the talk button) also continues, Circle (B) leaves.
        if(pressed(ImGuiKey_UpArrow) || pad.pressed(pad::up|pad::stick_up))dialogue_selection=0;
        if(pressed(ImGuiKey_DownArrow) || pad.pressed(pad::down|pad::stick_down))dialogue_selection=1;
        input.next|=pad.pressed(pad::a);input.cancel|=pad.pressed(pad::b);
        if(!input.next && !input.interact && input.choice<0)return;
        if(!dialogue_page_ready()){dialogue_revealed=true;input.next=input.interact=false;input.choice=-1;return;}
        if(dialogue_choices && !dialogue_answers_page) {
            dialogue_answers_page=true;input.next=input.interact=false;input.choice=-1;return;
        }
        if(dialogue_choices && input.choice<0 && (input.next || input.interact)){input.choice=static_cast<int>(dialogue_selection);input.next=input.interact=false;}
    }
    void draw_dialogue(ImVec2 origin,ImVec2 size,double seconds) {
        if(!renderer.interface_image() || !game)return;
        sync_dialogue();const bool active=game->dialogue.active();
        if(active){dialogue_visible=true;dialogue_close_age=0;}
        else if(dialogue_visible){dialogue_close_age+=std::min(seconds,0.1)*60;if(dialogue_close_age>=24)dialogue_visible=false;}
        RetailCanvas ui(interface_assets,ImGui::GetWindowDrawList(),reinterpret_cast<ImTextureID>(renderer.interface_image()),origin,size);
        if(!dialogue_visible) {
            if(game->dialogue.nearby())ui.centered(std::string(pad.connected()?"Y  Talk to ":"E  Talk to ")+game->dialogue.name(),256,219,1,false,0.85f);
            return;
        }
        float slide=0;
        const auto open_age=game->clock.ticks()-dialogue_entry_tick;
        const auto curve=[&](const auto& table,double tick){const auto last=static_cast<unsigned>(table.size()-1),i=std::min(last,static_cast<unsigned>(tick/2));return std::lerp(float(table[i]),float(table[std::min(i+1,last)]),std::clamp(float(tick/2-i),0.0f,1.0f));};
        if(!active)slide=curve(interface_assets.dialogue_exit,dialogue_close_age);
        else if(open_age<24)return;
        else if(open_age<60)slide=curve(interface_assets.dialogue_enter,open_age-24+game->clock.alpha());
        const auto& name=dialogue_answers_page?std::string("Spyro"):dialogue_name;
        const float name_width=layout_retail_text(interface_assets,name).width;
        const float name_x=slide+((dialogue_side==1)!=dialogue_answers_page?460-name_width:40);
        ui.draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
        // 8004d3fc: three semi-transparent rectangles and four 12 x 8 corners.
        ui.rect(slide+24,154,464,50,IM_COL32(8,8,8,128));ui.rect(slide+36,146,440,8,IM_COL32(8,8,8,128));ui.rect(slide+36,204,440,8,IM_COL32(8,8,8,128));
        ui.resident(36,slide+24,146);ui.resident(37,slide+476,146);ui.resident(38,slide+24,204);ui.resident(39,slide+476,204);
        ui.rect(name_x,134,name_width+14,12,IM_COL32(8,8,8,128));ui.rect(name_x+1,133,name_width+12,1,IM_COL32(8,8,8,128));ui.rect(name_x+4,132,name_width+6,1,IM_COL32(8,8,8,128));
        if(!active || open_age<60){ui.draw->PopClipRect();return;}
        ui.rect(name_x+24,145,std::max(0.0f,name_width-20),0.5f,IM_COL32(180,154,17,255));ui.text(name,name_x+8,134);
        const bool ready=active && dialogue_page_ready();
        const bool choices=dialogue_choices && ready && dialogue_answers_page;
        // Keep the spoken passage on screen, then place the two retail answers
        // below it. Long passages paginate before the choice page.
        const unsigned lines=choices?std::min(3u,dialogue_question.lines):5;
        ui.draw->PushClipRect(ui.point(40+slide,150),ui.point(474+slide,150+lines*12-2),true);
        const auto count=active?dialogue_letters():UINT32_MAX;
        const auto& text=choices?dialogue_question:dialogue_layout;
        float scroll_target=0;
        for(unsigned i=0;i<std::min(count,static_cast<unsigned>(text.glyphs.size()));++i)scroll_target=std::max(scroll_target,text.glyphs[i].y-48);
        dialogue_scroll=dialogue_revealed?scroll_target:std::min(scroll_target,dialogue_scroll+float(seconds)*30);
        for(unsigned i=0;i<text.glyphs.size();++i) {
            const auto& g=text.glyphs[i];if(i>=count)break;
            const float y=150+g.y-(choices?0:dialogue_scroll);
            if(y<142 || y>210)continue;
            ui.image(interface_assets.font[g.character].colors[g.color],40+slide+g.x,y);
        }
        ui.draw->PopClipRect();
        const auto hit=[&](const char* id,float x,float y,float width,float height) {
            ImGui::SetCursorScreenPos(ui.point(x+slide,y));return ImGui::InvisibleButton(id,{width*ui.sx,height*ui.sy});
        };
        if(choices) {
            const float y=150+lines*12;
            for(unsigned i=0;i<2;++i) {
                const auto color=dialogue_selection==i && (game->dialogue.elapsed()&31)<22?1u:0u;
                ui.text(dialogue_answers[i],72+slide,y+i*12,color);
                if(dialogue_selection==i)ui.text(interface_assets.continue_arrow,60+slide,y+i*12,1);
                if(hit(i?"dialogue-answer-1":"dialogue-answer-0",60,y+i*12,360,11))dialogue_input.choice=static_cast<int>(i);
                if(ImGui::IsItemHovered())dialogue_selection=i;
            }
        } else if(active && (ready?((game->dialogue.elapsed()&31)<16):true)) {
            if(ready)ui.text(interface_assets.continue_arrow,474+slide,200,1);
            if(hit("dialogue-continue",24,146,464,66))dialogue_input.next=true;
        }
        if(dialogue_purchase>=0 && choices) {ui.right("Your Total:",330+slide,196,1);ui.resident(11,336+slide,190);ui.text(std::to_string(game->player.gems),374+slide,196,1);}
        ui.draw->PopClipRect();
    }
    void refresh_guide() {
        if(mounted_profile && mounted_profile->yotd()){guide_stats={};return;}
        remember_game();guide_stats={};unsigned token=0;
        for(unsigned i=0;i<29;++i) {
            auto& s=guide_stats[i];const auto& l=interface_assets.levels[i];
            if(l.scene<50 && l.scene%10>=1 && l.scene%10<=4)s.talisman=(campaign.talismans&(1u<<token++))!=0;
            const auto slot=15+i*2;const auto found=campaign.levels.find(slot);s.visited=found!=campaign.levels.end();
            if(!s.visited)continue;
            const auto& awards=found->second.awards;s.orb_mask=awards.orbs;s.orbs=std::popcount(awards.orbs);s.talisman|=awards.talisman;s.boss=awards.boss;s.flight_tasks=awards.flight_tasks;s.flight_ticks=awards.flight_ticks;
            if(level && !level->universe && level->wad_slot==slot){s.gems=game->level_gems();s.spirits=game->spirits();continue;}
            if(slot==29 || slot==43 || slot==53 || slot==63){s.gems=std::popcount(s.flight_tasks)*80;continue;}
            if(!guide_objects.contains(slot))guide_objects[slot]=decode_object_instances(wad->read(slot),slot);
            const auto& objects=guide_objects.at(slot);const auto& saved=found->second;
            for(const auto& r:saved.removed)if(r.id<objects.size()) {
                const auto& o=objects[r.id];if((o.type==1 || is_breakable(o.type) || is_heavy_container(o.type)) && o.held_gem!=255)s.gems+=o.held_gem;
                s.spirits+=o.record[83]&127;
            }
            unsigned pending=0;for(const auto& g:saved.pending_gems)pending+=g.value;s.gems-=std::min(pending,s.gems);
            for(const auto& e:saved.entities)if(e.kind==EntityKind::Gem && e.collected)s.gems+=e.value;
        }
    }
    void retail_pause_widgets(HWND window,double seconds,bool& focus_first) {
        interface_ticks+=std::min(seconds,0.1)*60;guide_turn+=std::min(seconds,0.1)*60;
        const auto size=ImGui::GetIO().DisplaySize;
        RetailCanvas ui(interface_assets,ImGui::GetWindowDrawList(),reinterpret_cast<ImTextureID>(renderer.interface_image()),{0,0},size);
        const auto hit=[&](const char* id,float x,float y,float w,float h,bool enabled=true) {
            ImGui::SetCursorScreenPos(ui.point(x,y));ImGui::BeginDisabled(!enabled);
            const bool clicked=ImGui::InvisibleButton(id,{w*ui.sx,h*ui.sy},ImGuiButtonFlags_EnableNav);
            menu_buttons[id]=ui.point(x+w/2,y+h/2);
            if(focus_first && enabled){ImGui::SetKeyboardFocusHere(-1);focus_first=false;}
            if(clicked)audio.menu_sound(1);
            else if(ImGui::IsItemFocused() && menu_navigation!=ImGui::GetItemID()){menu_navigation=ImGui::GetItemID();audio.menu_sound(0);}
            ImGui::EndDisabled();return clicked;
        };
        const auto entry=[&](const char* id,const char* label,float y,bool enabled=true) {
            const bool click=hit(id,35,y-1,195,12,enabled);
            const bool focused=enabled && (ImGui::IsItemFocused() || ImGui::IsItemHovered());
            ui.centered(label,128,y,enabled?(focused?3u:2u):13u);return click;
        };
        const auto pressed=[](ImGuiKey key){return ImGui::IsKeyPressed(key,false);};
        const auto page=menu;
        ui.draw->PushClipRect(ui.point(0,0),ui.point(512,240),true);
        if(page==Menu::Upgrades)upgrade_page(ui,focus_first);
        else if(page==Menu::Elements)elements_page(ui,focus_first);
        else if(page==Menu::UpgradeHud)upgrade_hud_page(ui,focus_first);
        else if(page==Menu::Destruction)destruction_page(ui,focus_first);
        else if(page==Menu::Pause || page==Menu::Sound || page==Menu::Options) {
            ui.rect(0,12,256,108,IM_COL32(205,193,188,255));ui.stamps(0,false);
            if(mounted_profile->yotd()) {
                // Spyro 3's pause book is its Atlas: the same page without the
                // Spyro 2 Guidebook title (the one wide sprite along its top).
                float cx=384,cy=26;
                for(const auto& stamp:interface_assets.layouts.at(1)) {
                    const auto& sprite=interface_assets.pause.at(stamp.sprite);
                    if(stamp.y<40 && sprite.width>=60) {
                        cx=256+stamp.x+sprite.width/2.0f;cy=stamp.y+sprite.height/2.0f;
                        ui.rect(256+stamp.x,stamp.y,sprite.width,sprite.height,IM_COL32(226,219,204,255));   // the page's parchment
                        continue;
                    }
                    ui.image(sprite,256+stamp.x,stamp.y);
                }
                ui.centered("Atlas",cx,cy-7,1,false,1.6f);
            } else ui.stamps(1,false,256);
            unsigned index=level && level->wad_slot>=15 && level->wad_slot<=71?(level->wad_slot-15)/2:0;
            const auto& l=interface_assets.levels[index];const auto& s=guide_stats[index];
            ui.resident(31,362,48);const unsigned blink=static_cast<unsigned>(interface_ticks)%100<6?0:1;
            ui.resident(32+blink,362+hud.eye_x[blink],48+hud.eye_y[blink]);
            const unsigned gem=11+(static_cast<unsigned>(interface_ticks)/8)%6;
            ui.resident(gem,310,74);ui.image(interface_assets.pause[21],game->player.orbs>9?414:428,71);
            ui.text(std::to_string(game->player.lives),414,61,2);ui.text(std::to_string(game->player.gems),348,80,2);ui.right(std::to_string(game->player.orbs),484,80,2);
            ui.centered(mounted_profile->yotd()?game_level_name(executable,level->wad_slot):(level->universe?"Universe":l.name),398,107,2,true);
            if(l.scene<50 && l.scene%10>=1 && l.scene%10<=4)ui.image(interface_assets.pause[s.talisman?23:24],376,120);
            for(unsigned i=0;i<l.orbs;++i)ui.image(interface_assets.pause[(s.orb_mask&(1u<<l.orb_bits[i]))?21:22],float(400-l.orbs*22+i*44),192);
            ui.resident(gem,310,151);
            ui.right(std::to_string(game->level_gems()),416,157,2);
            if(!mounted_profile->yotd()) {
                ui.resident(19+(static_cast<unsigned>(interface_ticks)/8)%3,310,170);
                ui.text("/",421,157,2);ui.text(std::to_string(l.gems),436,157,2);
                ui.right(std::to_string(game->spirits()),416,175,2);ui.text("/",421,175,2);ui.text(std::to_string(l.spirits),436,175,2);
            }
            if(page==Menu::Options) {
                if(entry("camera-mode",game->dialogue.active_camera?"Camera: Active":"Camera: Passive",25)) {game->dialogue.active_camera=!game->dialogue.active_camera;game->camera.passive=!game->dialogue.active_camera;}
                if(entry("gameplay-hud",show_hud?"HUD: On":"HUD: Off",45))show_hud=!show_hud;
                if(entry("options-sound","Sound",65))open_menu(Menu::Sound);
                if(entry("options-workbench","Workbench",85)){frontend=false;leave_menu();}
                if(entry("options-done","Done",99))open_menu(Menu::Pause);
                ui.rect(28,127,208,99,IM_COL32(8,8,8,150));ui.centered("PC options",128,130,1,false,0.85f);
                if(entry("options-upgrade-hud","Upgrade HUD",143))open_menu(Menu::UpgradeHud);
                if(entry("options-destruction","Terrain damage",156))open_menu(Menu::Destruction);
                if(entry("options-upgrades","Upgrades",169))open_menu(Menu::Upgrades);
                // The right-stick camera's return to Spyro's default angle: 0 (never by
                // itself), 1 .. 10 seconds after the stick is let go; saved with the preferences.
                const auto recentre=preferences.camera_recentre_seconds?"Camera recentre: "+std::to_string(preferences.camera_recentre_seconds)+" s":std::string("Camera recentre: Never");
                if(entry("options-recentre",recentre.c_str(),182)){preferences.camera_recentre_seconds=(preferences.camera_recentre_seconds+1)%11;write_preferences();}
                if(entry("options-ps1-fov",preferences.ps1_fov?"PS1 FOV: On":"PS1 FOV: Off",195)){preferences.ps1_fov=!preferences.ps1_fov;write_preferences();}
                if(entry("options-ps1-hud",preferences.ps1_hud?"PS1 HUD size: On":"PS1 HUD size: Off",208)){preferences.ps1_hud=!preferences.ps1_hud;write_preferences();}
            } else if(page==Menu::Sound) {
                if(entry("voice-volume",("Voice: "+std::to_string(int(audio.voice_volume*100))+"%").c_str(),25))audio.voice_volume=audio.voice_volume>=0.99f?0:audio.voice_volume+0.1f;
                if(entry("effects-volume",("Sound: "+std::to_string(int(audio.effects_volume*100))+"%").c_str(),45))audio.effects_volume=audio.effects_volume>=0.99f?0:audio.effects_volume+0.1f;
                if(entry("music-volume",("Music: "+std::to_string(int(audio.music_volume*100))+"%").c_str(),65))audio.music_volume=audio.music_volume>=0.99f?0:audio.music_volume+0.1f;
                if(entry("sound-mute",audio.muted?"Sound: Off":"Sound: On",85))audio.muted=!audio.muted;
                if(entry("sound-back","Done",99))open_menu(Menu::Pause);
            } else {
                if(entry("resume","Continue",25))leave_menu();
                // Spyro 3 has no Spyro 2 guidebook: its pause menu offers the level select instead.
                if(mounted_profile->yotd()){if(entry("choose-level","Choose Level",39)){menu_back=Menu::Pause;open_menu(Menu::Levels);}}
                else if(entry("guidebook","Guidebook",39,interface_assets.contents[0].width!=0)){guide_world=index<9?0:index<21?1:2;guide_selected=index;guide_detail=guide_skills=false;open_menu(Menu::Guidebook);guide_turn=0;}
                if(entry("save","Save Game",53,!(mounted_profile->yotd() && level->source_area)))action([&]{save_game(window,false);refresh_guide();});
                if(entry("pause-options","Options",67)){options_back=Menu::Pause;open_menu(Menu::Options);}
                if(entry("sound","Sound",81))open_menu(Menu::Sound);
                if(entry("exit-level","Exit Level",95,menu_exit_destination().has_value()))action([&]{menu_exit_level();});
                // PC operations retain their existing file pickers, validation,
                // overwrite handling, and campaign/universe save separation.
                ui.rect(28,127,208,99,IM_COL32(8,8,8,150));ui.centered("PC options",128,130,1,false,0.85f);
                if(entry("save-as","Save game as...",143))action([&]{save_game(window,true);refresh_guide();});
                if(entry("load-menu","Load game",156))open_menu(Menu::Files);
                if(entry("main-menu","Main menu",169)){frontend=true;open_menu(Menu::Title);}
                if(entry("game-copy","Game copy...",182)){menu_back=Menu::Pause;open_menu(Menu::Source);}
                if(entry("workbench","Workbench",195)){frontend=false;leave_menu();}
                if(entry("quit","Quit game",208)){menu_back=Menu::Pause;open_menu(Menu::Quit);}
            }
        } else if(page==Menu::Guidebook) {
            const int index_begin=ui.draw->IdxBuffer.Size;
            const bool changed=guide_last_detail!=guide_detail || guide_last_skills!=guide_skills || guide_last_world!=guide_world || guide_last_selected!=guide_selected;
            if(changed){guide_turn=0;guide_last_detail=guide_detail;guide_last_skills=guide_skills;guide_last_world=guide_world;guide_last_selected=guide_selected;}
            ui.stamps(2,true);ui.stamps(3,true,256);ui.stamps(4,true);ui.stamps(5,true,256);
            const auto change=[&](unsigned world){guide_world=world;guide_detail=false;guide_turn=0;menu_focus=true;};
            if(guide_skills) {
                const auto& p=interface_assets.pages[30];ui.image(p[10],80,20);ui.image(p[11],308,20);ui.image(p[6],92,202);
                std::vector<unsigned> earned;for(unsigned i=0;i<17;++i)if(campaign.skill_points&(1u<<i))earned.push_back(i);
                for(unsigned row=0;row<10 && guide_skill_page*10+row<earned.size();++row) {
                    const auto i=earned[guide_skill_page*10+row];const unsigned color=(row&1)?12:8;
                    ui.text(interface_assets.levels[interface_assets.skill_levels[i]].name,40,float(52+row*14),color,true);
                    ui.text(interface_assets.skills[i],276,float(52+row*14),color,true);
                }
                if(earned.empty())ui.centered("No skill points yet.",384,75,8,true);
                if(guide_skill_page>0)ui.image(p[16],44,202);
                if(earned.size()>10 && guide_skill_page==0)ui.image(p[17],216,202);
                if(guide_skill_page>0 && (hit("skills-previous",32,196,45,22) || pressed(ImGuiKey_LeftArrow))){guide_skill_page=0;guide_turn=0;}
                if(earned.size()>10 && guide_skill_page==0 && (hit("skills-next",209,196,34,22) || pressed(ImGuiKey_RightArrow))){guide_skill_page=1;guide_turn=0;}
                if(hit("skills-return",92,200,128,14)){guide_skills=false;guide_turn=0;menu_focus=true;}
                if(earned.size()>15 && guide_skill_page==1)ui.image(p[12],308,154);
            } else if(!guide_detail) {
                const unsigned start=guide_world==0?0:guide_world==1?9:21,count=guide_world==0?9:guide_world==1?12:7;
                const auto& book=interface_assets.contents;
                ui.image(book[9],80,26);ui.image(book[11+guide_world],166,26);ui.image(book[16],112,41);
                ui.image(book[guide_world],60,57);ui.image(book[5],324,26);ui.image(book[16],340,41);
                ui.image(book[3],92,162);ui.image(book[4],92,182);
                for(unsigned row=0;row<count;++row) {
                    const unsigned i=interface_assets.order[start+row];const auto& l=interface_assets.levels[i];const auto& s=guide_stats[i];
                    const float y=float((guide_world==1?46:guide_world==0?61:72)+row*(guide_world==1?14:15));
                    if(hit(("guide-level-"+std::to_string(i)).c_str(),270,y-1,211,12,s.visited)){guide_selected=i;guide_detail=true;guide_turn=0;menu_focus=true;}
                    const bool selected=ImGui::IsItemFocused() || ImGui::IsItemHovered();
                    const unsigned color=selected && s.visited?9u:(row&1)?12u:8u;
                    ui.text(s.visited?l.name:"----------",271,y,color,s.visited);
                    ui.right(std::to_string(guide_completion(l,s.gems,s.orbs,s.talisman || s.boss)),466,y,color);ui.text("%",468,y,color);
                }
                if(guide_world>0){ui.image(book[6+guide_world-1],60,202);ui.image(book[14],44,202);}
                if(guide_world<2){ui.image(book[6+guide_world+1],60,guide_world==1?218:202);ui.image(book[15],216,guide_world==1?218:202);}
                if(guide_world>0 && (hit("guide-previous-world",35,200,195,14) || pressed(ImGuiKey_LeftArrow) || pad.pressed(pad::left)))change(guide_world-1);
                if(guide_world<2 && (hit("guide-next-world",35,guide_world==1?216:200,195,14) || pressed(ImGuiKey_RightArrow) || pad.pressed(pad::right)))change(guide_world+1);
                if(hit("guide-return",92,182,100,14))open_menu(Menu::Pause);
                if(guide_world==2 && (campaign.skill_points || guide_stats[28].boss)) {
                    ui.image(book[10],308,202);ui.image(book[15],440,202);
                    if(hit("guide-skills",300,199,184,16)){guide_skills=true;guide_skill_page=0;guide_turn=0;menu_focus=true;}
                }
            } else {
                const auto& l=interface_assets.levels[guide_selected];const auto& s=guide_stats[guide_selected];const auto& p=interface_assets.pages[guide_selected];
                ui.image(p[0],40,28);
                const bool token=l.scene<50 && l.scene%10>=1 && l.scene%10<=4;
                ui.image(p[7],36,token?103:107);ui.image(p[8],36,token?182:178);
                if(token){ui.image(p[3],76,110);ui.image(p[s.talisman?14:13],42,134);}
                ui.image(p[15],token?104:72,token?134:123);ui.image(p[6],92,200);
                if(l.gems) {ui.resident(11,274,26);ui.text(std::to_string(s.gems),359,28,8);ui.text("/",401,28,8);ui.text(std::to_string(l.gems),426,28,8);}
                const unsigned kind=l.scene%10;
                if(kind<6) {
                    const unsigned y=kind==5?152:52;ui.image(p[4],310,float(y+7));ui.image(p[7],274,float(y));ui.image(p[8],274,208);
                    for(unsigned i=0;i<l.orbs;++i){const bool earned=(s.orb_mask&(1u<<l.orb_bits[i]))!=0;ui.image(p[earned?1:2],282,float(y+32+i*34));ui.image(p[earned?9+i:5],334,float(y+29+i*34));}
                }
                if(kind==5 && guide_selected!=27) {
                    ui.image(p[7],274,52);ui.image(p[8],274,134);
                    const unsigned flight=std::clamp(l.scene/10,2u,5u)-2;
                    for(unsigned i=0;i<5;++i){ui.text(i<4?interface_assets.flight_labels[flight][i]:"All In One",286,float(60+i*16),8,true);ui.centered((s.flight_tasks&(1u<<i))?"Yes":"No",450,float(60+i*16),8);}
                }
                if(kind==6){ui.image(p[7],274,48);ui.image(p[8],274,184);ui.image(p[19],310,64);ui.image(p[18],310,92);ui.image(p[s.boss?20:21],278,160);}
                if(hit("guide-contents",92,199,108,15)) {guide_detail=false;guide_turn=0;menu_focus=true;}
                const auto move=[&](int dir){int i=static_cast<int>(guide_selected);do{i+=dir;}while(i>=0 && i<29 && !guide_stats[i].visited);if(i>=0 && i<29){guide_selected=static_cast<unsigned>(i);guide_turn=0;}};
                ui.image(p[16],36,200);ui.image(p[17],218,200);
                if(hit("guide-previous-level",32,195,34,24) || pressed(ImGuiKey_LeftArrow) || pad.pressed(pad::left))move(-1);
                if(hit("guide-next-level",211,195,30,24) || pressed(ImGuiKey_RightArrow) || pad.pressed(pad::right))move(1);
            }
            // Expose keyboard/mouse bindings without replacing the original
            // controller-labelled artwork in the book itself.
            std::vector<std::array<ImDrawVert,3>> current;
            for(const auto& command:ui.draw->CmdBuffer) {
                if(command.UserCallback || command.GetTexID()!=ui.texture)continue;
                const unsigned begin=std::max(command.IdxOffset,static_cast<unsigned>(index_begin)),end=command.IdxOffset+command.ElemCount;
                for(unsigned at=begin;at+2<end;at+=3) {
                    std::array<ImDrawVert,3> tri;
                    for(unsigned k=0;k<3;++k){tri[k]=ui.draw->VtxBuffer[ui.draw->IdxBuffer[at+k]+command.VtxOffset];tri[k].pos={(tri[k].pos.x-ui.left)/ui.sx,(tri[k].pos.y-ui.top)/ui.sy};}
                    current.push_back(tri);
                }
            }
            if(guide_turn<24 && !guide_previous_page.empty()) {
                const bool first=guide_turn<12;const float phase=first?float(guide_turn):float(guide_turn-12);
                const unsigned i=std::min(11u,static_cast<unsigned>(phase));
                const float angle=std::lerp(float(interface_assets.page_angles[i]),i<11?float(interface_assets.page_angles[i+1]):64.0f,phase-i)+(first?0:192);
                const auto append=[&](const auto& triangles,bool left,bool transform) {
                    for(const auto& tri:triangles) {
                        const float middle=(tri[0].pos.x+tri[1].pos.x+tri[2].pos.x)/3;
                        if((middle<256)!=left)continue;
                        ui.draw->PrimReserve(3,3);
                        for(auto v:tri){if(transform){const auto p=project_book_point(interface_assets,v.pos.x,v.pos.y,angle);v.pos={p[0],p[1]};}ui.draw->PrimVtx(ui.point(v.pos.x,v.pos.y),v.uv,v.col);}
                    }
                };
                ui.draw->PushTextureID(ui.texture);
                append(guide_previous_page,true,false);
                if(first)append(guide_previous_page,false,true);else append(current,true,true);
                ui.draw->PopTextureID();
            } else guide_previous_page=std::move(current);
            ui.centered("Arrows: pages   Enter: select  Esc: return",256,231,1,false,0.7f);
        }
        ui.draw->PopClipRect();
    }
