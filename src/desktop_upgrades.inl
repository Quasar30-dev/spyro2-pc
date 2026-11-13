    void draw_upgrade_icon(ImDrawList* draw,unsigned icon,ImVec2 p,ImVec2 q,ImU32 tint=IM_COL32_WHITE) {
        if(icon==7) {
            draw_upgrade_icon(draw,5,p,q,tint);
            const float w=q.x-p.x,h=q.y-p.y;const unsigned alpha=tint>>24;
            const ImVec2 c{p.x+w*.5f,p.y+h*.52f};
            draw->PathArcTo(c,w*.4f,-.5f,4.5f,20);draw->PathStroke(IM_COL32(255,216,112,alpha),0,std::max(1.0f,w*.06f));
            draw->AddTriangleFilled({p.x+w*.18f,p.y+h*.04f},{p.x+w*.49f,p.y+h*.1f},{p.x+w*.31f,p.y+h*.32f},IM_COL32(255,216,112,alpha));
            return;
        }
        if(icon==6) {
            // A downward impact arrow with the HUD's purple and gold palette.
            const float w=q.x-p.x,h=q.y-p.y,cx=(p.x+q.x)*.5f;
            const unsigned alpha=(tint>>24)&255;
            draw->AddEllipse({cx,p.y+h*.78f},{w*.43f,h*.18f},IM_COL32(40,13,47,alpha),0,20,w*.08f);
            draw->AddEllipse({cx,p.y+h*.77f},{w*.4f,h*.15f},IM_COL32(255,170,55,alpha),0,20,w*.045f);
            draw->AddTriangleFilled({cx-w*.2f,p.y+h*.36f},{cx+w*.2f,p.y+h*.36f},{cx,p.y+h*.74f},IM_COL32(255,225,128,alpha));
            draw->AddRectFilled({cx-w*.09f,p.y+h*.03f},{cx+w*.09f,p.y+h*.42f},IM_COL32(162,89,220,alpha));
            return;
        }
        if(!renderer.upgrade_image() || icon>=6)return;
        const auto& s=upgrade_art.sprites[icon];
        const ImVec2 uv{float(s.x)/upgrade_art.width,float(s.y)/upgrade_art.height},uv2{float(s.x+s.width)/upgrade_art.width,float(s.y+s.height)/upgrade_art.height};
        const auto image=reinterpret_cast<ImTextureID>(renderer.upgrade_image());
        const float offset=std::max(1.0f,(q.y-p.y)/36);
        draw->AddImage(image,{p.x+offset,p.y+offset},{q.x+offset,q.y+offset},uv,uv2,IM_COL32(12,5,24,((tint>>24)&255)*3/4));
        draw->AddImage(image,p,q,uv,uv2,tint);
    }
    void draw_upgrade_hud(ImVec2 origin,ImVec2 size) {
        if(!renderer.interface_image() || !renderer.upgrade_image())return;
        auto snapshot=upgrade_hud_snapshot(*game);
        snapshot.suppressed|=dialogue_visible || cinematic_playing;
        const auto& io=ImGui::GetIO();
        upgrade_display.update(snapshot,io.DeltaTime,(!io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_Tab)) || pad.down(pad::back),game->upgrade_hud_settings);
        RetailCanvas c(interface_assets,ImGui::GetWindowDrawList(),reinterpret_cast<ImTextureID>(renderer.interface_image()),origin,size);
        // Anchor to the viewport safe edge, including widescreen. Sprite aspect
        // compensates for the original PS1 display's non-square source pixels.
        c.left=origin.x+std::max(10.0f,size.x*0.02f);
        c.opacity=upgrade_display.points_opacity;
        if(c.opacity>0) {
            c.text("UPGRADES",2,183,1,true,0.7f);
            c.text(std::to_string(snapshot.points)+" POINTS",110,183,0,true,0.65f);
        }
        for(unsigned i=0;i<upgrade_icon_count;++i) {
            const float alpha=upgrade_display.opacity[i];if(alpha<=0)continue;
            const auto& item=snapshot.items[i];c.opacity=alpha;
            const float x=2+i*55.0f,y=196+(1-alpha)*7;
            const bool dim=item.status==UpgradeStatus::Locked || item.status==UpgradeStatus::Dormant || item.status==UpgradeStatus::Spent;
            const auto tint=dim?IM_COL32(137,120,162,unsigned(alpha*200)):IM_COL32(255,255,255,unsigned(alpha*255));
            if(item.status==UpgradeStatus::Active) {
                const float glow=0.65f+0.2f*std::sin(float(game->clock.ticks()%120)*0.10472f);
                c.image(interface_assets.resident[19],x+3,y+1,IM_COL32(255,222,134,unsigned(glow*alpha*200)),1.1f);
            }
            draw_upgrade_icon(c.draw,i,c.point(x,y),c.point(x+40,y+25),tint);
            for(unsigned p=0;p<item.maximum;++p) {
                const bool filled=p<(i==3?item.remaining:item.tier) && snapshot.enabled;
                c.image(interface_assets.resident[26],x+41,y+3+p*9,filled?IM_COL32_WHITE:IM_COL32(75,60,98,180),0.3f);
            }
            c.centered(item.value,x+20,y+27,item.status==UpgradeStatus::Active?1:0,true,0.57f);
        }
    }
    void upgrade_page(RetailCanvas& ui,bool& focus_first) {
        auto& u=game->upgrades;const auto snapshot=upgrade_hud_snapshot(*game);
        ui.rect(10,10,492,220,IM_COL32(28,17,49,245));
        ui.image(interface_assets.pause[15],10,10);ui.image(interface_assets.pause[16],454,10);
        ui.image(interface_assets.pause[17],10,198);ui.image(interface_assets.pause[18],454,198);
        ui.centered("UPGRADES",256,20,1,false,1.3f);
        const auto hit=[&](const char* id,float x,float y,float w,float h,bool enabled=true) {
            ImGui::SetCursorScreenPos(ui.point(x,y));ImGui::BeginDisabled(!enabled);
            const bool clicked=ImGui::InvisibleButton(id,{w*ui.sx,h*ui.sy},ImGuiButtonFlags_EnableNav);
            menu_buttons[id]=ui.point(x+w/2,y+h/2);
            if(focus_first && enabled){ImGui::SetKeyboardFocusHere(-1);focus_first=false;}
            if(clicked)audio.menu_sound(1);
            else if(ImGui::IsItemFocused() && menu_navigation!=ImGui::GetItemID()){menu_navigation=ImGui::GetItemID();audio.menu_sound(0);}
            ImGui::EndDisabled();return clicked;
        };
        if(hit("upgrade-enable",30,37,236,13)) {u.enabled=!u.enabled;if(!u.enabled)game->abilities.reset(*game);remember_settings();}
        ui.text(u.enabled?"PROGRESSION: ON":"PROGRESSION: OFF",32,39,ImGui::IsItemFocused() || ImGui::IsItemHovered()?3:0,true,0.78f);
        ui.text(std::to_string(u.points)+" POINTS",328,39,1,true,0.78f);
        for(unsigned i=0;i<upgrade_icon_count;++i) {
            const auto& item=snapshot.items[i];const float x=28+(i%2)*232.0f,y=55+(i/2)*32.0f;
            const auto price=u.cost(item.upgrade);const bool can_buy=u.enabled && price && u.points>=price && item.asset_ready;
            const auto id="upgrade-buy-"+std::to_string(i);
            // Locked/complete cards remain focusable so their requirements and
            // controls are readable with keyboard or controller navigation.
            if(hit(id.c_str(),x,y,220,30) && can_buy) {u.buy(item.upgrade);remember_settings();}
            const bool focused=ImGui::IsItemHovered() || ImGui::IsItemFocused();if(focused)upgrade_selection=i;
            ui.rect(x,y,220,30,focused?IM_COL32(92,56,113,180):IM_COL32(48,31,66,170));
            draw_upgrade_icon(ui.draw,i,ui.point(x+4,y+3),ui.point(x+44,y+27),u.enabled?IM_COL32_WHITE:IM_COL32(157,142,170,255));
            ui.text(upgrade_name(i),x+50,y+3,focused?1:0,true,0.69f);
            ui.text(std::to_string(item.tier)+"/"+std::to_string(item.maximum),x+186,y+3,0,true,0.63f);
            const auto label=!item.asset_ready?"IMPORT JULY BUILD":!price?"COMPLETE":!u.enabled?"ENABLE TO BUY":std::to_string(price)+(can_buy?" POINTS - BUY":" POINTS NEEDED");
            ui.text(label,x+50,y+18,can_buy?1:0,true,0.59f);
        }
        if(hit("upgrade-elements",30,183,220,12))open_menu(Menu::Elements);
        const bool elements_focus=ImGui::IsItemHovered() || ImGui::IsItemFocused();
        ui.rect(30,183,220,12,IM_COL32(48,31,66,170));
        ui.text("ELEMENTS",38,185,elements_focus?1:0,true,.6f);
        for(unsigned k=0;k<element_count;++k)draw_element_icon(ui.draw,static_cast<Element>(k),ui.point(112+k*26,183),ui.point(124+k*26,195),
            u.elements.owns(static_cast<Element>(k))?IM_COL32_WHITE:IM_COL32(120,104,140,200));
        if(hit("headbash-mode",260,183,220,12,u.headbash>0)) {
            u.headbash_mode=static_cast<HeadbashMode>((static_cast<unsigned>(u.headbash_mode)+1)%(u.headbash>=2?3u:2u));remember_settings();
        }
        const bool bash_focus=ImGui::IsItemHovered() || ImGui::IsItemFocused();
        ui.rect(260,183,220,12,IM_COL32(48,31,66,170));
        ui.text("HEADBASH",268,185,bash_focus?1:0,true,.6f);
        ui.text(headbash_mode_name(u.headbash_mode),354,185,u.headbash?0:13,true,.6f);
        const auto& selected=snapshot.items[upgrade_selection];
        ui.centered(upgrade_description(upgrade_selection,selected.tier),256,201,1,true,0.68f);
        if(hit("upgrade-back",181,214,150,12))open_menu(Menu::Pause);
        ui.centered("BACK",256,215,ImGui::IsItemHovered() || ImGui::IsItemFocused()?3:0,true,0.8f);
    }
    // Original element glyphs over a coloured gem disc (menu, HUD and workbench).
    void draw_element_icon(ImDrawList* draw,Element element,ImVec2 p,ImVec2 q,ImU32 tint=IM_COL32_WHITE) {
        const float w=q.x-p.x,h=q.y-p.y,cx=(p.x+q.x)*.5f,cy=(p.y+q.y)*.5f,r=std::min(w,h)*.5f;
        const unsigned a=(tint>>24)&255;const bool dim=(tint&0xffffff)!=0xffffff;
        if(renderer.element_icon_image()) {
            // Generated PS1-style element sprites (assets/ui/elements).
            const float k=static_cast<float>(static_cast<unsigned>(element));
            const auto image=reinterpret_cast<ImTextureID>(renderer.element_icon_image());
            const float side=std::min(w,h),x=cx-side/2,y=cy-side/2,shadow=std::max(1.0f,side/24);
            const float v1=32.0f/std::max(1u,element_hud.height);
            draw->AddImage(image,{x+shadow,y+shadow},{x+side+shadow,y+side+shadow},{k*32/160,0},{(k+1)*32/160,v1},IM_COL32(12,5,24,a*3/4));
            draw->AddImage(image,{x,y},{x+side,y+side},{k*32/160,0},{(k+1)*32/160,v1},tint);
            return;
        }
        const auto c=element_color(element);const auto k=element_core_color(element);
        const auto col=[&](Vec3 v,float m=1){return IM_COL32(int(std::clamp(v.x*m,0.0f,1.0f)*255),int(std::clamp(v.y*m,0.0f,1.0f)*255),int(std::clamp(v.z*m,0.0f,1.0f)*255),a);};
        const float m=dim?.5f:1;
        draw->AddCircleFilled({cx,cy},r,IM_COL32(18,8,28,a),16);
        draw->AddCircleFilled({cx,cy},r*.86f,col(c*.55f,m),16);
        draw->AddCircleFilled({cx-r*.12f,cy-r*.12f},r*.62f,col(c,m),16);
        draw->AddCircle({cx,cy},r*.9f,IM_COL32(255,212,110,a),16,std::max(1.0f,r*.12f));
        const auto glyph=col(k,m);const float t=std::max(1.0f,r*.16f);
        switch(element) {
        case Element::Fire:
            draw->AddTriangleFilled({cx-r*.32f,cy+r*.38f},{cx+r*.32f,cy+r*.38f},{cx+r*.05f,cy-r*.55f},glyph);
            draw->AddTriangleFilled({cx-r*.32f,cy+r*.38f},{cx,cy+r*.38f},{cx-r*.28f,cy-r*.15f},glyph);break;
        case Element::Water:
            draw->AddCircleFilled({cx,cy+r*.14f},r*.3f,glyph,12);draw->AddTriangleFilled({cx-r*.28f,cy+r*.04f},{cx+r*.28f,cy+r*.04f},{cx,cy-r*.55f},glyph);break;
        case Element::Ice:
            for(unsigned s=0;s<3;++s){const float an=s*1.0472f;draw->AddLine({cx-std::cos(an)*r*.55f,cy-std::sin(an)*r*.55f},{cx+std::cos(an)*r*.55f,cy+std::sin(an)*r*.55f},glyph,t);}break;
        case Element::Electric: {
            const ImVec2 bolt[]{{cx+r*.18f,cy-r*.6f},{cx-r*.22f,cy+r*.04f},{cx+r*.08f,cy+r*.04f},{cx-r*.18f,cy+r*.6f},{cx+r*.26f,cy-r*.08f},{cx-r*.04f,cy-r*.08f}};
            draw->AddConvexPolyFilled(bolt,3,glyph);draw->AddConvexPolyFilled(bolt+3,3,glyph);
            draw->AddTriangleFilled(bolt[1],bolt[2],bolt[5],glyph);draw->AddTriangleFilled(bolt[2],bolt[4],bolt[5],glyph);break;
        }
        case Element::Convexity:
            draw->AddCircleFilled({cx,cy},r*.42f,IM_COL32(8,2,14,a),14);
            draw->AddCircle({cx,cy},r*.42f,glyph,14,t);draw->AddCircleFilled({cx,cy},r*.16f,IM_COL32(240,236,255,a),10);break;
        }
    }
    void elements_page(RetailCanvas& ui,bool& focus_first) {
        auto& u=game->upgrades;auto& e=u.elements;
        ui.rect(10,10,492,220,IM_COL32(28,17,49,245));
        ui.image(interface_assets.pause[15],10,10);ui.image(interface_assets.pause[16],454,10);
        ui.image(interface_assets.pause[17],10,198);ui.image(interface_assets.pause[18],454,198);
        ui.centered("ELEMENTS",256,18,1,false,1.25f);
        const auto hit=[&](const char* id,float x,float y,float w,float h,bool enabled=true) {
            ImGui::SetCursorScreenPos(ui.point(x,y));ImGui::BeginDisabled(!enabled);
            const bool clicked=ImGui::InvisibleButton(id,{w*ui.sx,h*ui.sy},ImGuiButtonFlags_EnableNav);
            menu_buttons[id]=ui.point(x+w/2,y+h/2);
            if(focus_first && enabled){ImGui::SetKeyboardFocusHere(-1);focus_first=false;}
            if(clicked)audio.menu_sound(1);
            else if(ImGui::IsItemFocused() && menu_navigation!=ImGui::GetItemID()){menu_navigation=ImGui::GetItemID();audio.menu_sound(0);}
            ImGui::EndDisabled();return clicked;
        };
        ui.text(u.enabled?"PROGRESSION: ON":"PROGRESSION: OFF",30,34,u.enabled?0:13,true,.62f);
        ui.text(std::to_string(u.points)+" POINTS",360,34,1,true,.62f);
        for(unsigned k=0;k<element_count;++k) {
            const auto element=static_cast<Element>(k);const float y=46+k*25.0f;
            const auto price=u.element_cost(element);const bool can_buy=element_purchasable(*game,element) && u.points>=price;
            // Card body equips an owned element (a first purchase also equips);
            // the price column buys the next tier.
            const auto id="element-card-"+std::to_string(k),buy="element-buy-"+std::to_string(k);
            if(hit(id.c_str(),28,y,320,23)) {
                if(e.owns(element)){e.equipped=element;remember_settings();}
                else if(can_buy && u.buy_element(element,convexity_milestone(*game)))remember_settings();
            }
            bool focused=ImGui::IsItemHovered() || ImGui::IsItemFocused();
            if(hit(buy.c_str(),350,y,134,23,can_buy) && u.buy_element(element,convexity_milestone(*game)))remember_settings();
            focused=focused || ImGui::IsItemHovered() || ImGui::IsItemFocused();if(focused)element_selection=k;
            ui.rect(28,y,456,23,e.equipped==element && e.owns(element)?IM_COL32(110,64,138,190):focused?IM_COL32(92,56,113,180):IM_COL32(48,31,66,170));
            draw_element_icon(ui.draw,element,ui.point(32,y+2),ui.point(51,y+21),e.owns(element)?IM_COL32_WHITE:IM_COL32(120,104,140,200));
            ui.text(element_name(element),58,y+3,focused?1:0,true,.66f);
            ui.text(std::to_string(e.tier(element))+"/3",168,y+3,0,true,.62f);
            ui.text(std::string(element_breath_name(element))+"  "+element_shot_name(element)+"  "+element_fury_name(element),58,y+13,e.owns(element)?0:13,true,.5f);
            const std::string label=!price?"COMPLETE":!u.enabled?"ENABLE TO BUY":element==Element::Convexity && !e.owns(element) && !element_purchasable(*game,element)?"LOCKED":
                std::to_string(price)+(can_buy?" - BUY":" NEEDED");
            ui.text(label,356,y+3,can_buy?1:0,true,.58f);
            if(e.owns(element))ui.text(e.equipped==element?"EQUIPPED":"EQUIP",356,y+13,e.equipped==element?1:0,true,.5f);
        }
        ui.text("ENERGY "+std::to_string(static_cast<int>(e.energy))+"   FURY "+std::to_string(static_cast<int>(e.fury)),30,173,0,true,.62f);
        if(hit("element-dark",256,171,110,12)){cheats.dark_spyro=!cheats.dark_spyro;game->cheats=cheats;remember_settings();}
        ui.text(cheats.dark_spyro?"DARK SPYRO: ON":"DARK SPYRO: OFF",258,173,ImGui::IsItemHovered() || ImGui::IsItemFocused()?3:0,true,.6f);
        if(hit("element-smoke",372,171,110,12,cheats.dark_spyro)){cheats.dark_smoke=!cheats.dark_smoke;game->cheats=cheats;remember_settings();}
        ui.text(cheats.dark_smoke?"SMOKE: ON":"SMOKE: OFF",374,173,cheats.dark_spyro?(ImGui::IsItemHovered() || ImGui::IsItemFocused()?3:0):13,true,.6f);
        const auto chosen=static_cast<Element>(std::min(element_selection,element_count-1));
        ui.centered(chosen==Element::Convexity?"CONVEXITY NEEDS WATER, ICE, ELECTRICITY AND RIPTO DEFEATED (OR ALL MASTERED)":
            "F BREATH  Q SHOT  L FURY  1-5 SELECT   CIRCLE / R2 / L2 / D-PAD",256,190,1,true,.56f);
        if(hit("elements-back",181,212,150,12))open_menu(Menu::Upgrades);
        ui.centered("BACK",256,213,ImGui::IsItemHovered() || ImGui::IsItemFocused()?3:0,true,.8f);
    }
    void destruction_page(RetailCanvas& ui,bool& focus_first) {
        auto& settings=game->destruction_settings;
        ui.rect(10,10,492,220,IM_COL32(28,17,49,245));
        ui.image(interface_assets.pause[15],10,10);ui.image(interface_assets.pause[16],454,10);
        ui.image(interface_assets.pause[17],10,198);ui.image(interface_assets.pause[18],454,198);
        ui.centered("TERRAIN DESTRUCTION",256,23,1,false,1.1f);
        const auto choice=[&](const char* id,const std::string& label,float y) {
            ImGui::SetCursorScreenPos(ui.point(45,y-2));
            const bool clicked=ImGui::InvisibleButton(id,{422*ui.sx,18*ui.sy},ImGuiButtonFlags_EnableNav);
            menu_buttons[id]=ui.point(256,y+7);
            if(focus_first){ImGui::SetKeyboardFocusHere(-1);focus_first=false;}
            ui.centered(label,256,y,ImGui::IsItemHovered() || ImGui::IsItemFocused()?3:0,true,.78f);
            if(clicked)audio.menu_sound(1);
            else if(ImGui::IsItemFocused() && menu_navigation!=ImGui::GetItemID()){menu_navigation=ImGui::GetItemID();audio.menu_sound(0);}
            return clicked;
        };
        const auto toggle=[&](const char* id,const char* label,float y,bool& value) {
            if(choice(id,std::string(label)+(value?": ON":": OFF"),y)){value=!value;remember_settings();}
        };
        toggle("destruction-enable","Dynamic destruction",44,settings.enabled);
        toggle("destruction-charge","Supercharge dents and cracks",60,settings.charge);
        toggle("destruction-flame","Superflame building damage",76,settings.flame);
        toggle("destruction-bomb","Green bomb building damage",92,settings.bomb);
        toggle("destruction-protection","Protect gameplay surfaces",108,settings.protect_gameplay);
        if(choice("destruction-strength","Impact strength: "+std::to_string(settings.strength),124)){settings.strength=settings.strength%3+1;remember_settings();}
        toggle("destruction-void-slice","Void Beam slices terrain",140,settings.void_slice);
        if(choice("destruction-cut-size",std::string("Void cut size: ")+(settings.slice_size==1?"SMALL":settings.slice_size==2?"MEDIUM":"LARGE"),156)){settings.slice_size=settings.slice_size%3+1;remember_settings();}
        if(choice("destruction-reset","Restore current level terrain",174)){game->destruction.clear(*game);remember_settings();}
        ui.centered("Damage and these options are included in your saves.",256,193,1,true,.65f);
        if(choice("destruction-back","BACK",208))open_menu(Menu::Options);
    }
    void upgrade_hud_page(RetailCanvas& ui,bool& focus_first) {
        auto& settings=game->upgrade_hud_settings;
        ui.rect(10,10,492,220,IM_COL32(28,17,49,245));
        ui.image(interface_assets.pause[15],10,10);ui.image(interface_assets.pause[16],454,10);
        ui.image(interface_assets.pause[17],10,198);ui.image(interface_assets.pause[18],454,198);
        ui.centered("UPGRADE HUD",256,27,1,false,1.2f);
        const auto choice=[&](const char* id,const std::string& label,float y,bool enabled=true) {
            ImGui::SetCursorScreenPos(ui.point(55,y-2));ImGui::BeginDisabled(!enabled);
            const bool clicked=ImGui::InvisibleButton(id,{402*ui.sx,20*ui.sy},ImGuiButtonFlags_EnableNav);
            menu_buttons[id]=ui.point(256,y+7);
            if(focus_first && enabled){ImGui::SetKeyboardFocusHere(-1);focus_first=false;}
            const bool focused=enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
            ui.centered(label,256,y,enabled?(focused?3:0):13,true,.9f);
            if(clicked)audio.menu_sound(1);
            else if(ImGui::IsItemFocused() && menu_navigation!=ImGui::GetItemID()){menu_navigation=ImGui::GetItemID();audio.menu_sound(0);}
            ImGui::EndDisabled();return clicked;
        };
        if(choice("upgrade-hud-mode",std::string("Display: ")+upgrade_hud_mode_name(settings.mode),65)) {
            settings.mode=static_cast<UpgradeHudMode>((static_cast<unsigned>(settings.mode)+1)%5);upgrade_display.hide();remember_settings();
        }
        const bool timed=settings.mode==UpgradeHudMode::Once || settings.mode==UpgradeHudMode::OnUse;
        if(choice("upgrade-hud-duration","Reminder: "+std::to_string(settings.seconds)+" seconds",95,timed)) {
            settings.seconds=settings.seconds%8+1;upgrade_display.hide();remember_settings();
        }
        ui.centered(upgrade_hud_mode_description(settings.mode),256,132,1,true,.7f);
        ui.centered(settings.mode==UpgradeHudMode::Once?"Points, repeat uses and level changes do not replay reminders.":
            settings.mode==UpgradeHudMode::Off?"Your powers and the regular game HUD are unaffected.":"Dialogue, cutscenes and minigames still hide these icons.",256,148,0,true,.64f);
        ui.centered("These choices are included when you save your game.",256,171,0,true,.7f);
        if(choice("upgrade-hud-back","BACK",200))open_menu(Menu::Options);
    }
