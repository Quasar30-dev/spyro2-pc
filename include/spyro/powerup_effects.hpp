#pragma once
#include "spyro/assets.hpp"
#include "spyro/retail_math.hpp"

namespace spyro {
struct ChargeSmokeAssets {
    Bytes texture_rgba;
    unsigned width{},height{},blend_mode{};
};
ChargeSmokeAssets decode_charge_smoke(std::span<const std::uint8_t> archive);
struct ChargeSmokePose {Vec3 position;float radius{},angle{};};
class ChargeSmoke {
public:
    void tick(bool emitting,Vec3 origin,Vec3 velocity);
    void reset() {*this=ChargeSmoke{};}
    void reset_interpolation();
    std::vector<ChargeSmokePose> sample(float alpha=1) const;
    unsigned emitted() const {return emitted_;}
private:
    struct Particle {retail::IVec3 position,previous,velocity;unsigned age{},previous_age{},angle{};bool active{},rise{};};
    std::array<Particle,16> particles_{};
    retail::Random random_;
    unsigned phase_=1,next_{},emitted_{};
};
} // namespace spyro
