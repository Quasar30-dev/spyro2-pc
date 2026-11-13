// Native decoder checks of the exact candidate's remapped object sound lists.
#include "spyro/audio.hpp"
#include "spyro/game_build.hpp"
#include <fstream>
#include <iostream>
using namespace spyro;
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw FormatError("usage: soi_model_sound_check CANDIDATE_ARCHIVE");
        std::ifstream input(argv[1],std::ios::binary);Bytes archive(std::istreambuf_iterator<char>(input),{});
        AssetStore s2("data/disc"),s3("data-yotd");
        const auto exe2=s2.read_all(find_game_executable(s2)),exe3=s3.read_all(find_game_executable(s3));
        const auto target=decode_sound_bank(archive,exe2,17);
        const auto& profile=game_profile(exe3);WadArchive wad(s3,&profile);
        unsigned references=0;std::uint64_t samples=0;
        for(const std::array<unsigned,3> spec:{std::array<unsigned,3>{97,0x1a9,0x2e0},{141,0x19a,0x2e1},{123,5,0x2e2}}) {
            const auto donor=decode_sound_bank(wad.read(spec[0]),exe3,spec[0]);
            const auto& from=donor.object_sounds.at(spec[1]);const auto& to=target.object_sounds.at(spec[2]);
            if(from.size()!=to.size())throw FormatError("Inline sound list size");
            for(unsigned i=0;i<from.size();++i) {
                if(from[i]==255){if(to[i]!=255)throw FormatError("Unused inline sound");continue;}
                const auto& a=donor.sounds.at(from[i]);const auto& b=target.sounds.at(to[i]);
                if(a.pcm!=b.pcm || a.volume_min!=b.volume_min || a.volume_max!=b.volume_max ||
                   a.near_distance!=b.near_distance || a.far_distance!=b.far_distance ||
                   a.pitch_min!=b.pitch_min || a.pitch_max!=b.pitch_max || a.looping!=b.looping || a.priority!=b.priority)
                    throw FormatError("Native PCM/descriptor mismatch");
                std::cout << "NATIVE SPU PCM EQUALITY class " << std::hex << spec[2] << std::dec << " event " << i
                          << " source " << unsigned(from[i]) << " target " << unsigned(to[i]) << " samples " << a.pcm.samples.size() << '\n';
                ++references;samples+=a.pcm.samples.size();
            }
        }
        std::cout << "NATIVE MODEL SOUND DECODE EQUALITY " << references << " references / " << samples
                  << " signed16 samples; volume/pitch/distance/loop/priority exact; candidate native samples " << target.sounds.size() << '\n';
        std::cout << "LIMIT decoded object SFX only; no SPU hardware playback, listening or story voice claim\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
