// Crownworks remaps the original Crush bank; the retail copy stays immutable.
void install_mecha_crush(Level& level,ObjectModels& models,const Level& source,ObjectModels donor,std::span<const std::uint8_t> texture) {
    if(!level.custom_arena || texture.size()!=1024*768*4)throw FormatError("Mecha-Crush requires its 1024x768 RGBA texture sheet");
    constexpr std::array<unsigned,12> sx{192,160,224,0,0,192,224,160,160,128,0,0},sy{192,192,192,32,0,32,32,192,32,32,32,0};
    const auto old_width=donor.texture_width,old_height=donor.texture_height;
    auto bank=donor.models.at(0x8f);Bytes pixels(4096*256*4);
    for(unsigned cell=0;cell<12;++cell)for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
        const auto src=((cell/4*256+y)*1024+cell%4*256+x)*4,dst=(y*4096+cell*256+x)*4;
        const auto original=((sy[cell]+y/8)*old_width+(cell+1)*256+sx[cell]+x/8)*4;
        if(original+4>donor.texture_rgba.size())throw FormatError("Crush texture reference exceeds the mounted atlas");
        std::copy_n(texture.begin()+src,3,pixels.begin()+dst);pixels[dst+3]=donor.texture_rgba[original+3];
    }
    for(auto& mesh:bank.meshes)for(auto& f:mesh.faces)if(f.textured)for(unsigned corner=0;corner<(f.quad?4u:3u);++corner) {
        auto& uv=f.uv[corner];
        const float x=uv[0]*old_width,y=uv[1]*old_height;
        const unsigned tile=unsigned(x)/256+(unsigned(y)/256)*(old_width/256);
        if(tile<1 || tile>12)throw FormatError("Unexpected Crush texture patch in mounted model");
        const unsigned cell=tile-1;
        const float u=x-float(tile%16*256+sx[cell]),v=y-float(tile/16*256+sy[cell]);
        if(u<0 || u>32 || v<0 || v>32)throw FormatError("Crush UV lies outside its referenced patch");
        uv={(cell*256+u*8)/4096,v*8/256};
    }
    // The generated sheet already contains its metal colours. Retain the
    // retail lighting gradient without multiplying in Crush's blue skin tint.
    for(auto& mesh:bank.meshes)for(auto& f:mesh.faces)for(auto& color:f.colors) {
        const float shade=std::clamp(dot(color,Vec3{.2126f,.7152f,.0722f})*.8f,.12f,1.25f);
        color=(f.textured?Vec3{1,1,1}:Vec3{.68f,.73f,.8f})*shade;
    }
    donor.models.clear();donor.models.emplace(0x8f,std::move(bank));donor.texture_rgba=std::move(pixels);
    donor.texture_width=4096;donor.texture_height=256;donor.texture_tiles=12;
    const unsigned types[]{0x8f};import_object_models(models,donor,types);
    const auto found=std::find_if(source.objects.begin(),source.objects.end(),[](const auto& o){return o.type==0x8f;});
    if(found==source.objects.end())throw FormatError("Mounted copy has no Crush actor");
    auto o=*found;unsigned id=0;for(const auto& object:level.objects)id=std::max(id,object.id+1);
    o.id=id;o.source_id=UINT32_MAX;o.runtime_spawned=o.scripted_contact=o.scripted_transient=true;
    o.position=o.previous_position={5,.02f,3};o.rotation=o.previous_rotation={0,0,128};
    o.hidden=true;o.collected=false;o.state=o.attack_flags=0;o.enemy.reset();o.boss_body.reset();o.power_target=PowerTarget{1,false};
    o.animation=start_object_animation(models.models.at(o.type));o.previous_animation=o.animation;level.objects.push_back(std::move(o));
}

