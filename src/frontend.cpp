#include "spyro/frontend.hpp"
#include "spyro/assets.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace spyro {
namespace {
std::string encode(const std::filesystem::path& path) {
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::filesystem::path decode(const std::string& value) {
    if(value.size()>32768 || value.find('\0')!=std::string::npos)throw FormatError("Invalid menu preference path");
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));
}
}
void FrontendPreferences::remember_save(const std::filesystem::path& path) {
    if(path.empty())return;
    const auto absolute=std::filesystem::absolute(path).lexically_normal();
    std::erase(saves,absolute);saves.insert(saves.begin(),absolute);
    if(saves.size()>8)saves.resize(8);
}
void FrontendPreferences::write(const std::filesystem::path& path) const {
    if(saves.size()>8)throw FormatError("Too many recent saves in menu preferences");
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    auto temporary=path;temporary+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        {std::ofstream out(temporary,std::ios::binary);out<<"S2MENU 5\n"<<std::quoted(encode(source))<<'\n'<<saves.size()<<'\n';
            for(const auto& save:saves)out<<std::quoted(encode(save))<<'\n';
            out<<std::quoted(encode(july_roll_source))<<'\n';
            out<<std::quoted(encode(retail_powerup_source))<<'\n';
            out<<(show_fps?1:0)<<'\n';
            out<<std::min(camera_recentre_seconds,10u)<<' '<<(ps1_fov?1:0)<<' '<<(ps1_hud?1:0)<<'\n';
            out.flush();if(!out)throw FormatError("Could not write menu preferences");}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw FormatError("Could not replace menu preferences");
#else
        std::filesystem::rename(temporary,path);
#endif
    }catch(...){std::error_code error;std::filesystem::remove(temporary,error);throw;}
}
FrontendPreferences FrontendPreferences::read(const std::filesystem::path& path) {
    if(!std::filesystem::exists(path))return {};
    if(std::filesystem::file_size(path)>300000)throw FormatError("Menu preferences are too large");
    std::ifstream in(path,std::ios::binary);std::string magic,value;unsigned version{},count{};
    in>>magic>>version>>std::quoted(value)>>count;
    if(!in || magic!="S2MENU" || version<1 || version>5 || count>8)throw FormatError("Damaged menu preferences");
    FrontendPreferences result;result.source=decode(value);
    for(unsigned i=0;i<count;++i) {
        if(!(in>>std::quoted(value)) || value.empty())throw FormatError("Damaged recent save path");
        const auto save=decode(value);
        if(std::find(result.saves.begin(),result.saves.end(),save)==result.saves.end())result.saves.push_back(save);
    }
    if(version>=2){if(!(in>>std::quoted(value)))throw FormatError("Damaged roll source path");result.july_roll_source=decode(value);}
    if(version>=3){if(!(in>>std::quoted(value)))throw FormatError("Damaged retail power source path");result.retail_powerup_source=decode(value);}
    if(version>=4){unsigned fps=2;if(!(in>>fps) || fps>1)throw FormatError("Damaged frame-rate display preference");result.show_fps=fps==1;}
    if(version>=5) {
        unsigned seconds=99,fov=2,hud=2;
        if(!(in>>seconds>>fov>>hud) || seconds>10 || fov>1 || hud>1)throw FormatError("Damaged PC camera / PS1 view preferences");
        result.camera_recentre_seconds=seconds;result.ps1_fov=fov==1;result.ps1_hud=hud==1;
    }
    in>>std::ws;if(!in.eof())throw FormatError("Unexpected menu preference data");
    return result;
}
} // namespace spyro
