#include "spyro/audio.hpp"
#include "spyro/game_build.hpp"
#include <iostream>

using namespace spyro;
namespace {
unsigned checks=0;
void check(bool value,const std::string& why){++checks;if(!value)throw FormatError(why);}
const SoundSample& audible(const SoundBank& bank,unsigned sample,bool looping,const std::string& context) {
    check(sample<bank.sounds.size(),context+": sound event has no sample");
    const auto& s=bank.sounds[sample];
    check(s.looping==looping,context+": one-shot/loop mismatch");
    check(s.volume_max && s.pitch_min && s.pitch_max,context+": silent descriptor");
    double energy=0;for(int v:s.pcm.samples)energy+=double(v)*v;
    check(!s.pcm.samples.empty() && std::sqrt(energy/s.pcm.samples.size())*s.volume_max/32768.0>10,
          context+": decoded waveform is effectively silent");
    return s;
}
}
int main(int argc,char** argv)try {
    const auto root=std::filesystem::path(argc>1?argv[1]:".");
    if(!std::filesystem::exists(root/"data/disc/SCUS_944.25") ||
       !std::filesystem::exists(root/"data/builds/august-1999/disc/SPYRO2.EXE"))return 77;
    AssetStore retail(root/"data/disc");WadArchive retail_wad(retail);
    const auto exe=retail.read_all(find_game_executable(retail));
    const auto reference=decode_sound_bank(retail_wad.read(15),exe,15);
    AssetStore july(root/"data/builds/july-1999/disc");
    const auto july_exe=july.read_all(find_game_executable(july));
    WadArchive july_wad(july,&game_profile(july_exe));
    const auto roll_donor=decode_sound_bank(july_wad.read(17),july_exe,17);
    const auto& roll_sample=audible(roll_donor,roll_donor.globals[july_roll_sound_id],false,"July native roll");
    check(roll_sample.pcm!=roll_donor.sounds[roll_donor.globals[0x14]].pcm,"Cut roll was replaced by the retail headbash mapping");
    unsigned banks=0;
    for(const auto folder:{"data/disc","data/builds/july-1999/disc","data/builds/august-1999/disc","data/builds/winter-jampack-1999/disc"}) {
        AssetStore assets(root/folder);const auto executable=assets.read_all(find_game_executable(assets));
        const auto& profile=game_profile(executable);WadArchive wad(assets,&profile);
        unsigned scenes=0;
        for(auto slot:profile.levels()) {
            if(slot>71)continue;
            const auto bank=decode_sound_bank(wad.read(slot),executable,slot);
            const auto context=std::string(profile.name)+" slot "+std::to_string(slot);
            auto imported=bank;const auto headbash=bank.sounds[bank.globals[0x14]];
            check(import_july_roll_sound(imported,roll_donor),context+": could not import July roll audio");
            check(imported.sounds[imported.globals[july_roll_sound_id]]==roll_sample,context+": roll PCM/descriptor changed");
            check(imported.sounds[imported.globals[0x14]]==headbash,context+": roll import replaced headbash audio");
            const auto count=imported.sounds.size();import_july_roll_sound(imported,roll_donor);
            check(imported.sounds.size()==count,context+": repeated roll import duplicated samples");
            for(unsigned id:{3u,0x14u,0x22u}) {
                const auto& s=audible(bank,bank.globals[id],false,context+" event "+std::to_string(id));
                check(s.pcm.samples==reference.sounds[reference.globals[id]].pcm.samples,context+": wrong flame/headbash/jump waveform");
            }
            const auto& charge=audible(bank,bank.globals[0x21],true,context+" charge");
            if(profile.build==GameBuild::RetailUS || profile.build==GameBuild::August1999)
                check(charge.pcm.samples==reference.sounds[reference.globals[0x21]].pcm.samples,context+": wrong charge waveform");
            if(profile.build==GameBuild::July1999 && slot==57) {
                // This unfinished July scene explicitly has no footstep
                // categories or surface samples. Do not invent retail data.
                check(std::all_of(bank.footsteps.begin(),bank.footsteps.end(),[](auto n){return n==255;}) &&
                      std::all_of(bank.surfaces.begin(),bank.surfaces.end(),[](auto n){return n==255;}),context+": native silent-footstep sentinel changed");
            } else {
                check(bank.footsteps[0]<bank.surfaces.size(),context+": invalid footstep category");
                audible(bank,bank.surfaces[bank.footsteps[0]],false,context+" footsteps");
            }
            for(unsigned id:{6u,0x1cu,0x1fu})if(bank.globals[id]!=255)audible(bank,bank.globals[id],true,context+" loop "+std::to_string(id));
            for(unsigned id:{8u,0x18u})if(bank.globals[id]!=255) {
                const auto& s=audible(bank,bank.globals[id],false,context+" water event "+std::to_string(id));
                check(s.pcm.samples==reference.sounds[reference.globals[id]].pcm.samples,context+": wrong splash/underwater hurt waveform");
            }
            const auto gem=bank.object_sounds.find(1);
            if(gem!=bank.object_sounds.end() && !gem->second.empty() && gem->second[0]!=255)
                audible(bank,gem->second[0],false,context+" gem");
            ++banks;++scenes;
        }
        std::cout<<profile.name<<": "<<scenes<<" level sound banks verified\n";
    }
    std::cout<<checks<<" checks across "<<banks<<" banks: sound event types, audible PCM, original waveforms and object sounds OK\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
