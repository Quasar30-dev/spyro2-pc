// Level completion through the retail end-of-level flow in the real workbench
// (--level-exit-check SLOT, hidden, bounded): Spyro is placed at the level's
// end in front of its talisman speaker (Winter Tundra: its completion-orb
// speaker), talks to it with the real keys (E, Enter), watches the reward,
// walks through the raised Return Home, sees the outro and arrives in the home
// world at the level's portal. A teleport stands in for the walk to the end.
// When the speaker's own end condition is a task the teleport skipped (an
// end-of-level event, a finished quest), the check does what that event does
// - it arms the talisman line (80066f44), or sets the level's task flag
// (80067148) for a Winter Tundra orb - and makes the speaker talk by itself
// (+5 = 2), and reports it as "simulated". Photos go beside --capture.
struct LevelExitCheck {
    unsigned slot{},stage{},ticks{},speaker=UINT32_MAX,home{},scene{},assertions{},reward_ticks{},talks{};
    unsigned perfect{};bool winter=false,simulated=false,outro_seen=false,finished=false,breeze=false;
    // The level's own end trigger, when the speaker waits for one: Magma
    // Cone's host 13B (kind 2) waits for the class-401 trigger at header +44
    // to reach state 2, which it does once Spyro stands in its rectangle
    // (8003873c, header +14). Spyro is put there instead of before the host.
    unsigned trigger=UINT32_MAX,restarts{};bool trigger_checked=false;
    // Fracture Hills: the end plateau and its floor are built by the six
    // satyrs (class 8A) Spyro frees; the check frees them first, as a player
    // does (flame, charge, then talk to each), before going to the end.
    std::vector<unsigned> quest;unsigned quest_at{},quest_ticks{},freed{};bool quest_done=true;
    std::filesystem::path folder;std::string stem;std::vector<std::string> photos;
    Vec3 arrival;std::uint64_t arrival_tick{};
    void require(bool ok,const std::string& why){++assertions;if(!ok)throw FormatError("Level exit check (WAD "+std::to_string(slot)+", stage "+std::to_string(stage)+"): "+why);}
    void photo(Workbench& app,const char* name,bool cinematic=false) {
        const auto path=folder/(stem+"-"+name+".bmp");
        if(cinematic)app.cinematic_renderer.save_bmp(path);else app.renderer.save_bmp(path);
        photos.push_back(path.filename().string());
        if(app.game && !cinematic) {   // where the camera was, for judging the photo
            const auto& g=*app.game;const auto v=g.camera.sample(1);const auto s=g.player.position+retail::origin_above_feet;
            std::printf("photo %s: spyro %.2f %.2f %.2f eye %.2f %.2f %.2f forward %.2f %.2f %.2f distance %.2f obstructed %d scripted %d paused %d game tick %llu playing %d\n",name,
                s.x,s.y,s.z,v.eye.x,v.eye.y,v.eye.z,v.forward.x,v.forward.y,v.forward.z,length(v.eye-s),g.camera.obstructed()?1:0,g.camera.scripted_view?1:0,
                g.paused?1:0,static_cast<unsigned long long>(g.clock.ticks()),app.playing?1:0);
            if(const auto& r=g.reward_show;r.active()) {   // the reward: speaker, point, the item and where it is
                const auto& sp=g.retail_objects[r.speaker].position;
                std::printf("  reward speaker %u type %x at %.2f %.2f %.2f point %.2f %.2f %.2f class %x item %u",r.speaker,g.retail_objects[r.speaker].type,sp.x,sp.y,sp.z,r.point.x,r.point.y,r.point.z,r.reward_class,r.item);
                if(r.item<g.retail_objects.size()){const auto& o=g.retail_objects[r.item];std::printf(" item at %.2f %.2f %.2f hidden %d collected %d",o.position.x,o.position.y,o.position.z,o.hidden?1:0,o.collected?1:0);}
                std::printf("\n");
            }
            for(const auto& p:g.level.portals) {   // the doorway nearest Spyro: which side of its aperture are Spyro and the eye?
                Vec3 c{};for(auto q:p.aperture)c+=q;c=c/5;if(length(c-s)>4)continue;
                const auto n=normalized(cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]));
                std::printf("  portal %u enabled %d: spyro side %.2f eye side %.2f\n",p.destination,p.enabled?1:0,dot(s-p.aperture[0],n),dot(v.eye-p.aperture[0],n));
            }
            std::fflush(stdout);
        }
    }
    // Stand in front of an object, facing it, on dry floor.
    bool stand(Game& g,Vec3 at,float heading,float distance) {
        for(const float r:{distance,distance*.7f,distance*1.4f,distance*2})for(float spread:{0.0f,0.5f,-0.5f,1.0f,-1.0f,1.6f,-1.6f,2.4f,-2.4f,3.14f}) {
            const Vec3 d{std::sin(heading+spread),0,std::cos(heading+spread)};
            const auto floor=g.level.raycast(at+d*r+Vec3{0,2.5f,0},{0,-1,0},5);
            if(!floor || floor->normal.y<.7f || std::abs(floor->point.y-at.y)>1.6f)continue;
            g.player.position=g.player.previous_position=floor->point+Vec3{0,.01f,0};g.player.velocity={};
            g.player.yaw=g.player.previous_yaw=std::atan2(-d.x,-d.z);g.camera.reset(g.level,g.player.position,g.player.yaw);return true;
        }
        return false;
    }
    void setup(Workbench& app,unsigned wad,const std::filesystem::path& capture) {
        slot=wad;folder=capture.parent_path();stem=capture.stem().string();
        app.automatic_cinematics=false;app.load_level(slot,false);app.play();
        auto& g=*app.game;scene=g.level.campaign_scene;
        const auto exit=std::find_if(g.level.portals.begin(),g.level.portals.end(),[](const auto& p){return p.return_home;});
        require(exit!=g.level.portals.end(),"the level has no Return Home");
        home=exit->destination;
        const unsigned index=(slot-15)/2;winter=index>0x15;breeze=slot==39;
        // The speaker whose conversation gives the talisman (command 1), or in
        // Winter Tundra the orb of the level's "Perfect" mask (80066e72 + level).
        const auto& archive=g.level.script_image->archive;
        // Several records can parse as conversations; the speaker is the one
        // nearest the level's Return Home (the end of the level).
        const auto end=g.retail_objects[exit->object].position;float best=1e9f;
        for(unsigned i=0;i<g.level.objects.size();++i) {
            if(g.retail_objects[i].state<0)continue;
            std::optional<NpcDefinition> npc;try{npc=decode_npc_record(archive,slot,i);}catch(const std::exception&){}
            if(!npc || npc->lines.size()>32)continue;
            for(const auto& line:npc->lines)if((!winter && line.talisman>0) || (winter && (line.orb_bits&exe_perfect(app,index)))) {
                const float d=length(g.retail_objects[i].position-end);if(d<best){best=d;speaker=i;}break;
            }
        }
        perfect=winter?exe_perfect(app,index):0;
        require(speaker!=UINT32_MAX,"no talisman / completion speaker found");
        g.player.health=3;g.player.lives=std::max(g.player.lives,4u);
        if(breeze){g.retail_objects[23].variant=1;g.retail_objects[24].variant=1;}   // both ship engines burned (the native burner quest)
        const auto& o=g.retail_objects[speaker];
        require(stand(g,o.position,retail::native_heading(o.rotation[2]*16),1.2f),"no floor in front of the speaker");
        if(slot==51) {
            for(unsigned i=0;i<g.retail_objects.size();++i)if(g.retail_objects[i].type==0x8a)quest.push_back(i);
            quest_done=quest.empty();
        }
        app.scripted_input=[this,&app](InputState& in){input(app,in);};
    }
    // 80066e72 + level in the executable file (0x800 header, loaded at 80010000).
    static unsigned exe_perfect(Workbench& app,unsigned index){const std::size_t at=0x80066e72-0x80010000+0x800+index;return at<app.executable.size()?app.executable[at]:0;}
    void input(Workbench& app,InputState& in) {
        if(!app.game || stage>=2)return;
        auto& g=*app.game;in={};g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,2u);g.player.health=std::max(g.player.health,1u);
        if(!quest_done) {   // the satyrs: flame (0..240), charge at it (240..330), talk (330..)
            if(g.dialogue.active()){if(ticks%40==20)in.dialogue.next=true;return;}
            if(quest_at<quest.size() && quest_ticks>0) {
                const auto target=g.retail_objects[quest[quest_at]].position;
                if(quest_ticks<240)in.flame_pressed=quest_ticks%30==1;
                else if(quest_ticks<330){auto d=target-g.player.position;d.y=0;const float l=length(d);if(l>.05f){in.move_x=d.x/l;in.move_z=d.z/l;}in.charge=quest_ticks<285;}
                else in.dialogue.interact=quest_ticks%8==0;
            }
            return;
        }
        if(stage==0 && !g.dialogue.active() && ticks%20==10)in.dialogue.interact=true;
        if(stage==1 && g.dialogue.active() && ticks%40==20) {
            const auto* line=g.dialogue.line();
            if(line && line->yes>=0 && line->no>=0)in.dialogue.choice=0;else in.dialogue.next=true;
        }
    }
    void tick(Workbench& app) {
        if(finished || !app.game)return;
        auto& g=*app.game;++ticks;g.player.health=std::max(g.player.health,1u);
        if(!quest_done) {
            if(!g.overlay_scripts.active())return;
            if(g.dialogue.active())return;
            if(quest_ticks==0) {
                const auto& o=g.retail_objects[quest[quest_at]];
                stand(g,o.position,retail::native_heading(o.rotation[2]*16),1.3f);
            }
            if(++quest_ticks>=560) {
                quest_ticks=0;
                // Still encased (state 0; a freed satyr's thanks can interrupt the
                // next one's flame): come back to it once the others are done.
                if(g.overlay_scripts.peek(g.overlay_scripts.moby_address(quest[quest_at])+0x48,1)==0 && quest.size()<12)quest.push_back(quest[quest_at]);
                if(++quest_at>=quest.size()) {
                    const auto h=g.overlay_scripts.peek(g.overlay_scripts.moby_address(quest[0]));
                    freed=h>=0x80000000u?g.overlay_scripts.peek(g.overlay_scripts.peek(h+0x20)):0;
                    require(freed==6,"the six satyrs were not all freed ("+std::to_string(freed)+")");
                    quest_done=true;ticks=0;
                    for(unsigned k=0;k<1;++k){}   // the plateau rises while Spyro walks to the end
                    const auto& o=g.retail_objects[speaker];
                    require(stand(g,o.position,retail::native_heading(o.rotation[2]*16),1.2f),"no floor in front of the speaker");
                }
            }
            return;
        }
        if(stage==0 && !trigger_checked && g.overlay_scripts.active()) {   // once the scripts run: is there an end trigger?
            trigger_checked=true;
            const auto& o=g.retail_objects[speaker];
            if(const auto h=g.overlay_scripts.peek(g.overlay_scripts.moby_address(speaker));o.type==0x13b && h>=0x80000000u && g.overlay_scripts.peek(h,1)==2) {
                const auto linked=g.overlay_scripts.peek(h+0x44);
                if(linked<g.retail_objects.size() && g.retail_objects[linked].type==0x401)trigger=linked;
            }
            if(trigger!=UINT32_MAX) {
                const auto th=g.overlay_scripts.peek(g.overlay_scripts.moby_address(trigger));
                const auto centre=retail::to_world({static_cast<int>(g.overlay_scripts.peek(th+0x14)),static_cast<int>(g.overlay_scripts.peek(th+0x18)),static_cast<int>(g.overlay_scripts.peek(th+0x1c))});
                const auto floor=g.level.raycast(centre+Vec3{0,3,0},{0,-1,0},12);
                require(floor.has_value(),"no floor in the end trigger");
                g.player.position=g.player.previous_position=floor->point+Vec3{0,.01f,0};g.player.velocity={};
                const auto d=o.position-g.player.position;g.player.yaw=g.player.previous_yaw=std::atan2(d.x,d.z);g.camera.reset(g.level,g.player.position,g.player.yaw);
            }
        }
        if(stage==0) {   // at the end: the speaker talks by itself, or Spyro presses E
            if(ticks==2 && winter){g.overlay_scripts.poke(0x80067148,g.overlay_scripts.peek(0x80067148,1)|perfect,1);simulated=true;}   // the level's task done
            if(g.dialogue.active()){stage=1;ticks=0;++talks;restarts=g.overlay_scripts.checkpoint_restarts;return;}
            if(trigger!=UINT32_MAX) {   // the level's own end: no simulation
                if(ticks==60)photo(app,"0-end");
                require(ticks<1800,"the speaker never opened a conversation after the end trigger");
                return;
            }
            if(ticks==(slot==51?1800u:240u) && !simulated) {
                // The teleport skipped the level's own end condition (Colossus' end
                // event 11A, Aquaria Towers' task): do what it does.
                const auto m=g.overlay_scripts.moby_address(speaker);const auto h=g.overlay_scripts.peek(m);
                g.overlay_scripts.poke(0x80066f44,1,1);if(h>=0x80000000u && h<0x80800000u)g.overlay_scripts.poke(h+5,2,1);simulated=true;
            }
            require(ticks<(slot==51?2300u:720u),"the speaker never opened a conversation");
            return;
        }
        if(stage==1) {   // the conversation, then the reward
            if(g.reward_show.active() && !reward_ticks)reward_ticks=1;
            if(reward_ticks){++reward_ticks;if(reward_ticks==(winter?150u:230u))photo(app,"1-reward");}
            if(!g.dialogue.active() && !g.reward_show.active() && (reward_ticks || ticks>120)) {
                const bool given=winter?(g.guide_awards.orbs&perfect)!=0:g.guide_awards.talisman;
                if(!given && !simulated) {   // the speaker talked before its end condition held: arm it and talk again
                    const auto h=g.overlay_scripts.peek(g.overlay_scripts.moby_address(speaker));
                    g.overlay_scripts.poke(0x80066f44,1,1);if(h>=0x80000000u && h<0x80800000u)g.overlay_scripts.poke(h+5,2,1);
                    simulated=true;stage=0;ticks=0;reward_ticks=0;return;
                }
                require(winter?(g.guide_awards.orbs&perfect)!=0:g.guide_awards.talisman,winter?"the completion orb was not awarded":"the talisman was not awarded");
                require(g.guide_awards.talisman,"the level's talisman byte (completion) is not set");
                require(g.reward_this_visit,"the reward was not marked for this visit");
                stage=2;ticks=0;
            }
            require(ticks<5400,"the conversation or reward did not finish");
            return;
        }
        const auto exit=std::find_if(g.level.portals.begin(),g.level.portals.end(),[](const auto& p){return p.return_home;});
        if(stage==2) {   // Return Home rises; stand before it
            if(ticks==1) {
                require(exit!=g.level.portals.end() && exit->enabled,"Return Home is not enabled");
                const auto& o=g.retail_objects[exit->object];
                Vec3 c{};for(auto v:exit->aperture)c+=v;c=c/5;
                auto n=normalized(cross(exit->aperture[1]-exit->aperture[0],exit->aperture[2]-exit->aperture[0]));
                if(!stand(g,c-retail::origin_above_feet,std::atan2(-n.x,-n.z),5) && !stand(g,c-retail::origin_above_feet,std::atan2(n.x,n.z),5))
                    stand(g,o.position,0,5);
            }
            if(ticks==100) {
                require(exit->rise>=1,"Return Home did not rise");
                photo(app,"2-exit");
                // Walk through it (the portal check's crossing).
                Vec3 c{};for(auto v:exit->aperture)c+=v;c=c/5;
                auto n=normalized(cross(exit->aperture[1]-exit->aperture[0],exit->aperture[2]-exit->aperture[0]));
                if(dot(c-(g.player.position+retail::origin_above_feet),n)<0)n=n*-1;
                exit->opening=1;g.portal_travel.cooldown=0;
                g.player.previous_position=c-n*.5f-retail::origin_above_feet;g.player.position=c+n*.05f-retail::origin_above_feet;
                detect_portal_entry(g);require(g.portal_travel.phase==PortalTravel::Phase::Departing,"walking into Return Home did not start the exit");
                stage=3;ticks=0;
            }
            return;
        }
        if(stage==3) {   // departure, the outro, the home world
            if(app.cinematic_playing) {
                outro_seen=true;
                if(ticks%400==120)photo(app,"3-outro",true);
                if(ticks>=600)app.finish_cinematic();   // skip the rest (Enter)
                return;
            }
            if(g.level.wad_slot==home && g.portal_travel.phase==PortalTravel::Phase::Idle) {
                if(!arrival.x && !arrival.z){arrival=g.player.position;arrival_tick=g.clock.ticks();}
                // The photo once the home world has run 150 game ticks (the
                // frame count alone can end before the game has settled).
                if(ticks>200 && g.clock.ticks()>=arrival_tick+150) {
                    require(g.level.wad_slot==home,"did not arrive in the home world");
                    const auto door=std::find_if(g.level.portals.begin(),g.level.portals.end(),[&](const auto& p){return p.scene_id==scene && !p.return_home;});
                    require(door!=g.level.portals.end(),"the home world has no portal to this level");
                    Vec3 c{};for(auto v:door->aperture)c+=v;c=c/5;
                    require(length(arrival-c)<9,"Spyro did not arrive at the level's portal");
                    require(app.campaign.levels.contains(slot) && app.campaign.levels.at(slot).awards.talisman,"the home world forgot the level's talisman / completion");
                    require(outro_seen || !cinematic_slot(app.executable,slot,true),"the outro did not play");
                    photo(app,"4-home");finished=true;
                }
                return;
            }
            require(ticks<4800,"the trip home did not finish");
        }
    }
};
