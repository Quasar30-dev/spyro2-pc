void SparxCompanion::butterflies_tick(std::vector<RetailObject>& objects,const ObjectModels& models,const Level& level,retail::IVec3 origin,unsigned lives,std::vector<SoundEvent>* sounds) {
    std::vector<RetailObject> pending;
    for(auto& o:objects) {
        if(o.collected || o.state<0)continue;
        if(is_butterfly_source(o.type) && o.attack_flags && models.models.contains(0x10)) {
            // Summer Forest 80077578: a struck sheep releases class 10 and
            // its own sound 0. 8007bed8 selects an occasional extra-life fly.
            const unsigned interval=lives<4?7:lives<7?9:10;
            const bool extra=++butterfly_drops_>=interval && models.models.contains(0xd3);
            if(extra)butterfly_drops_=0;
            RetailObject food;food.type=extra?0xd3:0x10;food.runtime_spawned=true;food.source_id=o.id;
            food.held_gem=255;food.draw_flags=0x10;food.position=food.previous_position=o.position+Vec3{0,0.5f,0};
            food.animation=food.previous_animation=start_object_animation(models.models.at(food.type));
            food.butterfly=ButterflyState{food.position};pending.push_back(std::move(food));
            if(sounds)sounds->push_back({SoundScope::Object,0,o.type,o.position});
            o.collected=true;o.state=-1;o.attack_flags=0;
        }
        // The retail jar (class 233) keeps its D3 in state 2 and holds a
        // live pointer to it until an attack releases it (WAD 14 80075040).
        // It must neither wander nor enter Sparx's feeding controller yet.
        if((o.type!=0x10 && o.type!=0xd3) || o.state==2 || o.retail_script)continue;
        if(!o.butterfly)o.butterfly=ButterflyState{o.position};
        auto& b=*o.butterfly;
        if(!b.initialized){b.initialized=true;b.vertical_phase=random_.next()&0xfc;b.target_heading=unsigned(o.rotation[2])&255;}
        begin_object_transform(o,2);
        if(o.state==2){o.rotation[2]=float((unsigned(o.rotation[2])+2)&255);continue;}
        if(o.state==1 && (!food_target_ || *food_target_!=o.id))o.state=0;
        auto p=retail::from_world(o.position);
        const auto walk=[&](unsigned heading,unsigned speed) {
            const auto next=p+retail::IVec3{models.cosine[heading]*int(speed)>>12,models.sine[heading]*int(speed)>>12,0};
            const auto resolved=companion_collision(level,next,50);p=resolved;return resolved!=next;
        };
        if(o.state==1) {
            // 80071848 / 80039de4: approach the next point on an 800-unit
            // orbit. Turn the butterfly itself by at most 14 byte units.
            const auto relative=p-origin;const unsigned polar=models.math.angle(relative.x,relative.y);
            const unsigned next_polar=(polar+b.direction*3)&255;
            const retail::IVec3 goal{origin.x+(models.cosine[next_polar]*800>>12),origin.y+(models.sine[next_polar]*800>>12),p.z};
            o.rotation[2]=float(retail::approach_angle(unsigned(o.rotation[2]),models.math.angle(goal.x-p.x,goal.y-p.y),14));
            if(walk(unsigned(o.rotation[2])&255,120))food_timer_=0;
        } else {
            b.age+=2;
            if(b.age>=1800){o.collected=true;o.state=-1;continue;}
            if(retail::horizontal_distance(p-origin)<=12288) {
                b.turn_timer=b.turn_timer>2?b.turn_timer-2:0;
                if(!b.turn_timer) {
                    int turn=30+int(random_.next()%61);if(!(random_.next()&1))turn=-turn;if(random_.next()&1)turn=-turn;
                    b.target_heading=(b.target_heading+turn)&255;b.turn_timer=60+random_.next()%81;
                }
                const int error=signed_angle((b.target_heading-unsigned(o.rotation[2]))&255);
                if(std::abs(error)>2)o.rotation[2]=float(retail::approach_angle(unsigned(o.rotation[2]),b.target_heading,4));
                if(walk(unsigned(o.rotation[2])&255,30)) {
                    b.target_heading=(b.target_heading+98+random_.next()%61)&255;b.turn_timer=30+random_.next()%51;
                }
                const auto home=retail::from_world(b.home);
                if(!(script_steps_&3) && retail::horizontal_distance(home-p)>2000) {
                    b.target_heading=models.math.angle(home.x-p.x,home.y-p.y);b.turn_timer=40;
                }
            }
        }
        if(const auto ground=level.raycast(retail::to_world(p)+Vec3{0,1500/retail::units_per_world,0},{0,-1,0},4)) {
            const int floor=int(std::lround(ground->point.y*retail::units_per_world));
            const int desired=floor+640+(models.sine[b.vertical_phase]*int(b.amplitude)>>12);
            if(b.vertical_phase==0 || b.vertical_phase==128)b.amplitude=(256+random_.next()%257)&255;
            b.vertical_phase=(b.vertical_phase-4)&255;p.z+=std::clamp(desired-p.z,-32,32);
        }
        o.position=retail::to_world(p);
        if(food_target_ && *food_target_!=o.id && retail::horizontal_distance(p-origin)<3000 && std::abs(p.z-origin.z)<2800)
            food_timer_=std::min(food_timer_,30u);
    }
    if(!pending.empty()) {
        std::uint32_t id=0;for(const auto& o:objects){if(o.id==UINT32_MAX)throw FormatError("No free butterfly object ID");id=std::max(id,o.id+1);}
        if(pending.size()>static_cast<std::uint64_t>(UINT32_MAX)-id+1)throw FormatError("No free butterfly object ID");
        for(auto& o:pending){o.id=id++;objects.push_back(std::move(o));}
    }
}
bool SparxCompanion::feeding_tick(std::span<RetailObject> objects,const ObjectModels& models,const Level& level,
    RetailObject& sparx,Vec3 origin,float yaw,unsigned health,std::vector<SoundEvent>* sounds) {
    const auto clear_line=[&](Vec3 a,Vec3 b) {const auto ray=b-a;const auto hit=level.raycast(a,ray,length(ray));return !hit || hit->distance>=length(ray)-0.01f;};
    const auto play=[&](unsigned clip) {
        auto& a=sparx.animation;if(a.next_clip==clip)return;
        a.clip=a.next_clip;a.frame=a.next_frame;a.next_clip=static_cast<std::uint8_t>(clip);a.next_frame=0;a.blend=0x72;a.flags=0;
    };
    auto food=objects.end();
    if(food_target_)food=std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.id==*food_target_;});
    const auto cancel=[&] {
        if(food!=objects.end() && !food->collected && food->state==1 && !food->retail_script)food->state=0;
        food_stage_=food_timer_=0;food_target_.reset();sparx.variant=0;play(0);
    };
    const auto approach=[&](bool eating) {
        const auto angle=static_cast<unsigned>(food->rotation[2])&255;
        const unsigned shift=eating?6:4;
        const auto target=retail::from_world(food->position)-retail::IVec3{models.cosine[angle]>>shift,models.sine[angle]>>shift,0};
        const auto position=retail::from_world(sparx.position),delta=target-position;const int distance=models.math.length(delta);
        if(!clear_line(sparx.position,retail::to_world(target)))return -1;
        begin_object_transform(sparx,2);
        const int speed=eating?150:130;
        const auto step=distance?retail::rescale(delta,distance,distance>speed+5?speed+5:speed-5):retail::IVec3{};
        sparx.position=retail::to_world(position+step);
        if(distance) {
            sparx.rotation[2]=static_cast<float>(retail::approach_angle(static_cast<unsigned>(sparx.rotation[2]),models.math.angle(step.x,step.y),10));
            sparx.rotation[1]=static_cast<float>(std::clamp(signed_angle(models.math.angle(models.math.length(step,false),step.z)),-48,48)&255);
        }
        return distance;
    };
    if(!health && food_stage_>=2)cancel();
    if(food_stage_==2) {
        if(food==objects.end() || food->collected || food->state<0 || food->state==2 || food->retail_script || approach(true)<0){cancel();return false;}
        // The retail eat animation's frame 5 plays Sparx sound 1 through the
        // shared metadata path. Consume at next-frame 7 (8007259c).
        if(food!=objects.end() && !food->collected && sparx.animation.next_clip==1 && sparx.animation.next_frame>=7) {
            pending_healing_+=food->type==0xd3?models.sparx_max_health:1;
            if(food->type==0xd3)++pending_lives_;
            food->state=-1;food->collected=true;++eaten_;food_stage_=3;
            sparx.material_color=models.sparx_colors[std::min(health+pending_healing_,models.sparx_max_health)];
        }
    }
    if(food_stage_>=2) {
        const bool finished=(sparx.animation.flags&2) || (sparx.previous_animation.clip==1 && (sparx.previous_animation.flags&2));
        if(finished && sparx.animation.clip==1 && sparx.animation.next_clip==1) {
            play(0);food_stage_=0;food_target_.reset();sparx.variant=0;
        }
        return true;
    }
    if(food==objects.end() || food->collected || food->state<0 || food->state==2 || food->retail_script) {
        cancel();
        if(target_)return false;
        food=objects.end();
        // 800713d4: 3000 horizontal / 2800 vertical units; line of sight
        // also prevents picking up health through walls in the native engine.
        for(auto it=objects.begin();it!=objects.end();++it)if((it->type==0x10 || it->type==0xd3) && !it->collected && it->state>=0 && it->state!=2 && !it->retail_script) {
            const auto d=retail::from_world(it->position-origin);
            if(retail::horizontal_distance(d)>=3000 || std::abs(d.z)>=2800 || !clear_line(health?sparx.position:origin,it->position))continue;
            food=it;food_target_=it->id;food_stage_=1;food_timer_=80+random_.next()%51;target_.reset();it->state=1;
            if(!it->butterfly)it->butterfly=ButterflyState{it->position};
            const auto angle=models.math.angle(d.x,d.y);
            it->butterfly->direction=signed_angle((angle-byte_heading(yaw))&255)<0?1:-1;
            it->butterfly->vertical_phase=0;break;
        }
    }
    if(food==objects.end())return false;
    if(!health) {
        // 80071434: without Sparx, a nearby butterfly recreates him at its
        // position and plays sound 0 immediately.
        sparx.position=sparx.previous_position=food->position;sparx.state=0;
        pending_healing_+=food->type==0xd3?models.sparx_max_health:1;
        if(food->type==0xd3)++pending_lives_;
        sparx.material_color=models.sparx_colors[std::min(pending_healing_,4u)];
        if(sounds)sounds->push_back({SoundScope::Object,0,0x78,sparx.position});
        food->collected=true;food->state=-1;++eaten_;++feeding_starts_;food_stage_=0;food_target_.reset();return true;
    }
    if(models.models.at(0x78).animations.size()<2){cancel();return false;}
    const int distance=approach(false);
    if(distance<0){cancel();return false;}
    food_timer_=food_timer_>2?food_timer_-2:0;
    if(length(food->position-origin)>3)food_timer_=0;
    if(!food_timer_) {
        play(1);food_stage_=2;sparx.variant=1;sparx.rotation[1]=0;++feeding_starts_;
        if(sounds)sounds->push_back({SoundScope::Object,0,0x78,sparx.position});
    }
    return true;
}
