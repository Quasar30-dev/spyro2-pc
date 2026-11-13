#include "spyro/frontend.hpp"
#include "spyro/title.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

using namespace spyro;
int main(int argc,char** argv) {
    unsigned checks=0;
    const auto check=[&](bool value,const char* why){++checks;if(!value)throw FormatError(why);};
    const auto temp=std::filesystem::temp_directory_path()/std::filesystem::path("spyro-menu-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto path=temp/"frontend.cfg";
        check(FrontendPreferences::read(path).saves.empty(),"First launch is not empty");
        FrontendPreferences p;p.source=std::filesystem::path(u8"C:/Games/Spyrö 2/retail copy.cue");
        p.july_roll_source=std::filesystem::path(u8"C:/Games/Spyrö 2/July copy.cue");
        p.retail_powerup_source=std::filesystem::path(u8"C:/Games/Spyrö 2/Retail copy.cue");p.show_fps=true;
        for(unsigned i=0;i<12;++i)p.remember_save(temp/(std::to_string(i)+".s2save"));
        p.remember_save(temp/"8.s2save");p.write(path);auto read=FrontendPreferences::read(path);
        check(read.source==p.source && read.saves==p.saves && read.july_roll_source==p.july_roll_source && read.retail_powerup_source==p.retail_powerup_source && read.show_fps,"Unicode/space path preferences did not round-trip");
        check(read.saves.size()==8 && read.saves.front().filename()=="8.s2save","Recent saves lost their bounded MRU order");
        p.remember_save(temp/"saved games"/std::filesystem::path(u8"Élodie.s2save"));p.write(path);
        check(FrontendPreferences::read(path).saves==p.saves,"Replacing menu preferences lost recent saves");
        {std::ofstream out(path);out<<"S2MENU 1\n\"C:/old copy\"\n0\n";}
        check(FrontendPreferences::read(path).july_roll_source.empty(),"Older preferences invented a roll donor source");
        {std::ofstream out(path);out<<"S2MENU 2\n\"C:/old copy\"\n0\n\"C:/July\"\n";}
        check(FrontendPreferences::read(path).retail_powerup_source.empty(),"Older preferences invented a retail power donor source");
        {std::ofstream out(path);out<<"S2MENU 1\n\"\"\n999999999\n";}
        bool rejected=false;try{FrontendPreferences::read(path);}catch(const FormatError&){rejected=true;}
        check(rejected,"Unbounded menu data accepted");
        if(argc>1) {
            AssetStore store(std::filesystem::u8path(argv[1]));WadArchive wad(store);
            const auto title=decode_title(wad.read(95),wad.read(96),store.read_all("SCUS_944.25",4*1024*1024));
            check(title.models.models.size()==9 && title.chunks.size()==14,"Missing title actors/segments");
            for(unsigned tick=0;tick<3240;++tick) {
                const auto frame=title.sample(float(tick)+0.5f);
                check(finite(frame.eye) && finite(frame.forward) && std::abs(length(frame.forward)-1)<0.001f,"Invalid cinematic camera");
                for(unsigned i=0;i<frame.actors.size();++i) {
                    check(finite(frame.actors[i].position),"Invalid title actor translation");
                    for(const auto vertex:frame.poses[i].vertices)check(finite(vertex),"Invalid title actor pose");
                }
            }
            const auto a=title.sample(1032),b=title.sample(1620);
            check(length(a.eye-b.eye)<0.0001f && length(a.actors[0].position-b.actors[0].position)<0.0001f,"Title loop does not wrap to the retail frame");
            std::cout<<"Retail title: 9 actors, 14 segments, logo, panels and 71 sprite entries\n";
        }
        std::filesystem::remove(temp/"frontend.cfg");std::filesystem::remove(temp);std::cout<<checks<<" frontend checks passed\n";return 0;
    }catch(const std::exception& error){std::error_code ignored;std::filesystem::remove(temp/"frontend.cfg",ignored);std::filesystem::remove(temp,ignored);std::cerr<<error.what()<<'\n';return 1;}
}
