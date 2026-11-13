// Behaviour profiles translated from the class cases in overlays 38 and 58.
// Quest-linked choreography remains separate from these combat states.
bool expanded_enemy(EnemyKind kind){return kind>=EnemyKind::BucketBlubber;}
unsigned death_clip(EnemyKind kind) {
    switch(kind) {
        case EnemyKind::BucketBlubber:case EnemyKind::FireBlubber:return 8;
        case EnemyKind::RhinoSnail:return 4;
        case EnemyKind::SpittingMonkey:return 2;
        case EnemyKind::Platypus:return 3;
        case EnemyKind::GearGrinder:case EnemyKind::MetalIdol:return 5;
        default:return 6;
    }
}
void shoot(std::vector<RetailObject>& pending,const RetailObject& source,const ObjectModels& models,Vec3 target,unsigned type) {
    const auto bank=models.models.find(type);if(bank==models.models.end())return;
    RetailObject shot;shot.type=static_cast<std::uint16_t>(type);shot.runtime_spawned=true;shot.held_gem=255;
    shot.source_id=source.id;shot.draw_flags=32;shot.size=source.size;
    // Hanging monkeys spit from below their authored anchor; Blubbers emit
    // their shared flame/water Moby at the front of the body.
    const auto yaw=retail::native_heading(source.rotation[2]*16);
    const bool monkey=source.type==0x1fc;
    shot.position=shot.previous_position=source.position+Vec3{std::sin(yaw)*0.45f,monkey?-1.1f:0.45f,std::cos(yaw)*0.45f};
    shot.rotation=shot.previous_rotation=source.rotation;
    shot.animation=start_object_animation(bank->second,source.type==0x169 && type==0x1ea?1:0);shot.previous_animation=shot.animation;
    shot.enemy_projectile=EnemyProjectile{normalized(target-shot.position)*(monkey?6.0f:7.5f),180};
    pending.push_back(std::move(shot));
}
void marsh_and_harbor(std::vector<RetailObject>& objects,RetailObject& o,const ObjectModel& model,const ObjectModels& models,
    const Level& level,Vec3 origin,bool seen,std::optional<EnemyHit>& hit,std::vector<RetailObject>& pending) {
    auto& e=*o.enemy;const auto d=origin-o.position;const auto distance=horizontal(d);
    if(e.kind==EnemyKind::Platypus) {
        // 8007f590: preserve the authored aquatic route. Surface clips 0..2
        // and submerged clips 5..7 use different vertex layouts; the overlay
        // changes mesh at 8007f928 / 8007fb3c, never blends across that seam.
        const auto switch_mesh=[&](EnemyPhase phase,unsigned clip) {
            request(o,phase,clip);
            o.animation=start_object_animation(model,clip);
            const bool multiple=model.animations[clip].frames.size()>1;
            o.animation.next_frame=multiple?1:0;o.animation.blend=multiple?0x30:0;
        };
        if(!e.route.empty()) {
            const auto goal=e.route[e.target].position;const auto delta=goal-o.position;
            if(length(delta)<0.25f) {
                int next=int(e.target)+e.direction;
                if(next<0 || next>=int(e.route.size())){e.direction=-e.direction;next=int(e.target)+e.direction;}
                e.target=static_cast<unsigned>(std::clamp(next,0,int(e.route.size()-1)));
            } else {face(o,goal,6);const auto next=o.position+normalized(delta)*std::min(length(delta),128*unit);
                if(visible(level,o.position,next))o.position=next;}
        } else face(o,origin,6);
        switch(e.phase) {
            case EnemyPhase::Idle:if(e.age>=120)request(o,EnemyPhase::Alert,1);break;
            case EnemyPhase::Alert:if(e.animation_finished)switch_mesh(EnemyPhase::Settle,5);break;
            case EnemyPhase::Settle:if(e.animation_finished)request(o,EnemyPhase::Patrol,6);break;
            case EnemyPhase::Patrol:if(e.age>=120)request(o,EnemyPhase::Recover,7);break;
            case EnemyPhase::Recover:if(e.animation_finished)switch_mesh(EnemyPhase::Ready,2);break;
            case EnemyPhase::Ready:if(e.animation_finished)request(o,EnemyPhase::Idle,0);break;
            default:break;
        }
        if(o.animation.next_clip<3 && !e.cooldown && distance<0.9f && std::abs(d.y)<0.7f && visible(level,o.position,origin)){hit=EnemyHit{o.position,o.id};e.cooldown=90;}
        return;
    }
    if(e.kind==EnemyKind::SpittingMonkey) {
        seen=distance<e.notice && std::abs(d.y)<8 && visible(level,o.position+Vec3{0,-1.1f,0},origin);
        face(o,origin,6);
        if(e.phase==EnemyPhase::Idle && e.animation_finished)request(o,EnemyPhase::Ready,1);
        if(e.phase==EnemyPhase::Ready && seen && !e.cooldown)request(o,EnemyPhase::Swing,4);
        if(e.phase==EnemyPhase::Swing) {
            const auto time=phase_time(model,o.animation);
            // 8007f0d0: two projectiles per spit cycle, at half-frame 16/36.
            if(time>=16 && e.timer==0){if(seen)shoot(pending,o,models,origin,0x26f);e.timer=1;}
            if(time>=36 && e.timer==1){if(seen)shoot(pending,o,models,origin,0x26f);e.timer=2;}
            if(e.animation_finished){request(o,EnemyPhase::Ready,1);e.cooldown=120;e.timer=0;}
        }
        return;
    }
    if(e.kind==EnemyKind::RhinoSnail) {
        // 8007e54c..8007e98c: emerge, watch, short-range horn strike.
        const auto angle=face(o,origin,6);
        switch(e.phase) {
            case EnemyPhase::Idle:if(seen)request(o,EnemyPhase::Alert,1);break;
            case EnemyPhase::Alert:if(e.animation_finished)request(o,EnemyPhase::Ready,2);break;
            case EnemyPhase::Ready:
                if(distance>6144*unit)request(o,EnemyPhase::Idle,0);
                else if(seen && distance<e.reach && !e.cooldown && angle<32)request(o,EnemyPhase::Swing,3);break;
            case EnemyPhase::Swing: {
                const auto time=phase_time(model,o.animation);
                if(!e.hit_sent && time>=11 && time<18 && seen && distance<2048*unit && std::abs(d.y)<1 && angle<32){hit=EnemyHit{o.position,o.id};e.hit_sent=true;}
                if(e.animation_finished){request(o,EnemyPhase::Ready,2);e.cooldown=120;}break;
            }
            default:break;
        }
        return;
    }
    if(e.kind==EnemyKind::ElephantSnail) {
        switch(e.phase) {
            case EnemyPhase::Idle:if(seen)request(o,EnemyPhase::Alert,1);break;
            case EnemyPhase::Alert:face(o,origin,6);if(e.animation_finished)request(o,EnemyPhase::Ready,2);break;
            case EnemyPhase::Ready:
                if(distance>e.notice+2)request(o,EnemyPhase::Idle,0);
                else if(seen && distance<e.reach && !e.cooldown && face(o,origin,6)<32){request(o,EnemyPhase::Swing,3);e.velocity=normalized(Vec3{d.x,0,d.z});}
                break;
            case EnemyPhase::Swing:if(e.animation_finished){request(o,EnemyPhase::Chase,4);e.timer=120;}break;
            case EnemyPhase::Chase: {
                auto goal=o.position+e.velocity*2;
                if(!e.route.empty()) {
                    goal=e.route[e.target].position;
                    if(horizontal(goal-o.position)<0.35f)e.target=(e.target+1)%static_cast<unsigned>(e.route.size());
                }
                e.timer=e.timer>2?e.timer-2:0;
                if(!walk(level,o,goal,256,6,32) || !e.timer || horizontal(o.position-e.home)>16)request(o,EnemyPhase::Recover,5);
                if(distance<1.05f && std::abs(d.y)<1 && seen && !e.hit_sent){hit=EnemyHit{o.position,o.id};e.hit_sent=true;}
                // The rolling elephant also knocks over rhinoceros snails.
                for(auto& other:objects)if(other.enemy && other.enemy->kind==EnemyKind::RhinoSnail && length(other.position-o.position)<1)other.attack_flags|=charge_attack;
                break;
            }
            case EnemyPhase::Recover:if(e.animation_finished){request(o,EnemyPhase::Ready,2);e.cooldown=120;}break;
            default:break;
        }
        return;
    }
    // The Blubbers use separate armor rules, but share the original 1EA
    // projectile and movement/attack clip family. Floor sweeps keep them on
    // their platform; linked worker/fire choreographies remain level scripts.
    switch(e.phase) {
        case EnemyPhase::Idle:if(seen)request(o,EnemyPhase::Ready,1);break;
        case EnemyPhase::Ready:
            face(o,origin,6);
            if(!seen)request(o,EnemyPhase::Return,2);
            else if(distance>e.reach)request(o,EnemyPhase::Chase,2);
            else if(!e.cooldown && face(o,origin,6)<16)request(o,EnemyPhase::Swing,4);
            break;
        case EnemyPhase::Chase:
            if(!seen || horizontal(o.position-e.home)>7 || !walk(level,o,origin,96,6,32))request(o,EnemyPhase::Return,2);
            else if(distance<e.reach)request(o,EnemyPhase::Ready,1);break;
        case EnemyPhase::Swing:
            face(o,origin,6);
            if(e.animation_finished){if(seen)shoot(pending,o,models,origin,0x1ea);request(o,EnemyPhase::Recover,5);}break;
        case EnemyPhase::Recover:if(e.animation_finished){request(o,EnemyPhase::Ready,1);e.cooldown=90;}break;
        case EnemyPhase::Return:
            if(horizontal(o.position-e.home)<0.2f || !walk(level,o,e.home,96,6,32))request(o,EnemyPhase::Idle,0);break;
        default:break;
    }
}
