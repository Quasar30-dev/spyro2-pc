// Retail reference: 800495e8 interpolates Spyro's frame-dependent contour;
// 80043858/800499d4 select mobys with signed +1C shadow data and orient their
// shadows to the floor. PC: use the actual interpolated mesh silhouette in a
// vertical depth map, sampled on receiving terrain. No invented flat decal.
void Renderer::initialize_character_shadows() {
    static constexpr char shader[]=R"(
cbuffer Camera : register(b0) {float4x4 viewProjection;};
struct Vertex {float3 position:POSITION;float4 color:COLOR;float3 uv:TEXCOORD;};
float4 vs_shadow(Vertex v):SV_POSITION {return mul(float4(v.position,1),viewProjection);}
)";
    ComPtr<ID3DBlob> code,error;
    checked(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs_shadow","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error),"Compile character shadow shader");
    checked(device_->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shadow_vs_),"Create character shadow shader");
    D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=shadow_map_size_;texture.MipLevels=texture.ArraySize=1;
    texture.Format=DXGI_FORMAT_R32_TYPELESS;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;
    texture.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    checked(device_->CreateTexture2D(&texture,nullptr,&shadow_depth_),"Create character shadow map");
    D3D11_DEPTH_STENCIL_VIEW_DESC depth{};depth.Format=DXGI_FORMAT_D32_FLOAT;depth.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    checked(device_->CreateDepthStencilView(shadow_depth_.Get(),&depth,&shadow_depth_view_),"Create character shadow depth view");
    D3D11_SHADER_RESOURCE_VIEW_DESC resource{};resource.Format=DXGI_FORMAT_R32_FLOAT;resource.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;resource.Texture2D.MipLevels=1;
    checked(device_->CreateShaderResourceView(shadow_depth_.Get(),&resource,&shadow_depth_resource_),"Create character shadow resource");
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(shadow_values_);buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    checked(device_->CreateBuffer(&buffer,nullptr,&shadow_constants_),"Create character shadow constants");
    // Compare before bilinear filtering. Filtering raw depths interpolates
    // empty space with a caster, which expands or breaks its silhouette.
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
    for(auto& channel:sampler.BorderColor)channel=1;
    sampler.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;sampler.MaxAnisotropy=1;
    sampler.MaxLOD=D3D11_FLOAT32_MAX;
    checked(device_->CreateSamplerState(&sampler,&shadow_sampler_),"Create filtered character shadow sampler");
    enable_character_shadows(false);
}
void Renderer::enable_character_shadows(bool enabled) {
    shadow_values_[16]=enabled && rendered_character_shadows_?1.0f:0.0f;
    shadow_values_[17]=1.0f/shadow_map_size_;shadow_values_[18]=192;shadow_values_[19]=.46f;
    context_->UpdateSubresource(shadow_constants_.Get(),0,nullptr,shadow_values_.data(),0,0);
    auto* buffer=shadow_constants_.Get();context_->PSSetConstantBuffers(2,1,&buffer);
    auto* sampler=shadow_sampler_.Get();context_->PSSetSamplers(1,1,&sampler);
}
void Renderer::draw_character_shadows(const Level& level,Vec3 focus,const Player* player,float alpha,
    const SpyroModel& model,const SpyroPose& pose,unsigned animation,const ObjectModels& models,
    std::span<const RetailObject> instances,SuperchargeAppearance appearance,std::span<const ObjectPose> poses,
    std::span<const RemoteSpyro> remotes,const RiptoBoss* ripto,bool enabled) {
    rendered_character_shadows_=rendered_shadow_triangles_=0;
    if(!enabled){enable_character_shadows(false);return;}
    const Vec3 center=player?lerp(player->previous_position,player->position,alpha):focus;
    // Texel-snapped placement avoids subpixel shimmer as Spyro moves. The
    // 96m window includes offscreen casters at the edge of the camera view.
    constexpr float texel=96.0f/shadow_map_size_;
    const Vec3 anchor{std::floor(center.x/texel)*texel,center.y,std::floor(center.z/texel)*texel};
    using namespace DirectX;
    const auto view=XMMatrixLookAtLH(XMVectorSet(anchor.x,anchor.y+64,anchor.z,1),
        XMVectorSet(anchor.x,anchor.y,anchor.z,1),XMVectorSet(0,0,1,0));
    const auto matrix=XMMatrixTranspose(view*XMMatrixOrthographicLH(96,96,0,192));
    XMFLOAT4X4 values;XMStoreFloat4x4(&values,matrix);std::memcpy(shadow_values_.data(),&values,64);
    std::vector<RenderVertex> vertices;
    const auto append=[&](const auto& faces,std::span<const Vec3> world) {
        const auto before=vertices.size();
        for(const auto& face:faces) {
            constexpr unsigned corners[]{0,1,2,1,3,2};
            bool valid=true;for(unsigned k=0;k<(face.quad?4u:3u);++k)valid&=face.vertices[k]<world.size();
            if(!valid)continue;
            for(unsigned k=0;k<(face.quad?6u:3u);++k)vertices.push_back({world[face.vertices[corners[k]]],0,0,0,1});
        }
        if(vertices.size()>before)++rendered_character_shadows_;
    };
    const auto spyro=[&](const SpyroPose& p,unsigned clip,SuperchargeAppearance look,Vec3 origin,float yaw,float pitch,float roll) {
        if(p.vertices.empty())return;
        std::vector<Vec3> world;world.reserve(p.vertices.size());
        for(const auto v:p.vertices)world.push_back(origin+retail::rotate_model(v,yaw,pitch,roll));
        append(model.clip(clip,look).faces,world);
    };
    const float yaw=player?player->previous_yaw+std::remainder(player->yaw-player->previous_yaw,2*std::numbers::pi_v<float>)*alpha:level.spawn_yaw;
    const float pitch=player?player->previous_pitch+(player->pitch-player->previous_pitch)*alpha:0;
    const float roll=player?player->previous_roll+(player->roll-player->previous_roll)*alpha:0;
    const auto* controlled=element_game && player && element_game->playable_boss.active()?&element_game->playable_boss:nullptr;
    if(!controlled)spyro(pose,animation,appearance,(player?center:level.spawn)+retail::origin_above_feet,yaw,pitch,roll);
    for(const auto& remote:remotes)
        spyro(remote.pose,remote.clip,remote.appearance,remote.player.position+retail::origin_above_feet,
            remote.player.yaw,remote.player.pitch,remote.player.roll);
    const auto bosses=ripto?ripto->visuals():std::span<const RetailObject>{};
    const auto controlled_index=instances.size()+bosses.size();
    for(std::size_t i=0;i<controlled_index+(controlled?1:0);++i) {
        const auto& o=i<instances.size()?instances[i]:i<controlled_index?bosses[i-instances.size()]:controlled->body();
        if(o.hidden || o.script_undrawn || o.collected || o.state<0)continue;
        const auto found=models.models.find(o.type);if(found==models.models.end() || !found->second.animated)continue;
        // The original signed shadow word is the authority for authored
        // mobys. Native enemies/actors and the native boss visuals retain
        // character semantics when they have no authored record.
        const bool npc=std::any_of(level.npcs.begin(),level.npcs.end(),[&](const auto& n){return n.object==o.id;});
        if(!(o.record[0x1f]&0x80) && !o.enemy && !o.boss_body && !o.pc_actor && !npc && i<instances.size())continue;
        // Sparx, flies, particles, portals and fragments are not bodies.
        if(o.type==0x78 || o.type==0x10 || o.type==0xd3 || is_container_fragment(o.type) || o.powerup_gate)continue;
        const auto position=object_position(o,alpha);
        if(std::abs(position.x-anchor.x)>48 || std::abs(position.z-anchor.z)>48)continue;
        const auto& bank=found->second;ObjectPose p;
        if(i<poses.size())p=poses[i];
        else {
            p=sample_object_animation(bank,o.animation,models.animation_blends);
            if(alpha<1) {
                const auto old=sample_object_animation(bank,o.previous_animation,models.animation_blends);
                if(old.vertices.size()==p.vertices.size())for(unsigned k=0;k<p.vertices.size();++k)p.vertices[k]=lerp(old.vertices[k],p.vertices[k],alpha);
            }
        }
        if(ripto && i<controlled_index && o.type==0x2af)ripto->pose(p,alpha);
        if(p.mesh>=bank.meshes.size())continue;
        const auto basis=object_basis(o,alpha);const auto scale=object_model_scale(o);
        for(auto& v:p.vertices)v=position+basis*(v*scale);
        append(bank.meshes[p.mesh].faces,p.vertices);
    }
    rendered_shadow_triangles_=static_cast<unsigned>(vertices.size()/3);
    ID3D11ShaderResourceView* empty=nullptr;context_->PSSetShaderResources(1,1,&empty);
    context_->OMSetRenderTargets(0,nullptr,shadow_depth_view_.Get());
    context_->ClearDepthStencilView(shadow_depth_view_.Get(),D3D11_CLEAR_DEPTH,1,0);
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(shadow_map_size_),static_cast<float>(shadow_map_size_),0,1};context_->RSSetViewports(1,&viewport);
    context_->RSSetState(solid_.Get());context_->OMSetDepthStencilState(depth_state_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,~0u);
    context_->IASetInputLayout(layout_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->UpdateSubresource(matrix_.Get(),0,nullptr,&matrix,0,0);
    auto* constants=matrix_.Get();context_->VSSetConstantBuffers(0,1,&constants);
    context_->VSSetShader(shadow_vs_.Get(),nullptr,0);context_->PSSetShader(nullptr,nullptr,0);
    draw_vertices(vertices);
    context_->OMSetRenderTargets(0,nullptr,nullptr);
    auto* resource=shadow_depth_resource_.Get();context_->PSSetShaderResources(1,1,&resource);
    enable_character_shadows(true);
}
