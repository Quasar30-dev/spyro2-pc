// Bounded, non-recording integration through the workbench's actual loader.
struct PortalIntegrationCheck {
    unsigned assertions{},stage{},completed{};bool travelling=false;
    static constexpr std::array<unsigned,5> destinations{17,15,33,57,15};
    void require(bool ok,const char* why){++assertions;if(!ok)throw FormatError(std::string("Portal integration: ")+why);}
    void setup(Workbench& app) {
        app.automatic_cinematics=false;app.campaign={};app.campaign_ready=false;app.playing=false;
        app.unlock_travel=true;app.load_level(15,false);app.play();
        app.game->player.gems=321;app.game->player.health=1;app.game->purchases=1u<<10;
        app.game->upgrades.enabled=true;app.game->upgrades.glide=2;
    }
    void tick(Workbench& app) {
        auto& g=*app.game;
        if(travelling) {
            if(g.portal_travel.active())return;
            require(g.level.wad_slot==destinations[stage],"Wrong scene loaded");
            require(g.player.gems==321 && g.player.health==1 && g.purchases==(1u<<10),"Travel lost campaign totals");
            require(g.upgrades.enabled && g.upgrades.glide==2,"Travel lost PC upgrades");
            require(finite(g.player.position) && g.portal_travel.cooldown>0,"Invalid arrival or missing re-entry protection");
            require(app.campaign.levels.contains(g.level.wad_slot),"Arrival was not remembered for PC saves");
            ++stage;++completed;travelling=false;
        }
        if(stage==destinations.size())return;
        g.portal_travel.cooldown=0;
        if(stage<2) {
            const auto p=std::find_if(g.level.portals.begin(),g.level.portals.end(),[&](auto& p){return p.destination==destinations[stage];});
            require(p!=g.level.portals.end(),"Missing authored doorway");
            Vec3 center{};for(auto v:p->aperture)center+=v;center=center/5;
            const auto n=normalized(cross(p->aperture[1]-p->aperture[0],p->aperture[2]-p->aperture[0]));
            p->enabled=true;p->opening=1;
            g.player.previous_position=center-n*0.5f-retail::origin_above_feet;
            g.player.position=center+n*0.05f-retail::origin_above_feet;
        } else {
            const auto h=std::find_if(g.level.home_transports.begin(),g.level.home_transports.end(),[&](auto& h){return h.destination==destinations[stage];});
            require(h!=g.level.home_transports.end(),"Missing paired homeworld transporter");
            h->active=h->armed=true;g.player.position=g.player.previous_position=g.retail_objects[h->object].position+Vec3{0,0.8f,0};
        }
        detect_portal_entry(g);require(g.portal_travel.phase==PortalTravel::Phase::Departing,"Entry did not start departure");travelling=true;
    }
};
