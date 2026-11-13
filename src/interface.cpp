#include "spyro/game_build.hpp"
#include "spyro/interface.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
constexpr std::size_t offset(unsigned address){return address-0x8000f800;}
struct Atlas {
    InterfaceAssets& out;unsigned x=1,y=1,row=0;
    HudSprite add(View vram,unsigned page,unsigned u,unsigned v,unsigned clut,unsigned w,unsigned h,unsigned tint=0x808080,bool half=false) {
        if(!w || !h)return {};
        if(w>256 || h>256)throw FormatError("Invalid interface sprite extent");
        if(x+w+1>=out.width){x=1;y+=row+2;row=0;}
        if(y+h+1>4096)throw FormatError("Interface atlas is too large");
        out.rgba.resize(std::max(out.rgba.size(),std::size_t(out.width)*(y+h+2)*4));
        HudSprite s{x,y,w,h};
        const auto word=[&](unsigned xx,unsigned yy){
            if(vram.size()==1024*512*2){if(xx>=1024 || yy>=512)throw FormatError("Interface texture outside VRAM: page "+std::to_string(page)+", CLUT "+std::to_string(clut)+", x/y "+std::to_string(xx)+"/"+std::to_string(yy));return read_u16(vram,(yy*1024+xx)*2);}
            if(xx<512 || xx>=1024 || yy>=512)throw FormatError("Interface texture outside VRAM");return read_u16(vram,(yy*512+xx-512)*2);
        };
        const unsigned depth=(page>>7)&3;if(depth>1)throw FormatError("Unsupported interface texture depth");
        const unsigned per=depth?2:4,bits=depth?8:4;
        for(unsigned dy=0;dy<h;++dy)for(unsigned dx=0;dx<w;++dx) {
            const unsigned xx=(u+dx)&255,yy=(v+dy)&255;
            const auto p=word((page&15)*64+xx/per,((page>>4)&1)*256+yy);
            const auto c=word((clut&63)*16+((p>>((xx%per)*bits))&((1<<bits)-1)),clut>>6);
            const auto at=((y+dy)*out.width+x+dx)*4;
            for(unsigned k=0;k<3;++k){const auto n=(c>>(k*5))&31;out.rgba[at+k]=static_cast<std::uint8_t>(std::min(255u,((n<<3)|(n>>2))*((tint>>(k*8))&255)/128));}
            out.rgba[at+3]=c?(half && (c&0x8000)?128:255):0;
        }
        // Extrude one texel into the packing gutter. ImGui's linear sampler
        // otherwise blends transparent atlas padding into every parchment tile.
        for(int dy=-1;dy<=static_cast<int>(h);++dy)for(int dx=-1;dx<=static_cast<int>(w);++dx) {
            if(dx>=0 && dx<static_cast<int>(w) && dy>=0 && dy<static_cast<int>(h))continue;
            const auto src=((y+std::clamp(dy,0,int(h)-1))*out.width+x+std::clamp(dx,0,int(w)-1))*4;
            const auto dst=((y+dy)*out.width+x+dx)*4;std::copy_n(out.rgba.begin()+src,4,out.rgba.begin()+dst);
        }
        x+=w+2;row=std::max(row,h);out.height=std::max(out.height,y+h+2);return s;
    }
};
std::string string_at(View exe,unsigned address){std::string s;for(auto p=offset(address);p<exe.size() && s.size()<1024;++p){if(!exe[p])return s;s+=static_cast<char>(exe[p]);}throw FormatError("Invalid interface string");}
InterfaceAssets early_interface(View archive,View exe,const ExeTables& tables) {
    const auto sections=parse_level_archive(archive);const auto source=archive.subspan(sections[0].offset,sections[0].size);
    const bool august=tables.profile.build==GameBuild::August1999;
    const unsigned pause_bytes=august?0x6000:0x2800;
    if(source.size()<0x80000+pause_bytes)throw FormatError("Missing prerelease interface bank");
    Bytes vram(1024*512*2);
    for(unsigned y=0;y<512;++y)std::copy_n(source.begin()+y*1024,1024,vram.begin()+(y*1024+512)*2);
    InterfaceAssets out;out.width=2048;out.height=1;Atlas atlas{out};
    const auto font=tables(0x800636a4),colors=tables(0x800637d4);
    const unsigned color_count=august?10:tables.profile.demo()?4:6;
    for(unsigned c=0;c<128;++c) {
        auto& g=out.font[c];g.descriptor=august?read_u16(exe,font+c*2):std::uint16_t(0x0e00|exe[font+c]);
        const auto index=g.descriptor&255;if(index==255)continue;
        const bool controller=c>0 && c<8;
        for(unsigned color=0;color<g.colors.size();++color)
            g.colors[color]=atlas.add(vram,0x618,(index&63)*4,(index>>6)*9,controller?0x49a1:0x4920,12,9,
                controller?0x808080:read_u32(exe,colors+std::min(color,color_count-1)*4));
    }
    constexpr unsigned types[]{0,0,0,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,5,5,5,5,6,7,4,2,2,9,10,10,10,10,11,11,11,11,12,12,16,17,17,13,13,14,14,15,15,20,20,20,20};
    for(unsigned i=0;i<out.resident.size();++i) {
        const auto type=types[i];if(type>=(august?20u:16u))continue;
        const auto p=tables(0x8006351c)+i*4;
        out.resident[i]=atlas.add(vram,0x38,exe[p],exe[p+1],0x4020+(exe[p+2]&3)+exe[p+3]*64,
            exe[tables(0x800634b8)+type],exe[tables(0x800634d0)+type],0x808080,(exe[p+2]&128)!=0);
    }
    // July's 128x40 pause upload occupies left VRAM (384,472). August
    // moves to the later 192x64 upload at (512,128); do not decode SPU bytes
    // as the missing 0x3800 bytes of the retail pause sheet.
    const unsigned width=august?192:128,height=august?64:40,x=august?512:384,y=august?128:472;
    for(unsigned row=0;row<height;++row)std::copy_n(source.begin()+0x80000+row*width*2,width*2,vram.begin()+((y+row)*1024+x)*2);
    // July/Jampack have 19 descriptors; the following bytes are layout
    // commands, not the seven additional sprites introduced for retail.
    for(unsigned i=0;i<(august?out.pause.size():19u);++i) {
        const auto p=tables(0x8006387c)+i*6;
        if(august)out.pause[i]=atlas.add(vram,(exe[p]>>6)+8,(exe[p]&63)*4,(exe[p+1]+128)&255,(exe[p+2]&127)+exe[p+3]*64+0x2020,exe[p+4],exe[p+5]);
        else out.pause[i]=atlas.add(vram,(exe[p]>>7)+0x16,(exe[p]*2)&255,(exe[p+1]+216)&255,exe[p+2]+exe[p+3]*64+0x7618,exe[p+4],exe[p+5]);
    }
    for(unsigned i=0;i<29;++i) {
        auto& level=out.levels[i];level.scene=exe[tables(0x80064994)+i];
        level.gems=exe[tables(0x800647f4)+i]*100;level.spirits=exe[tables(0x80064814)+i];level.orbs=exe[tables(0x80064834)+i];
        const auto name_address=read_u32(exe,tables(0x800649b4)+i*4);
        if(!name_address && tables.profile.demo())continue; // Unnamed/unavailable Ripto entry.
        const auto p=tables.native(name_address);
        for(auto q=p;q<exe.size() && exe[q];++q){if(level.name.size()>=256)throw FormatError("Unterminated prerelease level name");level.name+=char(exe[q]);}
        out.order[i]=i;
    }
    for(unsigned i=0;i<18;++i)out.dialogue_enter[i]=std::bit_cast<short>(read_u16(exe,tables(0x800633f4)+i*2));
    for(unsigned i=0;i<12;++i)out.dialogue_exit[i]=std::bit_cast<short>(read_u16(exe,tables(0x80063418)+i*2));
    for(unsigned i=0;i<320;++i)out.page_sine[i]=std::bit_cast<short>(read_u16(exe,tables(0x80061bd8)+i*2));
    out.continue_arrow=">";out.rgba.resize(out.width*out.height*4);return out;
}
}
InterfaceAssets decode_interface(View archive,View exe,View book,bool ripto,bool boss) {
    const ExeTables tables(exe);
    if(tables.profile.prerelease())return early_interface(archive,exe,tables);
    const auto children=parse_level_archive(archive);
    if(children.empty() || children[0].size<0x86000 || book.size()!=0x180000)throw FormatError("Missing retail interface texture banks");
    const auto source=archive.subspan(children[0].offset,children[0].size);
    Bytes vram(source.begin(),source.begin()+0x80000);
    InterfaceAssets out;out.width=2048;out.height=1;Atlas atlas{out};
    std::copy_n(exe.begin()+offset(0x800637a4),46,out.escapes.begin());
    for(unsigned c=0;c<128;++c) {
        auto& g=out.font[c];g.descriptor=read_u16(exe,offset(0x800636a4)+c*2);const auto index=g.descriptor&255;
        if(index==255)continue;
        const bool controller=c==60 || c==62 || c==123 || c==125;
        for(unsigned color=0;color<14;++color)g.colors[color]=atlas.add(vram,0x618,(index&31)*12,index/32*9,controller?0x4962:0x4922,12,9,controller?0x808080:read_u32(exe,offset(0x800637d4)+color*4));
    }
    constexpr unsigned types[]{0,0,0,0,0,0,0,0,0,0,2,1,1,1,1,1,1,1,1,1,1,1,5,5,5,5,6,7,4,2,2,9,10,10,10,10,11,11,11,11,12,12,16,17,17,13,13,14,14,15,15,20,20,20,20};
    for(unsigned i=0;i<out.resident.size();++i) {
        const auto p=offset(0x8006351c)+i*4;const auto type=types[i];
        out.resident[i]=atlas.add(vram,0x38,exe[p],exe[p+1],0x4020+(exe[p+2]&3)+exe[p+3]*64,exe[offset(0x800634b8)+type],exe[offset(0x800634d0)+type],0x808080,(exe[p+2]&128)!=0);
    }
    if(ripto) {
        auto& boss=out.ripto.emplace();
        const auto sprite=[&](unsigned address,unsigned type,unsigned tint=0x808080,unsigned palette=0) {
            const auto p=offset(address);
            return atlas.add(vram,0x38,exe[p],exe[p+1],palette?palette:0x4020+(exe[p+2]&3)+exe[p+3]*64,
                exe[offset(0x800634b8)+type],exe[offset(0x800634d0)+type],tint,(exe[p+2]&128)!=0);
        };
        // Overlay 70, 80070b30: these scene-local sprites must be read
        // before the guidebook uploads replace their VRAM region.
        boss.orb_rim=sprite(0x8006368c,19);boss.empty_orb=sprite(0x80063688,19,0x303030);
        for(unsigned color=0;color<3;++color)for(unsigned i=0;i<=180;++i)
            boss.orbs[color][i]=sprite(0x80063688,19,(i+70)<<(color*8));
        for(unsigned i=0;i<5;++i)boss.portraits[i]=sprite(0x80063690+i*4,18);
        boss.portraits[5]=sprite(0x800636a0,18,0x808080,0x5c20);
    }
    if(boss) {
        // Crush / Gulp: also scene-local, read before the guidebook uploads.
        auto& hud=out.boss.emplace();
        const auto sprite=[&](unsigned address,unsigned type,unsigned palette=0) {
            const auto p=offset(address);
            return atlas.add(vram,0x38,exe[p],exe[p+1],palette?palette:0x4020+(exe[p+2]&3)+exe[p+3]*64,
                exe[offset(0x800634b8)+type],exe[offset(0x800634d0)+type],0x808080,(exe[p+2]&128)!=0);
        };
        hud.frame=sprite(0x80063660,21);
        for(unsigned i=0;i<4;++i)hud.eyes[i]=sprite(0x80063690+i*4,18);
        for(unsigned i=0;i<3;++i){hud.full[i]=sprite(0x800635f8+(13+i)*4,7);hud.empty[i]=sprite(0x800635f8+(9+i)*4,7,0x5960);}
    }
    out.scene_vram=vram;   // before the guidebook pages overwrite rows 128+ (scene-local HUD sprites)
    const auto upload=[&](View raw,unsigned w,unsigned h){if(raw.size()<w*h*2)throw FormatError("Truncated guidebook page");for(unsigned y=0;y<h;++y)std::copy_n(raw.begin()+y*w*2,w*2,vram.begin()+(128+y)*1024);};
    const auto sprites=[&](auto& dest,unsigned address,unsigned mode) {
        for(unsigned i=0;i<dest.size();++i) {
            const auto p=offset(address)+i*6;const bool depth=(mode==1 && i<3)||(mode==2 && (i==0 || i==14 || i==18));const bool half=(exe[p+2]&128)!=0;
            dest[i]=atlas.add(vram,(exe[p]>>6)+8+unsigned(depth)*128,(exe[p]&63)<<(2-unsigned(depth)),(exe[p+1]+128)&255,(exe[p+2]&127)+exe[p+3]*64+0x2020,exe[p+4],exe[p+5],half?0x404040:0x808080,half);
        }
    };
    upload(source.subspan(0x80000,0x6000),192,64);sprites(out.pause,0x8006387c,0);
    upload(book.first(0xc000),256,96);sprites(out.contents,0x80063e38,1);
    for(unsigned i=0;i<31;++i){upload(book.subspan((i+1)*0xc000,0xc000),256,96);sprites(out.pages[i],0x80063ec0,2);}
    constexpr unsigned addresses[]{0x80063918,0x800639b8,0x80063bf8,0x80063c78,0x80063cf8,0x80063d98};
    constexpr unsigned counts[]{40,49,32,32,40,40};
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<counts[i];++j){const auto p=offset(addresses[i])+j*4;out.layouts[i].push_back({exe[p],exe[p+1],exe[p+2],exe[p+3]});}
    for(unsigned i=0;i<29;++i){auto& l=out.levels[i];l.scene=exe[offset(0x80064994)+i];l.gems=exe[offset(0x800647f4)+i]*100;l.spirits=exe[offset(0x80064814)+i];l.orbs=exe[offset(0x80064834)+i];l.name=string_at(exe,read_u32(exe,offset(0x800649b4)+i*4));out.order[i]=exe[offset(0x80063ea0)+i];}
    for(unsigned i=0;i<29;++i)for(unsigned j=0;j<4;++j)out.levels[i].orb_bits[j]=exe[offset(0x80064854)+i*4+j];
    for(unsigned i=0;i<18;++i)out.dialogue_enter[i]=std::bit_cast<short>(read_u16(exe,offset(0x800633f4)+i*2));
    for(unsigned i=0;i<12;++i)out.dialogue_exit[i]=std::bit_cast<short>(read_u16(exe,offset(0x80063418)+i*2));
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)out.flight_labels[i][j]=string_at(exe,read_u32(exe,offset(0x80061930)+i*0x58+j*4));
    for(unsigned i=0;i<320;++i)out.page_sine[i]=std::bit_cast<short>(read_u16(exe,offset(0x80061bd8)+i*2));
    std::copy_n(exe.begin()+offset(0x80063870),12,out.page_angles.begin());out.continue_arrow=string_at(exe,0x80066eb0);
    for(unsigned i=0;i<20;++i){out.skills[i]=string_at(exe,read_u32(exe,offset(0x80064720)+i*4));out.skill_levels[i]=exe[offset(0x80064770)+i];}
    out.atlas_x=atlas.x;out.atlas_y=atlas.y;out.atlas_row=atlas.row;
    out.rgba.resize(out.width*out.height*4);return out;
}
const HudSprite& scene_sprite(InterfaceAssets& out,unsigned u,unsigned v,unsigned clut,unsigned width,unsigned height,unsigned color,bool half) {
    const std::pair<std::uint64_t,std::uint32_t> key{std::uint64_t(u&255)|std::uint64_t(v&255)<<8|std::uint64_t(clut&0xffff)<<16|std::uint64_t(width&255)<<32|
        std::uint64_t(height&255)<<40|std::uint64_t(half)<<48,color&0xffffff};
    if(const auto found=out.scene_sprites.find(key);found!=out.scene_sprites.end())return found->second;
    HudSprite sprite{};
    if(out.scene_vram.size()>=0x80000 && width && height && width<=256 && height<=256) {
        Atlas atlas{out,out.atlas_x,out.atlas_y,out.atlas_row};
        try{sprite=atlas.add(out.scene_vram,0x38,u,v,clut,width,height,color,half);}catch(const FormatError&){sprite={};}
        out.atlas_x=atlas.x;out.atlas_y=atlas.y;out.atlas_row=atlas.row;
        out.rgba.resize(std::size_t(out.width)*out.height*4);out.changed=true;
    }
    return out.scene_sprites.emplace(key,sprite).first->second;
}
std::string retail_pc_controls(std::string_view text,bool controller) {
    // Cross, Square, Circle, Triangle: the keys, or the controller's A X B Y.
    std::string result;
    for(char c:text)switch(c){case '<':result+=controller?"A":"Space";break;case '>':result+=controller?"X":"Shift";break;
        case '{':result+=controller?"B":"F";break;case '}':result+=controller?"Y":"H";break;default:result+=c;}
    return result;
}
RetailText layout_retail_text(const InterfaceAssets& a,std::string_view text,unsigned color,float wrap,bool dense) {
    RetailText result;float x=0,y=0;unsigned word_begin=0;float word_x=0;
    const auto newline=[&]{result.width=std::max(result.width,x);x=0;y+=12;++result.lines;word_begin=static_cast<unsigned>(result.glyphs.size());word_x=0;};
    for(unsigned i=0;i<text.size();++i) {
        unsigned c=static_cast<unsigned char>(text[i]);bool overlay=false;
        if(c=='@' && i+1<text.size()) {
            c=static_cast<unsigned char>(text[++i]);bool escape=false;
            for(unsigned j=0;j<a.escapes.size();j+=2)if(a.escapes[j]==c){overlay=(a.escapes[j+1]&128)!=0;c=(a.escapes[j+1]&127)/2;escape=true;break;}
            if(!escape){if(c>='0' && c<='9')color=c-'0';continue;}
        }
        if(c=='\n' || c=='\\' || c=='&'){newline();continue;}
        if(c>=128)c='?';
        const auto d=a.font[c].descriptor;const float advance=(d&255)==255?(dense?8.0f:10.0f):float((d>>8)&15);
        if(c==' '){x+=advance;word_begin=static_cast<unsigned>(result.glyphs.size());word_x=x;continue;}
        if(wrap>0 && x+advance>wrap && x>0) {
            if(word_x>0 && word_begin<result.glyphs.size()) {
                result.width=std::max(result.width,word_x-10);for(unsigned j=word_begin;j<result.glyphs.size();++j){result.glyphs[j].x-=word_x;result.glyphs[j].y+=12;}
                x-=word_x;y+=12;++result.lines;word_x=0;
            }else newline();
        }
        if(overlay)x-=advance;
        const int dy=static_cast<int>(static_cast<short>(d))>>12;
        if((d&255)!=255)result.glyphs.push_back({c,std::min(color,13u),i+1,x,y+dy});
        x+=advance;
    }
    result.width=std::max(result.width,x);result.height=y+9;return result;
}
unsigned guide_completion(const GuideLevel& level,unsigned gems,unsigned orbs,bool talisman) {
    const bool token=(level.scene%10>=1 && level.scene%10<=4 && level.scene<50) || level.scene%10==6;
    const unsigned possible=level.gems+level.orbs*200+(token?500:0),earned=std::min(gems,level.gems)+std::min(orbs,level.orbs)*200+(token && talisman?500:0);
    return possible && earned?std::max(1u,earned*100/possible):0;
}
std::array<float,2> project_book_point(const InterfaceAssets& assets,float x,float y,float angle) {
    // 8004bf94/8004c068: book spine (256,120), depth 512, 12-bit sine table.
    const auto phase=static_cast<unsigned>(angle)&255;const float f=angle-std::floor(angle);
    const float sine=std::lerp(float(assets.page_sine[phase]),float(assets.page_sine[(phase+1)&255]),f)/4096;
    const float cosine=std::lerp(float(assets.page_sine[phase+64]),float(assets.page_sine[((phase+1)&255)+64]),f)/4096;
    const float depth=512-(x-256)*sine;
    return {256+(x-256)*cosine*512/depth,120+(y-120)*512/depth};
}
} // namespace spyro
