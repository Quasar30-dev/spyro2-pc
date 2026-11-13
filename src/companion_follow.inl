// Summer Forest 80072978..80073378. Script quantities use original XYZ-up
// coordinates and byte angles; render transforms remain interpolated at 60 Hz.
namespace {
retail::IVec3 companion_collision(const Level& level,retail::IVec3 p,unsigned radius=256) {
    Vec3 velocity{};bool grounded=false;
    return retail::from_world(level.resolve_sphere(retail::to_world(p),radius/retail::units_per_world,velocity,grounded));
}
retail::IVec3 companion_local(retail::IVec3 p,float yaw,const CompanionContext& context,const ObjectModels& models) {
    if(context.pitch==0 && context.roll==0) {
        const auto angle=byte_heading(yaw);const int c=models.cosine[angle],s=models.sine[angle];
        return {(p.x*c-p.y*s)>>12,(p.x*s+p.y*c)>>12,p.z};
    }
    return retail::from_world(retail::rotate_model(retail::model_position({float(p.x),float(p.y),float(p.z)}),yaw,context.pitch,context.roll));
}
}
void SparxCompanion::follow_tick(RetailObject& sparx,const ObjectModels& models,const Level& level,retail::IVec3 spyro,
    float yaw,Vec3 velocity,bool charging,const CompanionContext& context) {
    if(context.idle)idle_delay_=idle_delay_>2?idle_delay_-2:0;else idle_delay_=120;
    const bool idle=context.idle && !idle_delay_;
    if(timer_<=0) {
        if(idle_state_==2){turn_rate_=12;idle_state_=idle && random_.next()<0x3fff?4:1;}
        else if(idle_state_==4){idle_state_=5;turn_rate_=16;}
        else if(idle_state_==5){idle_state_=0;turn_rate_=16;}
        else if(idle && random_.next()<0x2aaa) {
            const unsigned choice=random_.next()%10;
            idle_state_=choice<4?3:choice<7?2:choice<9?6:4;
            turn_rate_=idle_state_==2?12:idle_state_==6?20:16;
        } else {turn_rate_=idle_state_==3?16:4;idle_state_=0;}
        switch(idle_state_) {
        case 0:case 1:
            offset_={-600,int(random_.next()&0x310)-392,0};
            offset_.z=context.camera_mode==2?int(random_.next()&127)+100:int(random_.next()&511)-200;
            if(context.underwater){offset_.x=-200;offset_.z=int(random_.next()&127)+250;}
            if(!idle_state_){timer_=60+int(random_.next()&63);follow_flags_=5;}else timer_=120;
            break;
        case 2:offset_={500,0,300};timer_=120;break;
        case 3:timer_=60+int(random_.next()&31);break;
        case 4:case 5: {
            const int side=int(random_.next()%4)*50+100;
            const bool positive=idle_state_==4?random_.next()<0x3fff:offset_.y<0;
            offset_={700,positive?side:-side,int(random_.next()%4)*50};timer_=60;follow_flags_=10;break;
        }
        case 6:timer_=120;break;
        }
        return;
    }
    timer_-=2;if(idle_state_ && !idle)timer_=0;
    const auto old=retail::from_world(sparx.position);auto p=old;
    const auto face=[&](unsigned target){sparx.rotation[2]=float(retail::approach_angle(unsigned(sparx.rotation[2]),target,turn_rate_));};
    if(idle_state_==0 || idle_state_==4 || idle_state_==5) {
        retail::IVec3 destination;
        if(follow_flags_&1) {
            auto offset=offset_;
            if(charging || context.camera_mode==2) {
                offset.x+=int(std::lround(std::hypot(velocity.x,velocity.z)*64*retail::units_per_world/60))>>2;
                offset.z=std::max(offset.z,100);
            }else if(context.camera_mode==7)offset.x-=512;
            destination=spyro+companion_local(offset,yaw,context,models);
        } else {
            const auto forward=normalized(context.camera_forward),right=normalized(cross(Vec3{0,1,0},forward)),up=cross(forward,right);
            destination=retail::from_world(context.camera_eye+(forward*float(offset_.x)-right*float(offset_.y)+up*float(offset_.z))/retail::units_per_world);
        }
        const auto step=(destination-p)/4;p=companion_collision(level,p+step);
        sparx.rotation[0]=0;sparx.rotation[1]=float(std::clamp(step.z,-32,32)&255);
        if(follow_flags_&4)face(byte_heading(yaw));
        if(follow_flags_&8)face((models.math.angle(int(context.camera_forward.x*4096),int(context.camera_forward.z*4096))+128)&255);
    } else if(idle_state_==1 || idle_state_==2) {
        const auto current=p-spyro,destination=companion_local(offset_,yaw,context,models);
        int radius=models.math.length(current,false);radius+=(models.math.length(destination,false)-radius)>>3;
        const unsigned angle=(models.math.angle(current.x,current.y)+(signed_angle((models.math.angle(destination.x,destination.y)-models.math.angle(current.x,current.y))&255)>>3))&255;
        const int dz=(spyro.z+destination.z-p.z)>>3;
        p=companion_collision(level,{spyro.x+(models.cosine[angle]*radius>>12),spyro.y+(models.sine[angle]*radius>>12),p.z+dz});
        sparx.rotation[0]=0;sparx.rotation[1]=float(std::clamp(dz,-32,32)&255);
        face(idle_state_==1 && timer_<=100?byte_heading(yaw):(angle+128)&255);
    } else if(idle_state_==3) {
        const auto camera=retail::from_world(context.camera_eye)-p;face(models.math.angle(camera.x,camera.y));
    } else if(idle_state_==6) {
        // 80039de4 with radius zero: turn towards the center while moving,
        // making a small curved loop, rather than snapping onto an orbit.
        const auto toward=spyro-p;face(models.math.angle(toward.x,toward.y));
        const unsigned heading=unsigned(sparx.rotation[2])&255;
        p=p+retail::IVec3{models.cosine[heading]*160>>12,models.sine[heading]*160>>12,(spyro.z+300-p.z)>>2};
        const auto resolved=companion_collision(level,p);if(resolved!=p)timer_=0;p=resolved;
    }
    sparx.position=retail::to_world(p);
}
bool SparxCompanion::hint_tick(RetailObject& sparx,std::span<const RetailObject> objects,const ObjectModels& models,
    const Level& level,retail::IVec3 spyro,const CompanionContext& context) {
    const bool had_hint=hint_.has_value();hint_.reset();
    if(!context.gem_hint){if(had_hint)timer_=0;return false;}
    std::int64_t best=INT64_MAX;retail::IVec3 delta;
    for(const auto& o:objects) {
        if(o.collected || o.state<0 || (o.type!=1 && o.held_gem!=1 && o.held_gem!=2 && o.held_gem!=5 && o.held_gem!=10 && o.held_gem!=25))continue;
        const auto d=retail::from_world(o.position)-spyro;
        std::int64_t distance=std::abs(std::int64_t(d.x))+std::abs(std::int64_t(d.y))+std::abs(std::int64_t(d.z));
        if(distance<16384)distance=models.math.length(d);
        if(distance<best){best=distance;delta=d;hint_=o.id;}
    }
    if(!hint_)return false;
    if(best>1024) {
        while(std::abs(std::int64_t(delta.x))+std::abs(std::int64_t(delta.y))+std::abs(std::int64_t(delta.z))>16384)delta=delta/2;
        delta=retail::rescale(delta,models.math.length(delta),1024+(models.sine[(phase_*8)&255]>>5));
    }
    delta.z=std::max(-256,delta.z);
    auto p=retail::from_world(sparx.position);
    const auto facing=p-spyro;
    sparx.rotation[0]=0;sparx.rotation[1]=float(models.math.angle(models.math.length(delta,false),delta.z));
    sparx.rotation[2]=float(models.math.angle(facing.x,facing.y));
    p=companion_collision(level,p+(spyro+delta-p)/4);sparx.position=retail::to_world(p);
    return true;
}
void SparxCompanion::bubble_tick(std::vector<RetailObject>& objects,const ObjectModels& models,unsigned health,const CompanionContext& context) {
    const bool valid=bubble_ && *bubble_<objects.size() && objects[*bubble_].type==0xaa && objects[*bubble_].runtime_spawned;
    if(!valid)bubble_.reset();
    if(!health || !context.underwater || !index_ || *index_>=objects.size()) {
        if(valid)objects[*bubble_].state=-1;
        return;
    }
    const auto bank=models.models.find(0xaa);if(bank==models.models.end())return;
    if(!bubble_ || objects[*bubble_].state<0) {
        RetailObject bubble;bubble.type=0xaa;bubble.runtime_spawned=true;bubble.draw_flags=0x28;bubble.held_gem=255;bubble.size=127;
        bubble.source_id=objects[*index_].id;bubble.position=bubble.previous_position=objects[*index_].position;
        if(bank->second.animated)bubble.animation=bubble.previous_animation=start_object_animation(bank->second);
        if(bubble_){bubble.id=objects[*bubble_].id;objects[*bubble_]=bubble;}
        else {
            for(const auto& o:objects){if(o.id==UINT32_MAX)throw FormatError("No free companion bubble ID");bubble.id=std::max(bubble.id,o.id+1);}
            bubble_=objects.size();objects.push_back(bubble);
        }
    }
    auto& bubble=objects[*bubble_];begin_object_transform(bubble,2);
    bubble.size=std::uint8_t(std::max(26,int(bubble.size)-8));
    bubble.position=objects[*index_].position+Vec3{0,28/retail::units_per_world,0};
    bubble.rotation[0]=0;
    bubble.rotation[2]=float((models.math.angle(int(context.camera_forward.x*4096),int(context.camera_forward.z*4096))+128)&255);
    bubble.rotation[1]=float(models.math.angle(int(std::hypot(context.camera_forward.x,context.camera_forward.z)*4096),int(-context.camera_forward.y*4096)));
}
