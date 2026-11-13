#pragma once
#include "spyro/level.hpp"
#include "spyro/audio.hpp"

namespace spyro {
struct CompanionContext {
    bool idle=false,underwater=false,gem_hint=false;
    unsigned camera_mode{};
    float pitch{},roll{};
    Vec3 camera_eye,camera_forward{0,0,1};
};
class SparxCompanion {
public:
    struct SparkPose {Vec3 position,color;};
    void reset(std::vector<RetailObject>& objects,const ObjectModels* models,Vec3 spyro_origin,float yaw,unsigned health);
    void tick(std::vector<RetailObject>& objects,const ObjectModels* models,const Level& level,Vec3 spyro_origin,
              float yaw,Vec3 velocity,bool charging,bool falling,unsigned health,std::vector<SoundEvent>* sounds=nullptr,unsigned lives=3,
              const CompanionContext& context={});
    std::vector<SparkPose> sparkles(float alpha=1) const;
    void reset_interpolation();
    unsigned emitted_sparkles() const {return emitted_;}
    float glow_angle(float alpha=1) const {return -(float(phase_)+alpha)*2*std::numbers::pi_v<float>/256;}
    void award_deliveries(unsigned& total) {total+=pending_value_;pending_value_=0;}
    void award_health(unsigned& health,unsigned& lives,unsigned maximum);
    unsigned butterflies_eaten() const {return eaten_;}
    unsigned feeding_starts() const {return feeding_starts_;}
    unsigned butterfly_drops() const {return butterfly_drops_;}
    void restore_butterfly_drops(unsigned count) {butterfly_drops_=count;}
    std::optional<std::size_t> object_index() const {return index_;}
    std::optional<std::uint32_t> target_id() const {return target_;}
    unsigned attracted_gems() const {return attracted_;}
    unsigned delivered_gems() const {return delivered_;}
    std::uint64_t script_steps() const {return script_steps_;}
    unsigned idle_state() const {return idle_state_;}
    std::optional<std::uint32_t> hint_id() const {return hint_;}
    std::optional<std::size_t> bubble_index() const {return bubble_;}
private:
    std::optional<std::size_t> index_;
    std::optional<std::uint32_t> target_;
    retail::IVec3 offset_;
    retail::Random random_;
    int timer_{};
    unsigned idle_state_{},idle_delay_=120,turn_rate_=4,follow_flags_=5;
    std::optional<std::uint32_t> hint_;
    std::optional<std::size_t> bubble_;
    bool was_underwater_{};
    unsigned phase_{},attracted_{},delivered_{},pending_value_{};
    std::uint64_t script_steps_{};
    struct Spark {retail::IVec3 position,previous,velocity;Vec3 color;unsigned age{};bool active{};};
    std::array<Spark,64> sparks_{};
    retail::Random effect_random_;
    unsigned next_spark_{},emitted_{};
    std::optional<std::uint32_t> food_target_;
    unsigned food_stage_{},pending_healing_{},pending_lives_{},eaten_{},feeding_starts_{},butterfly_drops_{};
    unsigned food_timer_{};
    void butterflies_tick(std::vector<RetailObject>& objects,const ObjectModels& models,const Level& level,retail::IVec3 origin,unsigned lives,std::vector<SoundEvent>* sounds);
    bool feeding_tick(std::span<RetailObject> objects,const ObjectModels& models,const Level& level,
        RetailObject& sparx,Vec3 origin,float yaw,unsigned health,std::vector<SoundEvent>* sounds);
    void sparkle_tick(const RetailObject* sparx,const ObjectModels& models,unsigned health,bool emitting);
    void fly_gems(std::span<RetailObject> objects,const ObjectModels& models,retail::IVec3 spyro);
    void follow_tick(RetailObject& sparx,const ObjectModels& models,const Level& level,retail::IVec3 spyro,
        float yaw,Vec3 velocity,bool charging,const CompanionContext& context);
    bool hint_tick(RetailObject& sparx,std::span<const RetailObject> objects,const ObjectModels& models,
        const Level& level,retail::IVec3 spyro,const CompanionContext& context);
    void bubble_tick(std::vector<RetailObject>& objects,const ObjectModels& models,unsigned health,const CompanionContext& context);
};
} // namespace spyro
