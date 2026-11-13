    rendered_sparx_sparkles_=rendered_sparx_glows_=0;
    if(sparx && player && !studio) {
        const auto forward=normalized(focus-eye),right=normalized(cross(camera_up,forward)),up=cross(forward,right);
        const float ty=std::tan(vertical_fov/2),tx=ty*width_/height_;
        context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
        objects.clear();
        // 80048128's type-0 primitive is a short opaque, untextured line.
        // Keep its native screen size at high resolution, with terrain depth.
        for(const auto& spark:sparx->sparkles(alpha)) {
            const float depth=dot(spark.position-eye,forward);if(depth<0.05f || depth>128)continue;
            const auto x=right*(depth*tx*2/512),y=up*(depth*ty/240);
            append_triangle(objects,spark.position-x-y,spark.position+x-y,spark.position-x+y,spark.color);
            append_triangle(objects,spark.position+x-y,spark.position+x+y,spark.position-x+y,spark.color);
            ++rendered_sparx_sparkles_;
        }
        draw_vertices(objects);objects.clear();
        const auto index=sparx->object_index();
        if(player->health>=3 && index && *index<retail_objects.size()) {
            const auto& object=retail_objects[*index];const auto bank=object_models.models.find(0x78);
            if(object.state>=0 && bank!=object_models.models.end() && bank->second.attachment_vertices) {
                const auto& model=bank->second;
                const auto local=lerp(sample_object_attachment(model,object.previous_animation,object_models.animation_blends),
                    sample_object_attachment(model,object.animation,object_models.animation_blends),alpha);
                const auto center=object_position(object,alpha)+object_basis(object,alpha)*(local*object_model_scale(object));
                if(dot(center-eye,forward)>0.05f) {
                    const auto& shape=object_models.sparx_glows[std::min(player->health>=4?player->health-1:0,3u)];
                    const float angle=sparx->glow_angle(alpha),c=std::cos(angle),s=std::sin(angle);
                    const auto vertex=[&](unsigned i) {
                        const auto p=shape.outline[i];
                        return center+right*((-p.x*c+p.y*s)*1.5f/retail::units_per_world)+up*((p.x*s+p.y*c)/retail::units_per_world);
                    };
                    // 8004a87c / 8004a8d4: RGB 90,90,48 at the centre,
                    // black outer vertices, additive GP0 32 triangles.
                    for(const auto& face:shape.faces) {
                        objects.push_back({center,144/255.0f,144/255.0f,72/255.0f});
                        objects.push_back({vertex(face[0]),0,0,0});objects.push_back({vertex(face[1]),0,0,0});
                    }
                    context_->OMSetBlendState(additive_blend_.Get(),nullptr,0xffffffff);draw_vertices(objects);++rendered_sparx_glows_;
                }
            }
        }
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    }
