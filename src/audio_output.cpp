#include "spyro/audio_output.hpp"
#include "spyro/powerup_import.hpp"
#include "spyro/runtime.hpp"
#include "spyro/game_build.hpp"
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <mutex>
#include <thread>

namespace spyro {
namespace {
void checked(HRESULT result,const char* message){if(FAILED(result))throw FormatError(std::string(message)+": "+std::to_string(result));}
void destroy(IXAudio2SourceVoice*& voice){if(voice){voice->DestroyVoice();voice=nullptr;}}
// July's low-pitched roll cue is lost beneath the PC music mix at its original
// descriptor level. Preserve its PCM and SPU pitch; apply the same cue gain to
// Windows playback and offline captures.
float event_gain(const SoundEvent& event) {
    return event.scope==SoundScope::Global && event.id==july_roll_sound_id?4.0f:1.0f;
}
struct Job {
    std::jthread worker;std::mutex mutex;bool done=false;std::unique_ptr<PcmAudio> pcm;std::string error;
    void stop(){worker.request_stop();if(worker.joinable())worker.join();}
    ~Job(){stop();}
    void start(std::filesystem::path path,XaClip clip) {
        stop();{std::lock_guard lock(mutex);done=false;pcm.reset();error.clear();}
        worker=std::jthread([this,path=std::move(path),clip](std::stop_token token){
            std::unique_ptr<PcmAudio> result;std::string failure;
            try{result=std::make_unique<PcmAudio>(read_xa(path,clip,[&]{return token.stop_requested();}));}
            catch(const std::exception& e){failure=e.what();}
            if(token.stop_requested())return;
            std::lock_guard lock(mutex);pcm=std::move(result);error=std::move(failure);done=true;
        });
    }
};
}
struct AudioOutput::Impl {
    Microsoft::WRL::ComPtr<IXAudio2> engine;IXAudio2MasteringVoice* master{};
    IXAudio2SourceVoice* music{},*speech{};
    struct Effect {IXAudio2SourceVoice* voice{};unsigned sound{},global=255;Vec3 position;bool positional=false;float gain=1;unsigned hold=UINT32_MAX;};
    std::vector<Effect> effects;SoundBank bank;std::optional<SoundBank> roll_donor;
    std::unique_ptr<PcmAudio> music_pcm,speech_pcm;Job music_job,speech_job;
    std::filesystem::path source;std::uint32_t random=0x53505952;
    Bytes exe;unsigned music_scene{};   // the retail level whose music is playing (Crownworks phases switch it)
    bool playing=false,music_loop=true;
    bool com_initialized=false;std::string device_error;
    unsigned dialogue_revision=UINT32_MAX;const Game* game_id{};
    struct CaptureVoice {unsigned sound{},global=255;Vec3 position;bool positional{},loop{};double cursor{};float pitch=1,gain=1;unsigned hold=UINT32_MAX;};
    std::vector<CaptureVoice> capture_voices;
    bool capturing=false,offline=false,capture_playing=false,capture_muted=false;
    double capture_music{},capture_speech{};const PcmAudio* captured_speech{};
    Vec3 capture_listener,capture_right{1,0,0};float capture_music_volume{},capture_effect_volume{},capture_voice_volume{};
    std::uint32_t capture_random=0x53505952;
    ~Impl(){music_job.stop();speech_job.stop();clear();if(master)master->DestroyVoice();engine.Reset();if(com_initialized)CoUninitialize();}
    void clear_effects(){for(auto& e:effects)destroy(e.voice);effects.clear();}
    void clear(){destroy(music);destroy(speech);clear_effects();music_pcm.reset();speech_pcm.reset();capture_voices.clear();capture_music=capture_speech=0;captured_speech=nullptr;}
    void capture_effect(unsigned id,unsigned global,Vec3 position,bool positional,bool loop,float gain=1) {
        if(id>=bank.sounds.size() || (bank.sounds[id].looping && !loop))return;
        if(capture_voices.size()>=64)capture_voices.erase(capture_voices.begin());
        const auto& sound=bank.sounds[id];capture_random^=capture_random<<13;capture_random^=capture_random>>17;capture_random^=capture_random<<5;
        const auto lo=std::min(sound.pitch_min,sound.pitch_max),hi=std::max(sound.pitch_min,sound.pitch_max);
        capture_voices.push_back({id,global,position,positional,loop,0,std::clamp((lo+capture_random%(hi-lo+1))/4096.0f,0.01f,4.0f),gain});
    }
    void capture_loop(unsigned global,bool on) {
        auto it=std::find_if(capture_voices.begin(),capture_voices.end(),[&](const auto& v){return v.global==global;});
        if(!on){if(it!=capture_voices.end())capture_voices.erase(it);}
        else if(it==capture_voices.end())capture_effect(bank.globals[global],global,{},false,true);
    }
    // Held object sounds: one voice per request key, restarted after it ends
    // and stopped once the key is no longer requested.
    void hold(std::span<const HeldSound> held) {
        for(auto it=effects.begin();it!=effects.end();) {
            if(it->hold!=UINT32_MAX && std::none_of(held.begin(),held.end(),[&](const auto& h){return h.key==it->hold;})){destroy(it->voice);it=effects.erase(it);}
            else ++it;
        }
        for(const auto& h:held) {
            const auto id=resolve(h.event);if(id>=bank.sounds.size())continue;
            const auto found=std::find_if(effects.begin(),effects.end(),[&](const auto& e){return e.hold==h.key;});
            if(found!=effects.end()){found->position=h.event.position;continue;}
            if(effect(id,255,h.event.position,true,bank.sounds[id].looping))effects.back().hold=h.key;
        }
    }
    void capture_hold(std::span<const HeldSound> held) {
        std::erase_if(capture_voices,[&](const auto& v){return v.hold!=UINT32_MAX && std::none_of(held.begin(),held.end(),[&](const auto& h){return h.key==v.hold;});});
        for(const auto& h:held) {
            const auto id=resolve(h.event);if(id>=bank.sounds.size())continue;
            const auto found=std::find_if(capture_voices.begin(),capture_voices.end(),[&](const auto& v){return v.hold==h.key;});
            if(found!=capture_voices.end()){found->position=h.event.position;continue;}
            const auto before=capture_voices.size();capture_effect(id,255,h.event.position,true,bank.sounds[id].looping);
            if(capture_voices.size()>before || (before==64 && !capture_voices.empty()))capture_voices.back().hold=h.key;
        }
    }
    IXAudio2SourceVoice* create(const PcmAudio& pcm,bool loop) {
        if(!engine || pcm.samples.empty())return nullptr;
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=static_cast<WORD>(pcm.channels);
        format.nSamplesPerSec=pcm.rate;format.wBitsPerSample=16;format.nBlockAlign=static_cast<WORD>(pcm.channels*2);format.nAvgBytesPerSec=pcm.rate*format.nBlockAlign;
        IXAudio2SourceVoice* result{};checked(engine->CreateSourceVoice(&result,&format,0,4.0f),"Create audio voice");
        XAUDIO2_BUFFER buffer{};buffer.Flags=XAUDIO2_END_OF_STREAM;buffer.AudioBytes=static_cast<UINT32>(pcm.samples.size()*2);
        buffer.pAudioData=reinterpret_cast<const BYTE*>(pcm.samples.data());
        if(loop){buffer.LoopBegin=pcm.loop_start.value_or(0);buffer.LoopLength=pcm.frames()-buffer.LoopBegin;buffer.LoopCount=XAUDIO2_LOOP_INFINITE;}
        const auto submitted=result->SubmitSourceBuffer(&buffer);if(FAILED(submitted)){destroy(result);checked(submitted,"Queue audio");}
        return result;
    }
    unsigned resolve(const SoundEvent& e)const {
        if(e.scope==SoundScope::Global)return e.id<256?bank.globals[e.id]:255;
        if(e.scope==SoundScope::Surface) {const auto category=bank.footsteps[std::min(e.id,3u)];return category<32?bank.surfaces[category]:255;}
        const auto it=bank.object_sounds.find(e.object_type);return it!=bank.object_sounds.end() && e.id<it->second.size()?it->second[e.id]:255;
    }
    bool effect(unsigned sound,unsigned global,Vec3 position,bool positional,bool loop,float gain=1) {
        if(!engine || sound>=bank.sounds.size() || (bank.sounds[sound].looping && !loop))return false;
        if(effects.size()>=32) {
            const auto it=std::min_element(effects.begin(),effects.end(),[&](const auto& a,const auto& b){return bank.sounds[a.sound].priority<bank.sounds[b.sound].priority;});
            if(bank.sounds[it->sound].priority>bank.sounds[sound].priority)return false;
            destroy(it->voice);effects.erase(it);
        }
        const auto& sample=bank.sounds[sound];auto voice=create(sample.pcm,loop);if(!voice)return false;
        random^=random<<13;random^=random>>17;random^=random<<5;
        const unsigned lo=std::min(sample.pitch_min,sample.pitch_max),hi=std::max(sample.pitch_min,sample.pitch_max);
        voice->SetFrequencyRatio(std::clamp((lo+random%(hi-lo+1))/4096.0f,0.01f,4.0f));
        effects.push_back({voice,sound,global,position,positional,gain});return true;
    }
    void loop(unsigned id,bool enabled) {
        auto found=std::find_if(effects.begin(),effects.end(),[&](const auto& e){return e.global==id;});
        if(!enabled){if(found!=effects.end()){destroy(found->voice);effects.erase(found);}return;}
        if(found==effects.end())effect(bank.globals[id],id,{},false,true);
    }
    void finish(Job& job,IXAudio2SourceVoice*& voice,std::unique_ptr<PcmAudio>& pcm,bool loop,unsigned& frames,std::string& status) {
        std::lock_guard lock(job.mutex);if(!job.done)return;job.done=false;
        if(!job.error.empty()){status=job.error;return;}
        destroy(voice);pcm=std::move(job.pcm);frames=pcm?pcm->frames():0;
        if(pcm)voice=create(*pcm,loop);
        if(loop)status=engine?"Retail music and sound effects ready":device_error;
    }
};
AudioOutput::AudioOutput():impl_(std::make_unique<Impl>()) {
    try{const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(com!=RPC_E_CHANGED_MODE)checked(com,"Initialize Windows audio services");
        impl_->com_initialized=SUCCEEDED(com);
        checked(XAudio2Create(&impl_->engine,0,XAUDIO2_DEFAULT_PROCESSOR),"Open Windows audio");
        checked(impl_->engine->CreateMasteringVoice(&impl_->master,2,48000),"Open playback device");}
    catch(const std::exception& e){status=e.what();impl_->device_error=status;impl_->engine.Reset();}
}
AudioOutput::~AudioOutput()=default;
void AudioOutput::set_july_roll_sound(const SoundBank& donor) {
    if(!import_july_roll_sound(impl_->bank,donor))throw FormatError("July donor has no original roll sound");
    impl_->roll_donor=donor;sample_count=static_cast<unsigned>(impl_->bank.sounds.size());
}
void AudioOutput::load_cinematic(const PcmAudio& pcm) {
    auto& p=*impl_;p.music_job.stop();p.speech_job.stop();p.clear();
    p.music_job.done=p.speech_job.done=false;p.music_loop=false;p.bank={};
    p.music_pcm=std::make_unique<PcmAudio>(pcm);p.music=p.create(*p.music_pcm,false);
    music_frames=pcm.frames();voice_frames=0;p.game_id=nullptr;p.dialogue_revision=UINT32_MAX;
    status="Retail cinematic soundtrack";
}
void AudioOutput::load(const std::filesystem::path& source,std::span<const std::uint8_t> exe,std::span<const std::uint8_t> archive,unsigned slot,std::span<const std::uint8_t> ripto_archive,std::span<const std::uint8_t> forest_archive,bool ripto_encounter,const SoundBank* retail_powers,unsigned area) {
    auto& p=*impl_;p.music_job.stop();p.speech_job.stop();p.clear();p.music_loop=true;
    {std::lock_guard lock(p.speech_job.mutex);p.speech_job.done=false;}
    {std::lock_guard lock(p.music_job.mutex);p.music_job.done=false;}
    music_frames=voice_frames=active_voices=effects_played=roll_effects_played=sample_count=0;p.source=source;
    music_samples_played=voice_samples_played=0;
    p.game_id=nullptr;p.dialogue_revision=UINT32_MAX;track=0;
    try {
        if(game_profile(exe).yotd()) {
            p.bank=decode_sound_bank(archive,exe,slot,area);
            if(retail_powers)import_yotd_sound_fallbacks(p.bank,*retail_powers,retail_power_types);
            if(p.roll_donor)import_july_roll_sound(p.bank,*p.roll_donor);
            sample_count=static_cast<unsigned>(p.bank.sounds.size());
            status=retail_powers?"YOTD native effects with PC sound fallbacks; music unavailable":"YOTD native effects ready; music unavailable";
            return;
        }
        p.bank=decode_sound_bank(archive,exe,slot);const auto music=decode_level_music(exe,ripto_encounter?71:slot);
        p.exe.assign(exe.begin(),exe.end());p.music_scene=ripto_encounter?71:slot;
        const auto import=[&](std::span<const std::uint8_t> archive,unsigned slot,std::initializer_list<unsigned> types) {
            if(archive.empty() || std::all_of(types.begin(),types.end(),[&](unsigned type){return p.bank.object_sounds.contains(type);}))return;
            const auto donor=decode_sound_bank(archive,exe,slot);
            std::map<unsigned,unsigned> imported;
            for(const auto type:types)if(!p.bank.object_sounds.contains(type)) {
                const auto found=donor.object_sounds.find(type);if(found==donor.object_sounds.end())continue;
                auto& list=p.bank.object_sounds[type];
                for(const auto id:found->second) {
                    if(id>=donor.sounds.size()){list.push_back(255);continue;}
                    if(const auto existing=imported.find(id);existing!=imported.end()){list.push_back(static_cast<std::uint8_t>(existing->second));continue;}
                    if(p.bank.sounds.size()>=255)throw FormatError("Power-up sound bank is full");
                    imported[id]=static_cast<unsigned>(p.bank.sounds.size());
                    list.push_back(static_cast<std::uint8_t>(p.bank.sounds.size()));p.bank.sounds.push_back(donor.sounds[id]);
                }
            }
        };
        if(ripto_encounter)import(ripto_archive,71,{0x2c2,0x1e3,0x2af,0x2b0,0x2ad,0x2ac,0x2b7,0x2eb,0x2b9,0x2da,0x2e6,0x2e8});
        else import(ripto_archive,71,{0x2c2,0x1e3,0x2af,0x2b7});
        import(forest_archive,15,{0x1f8});
        if(retail_powers)import_object_sounds(p.bank,*retail_powers,retail_power_types,true);
        if(p.roll_donor)import_july_roll_sound(p.bank,*p.roll_donor);
        if(!game_profile(exe).prerelease()) {
            AssetStore boss_assets(source);WadArchive boss_wad(boss_assets);
            if(slot!=31 && boss_wad.contains(31))import(boss_wad.read(31),31,{0x8f});
            if(slot!=55 && boss_wad.contains(55))import(boss_wad.read(55),55,{0x191,0x289});
        }
        track=music.track;sample_count=static_cast<unsigned>(p.bank.sounds.size());
        status=p.engine?"Loading level music...":p.device_error;
        p.music_job.start(source,music.clip);
    }catch(const std::exception& e){p.bank={};status=e.what();}
}
void AudioOutput::speech(std::optional<XaClip> clip) {
    auto& p=*impl_;p.speech_job.stop();destroy(p.speech);p.speech_pcm.reset();voice_frames=0;
    voice_samples_played=0;
    {std::lock_guard lock(p.speech_job.mutex);p.speech_job.done=false;}
    if(clip)p.speech_job.start(p.source,*clip);
}
void AudioOutput::update(Game* game,bool playing) {
    auto& p=*impl_;
    try {
        if(game && (game!=p.game_id || game->dialogue.revision()!=p.dialogue_revision)) {
            p.game_id=game;p.dialogue_revision=game->dialogue.revision();
            speech(game->dialogue.line()?game->dialogue.line()->speech:std::nullopt);
        }
        // The Crownworks changes music with the fight's phase (6.5.7).
        if(game && game->ripto.crownworks() && !p.exe.empty() && game->ripto.music_scene()!=p.music_scene) {
            p.music_scene=game->ripto.music_scene();p.music_job.stop();destroy(p.music);p.music_pcm.reset();music_frames=0;p.capture_music=0;
            {std::lock_guard lock(p.music_job.mutex);p.music_job.done=false;}
            if(p.music_scene){const auto music=decode_level_music(p.exe,p.music_scene);track=music.track;p.music_job.start(p.source,music.clip);}
            else track=0;
            ++music_changes;
        }
        p.finish(p.music_job,p.music,p.music_pcm,true,music_frames,status);
        p.finish(p.speech_job,p.speech,p.speech_pcm,false,voice_frames,status);
        std::vector<HeldSound> held;
        if(game) {
            const auto powers=game->powers.held_sounds();held.assign(powers.begin(),powers.end());
            const auto boss=game->ripto.held_sounds(*game);held.insert(held.end(),boss.begin(),boss.end());
        }
        if(p.capturing) {
            p.capture_playing=playing;p.capture_muted=muted;p.capture_music_volume=music_volume;p.capture_effect_volume=effects_volume;p.capture_voice_volume=voice_volume;
            if(p.captured_speech!=p.speech_pcm.get()){p.captured_speech=p.speech_pcm.get();p.capture_speech=0;}
            if(playing && game) {
                p.capture_listener=game->player.position;p.capture_right=normalized(cross(Vec3{0,1,0},game->camera.sample(1).forward));
                for(const auto& event:game->sound_events)p.capture_effect(p.resolve(event),255,event.position,event.scope==SoundScope::Object,false,event_gain(event));
                p.capture_hold(held);
                const auto motion=game->player.motion;
                p.capture_loop(0x21,motion==Motion::Charge || motion==Motion::Supercharge || motion==Motion::SuperchargeJump);
                p.capture_loop(6,motion==Motion::UnderwaterSwim || motion==Motion::UnderwaterCharge);
                p.capture_loop(0x1f,motion==Motion::Slide);
                p.capture_loop(0x1c,std::any_of(game->level.whirlwinds.begin(),game->level.whirlwinds.end(),[&](const auto& w){return w.active && length(game->retail_objects[w.object].position-game->player.position)<12;}));
            }
        }
        if(!p.engine){if(game)game->sound_events.clear();return;}
        if(!playing && p.playing)p.clear_effects();p.playing=playing;
        if(p.master)p.master->SetVolume((muted || p.offline)?0.0f:1.0f);
        const bool talking=p.speech && [&]{XAUDIO2_VOICE_STATE state{};p.speech->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);return state.BuffersQueued!=0;}();
        if(p.music){p.music->SetVolume(music_volume*(talking?0.3f:1.0f));if(playing)p.music->Start();else p.music->Stop();}
        if(p.speech){p.speech->SetVolume(voice_volume);if(playing)p.speech->Start();else p.speech->Stop();}
        if(p.music){XAUDIO2_VOICE_STATE state{};p.music->GetState(&state);music_samples_played=state.SamplesPlayed;}
        if(p.speech){XAUDIO2_VOICE_STATE state{};p.speech->GetState(&state);voice_samples_played=state.SamplesPlayed;}
        for(auto it=p.effects.begin();it!=p.effects.end();) {
            XAUDIO2_VOICE_STATE state{};it->voice->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if(!state.BuffersQueued){destroy(it->voice);it=p.effects.erase(it);}else ++it;
        }
        if(playing && game) {
            for(const auto& e:game->sound_events) {
                const auto id=p.resolve(e);if(id>=p.bank.sounds.size())continue;
                if(e.scope==SoundScope::Object) {
                    const float distance=length(e.position-game->player.position)*retail::units_per_world/4;
                    const auto& sound=p.bank.sounds[id];
                    if(distance>=0x2400 || (!sound.volume_min && distance>=sound.far_distance))continue;
                }
                if(p.effect(id,255,e.position,e.scope==SoundScope::Object,false,event_gain(e))) {
                    ++effects_played;
                    if(e.scope==SoundScope::Global && e.id==july_roll_sound_id)++roll_effects_played;
                }
            }
            p.hold(held);
            const auto motion=game->player.motion;
            p.loop(0x21,motion==Motion::Charge || motion==Motion::Supercharge || motion==Motion::SuperchargeJump);
            p.loop(6,motion==Motion::UnderwaterSwim || motion==Motion::UnderwaterCharge);
            p.loop(0x1f,motion==Motion::Slide);
            p.loop(0x1c,std::any_of(game->level.whirlwinds.begin(),game->level.whirlwinds.end(),[&](const auto& w){return w.active && length(game->retail_objects[w.object].position-game->player.position)<12;}));
            const auto listener=game->player.position,forward=game->camera.sample(1).forward;
            const auto right=normalized(cross(Vec3{0,1,0},forward));
            for(auto& effect:p.effects) {
                const auto& sound=p.bank.sounds[effect.sound];float volume=sound.volume_max/32768.0f,pan=0;
                if(effect.positional) {
                    const auto delta=effect.position-listener;const float distance=length(delta)*retail::units_per_world/4;
                    const float span=std::max(1.0f,static_cast<float>(sound.far_distance)-sound.near_distance);
                    const float fade=std::clamp((distance-sound.near_distance)/span,0.0f,1.0f);
                    volume=std::lerp(sound.volume_max/32768.0f,sound.volume_min/32768.0f,fade);
                    if(distance>=0x2000)volume*=std::clamp((0x2400-distance)/1024,0.0f,1.0f);
                    if(length(delta)>0.001f)pan=dot(normalized(delta),right);
                }
                const float matrix[]={1.0f-std::max(0.0f,pan),1.0f+std::min(0.0f,pan)};
                effect.voice->SetOutputMatrix(p.master,1,2,matrix);effect.voice->SetVolume(volume*effects_volume*effect.gain*(talking?0.25f:1.0f));effect.voice->Start();
            }
        }
        if(game)game->sound_events.clear();active_voices=static_cast<unsigned>(p.effects.size());
    }catch(const std::exception& e){status=e.what();p.clear();if(game)game->sound_events.clear();}
}
void AudioOutput::menu_sound(unsigned global_id) {
    auto& p=*impl_;if(global_id>=p.bank.globals.size())return;
    const unsigned id=p.bank.globals[global_id];if(id>=p.bank.sounds.size())return;
    if(p.capturing)p.capture_effect(id,global_id,{},false,false);
    try {if(p.effect(id,global_id,{},false,false)) {
        auto* voice=p.effects.back().voice;voice->SetVolume(p.bank.sounds[id].volume_max/32768.0f*effects_volume);voice->Start();++effects_played;
    }}catch(const std::exception& e){status=e.what();}
}
void AudioOutput::begin_capture(bool offline) {
    auto& p=*impl_;
    if(p.music_job.worker.joinable())p.music_job.worker.join();
    p.finish(p.music_job,p.music,p.music_pcm,true,music_frames,status);
    if((!p.music_pcm || p.music_pcm->samples.empty()) && p.bank.source_game!=GameBuild::YearOfTheDragonUS)
        throw FormatError("Cannot record with sound: level music has not loaded. "+status);
    p.capturing=true;p.offline=offline;p.capture_music=p.capture_speech=0;p.capture_voices.clear();
    p.capture_playing=p.playing;
    if(!offline && p.music){XAUDIO2_VOICE_STATE state{};p.music->GetState(&state);p.capture_music=static_cast<double>(state.SamplesPlayed);}
    p.capture_music_volume=music_volume;p.capture_effect_volume=effects_volume;p.capture_voice_volume=voice_volume;
    p.capture_muted=muted;p.captured_speech=p.speech_pcm.get();
    if(!offline && p.speech){XAUDIO2_VOICE_STATE state{};p.speech->GetState(&state);p.capture_speech=static_cast<double>(state.SamplesPlayed);}
}
void AudioOutput::end_capture(){auto& p=*impl_;p.capturing=p.offline=false;p.capture_voices.clear();if(p.master)p.master->SetVolume(muted?0.0f:1.0f);}
std::array<std::int16_t,1600> AudioOutput::capture_frame() {
    auto& p=*impl_;std::array<std::int16_t,1600> result{};if(!p.capturing || !p.capture_playing)return result;
    std::array<float,1600> mixed{};
    const auto mix=[&](const PcmAudio* pcm,double& cursor,float pitch,float left,float right,bool loop) {
        if(!pcm || !pcm->frames())return;
        const auto end=pcm->frames(),start=pcm->loop_start.value_or(0);const double step=pcm->rate*pitch/48000.0;
        if(loop && cursor>=end)cursor=start+std::fmod(cursor-start,end-start);
        for(unsigned i=0;i<800;++i) {
            if(cursor>=end){if(!loop)break;cursor=start+std::fmod(cursor-start,end-start);}
            const auto a=static_cast<unsigned>(cursor),b=a+1<end?a+1:loop?start:a;const auto fraction=static_cast<float>(cursor-a);
            for(unsigned channel=0;channel<2;++channel) {
                const auto c=std::min(channel,pcm->channels-1);
                const auto value=std::lerp(float(pcm->samples[a*pcm->channels+c]),float(pcm->samples[b*pcm->channels+c]),fraction);
                mixed[i*2+channel]+=value*(channel?right:left);
            }
            cursor+=step;
        }
    };
    const bool talking=p.speech_pcm && p.capture_speech<p.speech_pcm->frames();
    mix(p.music_pcm.get(),p.capture_music,1,p.capture_music_volume*(talking?0.3f:1),p.capture_music_volume*(talking?0.3f:1),p.music_loop);
    mix(p.speech_pcm.get(),p.capture_speech,1,p.capture_voice_volume,p.capture_voice_volume,false);
    for(auto& v:p.capture_voices) {
        const auto& sound=p.bank.sounds[v.sound];float volume=sound.volume_max/32768.0f,pan=0;
        if(v.positional) {
            const auto delta=v.position-p.capture_listener;const float distance=length(delta)*retail::units_per_world/4;
            const float fade=std::clamp((distance-sound.near_distance)/std::max(1.0f,float(sound.far_distance)-sound.near_distance),0.0f,1.0f);
            volume=std::lerp(sound.volume_max/32768.0f,sound.volume_min/32768.0f,fade);
            if(distance>=0x2000)volume*=std::clamp((0x2400-distance)/1024,0.0f,1.0f);
            if(length(delta)>0.001f)pan=dot(normalized(delta),p.capture_right);
        }
        volume*=p.capture_effect_volume*v.gain*(talking?0.25f:1);
        mix(&sound.pcm,v.cursor,v.pitch,volume*(1-std::max(0.0f,pan)),volume*(1+std::min(0.0f,pan)),v.loop);
    }
    std::erase_if(p.capture_voices,[&](const auto& v){return !v.loop && v.cursor>=p.bank.sounds[v.sound].pcm.frames();});
    if(!p.capture_muted)for(unsigned i=0;i<1600;++i)result[i]=static_cast<std::int16_t>(std::clamp(std::lround(mixed[i]),-32768l,32767l));
    return result;
}
} // namespace spyro
