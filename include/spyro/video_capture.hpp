#pragma once
#include "spyro/assets.hpp"
#include <d3d11.h>
#include <memory>
#include <array>

namespace spyro {
struct CaptureArea {unsigned x{},y{},width{},height{};};
class VideoCapture {
public:
    VideoCapture();~VideoCapture();
    VideoCapture(const VideoCapture&)=delete;VideoCapture& operator=(const VideoCapture&)=delete;
    void start(const std::filesystem::path& path,unsigned width,unsigned height);
    void frame(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,CaptureArea area,
               std::span<const std::int16_t,1600> audio);
    void finish();
    bool active()const;
    unsigned frames()const;
    static void photo(const std::filesystem::path& path,ID3D11Device* device,ID3D11DeviceContext* context,
                      ID3D11Texture2D* source,CaptureArea area);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace spyro
