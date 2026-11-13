#pragma once
#include "spyro/assets.hpp"
#include "spyro/math.hpp"
#include "spyro/game_build.hpp"
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace spyro {
struct PcmAudio {
    unsigned rate=44100,channels=1;
    std::vector<std::int16_t> samples;
    std::optional<unsigned> loop_start;
    unsigned frames() const {return static_cast<unsigned>(samples.size()/channels);}
    bool operator==(const PcmAudio&) const =default;
};
struct XaClip {unsigned sector{},sectors{},channel{},file=1;std::string asset="SPEECH.STR";bool operator==(const XaClip&)const=default;};
struct LevelMusic {unsigned track{};XaClip clip;};
LevelMusic decode_level_music(std::span<const std::uint8_t> exe,unsigned slot);
class XaDecoder {
public:
    // One complete raw (2352) or subheader-preserving (2336) sector.
    bool append(std::span<const std::uint8_t> sector,const XaClip& filter,PcmAudio& output);
private:
    std::array<int,2> last_{},older_{};
    int coding_=-1;
};
PcmAudio decode_spu(std::span<const std::uint8_t> data);
PcmAudio decode_cinematic_sound(std::span<const std::uint8_t> movie,std::span<const unsigned> chunks,unsigned ticks);
PcmAudio read_xa(const std::filesystem::path& source,const XaClip& clip,const std::function<bool()>& cancelled={});
void write_wave(const std::filesystem::path& path,const PcmAudio& audio);
struct SoundSample {
    unsigned offset{},volume_min{},volume_max{},near_distance{},far_distance{},pitch_min{},pitch_max{},priority{};
    bool looping{};
    PcmAudio pcm;
    bool operator==(const SoundSample&) const =default;
};
struct SoundBank {
    // Runtime events use retail semantic IDs; prototype tables are translated
    // when decoded. Object-local sample indices remain native to their bank.
    std::array<std::uint8_t,256> globals{};
    std::array<std::uint8_t,32> surfaces{};
    std::array<std::uint8_t,4> footsteps{};
    std::map<unsigned,std::vector<std::uint8_t>> object_sounds;
    std::vector<SoundSample> sounds;
    GameBuild source_game=GameBuild::RetailUS;
    unsigned native_samples{};
    std::array<std::uint8_t,256> native_globals{};
    std::array<std::uint8_t,32> native_surfaces{};
    bool pc_fallbacks_imported{};
};
SoundBank decode_sound_bank(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> exe,unsigned slot,unsigned area=0);
// Keep native YOTD Moby IDs and proven global events. Referenced donor
// fallbacks and reserved power-model sound lists are imported once and deduped.
void import_yotd_sound_fallbacks(SoundBank& target,const SoundBank& donor,std::span<const unsigned> power_types={});
void import_object_sounds(SoundBank& target,const SoundBank& donor,std::span<const unsigned> types,bool replace=false);
// July native global 0x14 was removed before retail. Keep it outside the
// translated retail event IDs so importing the roll cannot replace headbash.
inline constexpr unsigned july_roll_sound_id=0xfe;
bool import_july_roll_sound(SoundBank& target,const SoundBank& donor);
enum class SoundScope {Global,Surface,Object};
struct SoundEvent {SoundScope scope=SoundScope::Global;unsigned id{},object_type{};Vec3 position;};
// A sound requested every tick: started when absent, restarted when it ends
// and stopped once no longer requested. The key identifies the request.
struct HeldSound {unsigned key{};SoundEvent event;};
} // namespace spyro
