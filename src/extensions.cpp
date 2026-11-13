#include "spyro/extensions.hpp"
#include "spyro/runtime.hpp"
#include <numbers>

namespace spyro {
unsigned UpgradeProgress::cost(Upgrade upgrade)const {
    const unsigned tier=upgrade==Upgrade::Supercharge?charge:upgrade==Upgrade::Superflame?flame:upgrade==Upgrade::Glide?glide:upgrade==Upgrade::Headbash?headbash:flight;
    if(upgrade==Upgrade::Carry)return carry?0:30;
    if(upgrade==Upgrade::Melee)return melee?0:30;
    if(upgrade==Upgrade::Roll)return roll?0:30;
    return tier>=2?0:tier?50:25;
}
unsigned element_tier_cost(Element element,unsigned tier) {
    if(tier>=3)return 0;
    static constexpr unsigned base[3]{30,50,80},convexity[3]{60,90,120};
    return element==Element::Convexity?convexity[tier]:base[tier];
}
bool ElementProgress::valid()const {
    for(const auto t:tiers)if(t>3)return false;
    return tiers[0]>=1 && static_cast<unsigned>(equipped)<element_count && tier(equipped)>0 &&
        std::isfinite(energy) && energy>=0 && energy<=100 && std::isfinite(fury) && fury>=0 && fury<=100;
}
bool UpgradeProgress::buy_element(Element element,bool milestone) {
    const auto price=element_cost(element);
    if(!enabled || !price || points<price)return false;
    if(element==Element::Convexity && !elements.owns(Element::Convexity) && !milestone)return false;
    points-=price;++elements.tiers[static_cast<unsigned>(element)];
    if(elements.tier(element)==1)elements.equipped=element;
    return true;
}
bool UpgradeProgress::valid()const{return points<=1000000 && charge<=2 && flame<=2 && flight<=2 && glide<=2 && headbash<=2 && static_cast<unsigned>(headbash_mode)<=2 && elements.valid();}
bool UpgradeProgress::buy(Upgrade upgrade) {
    const auto price=cost(upgrade);if(!enabled || !price || points<price)return false;
    points-=price;
    switch(upgrade){case Upgrade::Supercharge:++charge;break;case Upgrade::Superflame:++flame;break;
        case Upgrade::Superfly:++flight;break;case Upgrade::Carry:carry=true;break;case Upgrade::Melee:melee=true;break;case Upgrade::Glide:++glide;break;
        case Upgrade::Headbash:++headbash;headbash_mode=headbash==2?HeadbashMode::Lava:HeadbashMode::Crater;break;
        case Upgrade::Roll:roll=true;break;}
    return true;
}
namespace {
Vec3 forward(float yaw){return {std::sin(yaw),0,std::cos(yaw)};}
bool carryable(unsigned type){return is_breakable(type) || is_heavy_container(type);}
void carry_bounds(CarriedBody& body,const RetailObject& object,const ObjectModel& model) {
    Bounds bounds;const float scale=model.animated?object_model_scale(object):1;
    for(const auto& mesh:model.meshes)for(const auto vertex:mesh.vertices)bounds.add(vertex*scale);
    const auto center=bounds.center();float radius=0.15f;
    // Enclose the entire visible object, including chest corners and vase lips.
    for(const auto& mesh:model.meshes)for(const auto vertex:mesh.vertices)radius=std::max(radius,length(vertex*scale-center));
    body.local_center=center;body.radius=radius;body.top=bounds.max.y;body.bounds=bounds;
    // A pointed lid's uppermost vertices can all be one apex. Use four
    // separate footprints on the lid, not four copies of that apex.
    const auto middle=bounds.center();const float half_x=std::min(0.16f,(bounds.max.x-bounds.min.x)*0.28f);
    const float front=std::min(0.23f,(bounds.max.z-bounds.min.z)*0.28f),back=std::min(0.29f,(bounds.max.z-bounds.min.z)*0.28f);
    for(unsigned limb=0;limb<4;++limb) {
        const Vec3 wanted{middle.x+(limb%2?half_x:-half_x),bounds.max.y,middle.z+(limb<2?front:-back)};
        float best=1e30f,highest=-1e30f;body.grips[limb].point=wanted;
        for(const auto& mesh:model.meshes)for(const auto& face:mesh.faces) {
            constexpr unsigned corners[]{0,1,2,1,3,2};
            for(unsigned i=0;i<(face.quad?2u:1u);++i) {
                const Triangle triangle{mesh.vertices[face.vertices[corners[i*3]]]*scale,
                    mesh.vertices[face.vertices[corners[i*3+1]]]*scale,mesh.vertices[face.vertices[corners[i*3+2]]]*scale};
                auto normal=normalized(cross(triangle.b-triangle.a,triangle.c-triangle.a));
                if(normal.y<0)normal=normal*-1;if(normal.y<0.2f)continue;
                const float height=triangle.a.y-(normal.x*(wanted.x-triangle.a.x)+normal.z*(wanted.z-triangle.a.z))/normal.y;
                const Vec3 projected{wanted.x,height,wanted.z};
                const auto point=closest_point(projected,triangle);
                if(length(point-projected)<0.0001f && height>highest) {
                    highest=height;body.grips[limb]={projected,normal};
                } else if(highest< -1e20f) {
                    const auto nearest=closest_point(wanted,triangle);const float distance=length(nearest-wanted);
                    if(distance<best){best=distance;body.grips[limb]={nearest,normal};}
                }
            }
        }
        // Each contact is a supporting plane of the complete visible mesh,
        // including raised bands. Keeping the entire rigid paw outside this
        // plane keeps its faces, heels and claws outside the object volume.
        auto& grip=body.grips[limb];float support=-1e30f;
        for(const auto& mesh:model.meshes)for(const auto vertex:mesh.vertices)support=std::max(support,dot(vertex*scale,grip.normal));
        grip.point+=grip.normal*std::max(0.0f,support-dot(grip.point,grip.normal));
    }
}
bool visible(const Level& level,Vec3 a,Vec3 b) {
    const auto d=b-a;const auto n=length(d);return n<0.01f || !level.raycast(a,d,std::max(0.0f,n-0.08f));
}
Vec3 pickup_anchor(const RetailObject& object,const CarriedBody& body) {
    // Leave enough lift clearance for the carried body's collision sphere.
    // Aiming feet directly at the lid caused the carry collision check to
    // release a newly grabbed chest immediately against the ground.
    return object.position+object_basis(object)*Vec3{body.local_center.x,body.top,body.local_center.z}+Vec3{0,body.radius+0.08f,0};
}
void restore(Game& game,const CarriedBody& body) {
    if(body.object>=game.retail_objects.size())return;
    auto& object=game.retail_objects[body.object];object.pc_carried=false;
    if(object.collected || object.state<0)return;
    object.position=object.previous_position=body.home;object.rotation=object.previous_rotation=body.rotation;
    if(object.container){object.container->rest_position=body.home;object.container->rest_rotation=body.rotation;object.container->wobble_age=0;}
}
}
void Abilities::reset(Game& game) {
    if(carried!=UINT32_MAX)restore(game,body_);
    for(const auto& body:thrown_)restore(game,body);
    thrown_.clear();carried=UINT32_MAX;carry_ticks_=0;combo=combo_tick=0;queued_=false;hit_.clear();
    pickup_target=nearby_heavy_=UINT32_MAX;pickup_assist_ticks=0;nearby_distance_=1000;
}
std::vector<CarryCheckpoint> Abilities::checkpoint(const Game& game)const {
    std::vector<CarryCheckpoint> records;
    const auto save=[&](const CarriedBody& body,bool held) {
        if(body.object>=game.retail_objects.size())return;
        const auto& object=game.retail_objects[body.object];if(object.collected || object.state<0)return;
        records.push_back({object.id,object.type,body.age,held?carry_ticks_:0,held,object.position,body.home,body.velocity,object.rotation,body.rotation});
    };
    if(carried!=UINT32_MAX)save(body_,true);
    for(const auto& body:thrown_)save(body,false);
    return records;
}
Abilities Abilities::from_checkpoint(std::span<const CarryCheckpoint> records,std::vector<RetailObject>& objects,const ObjectModels* models) {
    Abilities result;std::set<unsigned> ids;
    if(records.size()>9 || (!records.empty() && !models))throw FormatError("Saved carry bodies need their retail model bank");
    for(const auto& saved:records) {
        const auto at=std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.id==saved.object && o.type==saved.type;});
        if(at==objects.end() || at->runtime_spawned || at->collected || at->state<0 || !carryable(at->type) || (at->source_game==GameBuild::YearOfTheDragonUS && !at->container) || !models->models.contains(at->type) ||
           saved.age>=300 || !ids.insert(saved.object).second || (saved.held && result.carried!=UINT32_MAX))throw FormatError("Saved carry body does not match an intact container");
        CarriedBody body;body.object=static_cast<unsigned>(at-objects.begin());body.home=saved.home;body.rotation=saved.home_rotation;
        body.velocity=saved.velocity;body.age=saved.age;body.mass=at->type==0xc8?1.0f:is_breakable(at->type)?2.2f:4.5f;
        carry_bounds(body,*at,models->models.at(at->type));
        at->position=at->previous_position=saved.position;at->rotation=at->previous_rotation=saved.rotation;
        at->pc_carried=true;at->attack_flags=0;at->transform_span=1;at->transform_phase=0;
        if(saved.held){result.carried=body.object;result.body_=body;result.carry_ticks_=saved.grip_ticks;}
        else result.thrown_.push_back(body);
    }
    if(result.thrown_.size()>8)throw FormatError("Too many saved airborne containers");
    return result;
}
void Abilities::release(Game& game,bool throw_it,bool drop) {
    if(carried==UINT32_MAX)return;
    if(throw_it || drop) {
        body_.velocity=throw_it?game.player.velocity*0.5f+forward(game.player.yaw)*(7/body_.mass)+Vec3{0,2,0}:game.player.velocity*0.35f;
        if(drop)body_.velocity.y=std::min(0.0f,body_.velocity.y);
        body_.age=0;thrown_.push_back(body_);if(throw_it)++throws;else ++drops;
    } else restore(game,body_);
    carried=UINT32_MAX;carry_ticks_=0;
}
void Abilities::begin_tick(Game& game,InputState& input,const ObjectModels* models) {
    if(!game.upgrades.enabled){reset(game);return;}
    if(game.player.hurt_ticks || game.dialogue.active()){release(game,false);pickup_target=UINT32_MAX;combo_tick=0;queued_=false;return;}
    nearby_heavy_=UINT32_MAX;nearby_distance_=1000;
    const bool can_pickup=game.upgrades.carry && models && game.powers.flying && carried==UINT32_MAX && thrown_.size()<8;
    if(!can_pickup)pickup_target=UINT32_MAX;
    if(can_pickup) {
        const auto origin=game.player.position+retail::origin_above_feet;
        const auto direction=retail::rotate_model({0,0,1},game.player.yaw,game.player.pitch,0);
        for(unsigned i=0;i<game.retail_objects.size();++i) {
            const auto& o=game.retail_objects[i];
            if((o.source_game==GameBuild::YearOfTheDragonUS && !o.container) || !is_heavy_container(o.type) || o.collected || o.state<0 || o.pc_carried)continue;
            const auto offset=o.position+Vec3{0,0.4f,0}-origin;const auto distance=length(offset);
            if(distance<4 && distance<nearby_distance_ && dot(normalized(offset),direction)>-0.1f &&
               visible(game.level,origin,o.position+Vec3{0,0.4f,0})) {nearby_heavy_=i;nearby_distance_=distance;}
        }
    }
    if((input.carry_pressed || input.throw_pressed) && game.upgrades.carry && models) {
        if(carried!=UINT32_MAX)release(game,input.throw_pressed,!input.throw_pressed);
        else if(pickup_target!=UINT32_MAX)pickup_target=UINT32_MAX;
        else if(input.carry_pressed && can_pickup) {
            const auto origin=game.player.position+retail::origin_above_feet;float best=5.5f;
            for(unsigned i=0;i<game.retail_objects.size();++i) {
                const auto& o=game.retail_objects[i];
                if((o.source_game==GameBuild::YearOfTheDragonUS && !o.container) || !carryable(o.type) || o.collected || o.state<0 || o.pc_carried)continue;
                const auto bank=models->models.find(o.type);if(bank==models->models.end())continue;
                CarriedBody candidate;carry_bounds(candidate,o,bank->second);
                const auto center=o.position+object_basis(o)*candidate.local_center;
                const auto distance=length(center-origin);
                if(distance<best && dot(normalized(center-origin),forward(game.player.yaw))>-0.35f && visible(game.level,origin,center)) {
                    best=distance;pickup_target=i;pickup_body_=candidate;pickup_body_.object=i;
                    pickup_body_.home=o.position;pickup_body_.rotation=o.rotation;
                    pickup_body_.mass=o.type==0xc8?1.0f:is_breakable(o.type)?2.2f:4.5f;
                }
            }
            pickup_assist_ticks=0;
        }
    }
    if(pickup_target<game.retail_objects.size()) {
        auto& o=game.retail_objects[pickup_target];
        const auto center=o.position+object_basis(o)*pickup_body_.local_center;
        const auto origin=game.player.position+retail::origin_above_feet;
        if(o.collected || o.state<0 || o.pc_carried || ++pickup_assist_ticks>120 ||
           length(center-origin)>7 || !visible(game.level,origin,center))pickup_target=UINT32_MAX;
        else {
            const auto aim=pickup_anchor(o,pickup_body_);
            const auto error=aim-game.player.position;
            // Existing close grabs stay responsive; farther grabs first fly
            // into position through Game's ordinary swept collision solver.
            const auto carry_center=game.player.position+retail::origin_above_feet+retail::rotate_model(
                {0,0.04f-retail::spyro_origin_height-(pickup_body_.top-pickup_body_.local_center.y),0},game.player.yaw,game.player.pitch,game.player.roll);
            Vec3 unused_velocity{};bool unused_grounded=false;
            const bool clear=length(game.level.resolve_sphere(carry_center,pickup_body_.radius,unused_velocity,unused_grounded)-carry_center)<=0.03f;
            if(std::hypot(error.x,error.z)<0.8f && std::abs(error.y)<1.2f && clear) {
                carried=pickup_target;body_=pickup_body_;pickup_target=UINT32_MAX;
                o.pc_carried=true;o.attack_flags=0;if(o.container)o.container->wobble_age=0;
                carry_ticks_=0;++pickups;if(pickup_assist_ticks>1)++assisted_pickups;
            }
        }
    }
    // Starting/playing an attack never depends on a target, ground contact,
    // or a held charge button. Only the hit window queries nearby objects.
    // The swipes are land gestures; they would overwrite the swim clips.
    const bool allowed=game.upgrades.melee && game.player.swim.mode==WaterMode::Dry;
    if(!allowed){combo_tick=0;queued_=false;}
    if(input.melee_pressed && allowed) {
        pickup_target=UINT32_MAX;
        if(carried!=UINT32_MAX)release(game,false,true);
        if(!combo_tick){combo=0;combo_tick=1;hit_.clear();++melee_swings;}
        else if(combo<2 && combo_tick>=7)queued_=true;
    }
    if(combo_tick) {
        input.move_x=input.move_z=0;input.flame_pressed=input.jump_pressed=input.charge=false;
        if(++combo_tick>28) {
            if(queued_ && combo<2){++combo;combo_tick=1;queued_=false;hit_.clear();++melee_swings;}
            else {combo_tick=0;queued_=false;}
        }
        // A missed attack still has its own audible exertion. Impacts retain
        // the target's break/defeat sound instead of doubling this breath.
        if(combo_tick==8)game.sound_events.push_back({SoundScope::Global,3,0,{}});
        if(combo==2 && combo_tick==22 && game.player.grounded)
            game.sound_events.push_back({SoundScope::Surface,0,0,{}});
    }
}
void Abilities::assist_flight(Game& game,const InputState& input) {
    if(!game.upgrades.enabled || !game.upgrades.carry || carried!=UINT32_MAX)return;
    auto& p=game.player;
    // This comfort assist belongs only to the optional carrying ability.
    // Charge overrides proximity braking, so passing a crate need not slow a
    // deliberate fast flight through the procedural space environment.
    if(nearby_heavy_!=UINT32_MAX && !input.charge)
        p.velocity=p.velocity*(0.72f+0.28f*std::clamp((nearby_distance_-1.5f)/2.5f,0.0f,1.0f));
    if(pickup_target>=game.retail_objects.size())return;
    const auto& o=game.retail_objects[pickup_target];
    const auto aim=pickup_anchor(o,pickup_body_);
    const auto delta=aim-p.position;const auto distance=length(delta);
    if(distance>0.001f)p.velocity=delta*(std::min(5.5f,distance*3.5f)/distance);
    p.velocity.y=std::clamp(p.velocity.y,-3.0f,3.0f);
    if(std::hypot(delta.x,delta.z)>0.15f)p.yaw+=std::clamp(std::remainder(std::atan2(delta.x,delta.z)-p.yaw,
        2*std::numbers::pi_v<float>),-0.07f,0.07f);
    p.pitch*=0.85f;p.roll*=0.85f;
}
void Abilities::tick(Game& game,const ObjectModels* models) {
    if(!game.upgrades.enabled || !models)return;
    if(carried!=UINT32_MAX) {
        auto& o=game.retail_objects[carried];
        if(!game.powers.flying || o.collected || o.state<0)release(game,false);
        else {
            const auto& player=game.player;
            const auto center=player.position+retail::origin_above_feet+retail::rotate_model(
                {0,0.04f-retail::spyro_origin_height-(body_.top-body_.local_center.y),0},player.yaw,player.pitch,player.roll);
            Vec3 velocity{};bool grounded=false;
            const auto resolved=game.level.resolve_sphere(center,body_.radius,velocity,grounded);
            if(length(resolved-center)>0.03f || !visible(game.level,o.position+object_basis(o)*body_.local_center,center))release(game,false);
            else {begin_object_transform(o,1);o.rotation={-player.roll/(16*retail::angle_to_radians),player.pitch/(16*retail::angle_to_radians),retail::original_heading(player.yaw)/16};
                o.position=center-object_basis(o)*body_.local_center;++carry_ticks_;}
        }
    }
    std::vector<std::pair<unsigned,Vec3>> impacts;
    for(auto& body:thrown_) {
        auto& object=game.retail_objects[body.object];
        if(object.collected || object.state<0){object.pc_carried=false;continue;}
        begin_object_transform(object,1);body.velocity.y-=24.0f/60;
        const auto basis=object_basis(object);const auto step=body.velocity/60;const auto distance=length(step);
        float fraction=1;bool impact=false;
        // Sweep the model bounds, including the bottom centre, to the actual
        // impact. The enclosing carry sphere would smash a box above the floor.
        for(unsigned corner=0;corner<9;++corner) {
            const Vec3 local=corner==8?Vec3{body.local_center.x,body.bounds.min.y,body.local_center.z}:
                Vec3{corner&1?body.bounds.max.x:body.bounds.min.x,corner&2?body.bounds.max.y:body.bounds.min.y,
                     corner&4?body.bounds.max.z:body.bounds.min.z};
            if(const auto hit=game.level.raycast(object.position+basis*local,step,distance)) {
                fraction=std::min(fraction,hit->distance/std::max(distance,0.000001f));impact=true;
            }
        }
        object.position+=step*fraction;const auto center=object.position+basis*body.local_center;++body.age;
        if(impact) {
            object.pc_carried=false;if(is_breakable(object.type))object.attack_flags|=0x160000;
            game.interactions.attack_sphere(game.retail_objects,*models,center,body.radius+0.7f,0x160000);
            impacts.emplace_back(body.object,body.velocity);
            body.age=600;
        } else if(body.age>=300 || object.position.y<game.level.bounds.min.y-10){restore(game,body);body.age=600;}
    }
    std::erase_if(thrown_,[&](const auto& body){return body.age>=600 || game.retail_objects[body.object].collected || game.retail_objects[body.object].state<0;});
    for(const auto& [index,velocity]:impacts)if(game.interactions.smash_carried(game.retail_objects,index,*models,game.level,
            game.player.position+retail::origin_above_feet,velocity)) {++smashes;game.camera.shake(900,8);}
    if(combo_tick>=9 && combo_tick<=(combo==2?19u:14u)) {
        const auto origin=game.player.position+retail::origin_above_feet,direction=forward(game.player.yaw);
        for(auto& object:game.retail_objects) {
            if(hit_.contains(object.id) || (!object.enemy && !is_breakable(object.type) && !object.power_target))continue;
            const auto offset=object.position+Vec3{0,0.35f,0}-origin;
            if(combo<2 && dot(normalized(offset),direction)<0.15f)continue;
            if(!visible(game.level,origin,object.position+Vec3{0,0.35f,0}))continue;
            if(container_contact(object,*models,origin+direction*(combo==2?0.1f:0.55f),combo==2?1.25f:0.7f)) {
                object.attack_flags|=combo==2?0x160000:flame_attack;hit_.insert(object.id);++melee_hits;
                game.camera.shake(combo==2?1000:650,combo==2?8:5);
            }
        }
    }
}
std::array<CarryGrip,4> Abilities::grip_contacts(const Game& game,float alpha)const {
    std::array<CarryGrip,4> result{};if(carried>=game.retail_objects.size())return result;
    const auto& object=game.retail_objects[carried];const auto position=object_position(object,alpha);const auto basis=object_basis(object,alpha);
    for(unsigned i=0;i<4;++i)result[i]={position+basis*body_.grips[i].point,basis*body_.grips[i].normal};return result;
}
void Abilities::pose(SpyroPose& pose,float alpha,const Game* game,const SpyroModel* model)const {
    if(game && carried<game->retail_objects.size() && pose.vertices.size()==230) {
        const auto& p=game->player;const auto origin=lerp(p.previous_position,p.position,alpha)+retail::origin_above_feet;
        const float yaw=p.previous_yaw+std::remainder(p.yaw-p.previous_yaw,2*std::numbers::pi_v<float>)*alpha;
        const float pitch=std::lerp(p.previous_pitch,p.pitch,alpha),roll=std::lerp(p.previous_roll,p.roll,alpha);
        const auto x=retail::rotate_model({1,0,0},yaw,pitch,roll),y=retail::rotate_model({0,1,0},yaw,pitch,roll),z=retail::rotate_model({0,0,1},yaw,pitch,roll);
        auto targets=grip_contacts(*game,alpha);for(auto& target:targets){const auto d=target.point-origin,n=target.normal;
            target={{dot(d,x),dot(d,y),dot(d,z)},{dot(n,x),dot(n,y),dot(n,z)}};}
        pose_carry_grip(pose,targets,std::clamp((static_cast<float>(carry_ticks_)-1+alpha)/10,0.0f,1.0f));
    }
    if(!combo_tick || pose.vertices.size()!=230)return;
    pose_melee(pose,combo,std::clamp((combo_tick-1+alpha)/28.0f,0.0f,1.0f),model);
}
} // namespace spyro
