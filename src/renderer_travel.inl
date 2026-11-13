    if(!studio && !collision && (!level.portals.empty() || !level.whirlwinds.empty() || !level.home_transports.empty())) {
        // Retail panorama layers share the portal plane; drawing them after
        // opaque geometry preserves foreground occlusion and arch depth.
        objects.clear();context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context_->VSSetShader(portal_vs_.Get(),nullptr,0);context_->PSSetShader(sky_ps_.Get(),nullptr,0);
        for(const auto& portal:level.portals)for(const auto& v:portal_picture(portal,eye,normalized(focus-eye),camera_up,vertical_fov,float(width_)/height_))objects.push_back({v.position,v.color.x,v.color.y,v.color.z});
        draw_vertices(objects);objects.clear();
        context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
        const auto wind_forward=normalized(focus-eye),wind_right=normalized(cross(Vec3{0,1,0},wind_forward)),wind_up=cross(wind_forward,wind_right);
        context_->OMSetBlendState(additive_blend_.Get(),nullptr,0xffffffff);
        texture=whirlwind_texture_.Get();context_->PSSetShaderResources(0,1,&texture);
        const auto billboard=[&](Vec3 p,float radius,float wind_angle) {
            const auto x=(wind_right*std::cos(wind_angle)+wind_up*std::sin(wind_angle))*radius,y=(wind_up*std::cos(wind_angle)-wind_right*std::sin(wind_angle))*radius;
            const std::array<Vec3,4> points{p-x-y,p+x-y,p-x+y,p+x+y};
            for(const auto k:{0,1,2,1,3,2})objects.push_back({points[k],1,1,1,1,float(k&1),float(k>>1),1});
        };
        for(const auto& wind:level.whirlwinds)if(wind.active && wind.object<level.objects.size()) {
            const auto base=level.objects[wind.object].position;if(length(base-eye)>100)continue;
            // Independently seeded retail sprites rise through the column.
            // Their random horizontal phase must never be tied to height.
            if(whirlwind_texture_)for(const auto& particle:whirlwind_particles(wind,alpha))
                billboard(base+particle.offset,0.125f,0);
        }
        draw_vertices(objects);objects.clear();
        for(const auto& wind:level.whirlwinds)if(wind.active && wind.object<level.objects.size()) {
            const auto base=level.objects[wind.object].position;if(length(base-eye)>100)continue;
            // Type 52: original blue/white 384-unit vertical line, advancing
            // 192 world units per script update, clipped at the lift ceiling.
            for(const auto& particle:whirlwind_particles(wind,alpha,true)) {
                const auto a=base+particle.offset,b=a+Vec3{0,particle.length,0};
                const auto side=wind_right*0.006f;const std::array<Vec3,4> v{a-side,a+side,b-side,b+side};
                for(const auto k:{0,1,2,1,3,2}){const Vec3 c=k<2?Vec3{112/255.0f,112/255.0f,224/255.0f}:Vec3{224/255.0f,224/255.0f,1};objects.push_back({v[k],c.x,c.y,c.z});}
            }
        }
        for(const auto& h:level.home_transports)if(h.active && h.object<level.objects.size()) {
            const auto base=level.objects[h.object].position;
            for(unsigned i=0;i<24;++i) {
                const float a=i*2.399963f+material_ticks*0.018f,y=std::fmod(material_ticks*0.01f+i*0.073f,2.1f);
                const auto p=base+Vec3{std::sin(a)*0.65f,y,std::cos(a)*0.65f},x=wind_right*0.025f,z=wind_up*0.06f;
                append_triangle(objects,p-x,p+z,p+x,{0.55f,0.65f,1});append_triangle(objects,p-x,p+x,p-z,{0.55f,0.65f,1});
            }
        }
        draw_vertices(objects);objects.clear();context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    }
