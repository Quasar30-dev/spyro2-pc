namespace homeworld {
void load(std::istream& in,Level& level) {
    std::string magic;unsigned version{};require(bool(in>>magic>>version) && magic=="GAMEPLAY" && version==1,"Invalid homeworld gameplay section");
    const auto n=count(in,128);std::vector<bool> used_v(level.visual_vertices.size()),used_t(level.triangles.size());
    for(unsigned i=0;i<n;++i) {
        HomeworldPiece p;const auto kind=count(in,4);require(kind>0,"Invalid homeworld mechanism");p.kind=HomeworldKind(kind);p.objective=count(in,7);
        p.name=text(in,80);p.center=point(in);p.extent=point(in);p.axis=point(in);p.travel=scalar(in,16);p.period=scalar(in,3600);
        require(p.extent.x>0 && p.extent.y>0 && p.extent.z>0 && p.period>0 && std::abs(length(p.axis)-1)<.01f,"Invalid homeworld mechanism dimensions");
        const auto meshes=count(in,32);require(meshes>0,"Homeworld mechanism has no mesh");
        for(unsigned j=0;j<meshes;++j) {
            HomeworldMesh m;m.vertex=count(in,unsigned(level.visual_vertices.size()));m.vertices=count(in,unsigned(level.visual_vertices.size()));
            m.triangle=count(in,unsigned(level.triangles.size()));m.triangles=count(in,unsigned(level.triangles.size()));
            require(m.vertices>0 && m.vertices%3==0 && m.vertex<=level.visual_vertices.size()-m.vertices && m.triangle<=level.triangles.size()-m.triangles,"Invalid homeworld mesh range");
            for(unsigned k=0;k<m.vertices;++k){const auto id=m.vertex+k;require(!used_v[id],"Overlapping homeworld render meshes");require(!level.terrain_materials[level.visual_vertices[id].material].translucent,"Homeworld mechanisms require opaque geometry");used_v[id]=true;m.positions.push_back(level.visual_vertices[id].position);m.colors.push_back(level.visual_vertices[id].color);}
            for(unsigned k=0;k<m.triangles;++k){const auto id=m.triangle+k;require(!used_t[id],"Overlapping homeworld collision meshes");used_t[id]=true;const auto& t=level.triangles[id];m.collision.insert(m.collision.end(),{t.a,t.b,t.c});}
            p.meshes.push_back(std::move(m));
        }
        for(const auto& m:p.meshes)for(unsigned k=0;k<m.triangles;++k)level.dynamic_triangles.push_back(m.triangle+k);   // moving platforms
        level.homeworld_pieces.push_back(std::move(p));
    }
    const auto trials=count(in,8);unsigned mask=0;
    for(unsigned i=0;i<trials;++i){HomeworldTrial t;t.objective=count(in,7);t.limit=count(in,36000);require(t.limit>=60 && !(mask&(1u<<t.objective)),"Invalid homeworld trial");mask|=1u<<t.objective;t.start=point(in);t.name=text(in,80);level.homeworld_trials.push_back(std::move(t));}
}
void notice(Game& g,const std::string& text,unsigned duration=180){g.custom_notice=text;g.custom_notice_ticks=duration;}
bool ready(const Game& g,unsigned objective) {
    for(const auto& p:g.level.homeworld_pieces)if(p.kind==HomeworldKind::FlameRelay && p.objective==objective && !p.active)return false;
    for(const auto& t:g.level.homeworld_trials)if(t.objective==objective && !t.active && !t.complete)return false;
    return true;
}
void tick(Game& g,const InputState& input) {
    if(g.dialogue.active() || g.portal_travel.active() || g.scene_locked)return;
    for(auto& p:g.level.homeworld_pieces) {
        const auto old=p.axis*p.offset,center=p.center+old;
        const auto feet=g.player.position;
        const bool riding=g.player.grounded && std::abs(feet.x-center.x)<p.extent.x-.15f && std::abs(feet.z-center.z)<p.extent.z-.15f && std::abs(feet.y-(center.y+p.extent.y))<.16f;
        float next=p.offset;Vec3 tint{1,1,1};++p.age;
        if(p.kind==HomeworldKind::Shuttle)next=std::sin(float(p.age)*6.283185307f/p.period)*p.travel;
        else if(p.kind==HomeworldKind::PressureLift || p.kind==HomeworldKind::SinkingSlab) {
            // A pressure lift holds briefly after dismount, so jumps have a
            // predictable takeoff. A sinking slab recovers after the rider leaves.
            if(riding){p.active=true;p.age=0;}
            if(!riding && p.age>90)p.active=false;
            const auto target=p.active?p.travel:0;
            next+=std::clamp(target-next,-std::abs(p.travel)/p.period,std::abs(p.travel)/p.period);
        } else if(p.kind==HomeworldKind::FlameRelay) {
            const auto d=p.center-(feet+retail::origin_above_feet);const auto horizontal=std::hypot(d.x,d.z);
            const float heading=std::abs(std::remainder(std::atan2(d.x,d.z)-g.player.yaw,6.283185307f));
            if(!p.active && input.flame_pressed && horizontal<2.7f && std::abs(d.y)<1.6f && heading<.8f) {
                const auto hit=g.level.raycast(feet+retail::origin_above_feet,d,length(d));
                if(!hit || hit->distance>length(d)-.9f) {
                    p.active=true;unsigned lit=0,total=0;
                    for(const auto& q:g.level.homeworld_pieces)if(q.kind==HomeworldKind::FlameRelay && q.objective==p.objective){++total;if(q.active)++lit;}
                    notice(g,lit==total?"THE WATCHFIRES ARE LIT - CLAIM THE ORB":std::to_string(lit)+" / "+std::to_string(total)+" WATCHFIRES LIT");
                }
            }
            tint=p.active?Vec3{1.7f,1.35f,.4f}:Vec3{.25f,.3f,.34f};
        }
        const auto delta=p.axis*(next-p.offset);const bool changed=length(delta)>.000001f || p.kind==HomeworldKind::FlameRelay;
        if(!changed)continue;
        if(riding && length(delta)>0){g.player.position+=delta;g.player.previous_position+=delta;}
        for(auto& o:g.retail_objects)if(o.type==1 && !o.collected) {
            const auto d=o.position-center;
            if(std::abs(d.x)<p.extent.x && std::abs(d.z)<p.extent.z && d.y>p.extent.y-.05f && d.y<p.extent.y+.55f){o.position+=delta;o.previous_position+=delta;}
        }
        p.offset=next;
        for(const auto& m:p.meshes) {
            for(unsigned i=0;i<m.vertices;++i){auto& v=g.level.visual_vertices[m.vertex+i];v.position=m.positions[i]+p.axis*next;v.color={std::min(1.f,m.colors[i].x*tint.x),std::min(1.f,m.colors[i].y*tint.y),std::min(1.f,m.colors[i].z*tint.z)};}
            for(unsigned i=0;i<m.triangles;++i){auto& t=g.level.triangles[m.triangle+i];t.a=m.collision[i*3]+p.axis*next;t.b=m.collision[i*3+1]+p.axis*next;t.c=m.collision[i*3+2]+p.axis*next;}
            g.level.touch_vertices(m.vertex,m.vertices);g.level.touch_triangles(m.triangle,m.triangles);
        }
        ++g.level.terrain_revision;
    }
    for(auto& t:g.level.homeworld_trials) {
        auto goal=std::find_if(g.level.custom_objectives.begin(),g.level.custom_objectives.end(),[&](const auto& v){return v.bit==t.objective;});
        if(goal==g.level.custom_objectives.end())continue;
        if(g.guide_awards.orbs&(1u<<t.objective)){t.active=false;t.complete=true;continue;}
        if(length(g.player.position-t.start)<1.8f && input.dialogue.interact) {
            t.active=true;t.remaining=t.limit;goal->progress=0;notice(g,t.name+" - FOLLOW THE GOLD MARKERS",180);
        }
        if(t.active) {
            if(t.remaining)--t.remaining;
            if(!t.remaining){t.active=false;goal->progress=0;notice(g,"TIME UP - RETURN TO THE COURIER PLATE TO RETRY",240);}
            else if(t.remaining%60==0)notice(g,t.name+"  "+std::to_string(t.remaining/60)+"s  "+std::to_string(goal->progress)+" / "+std::to_string(goal->checkpoints.size()),61);
        } else if(length(g.player.position-t.start)<1.8f && !g.custom_notice_ticks)notice(g,"PRESS E TO START THE CASTLE COURIER RUN",90);
    }
}
}
