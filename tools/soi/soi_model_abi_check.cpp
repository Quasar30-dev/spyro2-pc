// Reuse the retained bounded vertex probe without changing the VM/core library.
#define main retained_export_main
#include "yotd_model_export.cpp"
#undef main

Bytes file_bytes(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw FormatError("Cannot read "+path.string());
    return Bytes(std::istreambuf_iterator<char>(in),{});
}
void put_word(Bytes& b,unsigned at,unsigned value) {
    for(unsigned i=0;i<4;++i)b.at(at+i)=std::uint8_t(value>>(8*i));
}
void need(bool good,const std::string& message) {if(!good)throw FormatError(message);}

void loader_check(std::span<const std::uint8_t> bank,unsigned type,
                  std::span<const std::uint8_t> exe,std::span<const std::uint8_t> loader) {
    OverlayVm vm;
    vm.load(read_u32(exe,0x18),exe.subspan(0x800,read_u32(exe,0x1c)));
    vm.load(0x8006d264,loader);
    constexpr unsigned base=0x80100000;
    vm.load(base,bank);
    const std::array<std::uint32_t,3> args{base,type,base+unsigned(bank.size())};
    vm.call(0x80070018,args,100000);
    need(vm.fault.empty(),"Original loader: "+vm.fault);
    need(vm.read32(0x80068c94+type*4)==base,"Class table installation");
    for(unsigned at=20;at<52;at+=4)
        need(vm.read32(base+at)==(read_u32(bank,at)?base+read_u32(bank,at):0),"Collision pointer relocation");
    const auto common=base+read_u32(bank,56);
    need(vm.read32(base+52)==base+read_u32(bank,52),"Face patches pointer relocation");
    need(vm.read32(base+56)==common,"Common pointer relocation");
    for(unsigned c=0;c<read_u32(bank,0);++c) {
        const auto d=read_u32(bank,60+c*4);
        need(vm.read32(base+60+c*4)==base+d,"Descriptor pointer relocation");
        for(unsigned at:{12u,16u,20u,24u,28u,32u}) {
            const auto raw=read_u32(bank,d+at);
            const auto expected=at==24 && !raw?0:common+(at==28 && !raw?read_u32(bank,d+20):raw);
            need(vm.read32(base+d+at)==expected,"Descriptor common relocation +"+std::to_string(at));
        }
    }
    std::cout << "ORIGINAL S2 MODEL LOADER EQUALITY class " << std::hex << type << std::dec
              << " clips " << read_u32(bank,0) << " instructions " << vm.executed
              << "; complete original 80070018 function, original loader1 slot1\n";
}

int main(int argc,char** argv) {
    try {
        need(argc==4,"usage: soi_model_abi_check SOURCE_BANK CONVERTED_BANK TARGET_CLASS_HEX");
        const auto original=file_bytes(argv[1]),converted=file_bytes(argv[2]);
        const unsigned type=unsigned(std::stoul(argv[3],nullptr,16));
        AssetStore assets("data/disc");const auto exe=assets.read_all(find_game_executable(assets));
        WadArchive wad(assets);const auto loader=wad.read(1);
        need(type<0x300 && read_u32(converted,0)>0,"Animated S2 class range");
        loader_check(converted,type,exe,loader);
        std::array<std::uint32_t,125> codebook{};
        for(unsigned i=0;i<125;++i)codebook[i]=read_u32(exe,0x80062674-0x80010000+0x800+4*i);
        for(unsigned lod=0;lod<2;++lod) {
            Bytes source=original,target=converted;
            if(lod)for(auto* b:{&source,&target})for(unsigned c=0;c<read_u32(*b,0);++c) {
                const auto d=read_u32(*b,60+c*4);need((*b)[d+5]>0,"Far vertex count");
                (*b)[d+4]=(*b)[d+5];
                put_word(*b,d+16,read_u32(*b,d+24));
                const auto palette=read_u32(*b,d+28);
                if(palette)put_word(*b,d+20,palette);
            }
            ObjectModel a,b;a.animated=b.animated=true;
            a.animation_count=read_u32(source,0);b.animation_count=read_u32(target,0);
            decode_object_animations(a,source,codebook);decode_object_animations(b,target,codebook);
            need(a.meshes.size()==b.meshes.size() && a.animations.size()==b.animations.size(),"Clip/mesh shape");
            std::uint64_t frames=0,faces=0;
            for(unsigned c=0;c<a.animations.size();++c) {
                need(a.animations[c].frames.size()==b.animations[c].frames.size(),"Frame count");
                for(unsigned f=0;f<a.animations[c].frames.size();++f) {
                    const auto& x=a.animations[c].frames[f];const auto& y=b.animations[c].frames[f];
                    need(x.control==y.control && x.metadata==y.metadata && x.ticks==y.ticks,"Frame controls/timing");
                    need(x.vertices.size()==y.vertices.size(),"Vertex count");
                    for(unsigned v=0;v<x.vertices.size();++v)
                        need(x.vertices[v].x==y.vertices[v].x && x.vertices[v].y==y.vertices[v].y && x.vertices[v].z==y.vertices[v].z,"Converted vertex");
                    ++frames;
                }
            }
            for(unsigned m=0;m<a.meshes.size();++m) {
                need(a.meshes[m].faces.size()==b.meshes[m].faces.size(),"Face count");
                for(unsigned f=0;f<a.meshes[m].faces.size();++f) {
                    const auto& x=a.meshes[m].faces[f];const auto& y=b.meshes[m].faces[f];
                    need(x.vertices==y.vertices && x.flags==y.flags && x.quad==y.quad && x.textured==y.textured,"Face topology/flags");
                    for(unsigned k=0;k<(x.quad?4u:3u);++k)
                        need(x.colors[k].x==y.colors[k].x && x.colors[k].y==y.colors[k].y && x.colors[k].z==y.colors[k].z,"Corner palette");
                    ++faces;
                }
            }
            std::cout << "LOD " << lod << " EXACT CONVERTED FRAMES " << frames << " face packets " << faces << '\n';
            verify_vertices(b,target,exe);
        }
        std::cout << "LIMIT bounded original loader/vertex evidence; GPU face emission, audio playback, class AI and story are separate gates\n";
        return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
