// Read-only research audio extraction using the same verified retail decoder.
#include "spyro/audio.hpp"
#include <iostream>
using namespace spyro;
int main(int argc,char** argv) {
    try {
        if(argc<6)throw FormatError("Usage: spyro_cut_content_probe COPY --voice SLOT ID OUTPUT.wav | COPY --xa SECTOR SECTORS CHANNEL OUTPUT.wav");
        const auto source=std::filesystem::u8path(argv[1]);XaClip clip;
        std::filesystem::path output;
        const std::string mode=argv[2];
        if(mode=="--voice" && argc==6) {
            const unsigned slot=std::stoul(argv[3]),id=std::stoul(argv[4]);
            if(slot<15 || slot>93 || !(slot&1))throw FormatError("Voice table needs a gameplay scene slot");
            AssetStore assets(source);WadArchive wad(assets);const auto bytes=wad.read(slot);
            const auto children=parse_wad(std::span(bytes).first(0x800),bytes.size());
            if(children.size()!=4)throw FormatError("Missing gameplay child");
            const auto data=std::span(bytes).subspan(children[3].offset,children[3].size);
            std::size_t at=0x2c;
            for(unsigned i=0;i<7;++i) {
                const auto n=read_u32(data,at);
                if(n<4 || n%4 || n>data.size()-at)throw FormatError("Invalid gameplay section");
                at+=n;
            }
            const auto size=read_u32(data,at);
            if(size<4 || size>data.size()-at || (size-4)%8 || id>=(size-4)/8)throw FormatError("Invalid voice ID/table");
            at+=4+id*8;
            clip={read_u32(data,at+4),read_u16(data,at+2),read_u16(data,at),1};
            output=std::filesystem::u8path(argv[5]);
        }else if(mode=="--xa" && argc==7) {
            clip={static_cast<unsigned>(std::stoul(argv[3])),static_cast<unsigned>(std::stoul(argv[4])),
                  static_cast<unsigned>(std::stoul(argv[5])),1};
            output=std::filesystem::u8path(argv[6]);
        }else throw FormatError("Invalid research probe arguments");
        if(!clip.sectors || clip.sectors>150*60*20 || clip.channel>31)throw FormatError("Invalid/beyond research audio extent");
        const auto pcm=read_xa(source,clip);
        if(pcm.samples.empty())throw FormatError("No matching XA audio sectors");
        std::filesystem::create_directories(output.parent_path());write_wave(output,pcm);
        std::cout<<"{\"sector\":"<<clip.sector<<",\"sectors\":"<<clip.sectors<<",\"channel\":"<<clip.channel
            <<",\"rate\":"<<pcm.rate<<",\"channels\":"<<pcm.channels<<",\"frames\":"<<pcm.frames()<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
