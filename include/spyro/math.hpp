#pragma once
#include <algorithm>
#include <cmath>

namespace spyro {
struct Vec3 { float x{}, y{}, z{}; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline Vec3 operator/(Vec3 a, float s) { return a*(1.0f/s); }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a=a+b; return a; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(Vec3 v) { return std::sqrt(dot(v,v)); }
inline Vec3 normalized(Vec3 v) { const float len=length(v); return len>1e-7f ? v/len : Vec3{}; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a+(b-a)*t; }
inline bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
struct Bounds {
    Vec3 min{1e30f,1e30f,1e30f}, max{-1e30f,-1e30f,-1e30f};
    void add(Vec3 v) { min={std::min(min.x,v.x),std::min(min.y,v.y),std::min(min.z,v.z)};
                       max={std::max(max.x,v.x),std::max(max.y,v.y),std::max(max.z,v.z)}; }
    Vec3 center() const { return (min+max)*0.5f; }
};
} // namespace spyro
