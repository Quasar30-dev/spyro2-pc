// Elemental powers, demon eruptions, melee trails and Dark Spyro effects.
// Original procedural PS1-style geometry: Gouraud colour, the procedural
// 4-level sprite atlas and the four retail blend equations (subtractive for
// the void and black smoke). Included inside Renderer::draw.
rendered_element_effects_=rendered_demon_eruptions_=rendered_dark_smoke_=rendered_eye_glows_=rendered_melee_trails_=rendered_shockwave_triangles_=0;
if(element_game && player && !studio && !collision) {
    const auto& gs=*element_game;const auto& el=gs.elements;
    const auto view=normalized(focus-eye),side_axis=normalized(cross(camera_up,view)),up_axis=cross(view,side_axis);
    std::vector<RenderVertex> solid;
    std::array<std::vector<RenderVertex>,4> sprites,plain;
    const auto vtx=[](Vec3 p,Vec3 c,float a=1,float u=0,float v=0,float z=0){return RenderVertex{p,c.x,c.y,c.z,a,u,v,z};};
    const auto tri=[&](std::vector<RenderVertex>& out,Vec3 a,Vec3 b,Vec3 c,Vec3 ca,Vec3 cb,Vec3 cc,float alpha_value=1) {
        out.push_back(vtx(a,ca,alpha_value));out.push_back(vtx(b,cb,alpha_value));out.push_back(vtx(c,cc,alpha_value));
    };
    const auto billboard=[&](std::vector<RenderVertex>& out,Vec3 p,float r,float spin,Vec3 c,float a,EffectSprite s) {
        const auto uv=element_sprite_uv(s);
        const auto x=(side_axis*std::cos(spin)+up_axis*std::sin(spin))*r,y=(up_axis*std::cos(spin)-side_axis*std::sin(spin))*r;
        const std::array<Vec3,4> q{p-x-y,p+x-y,p-x+y,p+x+y};const float us[]{uv[0],uv[1],uv[0],uv[1]},vs[]{uv[3],uv[3],uv[2],uv[2]};
        for(const unsigned k:{0u,1u,2u,1u,3u,2u})out.push_back({q[k],c.x,c.y,c.z,a,us[k],vs[k],1});
    };
    // A camera-facing ribbon segment with independent widths/colours.
    const auto ribbon=[&](std::vector<RenderVertex>& out,Vec3 a,Vec3 b,float wa,float wb,Vec3 ca,Vec3 cb,float aa=1,float ab=1) {
        const auto s=normalized(cross(b-a,eye-(a+b)*.5f));if(length(s)<.5f)return;
        const std::array<Vec3,4> q{a-s*wa,a+s*wa,b-s*wb,b+s*wb};const std::array<Vec3,4> c{ca,ca,cb,cb};const float al[]{aa,aa,ab,ab};
        for(const unsigned k:{0u,1u,2u,1u,3u,2u})out.push_back(vtx(q[k],c[k],al[k]));
    };
    // Low-poly sphere (8 x 6) for orbs, bubbles and expanding shells.
    const auto sphere=[&](std::vector<RenderVertex>& out,Vec3 c,float r,Vec3 top,Vec3 bottom,float a,unsigned slices=8,unsigned stacks=6) {
        for(unsigned i=0;i<stacks;++i)for(unsigned j=0;j<slices;++j) {
            const auto p=[&](unsigned s,unsigned t){const float th=float(s)/stacks*std::numbers::pi_v<float>,ph=float(t)/slices*2*std::numbers::pi_v<float>;
                return c+Vec3{std::sin(th)*std::cos(ph),std::cos(th),std::sin(th)*std::sin(ph)}*r;};
            const auto col=[&](unsigned s){return lerp(top,bottom,float(s)/stacks);};
            tri(out,p(i,j),p(i+1,j),p(i+1,j+1),col(i),col(i+1),col(i+1),a);tri(out,p(i,j),p(i+1,j+1),p(i,j+1),col(i),col(i+1),col(i),a);
        }
    };
    // Faceted crystal: hexagonal bipyramid along `axis` (ice shards/crystals).
    const auto crystal=[&](Vec3 base,Vec3 axis,float length_value,float radius,Vec3 body,Vec3 tip,float spin,float tail=.35f) {
        axis=normalized(axis);const auto s0=normalized(cross(std::abs(axis.y)>.9f?Vec3{1,0,0}:Vec3{0,1,0},axis)),s1=cross(axis,s0);
        const auto head=base+axis*length_value,foot=base-axis*(length_value*tail);
        for(unsigned k=0;k<6;++k) {
            const float a=spin+k*std::numbers::pi_v<float>/3,b=spin+(k+1)*std::numbers::pi_v<float>/3;
            const auto pa=base+(s0*std::cos(a)+s1*std::sin(a))*radius,pb=base+(s0*std::cos(b)+s1*std::sin(b))*radius;
            const float light=.62f+.38f*std::max(0.0f,std::cos(a-.6f));
            tri(solid,pa,pb,head,body*light,body*light,tip);tri(solid,pb,pa,foot,body*(light*.7f),body*(light*.7f),body*.55f);
        }
    };
    // ---- Shockwaves: ground ripples follow the floor, spheres expand in 3D.
    for(const auto& w:el.shockwaves()) {
        const float life=std::clamp(1-(float(w.age)+alpha)/w.lifetime,0.0f,1.0f);
        const auto base_color=w.plain?Vec3{.55f,.46f,.34f}:element_color(w.element);
        const auto core_color=w.plain?Vec3{.75f,.65f,.5f}:element_core_color(w.element);
        if(w.shape==ShockShape::Ripple) {
            const unsigned mode=w.plain || w.element==Element::Water || w.element==Element::Ice?0u:1u;
            auto& out=plain[mode];const auto before=out.size();
            for(std::size_t r=0;r+1<w.rings.size();++r) {
                const auto& a=w.rings[r];const auto& b=w.rings[r+1];
                const float fade=life*(1-float(r)/w.rings.size());
                for(unsigned k=0;k<32;++k) {
                    const unsigned n=(k+1)%32;if(!a.valid[k] || !a.valid[n] || !b.valid[k] || !b.valid[n])continue;
                    const float t0=k*2*std::numbers::pi_v<float>/32,t1=n*2*std::numbers::pi_v<float>/32;
                    const auto at=[&](const ShockRing& ring,unsigned i,float t){return Vec3{w.center.x+std::sin(t)*ring.radius,ring.height[i]+.06f,w.center.z+std::cos(t)*ring.radius};};
                    const auto c0=mode?base_color*fade:base_color,c1=mode?core_color*fade:core_color;
                    const float o0=mode?1:.55f*fade,o1=mode?1:.7f*fade;
                    out.push_back(vtx(at(b,k,t0),c0,o0));out.push_back(vtx(at(a,k,t0),c1,o1));out.push_back(vtx(at(a,n,t1),c1,o1));
                    out.push_back(vtx(at(b,k,t0),c0,o0));out.push_back(vtx(at(a,n,t1),c1,o1));out.push_back(vtx(at(b,n,t1),c0,o0));
                    if(r==0) {
                        // Raised crest along the moving front: a real ripple wall.
                        const float crest=(.18f+(w.plain?.06f:.14f))*life;
                        const auto lift=[&](Vec3 p){return p+Vec3{0,crest,0};};
                        out.push_back(vtx(at(a,k,t0),c1,o1));out.push_back(vtx(lift(at(a,k,t0)),c0*.4f,mode?1:0));out.push_back(vtx(lift(at(a,n,t1)),c0*.4f,mode?1:0));
                        out.push_back(vtx(at(a,k,t0),c1,o1));out.push_back(vtx(lift(at(a,n,t1)),c0*.4f,mode?1:0));out.push_back(vtx(at(a,n,t1),c1,o1));
                    }
                }
            }
            rendered_shockwave_triangles_+=static_cast<unsigned>((out.size()-before)/3);
        } else {
            const float radius=std::lerp(w.previous_radius,w.radius,alpha);
            const auto center=w.center+Vec3{0,.25f,0};
            // Both shell faces draw; from inside, the far faces fill the whole
            // screen, so an engulfed camera only gets a third of the shell.
            const float engulfed=std::clamp((length(eye-center)-radius*.85f)/(radius*.3f),0.0f,1.0f);
            const float shell=life*(.35f+.65f*engulfed);
            if(w.element==Element::Convexity) {
                // A half-blended violet-black dome: it darkens toward the void
                // but never crushes a dark scene to pure black as subtraction did.
                // A Fury blast also gets the radiating violet front the other
                // elements' blasts have, with the void dome just behind it.
                const auto before0=plain[0].size(),before1=plain[1].size();
                if(w.fury) {
                    sphere(plain[1],center,radius,core_color*shell*.5f,base_color*shell*.55f,1,16,10);
                    sphere(plain[1],center,radius*.84f,core_color*shell*.18f,base_color*shell*.2f,1,14,9);
                }
                sphere(plain[0],center,radius*(w.fury?.8f:1.0f),Vec3{.10f,.03f,.16f},Vec3{.05f,.01f,.09f},(w.fury?.34f:.26f)*shell,12,8);
                rendered_shockwave_triangles_+=static_cast<unsigned>((plain[0].size()-before0+plain[1].size()-before1)/3);
                for(unsigned k=0;k<24;++k){const float a=k*2*std::numbers::pi_v<float>/24,b=(k+1)*2*std::numbers::pi_v<float>/24;
                    ribbon(plain[1],center+Vec3{std::sin(a),0,std::cos(a)}*radius,center+Vec3{std::sin(b),0,std::cos(b)}*radius,.07f,.07f,base_color*life*1.6f,base_color*life*1.6f);}
            } else if(w.fury) {
                // A Fury blast through the air: a bright leading shell over a
                // fainter trailing one, so the wave reads as a thick radiating
                // front rather than a bubble.
                const auto before=plain[1].size();
                sphere(plain[1],center,radius,core_color*shell*.55f,base_color*shell*.38f,1,16,10);
                sphere(plain[1],center,radius*.84f,core_color*shell*.22f,base_color*shell*.12f,1,14,9);
                rendered_shockwave_triangles_+=static_cast<unsigned>((plain[1].size()-before)/3);
            } else {
                const auto before=plain[1].size();
                sphere(plain[1],center,radius,core_color*shell*.35f,base_color*shell*.22f,1,12,8);
                rendered_shockwave_triangles_+=static_cast<unsigned>((plain[1].size()-before)/3);
            }
        }
        ++rendered_element_effects_;
    }
    // ---- Elemental water: the Water Fury flood and headbash pockets. Half-
    // blended Gouraud surfaces with a slow swell; terrain above the surface
    // occludes them, so the level reads as flooded rather than covered.
    for(const auto& w:el.water()) {
        const float h=std::lerp(w.previous_height,w.height,alpha);
        if(h<=w.floor+.02f)continue;
        const float swell=material_ticks*.03f;
        const auto surface=[&](float x,float z){return Vec3{x,h+.035f*std::sin(x*.9f+swell*2.1f)+.03f*std::cos(z*1.1f-swell*1.7f),z};};
        const auto shade=[&](Vec3 p){const float crest=std::clamp((p.y-h)*9+.5f,0.0f,1.0f);return lerp(Vec3{.08f,.26f,.48f},Vec3{.22f,.48f,.70f},crest);};
        if(w.cells) {
            // A flood held to its cells (the Crownworks terrace): the surface
            // over each wet cell, and a translucent face where it meets a dry one.
            const auto& c=*w.cells;
            const auto wet=[&](int i,int j){return i>=0 && j>=0 && i<int(c.columns) && j<int(c.rows) && c.wet[std::size_t(j)*c.columns+std::size_t(i)];};
            for(unsigned j=0;j<c.rows;++j)for(unsigned i=0;i<c.columns;++i) {
                if(!wet(int(i),int(j)))continue;
                const float x0=c.origin.x+c.cell*i,x1=x0+c.cell,z0=c.origin.z+c.cell*j,z1=z0+c.cell;
                const Vec3 mid{(x0+x1)*.5f,h,(z0+z1)*.5f};
                if(dot(mid-eye,view)<-2 || length(mid-eye)>140)continue;
                const auto p00=surface(x0,z0),p10=surface(x1,z0),p01=surface(x0,z1),p11=surface(x1,z1);
                tri(plain[0],p00,p10,p11,shade(p00),shade(p10),shade(p11),.55f);tri(plain[0],p00,p11,p01,shade(p00),shade(p11),shade(p01),.55f);
                const auto edge=[&](Vec3 a,Vec3 b2) {
                    const Vec3 fa{a.x,w.floor,a.z},fb{b2.x,w.floor,b2.z};
                    tri(plain[0],a,fa,fb,Vec3{.10f,.28f,.50f},Vec3{.04f,.12f,.26f},Vec3{.04f,.12f,.26f},.42f);
                    tri(plain[0],a,fb,b2,Vec3{.10f,.28f,.50f},Vec3{.04f,.12f,.26f},Vec3{.10f,.28f,.50f},.42f);
                    ribbon(plain[1],a,b2,.04f,.04f,Vec3{.18f,.26f,.32f},Vec3{.18f,.26f,.32f});   // foam line
                };
                if(!wet(int(i)-1,int(j)))edge(p00,p01);
                if(!wet(int(i)+1,int(j)))edge(p10,p11);
                if(!wet(int(i),int(j)-1))edge(p00,p10);
                if(!wet(int(i),int(j)+1))edge(p01,p11);
            }
        } else if(w.flood) {
            const auto& b=level.bounds;constexpr unsigned cells=48;
            const float sx=(b.max.x-b.min.x)/cells,sz=(b.max.z-b.min.z)/cells;
            for(unsigned i=0;i<cells;++i)for(unsigned j=0;j<cells;++j) {
                const float x0=b.min.x+sx*i,x1=x0+sx,z0=b.min.z+sz*j,z1=z0+sz;
                const auto p00=surface(x0,z0),p10=surface(x1,z0),p01=surface(x0,z1),p11=surface(x1,z1);
                tri(plain[0],p00,p10,p11,shade(p00),shade(p10),shade(p11),.55f);tri(plain[0],p00,p11,p01,shade(p00),shade(p11),shade(p01),.55f);
            }
        } else {
            constexpr unsigned sides=20;const auto c=Vec3{w.center.x,h,w.center.z};
            for(unsigned k=0;k<sides;++k) {
                const float a0=k*2*std::numbers::pi_v<float>/sides,a1=(k+1)*2*std::numbers::pi_v<float>/sides;
                const auto r0=surface(w.center.x+std::sin(a0)*w.radius,w.center.z+std::cos(a0)*w.radius),r1=surface(w.center.x+std::sin(a1)*w.radius,w.center.z+std::cos(a1)*w.radius);
                tri(plain[0],c,r0,r1,Vec3{.12f,.32f,.55f},shade(r0),shade(r1),.55f);
                // The pocket's wall: a translucent column down to its floor.
                const Vec3 f0{r0.x,w.floor,r0.z},f1{r1.x,w.floor,r1.z};
                tri(plain[0],r0,f0,f1,Vec3{.10f,.28f,.50f},Vec3{.04f,.12f,.26f},Vec3{.04f,.12f,.26f},.42f);
                tri(plain[0],r0,f1,r1,Vec3{.10f,.28f,.50f},Vec3{.04f,.12f,.26f},Vec3{.10f,.28f,.50f},.42f);
                ribbon(plain[1],r0,r1,.05f,.05f,Vec3{.18f,.26f,.32f},Vec3{.18f,.26f,.32f});   // foam rim
            }
        }
        ++rendered_element_effects_;
    }
    // ---- Frozen water (Winter Tundra's frozen pond): a pale frost base over
    // the water, then the retail ice tile (WAD 57 tile 35) added with the
    // pond's own blue vertex tint (0.84, 0.92, 1.62). Thawing ice fades out.
    std::vector<RenderVertex> ice_tiles;
    for(const auto& f:el.ice()) {
        const float fade=f.melt?float(f.melt)/ElementSystem::ice_melt:std::min(1.0f,(float(f.age)+alpha)/10.0f);
        const auto base=Vec3{.50f,.66f,.80f};const float k=.62f*fade;
        for(std::size_t i=0;i+2<f.sheet.size();i+=3) {
            const auto a=f.sheet[i],b=f.sheet[i+1],c=f.sheet[i+2];
            if(dot((a+b+c)*(1.0f/3)-eye,view)<-2)continue;
            tri(plain[0],a,b,c,base,base*.92f,base*1.04f,(ice_texture_?.38f:.55f)*fade);
            for(unsigned v=0;v<3;++v){const auto& uv=f.sheet_uv[i+v];
                ice_tiles.push_back({f.sheet[i+v]+Vec3{0,.004f,0},.84f*k,.92f*k,1.62f*k,1,uv[0],uv[1],1});}
        }
        ++rendered_element_effects_;
    }
    // Underwater in elemental water: a blue veil just in front of the camera.
    if(!el.water().empty())
        if(const auto under=level.water_at(eye);under && eye.y<under->height && !level.retail_water_at(eye)) {
            const auto c=eye+view*.3f;const auto sx=side_axis*.6f,uy=up_axis*.6f;
            const Vec3 tint{.05f,.17f,.33f};
            tri(plain[0],c-sx-uy,c+sx-uy,c+sx+uy,tint,tint,tint,.5f);tri(plain[0],c-sx-uy,c+sx+uy,c-sx+uy,tint,tint,tint,.5f);
        }
    // ---- Breath bodies: Torrent tubes with soft spiral bands; Frost puffs.
    {
        const auto& nodes=el.breath_nodes();
        // One translucent six-sided water segment. The two spiral bands are dim
        // and wide so the jet reads as water, never as an electric arc.
        const auto segment=[&](Vec3 q,float rq,float tq,float phase_q,Vec3 p,float r,float t,float phase_p) {
            const auto axis=normalized(p-q);const auto s0=normalized(cross(std::abs(axis.y)>.95f?Vec3{1,0,0}:Vec3{0,1,0},axis)),s1=cross(axis,s0);
            const auto ring=[&](Vec3 c,float rr,float ang){return c+(s0*std::cos(ang)+s1*std::sin(ang))*rr;};
            for(unsigned k=0;k<6;++k) {
                const float a=k*std::numbers::pi_v<float>/3,b=(k+1)*std::numbers::pi_v<float>/3;
                const auto ca=Vec3{.20f,.50f,.95f}*(.78f+.22f*std::cos(a-.8f)),cb=Vec3{.20f,.50f,.95f}*(.78f+.22f*std::cos(b-.8f));
                tri(plain[0],ring(q,rq,a),ring(q,rq,b),ring(p,r,a),ca,cb,ca,.5f*(1-tq));tri(plain[0],ring(q,rq,b),ring(p,r,b),ring(p,r,a),cb,cb,ca,.5f*(1-t));
            }
            for(unsigned h=0;h<2;++h) {
                const float ph=h*std::numbers::pi_v<float>;
                const auto hp=p+(s0*std::cos(phase_p+ph)+s1*std::sin(phase_p+ph))*r*.8f,hq=q+(s0*std::cos(phase_q+ph)+s1*std::sin(phase_q+ph))*rq*.8f;
                ribbon(plain[1],hq,hp,.035f,.035f,Vec3{.08f,.17f,.28f}*(1-tq),Vec3{.08f,.17f,.28f}*(1-t));
            }
        };
        std::optional<std::size_t> last_water,newest_water;
        for(std::size_t i=0;i<nodes.size();++i) {
            const auto& n=nodes[i];const auto p=lerp(n.previous,n.position,alpha);const float r=std::lerp(n.previous_radius,n.radius,alpha);
            const float t=std::clamp((float(n.age)+alpha)/n.lifetime,0.0f,1.0f);
            if(n.phase<0) {
                // Frost lightens what it passes (additive mist); half blending greyed it into smoke.
                billboard(sprites[1],p,r,n.phase*3+t*2,Vec3{.075f,.10f,.135f}*(1-t),1,EffectSprite::Steam);
                billboard(sprites[1],p,r*.45f,-n.phase*5+t*4,Vec3{.6f,.8f,1.0f}*(1-t),1,EffectSprite::Snowflake);
                continue;
            }
            if(last_water) {
                const auto& m=nodes[*last_water];const auto q=lerp(m.previous,m.position,alpha);const float rq=std::lerp(m.previous_radius,m.radius,alpha);
                const float tq=std::clamp((float(m.age)+alpha)/m.lifetime,0.0f,1.0f);
                if(length(p-q)<.95f && !n.stopped && !m.stopped)segment(q,rq,tq,m.phase,p,r,t,n.phase);
            }
            last_water=i;if(!n.stopped && (!newest_water || n.age<=nodes[*newest_water].age))newest_water=i;
            if(n.stopped)billboard(sprites[0],p,r*1.4f,n.phase,Vec3{.9f,.97f,1},.6f*(1-t),EffectSprite::Foam);
        }
        // Close the gap between the drawn snout and the youngest jet node, so the
        // Torrent visibly leaves the mouth through every head gesture.
        if(el.breathing() && newest_water && nodes[*newest_water].age<=2)
            if(const auto mouth=spyro_mouth(model,pose)) {
                const auto& n=nodes[*newest_water];const auto p=lerp(n.previous,n.position,alpha);
                const auto from=position+retail::rotate_model(*mouth,angle,pitch,roll);
                if(length(p-from)<.9f){segment(from,.05f,0,n.phase+.7f,p,std::lerp(n.previous_radius,n.radius,alpha),0,n.phase);
                    billboard(sprites[0],from,.07f,n.phase,Vec3{.75f,.9f,1},.45f,EffectSprite::Foam);}
            }
    }
    // ---- Particles (original sprites) and 3D crystals.
    for(const auto& q:el.particles()) {
        const auto p=lerp(q.previous,q.position,alpha);const float t=std::clamp((float(q.age)+alpha)/std::max(1u,q.lifetime),0.0f,1.0f);
        if(q.sprite==EffectSprite::Crystal) {
            const auto axis=retail::rotate_model({0,1,0},q.spin,q.spin*.7f,0);
            crystal(p,axis,q.radius*1.8f,q.radius*.75f,Vec3{.55f,.78f,1.0f},Vec3{.95f,1,1},q.spin,1.0f);continue;
        }
        const float r=q.radius*(1+t*(q.sprite==EffectSprite::Steam || q.sprite==EffectSprite::Mote || q.sprite==EffectSprite::Ring?1.6f:.3f));
        if(q.blend==0)billboard(sprites[0],p,r,q.spin,q.color,.75f*(1-t),q.sprite);
        else if(q.blend==2)billboard(sprites[2],p,r,q.spin,Vec3{.55f,.75f,.35f}*(1-t)+Vec3{.15f,.05f,.2f}*(1-t),1,q.sprite);
        else billboard(sprites[q.blend],p,r,q.spin,q.color*(1-t)*1.2f,1,q.sprite);
    }
    // ---- Bolts: coloured outer glow and a hot core.
    for(const auto& b:el.bolts()) {
        const float fade=std::clamp(1-(float(b.age)+alpha)/std::max(1u,b.lifetime),0.0f,1.0f);
        for(std::size_t k=1;k<b.points.size();++k) {
            // Thin bright core over a faint, narrow glow: lightning, not a lit bar.
            ribbon(plain[1],b.points[k-1],b.points[k],b.width*1.5f,b.width*1.5f,b.color*fade*.3f,b.color*fade*.3f);
            ribbon(plain[1],b.points[k-1],b.points[k],b.width*.45f,b.width*.45f,b.core*fade,b.core*fade);
        }
        ++rendered_element_effects_;
    }
    // ---- Void Beam: subtractive darkness with an additive violet core.
    if(el.beam().active) {
        const auto& now=el.beam();const auto before=el.previous_beam();
        const auto from=before.active?lerp(before.from,now.from,alpha):now.from,to=before.active?lerp(before.to,now.to,alpha):now.to;
        const float w=now.width;
        // A violet-black haze (subtractive, removing less blue) around a glowing core.
        ribbon(plain[2],from,to,w*.9f,w*1.7f,Vec3{.36f,.46f,.2f},Vec3{.42f,.52f,.24f});
        ribbon(plain[1],from,to,w*.6f,w*.9f,Vec3{.40f,.10f,.66f},Vec3{.46f,.13f,.72f});
        ribbon(plain[1],from,to,w*.25f,w*.35f,Vec3{.95f,.85f,1},Vec3{.9f,.8f,1});
        billboard(sprites[1],to,w*3.2f,material_ticks*.2f,Vec3{.55f,.2f,.9f},1,EffectSprite::Ring);
        billboard(sprites[2],from,w*1.6f,0,Vec3{.5f,.7f,.3f},1,EffectSprite::Mote);
        ++rendered_element_effects_;
    }
    // ---- Original projectiles.
    for(const auto& s:el.shots()) {
        const auto p=lerp(s.previous,s.position,alpha);const float spin=std::lerp(s.previous_spin,s.spin,alpha);
        if(s.exploding) {
            const float t=std::clamp((float(s.blast)+alpha)/16,0.0f,1.0f);
            const auto c=element_color(s.element)*(1-t);
            billboard(sprites[s.element==Element::Convexity?2:s.element==Element::Water?0:1],p,.2f+t*1.5f,spin,s.element==Element::Convexity?Vec3{.5f,.7f,.3f}*(1-t):c,s.element==Element::Water?(1-t)*.7f:1,EffectSprite::Ring);
            if(s.kind==ShotKind::Ember || s.kind==ShotKind::SparkOrb)billboard(sprites[1],p,.4f+t*.8f,0,element_core_color(s.element)*(1-t),1,EffectSprite::Mote);
            continue;
        }
        switch(s.kind) {
        case ShotKind::Ember: {
            // Molten core: faceted orb with flickering lava vertex colours.
            const auto hot=Vec3{1,.75f,.25f},cool=Vec3{.85f,.22f,.05f};
            for(unsigned k=0;k<8;++k) {
                const auto corner=[&](unsigned i){const float a=spin+i*std::numbers::pi_v<float>/2;return p+Vec3{std::cos(a),0,std::sin(a)}*s.radius;};
                const auto apex=p+Vec3{0,k<4?s.radius*1.2f:-s.radius*1.1f,0};
                const float flick=.5f+.5f*std::sin(material_ticks*.9f+k*1.7f);
                tri(solid,corner(k%4),corner((k+1)%4),apex,lerp(cool,hot,flick),lerp(hot,cool,flick),hot);
            }
            billboard(sprites[1],p,s.radius*2.8f,spin,Vec3{1,.45f,.1f},1,EffectSprite::Mote);
            for(std::size_t k=1;k<s.trail.size();++k){const float f=1-float(k)/s.trail.size();
                ribbon(plain[1],s.trail[k-1],s.trail[k],s.radius*.9f*f,s.radius*.9f*(f-1.0f/s.trail.size()),Vec3{1,.5f,.12f}*f,Vec3{.8f,.2f,.05f}*f);}
            break;
        }
        case ShotKind::Bubble: {
            const float wobble=1+.08f*std::sin(material_ticks*.35f);
            sphere(plain[0],p,s.radius*wobble,Vec3{.75f,.92f,1},Vec3{.25f,.5f,1},.42f);
            billboard(sprites[1],p+(up_axis-side_axis)*s.radius*.45f,s.radius*.35f,0,Vec3{.9f,.95f,1},1,EffectSprite::Mote);
            for(unsigned k=0;k<2;++k){const float a=spin*2+k*3.1416f;billboard(sprites[1],p+Vec3{std::cos(a),0,std::sin(a)}*s.radius*.45f,s.radius*.3f,a,Vec3{.3f,.6f,1},1,EffectSprite::Droplet);}
            break;
        }
        case ShotKind::Shard: {
            const auto axis=length(s.velocity)>.01f?normalized(s.velocity):Vec3{0,0,1};
            crystal(p-axis*.12f,axis,.34f,s.radius*1.3f,Vec3{.6f,.82f,1},Vec3{1,1,1},spin,.6f);
            for(std::size_t k=1;k<s.trail.size();++k){const float f=1-float(k)/s.trail.size();ribbon(plain[1],s.trail[k-1],s.trail[k],.03f*f,.02f*f,Vec3{.6f,.85f,1}*f,Vec3{.4f,.7f,1}*f);}
            break;
        }
        case ShotKind::SparkOrb:
            billboard(sprites[1],p,s.radius*3.4f,spin,Vec3{.7f,.65f,.2f},1,EffectSprite::Mote);
            billboard(sprites[1],p,s.radius*2.2f,-spin*1.7f,Vec3{1,1,.85f},1,EffectSprite::Spark);
            sphere(plain[1],p,s.radius*.8f,Vec3{1,1,.8f},Vec3{.8f,.75f,.3f},1,6,4);
            break;
        case ShotKind::RiftOrb:
            sphere(plain[2],p,s.radius*1.25f,Vec3{.6f,.78f,.4f},Vec3{.55f,.72f,.35f},1);
            billboard(sprites[1],p,s.radius*3.2f,spin,Vec3{.5f,.15f,.85f},1,EffectSprite::Ring);
            billboard(sprites[1],p,s.radius*2.0f,-spin,Vec3{.7f,.35f,1},1,EffectSprite::Ring);
            break;
        }
        ++rendered_element_effects_;
    }
    // ---- Ice spikes rising from the frost bloom.
    for(const auto& s:el.spikes()) {
        if(s.delay)continue;
        const float t=float(s.age)+alpha;const float grow=std::min(1.0f,t/6)*std::clamp((float(s.lifetime)-t)/16,0.0f,1.0f);
        if(grow<=0)continue;
        crystal(s.base,s.axis,s.height*grow,s.width,Vec3{.55f,.76f,.98f},Vec3{.97f,1,1},s.yaw,.05f);
    }
    // ---- Geysers: translucent water columns with foam caps.
    for(const auto& gy:el.geysers()) {
        if(gy.delay)continue;
        const float t=float(gy.age)+alpha,fade=std::clamp((float(gy.lifetime)-t)/12,0.0f,1.0f),h=std::min(3.2f,t*.25f)*fade;
        for(unsigned k=0;k<8;++k) {
            const float a=k*std::numbers::pi_v<float>/4+material_ticks*.15f,b=(k+1)*std::numbers::pi_v<float>/4+material_ticks*.15f;
            const auto lo_a=gy.base+Vec3{std::cos(a)*.42f,0,std::sin(a)*.42f},lo_b=gy.base+Vec3{std::cos(b)*.42f,0,std::sin(b)*.42f};
            const auto hi_a=gy.base+Vec3{std::cos(a)*.3f,h,std::sin(a)*.3f},hi_b=gy.base+Vec3{std::cos(b)*.3f,h,std::sin(b)*.3f};
            const auto c=Vec3{.55f,.8f,1}*(.75f+.25f*std::cos(a));
            tri(plain[0],lo_a,lo_b,hi_a,c,c,Vec3{.9f,.97f,1},.5f*fade);tri(plain[0],lo_b,hi_b,hi_a,c,Vec3{.9f,.97f,1},Vec3{.9f,.97f,1},.5f*fade);
        }
        billboard(sprites[0],gy.base+Vec3{0,h,0},.75f,material_ticks*.1f,Vec3{.95f,.98f,1},.8f*fade,EffectSprite::Foam);
        ++rendered_element_effects_;
    }
    // ---- Ground decals: frost, wet stone, scorch and void stains.
    for(const auto& d:el.decals()) {
        const float fade=std::clamp((float(d.lifetime)-float(d.age)-alpha)/40,0.0f,1.0f)*std::min(1.0f,(float(d.age)+alpha)/6);
        for(const auto& f:d.faces) {
            if(d.blend==0)tri(plain[0],f[0],f[1],f[2],d.color,d.color,d.color,.5f*fade);
            else tri(plain[d.blend],f[0],f[1],f[2],d.color*fade,d.color*fade,d.color*fade);
        }
    }
    // ---- Melee swipe trails (retail Superfly wing-trail texture, tinted).
    for(const auto& trail:el.melee_trails()) {
        const auto& pts=trail.points;if(pts.size()<2)continue;
        // A tapering swoosh: a soft wide band in the trail colour around a thin
        // bright core, both additive, fading toward the oldest sample.
        const auto core=lerp(trail.color,Vec3{1,1,1},.55f);
        for(std::size_t i=1;i<pts.size();++i) {
            const float f0=1-float(i-1)/pts.size(),f1=1-float(i)/pts.size();
            ribbon(plain[1],pts[i-1],pts[i],.16f*f0,.16f*f1,trail.color*(.55f*f0),trail.color*(.55f*f1));
            ribbon(plain[1],pts[i-1],pts[i],.04f*f0,.04f*f1,core*f0,core*f1);
        }
        ++rendered_melee_trails_;
    }
    // ---- Dark Spyro: glowing white eyes, eye-light trails, black smoke.
    std::vector<RenderVertex> smoke;
    if(el.dark(gs)) {
        const auto eyes=spyro_eye_centers(model,pose,&clip);
        if(eyes)for(unsigned e=0;e<2;++e) {
            const auto c=position+retail::rotate_model((*eyes)[e],angle,pitch,roll);
            for(unsigned k=0;k<10;++k) {
                const float a=k*2*std::numbers::pi_v<float>/10,b=(k+1)*2*std::numbers::pi_v<float>/10;
                tri(plain[1],c,c+(side_axis*std::cos(a)+up_axis*std::sin(a))*.055f,c+(side_axis*std::cos(b)+up_axis*std::sin(b))*.055f,
                    Vec3{.95f,.95f,1},Vec3{0,0,0},Vec3{0,0,0});
            }
            billboard(sprites[1],c,.03f,0,Vec3{1,1,1},1,EffectSprite::Mote);
            const auto& trail=el.eye_trails()[e];
            if(trail.size()>1) {
                Vec3 previous=c;
                for(std::size_t i=1;i<trail.size();++i) {
                    const float f0=1-float(i-1)/trail.size(),f1=1-float(i)/trail.size();
                    ribbon(plain[1],previous,trail[i],.012f*f0,.012f*f1,Vec3{.42f,.40f,.5f}*f0,Vec3{.25f,.2f,.4f}*f1);previous=trail[i];
                }
            }
            ++rendered_eye_glows_;
        }
        if(gs.cheats.dark_smoke && charge_smoke_texture_)for(const auto& puff:el.smoke.sample(alpha)) {
            if(dot(puff.position-eye,view)<.05f)continue;
            const float r=puff.radius*.7f;
            // Retail puffs grow from 24 to 86 quarter-units; darkness fades as they spread.
            const float age=std::clamp((puff.radius*retail::units_per_world/4-24)/62,0.0f,1.0f);
            const float k=.40f*(1-.8f*age);
            const auto a=(side_axis*std::cos(puff.angle)+up_axis*std::sin(puff.angle))*r,b=(up_axis*std::cos(puff.angle)-side_axis*std::sin(puff.angle))*r;
            const std::array<Vec3,4> q{puff.position-a-b,puff.position+a-b,puff.position-a+b,puff.position+a+b};
            for(const unsigned c:{0u,1u,2u,1u,3u,2u})smoke.push_back({q[c],k,k,k*.82f,1,float(c&1),float(c>>1),1});   // less blue removed: violet-black
            ++rendered_dark_smoke_;
        }
    }
    // ---- Demon eruptions: subtractive pools, glowing cracks, floating rifts.
    for(const auto& e:gs.demons.eruptions()) {
        const float t=std::clamp((float(e.age)+alpha)/e.lifetime,0.0f,1.0f);
        const float open=std::sin(std::min(1.0f,t*1.6f)*std::numbers::pi_v<float>*.5f)*(1-std::max(0.0f,t-.7f)/.3f);
        if(e.floating) {
            const auto c=e.center+Vec3{0,1.5f,0};
            billboard(sprites[1],c,1.1f*open,material_ticks*.08f,Vec3{.5f,.15f,.85f},1,EffectSprite::Ring);
            billboard(sprites[2],c,.8f*open,0,Vec3{.55f,.72f,.35f},1,EffectSprite::Mote);
        } else {
            const auto c=e.center+Vec3{0,.04f,0};
            for(unsigned k=0;k<16;++k) {
                const float a=k*2*std::numbers::pi_v<float>/16,b=(k+1)*2*std::numbers::pi_v<float>/16;
                tri(plain[2],c,c+Vec3{std::sin(a),0,std::cos(a)}*e.radius*open,c+Vec3{std::sin(b),0,std::cos(b)}*e.radius*open,
                    Vec3{.62f,.8f,.45f}*open,Vec3{0,0,0},Vec3{0,0,0});
            }
            for(const auto& seg:e.cracks)ribbon(plain[1],seg[0],seg[1],.035f*open,.02f*open,Vec3{.6f,.2f,1}*open,Vec3{.4f,.1f,.8f}*open);
        }
        ++rendered_demon_eruptions_;
    }
    // ---- Submit: opaque crystals first, then subtractive, half, additive.
    context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    draw_vertices(solid);
    context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
    auto* sprite_map=element_sprite_texture_.Get();
    for(const unsigned mode:{2u,0u,3u,1u}) {
        context_->OMSetBlendState(retail_blends_[mode].Get(),nullptr,0xffffffff);
        if(!plain[mode].empty())draw_vertices(plain[mode]);
        if(!sprites[mode].empty()){context_->PSSetShaderResources(0,1,&sprite_map);draw_vertices(sprites[mode]);}
        if(mode==2 && !smoke.empty()){auto* smoke_map=charge_smoke_texture_.Get();context_->PSSetShaderResources(0,1,&smoke_map);draw_vertices(smoke);}
        if(mode==0 && !ice_tiles.empty() && ice_texture_) {
            context_->OMSetBlendState(retail_blends_[1].Get(),nullptr,0xffffffff);
            auto* ice_map=ice_texture_.Get();context_->PSSetShaderResources(0,1,&ice_map);draw_vertices(ice_tiles);
            context_->OMSetBlendState(retail_blends_[mode].Get(),nullptr,0xffffffff);
        }
    }
    context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    rendered_element_effects_+=static_cast<unsigned>(el.particles().size());
}
