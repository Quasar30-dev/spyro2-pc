#include "spyro/audio_output.hpp"
#include "spyro/runtime.hpp"
#include "spyro/game_build.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// Opt-in integration check: needs the user's retail files and a Windows audio
// device. Muting the master preserves real voice consumption for verification.
using namespace spyro;
int main(int argc,char** argv) {
    try {
        const bool roll=argc==4 && std::string(argv[2])=="--roll-effects";
        if(argc<2 || (!roll && (argc>3 || (argc==3 && std::string(argv[2])!="--spyro-effects"))))
            throw FormatError("Usage: spyro_audio_playback_probe game-copy [--spyro-effects | --roll-effects July-copy]");
        const bool effects=argc>=3;
        const auto source=std::filesystem::u8path(argv[1]);AssetStore assets(source);
        const auto exe=assets.read_all(find_game_executable(assets));const auto& profile=game_profile(exe);
        WadArchive wad(assets,&profile);const auto available=profile.levels();
        const auto slot=roll && std::find(available.begin(),available.end(),17u)!=available.end()?17u:profile.first_level();
        const auto archive=wad.read(slot);
        AudioOutput output;output.muted=true;output.load(source,exe,archive,slot);
        if(roll) {
            AssetStore donor(std::filesystem::u8path(argv[3]));const auto donor_exe=donor.read_all(find_game_executable(donor));
            if(game_profile(donor_exe).build!=GameBuild::July1999)throw FormatError("Roll probe requires the original July donor");
            WadArchive donor_wad(donor,&game_profile(donor_exe));
            output.set_july_roll_sound(decode_sound_bank(donor_wad.read(17),donor_exe,17));
            // Reload verifies that changing level keeps the imported cue.
            output.load(source,exe,archive,slot);
        }
        const auto level=decode_level(archive,slot);Scene scene;scene.retail_crc=profile.crc;scene.wad_slot=slot;Game game(level,scene);
        using Clock=std::chrono::steady_clock;unsigned checks=0;
        const auto check=[&](bool success,const char* reason){++checks;if(!success)throw FormatError(std::string(reason)+" ("+output.status+")");};
        const auto pump=[&](unsigned milliseconds,bool playing){
            const auto end=Clock::now()+std::chrono::milliseconds(milliseconds);
            do {output.update(&game,playing);std::this_thread::sleep_for(std::chrono::milliseconds(8));}while(Clock::now()<end);
            output.update(&game,playing);
        };
        const auto await_audio=[&](bool voice){
            const auto end=Clock::now()+std::chrono::seconds(5);
            while((voice?output.voice_samples_played:output.music_samples_played)==0 && Clock::now()<end)pump(20,true);
            check((voice?output.voice_samples_played:output.music_samples_played)>0,"Windows did not consume queued audio");
        };
        await_audio(false);
        if(effects) {
            const auto bank=decode_sound_bank(archive,exe,slot);
            game.level.whirlwinds.clear(); // Isolate each player cue from ambient loops.
            output.music_volume=output.voice_volume=0;output.effects_volume=1;
            output.begin_capture(true);output.muted=false; // Offline mode mutes the hardware, not the capture mix.
            struct Cue {const char* name;SoundEvent event;Motion loop=Motion::Idle;};
            std::vector<Cue> cues{{"jump",{SoundScope::Global,0x22}},{"flame",{SoundScope::Global,3}},
                {"headbash",{SoundScope::Global,0x14}},{"footsteps",{SoundScope::Surface,0}},
                {"charge",{},Motion::Charge}};
            if(roll)cues={Cue{"July roll",{SoundScope::Global,july_roll_sound_id}}};
            for(auto id:{8u,0x18u})if(!roll && bank.globals[id]!=255)cues.push_back({id==8?"splash":"underwater hurt",{SoundScope::Global,id}});
            if(!roll && bank.globals[6]!=255)cues.push_back({"swim",{},Motion::UnderwaterSwim});
            for(const auto& cue:cues) {
                output.begin_capture(true);const auto before=output.effects_played;
                game.player.motion=cue.loop;
                if(cue.loop==Motion::Idle)game.sound_events.push_back(cue.event);
                pump(25,true);
                check(output.active_voices>0,"Spyro cue did not reach an XAudio2 voice");
                if(cue.loop==Motion::Idle)check(output.effects_played==before+1,"Spyro one-shot was rejected");
                std::uint64_t energy=0;unsigned peak=0;
                for(unsigned frame=0;frame<30;++frame) {
                    output.update(&game,true);
                    for(int value:output.capture_frame()){energy+=std::int64_t(value)*value;peak=std::max(peak,unsigned(std::abs(value)));}
                }
                check(peak>100 && std::sqrt(double(energy)/(30*1600))>10,"Spyro cue was silent in the 60 fps capture mixer");
                if(roll) {
                    check(output.roll_effects_played==1,"July roll did not reach the Windows playback path");
                    check(std::sqrt(double(energy)/(30*1600))>900,"July roll is too quiet in the PC effects mix");
                }
                std::cout<<profile.name<<" "<<cue.name<<": peak "<<peak<<", RMS "<<std::sqrt(double(energy)/(30*1600))<<'\n';
                game.player.motion=Motion::Idle;
                const auto end=Clock::now()+std::chrono::seconds(3);
                do {pump(20,true);}while(output.active_voices && Clock::now()<end);
                check(!output.active_voices,"Spyro cue did not end/loop did not stop");
                for(unsigned n=0;n<180;++n){output.update(&game,true);output.capture_frame();}
                const auto silence=output.capture_frame();
                check(std::all_of(silence.begin(),silence.end(),[](auto v){return v==0;}),"Stopped cue leaked into capture");
            }
            output.muted=true;output.end_capture();
            std::cout<<checks<<" Windows playback and 60 fps sound-capture checks passed\n";return 0;
        }
        check(profile.build==GameBuild::RetailUS,"Use --spyro-effects for prototype sources");
        check(output.track==0 && output.sample_count==70,"Summer Forest audio bank was not selected");
        game.sound_events.push_back({SoundScope::Global,0x22});pump(50,true);
        check(output.effects_played==1,"Jump sound event was not submitted");pump(800,true);
        game.player.motion=Motion::Charge;pump(100,true);
        check(output.active_voices>=1,"Charge sound loop did not start");
        game.player.motion=Motion::Idle;pump(100,true);
        check(output.active_voices==0,"Charge loop survived leaving charge");
        const auto npc=std::find_if(level.npcs.begin(),level.npcs.end(),[](const auto& n){return n.object==200;});
        check(npc!=level.npcs.end() && npc->lines[0].speech.has_value(),"Hunter's greeting has no retail speech entry");
        output.speech(npc->lines[0].speech);await_audio(true);
        pump(80,false);const auto music_paused=output.music_samples_played,voice_paused=output.voice_samples_played;
        pump(120,false);
        check(output.music_samples_played==music_paused && output.voice_samples_played==voice_paused,"Paused audio advanced");
        pump(120,true);
        check(output.music_samples_played>music_paused && output.voice_samples_played>voice_paused,"Resumed audio did not advance");
        output.speech({});pump(20,true);
        check(output.voice_frames==0 && output.voice_samples_played==0,"Leaving dialogue retained speech");
        output.speech(npc->lines[1].speech);await_audio(true);
        output.load(source,exe,wad.read(17),17);await_audio(false);
        check(output.track==1 && output.sample_count==75 && output.voice_frames==0,"Changing level retained the old music bank or voice");
        std::cout<<"{\"checks\":"<<checks<<",\"music_track\":"<<output.track
            <<",\"music_samples_played\":"<<output.music_samples_played<<",\"paused_music_sample\":"<<music_paused
            <<",\"paused_voice_sample\":"<<voice_paused<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
