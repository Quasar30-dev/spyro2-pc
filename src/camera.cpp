#include "spyro/camera.hpp"
#include "spyro/game_build.hpp"
#include "spyro/travel.hpp"
#include <bit>

namespace spyro {
namespace {
int wrap_angle(int value) {
    value&=4095;
    return value>2048?value-4096:value;
}
std::int32_t table_sample(const std::array<std::int16_t,256>& table,int angle) {
    const auto wrapped=static_cast<unsigned>(angle)&4095;
    const auto index=wrapped>>4;
    return table[index]+((table[(index+1)&255]-table[index])*static_cast<int>(wrapped&15)>>4);
}
}
CameraSettings::CameraSettings() {
    // Standalone core scenes have the same convention without requiring a disc.
    // Mounted retail games replace both tables with their executable's values.
    for(unsigned i=0;i<256;++i) {
        sine[i]=static_cast<std::int16_t>(std::lround(std::sin(i*16*retail::angle_to_radians)*4096));
        cosine[i]=static_cast<std::int16_t>(std::lround(std::cos(i*16*retail::angle_to_radians)*4096));
    }
    for(auto i:{1,2,4,5,10,11,16,17})state_map[i]=1;
    state_map[12]=2;state_map[9]=5;
    state_map[40]=state_map[41]=state_map[42]=4;
}
float CameraSettings::vertical_fov() const {
    return 2*std::atan(display_height/(2*focal_length*vertical_correction));
}
std::int32_t CameraSettings::sin_angle(int angle) const { return table_sample(sine,angle); }
std::int32_t CameraSettings::cos_angle(int angle) const { return table_sample(cosine,angle); }
CameraSettings decode_camera_settings(std::span<const std::uint8_t> exe) {
    const ExeTables tables(exe);
    CameraSettings result;
    if(tables.profile.yotd()) {
        // Provisional PC follow controller. YOTD inserts camera profiles and
        // expands its movement-state table; do not index those as S2's 62
        // states. Use the native controller defaults and YOTD's trig samples.
        for(unsigned i=0;i<256;++i) {
            result.sine[i]=std::bit_cast<std::int16_t>(read_u16(exe,tables(0x80061bd8)+i*2));
            result.cosine[i]=std::bit_cast<std::int16_t>(read_u16(exe,tables(0x80061c58)+i*2));
        }
        return result;
    }
    const auto file_at=[&](unsigned va) {return tables(va);};
    const auto read_parameters=[&](unsigned va) {
        CameraParameters p{};
        for(unsigned i=0;i<5;++i)p[i]=std::bit_cast<std::int32_t>(read_u32(exe,file_at(va)+i*4));
        return p;
    };
    result.follow=read_parameters(0x80061560);result.front=read_parameters(0x80061574);
    result.charge=read_parameters(0x80061588);
    result.swim=read_parameters(0x800615d8);
    for(unsigned i=0;i<5;++i)result.conversation[i]=read_parameters(0x8006163c+i*20);
    result.spring=read_parameters(0x8006172c);result.damping=read_parameters(0x800617b8);
    result.look=read_parameters(0x800615b0);result.look_spring=read_parameters(0x80061790);result.look_damping=read_parameters(0x8006181c);
    result.minimum_velocity=read_parameters(0x80061858);result.speed_limit=read_parameters(0x80061844);
    for(unsigned i=0;i<256;++i) {
        result.sine[i]=std::bit_cast<std::int16_t>(read_u16(exe,file_at(0x80061bd8)+i*2));
        result.cosine[i]=std::bit_cast<std::int16_t>(read_u16(exe,file_at(0x80061c58)+i*2));
    }
    for(unsigned i=0;i<result.state_map.size();++i)result.state_map[i]=read_u32(exe,file_at(0x80061468)+i*4);
    return result;
}
FollowCamera::FollowCamera(CameraSettings value):settings(std::move(value)) {}
Vec3 FollowCamera::offset() const {
    // 8001ed20: retain the original two Q12 products and their rounding.
    const auto horizontal=(current_[2]*settings.cos_angle(current_[1]))>>12;
    return retail::world_position({static_cast<float>(horizontal*settings.cos_angle(current_[0])>>12),
        static_cast<float>(horizontal*settings.sin_angle(current_[0])>>12),
        static_cast<float>(current_[2]*settings.sin_angle(current_[1])>>12)});
}
void FollowCamera::place(const Level& level,Vec3 anchor,bool sweep) {
    const auto desired=anchor+offset();
    auto candidate=sweep?view_.eye:desired;
    // 8001e454: camera sphere radius 0x100 and movement slices <=0xf0.
    constexpr float radius=256/retail::units_per_world,step=240/retail::units_per_world;
    const auto delta=desired-candidate;
    const int slices=std::max(1,static_cast<int>(length(delta)/step)+1);
    obstructed_=false;
    for(int slice=0;slice<slices;++slice) {
        candidate+=delta/static_cast<float>(slices);
        for(unsigned pass=0;pass<6;++pass) {
            Vec3 unused_velocity{};bool unused_ground=false;
            const auto resolved=level.resolve_sphere(candidate,radius,unused_velocity,unused_ground);
            if(length(resolved-candidate)<1e-6f)break;
            obstructed_=true;candidate=resolved;
        }
    }
    // Native visibility fallback until the original orbit-search / object
    // collision callbacks in 8001e808 have been reconstructed.
    const auto arm=candidate-anchor;
    if(const auto hit=level.raycast(anchor,arm,length(arm))) {
        candidate=anchor+normalized(arm)*std::max(0.05f,hit->distance-radius);
        obstructed_=true;
    }
    // Portal apertures (collision surface 6) are not walls for the terrain
    // queries, so the arm used to pass through a doorway into the panorama
    // behind it: a home-world arrival (2 units out of the level's doorway,
    // facing away from it) left the camera inside the portal, showing its
    // panorama or recess close up. The camera stops at the aperture.
    // A native rule: the retail sphere test 8003dcc0's handling of surface 6
    // is not reconstructed.
    for(const auto& p:level.portals) {
        const auto n=normalized(cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]));
        if(length(n)<.5f)continue;
        const auto arm=candidate-anchor;const float da=dot(anchor-p.aperture[0],n),dc=dot(candidate-p.aperture[0],n);
        if((da>0)==(dc>0) || std::abs(da-dc)<1e-6f)continue;
        const float t=da/(da-dc);
        if(!portal_contains(p,anchor+arm*t,radius))continue;
        candidate=anchor+arm*std::max(.05f,t-radius/std::max(length(arm),1e-3f));obstructed_=true;
    }
    // Backed against a wall, that fallback presses the camera into Spyro.
    // Search over and around him for a clearer arm instead (a native stand-in
    // for the orbit search) and ease towards it.
    if(obstructed_ && length(candidate-anchor)<1.1f) {
        const auto want=desired-anchor;const float reach=length(want);
        const float yaw=std::atan2(want.z,want.x),elevation=std::atan2(want.y,std::hypot(want.x,want.z));
        float best=length(candidate-anchor);auto best_eye=candidate;
        for(const float lift:{.35f,.7f,1.05f})for(const float turn:{0.0f,.5f,-.5f,1.0f,-1.0f}) {
            const float y=yaw+turn,p=std::min(1.35f,elevation+lift);
            const Vec3 direction{std::cos(y)*std::cos(p),std::sin(p),std::sin(y)*std::cos(p)};
            const auto hit=level.raycast(anchor,direction,reach);
            const float clear=hit?std::max(0.05f,hit->distance-radius):reach;
            if(clear>best+.25f){best=clear;best_eye=anchor+direction*clear;}
        }
        candidate=sweep?lerp(view_.eye,best_eye,.3f):best_eye;
    }
    view_.eye=candidate;
    const auto from_anchor=candidate-anchor;
    const auto horizontal=std::hypot(from_anchor.x,from_anchor.z);
    const float azimuth=std::atan2(from_anchor.z,from_anchor.x)+std::numbers::pi_v<float>+
        current_[4]*retail::angle_to_radians;
    const float pitch=std::atan2(from_anchor.y,horizontal)+current_[3]*retail::angle_to_radians;
    view_.forward={std::cos(azimuth)*std::cos(pitch),-std::sin(pitch),std::sin(azimuth)*std::cos(pitch)};
}
void FollowCamera::reset(const Level& level,Vec3 feet,float yaw) {
    scripted_view.reset();scripted_exact=false;
    shake_strength_=shake_remaining_=shake_duration_=shake_phase_=0;
    current_=settings.follow;
    current_[2]=static_cast<int>(std::lround(current_[2]*follow_scale));
    current_[0]=wrap_angle(current_[0]+static_cast<int>(std::lround(retail::original_heading(yaw))));
    velocity_={};state_=0;
    place(level,feet+retail::origin_above_feet*follow_scale,false);reset_interpolation();
}
void FollowCamera::reset_interpolation() { previous_=view_; }
void FollowCamera::center(float yaw) {
    current_[0]=wrap_angle(settings.follow[0]+static_cast<int>(std::lround(retail::original_heading(yaw))));velocity_[0]=0;
    current_[1]-=steer_pitch_;steered_=false;steer_hold_=0;steer_pitch_=0;
}
void FollowCamera::shake(unsigned strength,unsigned duration) {
    shake_strength_=strength;shake_remaining_=shake_duration_=duration;
}
CameraView FollowCamera::sample(float alpha) const {
    alpha=std::clamp(alpha,0.0f,1.0f);
    return {lerp(previous_.eye,view_.eye,alpha),normalized(lerp(previous_.forward,view_.forward,alpha))};
}
void FollowCamera::tick(const Level& level,Vec3 feet,float yaw,unsigned retail_state,float pitch,float roll) {
    previous_=view_;
    if(scripted_view) {
        state_=9;steer_x_=steer_y_=0;
        if(scripted_exact){view_=*scripted_view;return;}
        view_.eye=lerp(view_.eye,scripted_view->eye,0.125f);
        view_.forward=normalized(lerp(view_.forward,scripted_view->forward,0.125f));
        return;
    }
    const auto previous_state=state_;
    state_=retail_state<settings.state_map.size()?settings.state_map[retail_state]:0;
    int heading=wrap_angle(static_cast<int>(std::lround(retail::original_heading(yaw))));
    auto target=state_==2?settings.charge:settings.follow;
    // Lift camera mode 17 hexadecimal uses the emitter's exit heading,
    // supplied by Game, rather than following Spyro's spinning body.
    if(state_==23 && level.campaign_scene==51)target[2]=0x600;
    if(state_==4) {
        // Overlay 18, 80070548: underwater pitch follows the body with a
        // bounded pitch-lag bias and one quarter of body roll.
        target=settings.swim;target[1]=-static_cast<int>(std::lround(pitch/retail::angle_to_radians));
        target[3]=std::clamp((target[1]-current_[1])/4,-192,192);
        if(std::abs(target[3])<9)target[3]=0;
        target[4]=-static_cast<int>(std::lround(roll/retail::angle_to_radians))/4;
    }
    // State 1 keeps a front-facing camera when running towards it (8001fa58).
    if(state_==1 && std::abs(wrap_angle(current_[0]-heading))<0x280)target=settings.front;
    // Look mode: the close camera behind the head.
    const bool looking=look.active && !conversation_yaw;
    if(looking){state_=7;target=settings.look;}
    if(conversation_yaw) {
        // Retail camera mode 12 (800216d8 / 8001fa58): face the NPC
        // using its three authored dialogue views, with an obstruction fallback.
        state_=18;heading=wrap_angle(static_cast<int>(std::lround(retail::original_heading(*conversation_yaw)))+2048);
        if(previous_state!=18) {
            conversation_selected_=wrap_angle(current_[0]-heading)<0?1:0;
            const auto saved=current_;
            for(unsigned attempt=0;attempt<3;++attempt) {
                const auto side=attempt==2?2:conversation_selected_^attempt;
                current_=settings.conversation[side];current_[0]=wrap_angle(current_[0]+heading);
                const auto arm=offset(),anchor=feet+retail::origin_above_feet;
                const auto hit=level.raycast(anchor,arm,length(arm));
                if(!hit || hit->distance>=length(arm)-0.1f || attempt==2){conversation_selected_=side;break;}
            }
            current_=saved;
        }
        target=settings.conversation[conversation_side>=0 && conversation_side<5?conversation_side:conversation_selected_];
    }
    target[0]=wrap_angle(target[0]+heading);
    if(!looking && !conversation_yaw)target[2]=static_cast<int>(std::lround(target[2]*follow_scale));
    if(looking) {
        // 8001fa58 case 7: the azimuth turns by the yaw offset. Pitch first
        // raises / lowers the camera; below -0x140 the view tilts instead.
        // The camera is held short of straight overhead (retail allows
        // 0x500 more elevation, which carries it over the head).
        target[0]=wrap_angle(target[0]-look.yaw);
        if(look.pitch< -0x140){target[1]-=0x140;target[3]+=0x140+look.pitch;}
        else target[1]=std::min(target[1]+look.pitch,1000);
    }
    if(passive && state_!=2 && state_!=4 && state_!=18 && !looking)target[0]=current_[0];
    // PC right-stick orbit (no retail equivalent). Turning moves the camera
    // directly; it then stays put while the stick was used in the last 1.5 s
    // or Spyro stands still, and the follow spring takes over once he moves.
    // Up looks up (lowers the camera). recentre_delay ticks after the stick
    // was let go (the PC option, 4 s by default) the stick's angle and height
    // are dropped and the retail follow spring eases the camera back to
    // Spyro's default angle (no cut).
    if(!looking && state_!=18) {
        const bool stick=std::abs(steer_x_)>0 || std::abs(steer_y_)>0;
        if(std::abs(steer_x_)>0) {
            current_[0]=wrap_angle(current_[0]-static_cast<int>(std::lround(steer_x_*32)));velocity_[0]=0;
            steered_=true;steer_hold_=90;
        }
        // Height moves directly too: the elevation spring crawls at one unit a
        // tick near its target (the 80061858 minimum-velocity rule).
        if(std::abs(steer_y_)>0 && state_!=4) {
            const int next=std::clamp(steer_pitch_-static_cast<int>(std::lround(steer_y_*12)),-140,420);
            current_[1]+=next-steer_pitch_;steer_pitch_=next;
        }
        if(stick)steer_age_=0;
        else if(steered_ || steer_pitch_)++steer_age_;
        if(steered_ && !steer_hold_ && moving_)steered_=false;
        if(recentre_delay && steer_age_>=recentre_delay && (steered_ || steer_pitch_)){steered_=false;steer_pitch_=0;}
        if(steered_)target[0]=current_[0];
        if(state_!=4)target[1]+=steer_pitch_;
    }
    if(steer_hold_)--steer_hold_;
    steer_x_=steer_y_=0;
    for(unsigned i=0;i<5;++i) {
        const int error=i==2?target[i]-current_[i]:wrap_angle(target[i]-current_[i]);
        auto spring=(state_==2 && i==0)?std::max(100,settings.spring[i]):settings.spring[i];
        if(looking)spring=settings.look_spring[i];
        if(state_==4 && i==0)spring=std::max(spring,128);
        if(state_==4 && i==1)spring=std::max(spring,96);
        if(state_==23 && i==0)spring=std::max(spring,76);
        // 8001db58 / 8001dc6c. C++20 signed right shift matches MIPS SRA.
        const auto force=static_cast<std::int64_t>(spring)*error-
            static_cast<std::int64_t>(looking?settings.look_damping[i]:settings.damping[i])*velocity_[i];
        velocity_[i]+=static_cast<std::int32_t>(force>>6);
        if(std::abs(velocity_[i])<settings.minimum_velocity[i])velocity_[i]=std::clamp(error*128,-256,256);
        if(settings.speed_limit[i]>=0)velocity_[i]=std::clamp(velocity_[i],-settings.speed_limit[i]*256,settings.speed_limit[i]*256);
        current_[i]+=velocity_[i]>>8;
        if(i!=2)current_[i]=wrap_angle(current_[i]);
    }
    place(level,feet+retail::origin_above_feet*(conversation_yaw?1.0f:follow_scale),true);
    // 8001d8a4: alternating vertical impulse, linearly decaying over VBlanks.
    ++shake_phase_;
    if(shake_remaining_ && shake_duration_) {
        --shake_remaining_;
        const float sign=(shake_phase_/4)&1?1.0f:-1.0f;
        view_.eye.y+=sign*shake_strength_*(float(shake_remaining_)/shake_duration_)/(64*retail::units_per_world);
    }
}
} // namespace spyro
