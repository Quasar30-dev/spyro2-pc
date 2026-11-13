// PS1 Geometry Transformation Engine (COP2) for the overlay interpreter.
// Follows the psx-spx description: register quirks, MAC/IR saturation, the
// screen/depth FIFOs and the unsigned Newton-Raphson division of RTPS/RTPT.
#include "spyro/overlay_vm.hpp"
#include <algorithm>
#include <bit>

namespace spyro {
namespace {
std::int32_t lo16(std::uint32_t v){return static_cast<std::int16_t>(v&0xffff);}
std::int32_t hi16(std::uint32_t v){return static_cast<std::int16_t>(v>>16);}
std::uint32_t pack16(std::int32_t a,std::int32_t b){return (static_cast<std::uint32_t>(a)&0xffff)|(static_cast<std::uint32_t>(b)<<16);}
// The unsigned division table used by RTPS/RTPT (UNR reciprocal seeds).
std::uint8_t unr(unsigned i) {
    static const std::array<std::uint8_t,257> table=[]{
        std::array<std::uint8_t,257> t{};
        for(unsigned k=0;k<257;++k)t[k]=static_cast<std::uint8_t>(std::max(0,static_cast<int>((0x40000/(k+0x100)+1)/2)-0x101));
        return t;
    }();
    return table[i];
}
std::uint32_t divide(std::uint32_t h,std::uint32_t sz3,std::uint32_t& flags) {
    if(h>=sz3*2){flags|=1u<<17;return 0x1ffff;}
    const unsigned z=static_cast<unsigned>(std::countl_zero(static_cast<std::uint16_t>(sz3)));
    const std::uint64_t n=static_cast<std::uint64_t>(h)<<z;std::uint64_t d=static_cast<std::uint64_t>(sz3)<<z;
    const std::uint64_t u=unr(static_cast<unsigned>((d-0x7fc0)>>7))+0x101;
    d=(0x2000080-d*u)>>8;d=(0x0000080+d*u)>>8;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(0x1ffff,(n*d+0x8000)>>16));
}
}
std::uint32_t Gte::read_data(unsigned r) {
    switch(r) {
    case 1:case 3:case 5:case 8:case 9:case 10:case 11:return static_cast<std::uint32_t>(lo16(data[r]));
    case 7:case 16:case 17:case 18:case 19:return data[r]&0xffff;
    case 15:return data[14];
    case 28:case 29: {
        const auto c=[&](unsigned i){return static_cast<std::uint32_t>(std::clamp(lo16(data[9+i])>>7,0,31));};
        return c(0)|(c(1)<<5)|(c(2)<<10);
    }
    case 31: {
        const auto v=data[30];
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(v)<0?std::countl_one(v):std::countl_zero(v));
    }
    default:return data[r];
    }
}
void Gte::write_data(unsigned r,std::uint32_t v) {
    switch(r) {
    case 15:data[12]=data[13];data[13]=data[14];data[14]=v;data[15]=v;break;
    case 28:data[28]=v&0x7fff;data[9]=(v&31)<<7;data[10]=((v>>5)&31)<<7;data[11]=((v>>10)&31)<<7;break;
    case 29:case 31:break;
    default:data[r]=v;break;
    }
}
void Gte::write_control(unsigned r,std::uint32_t v) {
    switch(r) {
    case 4:case 12:case 20:case 26:case 27:case 29:case 30:control[r]=static_cast<std::uint32_t>(lo16(v));break;
    case 31:control[31]=v&0x7ffff000;if(control[31]&0x7f87e000)control[31]|=0x80000000;break;
    default:control[r]=v;break;
    }
}
std::int64_t Gte::a_check(unsigned i,std::int64_t v) {
    if(v>0x7ffffffffffLL)flags|=1u<<(31-i);
    if(v< -0x80000000000LL)flags|=1u<<(28-i);
    return (v<<20)>>20;   // 44-bit sign wrap
}
std::int64_t Gte::mac(unsigned i,std::int64_t v) {
    if(i==0) {
        if(v>0x7fffffffLL)flags|=1u<<16;else if(v< -0x80000000LL)flags|=1u<<15;
        data[24]=static_cast<std::uint32_t>(v);return v;
    }
    data[24+i]=static_cast<std::uint32_t>(v);return v;
}
std::int32_t Gte::ir(unsigned i,std::int64_t v,bool lm) {
    const std::int64_t lo=lm?0:-0x8000,hi=0x7fff;std::int64_t out=v;
    if(v<lo){out=lo;flags|=1u<<(25-i);}else if(v>hi){out=hi;flags|=1u<<(25-i);}
    data[8+i]=static_cast<std::uint32_t>(static_cast<std::int32_t>(out));return static_cast<std::int32_t>(out);
}
void Gte::push_sz(std::int64_t z) {
    if(z<0){z=0;flags|=1u<<18;}else if(z>0xffff){z=0xffff;flags|=1u<<18;}
    data[16]=data[17];data[17]=data[18];data[18]=data[19];data[19]=static_cast<std::uint32_t>(z);
}
void Gte::push_sxy(std::int64_t x,std::int64_t y) {
    if(x< -0x400){x=-0x400;flags|=1u<<14;}else if(x>0x3ff){x=0x3ff;flags|=1u<<14;}
    if(y< -0x400){y=-0x400;flags|=1u<<13;}else if(y>0x3ff){y=0x3ff;flags|=1u<<13;}
    data[12]=data[13];data[13]=data[14];data[14]=pack16(static_cast<std::int32_t>(x),static_cast<std::int32_t>(y));
}
void Gte::push_rgb(std::int64_t r,std::int64_t g,std::int64_t b) {
    const auto c=[&](std::int64_t v,unsigned bit){if(v<0){flags|=1u<<bit;return 0u;}if(v>255){flags|=1u<<bit;return 255u;}return static_cast<unsigned>(v);};
    data[20]=data[21];data[21]=data[22];
    data[22]=c(r,21)|(c(g,20)<<8)|(c(b,19)<<16)|(data[6]&0xff000000);
}
void Gte::rtp(unsigned v,bool last,int sf,bool lm) {
    const std::int64_t vx=lo16(data[v*2]),vy=hi16(data[v*2]),vz=lo16(data[v*2+1]);
    const auto m=[&](unsigned i,unsigned j){const auto w=control[(i*3+j)/2];return static_cast<std::int64_t>((i*3+j)&1?hi16(w):lo16(w));};
    const std::int64_t tr[3]{static_cast<std::int32_t>(control[5]),static_cast<std::int32_t>(control[6]),static_cast<std::int32_t>(control[7])};
    std::int64_t out[3];
    for(unsigned i=0;i<3;++i)out[i]=mac(1+i,a_check(1+i,a_check(1+i,a_check(1+i,(tr[i]<<12)+m(i,0)*vx)+m(i,1)*vy)+m(i,2)*vz)>>sf);
    ir(1,out[0],lm);ir(2,out[1],lm);ir(3,out[2],lm);
    push_sz(static_cast<std::int64_t>(static_cast<std::int32_t>(data[27]))>>(sf?0:12));
    const auto q=divide(control[26]&0xffff,data[19]&0xffff,flags);
    const std::int64_t ofx=static_cast<std::int32_t>(control[24]),ofy=static_cast<std::int32_t>(control[25]);
    const std::int64_t x=mac(0,static_cast<std::int64_t>(q)*lo16(data[9])+ofx)>>16,y=mac(0,static_cast<std::int64_t>(q)*lo16(data[10])+ofy)>>16;
    push_sxy(x,y);
    if(last) {
        const std::int64_t d=mac(0,static_cast<std::int64_t>(q)*static_cast<std::int32_t>(control[27])+static_cast<std::int32_t>(control[28]));
        std::int64_t h=d>>12;if(h<0){h=0;flags|=1u<<12;}else if(h>0x1000){h=0x1000;flags|=1u<<12;}data[8]=static_cast<std::uint32_t>(h);
    }
}
void Gte::mvmva(std::uint32_t op) {
    const int sf=(op&(1u<<19))?12:0;const bool lm=(op&(1u<<10))!=0;
    const unsigned mx=(op>>17)&3,vsel=(op>>15)&3,cv=(op>>13)&3;
    std::int64_t v[3];
    if(vsel<3){v[0]=lo16(data[vsel*2]);v[1]=hi16(data[vsel*2]);v[2]=lo16(data[vsel*2+1]);}
    else{v[0]=lo16(data[9]);v[1]=lo16(data[10]);v[2]=lo16(data[11]);}
    const auto mat=[&](unsigned i,unsigned j)->std::int64_t {
        if(mx==3) {   // garbage matrix quirk
            const std::int64_t r=static_cast<std::int64_t>(static_cast<std::uint8_t>(data[6]))<<4;
            const std::int64_t row0[3]{-r,r,lo16(data[8])},row1[3]{lo16(control[1]),lo16(control[1]),lo16(control[1])},row2[3]{lo16(control[2]),lo16(control[2]),lo16(control[2])};
            return i==0?row0[j]:i==1?row1[j]:row2[j];
        }
        const unsigned base=mx*8;const unsigned k=i*3+j;const auto w=control[base+k/2];return k&1?hi16(w):lo16(w);
    };
    const auto translate=[&](unsigned i)->std::int64_t {
        if(cv==0)return static_cast<std::int32_t>(control[5+i]);
        if(cv==1)return static_cast<std::int32_t>(control[13+i]);
        if(cv==2)return static_cast<std::int32_t>(control[21+i]);
        return 0;
    };
    for(unsigned i=0;i<3;++i) {
        std::int64_t acc=a_check(1+i,translate(i)<<12);
        if(cv==2) {   // far-colour quirk: only the last two products accumulate
            const auto part=a_check(1+i,acc+mat(i,0)*v[0]);ir(1+i,part>>sf,false);
            acc=a_check(1+i,mat(i,1)*v[1]);acc=a_check(1+i,acc+mat(i,2)*v[2]);
        } else {
            acc=a_check(1+i,acc+mat(i,0)*v[0]);acc=a_check(1+i,acc+mat(i,1)*v[1]);acc=a_check(1+i,acc+mat(i,2)*v[2]);
        }
        mac(1+i,acc>>sf);
    }
    for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);
}
void Gte::ncds(unsigned v,int sf,bool lm,bool depth,bool color) {
    // Normal x light matrix -> IR, then background + colour matrix, then the
    // optional vertex colour multiply and depth cue toward the far colour.
    const std::int64_t n[3]{lo16(data[v*2]),hi16(data[v*2]),lo16(data[v*2+1])};
    const auto L=[&](unsigned i,unsigned j){const unsigned k=i*3+j;const auto w=control[8+k/2];return static_cast<std::int64_t>(k&1?hi16(w):lo16(w));};
    const auto C=[&](unsigned i,unsigned j){const unsigned k=i*3+j;const auto w=control[16+k/2];return static_cast<std::int64_t>(k&1?hi16(w):lo16(w));};
    for(unsigned i=0;i<3;++i)mac(1+i,a_check(1+i,L(i,0)*n[0]+L(i,1)*n[1]+L(i,2)*n[2])>>sf);
    for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);
    std::int64_t lit[3]{lo16(data[9]),lo16(data[10]),lo16(data[11])};
    for(unsigned i=0;i<3;++i)mac(1+i,a_check(1+i,(static_cast<std::int64_t>(static_cast<std::int32_t>(control[13+i]))<<12)+C(i,0)*lit[0]+C(i,1)*lit[1]+C(i,2)*lit[2])>>sf);
    for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);
    if(color) {
        const std::int64_t rgb[3]{data[6]&255,(data[6]>>8)&255,(data[6]>>16)&255};
        for(unsigned i=0;i<3;++i)mac(1+i,(rgb[i]<<4)*lo16(data[9+i]));
        if(depth)for(unsigned i=0;i<3;++i) {
            const std::int64_t fc=static_cast<std::int32_t>(control[21+i]);const std::int64_t m=static_cast<std::int32_t>(data[25+i]);
            const auto diff=ir(1+i,((fc<<12)-m)>>sf,false);
            mac(1+i,m+lo16(data[8])*diff);
        }
        for(unsigned i=0;i<3;++i)mac(1+i,static_cast<std::int32_t>(data[25+i])>>sf);
    } else for(unsigned i=0;i<3;++i)mac(1+i,static_cast<std::int32_t>(data[25+i]));
    for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);
    push_rgb(static_cast<std::int32_t>(data[25])>>4,static_cast<std::int32_t>(data[26])>>4,static_cast<std::int32_t>(data[27])>>4);
}
void Gte::command(std::uint32_t op) {
    flags=0;
    const int sf=(op&(1u<<19))?12:0;const bool lm=(op&(1u<<10))!=0;
    switch(op&63) {
    case 0x01:rtp(0,true,sf,lm);break;
    case 0x30:rtp(0,false,sf,lm);rtp(1,false,sf,lm);rtp(2,true,sf,lm);break;
    case 0x06: {
        const std::int64_t x0=lo16(data[12]),y0=hi16(data[12]),x1=lo16(data[13]),y1=hi16(data[13]),x2=lo16(data[14]),y2=hi16(data[14]);
        mac(0,x0*y1+x1*y2+x2*y0-x0*y2-x1*y0-x2*y1);break;
    }
    case 0x0c: {
        const std::int64_t d1=lo16(control[0]),d2=lo16(control[2]),d3=lo16(control[4]);
        const std::int64_t i1=lo16(data[9]),i2=lo16(data[10]),i3=lo16(data[11]);
        mac(1,(i3*d2-i2*d3)>>sf);mac(2,(i1*d3-i3*d1)>>sf);mac(3,(i2*d1-i1*d2)>>sf);
        for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);break;
    }
    case 0x12:mvmva(op);break;
    case 0x28:
        for(unsigned i=0;i<3;++i){const std::int64_t v=lo16(data[9+i]);mac(1+i,(v*v)>>sf);}
        for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);break;
    case 0x2d:case 0x2e: {
        const bool four=(op&63)==0x2e;
        const std::int64_t sum=four?static_cast<std::int64_t>(lo16(control[30]))*(static_cast<std::int64_t>(data[16]&0xffff)+(data[17]&0xffff)+(data[18]&0xffff)+(data[19]&0xffff)):
            static_cast<std::int64_t>(lo16(control[29]))*(static_cast<std::int64_t>(data[17]&0xffff)+(data[18]&0xffff)+(data[19]&0xffff));
        mac(0,sum);std::int64_t z=sum>>12;if(z<0){z=0;flags|=1u<<18;}else if(z>0xffff){z=0xffff;flags|=1u<<18;}data[7]=static_cast<std::uint32_t>(z);break;
    }
    case 0x3d:case 0x3e: {
        const bool base=(op&63)==0x3e;const std::int64_t i0=lo16(data[8]);
        for(unsigned i=0;i<3;++i) {
            const std::int64_t prev=base?(static_cast<std::int64_t>(static_cast<std::int32_t>(data[25+i]))<<sf):0;
            mac(1+i,(prev+i0*lo16(data[9+i]))>>sf);
        }
        for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);
        push_rgb(static_cast<std::int32_t>(data[25])>>4,static_cast<std::int32_t>(data[26])>>4,static_cast<std::int32_t>(data[27])>>4);break;
    }
    case 0x11:case 0x10:case 0x2a: {   // INTPL / DPCS / DPCT: interpolate toward the far colour
        const unsigned count=(op&63)==0x2a?3u:1u;
        for(unsigned k=0;k<count;++k) {
            std::int64_t in[3];
            if((op&63)==0x11)for(unsigned i=0;i<3;++i)in[i]=static_cast<std::int64_t>(lo16(data[9+i]))<<12;
            else{const auto c=(op&63)==0x2a?data[20]:data[6];for(unsigned i=0;i<3;++i)in[i]=static_cast<std::int64_t>((c>>(i*8))&255)<<16;}
            for(unsigned i=0;i<3;++i) {
                const std::int64_t fc=static_cast<std::int32_t>(control[21+i]);
                const auto diff=ir(1+i,((fc<<12)-in[i])>>sf,false);
                mac(1+i,(in[i]+lo16(data[8])*diff)>>sf);
            }
            for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);
            push_rgb(static_cast<std::int32_t>(data[25])>>4,static_cast<std::int32_t>(data[26])>>4,static_cast<std::int32_t>(data[27])>>4);
        }
        break;
    }
    case 0x13:ncds(0,sf,lm,true,true);break;
    case 0x16:for(unsigned v=0;v<3;++v)ncds(v,sf,lm,true,true);break;
    case 0x1b:ncds(0,sf,lm,false,true);break;
    case 0x3f:for(unsigned v=0;v<3;++v)ncds(v,sf,lm,false,true);break;
    case 0x1e:ncds(0,sf,lm,false,false);break;
    case 0x20:for(unsigned v=0;v<3;++v)ncds(v,sf,lm,false,false);break;
    case 0x29:case 0x1c:case 0x14: {   // DCPL / CC / CDP: colour from IR
        const std::int64_t rgb[3]{data[6]&255,(data[6]>>8)&255,(data[6]>>16)&255};
        for(unsigned i=0;i<3;++i)mac(1+i,((rgb[i]<<4)*lo16(data[9+i]))>>sf);
        for(unsigned i=0;i<3;++i)ir(1+i,static_cast<std::int32_t>(data[25+i]),lm);
        push_rgb(static_cast<std::int32_t>(data[25])>>4,static_cast<std::int32_t>(data[26])>>4,static_cast<std::int32_t>(data[27])>>4);break;
    }
    default:break;
    }
    if(flags&0x7f87e000)flags|=0x80000000;
    control[31]=flags;
}
} // namespace spyro
