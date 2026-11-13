#include "spyro/assets.hpp"
#include "spyro/game_build.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <regex>
#include <sstream>

namespace spyro {
namespace {
std::string normalized(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    const auto version = name.find(';');
    if (version != std::string::npos) name.resize(version);
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos)
        throw FormatError("Unsafe asset path");
    std::istringstream stream(name); std::string part;
    while (std::getline(stream, part, '/'))
        if (part.empty() || part == "." || part == "..") throw FormatError("Unsafe asset path");
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return name;
}
void exact_read(std::ifstream& file, std::uint64_t offset, std::span<std::uint8_t> target) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) throw FormatError("File offset overflow");
    file.clear(); file.seekg(static_cast<std::streamoff>(offset));
    if (!file) throw FormatError("Cannot seek in asset file");
    file.read(reinterpret_cast<char*>(target.data()), static_cast<std::streamsize>(target.size()));
    if (file.gcount() != static_cast<std::streamsize>(target.size())) throw FormatError("Truncated asset file");
}
bool is_sync(std::span<const std::uint8_t> b) {
    return b.size() >= 12 && b[0] == 0 && b[11] == 0 &&
        std::all_of(b.begin()+1, b.begin()+11, [](auto x) { return x == 255; });
}
std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t o) {
    const auto x = read_u32(b, o);
    return (x >> 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x << 24);
}
}
std::uint32_t read_u32(std::span<const std::uint8_t> b, std::size_t o) {
    if (o > b.size() || b.size()-o < 4) throw FormatError("32-bit field outside asset");
    return std::uint32_t(b[o]) | (std::uint32_t(b[o+1])<<8) | (std::uint32_t(b[o+2])<<16) | (std::uint32_t(b[o+3])<<24);
}
std::uint16_t read_u16(std::span<const std::uint8_t> b, std::size_t o) {
    if (o > b.size() || b.size()-o < 2) throw FormatError("16-bit field outside asset");
    return static_cast<std::uint16_t>(b[o] | (b[o+1]<<8));
}
std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t value = 0xffffffff;
    for (auto byte : bytes) {
        value ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) value = (value>>1) ^ (0xedb88320u & (0u-(value&1u)));
    }
    return ~value;
}
ExeInfo inspect_exe(std::span<const std::uint8_t> bytes) {
    if (bytes.size()<2048 || std::memcmp(bytes.data(), "PS-X EXE", 8)) throw FormatError("Not a PS-X EXE");
    ExeInfo info{read_u32(bytes,16), read_u32(bytes,24), read_u32(bytes,28), read_u32(bytes,48), crc32(bytes)};
    if (info.load_size>bytes.size()-2048 || info.load_address<0x80000000u ||
        std::uint64_t(info.load_address)+info.load_size>0x80200000ull || info.pc<info.load_address ||
        std::uint64_t(info.pc)>=std::uint64_t(info.load_address)+info.load_size)
        throw FormatError("Invalid PS-X EXE load extent/entry point");
    return info;
}
void AssetStore::insert(AssetFile file) {
    file.name = normalized(file.name);
    const auto key = file.name;
    if (!files_.emplace(key, std::move(file)).second) throw FormatError("Duplicate normalized asset path: " + key);
}
AssetStore::AssetStore(const std::filesystem::path& original) {
    auto source = std::filesystem::absolute(original);
    if (std::filesystem::is_directory(source)) {
        description_ = "Extracted retail directory";
        const auto root = std::filesystem::weakly_canonical(source);
        for (const auto& node : std::filesystem::recursive_directory_iterator(root)) {
            if (node.is_symlink()) throw FormatError("Extracted asset trees must not contain symlinks");
            if (!node.is_regular_file()) continue;
            if (files_.size() > 100000) throw FormatError("Too many files in extracted tree");
            insert({node.path().lexically_relative(root).generic_string(), node.file_size(), 0, node.path()});
        }
        return;
    }
    std::string extension = source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".cue") {
        std::ifstream cue(source); if (!cue) throw FormatError("Cannot open cue file");
        std::string text((std::istreambuf_iterator<char>(cue)), {});
        const std::regex file_pattern(R"rx(^\s*FILE\s+"([^"]+)"\s+BINARY\s*$)rx",std::regex::icase);
        const std::regex track_pattern(R"(^\s*TRACK\s+\d+\s+(\S+)\s*$)",std::regex::icase);
        const std::regex index_pattern(R"(^\s*INDEX\s+0?1\s+(\d+):(\d+):(\d+)\s*$)",std::regex::icase);
        std::istringstream lines(text);std::string line,current_file,data_file;bool data=false,indexed=false;unsigned count=0;
        while(std::getline(lines,line)) {
            std::smatch m;
            if(std::regex_match(line,m,file_pattern)){current_file=m[1];continue;}
            if(std::regex_match(line,m,track_pattern)) {
                const auto mode=normalized(m[1]);data=mode!="AUDIO";
                if(data) {
                    if(++count!=1 || current_file.empty() || (mode!="MODE2/2352" && mode!="MODE1/2352" && mode!="MODE1/2048"))throw FormatError("CUE must contain one supported data track");
                    data_file=current_file;
                }
            }else if(data && std::regex_match(line,m,index_pattern)) {
                if(m[1]!="00" || m[2]!="00" || m[3]!="00")throw FormatError("CUE data track must start at 00:00:00");
                indexed=true;
            }
        }
        if(count!=1 || !indexed)throw FormatError("CUE is missing its data track or INDEX 01");
        source = source.parent_path() / std::filesystem::u8path(data_file);
    }
    image_.open(source, std::ios::binary); if (!image_) throw FormatError("Cannot open game image");
    image_size_ = std::filesystem::file_size(source);
    std::array<std::uint8_t,16> prefix{}; exact_read(image_,0,prefix);
    stride_ = is_sync(prefix) ? 2352 : 2048;
    if (image_size_%stride_) throw FormatError("Partial disc sector");
    description_ = stride_==2352 ? "Raw CUE/BIN (2352-byte sectors)" : "ISO (2048-byte sectors)";
    auto pvd = sector(16);
    if (pvd[0]!=1 || std::memcmp(pvd.data()+1,"CD001\x01",6) || read_u16(pvd,128)!=2048)
        throw FormatError("Expected ISO9660 primary volume with 2048-byte logical blocks");
    if (read_u32(pvd,158)!=be32(pvd,162) || read_u32(pvd,166)!=be32(pvd,170))
        throw FormatError("ISO root endian copies disagree");
    std::vector<std::uint32_t> visited;
    walk_iso(read_u32(pvd,158),read_u32(pvd,166),"",0,visited);
}
Bytes AssetStore::read_raw_sector(std::uint32_t lba) {
    if (!stride_ || std::uint64_t(lba)>=image_size_/stride_) throw FormatError("LBA outside image");
    Bytes bytes(stride_); exact_read(image_,std::uint64_t(lba)*stride_,bytes); return bytes;
}
Bytes AssetStore::sector(std::uint32_t lba) {
    auto bytes=read_raw_sector(lba);
    if (stride_==2048) return bytes;
    if (!is_sync(bytes)) throw FormatError("Invalid raw sector sync");
    std::size_t start{};
    if (bytes[15]==1) start=16;
    else if (bytes[15]==2) {
        if (!std::equal(bytes.begin()+16,bytes.begin()+20,bytes.begin()+20)) throw FormatError("XA subheader copies disagree");
        if (bytes[18]&0x20) throw FormatError("XA Form 2 requires the raw streaming reader");
        start=24;
    } else throw FormatError("Unsupported sector mode");
    return Bytes(bytes.begin()+start,bytes.begin()+start+2048);
}
void AssetStore::walk_iso(std::uint32_t lba, std::uint32_t size, const std::string& parent,
                          unsigned depth, std::vector<std::uint32_t>& visited) {
    if (depth>16 || size>16*1024*1024 || std::find(visited.begin(),visited.end(),lba)!=visited.end())
        throw FormatError("Invalid or cyclic ISO directory");
    visited.push_back(lba); Bytes data;
    for (std::uint64_t i=0; i<(std::uint64_t(size)+2047)/2048; ++i) {
        if (std::uint64_t(lba)+i>UINT32_MAX) throw FormatError("ISO extent overflow");
        auto bytes=sector(lba+static_cast<std::uint32_t>(i)); data.insert(data.end(),bytes.begin(),bytes.end());
    }
    data.resize(size);
    for (std::size_t at=0; at<data.size();) {
        auto length=data[at];
        if (!length) { at=(at/2048+1)*2048; continue; }
        if (length<34 || length>data.size()-at || (at%2048)+length>2048) throw FormatError("Malformed ISO directory record");
        std::span<const std::uint8_t> rec(data.data()+at,length);
        const auto child_lba=read_u32(rec,2), child_size=read_u32(rec,10);
        if (child_lba!=be32(rec,6) || child_size!=be32(rec,14) || rec[26] || rec[27] || (rec[25]&0x80) ||
            std::size_t(33)+rec[32]>length) throw FormatError("Unsupported or invalid ISO record");
        if (std::uint64_t(child_lba)+(std::uint64_t(child_size)+2047)/2048>image_size_/stride_)
            throw FormatError("ISO file extent outside image");
        std::string name(reinterpret_cast<const char*>(rec.data()+33),rec[32]);
        if (!(name.size()==1 && (name[0]==0 || name[0]==1))) {
            name=normalized(name);
            if (name.find('/')!=std::string::npos) throw FormatError("Slash in ISO file name");
            if (rec[25]&2) walk_iso(child_lba,child_size,parent+name+"/",depth+1,visited);
            else insert({parent+name,child_size,child_lba,{}});
        }
        at+=length;
    }
}
const AssetFile& AssetStore::file(const std::string& name) const {
    const auto it=files_.find(normalized(name));
    if (it==files_.end()) throw FormatError("Missing retail file: "+name);
    return it->second;
}
Bytes AssetStore::read(const std::string& name, std::uint64_t offset, std::size_t size) {
    const auto& f=file(name);
    if (offset>f.size || size>f.size-offset) throw FormatError("Read outside retail file: "+name);
    if (size>256*1024*1024) throw FormatError("Asset read exceeds 256 MiB budget; stream in chunks");
    Bytes result(size);
    if (!size) return result;
    if (!f.disk_path.empty()) {
        std::ifstream stream(f.disk_path,std::ios::binary); if (!stream) throw FormatError("Cannot open extracted asset");
        exact_read(stream,offset,result); return result;
    }
    std::size_t written=0;
    while (written<size) {
        const auto logical=offset+written;
        auto bytes=sector(f.lba+static_cast<std::uint32_t>(logical/2048));
        const auto inside=static_cast<std::size_t>(logical%2048);
        const auto count=std::min(size-written,2048-inside);
        std::copy_n(bytes.begin()+inside,count,result.begin()+written); written+=count;
    }
    return result;
}
Bytes AssetStore::read_all(const std::string& name, std::size_t limit) {
    auto size=file(name).size;
    if (size>limit) throw FormatError("Asset exceeds read budget: "+name);
    return read(name,0,static_cast<std::size_t>(size));
}
std::vector<WadEntry> parse_wad(std::span<const std::uint8_t> header, std::uint64_t size) {
    const auto boundary=read_u32(header,0);
    if (boundary<8 || boundary>0x10000 || boundary%8 || boundary>header.size() || boundary>size)
        throw FormatError("Invalid WAD directory boundary");
    std::vector<WadEntry> result; std::uint64_t previous_end=boundary;
    for (std::uint32_t at=0; at<boundary; at+=8) {
        const auto offset=read_u32(header,at), count=read_u32(header,at+4);
        if (!offset && !count) break; // nested level metadata follows the terminator
        if (!offset || offset<previous_end || std::uint64_t(offset)+count>size || offset%4)
            throw FormatError("Invalid WAD extent at slot "+std::to_string(at/8));
        result.push_back({at/8,offset,count}); previous_end=std::uint64_t(offset)+count;
    }
    if (result.empty()) throw FormatError("Empty WAD directory");
    return result;
}
WadArchive::WadArchive(AssetStore& source,const GameProfile* profile) : source_(source),profile_(profile) {
    file_name_=source.files().contains("WAD.WAD")?"WAD.WAD":game_asset_prefix(source)+"WAD.WAD";
    const auto size=source_.file(file_name_).size;
    entries_=parse_wad(source_.read(file_name_,0,static_cast<std::size_t>(std::min<std::uint64_t>(0x10000,size))),size);
}
const WadEntry& WadArchive::entry(std::uint32_t id) const {
    if(profile_) {
        const auto physical=profile_->physical_slot(id);
        if(!physical)throw FormatError(std::string(profile_->name)+": scene/bank "+std::to_string(id)+" is not present in this build");
        id=*physical;
    }
    if (id>=entries_.size() || entries_[id].id!=id) throw FormatError("WAD slot not present");
    return entries_[id];
}
bool WadArchive::contains(std::uint32_t id)const {
    if(profile_){const auto p=profile_->physical_slot(id);if(!p)return false;id=*p;}
    return id<entries_.size() && entries_[id].size!=0;
}
Bytes WadArchive::read(std::uint32_t id) { return read(id,0,entry(id).size); }
Bytes WadArchive::read(std::uint32_t id, std::uint32_t offset, std::size_t size) {
    const auto& e=entry(id);
    if (offset>e.size || size>e.size-offset) throw FormatError("Read outside WAD slot");
    return source_.read(file_name_,std::uint64_t(e.offset)+offset,size);
}
} // namespace spyro
