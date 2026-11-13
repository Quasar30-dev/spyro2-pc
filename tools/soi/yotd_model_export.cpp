// Offline local donor export and bounded original S2 MIPS vertex-consumer check.
// No game session, executable modification, generated character or semantic clip claims.
#include "spyro/assets.hpp"
#include "spyro/objects.hpp"
#include "spyro/overlay_vm.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace spyro;
namespace fs = std::filesystem;

void vector_json(std::ostream& out, Vec3 p) {out << '[' << p.x << ',' << p.y << ',' << p.z << ']';}
void verify_vertices(const ObjectModel& model, std::span<const std::uint8_t> bank,
                     std::span<const std::uint8_t> executable) {
    if(!model.animated)return;
    OverlayVm vm;
    const auto load = read_u32(executable, 0x18);
    vm.load(load, executable.subspan(0x800, read_u32(executable, 0x1c)));
    constexpr unsigned bank_address = 0x80100000, descriptor = 0x800a0000, state = 0x800a0100;
    vm.load(bank_address, bank);
    vm.hle[0x80044874] = [](OverlayVm&) {}; // Stop after the original decoder produces each packed coordinate.
    const auto common = read_u32(bank, 56);
    std::uint64_t vertices = 0;
    for(unsigned clip = 0; clip < model.animations.size(); ++clip) {
        const auto at = read_u32(bank, 60 + clip * 4);
        const auto base = bank_address + common + read_u32(bank, at + 12);
        const auto stream = bank_address + common + read_u32(bank, at + 32);
        const auto shift = bank[at + 2];
        for(const auto& frame : model.animations[clip].frames) {
            vm.r.fill(0); vm.r[1] = frame.control; vm.r[13] = 0; vm.r[19] = state;
            vm.r[20] = descriptor; vm.r[28] = 0x80062674; vm.r[29] = 0x800c0000;
            vm.write32(descriptor + 12, base); vm.write32(descriptor + 32, stream);
            vm.write32(state + 0x3c, 0);
            for(unsigned v = 0; v < frame.vertices.size(); ++v) {
                const std::array<std::uint32_t, 4> accumulators{vm.r[4], vm.r[5], vm.r[6], vm.r[7]};
                vm.call(v == 0 ? 0x80044684 : 0x80044714, accumulators, 300);
                if(!vm.fault.empty())throw FormatError("Original MIPS vertex decoder: " + vm.fault);
                const auto packed = vm.r[16];
                const auto x = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(packed));
                const auto y = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(packed >> 16));
                const auto z = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(vm.r[3]));
                const float scale = float(1u << shift) / 1024.0f;
                const Vec3 actual{x * scale, -y * scale, z * scale};
                const auto expected = frame.vertices[v];
                if(actual.x != expected.x || actual.y != expected.y || actual.z != expected.z) {
                    std::cerr << "DETAIL control " << std::hex << frame.control << " base " << base << " stream " << stream
                              << " packed " << packed << " regs " << vm.r[3] << ' ' << vm.r[4] << ' ' << vm.r[5]
                              << " mode " << vm.r[6] << " hw " << vm.r[2] << " byte " << vm.r[1] << std::dec
                              << " actual " << actual.x << ',' << actual.y << ',' << actual.z
                              << " expected " << expected.x << ',' << expected.y << ',' << expected.z << '\n';
                    throw FormatError("Original MIPS vertex mismatch clip " + std::to_string(clip) + " vertex " + std::to_string(v));
                }
                ++vertices;
            }
        }
    }
    std::cout << "ORIGINAL S2 VERTEX SLICE EQUALITY " << vertices << " vertices, " << vm.executed
              << " instructions; stop 80044874; no whole renderer/behavior proof\n";
}

