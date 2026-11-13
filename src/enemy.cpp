#include "spyro/enemy.hpp"
#include "spyro/object_interaction.hpp"
#include <bit>

namespace spyro {
namespace {
constexpr float unit=1/retail::units_per_world;
float horizontal(Vec3 v){return std::hypot(v.x,v.z);}
bool visible(const Level& level,Vec3 from,Vec3 to) {
    const auto delta=to-from;const auto distance=length(delta);
    if(distance<0.001f)return true;
    const auto wall=level.raycast(from,delta,distance);
    return !wall || wall->distance>=distance-0.03f;
}
float face(RetailObject& object,Vec3 target,float rate) {
    const auto delta=target-object.position;
    const auto wanted=retail::original_heading(std::atan2(delta.x,delta.z))/16;
    const auto turn=std::remainder(wanted-object.rotation[2],256.0f);
    object.rotation[2]=std::fmod(object.rotation[2]+std::clamp(turn,-rate,rate)+256,256.0f);
    return std::abs(turn);
}
void request(RetailObject& object,EnemyPhase phase,unsigned clip) {
    auto& e=*object.enemy;e.phase=phase;e.age=0;e.animation_finished=false;e.hit_sent=false;
    auto& a=object.animation;
    if(a.next_clip==clip)return;
    // Overlay transitions preserve the next endpoint, then blend to frame 0.
    a.clip=a.next_clip;a.frame=a.next_frame;a.next_clip=static_cast<std::uint8_t>(clip);
    a.next_frame=0;a.blend=0x72;a.flags=0;
}
unsigned phase_time(const ObjectModel& model,const ObjectAnimationState& a) {
    // 800416a0: cumulative half-VBlank time in metadata bits 0..8.
    const bool adjacent=a.clip==a.next_clip && a.frame+1==a.next_frame;
    const auto clip=a.clip==a.next_clip?a.clip:a.next_clip;
    const auto frame=a.clip==a.next_clip?a.frame:a.next_frame;
    if(clip>=model.animations.size() || frame>=model.animations[clip].frames.size())return 0;
    return (model.animations[clip].frames[frame].metadata&511)+(adjacent?(a.blend&15)/2:0);
}
bool walk(const Level& level,RetailObject& object,Vec3 destination,float speed=135,float turn=6,float tolerance=32) {
    const auto delta=destination-object.position;
    if(horizontal(delta)<0.001f)return true;
    // 80037fcc / 80038290: 0x87 original units per two VBlanks.
    const auto angle=face(object,destination,turn);
    if(angle>tolerance)return true;
    const auto yaw=retail::native_heading(object.rotation[2]*16);
    const auto move=Vec3{std::sin(yaw),0,std::cos(yaw)}*std::min(speed*unit,horizontal(delta));
    auto next=object.position+move;
    if(!visible(level,object.position+Vec3{0,0.45f,0},next+Vec3{0,0.45f,0}))return false;
    // Native floor following is conservative at ledges. Authored jumps and
    // linked challenge scripts remain separate from this ground traversal.
    const auto floor=level.raycast(next+Vec3{0,0.7f,0},{0,-1,0},1.7f);
    if(!floor || floor->normal.y<0.55f)return false;
    next.y=floor->point.y;object.position=next;return true;
}
void flee(const Level& level,RetailObject& object,Vec3 player) {
    auto& e=*object.enemy;
    if(e.route.empty() || e.target>=e.route.size()){request(object,EnemyPhase::Taunt,4);return;}
    if(horizontal(e.route[e.target].position-object.position)<155*unit) {
        const auto target=static_cast<int>(e.target)+e.direction;
        if(target<0 || target>=static_cast<int>(e.route.size())) {
            if(e.loop)e.target=target<0?static_cast<unsigned>(e.route.size()-1):0;
            else {request(object,EnemyPhase::Taunt,4);return;}
        } else e.target=static_cast<unsigned>(target);
    }
    if(!walk(level,object,e.route[e.target].position))request(object,EnemyPhase::Taunt,4);
    else if(e.loop && horizontal(player-object.position)>e.notice*1.25f)request(object,EnemyPhase::Idle,1);
}
RetailObject* linked_enemy(std::span<RetailObject> objects,const EnemyState& e,EnemyKind kind) {
    const auto i=std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.id==e.linked;});
    return i==objects.end() || !i->enemy || i->enemy->kind!=kind || i->state<0 || i->collected?nullptr:&*i;
}
void initialize_links(std::span<RetailObject> objects) {
    for(auto& o:objects)if(o.enemy && !o.enemy->initialized) {
        auto& e=*o.enemy;e.initialized=true;
        if(e.kind!=EnemyKind::GearGrinder)continue;
        // 80076610: place the robot 2000 world units along the grinder's
        // heading, and use that placement as its eventual return position.
        if(auto* robot=linked_enemy(objects,e,EnemyKind::Robot)) {
            const auto yaw=retail::native_heading(o.rotation[2]*16);
            robot->position=robot->previous_position=o.position+Vec3{std::sin(yaw),0,std::cos(yaw)}*(2000*unit);
            robot->rotation=robot->previous_rotation=o.rotation;
            robot->enemy->home=robot->position;robot->enemy->home_heading=o.rotation[2];
        }
        if(e.patrol_initial)request(o,EnemyPhase::Patrol,9);
    }
}
bool striking_contact(const RetailObject& o,const ObjectModel& model,const ObjectModels& models,Vec3 origin) {
    // 80041538 selects the frame's collision bank at the blend midpoint.
    // 8004055c tests type-2 spheres at one bone or the mean of two bones.
    // These are separate packed vertices, not visible mesh vertex numbers.
    const auto& a=model.animations.at(o.animation.clip).frames.at(o.animation.frame);
    const auto& b=model.animations.at(o.animation.next_clip).frames.at(o.animation.next_frame);
    const auto weight=models.animation_blends.at(o.animation.blend);
    const auto selector=((weight<128?a.metadata:b.metadata)>>13)&7;
    if(!model.damage_banks[selector])return false;
    const auto& bank=*model.damage_banks[selector];constexpr float player_radius=retail::spyro_origin_height;
    if(length(origin-o.position)>=bank.broad_radius*unit+player_radius)return false;
    const auto basis=object_basis(o);
    const auto point=[&](int i){return o.position+basis*lerp(a.collision_vertices.at(i),b.collision_vertices.at(i),weight/256.0f);};
    for(const auto& sphere:bank.spheres) {
        auto center=point(sphere.first);if(sphere.second>=0)center=(center+point(sphere.second))*0.5f;
        if(length(center-origin)<sphere.radius*unit+player_radius)return true;
    }
    if(bank.cylinder) {
        const auto& c=*bank.cylinder;const auto d=origin-o.position;
        if(horizontal(d)<c.radius*unit+player_radius) {
            const float margin=player_radius*11/16;
            if(d.y+margin>=c.bottom*unit && d.y-margin<=c.top*unit)return true;
            const auto height=d.y-(d.y+margin<c.bottom*unit?c.bottom:c.top)*unit;
            if(length(Vec3{d.x,height,d.z})<c.cap_radius*unit+player_radius)return true;
        }
    }
    return false;
}
void robot_debris(std::vector<RetailObject>& pending,const RetailObject& parent,const ObjectModels& models,retail::Random& random) {
    const auto bank=models.models.find(0x58);
    if(bank==models.models.end() || bank->second.meshes.size()<5)throw FormatError("Hurricos robot debris bank is missing");
    // 80078f3c..80079210: head, body, tool, then seven leg/gear pieces.
    // Case 3 deliberately falls through to case 4 in the retail offset code.
    const Vec3 offsets[]={{11,122,-104},{-6,364,0},{18,196,604},{-4,-123,167},{-4,-123,167}};
    const auto yaw=retail::native_heading(parent.rotation[2]*16);
    const auto forward=retail::from_world(Vec3{std::sin(yaw),0,std::cos(yaw)});
    for(unsigned i=0;i<10;++i) {
        RetailObject part;part.type=0x58;part.model=static_cast<std::uint8_t>(i<3?i:3+i%2);
        part.source_id=parent.id;part.runtime_spawned=true;part.held_gem=255;part.draw_flags=0x3f;
        part.material_color=1;part.variant=1;part.rotation=part.previous_rotation=parent.rotation;
        part.position=part.previous_position=parent.position+object_basis(parent)*(offsets[part.model]*unit);
        const int bonus=100+((parent.attack_flags&0x40000)?120:0)+((parent.attack_flags&0x100000)?120:0);
        part.script_velocity={static_cast<int>(random.next()%121)-60+forward.x*bonus/1024,
            static_cast<int>(random.next()%121)-60+forward.y*bonus/1024,60+static_cast<int>(random.next()%61)};
        if(parent.attack_flags&0x100000)part.script_velocity.z+=40+random.next()%31;
        for(auto& spin:part.gem_spin)spin=static_cast<int>(random.next()%31)-15;
        part.lifetime=65+random.next()%16;part.fragment_floor=retail::from_world(parent.position).z-64;
        pending.push_back(std::move(part));
    }
}
void hurricos(std::span<RetailObject> objects,RetailObject& o,const ObjectModel& model,const ObjectModels& models,
               const Level& level,Vec3 origin,bool seen,std::optional<EnemyHit>& hit) {
    auto& e=*o.enemy;const auto distance=horizontal(origin-o.position);
    if(e.kind==EnemyKind::GearGrinder) {
        auto* robot=linked_enemy(objects,e,EnemyKind::Robot);
        const auto command=[&](unsigned delay){e.command_available=false;e.timer=delay;request(o,EnemyPhase::Command,6);};
        switch(e.phase) {
            case EnemyPhase::Idle:
                if(robot && e.command_available && e.activation>0 && distance<e.activation && visible(level,o.position+Vec3{0,0.6f,0},origin)) {
                    e.activation=0;command(e.command_delay?e.command_delay:90);
                } else if(seen && !e.cooldown)request(o,EnemyPhase::Alert,1);
                break;
            case EnemyPhase::Alert:
                face(o,origin,4);if(e.animation_finished)request(o,EnemyPhase::Ready,2);break;
            case EnemyPhase::Ready:
                if(robot && e.command_available){command(100);break;}
                if(distance>11000*unit)request(o,EnemyPhase::Settle,3);
                else if(seen && distance<e.reach && !e.cooldown && face(o,origin,4)<50)request(o,EnemyPhase::Swing,4);
                else face(o,origin,4);
                break;
            case EnemyPhase::Swing:
                face(o,origin,4);
                if(!e.hit_sent && seen && distance<e.reach && striking_contact(o,model,models,origin)) {hit=EnemyHit{o.position,o.id};e.hit_sent=true;}
                if(e.animation_finished){request(o,EnemyPhase::Ready,2);e.cooldown=180;}break;
            case EnemyPhase::Settle:
                face(o,origin,4);if(e.animation_finished)request(o,EnemyPhase::Idle,0);break;
            case EnemyPhase::Command:
                if(!robot){request(o,EnemyPhase::Alert,1);break;}
                if(face(o,robot->position,6)>6)break;
                if(robot->enemy->phase!=EnemyPhase::Command)request(*robot,EnemyPhase::Command,1);
                e.timer=e.timer>2?e.timer-2:0;
                if(!e.timer && e.animation_finished) {
                    auto& r=*robot->enemy;r.route_mode=0;r.speed=80;r.timer=r.patrol_duration;
                    request(*robot,EnemyPhase::Patrol,2);request(o,EnemyPhase::CommandFinish,7);
                }
                break;
            case EnemyPhase::CommandFinish:
                if(robot)face(o,robot->position,6);
                if(e.animation_finished)request(o,EnemyPhase::Idle,0);break;
            case EnemyPhase::Patrol:
                if(!e.route.empty()) {
                    if(horizontal(e.route[e.target].position-o.position)<256*unit)e.target=(e.target+1)%static_cast<unsigned>(e.route.size());
                    walk(level,o,e.route[e.target].position,90,6,20);
                }
                if(seen && distance<7000*unit && face(o,origin,0)<32)request(o,EnemyPhase::Alert,1);
                break;
            default:break;
        }
        return;
    }
    auto* leader=linked_enemy(objects,e,EnemyKind::GearGrinder);
    const auto free_guard=[&]{e.home=o.position;e.assigned_waypoint=-1;request(o,EnemyPhase::Ready,0);};
    const auto resume=[&](unsigned cooldown){e.cooldown=cooldown;request(o,e.assigned_waypoint<0?EnemyPhase::Return:EnemyPhase::Patrol,2);};
    switch(e.phase) {
        case EnemyPhase::Idle:case EnemyPhase::Command:
            if(!leader)free_guard();break;
        case EnemyPhase::Ready:
            face(o,origin,4);
            if(seen && !e.cooldown && std::abs(origin.y-o.position.y)<600*unit)request(o,EnemyPhase::Chase,5);
            break;
        case EnemyPhase::Patrol:
            e.speed=std::min(e.speed+20,e.maximum_speed);e.timer=e.timer>2?e.timer-2:0;
            if(e.route_mode==0 && !e.route.empty()) {
                if(horizontal(e.route[e.target].position-o.position)<512*unit) {
                    if(!e.timer && e.assigned_waypoint==static_cast<int>(e.target))e.route_mode=1;
                    e.target=(e.target+1)%static_cast<unsigned>(e.route.size());
                }
                if(e.route_mode==0)walk(level,o,e.route[e.target].position,e.speed,6,e.route.size()==2?128.0f:64.0f);
            } else if(e.route_mode==0)e.route_mode=1;
            if(e.route_mode==1) {
                if(!leader){free_guard();break;}
                if(horizontal(e.home-o.position)<(e.speed/2+10)*unit)e.route_mode=2;
                else walk(level,o,e.home,e.speed,6,64);
            }
            if(e.route_mode==2) {
                if(!leader){free_guard();break;}
                const auto yaw=retail::native_heading(e.home_heading*16);
                if(face(o,o.position+Vec3{std::sin(yaw),0,std::cos(yaw)},4)<4)request(o,EnemyPhase::Settle,3);
            }
            if(e.phase==EnemyPhase::Patrol && distance<4000*unit && !e.cooldown && std::abs(origin.y-o.position.y)<800*unit &&
               visible(level,o.position+Vec3{0,0.6f,0},origin))request(o,EnemyPhase::Chase,5);
            break;
        case EnemyPhase::Settle:
            if(e.animation_finished){if(leader)leader->enemy->command_available=true;request(o,EnemyPhase::Idle,0);}break;
        case EnemyPhase::Chase: {
            const auto anchor=e.assigned_waypoint<0 || e.route.empty()?e.home:e.route[e.target].position;
            if(horizontal(o.position-anchor)>6000*unit || !visible(level,o.position+Vec3{0,0.6f,0},origin) || !walk(level,o,origin,200,4,20))resume(120);
            else if(distance<e.reach && std::abs(origin.y-o.position.y)<600*unit)request(o,EnemyPhase::Swing,4);
            break;
        }
        case EnemyPhase::Swing:
            face(o,origin,4);
            if(!e.hit_sent && visible(level,o.position+Vec3{0,0.6f,0},origin) && striking_contact(o,model,models,origin)) {hit=EnemyHit{o.position,o.id};e.hit_sent=true;}
            if(e.animation_finished)resume(180);break;
        case EnemyPhase::Return:
            if(horizontal(e.home-o.position)<128*unit)request(o,EnemyPhase::Ready,0);
            else walk(level,o,e.home,80,4,20);break;
        default:break;
    }
}
#include "level_enemies.inl"

}
void initialize_enemy_objects(std::span<RetailObject> objects) {initialize_links(objects);}
bool enemy_accepts_attack(const RetailObject& object,std::uint32_t flags) {
    if(!object.enemy || object.enemy->defeated || object.collected || object.state<0)return false;
    // Overlay 58, 8007f5d0: submerged platypuses cannot be struck. Their
    // death mesh belongs to the surface bank, so it must not blend from 5..7.
    if(object.enemy->kind==EnemyKind::Platypus && object.animation.next_clip>=3)return false;
    if(flags&0x540000)return true; // Powered flame, supercharge and PC green bomb.
    switch(object.enemy->kind) {
        case EnemyKind::Robot:case EnemyKind::MetalIdol:return (flags&charge_attack)!=0;
        case EnemyKind::GearGrinder:return (flags&flame_attack)!=0;
        case EnemyKind::SmallLizard:return (flags&(0x30000|headbash_attack))!=0;
        case EnemyKind::ClubLizard:return (flags&(flame_attack|headbash_attack))!=0;
        case EnemyKind::BucketBlubber:case EnemyKind::RhinoSnail:case EnemyKind::Platypus:return (flags&charge_attack)!=0;
        case EnemyKind::FireBlubber:case EnemyKind::ElephantSnail:return (flags&flame_attack)!=0;
        case EnemyKind::SpittingMonkey:return (flags&(flame_attack|charge_attack))!=0;
    }
    return false;
}
const char* enemy_phase_name(const RetailObject& object) {
    if(!object.enemy)return "Unmapped";
    switch(object.enemy->phase) {
        case EnemyPhase::Idle:return "Idle";case EnemyPhase::Alert:return "Alert";
        case EnemyPhase::Flee:return "Escaping";case EnemyPhase::Taunt:return "Taunting";
        case EnemyPhase::Ready:return "Watching Spyro";case EnemyPhase::Swing:return "Attacking";
        case EnemyPhase::Recover:return "Recovering";case EnemyPhase::Settle:return "Settling";
        case EnemyPhase::Dying:return "Defeated";
        case EnemyPhase::Command:return "Robot command";case EnemyPhase::CommandFinish:return "Command finished";
        case EnemyPhase::Patrol:return "Patrolling";case EnemyPhase::Chase:return "Pursuing Spyro";case EnemyPhase::Return:return "Returning home";
    }
    return "Unknown";
}
std::optional<EnemyHit> EnemySystem::tick(std::vector<RetailObject>& objects,const ObjectModels* models,
    const Level& level,Vec3 origin,bool protected_player) {
    if(!models)return {};
    if(level.source_game==GameBuild::YearOfTheDragonUS)return yotd_tick(objects,*models,level,origin,protected_player);
    initialize_links(objects);std::vector<RetailObject> pending;
    const bool script=(ticks_++&1)==0;std::optional<EnemyHit> hit;
    for(auto& object:objects) {
        if(object.enemy_projectile && !object.collected) {
            auto& shot=*object.enemy_projectile;begin_object_transform(object,1);
            const auto from=object.position,next=from+shot.velocity/60;
            const auto ray=next-from;const auto distance=length(ray);
            const auto wall=level.raycast(from,ray,distance);
            if(!shot.ticks-- || (wall && wall->distance<=distance)){object.collected=true;object.state=-1;continue;}
            object.position=next;
            const auto t=distance>0?std::clamp(dot(origin-from,ray)/(distance*distance),0.0f,1.0f):0;
            if(!protected_player && length(origin-(from+ray*t))<0.45f){hit=EnemyHit{from,object.source_id};object.collected=true;object.state=-1;}
            continue;
        }
        if(!object.enemy || object.collected || object.state<0 || object.frozen_ticks)continue;
        auto& e=*object.enemy;
        const auto found=models->models.find(object.type);if(found==models->models.end())continue;
        const auto& model=found->second;
        object.transform_phase=std::min(object.transform_phase+1,object.transform_span);
        if((object.animation.flags&2) && object.animation.clip==object.animation.next_clip)e.animation_finished=true;
        if(enemy_accepts_attack(object,object.attack_flags)) {
            const bool charge=(object.attack_flags&(charge_attack|0x100000))!=0;
            e.defeated=true;
            if(e.kind==EnemyKind::Robot) {
                robot_debris(pending,object,*models,random_);object.state=-1;object.collected=true;object.attack_flags=0;object.attack_sourced=false;continue;
            }
            request(object,EnemyPhase::Dying,death_clip(e.kind));
            // PC elemental/demon/shockwave hits carry their own source.
            auto away=object.position-(object.attack_sourced?object.attack_source:origin);away.y=0;away=normalized(away);
            if(horizontal(away)<0.01f)away={0,0,1};
            const float speed=(e.kind==EnemyKind::ClubLizard?(charge?320.0f:170.0f):(charge?350.0f:200.0f))*unit*30;
            e.velocity=away*speed;
            if(e.kind==EnemyKind::GearGrinder || e.kind==EnemyKind::MetalIdol) {
                const bool idol=e.kind==EnemyKind::MetalIdol;
                float launch=idol?250.0f:((object.attack_flags&0x40000)?500.0f:150.0f);
                if(object.attack_flags&0x100000)launch+=idol?50:100;
                e.velocity=away*(launch*unit*30);e.velocity.y=(idol?150:80)*unit*30;
            }
        }
        if(!e.defeated && (e.kind==EnemyKind::Robot || e.kind==EnemyKind::MetalIdol) && script) {
            if(e.heat || (object.attack_flags&flame_attack)) {
                e.heat=(object.attack_flags&flame_attack)?std::min(e.heat+16,63u):e.heat-(e.heat>33?2:1);
                object.material_color=0x30000080+(e.heat>>2)*0x1000000;
            }
        }
        // Stunned (PC Electricity): AI paused; hits above were still accepted.
        if(object.stun_ticks && !e.defeated){object.attack_flags=0;object.attack_sourced=false;continue;}
        if(!script && (e.kind==EnemyKind::Robot || e.kind==EnemyKind::MetalIdol))continue;
        object.attack_flags=0;object.attack_sourced=false;
        if(!script)continue;
        begin_object_transform(object,2);e.age+=2;if(e.cooldown)e.cooldown=e.cooldown>2?e.cooldown-2:0;
        if(e.defeated) {
            // Keep the retail death clip visible; persistence is committed at
            // the hit so saving during its animation cannot resurrect it.
            const auto displacement=e.velocity/30;auto next=object.position+displacement;
            if(visible(level,object.position+Vec3{0,0.4f,0},next+Vec3{0,0.4f,0})) {
                if(const auto floor=level.raycast(next+Vec3{0,0.6f,0},{0,-1,0},1.6f);
                   floor && (e.kind==EnemyKind::SmallLizard || e.kind==EnemyKind::ClubLizard || next.y<floor->point.y))next.y=floor->point.y;
                object.position=next;
            }
            if(e.kind==EnemyKind::SmallLizard || e.kind==EnemyKind::ClubLizard)e.velocity=e.velocity*0.86f;
            else e.velocity.y-=10*unit*30;
            if(e.animation_finished){object.state=-1;object.collected=true;}
            continue;
        }
        if(protected_player)continue;
        const auto distance=horizontal(origin-object.position);
        const bool seen=distance<e.notice && std::abs(origin.y-object.position.y)<2.0f &&
            visible(level,object.position+Vec3{0,0.6f,0},origin);
        if(expanded_enemy(e.kind)) {
            marsh_and_harbor(objects,object,model,*models,level,origin,seen,hit,pending);
        } else if(e.kind==EnemyKind::SmallLizard) {
            switch(e.phase) {
                case EnemyPhase::Idle:
                    if(seen) {
                        if(e.loop && !e.route.empty()) {
                            const auto n=static_cast<unsigned>(e.route.size());const auto a=(e.target+1)%n,b=(e.target+n-1)%n;
                            e.direction=horizontal(origin-e.route[a].position)>horizontal(origin-e.route[b].position)?1:-1;
                            e.target=e.direction==1?a:b;
                        }
                        request(object,e.route.empty()?EnemyPhase::Taunt:EnemyPhase::Flee,e.route.empty()?4:2);
                    } else if(distance<e.notice*1.5f)face(object,origin,4);
                    break;
                case EnemyPhase::Flee:flee(level,object,origin);break;
                case EnemyPhase::Taunt:
                    face(object,origin,4);
                    if(!seen)request(object,EnemyPhase::Idle,1);
                    else if(e.animation_finished){e.animation_finished=false;request(object,EnemyPhase::Ready,1);}
                    break;
                case EnemyPhase::Ready:
                    face(object,origin,4);
                    if(!seen)request(object,EnemyPhase::Idle,0);
                    else if(e.animation_finished)request(object,EnemyPhase::Taunt,4);
                    break;
                default:break;
            }
        } else if(e.kind==EnemyKind::GearGrinder || e.kind==EnemyKind::Robot) {
            hurricos(objects,object,model,*models,level,origin,seen,hit);
        } else if(e.kind==EnemyKind::MetalIdol) {
            const auto facing=face(object,origin,4);
            switch(e.phase) {
                case EnemyPhase::Idle:if(seen)request(object,EnemyPhase::Alert,1);break;
                case EnemyPhase::Alert:case EnemyPhase::Ready:
                    if(distance>5120*unit)request(object,EnemyPhase::Settle,3);
                    else if(seen && distance<2560*unit && !e.cooldown && facing<24)request(object,EnemyPhase::Swing,4);
                    if(distance>3072*unit)e.cooldown=0;break;
                case EnemyPhase::Swing: {
                    const auto time=phase_time(model,object.animation);
                    if(!e.hit_sent && time>=16 && time<=21 && seen && distance<1024*unit && facing<32 &&
                       std::abs(origin.y-(object.position.y-std::bit_cast<std::int16_t>(read_u16(object.record,56))*unit+retail::spyro_origin_height))<300*unit)
                        {hit=EnemyHit{object.position,object.id};e.hit_sent=true;}
                    if(e.animation_finished){request(object,EnemyPhase::Ready,1);e.cooldown=UINT32_MAX;}
                    break;
                }
                case EnemyPhase::Settle:if(e.animation_finished)request(object,EnemyPhase::Idle,0);break;
                default:break;
            }
        } else {
            const auto facing=face(object,origin,4);
            switch(e.phase) {
                case EnemyPhase::Idle:if(seen)request(object,EnemyPhase::Alert,1);break;
                case EnemyPhase::Alert:if(e.animation_finished)request(object,EnemyPhase::Ready,2);break;
                case EnemyPhase::Ready:
                    if(distance>e.notice+0.5f)request(object,EnemyPhase::Settle,5);
                    else if(seen && distance<e.reach && facing<24 && !e.cooldown)request(object,EnemyPhase::Swing,3);
                    break;
                case EnemyPhase::Swing: {
                    const auto time=phase_time(model,object.animation);
                    if(!e.hit_sent && time>=34 && time<=36 && seen && distance<e.reach && facing<8 &&
                       std::abs(origin.y-object.position.y)<1.0f) {hit=EnemyHit{object.position,object.id};e.hit_sent=true;}
                    if(e.animation_finished)request(object,EnemyPhase::Recover,4);
                    break;
                }
                case EnemyPhase::Recover:
                    if(e.animation_finished){request(object,EnemyPhase::Ready,2);e.cooldown=60;}break;
                case EnemyPhase::Settle:if(e.animation_finished)request(object,EnemyPhase::Idle,0);break;
                default:break;
            }
        }
    }
    if(!pending.empty()) {
        std::uint32_t id=0;for(const auto& o:objects){if(o.id==UINT32_MAX)throw FormatError("No free runtime object ID");id=std::max(id,o.id+1);}
        for(auto& o:pending){if(id==UINT32_MAX)throw FormatError("No free runtime object ID");o.id=id++;objects.push_back(std::move(o));}
    }
    return hit;
}
} // namespace spyro
