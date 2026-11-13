// Renders text in the retail Spyro 2 font to a transparent 32-bit TGA, using
// the engine's own interface decode (glyph pixels and colour variants from the
// mounted disc) and layout_retail_text (advances and vertical offsets), so
// editorial plates match what the game draws.
//
// spyro_retail_text_plate <retail data> <out.tga> [options] <text>
//   --scale H     output pixels per glyph texel vertically (default 6)
//   --aspect A    horizontal texel size relative to vertical (default 0.625:
//                 the 512-wide UI shown on a 320-wide screen)
//   --color N     retail text colour 0..13 (default 1)
//   --wrap U      word-wrap width in retail UI units (default none)
//   --center      centre each line (default left)
//   --gap U       extra space between lines in UI units (default 3)
//   --outline P   black outline radius in output pixels (default 0)
//   --shadow X Y  black drop shadow offset in output pixels (default none)
//   --slot S      scene bank to decode the interface from (default 17)
//   --kern-decimal U  pull digits after a decimal point closer (default 3; the
//                 retail period advance reads "2. 5")
// A '|' in the text forces a line break. Retail '@0'..'@9' colour codes carry
// across wrapped lines.
#include "spyro/assets.hpp"
#include "spyro/interface.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace spyro;
namespace {
struct Options {float scale=6,aspect=.625f,wrap=0,gap=3,kern_decimal=3;unsigned color=1,slot=17;bool center=false;int outline=0,shadow_x=0,shadow_y=0;bool shadow=false;};
std::vector<std::string> wrap_lines(const InterfaceAssets& ui,const std::string& text,const Options& o) {
    std::vector<std::string> lines;std::stringstream paragraphs(text);std::string paragraph;
    while(std::getline(paragraphs,paragraph,'|')) {
        std::stringstream words(paragraph);std::string word,line;char colour=0;
        const auto track=[&](const std::string& w){for(std::size_t i=0;i+1<w.size();++i)if(w[i]=='@' && w[i+1]>='0' && w[i+1]<='9')colour=w[++i];};
        while(words>>word) {
            const auto candidate=line.empty()?word:line+' '+word;
            if(o.wrap>0 && !line.empty() && layout_retail_text(ui,candidate,o.color).width>o.wrap) {
                lines.push_back(line);line=colour?std::string{'@',colour}+word:word;
            } else line=candidate;
            track(word);
        }
        lines.push_back(line);
    }
    return lines;
}
// Premultiplied RGBA; over() takes a straight colour.
struct Canvas {
    int width{},height{};std::vector<float> rgba;
    Canvas(int w,int h):width(w),height(h),rgba(std::size_t(w)*h*4){}
    void over(int x,int y,const float* c) {
        if(x<0 || y<0 || x>=width || y>=height)return;
        float* d=&rgba[(std::size_t(y)*width+x)*4];const float a=c[3],keep=1-a;
        for(int k=0;k<3;++k)d[k]=c[k]*a+d[k]*keep;
        d[3]=a+d[3]*keep;
    }
};
}
int main(int argc,char** argv) {
    try {
        if(argc<4){std::cerr<<"Usage: spyro_retail_text_plate <retail data> <out.tga> [options] <text>\n";return 2;}
        Options o;std::string text;
        for(int i=3;i<argc;++i) {
            const std::string a=argv[i];
            const auto next=[&]{if(i+1>=argc)throw std::runtime_error("Missing value after "+a);return std::string(argv[++i]);};
            if(a=="--scale")o.scale=std::stof(next());else if(a=="--aspect")o.aspect=std::stof(next());
            else if(a=="--color")o.color=std::stoul(next());else if(a=="--wrap")o.wrap=std::stof(next());
            else if(a=="--gap")o.gap=std::stof(next());else if(a=="--outline")o.outline=std::stoi(next());
            else if(a=="--slot")o.slot=std::stoul(next());else if(a=="--kern-decimal")o.kern_decimal=std::stof(next());else if(a=="--center")o.center=true;
            else if(a=="--shadow"){o.shadow_x=std::stoi(next());o.shadow_y=std::stoi(next());o.shadow=true;}
            else text=a;
        }
        if(text.empty() || o.color>13 || !(o.scale>0) || !(o.aspect>0))throw std::runtime_error("Invalid text plate options");
        AssetStore assets(argv[1]);WadArchive wad(assets);
        const auto ui=decode_interface(wad.read(o.slot),assets.read_all("SCUS_944.25"),wad.read(4));
        const auto lines=wrap_lines(ui,text,o);
        // Glyph rectangles in UI units: line layouts stacked 12 + gap apart.
        struct Placed {const HudSprite* sprite;float x,y;};std::vector<Placed> glyphs;
        float widest=0;std::vector<float> widths;
        for(const auto& line:lines){const auto l=layout_retail_text(ui,line,o.color);widths.push_back(l.width);widest=std::max(widest,l.width);}
        for(std::size_t n=0;n<lines.size();++n) {
            const auto l=layout_retail_text(ui,lines[n],o.color);
            const float left=o.center?(widest-widths[n])/2:0,top=n*(12+o.gap);
            const auto& t=lines[n];float pull=0;
            for(const auto& g:l.glyphs) {
                const auto i=g.source_end-1;      // the glyph's character in the line
                const auto digit=[&](std::size_t k){return k<t.size() && t[k]>='0' && t[k]<='9';};
                if(i>=2 && t[i-1]=='.' && digit(i) && digit(i-2))pull+=o.kern_decimal;
                glyphs.push_back({&ui.font[g.character].colors[g.color],left+g.x-pull,top+g.y});
            }
        }
        if(glyphs.empty())throw std::runtime_error("Nothing to draw");
        float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f;
        for(const auto& g:glyphs){x0=std::min(x0,g.x);y0=std::min(y0,g.y);x1=std::max(x1,g.x+g.sprite->width);y1=std::max(y1,g.y+g.sprite->height);}
        const float sx=o.scale*o.aspect,sy=o.scale;
        const int pad=o.outline+std::max(std::abs(o.shadow_x),std::abs(o.shadow_y))+2;
        Canvas text_layer(int(std::ceil((x1-x0)*sx))+pad*2,int(std::ceil((y1-y0)*sy))+pad*2);
        // Nearest-texel scaling keeps the PS1 pixels crisp.
        for(const auto& g:glyphs) {
            const auto& s=*g.sprite;
            const int ox=int(std::lround((g.x-x0)*sx))+pad,oy=int(std::lround((g.y-y0)*sy))+pad;
            const int w=int(std::lround(s.width*sx)),h=int(std::lround(s.height*sy));
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                const unsigned tx=std::min(s.width-1,unsigned(x/sx)),ty=std::min(s.height-1,unsigned(y/sy));
                const auto* p=&ui.rgba[(std::size_t(s.y+ty)*ui.width+s.x+tx)*4];if(!p[3])continue;
                const float c[4]{p[0]/255.0f,p[1]/255.0f,p[2]/255.0f,p[3]/255.0f};text_layer.over(ox+x,oy+y,c);
            }
        }
        Canvas out(text_layer.width,text_layer.height);
        const auto stamp=[&](int dx,int dy,float alpha) {
            for(int y=0;y<text_layer.height;++y)for(int x=0;x<text_layer.width;++x) {
                const float a=text_layer.rgba[(std::size_t(y)*text_layer.width+x)*4+3];
                if(a>0){const float c[4]{0,0,0,a*alpha};out.over(x+dx,y+dy,c);}
            }
        };
        if(o.shadow)stamp(o.shadow_x,o.shadow_y,.85f);
        for(int r=o.outline;r>0;--r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx)if(dx*dx+dy*dy<=r*r+r)stamp(dx,dy,1);
        for(int y=0;y<text_layer.height;++y)for(int x=0;x<text_layer.width;++x) {
            const float* p=&text_layer.rgba[(std::size_t(y)*text_layer.width+x)*4];if(p[3]<=0)continue;
            const float c[4]{p[0]/p[3],p[1]/p[3],p[2]/p[3],p[3]};out.over(x,y,c);
        }
        // Uncompressed 32-bit TGA, top-left origin, straight alpha.
        std::ofstream file(argv[2],std::ios::binary);
        const unsigned char header[18]{0,0,2,0,0,0,0,0,0,0,0,0,static_cast<unsigned char>(out.width&255),static_cast<unsigned char>(out.width>>8),
            static_cast<unsigned char>(out.height&255),static_cast<unsigned char>(out.height>>8),32,0x28};
        file.write(reinterpret_cast<const char*>(header),18);
        for(std::size_t i=0;i<out.rgba.size();i+=4) {
            const float a=out.rgba[i+3];const auto channel=[&](float v){return static_cast<char>(std::lround(std::clamp(a>0?v/a:0.0f,0.0f,1.0f)*255));};
            const char bgra[4]{channel(out.rgba[i+2]),channel(out.rgba[i+1]),channel(out.rgba[i]),static_cast<char>(std::lround(std::clamp(a,0.0f,1.0f)*255))};
            file.write(bgra,4);
        }
        if(!file)throw std::runtime_error("Could not write the text plate");
        std::cout<<lines.size()<<" lines, "<<out.width<<'x'<<out.height<<'\n';
        for(const auto& l:lines)std::cout<<"  "<<l<<'\n';
        return 0;
    } catch(const std::exception& e){std::cerr<<"spyro_retail_text_plate: "<<e.what()<<'\n';return 1;}
}
