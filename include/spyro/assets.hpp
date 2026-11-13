#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace spyro {
using Bytes = std::vector<std::uint8_t>;
struct FormatError : std::runtime_error { using runtime_error::runtime_error; };
std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset);
std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset);
std::uint32_t crc32(std::span<const std::uint8_t> bytes);

struct AssetFile { std::string name; std::uint64_t size{}; std::uint32_t lba{}; std::filesystem::path disk_path; };
struct ExeInfo { std::uint32_t pc{}, load_address{}, load_size{}, stack{}, crc{}; };
ExeInfo inspect_exe(std::span<const std::uint8_t> bytes);

// Immutable retail bytes; all reads are extent-bounded. File names are ISO-style
// paths regardless of whether the backing store is a disc image or a directory.
class AssetStore {
public:
    explicit AssetStore(const std::filesystem::path& source);
    const std::map<std::string, AssetFile>& files() const { return files_; }
    const std::string& description() const { return description_; }
    const AssetFile& file(const std::string& name) const;
    Bytes read(const std::string& name, std::uint64_t offset, std::size_t size);
    Bytes read_all(const std::string& name, std::size_t limit = 256 * 1024 * 1024);
    // Streaming XA must be requested explicitly, retaining all sector metadata.
    Bytes read_raw_sector(std::uint32_t lba);
private:
    Bytes sector(std::uint32_t lba);
    void walk_iso(std::uint32_t lba, std::uint32_t size, const std::string& parent, unsigned depth,
                  std::vector<std::uint32_t>& visited);
    void insert(AssetFile file);
    std::map<std::string, AssetFile> files_;
    std::ifstream image_;
    std::uint64_t image_size_{};
    std::uint32_t stride_{};
    std::string description_;
};

struct WadEntry { std::uint32_t id{}, offset{}, size{}; };
std::vector<WadEntry> parse_wad(std::span<const std::uint8_t> header, std::uint64_t total_size);
class WadArchive {
public:
    explicit WadArchive(AssetStore& source,const struct GameProfile* profile=nullptr);
    const std::vector<WadEntry>& entries() const { return entries_; }
    const WadEntry& entry(std::uint32_t id) const;
    Bytes read(std::uint32_t id);
    Bytes read(std::uint32_t id, std::uint32_t relative_offset, std::size_t size);
    bool contains(std::uint32_t id)const;
private:
    AssetStore& source_;
    const GameProfile* profile_{};
    std::string file_name_;
    std::vector<WadEntry> entries_;
};
} // namespace spyro
