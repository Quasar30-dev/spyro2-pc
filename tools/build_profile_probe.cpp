#include "spyro/game_build.hpp"
#include "spyro/runtime.hpp"
#include "spyro/interface.hpp"
#include "spyro/travel.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
using namespace spyro;
int main(int argc,char** argv)try {
    if(argc<3)throw std::runtime_error("source output.tsv [logical-slot]");
    AssetStore assets(std::filesystem::u8path(argv[1]));const auto exe=assets.read_all(find_game_executable(assets));
    const auto& profile=game_profile(exe);WadArchive wad(assets,&profile);const auto global=wad.read(0);
    std::ofstream out(argv[2]);out<<"build\tlogical_slot\tphysical_slot\tcomponent\tresult\n";
    const auto inspect=[&](unsigned slot,const char* component,auto function) {
        try{function();out<<profile.name<<'\t'<<slot<<'\t'<<*profile.physical_slot(slot)<<'\t'<<component<<"\tOK\n";}
        catch(const std::exception& e){out<<profile.name<<'\t'<<slot<<'\t'<<*profile.physical_slot(slot)<<'\t'<<component<<'\t'<<e.what()<<'\n';}
        out.flush();
    };
    for(unsigned slot:profile.levels()) {
        if(argc>3 && slot!=std::stoul(argv[3]))continue;
        const auto a=wad.read(slot);std::optional<Level> level;
        inspect(slot,"level",[&]{level=decode_level(a,slot);});
        inspect(slot,"spyro",[&]{const auto m=decode_spyro(global,a,exe);for(const auto& [id,c]:m.animations)for(unsigned f=0;f<c.frames.size();++f)(void)m.frame(id,float(f));});
        inspect(slot,"camera",[&]{(void)decode_camera_settings(exe);});
        inspect(slot,"objects",[&]{(void)decode_object_models(a,exe);});
        inspect(slot,"hud",[&]{(void)decode_hud(a,exe);});
        inspect(slot,"interface",[&]{(void)decode_interface(a,exe,wad.contains(4)?wad.read(4):Bytes{});});
        inspect(slot,"power_effects",[&]{(void)decode_powerup_assets(a);});
        inspect(slot,"sound",[&]{(void)decode_sound_bank(a,exe,slot);});
        inspect(slot,"music_directory",[&]{(void)decode_level_music(exe,slot);});
        if(level)inspect(slot,"travel",[&]{decode_travel(a,exe,*level);});
        std::cout<<profile.name<<" / "<<slot<<'\n';
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
