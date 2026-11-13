// Optional S2WORLD 3 arena module. Retail levels and earlier custom worlds do
// not instantiate it. Geometry keeps fixed topology for native GPU updates.
namespace {
void load_custom_arena(std::istream& in,Level& level) {
    std::string magic;unsigned version{};require(bool(in>>magic>>version) && magic=="ARENA" && version==1,"Invalid arena section");
    level.custom_arena.emplace();auto& arena=*level.custom_arena;
    const auto n=count(in,128);require(n>0,"Empty arena section");
    std::vector<bool> used_v(level.visual_vertices.size()),used_t(level.triangles.size());
    for(unsigned i=0;i<n;++i) {
        ArenaPiece p;p.kind=static_cast<ArenaKind>(count(in,16));p.name=text(in,80);p.center=point(in);p.extent=point(in);p.radius=scalar(in,64);p.parameter=scalar(in,3600);
        require(p.extent.x>=0 && p.extent.y>=0 && p.extent.z>=0 && p.radius>=0,"Invalid arena dimensions");
        const auto meshes=count(in,32);
        for(unsigned j=0;j<meshes;++j) {
            ArenaMesh m;m.vertex=count(in,unsigned(level.visual_vertices.size()));m.vertices=count(in,unsigned(level.visual_vertices.size()));
            m.triangle=count(in,unsigned(level.triangles.size()));m.triangles=count(in,unsigned(level.triangles.size()));m.role=count(in,3);
            require(m.vertex<=level.visual_vertices.size()-m.vertices && m.triangle<=level.triangles.size()-m.triangles,"Arena mesh range exceeds geometry");
            require(m.vertices%3==0,"Arena mesh range splits a triangle");
            for(unsigned k=0;k<m.vertices;++k){const auto id=m.vertex+k;require(!used_v[id],"Overlapping arena vertex ranges");used_v[id]=true;m.base_positions.push_back(level.visual_vertices[id].position);m.base_colors.push_back(level.visual_vertices[id].color);}
            for(unsigned k=0;k<m.triangles;++k){const auto id=m.triangle+k;require(!used_t[id],"Overlapping arena collision ranges");used_t[id]=true;const auto& t=level.triangles[id];for(auto v:{t.a,t.b,t.c})m.base_collision.push_back(v);}
            p.meshes.push_back(std::move(m));
        }
        for(const auto& m:p.meshes)for(unsigned k=0;k<m.triangles;++k)level.dynamic_triangles.push_back(m.triangle+k);   // moving arena parts
        arena.pieces.push_back(std::move(p));
    }
    require(std::count_if(arena.pieces.begin(),arena.pieces.end(),[](const auto& p){return p.kind>=ArenaKind::ShieldRelay && p.kind<=ArenaKind::OverdriveRelay;})==3,"Arena needs three distinct relay kinds");
    for(auto kind:{ArenaKind::ShieldRelay,ArenaKind::RepairRelay,ArenaKind::OverdriveRelay})require(std::count_if(arena.pieces.begin(),arena.pieces.end(),[&](const auto& p){return p.kind==kind;})==1,"Duplicate arena relay kind");
}
void arena_notice(Game& g,std::string text,unsigned ticks=180){g.custom_notice=std::move(text);g.custom_notice_ticks=ticks;}
float arena_horizontal(Vec3 d){return std::hypot(d.x,d.z);}
void arena_mesh(Game& g,ArenaPiece& p,float value,Vec3 tint={1,1,1}) {
    auto& level=g.level;
    // Colours can change even while a door/platform is stationary.
    const auto transform=[&](Vec3 v,unsigned role) {
        if(p.kind==ArenaKind::Trapdoor) {
            const float side=role?1.f:-1.f;const Vec3 pivot=p.center+Vec3{side*p.extent.x,0,0};auto d=v-pivot;
            const float a=side*value*1.48f,c=std::cos(a),s=std::sin(a);return pivot+Vec3{d.x*c-d.y*s,d.x*s+d.y*c,d.z};
        }
        if(p.kind==ArenaKind::Cover || p.kind==ArenaKind::ExitGate)return v+Vec3{0,value,0};
        if(p.kind==ArenaKind::Vent || (p.kind==ArenaKind::UnderwaterJet && role==0))return p.center+Vec3{v.x-p.center.x,(v.y-p.center.y)*value,v.z-p.center.z};
        if(p.kind==ArenaKind::UnderwaterCrusher && role==0)return v+Vec3{0,-value*2.15f,0};
        if(p.kind==ArenaKind::Lava)return v+Vec3{0,value,0};
        if(p.kind>=ArenaKind::ShieldRelay && p.kind<=ArenaKind::OverdriveRelay)return v+Vec3{0,-value*2.5f,0};
        return v;
    };
    bool changed=false;
    for(const auto& m:p.meshes) {
        for(unsigned k=0;k<m.vertices;++k) {
            auto& v=level.visual_vertices[m.vertex+k];const auto pos=transform(m.base_positions[k],m.role),color=Vec3{m.base_colors[k].x*tint.x,m.base_colors[k].y*tint.y,m.base_colors[k].z*tint.z};
            if(length(v.position-pos)>1e-5f || length(v.color-color)>1e-5f){v.position=pos;v.color=color;changed=true;}
        }
        level.touch_vertices(m.vertex,m.vertices);
        if(std::abs(p.value-value)>1e-5f){
            for(unsigned k=0;k<m.triangles;++k){auto& t=level.triangles[m.triangle+k];t.a=transform(m.base_collision[k*3],m.role);t.b=transform(m.base_collision[k*3+1],m.role);t.c=transform(m.base_collision[k*3+2],m.role);changed=true;}
            level.touch_triangles(m.triangle,m.triangles);
        }
    }
    p.value=value;if(changed)++level.terrain_revision;
}
void tick_custom_arena(Game& g,const InputState& input) {
    if(g.level.custom_arena && g.scene_locked && g.ripto.cinematic==CrownCinematic::Outro && g.ripto.cinematic_ticks>=210) {
        // The outro's last shot: the exit gate rises and Return Home opens
        // while the rest of the arena stays held.
        auto& a=*g.level.custom_arena;
        for(auto& p:a.pieces)if(p.kind==ArenaKind::ExitGate) {
            const float desired=a.complete?8.f:0.f;const float current=p.value< -100?0:p.value;arena_mesh(g,p,current+std::clamp(desired-current,0.f,.08f));
        }
        for(auto& p:g.level.portals)if(p.return_home){auto& o=g.retail_objects.at(p.object);o.state=a.complete?2:0;p.enabled=a.complete;}
        return;
    }
    if(!g.level.custom_arena || g.dialogue.active() || g.scene_locked || g.portal_travel.active())return;
    auto& a=*g.level.custom_arena;
    if(!a.initialized){a.initialized=true;a.last_lives=g.player.lives;arena_notice(g,a.ripto_encounter?"THE CROWNWORKS - Defeat Ripto. Relays strengthen his mechs. E operates stations.":"THE CROWNWORKS - Break the three relays. E operates stations.",360);}
    if(a.last_lives!=g.player.lives){a.last_lives=g.player.lives;a.charge_ticks=0;for(auto& p:a.pieces)if(p.kind==ArenaKind::Trapdoor){p.phase=p.timer=0;p.cooldown=180;}}
    ++a.ticks;if(a.cooling)--a.cooling;if(a.charge_ticks)--a.charge_ticks;
    g.powers.charge=a.charge_ticks>0;
    a.boss_shield=a.boss_repair=a.boss_overdrive=false;
    for(const auto& p:a.pieces)if(!p.destroyed){if(p.kind==ArenaKind::ShieldRelay)a.boss_shield=true;if(p.kind==ArenaKind::RepairRelay)a.boss_repair=true;if(p.kind==ArenaKind::OverdriveRelay)a.boss_overdrive=true;}
    // Trap shafts and missed lava escapes have a bounded death plane. The
    // existing hurt/reset path preserves the fixed clock while resetting Spyro.
    if(g.player.position.y< -5.0f && !g.level.water_at(g.player.position+retail::origin_above_feet)) {
        ++a.death_count;g.player.health=0;g.player.invulnerable_ticks=0;g.powers.invincibility={};g.hurt(g.player.position);
        a.last_lives=g.player.lives;a.charge_ticks=0;for(auto& p:a.pieces)if(p.kind==ArenaKind::Trapdoor){p.phase=p.timer=0;p.cooldown=180;}
        arena_notice(g,"Back at the entrance. Disabled relays stay disabled.");return;
    }
    bool action_used=false;float closest=1000;std::string prompt;
    for(auto& p:a.pieces) {
        if(p.cooldown)--p.cooldown;
        auto d=g.player.position-p.center;const float dist=arena_horizontal(d);const bool near=dist<p.radius && std::abs(d.y)<1.2f;
        const bool use=near && input.dialogue.interact && !action_used && !p.cooldown;
        const auto interact=[&](const std::string& label){if(near && dist<closest){closest=dist;prompt=p.cooldown?p.name+" - recharging "+std::to_string((p.cooldown+59)/60)+"s":"E - "+label;}};
        switch(p.kind) {
        case ArenaKind::Lava: {
            const float rise=a.boss_overdrive && !a.cooling?.45f:0.f;arena_mesh(g,p,rise,a.cooling?Vec3{.4f,.48f,.58f}:Vec3{1,.78f+.22f*std::sin(a.ticks*.04f),.8f});
            const float radius=arena_horizontal(g.player.position-p.center);
            if(!a.cooling && radius>p.extent.x && radius<p.extent.z && g.player.position.y<p.center.y+rise+.18f && g.player.position.y> -5 && g.hurt(g.player.position+Vec3{0,-1,0})){++a.damage_events;g.sound_events.push_back({SoundScope::Global,0x1d,0,g.player.position});}
            break;
        }
        case ArenaKind::Trapdoor: {
            // Circular lids use their true footprint; legacy rectangular lids
            // keep their original trigger and closing-clearance behaviour.
            const bool circular=p.parameter==1;
            const bool on_lid=circular?dist<p.radius-.1f:std::abs(d.x)<p.extent.x-.1f && std::abs(d.z)<p.extent.z-.1f;
            const bool in_shaft=circular?dist<p.radius+.35f:std::abs(d.x)<p.extent.x+.35f && std::abs(d.z)<p.extent.z+.35f;
            const bool above=on_lid && d.y>-.5f && d.y<.8f;
            if(p.phase==0 && above && g.player.grounded && !p.cooldown && !a.cooling){p.phase=1;p.timer=a.boss_overdrive?75:120;g.sound_events.push_back({SoundScope::Global,0x1d,0,p.center});}
            if(p.phase && p.timer)--p.timer;
            if(p.phase==1 && !p.timer){p.phase=2;p.timer=30;++a.trap_opens;}
            else if(p.phase==2 && !p.timer){p.phase=3;p.timer=180;}
            else if(p.phase==3 && !p.timer && !(in_shaft && d.y<1 && d.y> -5.5f)){p.phase=4;p.timer=30;}
            else if(p.phase==4 && !p.timer){p.phase=0;p.cooldown=180;}
            const float open=p.phase==2?1-p.timer/30.f:p.phase==3?1:p.phase==4?p.timer/30.f:0;
            // Arena tells are white (6.5.3: red is Ripto's, amber the mechs').
            const Vec3 tint=p.phase==1?(a.ticks%20<10?ripto_tell::arena*1.6f:Vec3{.55f,.55f,.6f}):Vec3{1,1,1};arena_mesh(g,p,open,tint);break;
        }
        case ArenaKind::Vent: {
            const bool overdrive=a.boss_overdrive || (g.ripto.crownworks() && g.ripto.difficulty==BossDifficulty::Hard);
            const unsigned period=overdrive?300:420,phase=(a.ticks+unsigned(p.parameter))%period;
            const bool active=!a.cooling && phase>=180 && phase<240;const bool warning=!a.cooling && phase>=135 && phase<180;
            arena_mesh(g,p,active?1.f:warning?.10f:.002f,warning?(a.ticks%20<10?ripto_tell::arena*1.6f:Vec3{.7f,.7f,.75f}):Vec3{1,.62f,.28f});
            if(active && dist<p.radius && d.y>=-.2f && d.y<3.3f && g.hurt(p.center))++a.damage_events;
            break;
        }
        case ArenaKind::UnderwaterJet:case ArenaKind::UnderwaterCrusher: {
            // Each obstacle telegraphs for a second, then leaves a generous
            // safe window. The cooling valve pauses both types of machinery.
            const unsigned cycle=(a.ticks+unsigned(p.parameter))%300;
            const bool active=!a.cooling && cycle>=180 && cycle<240;
            const bool warning=!a.cooling && cycle>=120 && cycle<180;
            p.phase=active?2:warning?1:0;
            const float descent=active?std::min(1.f,(cycle-180)/10.f):cycle>=240?std::max(0.f,1-(cycle-240)/30.f):0;
            const float value=p.kind==ArenaKind::UnderwaterJet?(active?1.f:warning?.12f:.001f):(a.cooling?0:descent);
            arena_mesh(g,p,value,active?Vec3{1,.32f,.18f}:warning?(a.ticks%20<10?ripto_tell::arena*1.6f:Vec3{.55f,.55f,.6f}):Vec3{.25f,.85f,1});
            if(cycle==180 && !a.cooling && length(d)<10)g.sound_events.push_back({SoundScope::Global,8,0,p.center});
            const auto origin=g.player.position+retail::origin_above_feet;
            const auto hit=origin-(p.kind==ArenaKind::UnderwaterCrusher?p.center+Vec3{0,-value*2.15f,0}:p.center);
            const bool contact=p.kind==ArenaKind::UnderwaterJet?dist<p.radius && hit.y>=0 && hit.y<p.extent.y:
                std::abs(hit.x)<p.extent.x+.2f && std::abs(hit.y)<p.extent.y+.2f && std::abs(hit.z)<p.extent.z+.2f;
            if(active && contact && g.player.swim.mode==WaterMode::Underwater && g.hurt(p.center)) {
                ++a.damage_events;
            }
            break;
        }
        case ArenaKind::Cover: {
            float desired=a.covers?0.f:-3.45f;float current=p.value< -100?desired:p.value;float next=current+std::clamp(desired-current,-.07f,.07f);
            // Raising cover waits until its whole footprint is clear.
            if(next>current && std::abs(d.x)<p.extent.x+.5f && std::abs(d.z)<p.extent.z+.5f && d.y<3.5f && d.y>-.5f)next=current;
            arena_mesh(g,p,next);break;
        }
        case ArenaKind::FireStation:case ArenaKind::FlyStation:case ArenaKind::ChargeStation:case ArenaKind::HealStation: {
            interact(p.name);
            if(use){action_used=true;++a.power_uses;p.cooldown=2700;
                if(p.kind==ArenaKind::FireStation)g.powers.fire={1200,1200};
                if(p.kind==ArenaKind::FlyStation)g.powers.flight={900,900};
                if(p.kind==ArenaKind::ChargeStation){a.charge_ticks=900;g.powers.charge=true;}
                if(p.kind==ArenaKind::HealStation){g.player.health=3;g.player.invulnerable_ticks=180;}
                arena_notice(g,p.name+" activated",180);g.sound_events.push_back({SoundScope::Global,27,0,p.center});
            }
            arena_mesh(g,p,0,p.cooldown?Vec3{.35f,.38f,.42f}:Vec3{1,1,1});break;
        }
        case ArenaKind::CoolingValve:case ArenaKind::CoverSwitch: {
            interact(p.name);if(use){action_used=true;++a.switch_uses;p.cooldown=p.kind==ArenaKind::CoolingValve?1800:60;
                if(p.kind==ArenaKind::CoolingValve){a.cooling=900;arena_notice(g,"Cooling active - lava, vents and tunnel traps safe for 15 seconds");}
                else {a.covers=!a.covers;arena_notice(g,a.covers?"Cover raised":"Cover lowered");}
                g.sound_events.push_back({SoundScope::Global,27,0,p.center});}
            arena_mesh(g,p,0,(p.kind==ArenaKind::CoolingValve?a.cooling>0:a.covers)?Vec3{.35f,1,.5f}:Vec3{1,1,1});break;
        }
        case ArenaKind::ShieldRelay:case ArenaKind::RepairRelay:case ArenaKind::OverdriveRelay: {
            if(p.destroyed){arena_mesh(g,p,1,{.22f,.25f,.3f});break;}
            if(a.boss_repair && p.kind!=ArenaKind::RepairRelay && a.ticks%600==0 && !p.cooldown && p.hits)--p.hits;
            if(near && dist<closest){closest=dist;prompt=p.name+" - charge, flame or melee to disable";}
            const Vec3 forward{std::sin(g.player.yaw),0,std::cos(g.player.yaw)};const bool facing=dist<.5f || dot(forward,normalized(Vec3{-d.x,0,-d.z}))>.25f;
            const bool charging=g.player.motion==Motion::Charge || g.player.motion==Motion::Supercharge;
            bool powered_hit=false;
            for(const auto& shot:g.powers.projectiles())if(shot.object<g.retail_objects.size() && length(g.retail_objects[shot.object].position-(p.center+Vec3{0,1,0}))<2.2f){powered_hit=true;break;}
            if(!p.cooldown && ((near && facing && (input.flame_pressed || input.melee_pressed || charging)) || powered_hit)){
                p.hits+=(powered_hit || g.powers.flame_enabled(g) || g.powers.charge_enabled(g))?2:1;p.cooldown=45;
                if(p.hits>=3){p.destroyed=true;++a.relay_breaks;arena_notice(g,p.name+" disabled ("+std::to_string(a.relay_breaks)+"/3)",240);g.sound_events.push_back({SoundScope::Global,28,0,p.center});}
            }
            arena_mesh(g,p,p.destroyed?1:0,p.cooldown?Vec3{1,.35f,.25f}:Vec3{1,1,1});break;
        }
        case ArenaKind::ExitGate: {
            const float desired=a.complete?8.f:0.f;const float current=p.value< -100?0:p.value;arena_mesh(g,p,current+std::clamp(desired-current,0.f,.08f));break;
        }
        case ArenaKind::LaunchPad: {
            interact(p.name);if(use && g.player.grounded){action_used=true;++a.launches;p.cooldown=120;g.player.velocity={0,14,p.parameter*9};g.player.grounded=false;g.player.airborne_ticks=4;g.player.jump={};g.player.jump.kind=JumpState::Kind::Normal;g.player.jump.released=true;g.player.air={};g.sound_events.push_back({SoundScope::Global,27,0,p.center});arena_notice(g,"Launch pad - steer or glide to a terrace");}break;
        }
        }
    }
    if(a.relay_breaks==3 && !a.complete && !a.ripto_encounter){a.complete=true;g.award_orb(0);arena_notice(g,"ARENA SECURED - Return Home is open",360);}
    for(auto& p:g.level.portals)if(p.return_home){auto& o=g.retail_objects.at(p.object);o.state=a.complete?2:0;p.enabled=a.complete;if(!a.complete)p.opening=p.rise=0;}
    if(!prompt.empty() && !action_used && g.custom_notice_ticks<65)arena_notice(g,prompt,60);
}
}
