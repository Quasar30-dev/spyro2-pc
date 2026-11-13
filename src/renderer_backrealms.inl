// Pre-production materials follow the original animated vertex topology.
bool backrealms_spyro_plate(const SpyroFace& face) {
    const auto group=[](unsigned v) {
        if(v>=204 && v<=210)return 1;if(v>=211 && v<=217)return 2;
        if(v<=5)return 3;if(v>=14 && v<=19)return 4;
        if(v>=33 && v<=42)return 5;if(v>=78 && v<=87)return 6;return 0;
    };
    const auto g=group(face.vertices[0]);if(!g)return false;
    for(unsigned i=1;i<(face.quad?4u:3u);++i)if(group(face.vertices[i])!=g)return false;
    return true;
}
Vec3 backrealms_metal(Vec3 a,Vec3 b,Vec3 c,Vec3 eye) {
    const auto n=normalized(cross(b-a,c-a));
    const float rim=std::pow(std::abs(dot(n,normalized(eye-(a+b+c)/3))),10.0f);
    const float light=.39f+.32f*std::abs(dot(n,normalized(Vec3{-.4f,1,-.3f})))+.27f*rim;
    return Vec3{.80f,.88f,1.0f}*light;
}
bool backrealms_ripto_plate(const ObjectFace& face) {
    const auto head=[](unsigned v) {
        switch(v) {case 18:case 19:case 20:case 66:case 71:case 72:case 74:case 75:case 77:case 81:
        case 86:case 128:case 129:case 130:case 131:case 132:case 141:case 142:case 143:case 144:
        case 145:case 151:case 152:case 157:case 158:case 160:case 161:case 162:case 165:case 166:
        case 167:case 168:case 48:return true;default:return false;}
    };
    for(unsigned i=0;i<(face.quad?4u:3u);++i)if(!head(face.vertices[i]))return false;
    return true;
}
void backrealms_rod(std::vector<RenderVertex>& out,Vec3 a,Vec3 b,float radius,Vec3 color) {
    const auto d=normalized(b-a),u=normalized(cross(d,std::abs(d.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0}))*radius,v=cross(d,u);
    for(unsigned i=0;i<6;++i) {
        const float x=i*6.283185307f/6,y=(i+1)*6.283185307f/6;
        const auto p=u*std::cos(x)+v*std::sin(x),q=u*std::cos(y)+v*std::sin(y);
        append_triangle(out,a+p,a+q,b+p,color*(.65f+.07f*i));append_triangle(out,b+p,a+q,b+q,color*(.65f+.07f*i));
    }
}
void backrealms_ripto_details(std::vector<RenderVertex>& out,std::vector<RenderVertex>& glow,std::span<const Vec3> p) {
    if(p.size()!=222)return;
    const auto socket=ripto_eye_socket(p);
    const auto eye=socket.center,front=socket.normal;
    const float w=socket.width*.31f,h=std::max(socket.height*.32f,w*.56f);
    const auto ellipse=[&](float angle,float scale){return socket.right*(std::cos(angle)*w*scale)+socket.up*(std::sin(angle)*h*scale);};
    // Recessed red optic, bevelled steel ring, four fasteners. All vertices
    // derive from the animated eye quad, including during head turns/hits.
    for(unsigned i=0;i<12;++i) {
        const float a=i*6.283185307f/12,b=(i+1)*6.283185307f/12;
        const auto base=eye+front*.014f,rim=eye+front*.027f,lens=eye+front*.020f;
        const auto outer_a=base+ellipse(a,1.26f),outer_b=base+ellipse(b,1.26f);
        const auto inner_a=rim+ellipse(a,.84f),inner_b=rim+ellipse(b,.84f);
        const auto color=Vec3{.55f,.62f,.70f}*(.6f+.35f*std::sin(a));
        append_triangle(out,outer_a,outer_b,inner_a,color);append_triangle(out,outer_b,inner_b,inner_a,color);
        append_triangle(out,lens,lens+ellipse(a,.85f),lens+ellipse(b,.85f),{.33f,.005f,.002f});
        append_triangle(glow,lens+front*.002f,lens+front*.002f+ellipse(a,.55f),lens+front*.002f+ellipse(b,.55f),{.85f,.045f,.008f});
        if(i%3==0)diamond(out,base+ellipse(a,1.06f)+front*.018f,w*.08f,w*.08f,{.77f,.79f,.8f});
    }
    backrealms_rod(glow,eye+front*.027f-socket.up*(h*.29f),eye+front*.027f+socket.up*(h*.29f),w*.065f,{1,.5f,.13f});
    // Panel seams and a brow cable retain their original skin attachments.
    for(const auto pair:{std::pair{18u,77u},std::pair{77u,48u},std::pair{165u,166u}})
        backrealms_rod(out,p[pair.first]+front*.007f,p[pair.second]+front*.007f,.006f,{.055f,.065f,.075f});
    for(const auto vertex:{18u,77u,166u})diamond(out,p[vertex]+front*.012f,.013f,.013f,{.64f,.69f,.73f});
    const auto tip=p[123],hand=(p[110]+p[111])/2;
    const auto axis=normalized(tip-hand),u=normalized(cross(axis,Vec3{0,1,0})),v=cross(axis,u);
    backrealms_rod(out,hand,tip,.044f,{.48f,.34f,.15f});
    diamond(out,tip+axis*.14f,.22f,.32f,{1,.12f,.025f});
    for(unsigned i=0;i<3;++i) {
        const float a=i*6.283185307f/3;const auto radial=u*std::cos(a)+v*std::sin(a);
        const auto base=tip-axis*.12f,bend=tip+radial*.29f,end=tip+axis*.43f+radial*.16f;
        backrealms_rod(out,base,bend,.035f,{.57f,.40f,.20f});backrealms_rod(out,bend,end,.03f,{.70f,.51f,.25f});
    }
}

