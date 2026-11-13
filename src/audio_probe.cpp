#include "spyro/audio.hpp"
#include "spyro/game_build.hpp"
#include "spyro/npc.hpp"
#include <iostream>
#include <set>

using namespace spyro;
int main(int argc,char** argv) {
    try {
        if(argc<2)throw FormatError("Usage: spyro_audio_probe retail-copy [--all] [--slot N] [--seconds N] [--wave PATH]");
        std::filesystem::path source=std::filesystem::u8path(argv[1]),wave;unsigned slot=15,seconds=0;bool all=false,npcs=false;
        unsigned npc_record=UINT32_MAX,dialogue=0,sample=UINT32_MAX,global_id=UINT32_MAX,object_type=UINT32_MAX;
        for(int i=2;i<argc;++i) {
            const std::string flag=argv[i];
            if(flag=="--all")all=true;
            else if(flag=="--npcs")npcs=true;
            else if(i+1>=argc)throw FormatError("Missing audio probe argument");
            else if(flag=="--slot")slot=static_cast<unsigned>(std::stoul(argv[++i]));
            else if(flag=="--seconds")seconds=static_cast<unsigned>(std::stoul(argv[++i]));
            else if(flag=="--npc-record"){npc_record=static_cast<unsigned>(std::stoul(argv[++i]));npcs=true;}
            else if(flag=="--dialogue")dialogue=static_cast<unsigned>(std::stoul(argv[++i]));
            else if(flag=="--sample")sample=static_cast<unsigned>(std::stoul(argv[++i]));
            else if(flag=="--global-id")global_id=static_cast<unsigned>(std::stoul(argv[++i],nullptr,0));
            else if(flag=="--object-sounds")object_type=static_cast<unsigned>(std::stoul(argv[++i],nullptr,0));
            else if(flag=="--wave")wave=std::filesystem::u8path(argv[++i]);
            else throw FormatError("Unknown audio probe argument");
        }
        AssetStore assets(source);const auto exe=assets.read_all(find_game_executable(assets));
        const auto& profile=game_profile(exe);WadArchive wad(assets,&profile);
        if(profile.demo() && slot==15)slot=profile.first_level();
        std::set<unsigned> tracks;unsigned banks=0,sounds=0,npc_count=0,speech_count=0;
        for(unsigned current:all?profile.levels():std::vector<unsigned>{slot}) {
            const auto archive=wad.read(current);
            if(npcs) {
                const auto definitions=decode_npcs(archive,current);
                for(const auto& npc:definitions) {
                    if(npc_record!=UINT32_MAX && npc.object!=npc_record)continue;
                    ++npc_count;
                    for(unsigned n=0;n<npc.lines.size();++n) {
                        if(npc_record!=UINT32_MAX && n!=dialogue)continue;
                        const auto& line=npc.lines[n];if(!line.speech)continue;auto clip=*line.speech;
                        if(seconds)clip.sectors=std::min(clip.sectors,seconds*150);
                        const auto pcm=read_xa(source,clip);
                        ++speech_count;
                        if(!wave.empty())write_wave(wave,pcm);
                        std::cout<<"{\"slot\":"<<current<<",\"npc\":"<<npc.object<<",\"line\":"<<n<<",\"frames\":"<<pcm.frames()
                            <<",\"rate\":"<<pcm.rate<<",\"channels\":"<<pcm.channels<<",\"supported\":"<<line.supported<<"}\n";
                    }
                }
                continue;
            }
            const auto bank=decode_sound_bank(archive,exe,current);++banks;sounds+=static_cast<unsigned>(bank.sounds.size());
            if(global_id!=UINT32_MAX) {
                if(global_id>=bank.globals.size())throw FormatError("Global event outside sound mapping");
                sample=bank.globals[global_id];
            }
            if(object_type!=UINT32_MAX) {
                // Class sound slots (model bank +4) and their samples' flags.
                const auto slots=bank.object_sounds.find(object_type);
                std::cout<<"{\"slot\":"<<current<<",\"class\":"<<object_type<<",\"sounds\":[";
                if(slots!=bank.object_sounds.end())for(unsigned n=0;n<slots->second.size();++n) {
                    const unsigned id=slots->second[n];std::cout<<(n?",":"")<<"{\"sample\":"<<id;
                    if(id<bank.sounds.size()){const auto& s=bank.sounds[id];std::cout<<",\"looping\":"<<s.looping<<",\"frames\":"<<s.pcm.frames()
                        <<",\"volume\":["<<s.volume_min<<","<<s.volume_max<<"],\"distance\":["<<s.near_distance<<","<<s.far_distance<<"]";}
                    std::cout<<"}";
                }
                std::cout<<"]}\n";continue;
            }
            if(sample!=UINT32_MAX) {
                if(sample>=bank.sounds.size())throw FormatError("Sample outside sound bank");
                if(!wave.empty())write_wave(wave,bank.sounds[sample].pcm);
                std::cout<<"{\"sample\":"<<sample<<",\"pitch\":"<<bank.sounds[sample].pitch_min<<"}\n";continue;
            }
            const auto music=decode_level_music(exe,current);
            std::cout<<"{\"slot\":"<<current<<",\"sounds\":"<<bank.sounds.size()<<",\"track\":"<<music.track
                <<",\"asset\":\""<<music.clip.asset<<"\",\"sector\":"<<music.clip.sector<<",\"channel\":"<<music.clip.channel;
            if(tracks.insert(music.track).second || !all) {
                auto clip=music.clip;if(seconds)clip.sectors=std::min(clip.sectors,seconds*150);
                const auto pcm=read_xa(source,clip);std::int64_t sum=0;unsigned peak=0;
                for(const auto s:pcm.samples){sum+=std::int64_t(s)*s;peak=std::max(peak,static_cast<unsigned>(std::abs(static_cast<int>(s))));}
                const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(pcm.samples.data()),pcm.samples.size()*2);
                std::cout<<",\"frames\":"<<pcm.frames()<<",\"rate\":"<<pcm.rate<<",\"channels\":"<<pcm.channels
                    <<",\"peak\":"<<peak<<",\"rms\":"<<std::sqrt(static_cast<double>(sum)/pcm.samples.size())<<",\"pcm_crc\":"<<crc32(bytes);
                if(!wave.empty() && current==slot)write_wave(wave,pcm);
            }
            std::cout<<"}\n";
        }
        if(npcs)std::cout<<"{\"npcs\":"<<npc_count<<",\"speech_lines\":"<<speech_count<<"}\n";
        else std::cout<<"{\"banks\":"<<banks<<",\"sounds\":"<<sounds<<",\"music_tracks\":"<<tracks.size()<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
