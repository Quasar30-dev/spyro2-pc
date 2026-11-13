#include "spyro/companion.hpp"
#include <algorithm>
#include <limits>

namespace spyro {
namespace {
unsigned byte_heading(float yaw) {return static_cast<unsigned>(std::lround(retail::original_heading(yaw)/16))&255;}
int signed_angle(unsigned angle) {return angle>128?static_cast<int>(angle)-256:static_cast<int>(angle);}
bool pickup_height(retail::IVec3 delta) {return delta.z>=-383 && delta.z<512;}
bool available_gem(const RetailObject& gem) {return gem.type==1 && !gem.collected && gem.gem_value &&
    (gem.state==1 || (gem.state==2 && gem.bounces<3));}
}
void SparxCompanion::reset(std::vector<RetailObject>& objects,const ObjectModels* models,Vec3 origin,float yaw,unsigned health) {
    for(auto& o:objects)if(food_target_ && o.id==*food_target_ && o.state==1 && !o.retail_script)o.state=0;
    if(bubble_ && *bubble_<objects.size() && objects[*bubble_].type==0xaa)objects[*bubble_].state=-1;
    food_target_.reset();food_stage_=pending_healing_=pending_lives_=food_timer_=0;
    target_.reset();timer_=0;offset_={};phase_=0;
    idle_state_=0;idle_delay_=120;turn_rate_=4;follow_flags_=5;hint_.reset();was_underwater_=false;
    sparks_={};next_spark_=0;
    if(!models || !models->models.contains(0x78)) {index_.reset();return;}
    const auto& bank=models->models.at(0x78);
    if(!bank.animated || bank.animations.empty())throw FormatError("Sparx requires his retail animated model bank");
    RetailObject object;object.type=0x78;object.runtime_spawned=true;object.draw_flags=0x10;
    object.state=health?0:-1;
    object.material_color=models->sparx_colors[std::min(health,4u)];
    // 800375c8's dynamic object defaults precede 80041438 (StartMoby).
    object.animation=start_object_animation(bank);
    if(bank.animations[0].frames.size()>1)object.animation.next_frame=1;
    object.previous_animation=object.animation;
    const auto angle=(byte_heading(yaw)-128)&255;
    const auto start=retail::from_world(origin)+retail::IVec3{models->cosine[angle]*125>>9,models->sine[angle]*125>>9,1000};
    object.position=object.previous_position=retail::to_world(start);
    object.interpolate_position=true;object.transform_span=2;
    // The spawn routine uses this angle for position only; its zeroed yaw stays zero.
    if(index_ && *index_<objects.size() && objects[*index_].runtime_spawned && objects[*index_].type==0x78) {
        object.id=objects[*index_].id;objects[*index_]=object;
    }
    else {
        for(const auto& existing:objects) {
            if(existing.id==std::numeric_limits<std::uint32_t>::max())throw FormatError("No free runtime object ID");
            object.id=std::max(object.id,existing.id+1);
        }
        index_=objects.size();objects.push_back(object);
    }
}
void SparxCompanion::fly_gems(std::span<RetailObject> objects,const ObjectModels& models,retail::IVec3 spyro) {
    for(auto& gem:objects) {
        if(gem.type!=1 || gem.state!=3 || gem.collected || !gem.gem_value)continue;
        // 800707f8 computes horizontal distance before the state handler moves it.
        const int horizontal=retail::horizontal_distance(spyro-retail::from_world(gem.position));
        begin_object_transform(gem,2);gem.gem_age+=2;
        retail::IVec3 position;
        if(gem.gem_age>=30)position=spyro;
        else {
            const auto start=retail::from_world(gem.gem_flight_origin),step=(spyro-start)/30;
            if(models.math.length(step)>480) {position=spyro;gem.gem_age=30;}
            else {
                const auto age=static_cast<int>(gem.gem_age);
                position=start+retail::IVec3{step.x*age,step.y*age,step.z*age};
                // 80070f68: four byte-angle units per VBlank, signed division by ten.
                position.z+=models.sine[(gem.gem_age*4)&255]/10;
            }
        }
        gem.position=retail::to_world(position);
        for(unsigned axis=0;axis<3;++axis)gem.rotation[axis]=static_cast<float>((static_cast<int>(gem.rotation[axis])+gem.gem_spin[axis])&255);
        if((gem.gem_age>=30 && horizontal<512 && pickup_height(spyro-position)) || gem.gem_age>=65) {
            gem.collected=true;pending_value_+=gem.gem_value;++delivered_;
        }
    }
}
void SparxCompanion::tick(std::vector<RetailObject>& objects,const ObjectModels* models,const Level& level,Vec3 origin,
                          float yaw,Vec3 velocity,bool charging,bool falling,unsigned health,std::vector<SoundEvent>* sounds,unsigned lives,const CompanionContext& context) {
    if(!models)return;
    // The original controller enforces >=2 VBlanks (8001271c); the dispatcher
    // runs once per game frame. Retain its normal 30 Hz step, then interpolate.
    const bool script_tick=(phase_++&1)==0;
    for(auto& object:objects)if(((object.type==0x78 || object.type==0xaa) && object.runtime_spawned) || (object.type==1 && object.state==3) || object.butterfly)
        object.transform_phase=std::min(object.transform_phase+1,object.transform_span);
    const auto spyro=retail::from_world(origin);
    if(script_tick) {++script_steps_;fly_gems(objects,*models,spyro);butterflies_tick(objects,*models,level,spyro,lives,sounds);}
    sparkle_tick(index_ && *index_<objects.size()?&objects[*index_]:nullptr,*models,health,script_tick);
    if(!index_ || *index_>=objects.size())return;
    auto& sparx=objects[*index_];sparx.material_color=models->sparx_colors[std::min(health,4u)];
    if(script_tick && feeding_tick(objects,*models,level,sparx,origin,yaw,health,sounds)) {hint_.reset();bubble_tick(objects,*models,health,context);return;}
    if(!health) {sparx.state=-1;target_.reset();hint_.reset();if(script_tick)bubble_tick(objects,*models,health,context);return;}
    sparx.state=0;
    if(!script_tick)return;
    if(was_underwater_!=context.underwater){timer_=0;idle_state_=0;follow_flags_=5;was_underwater_=context.underwater;}
    begin_object_transform(sparx,2);
    auto position=retail::from_world(sparx.position);
    auto target=objects.end();
    if(target_)target=std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.id==*target_ && available_gem(o);});
    if(target==objects.end())target_.reset();
    if(!target_) {
        // 80071078..800711b4: record order, original range tests and Sparx-to-gem LOS.
        for(auto it=objects.begin();it!=objects.end();++it) {
            if(!available_gem(*it))continue;
            auto delta=spyro-retail::from_world(it->position);delta.z-=retail::spyro_origin_units;
            if(retail::horizontal_distance(delta)>=static_cast<int>(models->sparx_pickup_radius) ||
               std::abs(delta.z)>=static_cast<int>(models->sparx_pickup_height)+(falling?512:0))continue;
            const auto destination=it->position+Vec3{0,100/retail::units_per_world,0},line=destination-sparx.position;
            if(const auto hit=level.raycast(sparx.position,line,length(line));hit && hit->distance<length(line)-0.001f)continue;
            target=it;target_=it->id;timer_=0;break;
        }
    }
    if(target!=objects.end()) {
        hint_.reset();
        sparx.variant=4;
        const auto delta=retail::from_world(target->position)-position;
        const int distance=models->math.length(delta);
        if(distance<512) {
            // 8007285c: start flight; the gem's own state 3 awards it later.
            target->gem_age=0;target->gem_flight_origin=target->position;
            for(auto& spin:target->gem_spin)spin=static_cast<int>(random_.next()&14)-7;
            target->state=3;begin_object_transform(*target,2);++attracted_;target_.reset();
            // 800728cc: gem sound slot 1 starts attraction, slot 0 is the
            // separate delivery sound already emitted by the runtime.
            if(sounds)sounds->push_back({SoundScope::Object,1,1,target->position});
        } else {
            const auto step=retail::rescale(delta,distance,distance>310?310:290);
            position=position+step;
            sparx.rotation[2]=static_cast<float>(retail::approach_angle(static_cast<unsigned>(sparx.rotation[2]),models->math.angle(step.x,step.y),10));
            sparx.rotation[1]=static_cast<float>(std::clamp(signed_angle(models->math.angle(models->math.length(step,false),step.z)),-48,48)&255);
        }
        sparx.position=retail::to_world(position);
    } else {
        sparx.variant=0;
        if(!hint_tick(sparx,objects,*models,level,spyro,context))follow_tick(sparx,*models,level,spyro,yaw,velocity,charging,context);
    }
    bubble_tick(objects,*models,health,context);
}
#include "companion_follow.inl"
#include "companion_feeding.inl"
void SparxCompanion::award_health(unsigned& health,unsigned& lives,unsigned maximum) {
    if(pending_healing_)health=std::min(maximum,health+pending_healing_);
    if(pending_lives_)lives=std::min(99u,lives+pending_lives_);
    pending_healing_=pending_lives_=0;
}
void SparxCompanion::sparkle_tick(const RetailObject* sparx,const ObjectModels& models,unsigned health,bool emitting) {
    if(sparx && health>=2 && emitting) {
        // Overlay 14, 80072148..8007220c: two particles at either side,
        // 100 original units from the pre-move companion position.
        const unsigned angle=(static_cast<unsigned>(sparx->rotation[2])+64)&255;
        const retail::IVec3 side{models.cosine[angle]*25>>10,models.sine[angle]*25>>10,0};
        const auto center=retail::from_world(sparx->position);
        constexpr Vec3 colors[]={{1,0,0},{1,1,0},{0,1,0},{0,1,1},{0,0,1},{1,0,1},{1,128/255.0f,0},{0,128/255.0f,1}};
        for(int sign:{1,-1})for(unsigned i=0;i<2;++i) {
            auto& p=sparks_[next_spark_++%sparks_.size()];p={};p.active=true;p.age=effect_random_.next()&15;
            const auto at=center+retail::IVec3{side.x*sign,side.y*sign,0};
            p.position={(at.x>>2)+int(effect_random_.next()&15)-8,(at.y>>2)+int(effect_random_.next()&15)-8,(at.z>>2)+int(effect_random_.next()&15)-8};
            p.previous=p.position;p.color=health>2?Vec3{1,1,0}:colors[effect_random_.next()&7];++emitted_;
        }
    }
    // 8007d8b0: quarter-resolution random walk, advanced per VBlank.
    for(auto& p:sparks_)if(p.active) {
        p.previous=p.position;
        if(p.age&1) {
            p.position=p.position+p.velocity;
            if(p.age&2) {p.velocity.x+=int(effect_random_.next()&2)-1;p.velocity.y+=int(effect_random_.next()&2)-1;p.velocity.z+=int(effect_random_.next()&2)-1;}
        }
        if(++p.age>=24)p.active=false;
    }
}
void SparxCompanion::reset_interpolation(){for(auto& p:sparks_)p.previous=p.position;}
std::vector<SparxCompanion::SparkPose> SparxCompanion::sparkles(float alpha)const {
    std::vector<SparkPose> out;alpha=std::clamp(alpha,0.0f,1.0f);
    for(const auto& p:sparks_)if(p.active)out.push_back({lerp(retail::to_world(p.previous),retail::to_world(p.position),alpha)*4,p.color});
    return out;
}
} // namespace spyro
