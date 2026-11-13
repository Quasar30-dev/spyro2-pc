#pragma once
#include "spyro/level.hpp"

namespace spyro {
inline constexpr std::uint32_t flame_attack=0x10000,charge_attack=0x20000,headbash_attack=0x80000;
struct ObjectContact {Vec3 normal;float penetration{};};
std::optional<ObjectContact> container_contact(const RetailObject& object,const ObjectModels& models,Vec3 center,float radius);
bool container_accepts_attack(const RetailObject& object,std::span<const RetailObject> objects,std::uint32_t flags);
class ObjectInteractions {
public:
    Vec3 resolve_player(std::span<RetailObject> objects,const ObjectModels* models,Vec3 center,float radius,
                        Vec3& velocity,bool& grounded,bool charging,std::uint32_t charging_flags=charge_attack);
    void attack_sphere(std::span<RetailObject> objects,const ObjectModels& models,Vec3 center,float radius,std::uint32_t flags);
    void start_flame();
    void stop_flame() {flame_active_=false;flame_age_=0;flames_={};flame_pose_={};previous_flame_pose_={};}
    void follow_flame_attachment(bool follow) {flame_following_=follow;}
    bool flame_active() const {return flame_active_;}
    FlamePose sample_flame(float alpha) const;
    void reset_transients() {phase_=0;flame_active_=false;flame_age_=0;flames_={};flame_pose_={};previous_flame_pose_={};}
    void tick(std::vector<RetailObject>& objects,const ObjectModels* models,const Level& level,Vec3 origin,
              Vec3 velocity,float yaw,Vec3 attachment,bool charging,unsigned& gem_total);
    unsigned broken_containers() const {return broken_;}
    // Flame stream points advanced during the latest flame step (8002f08c
    // tests each against the moby collision spheres; RetailScripts does too).
    const std::vector<Vec3>& flame_points() const {return flame_points_;}
    // The points the retail moby query runs at (8002f4f4): the flame points and
    // the advanced point of a stream the terrain stops.
    const std::vector<Vec3>& flame_query_points() const {return flame_query_points_;}
    unsigned flame_step() const {return flame_step_;}
    unsigned released_gems() const {return released_;}
    bool smash_carried(std::vector<RetailObject>& objects,unsigned index,const ObjectModels& models,const Level& level,
                       Vec3 origin,Vec3 velocity);
private:
    struct Flame {unsigned age{};bool stopped{};Vec3 position;};
    std::array<Flame,8> flames_{};
    std::vector<Vec3> flame_points_,flame_query_points_;
    unsigned flame_step_{};
    FlamePose flame_pose_,previous_flame_pose_;
    bool flame_active_{},flame_following_=true;
    unsigned phase_{},flame_age_{},flame_pattern_{},broken_{},released_{};
    Vec3 flame_offset_;
    float flame_yaw_{};
    retail::Random random_;
    void flame_tick(std::span<RetailObject> objects,const ObjectModels& models,const Level& level,Vec3 origin,float yaw,Vec3 attachment);
    void release(std::vector<RetailObject>& pending,const RetailObject& parent,std::span<RetailObject> objects,
                 const ObjectModels& models,const Level& level,Vec3 origin,Vec3 velocity,bool charging);
    void gem_tick(RetailObject& gem,const ObjectModels& models,const Level& level,Vec3 origin,unsigned& total);
    void fragment_tick(RetailObject& fragment,const Level& level);
};
} // namespace spyro
