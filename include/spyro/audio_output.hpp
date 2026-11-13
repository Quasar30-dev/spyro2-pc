#pragma once
#include "spyro/audio.hpp"
namespace spyro {
class Game;
class AudioOutput {
public:
    AudioOutput();~AudioOutput();
    AudioOutput(const AudioOutput&)=delete;AudioOutput& operator=(const AudioOutput&)=delete;
    void load(const std::filesystem::path& source,std::span<const std::uint8_t> exe,std::span<const std::uint8_t> archive,unsigned slot,
              std::span<const std::uint8_t> ripto_archive={},std::span<const std::uint8_t> forest_archive={},bool ripto_encounter=false,
              const SoundBank* retail_powers=nullptr,unsigned area=0);
    void update(Game* game,bool playing);
    void load_cinematic(const PcmAudio& pcm);
    void menu_sound(unsigned global_id);
    void set_july_roll_sound(const SoundBank& donor);
    void speech(std::optional<XaClip> clip);
    void begin_capture(bool offline=false);
    std::array<std::int16_t,1600> capture_frame(); // 800 stereo samples = 1/60 s at 48 kHz.
    void end_capture();
    float music_volume=0.5f,effects_volume=0.8f,voice_volume=1.0f;
    bool muted=false;
    std::string status;
    unsigned track{},sample_count{},active_voices{},effects_played{},roll_effects_played{},music_frames{},voice_frames{},music_changes{};
    std::uint64_t music_samples_played{},voice_samples_played{};
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace spyro