int main(int argc, char** argv) {
    try {
        if(argc != 5)throw FormatError("usage: yotd_model_export DATA_DIR SLOT OUTPUT_DIR CLASS_HEX[,CLASS_HEX...] or all");
        AssetStore source(argv[1]), oracle("data/disc");
        const auto exe = source.read_all(find_game_executable(source));
        const auto s2exe = oracle.read_all(find_game_executable(oracle));
        const auto& profile = game_profile(exe);
        WadArchive wad(source, &profile);
        const auto slot = unsigned(std::stoul(argv[2]));
        const auto archive = wad.read(slot);
        auto models = decode_object_models(archive, exe);
        const fs::path out(argv[3]); fs::create_directories(out);
        std::set<unsigned> wanted;
        if(std::string(argv[4]) != "all") {
            std::istringstream input(argv[4]); std::string word;
            while(std::getline(input, word, ','))wanted.insert(unsigned(std::stoul(word, nullptr, 16)));
        }
        std::ofstream atlas(out / "atlas.rgba", std::ios::binary);
        atlas.write(reinterpret_cast<const char*>(models.texture_rgba.data()), models.texture_rgba.size()); atlas.close();
        std::ofstream json(out / "models.json");
        json << "{\"slot\":" << slot << ",\"texture_width\":" << models.texture_width
             << ",\"texture_height\":" << models.texture_height << ",\"models\":[";
        bool first_model = true;
        for(const auto& [type, model] : models.models) {
            if(!wanted.empty() && !wanted.contains(type))continue;
            std::cout << "MODEL " << std::hex << type << std::dec << " source " << model.source_offset << " bytes " << model.source_bytes
                      << " clips " << model.animations.size() << " meshes " << model.meshes.size() << '\n';
            verify_vertices(model, std::span(archive).subspan(model.source_offset, model.source_bytes), s2exe);
            if(!first_model)json << ',';first_model = false;
            json << "{\"class\":" << type << ",\"source_offset\":" << model.source_offset << ",\"source_bytes\":" << model.source_bytes
                 << ",\"meshes\":[";
            for(unsigned i = 0; i < model.meshes.size(); ++i) {
                if(i)json << ',';const auto& mesh = model.meshes[i];json << "{\"vertices\":[";
                for(unsigned v = 0; v < mesh.vertices.size(); ++v) {if(v)json << ',';vector_json(json, mesh.vertices[v]);}
                json << "],\"faces\":[";
                for(unsigned f = 0; f < mesh.faces.size(); ++f) {
                    if(f)json << ',';const auto& face = mesh.faces[f];const auto count = face.quad ? 4u : 3u;
                    json << "{\"vertices\":[";
                    for(unsigned k = 0; k < count; ++k) {if(k)json << ',';json << face.vertices[k];}
                    json << "],\"colours\":[";
                    for(unsigned k = 0; k < count; ++k) {if(k)json << ',';vector_json(json, face.colors[k]);}
                    json << "],\"uv\":[";
                    for(unsigned k = 0; k < count; ++k) {if(k)json << ',';json << '[' << face.uv[k][0] << ',' << face.uv[k][1] << ']';}
                    json << "],\"textured\":" << (face.textured ? "true" : "false") << ",\"page\":" << face.page
                         << ",\"clut\":" << face.clut << ",\"flags\":" << face.flags << '}';
                }
                json << "]}";
            }
            json << "],\"clips\":[";
            for(unsigned c = 0; c < model.animations.size(); ++c) {
                if(c)json << ',';const auto& clip = model.animations[c];json << "{\"clip\":" << c << ",\"frames\":[";
                for(unsigned f = 0; f < clip.frames.size(); ++f) {
                    if(f)json << ',';const auto& frame = clip.frames[f];
                    json << "{\"mesh\":" << frame.mesh << ",\"control\":" << frame.control << ",\"metadata\":" << frame.metadata
                         << ",\"ticks\":" << frame.ticks << ",\"vertices\":[";
                    for(unsigned v = 0; v < frame.vertices.size(); ++v) {if(v)json << ',';vector_json(json, frame.vertices[v]);}
                    json << "]}";
                }
                json << "]}";
            }
            json << "]}";
        }
        json << "]}\n";json.close();
        if(!json || !atlas)throw FormatError("Export write failure");
        std::cout << "EXPORTED " << (out / "models.json").string() << "\nLIMIT source clip indices retained; identity, clip semantics, far decoder, audio and runtime integration require separate evidence\n";
        return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
