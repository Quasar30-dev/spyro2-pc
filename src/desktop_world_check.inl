// Bounded integration of world doors (plan H1) through the workbench's own
// loader: from the Spring Realm hub (--custom-level), through the crown gate
// to the Crownworks with its boss encounter, then Return Home back out in
// front of the gate. Entries are made exactly as the portal check makes them.
struct WorldDoorCheck {
    unsigned assertions{},stage{},completed{};bool travelling=false;
    Vec3 expected_return;std::string hub;
    void require(bool ok,const char* why){++assertions;if(!ok)throw FormatError(std::string("World door check: ")+why);}
    void setup(Workbench& app) {
        require(app.game && app.level && !app.level->custom_path.empty(),"needs a custom hub world (--custom-level)");
        app.unlock_travel=true;app.game->portal_travel.unlock=true;hub=app.level->custom_name;
    }
    void tick(Workbench& app) {
        auto& g=*app.game;
        if(travelling) {
            if(g.portal_travel.active())return;
            if(stage==0) {
                require(g.level.custom_name=="The Crownworks","the crown gate did not lead to the Crownworks");
                require(g.ripto.enabled && g.level.custom_arena && app.night_presentation && app.backrealms,"the Crownworks arrived without its night and boss encounter");
                require(g.world_return.has_value() && app.world_returns.size()==1 && g.world_return->world==app.world_returns.back().world,"the Crownworks does not know its way back");
                expected_return=app.world_returns.back().position;
            } else {
                require(g.level.custom_name==hub && app.world_returns.empty() && !g.world_return,"Return Home did not come back to the hub");
                require(length(Vec3{g.player.position.x-expected_return.x,0,g.player.position.z-expected_return.z})<1.5f,"Return Home did not come back out in front of the crown gate");
                require(!app.night_presentation && !app.backrealms,"the hub kept the Crownworks presentation");
            }
            require(finite(g.player.position) && g.portal_travel.cooldown>0,"invalid arrival or missing re-entry protection");
            ++stage;++completed;travelling=false;
        }
        if(stage==2)return;
        g.portal_travel.cooldown=0;
        CampaignPortal* door=nullptr;
        for(auto& p:g.level.portals)if(stage==0?!p.world.empty():p.return_home){door=&p;break;}
        require(door!=nullptr,stage==0?"the hub has no world door":"the Crownworks has no Return Home");
        if(stage==0)require(!door->world.empty() && door->world_emblems==4,"the crown gate is not a sealed world door");
        else {
            // Victory opens Return Home (as the boss's death sequence does).
            g.level.custom_arena->complete=true;g.retail_objects.at(door->object).state=2;
        }
        Vec3 center{};for(auto v:door->aperture)center+=v;center=center/5;
        const auto n=normalized(cross(door->aperture[1]-door->aperture[0],door->aperture[2]-door->aperture[0]));
        door->enabled=true;door->opening=1;
        // Walk in from the front, as a player coming from the castle does.
        g.player.previous_position=center+n*0.5f-retail::origin_above_feet;
        g.player.position=center-n*0.05f-retail::origin_above_feet;
        detect_portal_entry(g);require(g.portal_travel.phase==PortalTravel::Phase::Departing,"entry did not start departure");
        require(stage==0?g.portal_travel.request.world==door->world:g.portal_travel.request.world_return,"the entry is not a world request");
        travelling=true;
    }
};
