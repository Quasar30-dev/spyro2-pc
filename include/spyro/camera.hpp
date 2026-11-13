#pragma once
#include "spyro/coordinates.hpp"
#include "spyro/level.hpp"
#include <array>

namespace spyro {
// 80061560 onward: azimuth, elevation, radius, view-pitch bias, view-yaw bias.
// Angles are one turn / 4096; radius is in original world units.
using CameraParameters=std::array<std::int32_t,5>;
struct CameraSettings {
    CameraParameters follow{2048,160,2560,0,0};
    CameraParameters front{0,160,2560,96,0};
    CameraParameters charge{2048,224,1632,-64,0};
    CameraParameters swim{2048,0,2048,0,0};
    // 8006163c: the conversation views 0..2 and the reward views 3 / 4
    // (80016328 picks 3 when the talk ended in view 0, otherwise 4).
    std::array<CameraParameters,5> conversation{{{1408,0,1408,-40,176},{2688,0,1408,-40,-176},{2048,320,2354,-34,0},{1408,0,1408,-40,176},{2688,0,1408,-40,-176}}};
    // Look mode (camera mode 7): 800615b0, springs 80061790, damping 8006181c.
    CameraParameters look{2048,240,768,-128,0},look_spring{256,256,256,256,256},look_damping{12,12,12,12,12};
    CameraParameters spring{48,48,128,64,64};
    CameraParameters damping{12,12,8,12,12};
    CameraParameters minimum_velocity{384,384,768,384,384};
    CameraParameters speed_limit{512,512,512,512,512};
    std::array<std::int16_t,256> sine{},cosine{};
    std::array<std::uint32_t,62> state_map{};
    // 80011d24 sets H=0x155. 8001c2f8 scales the view Y row by 320/512.
    // Preserve the full 512x240 display's vertical framing; wide windows add
    // horizontal coverage. PC rendering reclaims the 12-line safe-area edges.
    float focal_length=341,vertical_correction=320.0f/512,display_height=240;
    CameraSettings();
    float vertical_fov() const;
    std::int32_t sin_angle(std::int32_t angle) const;
    std::int32_t cos_angle(std::int32_t angle) const;
};
CameraSettings decode_camera_settings(std::span<const std::uint8_t> executable);
struct CameraView { Vec3 eye,forward; };
class FollowCamera {
public:
    explicit FollowCamera(CameraSettings settings={});
    void reset(const Level& level,Vec3 feet,float native_yaw);
    // Exactly one original VBlank step. Called once per native 60 Hz tick.
    void tick(const Level& level,Vec3 feet,float native_yaw,unsigned retail_state,float pitch=0,float roll=0);
    void reset_interpolation();
    bool passive=false;
    float follow_scale=1.0f;
    std::optional<float> conversation_yaw;
    std::optional<CameraView> scripted_view;
    // Follow the scripted view exactly (keyed cutscene moves) rather than
    // easing toward it; rendering still interpolates between ticks.
    bool scripted_exact=false;
    int conversation_side=-1;
    // The conversation view in use (80066f5c): the line's own (command 2C) or the one chosen.
    unsigned conversation_view()const{return conversation_side>=0?static_cast<unsigned>(conversation_side):conversation_selected_;}
    // A hard cut to a scripted view (no blend from the current one).
    void cut(CameraView view){scripted_view=view;previous_=view_=view;}
    void center(float yaw);
    // Retail look mode (camera mode 7, 8001fa58): set by Game while Spyro
    // looks. yaw / pitch are the view offsets 80067fcc / 80067fd0.
    struct Look {bool active=false;int yaw{},pitch{};} look;
    // The close look camera has drawn in (8001fa58 sub-state 2): within 0x200
    // of its radius.
    bool look_ready() const {return state_==7 && std::abs(current_[2]-settings.look[2])<0x200;}
    // PC right-stick orbit for the next tick: x / y in -1..1 (right, up);
    // moving says whether Spyro is moving. See tick().
    void steer(float x,float y,bool moving){steer_x_=x;steer_y_=y;moving_=moving;}
    // PC option: ticks (60 a second) after the right stick is let go before
    // the camera returns to Spyro's default angle and height through its own
    // follow spring; 0 = never by itself (it still follows once Spyro runs).
    unsigned recentre_delay=240;
    // Ticks since the right stick last moved the camera (for diagnostics);
    // steered() is true while the stick's angle is held.
    unsigned steer_age()const{return steer_age_;}
    bool steered()const{return steered_;}
    void shake(unsigned strength,unsigned duration);
    CameraView sample(float alpha) const;
    const CameraParameters& parameters() const { return current_; }
    const CameraParameters& velocity() const { return velocity_; }
    unsigned state() const { return state_; }
    bool obstructed() const { return obstructed_; }
    const CameraSettings settings;
private:
    CameraParameters current_{},velocity_{};
    CameraView previous_,view_;
    unsigned state_{};
    unsigned conversation_selected_{};
    bool obstructed_{};
    unsigned shake_strength_{},shake_remaining_{},shake_duration_{},shake_phase_{};
    float steer_x_{},steer_y_{};bool moving_=false,steered_=false;
    int steer_pitch_{};unsigned steer_hold_{},steer_age_{};
    Vec3 offset() const;
    void place(const Level& level,Vec3 anchor,bool sweep);
};
} // namespace spyro
