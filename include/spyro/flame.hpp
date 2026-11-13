#pragma once
#include "spyro/assets.hpp"
#include "spyro/coordinates.hpp"
#include <array>

namespace spyro {
struct FlamePoint {Vec3 position;float twist{};};
struct FlameCurve {
    std::vector<FlamePoint> points;
    // Age 24 of the long envelope reads the preceding packed record. Keep
    // that record explicitly, instead of reproducing a negative array access.
    FlamePoint prefix;
};
struct FlameEnvelope {float head{},span{},fade{};};
struct FlameAssets {
    std::array<FlameCurve,8> curves;
    std::array<std::vector<FlameEnvelope>,2> envelopes;
    std::array<std::uint8_t,16> order{};
    std::array<Vec3,8> offsets{};
    std::array<Vec3,4> tip_colors{};
    std::array<std::uint8_t,62> allowed{};
    Bytes texture_rgba;
    static constexpr unsigned texture_width=32,texture_height=128;
};
FlameAssets decode_flame_assets(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> executable);
enum class FlameEvent {None,Start,Cancel};
class FlameAction {
public:
    FlameEvent tick(bool pressed,bool allowed,bool charging,bool effect_active);
    void reset() {active_=following_=force_follow_=false;age_=0;}
    unsigned action() const {return active_?1:0;}
    unsigned age() const {return age_;}
    bool follows_attachment() const {return following_;}
    bool force_follow() const {return force_follow_;}
private:
    bool active_{},following_{},force_follow_{};
    unsigned age_{};
};
struct FlameStreamPose {float age{};unsigned cutoff=32;};
struct FlamePose {Vec3 base;float yaw{};std::array<FlameStreamPose,8> streams;};
struct FlameVertex {Vec3 position,color;float u{},v{};};
struct FlameGeometry {
    std::vector<FlameVertex> tips,trails;
    unsigned streams{};
};
// Presentation only: the native 30 Hz attack script owns ages and collision.
FlameGeometry flame_geometry(const FlameAssets& assets,const FlamePose& pose,
                             std::span<const std::int16_t,256> sine,std::span<const std::int16_t,256> cosine);
} // namespace spyro