void backrealms_ripto_effects(std::vector<RenderVertex>& out,const RiptoBoss& boss,float alpha) {
    const auto ring=[&](Vec3 center,float radius,float thickness,Vec3 color) {
        for(unsigned i=0;i<64;++i){const float a=i*6.283185307f/64,b=(i+1)*6.283185307f/64;
            backrealms_rod(out,center+Vec3{std::cos(a)*radius,0,std::sin(a)*radius},center+Vec3{std::cos(b)*radius,0,std::sin(b)*radius},thickness,color);}
    };
    for(const auto& shot:boss.shots()) {
        const auto at=object_position(shot.visual,alpha);
        if(shot.kind==RiptoShotKind::Shockwave) {
            const auto r=std::lerp(shot.previous_radius,shot.radius,alpha);
            // The retail wave is an undulating strip, not a solid torus.
            // Fifteen radial sectors, six samples across the band (8007be8c).
            for(unsigned sector=0;sector<60;++sector)for(unsigned band=0;band<5;++band){
                const auto point=[&](unsigned i,unsigned j){const float angle=i*6.283185307f/60,w=float(j)/5;const float radius=std::max(0.f,r+(w-.5f)*.8f);
                    return at+Vec3{std::cos(angle)*radius,std::sin(w*3.141592654f)*(.10f+.08f*std::sin(angle*15+shot.age*.18f)),std::sin(angle)*radius};};
                const auto p=point(sector,band),q=point(sector+1,band),s=point(sector,band+1),t=point(sector+1,band+1);
                const float glow=std::sin((band+.5f)*3.141592654f/5);const auto color=shot.color*glow;
                append_triangle(out,p,q,s,color);append_triangle(out,q,t,s,color);
            }continue;
        }
        if(shot.kind==RiptoShotKind::ShieldOrb){diamond(out,at,.085f,.12f,{.13f,.23f,.8f});continue;}
        const Vec3 color=shot.kind==RiptoShotKind::Homing?Vec3{.26f,.09f,.85f}:shot.color*.8f;
        for(unsigned i=1;i<shot.trail.size();++i) {
            const auto fade=1-float(i)/shot.trail.size();
            backrealms_rod(out,i==1?at:shot.trail[i-1],shot.trail[i],.07f*fade,color*(fade*.65f));
        }
        diamond(out,at,.08f,.10f,{.9f,.6f,.18f});
    }
    for(const auto& beam:boss.beams()) {
        if(!boss.retail_arena) {
            // Crownworks: one colour per source (red Ripto, amber mechs).
            if(beam.warning) {
                for(unsigned i=0;i<16;i+=2)backrealms_rod(out,lerp(beam.from,beam.to,i/16.f),lerp(beam.from,beam.to,(i+1)/16.f),std::max(beam.radius,.022f),beam.color*.55f);
            } else {
                backrealms_rod(out,beam.from,beam.to,beam.radius,beam.color);
                backrealms_rod(out,beam.from,beam.to,beam.radius*.35f,lerp(beam.color,Vec3{1,1,1},.6f));diamond(out,beam.to,.15f,.22f,beam.color);
            }
            if(boss.action==RiptoAction::CyborgLance){const float f=std::min(1.f,(boss.action_ticks+alpha)/72);diamond(out,beam.from,.08f+f*.09f,.1f+f*.1f,beam.color*f);}
            continue;
        }
        if(beam.warning && (boss.action==RiptoAction::BirdDive || boss.action==RiptoAction::GulpPincer)) {
            for(unsigned i=0;i<16;i+=2)backrealms_rod(out,lerp(beam.from,beam.to,i/16.f),lerp(beam.from,beam.to,(i+1)/16.f),.026f,{.12f,.4f,.7f});continue;
        }
        const bool optic=boss.action==RiptoAction::CyborgLance;
        backrealms_rod(out,beam.from,beam.to,beam.radius,optic?(beam.warning?Vec3{.4f,.005f,.01f}:Vec3{1,.015f,.025f}):beam.warning?Vec3{.18f,.25f,.03f}:Vec3{.22f,.9f,.03f});
        if(!beam.warning){backrealms_rod(out,beam.from,beam.to,beam.radius*.35f,optic?Vec3{1,.6f,.4f}:Vec3{.8f,1,.4f});diamond(out,beam.to,.15f,.22f,optic?Vec3{1,.06f,.02f}:Vec3{.3f,1,.1f});}
        if(optic){const float f=std::min(1.f,(boss.action_ticks+alpha)/72);diamond(out,beam.from,.08f+f*.09f,.1f+f*.1f,{f,.02f,.01f});}
    }
    for(const auto& mark:boss.marks()) {
        const auto color=!boss.retail_arena?mark.color:boss.action==RiptoAction::GulpBombard?Vec3{1,.35f,.04f}:Vec3{.1f,.55f,1};
        ring(mark.position,mark.radius,.018f,color*.65f);
        ring(mark.position,mark.radius*(1-std::clamp(mark.progress,0.f,1.f)),.025f,color);
        for(unsigned i=0;i<4;++i){const float a=i*1.570796327f;const Vec3 d{std::cos(a),0,std::sin(a)};
            backrealms_rod(out,mark.position+d*(mark.radius-.12f),mark.position+d*(mark.radius+.12f),.025f,color);}
    }
    const auto& trail=boss.dive_trail();
    for(unsigned i=1;i<trail.size();++i){const float fade=1-float(i)/trail.size();for(float side:{-1.f,1.f})
        backrealms_rod(out,trail[i-1]+Vec3{side*.65f,.1f,0},trail[i]+Vec3{side*.65f,.1f,0},.065f*fade,Vec3{.15f,.45f,.9f}*fade);}
    const float energy=std::lerp(boss.previous_effect_charge,boss.effect_charge,alpha);
    if(energy>.001f && !boss.retail_arena) {
        const auto at=lerp(boss.previous_effect_origin,boss.effect_origin,alpha);
        const auto color=boss.action==RiptoAction::CyborgNova?Vec3{.65f,.09f,1}:Vec3{1,.025f,.015f};
        diamond(out,at,.05f+energy*.17f,.07f+energy*.23f,color*energy);
        if(boss.action==RiptoAction::CyborgNova)for(unsigned i=0;i<3;++i){
            const float a=(boss.ticks+alpha)*.07f+i*2.0943951f;
            const auto end=at+Vec3{std::cos(a)*.42f,std::sin(a*2)*.22f,std::sin(a)*.42f}*energy;
            backrealms_rod(out,at,end,.012f,color*energy);diamond(out,end,.03f,.045f,color);}
    }
    for(const auto& orb:boss.orbs()) {
        const Vec3 color=orb.color==0?Vec3{.8f,.05f,.008f}:orb.color==1?Vec3{.08f,.7f,.015f}:Vec3{.03f,.18f,.9f};
        diamond(out,object_position(orb.visual,alpha),.11f,.17f,color);
    }
    for(const auto& flash:boss.flashes()) {
        const float t=(flash.age+alpha)/24;
        ring(flash.position,flash.radius*t,.06f*(1-t),flash.color*(1-t));
        for(unsigned i=0;i<9;++i){const float a=i*6.283185307f/9;
            diamond(out,flash.position+Vec3{std::cos(a),std::sin(a*3)*.35f+.45f,std::sin(a)}*(flash.radius*t),
                flash.radius*.12f*(1-t),flash.radius*.18f*(1-t),flash.color*(1-t));}
    }
}
