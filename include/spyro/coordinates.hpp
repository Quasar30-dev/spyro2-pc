#pragma once
#include "spyro/math.hpp"
#include <numbers>

namespace spyro::retail {
// All decoded positions keep the game's relative scale. 1024 is a common
// unit conversion, not a model size adjustment. Original axes are X/Y/Z-up.
inline constexpr float units_per_world=1024.0f;
inline constexpr float angle_to_radians=2*std::numbers::pi_v<float>/4096;
inline constexpr int spyro_origin_units=0x164; // 8001a2fc, 8001722c
inline constexpr float spyro_origin_height=spyro_origin_units/units_per_world;
inline constexpr Vec3 origin_above_feet{0,spyro_origin_height,0};
inline Vec3 world_position(Vec3 original) {
    return {original.x/units_per_world,original.z/units_per_world,original.y/units_per_world};
}
inline float native_heading(float original_angle) {
    return std::numbers::pi_v<float>/2-original_angle*angle_to_radians;
}
inline float original_heading(float native_yaw) {
    return (std::numbers::pi_v<float>/2-native_yaw)/angle_to_radians;
}
// Local +Z is Spyro's forward direction. The minus sign preserves handedness
// when this local frame is rotated into the native world axes.
inline Vec3 model_position(Vec3 original) {
    return {-original.y/units_per_world,original.z/units_per_world,original.x/units_per_world};
}
inline Vec3 rotate_model(Vec3 local,float native_yaw) {
    const auto s=std::sin(native_yaw),c=std::cos(native_yaw);
    return {local.x*c+local.z*s,local.y,-local.x*s+local.z*c};
}
inline Vec3 rotate_model(Vec3 local,float yaw,float pitch,float roll) {
    const float sr=std::sin(roll),cr=std::cos(roll),sp=std::sin(pitch),cp=std::cos(pitch);
    const Vec3 banked{local.x*cr-local.y*sr,local.x*sr+local.y*cr,local.z};
    return rotate_model({banked.x,banked.y*cp+banked.z*sp,-banked.y*sp+banked.z*cp},yaw);
}
} // namespace spyro::retail
