// Crownworks presentation (EXPANSION_LEVEL_PLANS 6.5.2 and 6.5.4): the phase
// light and the four cutscenes. Included by ripto.cpp after its helpers.
CrownLight mix(const CrownLight& a,const CrownLight& b,float t) {
    CrownLight m;m.lower=lerp(a.lower,b.lower,t);m.terrace=lerp(a.terrace,b.terrace,t);m.walk=lerp(a.walk,b.walk,t);
    m.sky=lerp(a.sky,b.sky,t);m.core=lerp(a.core,b.core,t);m.core_radius=std::lerp(a.core_radius,b.core_radius,t);
    m.lower_cast=lerp(a.lower_cast,b.lower_cast,t);m.terrace_cast=lerp(a.terrace_cast,b.terrace_cast,t);
    m.walk_cast=lerp(a.walk_cast,b.walk_cast,t);m.sky_cast=lerp(a.sky_cast,b.sky_cast,t);return m;
}
namespace {
// 0 dormant, 1 the lower ring lit blue (Ripto on foot), 2 the terrace in amber
// (the mechs), 3 a violet sky (the bird), 4 the white crown core (finale and
// its shattering), 5 silent (defeated). Multipliers over the arena's colours.
CrownLight phase_light(int phase,unsigned clock) {
    CrownLight l;
    switch(phase) {
    case 0:l.lower={.7f,.72f,.85f};l.terrace={.74f,.74f,.84f};l.walk={.84f,.84f,.92f};l.sky={.8f,.8f,.9f};break;
    case 1:l.lower={1.05f,1.15f,1.32f};l.lower_cast={0,.07f,.3f};l.terrace={.8f,.82f,.92f};l.walk={.82f,.84f,.94f};
        l.sky={1,1.05f,1.3f};l.sky_cast={0,.01f,.04f};l.core={0,.08f,.3f};l.core_radius=10;break;
    case 2:l.lower={.85f,.8f,.8f};l.terrace={.9f,.75f,.45f};l.terrace_cast={.65f,.32f,.02f};l.walk={1,.86f,.7f};l.walk_cast={.2f,.09f,0};
        l.sky={1.2f,.9f,.7f};l.sky_cast={.07f,.03f,0};break;
    case 3:l.lower={.78f,.72f,.98f};l.terrace={.95f,.82f,1.1f};l.terrace_cast={.08f,0,.12f};l.walk={1.1f,.9f,1.3f};l.walk_cast={.25f,.05f,.35f};
        l.sky={1.5f,.9f,1.8f};l.sky_cast={.13f,.03f,.22f};break;
    case 4:l=phase_light(3,clock);l.core=Vec3{1.1f,1,.8f}*(.75f+.25f*std::sin(clock*.21f));l.core_radius=12;break;
    default:l.lower=l.terrace={.92f,.92f,.98f};l.walk={.95f,.95f,1};l.sky={1.15f,1.12f,1.25f};break;
    }
    return l;
}
struct Shot {unsigned start{},end{};Vec3 eye0,eye1,target0,target1;};
Vec3 around(Vec3 c,float degrees,float radius,float height) {
    const float a=degrees*pi/180;return c+Vec3{std::sin(a)*radius,height,std::cos(a)*radius};
}
const ArenaPiece* arena_piece(const Game& g,ArenaKind kind) {
    if(!g.level.custom_arena)return nullptr;
    for(const auto& p:g.level.custom_arena->pieces)if(p.kind==kind)return &p;
    return nullptr;
}
// Each shot eases from its first key to its second; a new shot is a cut.
std::vector<Shot> crown_shots(const RiptoBoss& boss,const Game& g,CrownCinematic cinematic) {
    const auto c=boss.center;std::vector<Shot> s;
    const auto& r=g.retail_objects[boss.actor];const auto f=forward(r);
    switch(cinematic) {
    case CrownCinematic::Intro: {
        // The arena from high over the south-west crown walk (clear of the
        // gate tower), drifting in as the ring wakes.
        s.push_back({0,150,around(c,205,38,23),around(c,190,31,16),c,c+Vec3{0,1,0}});
        // Past each relay on the crown walk.
        unsigned at=150;
        for(const auto kind:{ArenaKind::ShieldRelay,ArenaKind::RepairRelay,ArenaKind::OverdriveRelay}) {
            if(const auto* p=arena_piece(g,kind)) {
                const auto out=normalized(horizontal(p->center-c)),side=Vec3{out.z,0,-out.x};
                const auto base=p->center-out*6.5f+Vec3{0,2.6f,0},look=p->center+Vec3{0,1.3f,0};
                s.push_back({at,at+80,base-side*1.6f,base+side*1.6f,look,look});
            }
            at+=80;
        }
        // Ripto himself, turning to face Spyro.
        const auto head=r.position+Vec3{0,1.05f,0};
        s.push_back({390,480,r.position+f*4.6f+Vec3{0,1.6f,0},r.position+f*3.1f+Vec3{0,1.1f,0},head,head});
        break;
    }
    case CrownCinematic::Summon: {
        const auto head=r.position+Vec3{0,1.1f,0};
        s.push_back({0,80,r.position+f*3.6f+Vec3{-1,.7f,0},r.position+f*3.2f+Vec3{1,1.1f,0},head,head});
        // A low orbit from the terrace edge as the mechs arrive.
        s.push_back({80,240,around(c,-150,17,5),around(c,-105,15,4),c+Vec3{0,1.6f,0},c+Vec3{0,1.4f,0}});
        break;
    }
    case CrownCinematic::Flight: {
        s.push_back({0,110,around(c,200,10,1.2f),around(c,215,9,1),c+Vec3{0,1.6f,0},c+Vec3{0,3.2f,0}});
        // Up at the bird against the violet sky.
        s.push_back({110,240,around(c,160,12,1.8f),around(c,130,11,2.4f),c+Vec3{0,4.4f,0},c+Vec3{0,4.8f,0}});
        break;
    }
    case CrownCinematic::Outro: {
        s.push_back({0,150,around(c,180,13,6),around(c,150,12,5),c+Vec3{0,2.2f,0},c+Vec3{0,1.2f,0}});
        s.push_back({150,210,c+Vec3{0,30,26},c+Vec3{0,34,32},c,c});
        // The exit gate rising and Return Home beyond it.
        Vec3 gate=c+Vec3{0,7,-41.4f};
        if(const auto* p=arena_piece(g,ArenaKind::ExitGate))gate=p->center;
        const auto in=normalized(horizontal(c-gate));
        s.push_back({210,360,gate+in*10+Vec3{0,3.6f,0},gate+in*7.5f+Vec3{0,2.6f,0},gate+Vec3{0,2.5f,0},gate+Vec3{0,2.2f,0}});
        break;
    }
    default:break;
    }
    return s;
}
}
std::vector<HeldSound> RiptoBoss::held_sounds(const Game& g)const {
    std::vector<HeldSound> held;
    if(!crownworks() || !engaged)return held;
    // Sample 23 in Ripto's bank: Mecha-Gulp's looping servo hum.
    const auto motor=[&](unsigned key,unsigned index){
        if(index<g.retail_objects.size() && !g.retail_objects[index].hidden)held.push_back({key,{SoundScope::Object,0,0x2b0,g.retail_objects[index].position}});
    };
    if(stage==RiptoStage::MechaGulp && action!=RiptoAction::Transform) {
        if(health)motor(0x2b00,gulp);
        if(crush_health)motor(0x2b01,crush);
    }
    return held;
}
unsigned RiptoBoss::music_scene()const {
    if(!enabled)return 0;
    if(retail_arena)return 71;
    // Ripto's arena theme on foot, in the sky and at the core; Gulp's
    // Overlook for the mechs; nothing once the Crownworks falls silent.
    if(stage==RiptoStage::Defeated)return action==RiptoAction::Death?71:0;
    if(stage==RiptoStage::MechaGulp && action!=RiptoAction::Transform)return 55;
    if(stage==RiptoStage::Foot && action==RiptoAction::Transform)return 55;   // the mechs' theme starts with their summon
    return 71;
}
unsigned RiptoBoss::cinematic_length(CrownCinematic cinematic) {
    switch(cinematic) {
    case CrownCinematic::Intro:return 480;
    case CrownCinematic::Summon:case CrownCinematic::Flight:return 240;
    case CrownCinematic::Outro:return 360;
    default:return 0;
    }
}
const char* RiptoBoss::cinematic_caption()const {
    const auto t=cinematic_ticks;
    switch(cinematic) {
    case CrownCinematic::Intro:return t<150?"THE CROWNWORKS":t<230?"SHIELD RELAY - SHIELDS HIS MECHS":t<310?"REPAIR RELAY - MENDS HIS MECHS":
        t<390?"OVERDRIVE RELAY - SPEEDS THEIR ATTACKS":"CYBORG RIPTO";
    case CrownCinematic::Summon:return t<80?"RIPTO CALLS HIS MACHINES":"MECHA-GULP AND MECHA-CRUSH";
    case CrownCinematic::Flight:return "RIPTO TAKES TO THE SKY";
    case CrownCinematic::Outro:return t<150?"THE CROWN CORE SHATTERS":t<210?"THE CROWNWORKS FALLS SILENT":"RETURN HOME IS OPEN";
    default:return "";
    }
}
void RiptoBoss::start_cinematic(Game& g,CrownCinematic next) {
    if(!crownworks() || quiet_ || !g.cheats.boss_cinematics)return;
    cinematic=next;cinematic_ticks=0;skip_requested=false;++cinematics_played;
    g.scene_locked=true;g.camera.scripted_exact=true;g.custom_notice_ticks=0;
}
void RiptoBoss::end_cinematic(Game& g) {
    cinematics_seen|=1u<<static_cast<unsigned>(cinematic);
    cinematic=CrownCinematic::None;cinematic_ticks=0;skip_requested=false;
    g.scene_locked=false;g.camera.reset(g.level,g.player.position,g.player.yaw);
    g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,60u);
}
bool RiptoBoss::tick_cinematic(Game& g,const ObjectModels& models) {
    if(cinematic==CrownCinematic::None)return false;
    const auto length=cinematic_length(cinematic);
    if(skip_requested && cinematic_skippable()){++cinematics_skipped;cinematic_ticks=length;}
    skip_requested=false;
    ++cinematic_ticks;g.scene_locked=true;
    auto& r=g.retail_objects[actor];
    if(cinematic==CrownCinematic::Intro) {
        // Ripto wakes the Crownworks, then raises his scepter at Spyro.
        face(r,g.player.position,1.5f);
        if(cinematic_ticks==40)clip(r,models,14,true);
        if(cinematic_ticks==400)clip(r,models,2,true);
        if(cinematic_ticks==60)g.sound_events.push_back({SoundScope::Global,28,0,center});
    }
    if(cinematic_ticks>=length) {
        const auto ended=cinematic;end_cinematic(g);
        if(ended==CrownCinematic::Intro){enter(g,models,RiptoAction::Seek);notice(g,"CYBORG RIPTO - Collect Hunter's orbs. Break relays to weaken him.",300);}
        if(ended==CrownCinematic::Flight)notice(g,"Ripto is taking flight - jump, then jump again to superfly!",300);
        return false;
    }
    for(const auto& s:crown_shots(*this,g,cinematic))if(cinematic_ticks>=s.start && cinematic_ticks<s.end) {
        const float u=smooth(float(cinematic_ticks-s.start)/float(s.end-s.start));
        cinematic_eye_=lerp(s.eye0,s.eye1,u);cinematic_target_=lerp(s.target0,s.target1,u);
        const CameraView view{cinematic_eye_,normalized(cinematic_target_-cinematic_eye_)};
        if(cinematic_ticks==s.start || cinematic_ticks==1)g.camera.cut(view);else g.camera.scripted_view=view;
        break;
    }
    return true;
}
void RiptoBoss::tick_light(Game&) {
    if(!crownworks())return;
    ++light_clock_;
    int phase=0;
    if(stage==RiptoStage::Defeated)phase=action==RiptoAction::Death?4:5;
    else if(action==RiptoAction::Finale)phase=4;
    else if(engaged)phase=1+static_cast<int>(stage);
    // Cutscene beats: the ring wakes after Ripto stirs; the terrace and the
    // sky take their colours as the mechs and the bird arrive.
    if(cinematic==CrownCinematic::Intro && cinematic_ticks<60)phase=0;
    if(cinematic==CrownCinematic::Summon && cinematic_ticks>=100)phase=2;
    if(cinematic==CrownCinematic::Flight && cinematic_ticks>=60)phase=3;
    previous_light_=light_;
    if(light_phase_<0){light_phase_=phase;light_=previous_light_=light_from_=phase_light(phase,light_clock_);return;}
    if(phase!=light_phase_){light_from_=light_;light_phase_=phase;light_blend_=0;++light_changes;}
    if(light_blend_<120)++light_blend_;
    light_=mix(light_from_,phase_light(phase,light_clock_),smooth(light_blend_/120.0f));
}
