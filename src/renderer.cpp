#include "spyro/renderer.hpp"
#include "spyro/travel.hpp"
#include "spyro/element_art.hpp"
#include "spyro/spyro_dark.hpp"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <array>
#include <fstream>
#include <cstring>
#include <chrono>
#include <numbers>

using Microsoft::WRL::ComPtr;
namespace spyro {
namespace {
void checked(HRESULT result,const char* operation) {if (FAILED(result))throw FormatError(std::string(operation)+" failed (HRESULT "+std::to_string(result)+")");}
RenderVertex vertex(Vec3 p,Vec3 color) {return {p,color.x,color.y,color.z,1};}
void append_triangle(std::vector<RenderVertex>& out,Vec3 a,Vec3 b,Vec3 c,Vec3 color) {
    out.push_back(vertex(a,color));out.push_back(vertex(b,color));out.push_back(vertex(c,color));
}
void diamond(std::vector<RenderVertex>& out,Vec3 center,float radius,float height,Vec3 color) {
    for(unsigned side=0;side<4;++side) {
        const float angle=static_cast<float>(side)*1.570796327f;
        const float next=angle+1.570796327f;
        const auto a=center+Vec3{std::cos(angle)*radius,0,std::sin(angle)*radius};
        const auto b=center+Vec3{std::cos(next)*radius,0,std::sin(next)*radius};
        const float light=0.7f+0.1f*static_cast<float>(side);
        append_triangle(out,a,b,center+Vec3{0,height,0},color*light);
        append_triangle(out,b,a,center-Vec3{0,height*0.55f,0},color*light);
    }
}
// The shader's Light buffer (b1). Null: neutral, as in every level but the
// Crownworks. Bands split the tiers at 1.75 (lower ring / terrace) and 5.25
// (terrace / crown walk); below -0.65 (the molten pit) stays ungraded; the
// core light sits at the arena centre.
LightConstants light_constants(const RiptoBoss* boss,float alpha=1) {
    const auto v=[](Vec3 c,float w=1){return std::array<float,4>{c.x,c.y,c.z,w};};
    LightConstants k{};const CrownLight l=boss?boss->light(alpha):CrownLight{};
    k.values={v(l.lower),v(l.terrace),v(l.walk),v(l.sky),v(boss?boss->center:Vec3{},l.core_radius),v(l.core,boss?1.f:0.f),v({1.75f,5.25f,-.65f},0),
        v(l.lower_cast),v(l.terrace_cast),v(l.walk_cast),v(l.sky_cast)};
    return k;
}
// The clear colour behind the sky takes the same sky grade as ps_sky.
Vec3 graded_sky(Vec3 c,const CrownLight& l) {
    const float lum=c.x*.3f+c.y*.59f+c.z*.11f;
    return Vec3{c.x*l.sky.x,c.y*l.sky.y,c.z*l.sky.z}+l.sky_cast*(.5f+lum*8);
}
}
#include "renderer_backrealms.inl"
#include "renderer_spyro_armor.inl"
#include "renderer_shadows.inl"
void Renderer::initialize(ID3D11Device* device,ID3D11DeviceContext* context) {
    device_=device;context_=context;
    static const char* shader=R"(
cbuffer Camera : register(b0) { float4x4 viewProjection; };
// Crownworks phase light (identity everywhere else): tints for the lower
// ring, terrace and crown walk by height, the sky, and a light at the core.
cbuffer Light : register(b1) { float4 lowTint; float4 midTint; float4 highTint; float4 skyTint; float4 core; float4 coreColor; float4 bands;
    float4 lowCast; float4 midCast; float4 highCast; float4 skyCast; };
Texture2D retailTexture : register(t0);
Texture2D<float> characterShadow : register(t1);
cbuffer Shadow : register(b2) {float4x4 shadowProjection;float4 shadowSettings;};
SamplerState nearestSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);
struct Vertex { float3 position : POSITION; float4 color : COLOR; float3 uvTexture : TEXCOORD; };
struct Fragment { float4 position : SV_POSITION; float4 color : COLOR; float3 uvTexture : TEXCOORD; float3 world : TEXCOORD1; };
Fragment vs_main(Vertex v) { Fragment f; f.position=mul(float4(v.position,1),viewProjection); f.color=v.color; f.uvTexture=v.uvTexture; f.world=v.position; return f; }
float3 grade(float3 c,float3 p) {
    float low=1-smoothstep(bands.x-.6,bands.x+.6,p.y),high=smoothstep(bands.y-.6,bands.y+.6,p.y),mid=saturate(1-low-high);
    float3 t=lowTint.rgb*low+highTint.rgb*high+midTint.rgb*mid;
    float3 cast=lowCast.rgb*low+highCast.rgb*high+midCast.rgb*mid;
    float d=length(p.xz-core.xz);cast+=coreColor.rgb*coreColor.a*saturate(1-d/max(core.w,.01));
    // Below the lowest floor (the molten pit, flooded tunnels) keeps its own
    // light: lava glows by itself.
    float pit=1-smoothstep(bands.z-.25,bands.z+.25,p.y);
    t=lerp(t,float3(1,1,1),pit);cast*=1-pit;
    return c*t+cast*dot(c,float3(.3,.59,.11))*2.2;
}
struct SkyFragment {float4 position : SV_POSITION; noperspective float4 color : COLOR;};
SkyFragment vs_sky(Vertex v) {
    SkyFragment f; f.position=mul(float4(v.position,1),viewProjection);
    f.position.z=f.position.w; f.color=v.color; return f;
}
float4 ps_sky(SkyFragment f) : SV_TARGET {
    float l=dot(f.color.rgb,float3(.3,.59,.11));
    return saturate(float4(f.color.rgb*skyTint.rgb+skyCast.rgb*(.5+l*8),f.color.a));
}
SkyFragment vs_portal(Vertex v) {SkyFragment f;f.position=mul(float4(v.position,1),viewProjection);f.color=v.color;return f;}
float4 ps_main(Fragment f) : SV_TARGET {
    if(f.uvTexture.z>4.5) {
        // Cooled crater (PC Water/Ice): dark basalt with faint cold seams,
        // tinted by the vertex colour (frost or stone).
        float2 p=f.uvTexture.xy;
        float flow=sin(p.x*5+sin(p.y*3)*1.7)*cos(p.y*4+sin(p.x*2)*1.2);
        float seam=1-smoothstep(.02,.12,abs(flow));
        float grain=.5+.5*sin(p.x*23+p.y*17);
        return float4(saturate(f.color.rgb*(.55+.12*grain)+seam*float3(.08,.1,.14)),1);
    }
    if(f.uvTexture.z>3.5) {
        // Animated molten seams with dark floating crust. UVs are world-space,
        // so the lava stays coherent across the newly tessellated floor.
        float2 p=f.uvTexture.xy;
        float flow=sin(p.x*5+sin(p.y*3)*1.7)*cos(p.y*4+sin(p.x*2)*1.2);
        float vein=1-smoothstep(.035,.23,abs(flow));
        float heat=.5+.5*sin(p.x*11+p.y*8+flow*4);
        return float4(saturate(lerp(float3(.21,.012,.008),float3(1,.25+.5*heat,.015),vein)*f.color.rgb),1);
    }
    float4 color=f.color;
    if(f.uvTexture.z>0.5) {
        float4 texel=retailTexture.Sample(nearestSampler,f.uvTexture.xy);clip(texel.a-0.01);
        if(f.uvTexture.z>2.5)clip(texel.a-0.75);
        else if(f.uvTexture.z>1.5)clip(0.75-texel.a);
        texel.a=1;color*=texel;
    }
    color.rgb=grade(color.rgb,f.world);
    if(shadowSettings.x>0.5) {
        float4 projected=mul(float4(f.world,1),shadowProjection);
        float2 uv=projected.xy*float2(.5,-.5)+.5;
        float shade=0;
        if(all(uv>.002) && all(uv<.998) && projected.z>0 && projected.z<1) {
            // A silhouette only shades surfaces below it; nearby geometry
            // does not self-darken through overlapping caster triangles.
            [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
                float2 tap=uv+float2(x,y)*shadowSettings.y;
                float depth=characterShadow.SampleLevel(nearestSampler,tap,0);
                float drop=(projected.z-depth)*shadowSettings.z;
                // Hardware PCF interpolates four depth comparisons, giving
                // continuous sub-texel coverage instead of nine hard steps.
                // The 1/2/1 kernel preserves thin wings and tail outlines.
                float weight=(x==0?2:1)*(y==0?2:1);
                float lit=characterShadow.SampleCmpLevelZero(shadowSampler,tap,projected.z-.025/shadowSettings.z);
                shade+=weight*(1-lit)*(1-smoothstep(3,12,max(drop,0)));
            }
        }
        float edge=saturate(min(min(uv.x,uv.y),min(1-uv.x,1-uv.y))*48);
        color.rgb*=1-shadowSettings.w*(shade/16)*edge;
    }
    return saturate(color);
}
)";
    ComPtr<ID3DBlob> v,p,error;
    checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"vs_main","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&v,&error),"Compile vertex shader");
    checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"ps_main","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&p,&error),"Compile pixel shader");
    checked(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs_),"Create vertex shader");
    checked(device_->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps_),"Create pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0}};
    checked(device_->CreateInputLayout(elements,3,v->GetBufferPointer(),v->GetBufferSize(),&layout_),"Create vertex layout");
    v.Reset();p.Reset();
    checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"vs_sky","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&v,&error),"Compile sky vertex shader");
    checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"ps_sky","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&p,&error),"Compile sky pixel shader");
    checked(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&sky_vs_),"Create sky vertex shader");
    checked(device_->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&sky_ps_),"Create sky pixel shader");
    v.Reset();checked(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"vs_portal","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&v,&error),"Compile portal vertex shader");
    checked(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&portal_vs_),"Create portal vertex shader");
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    checked(device_->CreateSamplerState(&sampler,&point_sampler_),"Create retail texture sampler");
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=64;buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    checked(device_->CreateBuffer(&buffer,nullptr,&matrix_),"Create camera buffer");
    buffer.ByteWidth=sizeof(LightConstants);
    const auto neutral=light_constants(nullptr);D3D11_SUBRESOURCE_DATA light{&neutral,0,0};
    checked(device_->CreateBuffer(&buffer,&light,&light_),"Create light buffer");
    buffer.ByteWidth=sizeof(RenderVertex)*65536;buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    checked(device_->CreateBuffer(&buffer,nullptr,&dynamic_buffer_),"Create dynamic mesh buffer");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    checked(device_->CreateRasterizerState(&raster,&solid_),"Create raster state");
    raster.FillMode=D3D11_FILL_WIREFRAME;raster.DepthBias=-1;
    checked(device_->CreateRasterizerState(&raster,&wire_),"Create wireframe state");
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    checked(device_->CreateDepthStencilState(&depth,&depth_state_),"Create depth state");
    depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    checked(device_->CreateDepthStencilState(&depth,&transparent_depth_),"Create transparent depth state");
    D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];rt.BlendEnable=TRUE;
    rt.SrcBlend=D3D11_BLEND_ONE;rt.DestBlend=D3D11_BLEND_ONE;rt.BlendOp=D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha=D3D11_BLEND_ZERO;rt.DestBlendAlpha=D3D11_BLEND_ONE;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    checked(device_->CreateBlendState(&blend,&additive_blend_),"Create retail additive blend state");
    {
        // Original procedural PS1-style element sprites (no retail assets).
        const auto atlas=element_sprite_atlas();
        D3D11_TEXTURE2D_DESC sprites{};sprites.Width=atlas.width;sprites.Height=atlas.height;sprites.MipLevels=1;sprites.ArraySize=1;
        sprites.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sprites.SampleDesc.Count=1;sprites.Usage=D3D11_USAGE_IMMUTABLE;sprites.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA pixels{};pixels.pSysMem=atlas.rgba.data();pixels.SysMemPitch=atlas.width*4;
        ComPtr<ID3D11Texture2D> texture;checked(device_->CreateTexture2D(&sprites,&pixels,&texture),"Upload element sprites");
        checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&element_sprite_texture_),"Create element sprite view");
    }
    for(unsigned mode=0;mode<4;++mode) {
        rt.SrcBlend=mode==0 || mode==3?D3D11_BLEND_SRC_ALPHA:D3D11_BLEND_ONE;
        rt.DestBlend=mode==0?D3D11_BLEND_INV_SRC_ALPHA:D3D11_BLEND_ONE;
        rt.BlendOp=mode==2?D3D11_BLEND_OP_REV_SUBTRACT:D3D11_BLEND_OP_ADD;
        checked(device_->CreateBlendState(&blend,&retail_blends_[mode]),"Create retail material blend state");
    }
    initialize_character_shadows();
}
void Renderer::upload_spyro(const SpyroModel& model) {
    D3D11_TEXTURE2D_DESC descriptor{};descriptor.Width=model.texture_width;descriptor.Height=model.texture_height;
    descriptor.MipLevels=1;descriptor.ArraySize=1;descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.SampleDesc.Count=1;descriptor.Usage=D3D11_USAGE_IMMUTABLE;descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=model.texture_rgba.data();data.SysMemPitch=model.texture_width*4;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    checked(device_->CreateTexture2D(&descriptor,&data,&texture),"Upload Spyro's retail textures");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&view),"Create Spyro texture view");spyro_texture_=view;
    {
        // Dark Spyro: CLUT-level remap of the mounted palette plus white eyes.
        const auto dark=dark_spyro_atlas(model);data.pSysMem=dark.data();dark_keep_=dark_spyro_keep(model);
        ComPtr<ID3D11Texture2D> dark_texture;checked(device_->CreateTexture2D(&descriptor,&data,&dark_texture),"Upload Dark Spyro palette");
        dark_spyro_texture_.Reset();checked(device_->CreateShaderResourceView(dark_texture.Get(),nullptr,&dark_spyro_texture_),"Create Dark Spyro view");
        data.pSysMem=model.texture_rgba.data();
    }
    if(backrealms_armor && !backrealms_armor_texture_) {
        // Dedicated material, separate from every mounted retail atlas.
        const auto relative=std::filesystem::path("data/expansions/backrealms/armor-steel.rgba");
        auto path=relative;wchar_t module[MAX_PATH]{};GetModuleFileNameW(nullptr,module,MAX_PATH);
        auto folder=std::filesystem::path(module).parent_path();
        for(unsigned depth=0;!std::filesystem::exists(path) && depth<5;++depth,folder=folder.parent_path())path=folder/relative;
        std::ifstream in(path,std::ios::binary);Bytes pixels(128*128*4);
        if(!in.read(reinterpret_cast<char*>(pixels.data()),pixels.size()) || in.peek()!=std::ifstream::traits_type::eof())
            throw FormatError("Backrealms requires its 128x128 steel material: data/expansions/backrealms/armor-steel.rgba");
        descriptor.Width=descriptor.Height=128;data.pSysMem=pixels.data();data.SysMemPitch=128*4;texture.Reset();
        checked(device_->CreateTexture2D(&descriptor,&data,&texture),"Upload Backrealms steel material");
        checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&backrealms_armor_texture_),"Create armour material view");
    }
    charge_smoke_texture_.Reset();
    if(!model.charge_smoke.texture_rgba.empty()) {
        descriptor.Width=model.charge_smoke.width;descriptor.Height=model.charge_smoke.height;
        data.pSysMem=model.charge_smoke.texture_rgba.data();data.SysMemPitch=descriptor.Width*4;
        texture.Reset();checked(device_->CreateTexture2D(&descriptor,&data,&texture),"Upload supercharge smoke texture");
        checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&charge_smoke_texture_),"Create supercharge smoke texture view");
    }
}
void Renderer::upload_ice_texture(const Bytes& rgba) {
    ice_texture_.Reset();
    if(rgba.size()!=64*64*4)return;
    D3D11_TEXTURE2D_DESC descriptor{};descriptor.Width=descriptor.Height=64;descriptor.MipLevels=1;descriptor.ArraySize=1;
    descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;descriptor.SampleDesc.Count=1;descriptor.Usage=D3D11_USAGE_IMMUTABLE;
    descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=rgba.data();data.SysMemPitch=64*4;
    ComPtr<ID3D11Texture2D> texture;checked(device_->CreateTexture2D(&descriptor,&data,&texture),"Upload ice texture");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&ice_texture_),"Create ice texture view");
}
void Renderer::upload_objects(const ObjectModels& model) {
    flame_texture_.Reset();
    if(!model.flame.texture_rgba.empty()) {
        D3D11_TEXTURE2D_DESC descriptor{};descriptor.Width=FlameAssets::texture_width;descriptor.Height=FlameAssets::texture_height;
        descriptor.MipLevels=1;descriptor.ArraySize=1;descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        descriptor.SampleDesc.Count=1;descriptor.Usage=D3D11_USAGE_IMMUTABLE;descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=model.flame.texture_rgba.data();data.SysMemPitch=FlameAssets::texture_width*4;
        ComPtr<ID3D11Texture2D> texture;
        checked(device_->CreateTexture2D(&descriptor,&data,&texture),"Upload retail flame texture");
        checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&flame_texture_),"Create flame texture view");
    }
    object_texture_.Reset();if(model.texture_rgba.empty())return;
    D3D11_TEXTURE2D_DESC descriptor{};descriptor.Width=model.texture_width;descriptor.Height=model.texture_height;
    descriptor.MipLevels=1;descriptor.ArraySize=1;descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.SampleDesc.Count=1;descriptor.Usage=D3D11_USAGE_IMMUTABLE;descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=model.texture_rgba.data();data.SysMemPitch=model.texture_width*4;
    ComPtr<ID3D11Texture2D> texture;
    checked(device_->CreateTexture2D(&descriptor,&data,&texture),"Upload retail object textures");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&object_texture_),"Create object texture view");
}
void Renderer::upload_hud(const HudAssets& hud) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=hud.width;desc.Height=hud.height;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA pixels{hud.rgba.data(),hud.width*4,0};ComPtr<ID3D11Texture2D> texture;
    checked(device_->CreateTexture2D(&desc,&pixels,&texture),"Upload retail HUD sprites");hud_texture_.Reset();
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&hud_texture_),"Create HUD sprite view");
}
void Renderer::upload_powerups(const PowerupAssets& assets) {
    const auto upload=[&](const ChargeSmokeAssets& a,ComPtr<ID3D11ShaderResourceView>& view) {
        view.Reset();if(a.texture_rgba.empty())return;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=a.width;desc.Height=a.height;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA pixels{a.texture_rgba.data(),a.width*4,0};ComPtr<ID3D11Texture2D> texture;
        checked(device_->CreateTexture2D(&desc,&pixels,&texture),"Upload power-up effect");
        checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&view),"Create power-up effect view");
    };
    upload(assets.wing_trail,wing_texture_);upload(assets.green_particle,power_particle_texture_);upload(assets.fire_particle,power_fire_texture_);
    upload(assets.bubble_particle,power_bubble_texture_);upload(assets.sparkle_particle,power_sparkle_texture_);upload(assets.gate_particle,power_gate_texture_);
    upload(assets.whirlwind_particle,whirlwind_texture_);
}
void Renderer::upload_level(const Level& level) {
    upload_splats(level);
    sky_buffer_.Reset();
    if(!level.sky_vertices.empty()) {
        std::vector<RenderVertex> sky;sky.reserve(level.sky_vertices.size());
        for(const auto& v:level.sky_vertices)sky.push_back(vertex(v.direction,v.color));
        D3D11_BUFFER_DESC descriptor{};descriptor.ByteWidth=static_cast<UINT>(sky.size()*sizeof(RenderVertex));
        descriptor.Usage=D3D11_USAGE_IMMUTABLE;descriptor.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=sky.data();
        checked(device_->CreateBuffer(&descriptor,&data,&sky_buffer_),"Upload retail sky geometry");
    }
    upload_level_geometry(level);
    if(level.visual_vertices.empty())return;
    D3D11_TEXTURE2D_DESC texture_desc{};texture_desc.Width=level.texture_width;texture_desc.Height=level.texture_height;
    texture_desc.MipLevels=1;texture_desc.ArraySize=1;texture_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count=1;texture_desc.Usage=D3D11_USAGE_IMMUTABLE;texture_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA pixels{};pixels.pSysMem=level.texture_rgba.data();pixels.SysMemPitch=level.texture_width*4;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    checked(device_->CreateTexture2D(&texture_desc,&pixels,&texture),"Upload retail terrain textures");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&view),"Create terrain texture view");level_texture_=view;
}
void Renderer::upload_level_geometry(const Level& level) {
    geometry_source_=&level;geometry_revision_=level.terrain_revision;
    transparent_sources_.clear();terrain_cache_.clear();collision_cache_.clear();
    transparent_terrain_.clear();
    std::vector<RenderVertex> vertices;vertices.reserve(level.triangles.size()*3);
    for (const auto& t:level.triangles) {
        const auto n=normalized(cross(t.b-t.a,t.c-t.a));
        const float height=std::clamp(((t.a.y+t.b.y+t.c.y)/3-level.bounds.min.y)/std::max(1.0f,level.bounds.max.y-level.bounds.min.y),0.0f,1.0f);
        const auto base=lerp(Vec3{0.15f,0.31f,0.36f},Vec3{0.61f,0.52f,0.75f},height);
        const float light=0.38f+0.62f*std::abs(dot(n,normalized(Vec3{0.4f,1,0.25f})));
        append_triangle(vertices,t.a,t.b,t.c,base*light);
    }
    if (vertices.empty()) throw FormatError("Level has no renderable collision triangles");
    D3D11_BUFFER_DESC descriptor{};descriptor.ByteWidth=static_cast<UINT>(vertices.size()*sizeof(RenderVertex));
    descriptor.Usage=D3D11_USAGE_DEFAULT;descriptor.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=vertices.data();
    collision_cache_=vertices;
    ComPtr<ID3D11Buffer> next;checked(device_->CreateBuffer(&descriptor,&data,&next),"Upload retail collision mesh");
    collision_buffer_=next;collision_vertices_=static_cast<unsigned>(vertices.size());
    if(level.visual_vertices.empty()) {
        level_buffer_=next;level_vertices_=level.splats.empty() && level.splat_surface.vertices.empty()?collision_vertices_:0;
        level_texture_.Reset();return;
    }
    vertices.clear();vertices.reserve(level.visual_vertices.size());
    if(level.visual_vertices.size()%3)throw FormatError("Incomplete terrain triangle");
    for(std::size_t i=0;i<level.visual_vertices.size();i+=3) {
        const auto material=level.visual_vertices[i].material;
        if(material>=level.terrain_materials.size())throw FormatError("Missing terrain material");
        const auto& blend=level.terrain_materials[material];
        if(blend.blend_mode>3)throw FormatError("Invalid terrain blend mode");
        TransparentTerrainTriangle transparent;transparent.blend_mode=blend.blend_mode;
        const float opacity=blend.blend_mode==0?0.5f:(blend.blend_mode==3?0.25f:1.0f);
        for(unsigned corner=0;corner<3;++corner) {
            const auto& v=level.visual_vertices[i+corner];
            if(v.material!=material)throw FormatError("Terrain triangle crosses material boundaries");
            // Mixed STP/opaque textures need both passes, just like Spyro's
            // native materials. Opaque terrain ignores STP, as the PSX did.
            vertices.push_back({v.position,v.color.x,v.color.y,v.color.z,1,v.u,v.v,blend.translucent?3.0f:1.0f});
            transparent.vertices[corner]={v.position,v.color.x,v.color.y,v.color.z,opacity,v.u,v.v,2};
            transparent.center+=v.position/3.0f;
        }
        if(blend.translucent){transparent_terrain_.push_back(transparent);transparent_sources_.push_back(static_cast<unsigned>(i));}
    }
    descriptor.ByteWidth=static_cast<UINT>(vertices.size()*sizeof(RenderVertex));data.pSysMem=vertices.data();
    next.Reset();checked(device_->CreateBuffer(&descriptor,&data,&next),"Upload retail terrain mesh");
    terrain_cache_=vertices;
    level_buffer_=next;level_vertices_=static_cast<unsigned>(vertices.size());
}
void Renderer::upload_interface(const HudAssets& assets) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=assets.width;desc.Height=assets.height;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA pixels{assets.rgba.data(),assets.width*4,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    checked(device_->CreateTexture2D(&desc,&pixels,&texture),"Upload retail interface");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&view),"Create retail interface view");interface_texture_=std::move(view);
}
void Renderer::upload_upgrades(const HudAssets& assets) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=assets.width;desc.Height=assets.height;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA pixels{assets.rgba.data(),assets.width*4,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    checked(device_->CreateTexture2D(&desc,&pixels,&texture),"Upload PC upgrade sprites");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&view),"Create PC upgrade sprite view");upgrade_texture_=std::move(view);
}
void Renderer::upload_element_icons(const HudAssets& assets) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=assets.width;desc.Height=assets.height;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA pixels{assets.rgba.data(),assets.width*4,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
    checked(device_->CreateTexture2D(&desc,&pixels,&texture),"Upload element icons");
    checked(device_->CreateShaderResourceView(texture.Get(),nullptr,&view),"Create element icon view");element_icon_texture_=std::move(view);
}
void Renderer::update_level_geometry(const Level& level) {
    if(geometry_source_==&level && geometry_revision_==level.terrain_revision)return;
    if(geometry_source_!=&level){upload_level(level);return;}
    if(terrain_cache_.size()!=level.visual_vertices.size() || collision_cache_.size()!=level.triangles.size()*3)
        {level.dirty_vertices.clear();level.dirty_triangles.clear();geometry_collision_revision_=level.collision_revision;upload_level_geometry(level);return;}
    const bool structural=geometry_collision_revision_!=level.collision_revision;
    if(!structural && (!level.dirty_vertices.empty() || !level.dirty_triangles.empty())) {
        // Moving platforms, arena parts and scene morphs recorded what they
        // rewrote: stream those ranges only (merged), never the whole level.
        const auto merged=[](std::vector<std::array<std::uint32_t,2>>& ranges,std::size_t limit) {
            std::sort(ranges.begin(),ranges.end());std::vector<std::array<std::uint32_t,2>> out;
            for(auto [first,count]:ranges) {
                if(first>=limit)continue;count=std::min<std::uint32_t>(count,std::uint32_t(limit-first));
                if(!out.empty() && first<=out.back()[0]+out.back()[1]+8)out.back()[1]=std::max(out.back()[0]+out.back()[1],first+count)-out.back()[0];
                else out.push_back({first,count});
            }
            ranges.clear();return out;
        };
        const auto vertices=merged(level.dirty_vertices,terrain_cache_.size());
        for(const auto [first,count]:vertices) {
            for(std::uint32_t i=first;i<first+count;++i){const auto& v=level.visual_vertices[i];auto& out=terrain_cache_[i];out.position=v.position;out.r=v.color.x;out.g=v.color.y;out.b=v.color.z;out.u=v.u;out.v=v.v;}
            D3D11_BOX box{UINT(first*sizeof(terrain_cache_[0])),0,0,UINT((first+count)*sizeof(terrain_cache_[0])),1,1};
            context_->UpdateSubresource(level_buffer_.Get(),0,&box,terrain_cache_.data()+first,0,0);
        }
        for(const auto [first,count]:merged(level.dirty_triangles,level.triangles.size())) {
            for(std::uint32_t i=first;i<first+count;++i){const auto& t=level.triangles[i];collision_cache_[i*3].position=t.a;collision_cache_[i*3+1].position=t.b;collision_cache_[i*3+2].position=t.c;}
            D3D11_BOX box{UINT(first*3*sizeof(collision_cache_[0])),0,0,UINT((first+count)*3*sizeof(collision_cache_[0])),1,1};
            context_->UpdateSubresource(collision_buffer_.Get(),0,&box,collision_cache_.data()+first*3,0,0);
        }
        if(!vertices.empty())for(std::size_t i=0;i<transparent_terrain_.size();++i) {
            const auto source=transparent_sources_[i];
            // First range ending after this triangle starts (ranges are sorted, disjoint).
            const auto r=std::partition_point(vertices.begin(),vertices.end(),[&](const auto& x){return x[0]+x[1]<=source;});
            if(r==vertices.end() || (*r)[0]>=source+3)continue;
            auto& triangle=transparent_terrain_[i];triangle.center={};
            for(unsigned corner=0;corner<3;++corner){const auto& v=level.visual_vertices[source+corner];auto& out=triangle.vertices[corner];out.position=v.position;out.r=v.color.x;out.g=v.color.y;out.b=v.color.z;out.u=v.u;out.v=v.v;triangle.center+=out.position/3.0f;}
        }
        geometry_revision_=level.terrain_revision;return;
    }
    level.dirty_vertices.clear();level.dirty_triangles.clear();geometry_collision_revision_=level.collision_revision;
    for(std::size_t i=0;i<terrain_cache_.size();++i){const auto& v=level.visual_vertices[i];auto& out=terrain_cache_[i];out.position=v.position;out.r=v.color.x;out.g=v.color.y;out.b=v.color.z;out.u=v.u;out.v=v.v;}
    if(!terrain_cache_.empty())context_->UpdateSubresource(level_buffer_.Get(),0,nullptr,terrain_cache_.data(),0,0);
    for(std::size_t i=0;i<level.triangles.size();++i) {
        const auto& t=level.triangles[i];collision_cache_[i*3].position=t.a;collision_cache_[i*3+1].position=t.b;collision_cache_[i*3+2].position=t.c;
    }
    if(!collision_cache_.empty())context_->UpdateSubresource(collision_buffer_.Get(),0,nullptr,collision_cache_.data(),0,0);
    for(std::size_t i=0;i<transparent_terrain_.size();++i) {
        auto& triangle=transparent_terrain_[i];triangle.center={};
        for(unsigned corner=0;corner<3;++corner){const auto& v=level.visual_vertices[transparent_sources_[i]+corner];auto& out=triangle.vertices[corner];out.position=v.position;out.r=v.color.x;out.g=v.color.y;out.b=v.color.z;out.u=v.u;out.v=v.v;triangle.center+=out.position/3.0f;}
    }
    geometry_source_=&level;geometry_revision_=level.terrain_revision;
}
void Renderer::resize(unsigned width,unsigned height) {
    width=std::clamp(width,1u,8192u);height=std::clamp(height,1u,8192u);
    if (width==width_ && height==height_) return;
    context_->OMSetRenderTargets(0,nullptr,nullptr);ID3D11ShaderResourceView* empty=nullptr;context_->PSSetShaderResources(0,1,&empty);
    color_view_.Reset();target_.Reset();color_.Reset();depth_view_.Reset();depth_.Reset();
    D3D11_TEXTURE2D_DESC descriptor{};descriptor.Width=width;descriptor.Height=height;descriptor.MipLevels=1;descriptor.ArraySize=1;
    descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;descriptor.SampleDesc.Count=1;descriptor.Usage=D3D11_USAGE_DEFAULT;
    descriptor.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    checked(device_->CreateTexture2D(&descriptor,nullptr,&color_),"Create viewport texture");
    checked(device_->CreateRenderTargetView(color_.Get(),nullptr,&target_),"Create viewport target");
    checked(device_->CreateShaderResourceView(color_.Get(),nullptr,&color_view_),"Create viewport view");
    descriptor.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;descriptor.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    checked(device_->CreateTexture2D(&descriptor,nullptr,&depth_),"Create viewport depth");
    checked(device_->CreateDepthStencilView(depth_.Get(),nullptr,&depth_view_),"Create depth view");
    width_=width;height_=height;
}
void Renderer::draw_vertices(std::span<const RenderVertex> vertices) {
    constexpr std::size_t capacity=65535; // divisible by three
    UINT stride=sizeof(RenderVertex),offset=0;
    auto* buffer=dynamic_buffer_.Get();context_->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    while(!vertices.empty()) {
        const auto count=std::min(vertices.size(),capacity);
        D3D11_MAPPED_SUBRESOURCE map{};checked(context_->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&map),"Map dynamic vertices");
        std::memcpy(map.pData,vertices.data(),count*sizeof(RenderVertex));context_->Unmap(buffer,0);
        context_->Draw(static_cast<UINT>(count),0);vertices=vertices.subspan(count);
    }
}
void Renderer::draw(const Level& level,const Scene& scene,Vec3 eye,Vec3 focus,const Player* player,
                    float alpha,bool wireframe,const SpyroModel& model,const SpyroPose& pose,
                    unsigned animation,bool studio,bool collision,float vertical_fov,std::uint32_t selected,
                    const ObjectModels& object_models,std::span<const RetailObject> retail_objects,const ObjectPreview* object_preview,
                    const ObjectInteractions* interactions,SuperchargeAppearance appearance,const ChargeSmoke* charge_smoke,const Powerups* powers,Vec3 spyro_tint,std::span<const RemoteSpyro> remotes,float material_ticks,
                    std::span<const ObjectPose> object_poses,Vec3 camera_up,const SparxCompanion* sparx,const HeadbashTerrain* headbash,const RiptoBoss* ripto,const TerrainDestruction* destruction) {
    if (!width_ || !height_) return;
    auto stage_start=std::chrono::steady_clock::now();
    const auto stage=[&](unsigned index){const auto now=std::chrono::steady_clock::now();stage_ms[index]=std::chrono::duration<double,std::milli>(now-stage_start).count();stage_start=now;};
    update_level_geometry(level);
    const auto* controlled=element_game && player && !studio && element_game->playable_boss.active()?&element_game->playable_boss:nullptr;
    draw_character_shadows(level,focus,player,alpha,model,pose,animation,object_models,retail_objects,appearance,
        object_poses,remotes,ripto,character_shadows && !studio && !collision && !object_preview);
    using namespace DirectX;
    const auto e=XMVectorSet(eye.x,eye.y,eye.z,1), f=XMVectorSet(focus.x,focus.y,focus.z,1);
    const auto camera_up_vector=XMVectorSet(camera_up.x,camera_up.y,camera_up.z,0);
    auto matrix=XMMatrixTranspose(XMMatrixLookAtLH(e,f,camera_up_vector)*
        XMMatrixPerspectiveFovLH(vertical_fov,static_cast<float>(width_)/height_,0.05f,1500));
    context_->UpdateSubresource(matrix_.Get(),0,nullptr,&matrix,0,0);
    const auto backdrop=studio?Vec3{0.032f,0.041f,0.065f}:ripto && ripto->crownworks() && !collision?graded_sky(level.sky_color,ripto->light(alpha)):level.sky_color;
    const float clear[]={backdrop.x,backdrop.y,backdrop.z,1};context_->ClearRenderTargetView(target_.Get(),clear);
    context_->ClearDepthStencilView(depth_view_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
    auto* target=target_.Get();context_->OMSetRenderTargets(1,&target,depth_view_.Get());
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};context_->RSSetViewports(1,&viewport);
    context_->OMSetDepthStencilState(depth_state_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    context_->RSSetState(solid_.Get());context_->IASetInputLayout(layout_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
    auto* texture=level_texture_.Get();context_->PSSetShaderResources(0,1,&texture);
    auto* sampler=point_sampler_.Get();context_->PSSetSamplers(0,1,&sampler);
    auto* constants=matrix_.Get();context_->VSSetConstantBuffers(0,1,&constants);
    // The Crownworks phase light grades the sky and terrain only; everything
    // drawn after the terrain (objects, effects, water) uses the neutral light.
    const bool lit=ripto && ripto->crownworks() && !studio && !collision;
    const auto set_light=[&](bool on){const auto k=light_constants(on?ripto:nullptr,alpha);context_->UpdateSubresource(light_.Get(),0,nullptr,&k,0,0);auto* b=light_.Get();context_->PSSetConstantBuffers(1,1,&b);};
    set_light(lit);if(lit)++lit_draws_;
    rendered_sky_regions_=rendered_sky_triangles_=0;
    if(!studio && sky_buffer_) {
        // 80029170 clears all camera translation before drawing the sky.
        // Preserve authored region/packet order, screen-linear gradients,
        // and background depth even when the player travels far away.
        const auto sky_matrix=XMMatrixTranspose(XMMatrixLookToLH(XMVectorZero(),XMVectorSubtract(f,e),camera_up_vector)*
            XMMatrixPerspectiveFovLH(vertical_fov,static_cast<float>(width_)/height_,0.0001f,1500));
        context_->UpdateSubresource(matrix_.Get(),0,nullptr,&sky_matrix,0,0);
        context_->VSSetShader(sky_vs_.Get(),nullptr,0);context_->PSSetShader(sky_ps_.Get(),nullptr,0);
        context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
        UINT sky_stride=sizeof(RenderVertex),sky_offset=0;auto* sky=sky_buffer_.Get();
        context_->IASetVertexBuffers(0,1,&sky,&sky_stride,&sky_offset);
        const auto visible=visible_sky_regions(level,eye);
        for(std::size_t i=0;i<level.sky_regions.size();++i)if(visible[i]) {
            const auto& region=level.sky_regions[i];context_->Draw(region.count,region.first);
            ++rendered_sky_regions_;rendered_sky_triangles_+=region.count/3;
        }
        context_->UpdateSubresource(matrix_.Get(),0,nullptr,&matrix,0,0);
        context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
        context_->OMSetDepthStencilState(depth_state_.Get(),0);
    }
    if(!studio && !collision && (!level.splats.empty() || !level.splat_surface.vertices.empty()))draw_splats(level,eye,focus,camera_up,vertical_fov);
    UINT stride=sizeof(RenderVertex),offset=0;auto* buffer=collision?collision_buffer_.Get():level_buffer_.Get();
    const auto count=collision?collision_vertices_:level_vertices_;context_->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    if(!studio)context_->Draw(count,0);
    enable_character_shadows(false);
    if(lit)set_light(false);
    if(wireframe && !studio) {context_->RSSetState(wire_.Get());context_->Draw(count,0);context_->RSSetState(solid_.Get());}
    if(headbash && !studio && !collision) {
        std::vector<RenderVertex> molten,wave;
        const float front=headbash->wave_radius(alpha);
        for(const auto& patch:headbash->lava()) {
            const auto center=(patch.vertices[0]+patch.vertices[1]+patch.vertices[2])/3;
            const auto origin=headbash->stamps()[patch.stamp].center;
            if(headbash->revealing() && patch.stamp==headbash->wave_stamp() && std::hypot(center.x-origin.x,center.z-origin.z)>front)continue;
            // PC elements: cooled stone/frost, void-violet lava, or an electric
            // pulse while a lightning wave passes. Colour/flag passes only.
            const auto& stamp=headbash->stamps()[patch.stamp];
            Vec3 tint{1,1,1};float material=4;
            if(stamp.cooled){material=5;tint=stamp.element==Element::Ice?Vec3{.62f,.74f,.86f}:Vec3{.36f,.33f,.38f};}
            else if(stamp.element==Element::Convexity)tint={.62f,.22f,1.25f};
            if(element_game && !stamp.cooled)for(const auto& w:element_game->elements.shockwaves())
                if(!w.plain && w.element==Element::Electric && std::hypot(w.center.x-origin.x,w.center.z-origin.z)<w.max_radius+2)tint=tint*(1.35f+.35f*std::sin(material_ticks*.6f));
            for(auto p:patch.vertices){p.y+=.045f;molten.push_back({p,tint.x,tint.y,tint.z,1,p.x+(material>4.5f?0:material_ticks*.0025f),p.z+(material>4.5f?0:material_ticks*.001f),material});}
        }
        draw_vertices(molten);
        if(headbash->wave_ticks()<58 && headbash->wave_stamp()<headbash->stamps().size()) {
            const auto stamp=headbash->stamps()[headbash->wave_stamp()];
            for(const auto& patch:headbash->shock()) {
                if(patch.stamp!=headbash->wave_stamp())continue;
                std::array<RenderVertex,3> face;float brightest=0;
                for(unsigned k=0;k<3;++k) {
                    auto p=patch.vertices[k];const float d=std::hypot(p.x-stamp.center.x,p.z-stamp.center.z);
                    const float glow=std::max(0.0f,1-std::abs(d-front)/.55f)*std::clamp((58.0f-headbash->wave_ticks())/16,0.0f,1.0f);
                    // The additive state is ONE/ONE (alpha is ignored), so the glow must scale the colour.
                    p.y+=.06f;face[k]={p,glow,glow*(stamp.mode==HeadbashMode::Lava?.28f:.75f),glow*.12f,glow};brightest=std::max(brightest,glow);
                }
                if(brightest>0)wave.insert(wave.end(),face.begin(),face.end());
            }
            context_->OMSetDepthStencilState(transparent_depth_.Get(),0);context_->OMSetBlendState(additive_blend_.Get(),nullptr,0xffffffff);
            draw_vertices(wave);context_->OMSetDepthStencilState(depth_state_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        }
    }
    if(destruction && !studio && !collision) {
        std::vector<RenderVertex> shards,lines,dust;
        for(const auto& chunk:destruction->debris()) {
            const auto origin=lerp(chunk.previous,chunk.position,alpha),rotation=lerp(chunk.previous_rotation,chunk.rotation,alpha);
            const float opacity=std::min(1.0f,float(chunk.lifetime-chunk.age)/45);
            for(const auto& v:chunk.vertices) {
                const auto p=origin+retail::rotate_model(v.position,rotation.y,rotation.x,rotation.z);
                shards.push_back({p,v.color.x,v.color.y,v.color.z,opacity,v.u,v.v,1});
            }
        }
        // Convexity slabs in flight (cut terrain falling before it lands).
        for(const auto& slab:destruction->slabs())for(const auto& v:slab.visual) {
            const auto p=slab.transform(v.position,alpha);shards.push_back({p,v.color.x,v.color.y,v.color.z,1,v.u,v.v,1});
        }
        for(const auto& face:destruction->cracks())for(auto p:face)lines.push_back({p,.045f,.028f,.03f,1});
        draw_vertices(lines);
        context_->OMSetBlendState(retail_blends_[0].Get(),nullptr,0xffffffff);draw_vertices(shards);
        const auto view=normalized(focus-eye),right=normalized(cross(camera_up,view)),up=cross(view,right);
        for(const auto& puff:destruction->dust()) {
            const auto p=lerp(puff.previous,puff.position,alpha);const float fade=float(puff.lifetime-puff.age)/puff.lifetime;
            // Soft concentric billboards avoid a texture dependency in early
            // banks while keeping debris attached to the original materials.
            for(unsigned side=0;side<10;++side) {
                const float a=side*2*std::numbers::pi_v<float>/10,b=(side+1)*2*std::numbers::pi_v<float>/10;
                dust.push_back({p,.6f,.48f,.34f,fade*.2f});
                dust.push_back({p+(right*std::cos(a)+up*std::sin(a))*puff.radius,.6f,.48f,.34f,0});
                dust.push_back({p+(right*std::cos(b)+up*std::sin(b))*puff.radius,.6f,.48f,.34f,0});
            }
        }
        context_->OMSetDepthStencilState(transparent_depth_.Get(),0);draw_vertices(dust);
        context_->OMSetDepthStencilState(depth_state_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    }
    std::vector<RenderVertex> objects,additive,ripto_metal;objects.reserve(scene.entities.size()*24+128);
    std::array<std::vector<RenderVertex>,4> object_transparent;
    struct Transparent {std::array<RenderVertex,3> vertices;ID3D11ShaderResourceView* texture;unsigned mode;float depth;};
    std::vector<Transparent> transparent;
    const auto view_forward=normalized(focus-eye);
    const auto queue_transparent=[&](std::span<const RenderVertex> vertices,ID3D11ShaderResourceView* map,unsigned mode) {
        for(std::size_t i=0;i+2<vertices.size();i+=3) {
            const auto center=(vertices[i].position+vertices[i+1].position+vertices[i+2].position)/3;
            transparent.push_back({{vertices[i],vertices[i+1],vertices[i+2]},map,mode,dot(center-eye,view_forward)});
        }
    };
    for(const auto& entity:scene.entities) if(!studio && !entity.collected)
        diamond(objects,entity.position,0.3f,0.5f,entity.kind==EntityKind::Gem?Vec3{0.2f,0.9f,0.65f}:Vec3{1,0.55f,0.1f});
    if(!studio && selected<level.triangles.size()) {
        const auto& t=level.triangles[selected];const Vec3 bias{0,0.008f,0};append_triangle(objects,t.a+bias,t.b+bias,t.c+bias,{1,0.72f,0.15f});
    }
    if(studio) {
        for(int z=-4;z<4;++z)for(int x=-4;x<4;++x) {
            const Vec3 a{x*0.5f,-retail::spyro_origin_height,z*0.5f},b=a+Vec3{0.5f,0,0},c=a+Vec3{0,0,0.5f},d=a+Vec3{0.5f,0,0.5f};
            const auto color=((x+z)&1)?Vec3{0.10f,0.12f,0.17f}:Vec3{0.12f,0.14f,0.19f};
            append_triangle(objects,a,b,c,color);append_triangle(objects,b,d,c,color);
        }
    }
    draw_vertices(objects);
    objects.clear();
    stage(0);
    rendered_objects_=0;rendered_animated_objects_=0;rendered_power_effects_=0;
    if(!studio || object_preview) {
        texture=object_texture_.Get();context_->PSSetShaderResources(0,1,&texture);
        const auto forward=normalized(focus-eye),right=normalized(cross(camera_up,forward)),up=cross(forward,right);
        const float ty=std::tan(vertical_fov/2),tx=ty*width_/height_,plane_x=std::sqrt(1+tx*tx),plane_y=std::sqrt(1+ty*ty);
        std::vector<Vec3> transformed;
        const auto instances=object_preview?std::span(&object_preview->object,1):retail_objects;
        const auto effects=powers && !studio?std::span(powers->visuals()):std::span<const PowerVisual>{};
        const auto boss_visuals=ripto && !studio?ripto->visuals():std::span<const RetailObject>{};
        const auto labels=!studio?portal_letters(level,eye,material_ticks):std::vector<RetailObject>{};
        const auto boss_begin=instances.size()+effects.size(),label_begin=boss_begin+boss_visuals.size();
        const auto controlled_index=label_begin+labels.size();
        const auto extra_count=controlled?1+controlled->shots().size():0;
        for(std::size_t index=0;index<controlled_index+extra_count;++index) {
            const auto* effect=index>=instances.size() && index<instances.size()+effects.size()?&effects[index-instances.size()]:nullptr;
            const auto& object=effect?effect->object:index<instances.size()?instances[index]:
                index<label_begin?boss_visuals[index-boss_begin]:index<controlled_index?labels[index-label_begin]:
                index==controlled_index?controlled->body():controlled->shots()[index-controlled_index-1].body;
            if(object.collected || object.state<0 || object.hidden || object.script_undrawn)continue;
            const auto return_gate=object.type==0x1aa && object.source_game!=GameBuild::YearOfTheDragonUS && !object_preview?
                std::find_if(level.portals.begin(),level.portals.end(),[&](const auto& p){return p.object==object.id;}):level.portals.end();
            if(return_gate!=level.portals.end() && !return_gate->enabled)continue;
            const auto found=object_models.models.find(object.type);
            if(found==object_models.models.end())continue;
            const auto& bank=found->second;
            if(!bank.animated && object.model>=bank.meshes.size())continue;
            const auto scale=effect?effect->previous_scale+(effect->scale-effect->previous_scale)*alpha:bank.animated?object_model_scale(object):1.0f;
            const float radius=(bank.animated?bank.animations.at(object.animation.clip).radius:bank.meshes[object.model].radius)*scale;
            auto position=object_position(object,alpha);
            if(return_gate!=level.portals.end())position.y-=(1-return_gate->rise)*(2000/retail::units_per_world);
            if(object.stun_ticks)position+=Vec3{std::sin(material_ticks*2.7f+float(index))*.028f,0,std::cos(material_ticks*3.1f+float(index)*.7f)*.028f};
            const auto relative=position-eye;const float depth=dot(relative,forward);
            if(depth+radius<0.05f || depth-radius>1500 || std::abs(dot(relative,right))>depth*tx+radius*plane_x ||
               std::abs(dot(relative,up))>depth*ty+radius*plane_y)continue;
            if(effect)++rendered_power_effects_;
            auto previous_material=object;if(effect)previous_material.material_color=effect->previous_material;
            ObjectPose animated_pose;
            if(bank.animated) {
                ++rendered_animated_objects_;
                if(index<object_poses.size())animated_pose=object_poses[index];
                else if(object_preview)animated_pose=object_preview->pose;
                else {
                    animated_pose=sample_object_animation(bank,object.animation,object_models.animation_blends);
                    if(alpha<1) {
                        const auto previous=sample_object_animation(bank,object.previous_animation,object_models.animation_blends);
                        if(previous.vertices.size()==animated_pose.vertices.size())
                            for(unsigned v=0;v<animated_pose.vertices.size();++v)animated_pose.vertices[v]=lerp(previous.vertices[v],animated_pose.vertices[v],alpha);
                    }
                }
            } else ++rendered_objects_;
            if(ripto && index<controlled_index && !studio && object.type==0x2af && bank.animated)ripto->pose(animated_pose,alpha);
            const auto& mesh=bank.meshes[bank.animated?animated_pose.mesh:object.model];
            const auto& vertices=bank.animated?animated_pose.vertices:mesh.vertices;
            const auto basis=object_basis(object,alpha);
            transformed.clear();transformed.reserve(vertices.size());
            for(const auto p:vertices)transformed.push_back(position+basis*(p*scale));
            if(backrealms_armor && index<controlled_index && object.type==0x2af)backrealms_ripto_details(objects,additive,transformed);
            for(const auto& face:mesh.faces) {
                const bool normal_material=object_normal_material(object,face);
                const bool environment=object_environment_material(object,face);
                if(!face.palette_valid && !normal_material)throw FormatError("Object material does not match its packed face payload: class "+std::to_string(object.type));
                const std::array<Vec3,4> corners_world{transformed[face.vertices[0]],transformed[face.vertices[1]],
                    transformed[face.vertices[2]],transformed[face.vertices[3]]};
                if(object.type==0x1aa && object.source_game!=GameBuild::YearOfTheDragonUS && !object_preview) {
                    const auto gate=std::find_if(level.portals.begin(),level.portals.end(),[&](const auto& p){return p.object==object.id && p.enabled && p.opening>0.95f;});
                    if(gate!=level.portals.end()) {
                        const auto plane=normalized(cross(gate->aperture[1]-gate->aperture[0],gate->aperture[2]-gate->aperture[0]));bool mask=true;
                        for(unsigned k=0;k<(face.quad?4u:3u);++k)mask&=std::abs(dot(corners_world[k]-gate->aperture[0],plane))<0.06f && portal_contains(*gate,corners_world[k],0.08f);
                        if(mask)continue;
                    }
                }
                const auto visible=object_face_view(face,corners_world,eye);
                if(!visible.count)continue;
                const auto pass=object_face_pass(face,visible.backface,depth);
                if(pass==ObjectPass::Hidden)continue;
                auto& batch=pass==ObjectPass::Additive?additive:objects;
                const auto shiny=face.shiny?shiny_object_color(object_models,object,face,basis,right,up,forward,visible.backface):Vec3{};
                const auto normal_color=normal_material?normal_object_color(object,face,basis,right,up,forward):Vec3{};
                const unsigned corners[]={0,1,2,1,3,2};
                for(unsigned i=0;i<(visible.count==4?6u:3u);++i) {
                    const auto k=visible.corners[corners[i]];
                    auto color=normal_material?normal_color:face.shiny?shiny:(face.animated?animated_object_color(object,face,face.colors[k]):face.colors[k]);
                    if(object.frozen_ticks)color=lerp(color,Vec3{0.7f,1.3f,1.7f},0.65f);
                    if(object.soak_ticks)color=lerp(color,Vec3{.55f,.72f,1.1f},.35f);
                    if(object.stun_ticks && (int(material_ticks)/3)%2)color=lerp(color,Vec3{1.6f,1.6f,.9f},.45f);
                    if(object.void_mark && (int(material_ticks)/5)%2)color=lerp(color,Vec3{.55f,.2f,.95f},.45f);
                    if(effect && face.animated)color=lerp(animated_object_color(previous_material,face,face.colors[k]),color,alpha);
                    const auto uv=environment?object_environment_uv(object_models,basis*vertices[face.vertices[k]],depth,material_ticks):face.uv[k];
                    const bool translucent=!environment && face.textured && face.translucent;
                    if(backrealms_armor && index<controlled_index && object.type==0x2af && backrealms_ripto_plate(face)) {
                        const auto metal=backrealms_metal(corners_world[0],corners_world[1],corners_world[2],eye)*1.35f;
                        // Stable local UVs over the neutral head keep the
                        // steel grain on the skin instead of clamping an atlas UV.
                        const auto rest=bank.meshes[0].vertices.at(face.vertices[k]);
                        ripto_metal.push_back({transformed[face.vertices[k]],metal.x,metal.y,metal.z,1,(rest.z+.6f)*.85f,(rest.y-.5f)*.9f,1});continue;
                    }
                    batch.push_back({transformed[face.vertices[k]],color.x,color.y,color.z,1,uv[0],uv[1],translucent?3.0f:(face.textured || environment)?1.0f:0.0f});
                    if(translucent) {
                        const float opacity=face.blend_mode==0?0.5f:face.blend_mode==3?0.25f:1;
                        object_transparent[face.blend_mode].push_back({transformed[face.vertices[k]],color.x,color.y,color.z,opacity,uv[0],uv[1],2});
                    }
                }
            }
        }
        draw_vertices(objects);objects.clear();
        texture=backrealms_armor_texture_.Get();context_->PSSetShaderResources(0,1,&texture);draw_vertices(ripto_metal);
    }
    stage(1);
    // Dark Spyro (PC cosmetic / Convexity Fury): alternate CLUT atlas, remapped
    // vertex light and full-bright eye faces that sample the white eye cell.
    const bool dark_spyro=!controlled && !object_preview && dark_spyro_texture_ &&
        ((element_game && player && !studio && element_game->elements.dark(*element_game)) || (studio && dark_spyro_preview));
    rendered_dark_spyro_=dark_spyro;
    auto* spyro_map=dark_spyro?dark_spyro_texture_.Get():spyro_texture_.Get();
    texture=spyro_map;context_->PSSetShaderResources(0,1,&texture);
    const auto position=studio?Vec3{}:(player?lerp(player->previous_position,player->position,alpha):scene.spawn)+retail::origin_above_feet;
    const float angle=studio?0:(player?player->previous_yaw+std::remainder(player->yaw-player->previous_yaw,
        2*std::numbers::pi_v<float>)*alpha:level.spawn_yaw);
    const float pitch=player && !studio?player->previous_pitch+(player->pitch-player->previous_pitch)*alpha:0;
    const float roll=player && !studio?player->previous_roll+(player->roll-player->previous_roll)*alpha:0;
    const auto& clip=model.clip(animation,appearance);
    const bool fitted_armor=!controlled && backrealms_armor && (pose.vertices.size()==230 || (appearance!=SuperchargeAppearance::Retail && pose.vertices.size()>=230));
    std::array<std::vector<RenderVertex>,4> spyro_transparent,kept_transparent;std::vector<RenderVertex> kept_faces;
    const auto eye_sets=dark_spyro?spyro_eye_vertices(model,clip,pose.vertices.size()):std::array<std::vector<unsigned>,2>{};
    const auto eye_face=[&](const SpyroFace& face) {
        if(eye_sets[0].empty())return false;
        for(unsigned k=0;k<(face.quad?4u:3u);++k)
            if(std::find(eye_sets[0].begin(),eye_sets[0].end(),face.vertices[k])==eye_sets[0].end() &&
               std::find(eye_sets[1].begin(),eye_sets[1].end(),face.vertices[k])==eye_sets[1].end())return false;
        return true;
    };
    if(!controlled && !object_preview)for(const auto& face:clip.faces) {
        if(fitted_armor && (backrealms_original_horn(face) || backrealms_spyro_plate(face)))continue;
        const unsigned corners[]={0,1,2,1,3,2};
        const bool glowing_eye=dark_spyro && eye_face(face);
        // Horns, crest, wings and tail tip keep the retail atlas under a dark tint.
        const bool kept=dark_spyro && !glowing_eye && dark_spyro_kept(dark_keep_,face,pose.vertices.size());
        for(unsigned i=0;i<(face.quad?6u:3u);++i) {
            const unsigned corner=corners[i];const auto local=pose.vertices.at(face.vertices[corner]);
            const auto world=position+retail::rotate_model(local,angle,pitch,roll);
            auto color=face.colors[corner];const auto uv=face.uv[corner];
            if(dark_spyro)color=glowing_eye?Vec3{1.8f,1.8f,1.9f}:kept?Vec3{color.x*dark_spyro_keep_tint.x,color.y*dark_spyro_keep_tint.y,color.z*dark_spyro_keep_tint.z}:dark_spyro_vertex(color);
            // 80041dbc: superflame depth-cues the model palette toward RGB
            // 90/20..A0/10 with IR0=D8*16. Multiplication made Spyro too dark.
            // Dark Spyro keeps only a faint cast of a power tint so the dark palette survives.
            if(!glowing_eye && (spyro_tint.x!=1 || spyro_tint.y!=1 || spyro_tint.z!=1))color=lerp(color,spyro_tint,(dark_spyro?56:216)/256.0f);
            (kept?kept_faces:objects).push_back({world,color.x,color.y,color.z,1,uv[0],uv[1],face.translucent?3.0f:1.0f});
            if(face.translucent) {
                const float opacity=face.blend_mode==0?0.5f:(face.blend_mode==3?0.25f:1.0f);
                (kept?kept_transparent:spyro_transparent)[face.blend_mode].push_back({world,color.x,color.y,color.z,opacity,uv[0],uv[1],2});
            }
        }
    }
    if(wireframe && studio)context_->RSSetState(wire_.Get());
    std::vector<RenderVertex> remote_dark,remote_normal;
    std::array<std::vector<RenderVertex>,4> remote_dark_transparent,remote_normal_transparent;
    if(!studio && !object_preview)for(const auto& remote:remotes) {
        const auto& other_clip=model.clip(remote.clip,remote.appearance);
        const bool other_dark=remote.dark && dark_spyro_texture_;
        const auto other_eyes=other_dark?spyro_eye_vertices(model,other_clip,remote.pose.vertices.size()):std::array<std::vector<unsigned>,2>{};
        auto& batch=other_dark==dark_spyro?objects:other_dark?remote_dark:remote_normal;
        auto& clear_batch=other_dark==dark_spyro?spyro_transparent:other_dark?remote_dark_transparent:remote_normal_transparent;
        for(const auto& face:other_clip.faces) {
            const unsigned corners[]{0,1,2,1,3,2};
            bool eye=other_dark && !other_eyes[0].empty();
            const bool other_kept=other_dark && dark_spyro_kept(dark_keep_,face,remote.pose.vertices.size());
            if(eye)for(unsigned k=0;k<(face.quad?4u:3u);++k)eye&=std::find(other_eyes[0].begin(),other_eyes[0].end(),face.vertices[k])!=other_eyes[0].end() ||
                std::find(other_eyes[1].begin(),other_eyes[1].end(),face.vertices[k])!=other_eyes[1].end();
            for(unsigned i=0;i<(face.quad?6u:3u);++i) {
                const auto k=corners[i];if(face.vertices[k]>=remote.pose.vertices.size())continue;
                const auto world=remote.player.position+retail::origin_above_feet+retail::rotate_model(remote.pose.vertices[face.vertices[k]],remote.player.yaw,remote.player.pitch,remote.player.roll);
                auto color=face.colors[k];const auto uv=face.uv[k];
                if(other_dark)color=eye?Vec3{1.8f,1.8f,1.9f}:other_kept?Vec3{color.x*dark_spyro_keep_tint.x,color.y*dark_spyro_keep_tint.y,color.z*dark_spyro_keep_tint.z}:dark_spyro_vertex(color);
                (other_kept && !eye?remote_normal:batch).push_back({world,color.x,color.y,color.z,1,uv[0],uv[1],face.translucent?3.0f:1.0f});
                if(face.translucent)clear_batch[face.blend_mode].push_back({world,color.x,color.y,color.z,face.blend_mode==0?0.5f:face.blend_mode==3?0.25f:1.0f,uv[0],uv[1],2});
            }
        }
    }
    draw_vertices(objects);
    if(!kept_faces.empty()){texture=spyro_texture_.Get();context_->PSSetShaderResources(0,1,&texture);draw_vertices(kept_faces);}
    if(!remote_dark.empty()){texture=dark_spyro_texture_.Get();context_->PSSetShaderResources(0,1,&texture);draw_vertices(remote_dark);}
    if(!remote_normal.empty()){texture=spyro_texture_.Get();context_->PSSetShaderResources(0,1,&texture);draw_vertices(remote_normal);}
    for(unsigned mode=0;mode<4;++mode) {
        if(!remote_dark_transparent[mode].empty())queue_transparent(remote_dark_transparent[mode],dark_spyro_texture_.Get(),mode);
        if(!remote_normal_transparent[mode].empty())queue_transparent(remote_normal_transparent[mode],spyro_texture_.Get(),mode);
    }
    texture=spyro_map;context_->PSSetShaderResources(0,1,&texture);
    context_->RSSetState(solid_.Get());
    if(fitted_armor && !object_preview && backrealms_armor_texture_) {
        objects.clear();backrealms_spyro_armor(objects,pose,position,eye,angle,pitch,roll,pose.vertices.size()>230);
        auto* material=backrealms_armor_texture_.Get();context_->PSSetShaderResources(0,1,&material);draw_vertices(objects);
        material=spyro_texture_.Get();context_->PSSetShaderResources(0,1,&material);
    }
    for(unsigned mode=0;mode<4;++mode){queue_transparent(spyro_transparent[mode],spyro_map,mode);if(!kept_transparent[mode].empty())queue_transparent(kept_transparent[mode],spyro_texture_.Get(),mode);}
    context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    objects.clear();std::vector<RenderVertex> flame_trails;
    stage(2);
    rendered_flame_streams_=0;rendered_flame_triangles_=0;
    if(interactions && !studio && flame_texture_) {
        const auto fire=flame_geometry(object_models.flame,interactions->sample_flame(alpha),object_models.sine,object_models.cosine);
        rendered_flame_streams_=fire.streams;
        for(std::size_t i=0;i<fire.tips.size();i+=3) {
            if(object_backface(fire.tips[i].position,fire.tips[i+1].position,fire.tips[i+2].position,eye))continue;
            for(unsigned j=0;j<3;++j) {
                const auto& v=fire.tips[i+j];const auto color=powers && powers->ice.remaining?Vec3{v.color.z*0.6f+0.2f,v.color.x,v.color.x*1.4f}:v.color;objects.push_back({v.position,color.x,color.y,color.z});
            }
        }
        for(const auto& v:fire.trails){const auto color=powers && powers->ice.remaining?Vec3{v.color.z*0.6f+0.2f,v.color.x,v.color.x*1.4f}:v.color;flame_trails.push_back({v.position,color.x,color.y,color.z,1,v.u,v.v,1});}
        rendered_flame_triangles_=static_cast<unsigned>((objects.size()+flame_trails.size())/3);
        draw_vertices(objects);
    }
    if(ripto && !studio)backrealms_ripto_effects(additive,*ripto,alpha);
    if(controlled)for(const auto& wave:controlled->waves()) {
        const float radius=(wave.age-1+alpha)*.16f;
        const auto color=Vec3{.15f,.58f,1.0f}*(1-wave.age/33.0f);
        for(unsigned i=0;i<48;++i) {
            const auto point=[&](unsigned j) {
                const float angle=j*(2*std::numbers::pi_v<float>/48);
                return wave.origin+Vec3{std::cos(angle)*radius,.11f,std::sin(angle)*radius};
            };
            const auto a=point(i),b=point(i+1);
            if(length(b-a)>.001f)backrealms_rod(additive,a,b,.055f,color);
        }
    }
    queue_transparent(additive,object_texture_.Get(),1);
    for(unsigned mode=0;mode<4;++mode)queue_transparent(object_transparent[mode],object_texture_.Get(),mode);
    context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    if(!flame_trails.empty()) {
        texture=flame_texture_.Get();context_->PSSetShaderResources(0,1,&texture);
        context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
        context_->OMSetBlendState(additive_blend_.Get(),nullptr,0xffffffff);
        draw_vertices(flame_trails);
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    }
    #include "renderer_companion.inl"
    rendered_charge_smoke_=0;
    if(charge_smoke && charge_smoke_texture_ && !studio) {
        const auto forward=normalized(focus-eye),right=normalized(cross(Vec3{0,1,0},forward)),up=cross(forward,right);
        objects.clear();
        for(const auto& puff:charge_smoke->sample(alpha)) {
            if(dot(puff.position-eye,forward)<0.05f)continue;
            const auto a=(right*std::cos(puff.angle)+up*std::sin(puff.angle))*puff.radius;
            const auto b=(up*std::cos(puff.angle)-right*std::sin(puff.angle))*puff.radius;
            const std::array<Vec3,4> p{puff.position-a-b,puff.position+a-b,puff.position-a+b,puff.position+a+b};
            const unsigned corners[]{0,1,2,1,3,2};
            for(auto i:corners)objects.push_back({p[i],1,1,1,1,static_cast<float>(i&1),static_cast<float>(i>>1),1});
            ++rendered_charge_smoke_;
        }
        texture=charge_smoke_texture_.Get();context_->PSSetShaderResources(0,1,&texture);
        context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
        context_->OMSetBlendState(additive_blend_.Get(),nullptr,0xffffffff);draw_vertices(objects);
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    }
    if(powers && !studio) {
        const auto forward=normalized(focus-eye),right=normalized(cross(Vec3{0,1,0},forward)),up=cross(forward,right);
        context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
        context_->OMSetBlendState(additive_blend_.Get(),nullptr,0xffffffff);
        objects.clear();
        // 800502fc / 8004b72c: a retail trail through its positions, newest
        // first. Width byte w spreads |w|*2048/SZ pixels about each point:
        // screen-vertical when positive, screen-horizontal when negative
        // (GTE H 341, vertical scale 320/512; SZ is half the retail depth),
        // tapering linearly to nothing. The colour depth-cues to black along
        // it. The 8x32 trail texture's 32-texel glow profile runs ACROSS the
        // trail (v) and its 8 texels along each segment (u).
        const auto retail_trail=[&](std::span<const Vec3> points,float colour,int width) {
            if(points.size()<2)return;
            const auto axis=width<0?right:up;
            const float half=std::abs(width)*4096.0f/(width<0?341.0f:341.0f*320/512)/retail::units_per_world;
            const float last=static_cast<float>(points.size()-1);
            for(std::size_t i=1;i<points.size();++i) {
                const float t0=(i-1)/last,t1=i/last;
                const auto a=points[i-1],b=points[i];
                const std::array<Vec3,4> quad{a-axis*(half*(1-t0)),b-axis*(half*(1-t1)),a+axis*(half*(1-t0)),b+axis*(half*(1-t1))};
                for(const auto k:{0,1,2,1,3,2}){const float shade=colour*(1-((k&1)?t1:t0));objects.push_back({quad[k],shade,shade,shade,1,float(k&1),float(k>>1),1});}
            }
        };
        // 80032678 / 800326a8: while flying (mode 6) each wing tip trails six
        // updates (twelve native ticks) at width -3, colour 0x303030.
        for(const auto& wing:powers->wings()) {
            const std::vector<Vec3> points(wing.begin(),wing.end());
            retail_trail(points,48/128.0f,-3);
        }
        // A spirit (class 68) trails six positions at width 3, colour 0x808080.
        for(const auto& spirit:powers->spirit_flights())if(spirit.trail.size()>1) {
            std::vector<Vec3> points{object_position(spirit.body,alpha)};
            for(std::size_t i=1;i<spirit.trail.size();++i)points.push_back(spirit.trail[i]);
            retail_trail(points,1,3);
        }
        // 8004b194: an open gate's bolts. Interior bends jitter about the
        // straight line; each segment is a ribbon 1/16 of its length wide,
        // colour 0x608080 on the trail texture.
        for(const auto* list:{&powers->gate_bolts(),&powers->script_bolts()})for(const auto& bolt:*list) {
            std::array<Vec3,5> points{bolt.from};
            for(unsigned k=1;k<4;++k)points[k]=lerp(bolt.from,bolt.to,k/4.0f)+retail::world_position({float(bolt.bends[k-1].x),float(bolt.bends[k-1].y),float(bolt.bends[k-1].z)});
            points[4]=bolt.to;
            for(unsigned k=1;k<points.size();++k) {
                const auto a=points[k-1],b=points[k];const float span=length(b-a);if(span<1e-5f)continue;
                const auto across=normalized(cross(b-a,eye-(a+b)*0.5f))*(span/16);
                const std::array<Vec3,4> quad{a-across,a+across,b-across,b+across};
                for(const auto c:{0,1,2,1,3,2})objects.push_back({quad[c],1,1,96/128.0f,1,float(c>>1),float(c&1),1});
            }
        }
        texture=wing_texture_.Get();context_->PSSetShaderResources(0,1,&texture);draw_vertices(objects);objects.clear();
        // 1E3's 407 trail: a five-sided orange tube, retaining twelve retail
        // history positions (24 native ticks) with the old endpoint fading out.
        for(const auto& shot:powers->projectiles())if(shot.kind==PowerShot::Superflame && shot.trail.size()>1) {
            const float fade=shot.exploding?1-shot.blast_age/20.0f:1;
            for(std::size_t n=1;n<shot.trail.size();++n) {
                const auto a=shot.trail[n-1],b=shot.trail[n],axis=normalized(a-b);
                const auto side=normalized(cross(std::abs(axis.y)>0.95f?Vec3{1,0,0}:Vec3{0,1,0},axis)),vertical=cross(axis,side);
                const auto ring=[&](unsigned k,float radius){const float angle=k*2*std::numbers::pi_v<float>/5;return (side*std::cos(angle)+vertical*std::sin(angle))*(radius*(shot.fragment?0.45f:1.0f));};
                const float old=1-float(n)/shot.trail.size(),recent=1-float(n-1)/shot.trail.size();
                for(unsigned k=0;k<5;++k) {
                    const std::array<Vec3,4> points{a+ring(k,150/retail::units_per_world),a+ring(k+1,150/retail::units_per_world),
                        b+ring(k,100/retail::units_per_world),b+ring(k+1,100/retail::units_per_world)};
                    for(const auto corner:{0,1,2,1,3,2}) {
                        const auto c=Vec3{140/255.0f,100/255.0f,40/255.0f}*((corner<2?recent:old)*fade);
                        objects.push_back({points[corner],c.x,c.y,c.z});
                    }
                }
            }
        }
        for(const auto& p:powers->sparks()) {
            const auto a=lerp(p.previous,p.position,alpha),b=lerp(p.previous_tip,p.tip,alpha);
            const auto side=normalized(cross(b-a,eye-(a+b)*0.5f))*(2/retail::units_per_world);
            const float age=p.age*0.5f;
            const auto warm=Vec3{std::max(128.0f,224-age*3),std::max(96.0f,192-age*4),0}/255;
            const auto hot=Vec3{std::max(192.0f,255-age*3),std::max(128.0f,255-age*4),std::max(32.0f,128-age*6)}/255;
            const std::array<Vec3,4> points{a-side,a+side,b-side,b+side};
            for(const auto k:{0,1,2,1,3,2}) {const auto c=k<2?warm:hot;objects.push_back({points[k],c.x,c.y,c.z});}
        }
        // Particle 47: a shaded line from a gold head (E0,E0,60; -3 each per
        // update) to a white tail (FF,FF,FF; -2,-2,-4).
        for(const auto& c:powers->gate_crackles()) {
            const auto a=lerp(c.previous_head,c.head,alpha),b=lerp(c.previous_tail,c.tail,alpha);
            if(length(b-a)<1e-5f)continue;
            const auto side=normalized(cross(b-a,eye-(a+b)*0.5f))*(4/retail::units_per_world);
            const float updates=(c.age+alpha)/2;
            const auto fade=[](float v){return std::max(0.0f,v)/255;};
            const Vec3 gold{fade(224-updates*3),fade(224-updates*3),fade(96-updates*3)},white{fade(255-updates*2),fade(255-updates*2),fade(255-updates*4)};
            const std::array<Vec3,4> points{a-side,a+side,b-side,b+side};
            for(const auto k:{0,1,2,1,3,2}){const auto col=k<2?gold:white;objects.push_back({points[k],col.x,col.y,col.z});}
        }
        draw_vertices(objects);objects.clear();
        for(const auto kind:{PowerParticleKind::Puff,PowerParticleKind::Green,PowerParticleKind::Fire,PowerParticleKind::ChargeFlame,PowerParticleKind::Bubble,PowerParticleKind::Sparkle,PowerParticleKind::GateGlow}) {
        for(const auto& p:powers->particles())if(p.kind==kind) {
            const auto particle_position=lerp(p.previous,p.position,alpha);const float age=p.age+alpha;
            float radius=p.radius*(0.4f+age/p.lifetime),spin=0;auto color=p.color*(1-age/p.lifetime);
            if(kind==PowerParticleKind::Bubble) {
                radius=p.radius+age/retail::units_per_world;color={1,1,1};
            } else if(kind==PowerParticleKind::Sparkle) {
                // 0C (Glimmer 8007f588, overlay 70 80089a68): the update's base
                // register is the record + 1, so its "+0A += 8" is the spin byte
                // +0B (8 / 256 of a turn per update) and the three channels -8 per
                // update until all reach zero. The size (+0A, 32 + rand & 7, four
                // units per size) never changes.
                radius=p.radius;spin=age*64*retail::angle_to_radians;
                const auto fade=[&](float c){return std::max(0.0f,c-age*4/128);};color={fade(p.color.x),fade(p.color.y),fade(p.color.z)};
            } else if(kind==PowerParticleKind::GateGlow) {
                radius=p.radius;color=p.color;   // 04: size and colour 0x80 are set by its update
            } else if(kind==PowerParticleKind::Green || kind==PowerParticleKind::Fire) {
                radius=(96+age*8)/retail::units_per_world;
                // 0D / 3A grow and cool from a bright core to their fire colour.
                const auto a=std::max(0.0f,age-1);
                if(a<5)color=kind==PowerParticleKind::Green?Vec3{255/128.0f,1,0}:Vec3{1,1,std::max(0.0f,128-a*32)/128};
                else if(a<13)color={1,(128-(a-4)*8)/128,0};
                else color={std::max(0.0f,128-(a-12)*8)/128,std::max(0.0f,64-(a-12)*4)/128,0};
                if(kind==PowerParticleKind::Green)std::swap(color.x,color.y);
            }
            const float turn=p.angle+spin;
            const auto x=(right*std::cos(turn)+up*std::sin(turn))*radius;
            const auto y=(up*std::cos(turn)-right*std::sin(turn))*radius;
            const std::array<Vec3,4> points{particle_position-x-y,particle_position+x-y,particle_position+y-x,particle_position+x+y};
            const unsigned corners[]{0,1,2,1,3,2};
            for(const auto k:corners)objects.push_back({points[k],color.x,color.y,color.z,1,float(k&1),float(k>>1),1});
        }
        texture=kind==PowerParticleKind::Bubble?power_bubble_texture_.Get():(kind==PowerParticleKind::Fire || kind==PowerParticleKind::ChargeFlame)?power_fire_texture_.Get():
            kind==PowerParticleKind::Sparkle && power_sparkle_texture_?power_sparkle_texture_.Get():
            kind==PowerParticleKind::GateGlow && power_gate_texture_?power_gate_texture_.Get():power_particle_texture_.Get();
        context_->PSSetShaderResources(0,1,&texture);draw_vertices(objects);objects.clear();
        }
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    }
    #include "renderer_elements.inl"
    stage(3);
    #include "renderer_tunnels.inl"
    #include "renderer_travel.inl"
    if(level.universe && !studio && !collision) {
        const auto& world=*level.universe;
        const auto forward=normalized(focus-eye),right=normalized(cross(Vec3{0,1,0},forward)),up=cross(forward,right);
        const float time=player?(player->motion_ticks+alpha)/60.0f:0;
        objects.clear();context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
        context_->OMSetBlendState(additive_blend_.Get(),nullptr,0xffffffff);
        const auto billboard=[&](Vec3 p,float radius,Vec3 color) {
            const auto x=right*radius,y=up*radius;
            append_triangle(objects,p-x,p+y,p+x,color);append_triangle(objects,p-x,p+x,p-y,color);
        };
        for(const auto& star:world.stars)billboard(star.position,star.radius,star.color);
        draw_vertices(objects);objects.clear();
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        // Original Return Home destination artwork, clipped to the original
        // five-sided aperture. The white/gold 1AA Moby was drawn with actors.
        for(const auto& portal:world.portals) {
            const auto base=portal.center-Vec3{0,portal.radius,0};
            const float glow=0.98f+0.02f*std::sin(time*2);
            for(const auto& vertex:world.return_portal.picture) {
                const auto p=base+vertex.position*portal.scale,c=vertex.color*glow;
                objects.push_back({p,c.x,c.y,c.z});
            }
        }
        draw_vertices(objects);objects.clear();
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    }
    stage(4);
    rendered_transparent_terrain_triangles_=0;
    if(!studio && !collision && !transparent_terrain_.empty()) {
        for(const auto& triangle:transparent_terrain_)queue_transparent(triangle.vertices,level_texture_.Get(),triangle.blend_mode);
        rendered_transparent_terrain_triangles_=static_cast<unsigned>(transparent_terrain_.size());
    }
    if(!transparent.empty()) {
        // One depth order across object materials, Spyro and water. Grouping
        // by blend mode used to reverse overlapping half/subtractive faces.
        std::stable_sort(transparent.begin(),transparent.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});
        context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
        // Upload a sorted chunk once, then draw its contiguous material runs.
        // Discard-mapping this 2.6 MB buffer for each tiny run exhausted the
        // driver's renaming pool in water-heavy speedways. Order is unchanged.
        constexpr std::size_t chunk_triangles=65535/3;
        auto* dynamic=dynamic_buffer_.Get();UINT dynamic_stride=sizeof(RenderVertex),dynamic_offset=0;
        context_->IASetVertexBuffers(0,1,&dynamic,&dynamic_stride,&dynamic_offset);
        for(std::size_t base=0;base<transparent.size();base+=chunk_triangles) {
            const auto end=std::min(transparent.size(),base+chunk_triangles);
            D3D11_MAPPED_SUBRESOURCE map{};checked(context_->Map(dynamic,0,D3D11_MAP_WRITE_DISCARD,0,&map),"Map sorted transparency");
            auto* destination=static_cast<RenderVertex*>(map.pData);
            for(auto i=base;i<end;++i)std::memcpy(destination+(i-base)*3,transparent[i].vertices.data(),3*sizeof(RenderVertex));
            context_->Unmap(dynamic,0);
            for(auto first=base;first<end;) {
                const auto mode=transparent[first].mode;texture=transparent[first].texture;auto last=first+1;
                while(last<end && transparent[last].mode==mode && transparent[last].texture==texture)++last;
                context_->PSSetShaderResources(0,1,&texture);context_->OMSetBlendState(retail_blends_[mode].Get(),nullptr,0xffffffff);
                context_->Draw(static_cast<UINT>((last-first)*3),static_cast<UINT>((first-base)*3));first=last;
            }
        }
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
    }
    stage(5);
    context_->OMSetRenderTargets(0,nullptr,nullptr);
}
void Renderer::save_bmp(const std::filesystem::path& path,ID3D11Texture2D* source_texture) {
    if(!source_texture)source_texture=color_.Get();
    if(!source_texture)throw FormatError("No rendered image to capture");
    D3D11_TEXTURE2D_DESC desc{};source_texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    const auto width=desc.Width,height=desc.Height;
    ComPtr<ID3D11Texture2D> staging;checked(device_->CreateTexture2D(&desc,nullptr,&staging),"Create screenshot staging");
    context_->CopyResource(staging.Get(),source_texture);D3D11_MAPPED_SUBRESOURCE mapped{};
    checked(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map screenshot");
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const auto* source=static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*4;
        auto* dest=pixels.data()+(static_cast<std::size_t>(height-1-y)*width+x)*4;
        dest[0]=source[2];dest[1]=source[1];dest[2]=source[0];dest[3]=255;
    }
    context_->Unmap(staging.Get(),0);
    std::array<std::uint8_t,54> header{};header[0]='B';header[1]='M';
    const auto put=[&](unsigned at,std::uint32_t value) {for(unsigned i=0;i<4;++i)header[at+i]=static_cast<std::uint8_t>(value>>(8*i));};
    put(2,54+static_cast<std::uint32_t>(pixels.size()));put(10,54);put(14,40);put(18,width);put(22,height);header[26]=1;header[28]=32;
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(header.data()),header.size());
    output.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
    if(!output)throw FormatError("Could not save rendered screenshot");
}
} // namespace spyro
