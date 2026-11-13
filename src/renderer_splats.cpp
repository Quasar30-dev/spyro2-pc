#include "spyro/renderer.hpp"
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <algorithm>

namespace spyro {
namespace {
void check_splat(HRESULT hr,const char* what){if(FAILED(hr))throw FormatError(std::string(what)+" failed: "+std::to_string(hr));}
constexpr char shader[]=R"(
struct Gaussian {float3 mean;float opacity;float3 color;float pad0;float3 diag;float pad1;float3 off;float pad2;};
StructuredBuffer<Gaussian> scene:register(t0);
StructuredBuffer<uint> order:register(t1);
cbuffer Camera:register(b0){float4 eye;float4 right;float4 up;float4 forward;float4 projection;};
struct Fragment{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;float4 color:COLOR0;};
float3 covariance(Gaussian s,float3 v){return float3(dot(float3(s.diag.x,s.off.x,s.off.y),v),dot(float3(s.off.x,s.diag.y,s.off.z),v),dot(float3(s.off.y,s.off.z,s.diag.z),v));}
Fragment vs_main(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
    Gaussian s=scene[order[instance]];float3 d=s.mean-eye.xyz;
    float z=dot(d,forward.xyz),x=dot(d,right.xyz),y=dot(d,up.xyz);
    float safeZ=max(z,.05),fx=projection.x,fy=projection.y;
    float3 jx=(right.xyz-forward.xyz*x/safeZ)*(fx/safeZ);
    float3 jy=(up.xyz-forward.xyz*y/safeZ)*(fy/safeZ);
    float a=dot(jx,covariance(s,jx))+.3,b=dot(jx,covariance(s,jy)),c=dot(jy,covariance(s,jy))+.3;
    // Cholesky factor projects the entire anisotropic covariance ellipse.
    float ax=sqrt(max(a,.3)),bx=b/ax,by=sqrt(max(c-bx*bx,.3));
    float2 corners[6]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,-1),float2(1,1),float2(-1,1)};
    float2 q=corners[vertex]*3.0;float2 offset=float2(ax*q.x,bx*q.x+by*q.y);
    float2 ndc=float2(x*fx,y*fy)/safeZ*2/projection.zw+offset*2/projection.zw;
    Fragment f;f.pos=float4(ndc,1500.0/(1500.0-.05)-1500.0*.05/((1500.0-.05)*safeZ),1);
    f.uv=q;f.color=float4(s.color,z>.05?s.opacity:0);return f;
}
float4 ps_main(Fragment f):SV_TARGET{
    float alpha=f.color.a*exp(-.5*dot(f.uv,f.uv));clip(alpha-1.0/255.0);return float4(f.color.rgb,min(alpha,.99));
}
float4 ps_depth(Fragment f):SV_TARGET{
    // An opacity core supplies approximate scene depth for real mesh actors.
    // Fine translucent silhouettes retain blending but do not form solid walls.
    clip(f.color.a*exp(-.5*dot(f.uv,f.uv))-.28);return 0;
}
)";
}
void Renderer::upload_splats(const Level& level){
    splat_surface_buffer_.Reset();splat_surface_texture_.Reset();splat_surface_vertices_=0;
    splat_data_.Reset();splat_order_.Reset();splat_data_view_.Reset();splat_order_view_.Reset();
    splat_visible_.clear();splat_sorted_.clear();
    if(!level.splats.empty()) {
    if(!splat_vs_){
        Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,depth,error;
        const auto compile=[&](const char* entry,const char* target,ID3DBlob** blob){
            const auto hr=D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob,&error);
            if(FAILED(hr))throw FormatError(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"Gaussian shader compile failed");
        };
        compile("vs_main","vs_5_0",&vs);compile("ps_main","ps_5_0",&ps);compile("ps_depth","ps_5_0",&depth);
        check_splat(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&splat_vs_),"Gaussian vertex shader");
        check_splat(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&splat_ps_),"Gaussian color shader");
        check_splat(device_->CreatePixelShader(depth->GetBufferPointer(),depth->GetBufferSize(),nullptr,&splat_depth_ps_),"Gaussian depth shader");
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=80;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check_splat(device_->CreateBuffer(&cb,nullptr,&splat_camera_),"Gaussian camera buffer");
        D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=0;
        check_splat(device_->CreateBlendState(&blend,&splat_depth_blend_),"Gaussian depth-only state");
    }
    D3D11_BUFFER_DESC b{};b.ByteWidth=UINT(level.splats.size()*sizeof(GaussianSplat));b.Usage=D3D11_USAGE_IMMUTABLE;
    b.BindFlags=D3D11_BIND_SHADER_RESOURCE;b.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;b.StructureByteStride=sizeof(GaussianSplat);
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=level.splats.data();
    check_splat(device_->CreateBuffer(&b,&data,&splat_data_),"Gaussian scene upload");
    check_splat(device_->CreateShaderResourceView(splat_data_.Get(),nullptr,&splat_data_view_),"Gaussian scene view");
    b.ByteWidth=UINT(level.splats.size()*sizeof(unsigned));b.StructureByteStride=sizeof(unsigned);b.Usage=D3D11_USAGE_DYNAMIC;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    check_splat(device_->CreateBuffer(&b,nullptr,&splat_order_),"Gaussian order buffer");
    check_splat(device_->CreateShaderResourceView(splat_order_.Get(),nullptr,&splat_order_view_),"Gaussian order view");
    splat_visible_.reserve(level.splats.size());splat_sorted_.reserve(level.splats.size());
    }
    if(!level.splat_surface.vertices.empty()) {
        std::vector<RenderVertex> vertices;vertices.reserve(level.splat_surface.vertices.size());
        for(const auto& v:level.splat_surface.vertices)vertices.push_back({v.position,v.color.x,v.color.y,v.color.z,1,v.u,v.v,1});
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(vertices.size()*sizeof(RenderVertex));desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=vertices.data();check_splat(device_->CreateBuffer(&desc,&initial,&splat_surface_buffer_),"Gaussian coverage mesh");splat_surface_vertices_=UINT(vertices.size());
        // High-resolution scenery atlases need mipmaps: sampling the full-size
        // sheet on distant floors/trim produces crawling speckles during turns.
        D3D11_TEXTURE2D_DESC tex{};tex.Width=level.splat_surface.width;tex.Height=level.splat_surface.height;tex.MipLevels=0;tex.ArraySize=1;tex.Format=DXGI_FORMAT_R8G8B8A8_UNORM;tex.SampleDesc.Count=1;tex.Usage=D3D11_USAGE_DEFAULT;tex.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;tex.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        check_splat(device_->CreateTexture2D(&tex,nullptr,&texture),"Scenery atlas texture");
        context_->UpdateSubresource(texture.Get(),0,nullptr,level.splat_surface.rgba.data(),tex.Width*4,0);
        check_splat(device_->CreateShaderResourceView(texture.Get(),nullptr,&splat_surface_texture_),"Scenery atlas texture view");
        context_->GenerateMips(splat_surface_texture_.Get());
        if(!splat_surface_sampler_){D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_ANISOTROPIC;sampler.MaxAnisotropy=8;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;check_splat(device_->CreateSamplerState(&sampler,&splat_surface_sampler_),"Scenery atlas sampler");}
    }
}
void Renderer::draw_splats(const Level& level,Vec3 eye,Vec3 focus,Vec3 camera_up,float fov){
    if(splat_surface_buffer_){
        UINT stride=sizeof(RenderVertex),offset=0;auto* buffer=splat_surface_buffer_.Get();auto* tex=splat_surface_texture_.Get();auto* sampler=splat_surface_sampler_.Get();
        context_->IASetVertexBuffers(0,1,&buffer,&stride,&offset);context_->PSSetShaderResources(0,1,&tex);context_->PSSetSamplers(0,1,&sampler);context_->Draw(splat_surface_vertices_,0);
        tex=level_texture_.Get();sampler=point_sampler_.Get();context_->PSSetShaderResources(0,1,&tex);context_->PSSetSamplers(0,1,&sampler);
    }
    if(!splat_data_)return;
    const auto forward=normalized(focus-eye),right=normalized(cross(camera_up,forward)),up=cross(forward,right);
    const float tan_y=std::tan(fov*.5f),tan_x=tan_y*width_/height_;
    // Linear-time depth buckets avoid an N log N CPU sort at 60 Hz. Reciprocal depth
    // gives fine ordering near the player and coarser precision in distant decor.
    constexpr unsigned bins=32768;std::array<unsigned,bins> counts{};splat_visible_.clear();
    for(unsigned i=0;i<level.splats.size();++i){
        const auto& s=level.splats[i];const auto d=s.position-eye;const float z=dot(d,forward);
        const float margin=s.reserved0;
        if(z<=.05f||z>1500||std::abs(dot(d,right))>z*tan_x+margin||std::abs(dot(d,up))>z*tan_y+margin)continue;
        const unsigned bucket=std::min(bins-1,unsigned((bins-1)/(1+z*.07f)));
        ++counts[bucket];splat_visible_.push_back({bucket,i});
    }
    if(splat_visible_.empty())return;
    unsigned total=0;for(auto& n:counts){const auto old=n;n=total;total+=old;}
    splat_sorted_.resize(total);for(auto [bucket,index]:splat_visible_)splat_sorted_[counts[bucket]++]=index;
    D3D11_MAPPED_SUBRESOURCE mapped{};check_splat(context_->Map(splat_order_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Gaussian order map");
    std::memcpy(mapped.pData,splat_sorted_.data(),total*sizeof(unsigned));context_->Unmap(splat_order_.Get(),0);
    const std::array<float,20> camera={eye.x,eye.y,eye.z,0,right.x,right.y,right.z,0,up.x,up.y,up.z,0,forward.x,forward.y,forward.z,0,width_/(2*tan_x),height_/(2*tan_y),float(width_),float(height_)};
    context_->UpdateSubresource(splat_camera_.Get(),0,nullptr,camera.data(),0,0);
    auto* cb=splat_camera_.Get();context_->VSSetConstantBuffers(0,1,&cb);
    ID3D11ShaderResourceView* views[]={splat_data_view_.Get(),splat_order_view_.Get()};context_->VSSetShaderResources(0,2,views);
    context_->IASetInputLayout(nullptr);context_->VSSetShader(splat_vs_.Get(),nullptr,0);context_->PSSetShader(splat_ps_.Get(),nullptr,0);
    context_->OMSetBlendState(retail_blends_[0].Get(),nullptr,0xffffffff);context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
    context_->DrawInstanced(6,total,0,0);
    context_->OMSetBlendState(splat_depth_blend_.Get(),nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);context_->PSSetShader(splat_depth_ps_.Get(),nullptr,0);
    context_->DrawInstanced(6,total,0,0);
    ID3D11ShaderResourceView* empty[2]={};context_->VSSetShaderResources(0,2,empty);
    cb=matrix_.Get();context_->VSSetConstantBuffers(0,1,&cb);context_->IASetInputLayout(layout_.Get());
    context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
}
}