void RiptoBoss::tick_crush(Game& g,const ObjectModels& models) {
    if(retail_arena || crush>=g.retail_objects.size())return;
    auto& c=g.retail_objects[crush];begin_object_transform(c,1);
    const auto flags=c.attack_flags;const bool fury=c.fury_hit,chain=c.element_chain,freeze=c.element_freeze;const auto element=c.element_damage;
    c.attack_flags=0;c.fury_hit=c.element_chain=c.element_freeze=false;c.element_damage=0;
    c.frozen_ticks=crush_frozen;c.element_frozen=crush_frozen>0;
    if(stage!=RiptoStage::MechaGulp || action==RiptoAction::Transform)return;
    ++crush_ticks;if(crush_cooldown_)--crush_cooldown_;
    const auto change=[&](CrushAction next,unsigned animation) {crush_action=next;crush_ticks=0;clip(c,models,animation,true);};
    if(crush_action==CrushAction::Defeated){if(crush_ticks>=100)c.hidden=true;return;}
    if(!crush_health)return;
    // Ice freezes Crush for at most 1.5 s, once per 10 s; an Electric breath
    // on him chains 1 HP to Gulp, and one on Gulp chains to him (6.4).
    if(freeze && !crush_frozen && !crush_freeze_ready_){crush_frozen=mech_freeze;crush_freeze_ready_=mech_freeze_cooldown;++mech_freezes;
        flashes_.push_back({c.position+Vec3{0,1.2f,0},0,1.6f,{.7f,.9f,1}});}
    if(chain && health)chain_to_gulp_=true;
    const bool chained=chain_to_crush_;chain_to_crush_=false;
    unsigned damage=0;
    if((flags || chained) && !crush_cooldown_) {
        damage=(flags&green_bomb_attack)?15:(flags&0x140000)?10:(flags&headbash_attack)?8:(flags&charge_attack)?3:1;
        if(element)damage=(flags&green_bomb_attack)?std::max(15u,element):element;
        if(chained && !flags)++chain_hits;
        if(g.level.custom_arena->boss_shield)damage=(damage+1)/2;
        if(fury){damage=std::min(damage,crush_fury_damage_<20?20-crush_fury_damage_:0u);crush_fury_damage_+=damage;}   // a third of 60
    }
    if(damage) {
        crush_health-=std::min(crush_health,damage);++crush_hits;crush_cooldown_=damage>=5?70:40;
        flashes_.push_back({c.position+Vec3{0,1,0},0,1,{.4f,.7f,1}});
        g.sound_events.push_back({SoundScope::Object,2,0x8f,c.position});
        if(!crush_health) {
            c.boss_body.reset();change(CrushAction::Defeated,2);g.camera.shake(1800,18);
            if(!health)finish_phase(g,models);else notice(g,"Mecha-Crush down - finish Mecha-Gulp!");
            return;
        }
        if(damage>=5){change(CrushAction::Hit,6);if(action==RiptoAction::GulpPincer)enter(g,models,RiptoAction::Seek);}
    }
    if(crush_frozen){--crush_frozen;c.frozen_ticks=crush_frozen;c.element_frozen=crush_frozen>0;return;}   // frozen: no movement or attacks
    const auto origin=g.player.position+retail::origin_above_feet;
    switch(crush_action) {
    case CrushAction::Approach: {
        auto away=normalized(horizontal(c.position-g.player.position));if(flat(away)<.01f)away={1,0,0};
        auto destination=g.player.position+away*3.2f;
        const auto& m=g.retail_objects[gulp];
        if(!m.hidden && flat(destination-m.position)<3.4f)destination=m.position+normalized(horizontal(c.position-m.position))*3.4f;
        clip(c,models,ground_move(g,c,destination,3.2f)?3:0);face(c,g.player.position,1.2f);
        // Both bosses move together, but avoid simultaneous heavy windups.
        const bool opening=!health || action==RiptoAction::Seek || action==RiptoAction::Hit || action==RiptoAction::Waiting;
        if(crush_ticks>=150 && opening && clear(g,c.position+Vec3{0,1,0},origin)) {
            crush_aim_=origin;++crush_attacks;
            const bool slam=(crush_sequence_++%2)==0;
            change(slam?CrushAction::Slam:CrushAction::Blast,slam?7:1);
            g.sound_events.push_back({SoundScope::Object,0,0x8f,c.position});
        }break;
    }
    case CrushAction::Slam: {
        if(crush_ticks<30)face(c,g.player.position,1);
        const auto impact=c.position+forward(c)*1.6f+Vec3{0,.1f,0};
        // Overlay 30's club clip emits pulses at animation times 25/53/89.
        const unsigned release[]{50,106,178};
        for(unsigned i=0;i<3;++i)if(crush_ticks<=release[i] && (i==0 || crush_ticks>release[i-1]))marks_.push_back({impact,2.1f,float(crush_ticks-(i?release[i-1]:0))/(release[i]-(i?release[i-1]:0))});
        if(crush_ticks==50 || crush_ticks==106 || crush_ticks==178) {
            shoot(g,models,RiptoShotKind::Shockwave,{impact.x,center.y+.1f,impact.z},{},0);shots_.back().color={.1f,.55f,1};
            flashes_.push_back({impact,0,2.1f,{.15f,.65f,1}});g.camera.shake(1800,14);
            g.sound_events.push_back({SoundScope::Object,1,0x8f,c.position});
            if(flat(origin-impact)<2.1f && std::abs(origin.y-impact.y)<.8f && clear(g,impact+Vec3{0,.15f,0},origin) && g.hurt(impact)){++crush_damage;++damage_to_spyro;}
        }
        if(crush_ticks>=215)change(CrushAction::Recover,0);break;
    }
    case CrushAction::Blast: {
        if(crush_ticks<30){face(c,g.player.position,1);crush_aim_=origin;}
        const auto from=c.position+forward(c)*1.1f+Vec3{0,1.25f,0};
        if(crush_ticks<52)marks_.push_back({crush_aim_-Vec3{0,.35f,0},.8f,crush_ticks/52.f});
        if(crush_ticks==52) {
            shoot(g,models,RiptoShotKind::Fireball,from,crush_aim_-from,11);shots_.back().color={.12f,.55f,1};
            flashes_.push_back({from,0,.7f,{.15f,.7f,1}});g.sound_events.push_back({SoundScope::Object,1,0x8f,from});
        }
        if(crush_ticks>=110)change(CrushAction::Recover,0);break;
    }
    case CrushAction::Combo:break; // The synchronized encounter timeline owns both actors.
    case CrushAction::Recover:if(crush_ticks>=100)change(CrushAction::Approach,3);break;
    case CrushAction::Hit:if(crush_ticks>=55)change(CrushAction::Approach,3);break;
    default:break;
    }
}
