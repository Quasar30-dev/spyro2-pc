#include "spyro/game_build.hpp"
#include <algorithm>
#include <sstream>

namespace spyro {
namespace {
constexpr GameProfile profiles[]{
    {GameBuild::RetailUS,0xcdc0917c,"Spyro 2 US retail"},
    {GameBuild::July1999,0x2c8235e7,"Spyro 2 July 13, 1999 prototype"},
    {GameBuild::August1999,0xf22e71e1,"Spyro 2 August 11, 1999 prototype"},
    {GameBuild::WinterJampack1999,0x3da0212b,"Spyro 2 Winter Jampack '99 demo"},
    {GameBuild::YearOfTheDragonUS,0x02c69f63,"Spyro 3: Year of the Dragon US (provisional)"}
};
struct Table {unsigned retail,july,august,demo;};
// Evidence: data/analysis/prerelease-20260930/resident-tables.json and the
// isolated Ghidra projects. Entries are addresses, never shipped game data.
constexpr Table tables[]{
    {0x80060334,0x800627e4,0x80065078,0x80066f50},
    {0x8006042c,0x800628dc,0x80065170,0x80067048},
    {0x80061330,0x800637e0,0x80066074,0x80067f4c},
    {0x80066d54,0x80068150,0x8006b340,0x8006c688},
    {0x80062674,0x800649d8,0x800672a8,0x80069134},
    {0x80062868,0x80064bcc,0x8006749c,0x80069328},
    {0x80061bd8,0x80063f3c,0x8006680c,0x80068698},
    {0x80061c58,0x80063fbc,0x8006688c,0x80068718},
    {0x80061e5c,0x800641c0,0x80066a90,0x8006891c},
    {0x80066478,0x80067a48,0x8006ac28,0x8006bf78},
    {0x80061468,0x80063918,0x800661ac,0x80068084},
    {0x80061560,0x80063a10,0x800662a4,0x8006817c},
    {0x80061574,0x80063a24,0x800662b8,0x80068190},
    {0x80061588,0x80063a38,0x800662cc,0x800681a4},
    {0x800615d8,0x80063a88,0x8006631c,0x800681f4},
    {0x8006163c,0x80063aec,0x80066380,0x80068258},
    {0x80061650,0x80063b00,0x80066394,0x8006826c},
    {0x80061664,0x80063b14,0x800663a8,0x80068280},
    {0x8006172c,0x80063b8c,0x8006645c,0x800682f8},
    {0x800617b8,0x80063c18,0x800664e8,0x80068384},
    {0x80061858,0x80063ca4,0x80066588,0x80068410},
    {0x80061844,0x80063cb8,0x80066574,0x80068424},
    {0x80063430,0x800657e8,0x800680b8,0x80069ea8},
    {0x80063448,0x80065800,0x800680d0,0x80069ec0},
    {0x800633f4,0x800657ac,0x8006807c,0x80069e6c},
    {0x80063418,0x800657d0,0x800680a0,0x80069e90},
    {0x8006440c,0x80065d7c,0x80068dc8,0x8006a3f0},
    {0x80064428,0x80065d98,0x80068de4,0x8006a40c},
    {0x80064430,0x80065da0,0x80068dec,0x8006a414},
    {0x80064438,0x80065da8,0x80068df4,0x8006a41c},
    {0x80064440,0x80065db0,0x80068dfc,0x8006a424},
    {0x80061370,0x80063820,0x800660b4,0x80067f8c},
    {0x80061373,0x80063823,0x800660b7,0x80067f8f},
    {0x80064670,0x80065fbc,0x80069008,0x8006a630},
    {0x800625ac,0x80064910,0x800671e0,0x8006906c},
    {0x80062630,0x80064994,0x80067264,0x800690f0},
    {0x8006430c,0x80065c7c,0x80068cc8,0x8006a2f0},
    {0x80061adc,0x80063f04,0x800667d4,0x80068660},
    {0x80061a8c,0x80063edc,0x800667ac,0x8006863c},
    {0x80061a90,0x80063ee0,0x800667b0,0x80068640},
    {0x80061a98,0x80063ee8,0x800667b8,0x80068648},
    {0x800634b8,0x80065870,0x80068140,0x80069f30},
    {0x800634d0,0x80065880,0x80068154,0x80069f40},
    {0x800634e8,0x80065890,0x80068168,0x80069f50},
    {0x800634f4,0x8006589c,0x80068174,0x80069f5c},
    {0x8006351c,0x800658c4,0x8006819c,0x80069fa0},
    {0x800636a4,0x80065a1c,0x80068314,0x8006a0f8},
    {0x800637d4,0x80065a9c,0x80068414,0x8006a178},
    {0x8006387c,0x80065b44,0x800684a0,0x8006a1b8},
    {0x80062968,0x80064ccc,0x8006759c,0x80069428},
    {0x800629e0,0x80064d4c,0x8006761c,0x800694a8},
    {0x80062a70,0x80064dec,0x800676bc,0x80069548},
    {0x80062ad0,0x80064e28,0x800676f8,0x80069584},
    {0x80062d90,0x80065128,0x800679f8,0x80069884},
    {0x80063110,0x80065528,0x80067df8,0x80069c84},
    {0x80063130,0x80065548,0x80067e18,0x80069ca4},
    {0x800631f0,0x80065608,0x80067ed8,0},
    {0x800602b4,0x80062764,0x80064ff8,0x80066ed0},
    {0x80060140,0x800622e4,0x80064cf8,0x80066a50},
    {0x80064de4,0x80062304,0x800694d4,0x80066a70},
    {0x80064994,0x800660f4,0x800691c4,0x8006a760},
    {0x800649b4,0x80066114,0x800691e4,0x8006a780},
    {0x800647f4,0x80066050,0x800690f4,0x8006a6bc},
    {0x80064814,0x80066070,0x80069114,0x8006a6dc},
    {0x80064834,0x80066090,0x80069134,0x8006a6fc},
    {0x80064b84,0x800662c4,0x800693b4,0x8006a8c0}
};
struct YotdTable {unsigned retail,yotd;};
// SCUS_944.67 CRC32 02c69f63. Bounded byte comparisons and decoder surveys;
// see analysis-yotd/resident-table-candidates.json and docs/SPYRO3_FORMATS.md.
// An absent table is an explicit unsupported capability, never an S2 address.
constexpr YotdTable yotd_tables[]{
    {0x80060334,0x80067aa8},{0x8006042c,0x80067bac},
    {0x80062674,0x80066274},{0x80062868,0x80066530},
    {0x80061bd8,0x800658a0},{0x80061c58,0x80065920},
    {0x80061e5c,0x80065b24},{0x80066478,0x80069f40},
    {0x800625ac,0x80066468},{0x80062630,0x800664ec},
    {0x8006440c,0x80066964},{0x80064428,0x80066980},
    {0x80064430,0x80066988},{0x80064438,0x80066990},{0x80064440,0x80066998}
    ,{0x80061adc,0x8006585c},{0x8006430c,0x80066864}
};
}
const GameProfile& game_profile(std::uint32_t crc) {
    for(const auto& p:profiles)if(p.crc==crc)return p;
    std::ostringstream text;text<<"Unmapped Spyro executable CRC32 "<<std::hex<<crc;
    throw FormatError(text.str());
}
const GameProfile& game_profile(std::span<const std::uint8_t> executable){return game_profile(inspect_exe(executable).crc);}
std::string find_game_executable(const AssetStore& source) {
    for(const auto name:{"SCUS_944.25","SPYRO2.EXE","DEMO2.EXE","SPYRO2/DEMO2.EXE","SCUS_944.67"})if(source.files().contains(name))return name;
    throw FormatError("No supported Spyro executable found in this copy");
}
std::string game_asset_prefix(const AssetStore& source) {
    const auto name=find_game_executable(source);const auto slash=name.rfind('/');
    return slash==std::string::npos?std::string{}:name.substr(0,slash+1);
}
std::optional<unsigned> GameProfile::physical_slot(unsigned logical)const {
    if(!prerelease())return logical;
    if(demo()) {
        if(logical==0 || logical==1)return logical;
        if(logical==27)return 29;if(logical==26)return 28;
        if(logical==35)return 45;if(logical==34)return 44;
        return {};
    }
    if(logical==0 || logical==1)return logical;
    const unsigned count=build==GameBuild::July1999?28:29,first=build==GameBuild::July1999?6:8;
    if(logical>=14 && logical<14+count*2)return first+(logical-14)/2*4-unsigned(!(logical&1));
    return {};
}
std::vector<unsigned> GameProfile::levels()const {
    std::vector<unsigned> result;
    if(yotd()){for(unsigned i=97;i<=169;i+=2)result.push_back(i);return result;}
    for(unsigned i=15;i<=95;i+=2)if(physical_slot(i))result.push_back(i);
    return result;
}
std::string game_level_name(std::span<const std::uint8_t> executable,unsigned slot) {
    const ExeTables tables(executable);if(!tables.profile.yotd())return {};
    if(slot<97 || slot>169 || !(slot&1))throw FormatError("Invalid YOTD level name index");
    // SCUS_944.67: 37 resident name pointers, in main-scene WAD order.
    const auto p=tables.native(read_u32(executable,tables.native(0x80069cf8)+(slot-97)/2*4));
    std::string name;for(auto at=p;at<executable.size() && name.size()<128;++at) {
        if(!executable[at])return name;name+=char(executable[at]);
    }
    throw FormatError("Unterminated YOTD level name");
}
std::size_t ExeTables::native(unsigned address)const {
    const auto base=read_u32(bytes_,24);
    if(address<base || std::uint64_t(address)-base+0x800>=bytes_.size())throw FormatError("Resident table outside executable");
    return std::size_t(address)-base+0x800;
}
std::size_t ExeTables::operator()(unsigned address)const {
    if(profile.yotd()) {
        for(const auto& t:yotd_tables)if(t.retail==address)return native(t.yotd);
        std::ostringstream text;text<<profile.name<<": unmapped resident table "<<std::hex<<address;throw FormatError(text.str());
    }
    if(!profile.prerelease())return native(address);
    for(const auto& t:tables)if(t.retail==address) {
        const auto target=profile.build==GameBuild::July1999?t.july:profile.build==GameBuild::August1999?t.august:t.demo;
        if(target)return native(target);
    }
    std::ostringstream text;text<<profile.name<<": unmapped resident table "<<std::hex<<address;throw FormatError(text.str());
}
namespace {
std::vector<WadEntry> level_extents(std::span<const std::uint8_t> archive,bool check_models) {
    if(archive.size()<32)throw FormatError("Invalid level directory");
    const auto boundary=read_u32(archive,0);
    if(boundary<32 || boundary>archive.size() || boundary%4)throw FormatError("Invalid level directory boundary");
    std::vector<WadEntry> result;std::uint64_t end=boundary;
    for(unsigned i=0;i<4;++i) {
        const auto at=read_u32(archive,i*8),size=read_u32(archive,i*8+4);
        if(at<end || at%4 || std::uint64_t(at)+size>archive.size())throw FormatError("Invalid level section extent");
        result.push_back({i,at,size});end=std::uint64_t(at)+size;
    }
    if(check_models && boundary==0x800)(void)level_model_directory(archive);
    return result;
}
}
std::vector<WadEntry> parse_level_archive(std::span<const std::uint8_t> archive) {return level_extents(archive,true);}
std::vector<WadEntry> parse_level_archive(std::span<const std::uint8_t> archive,const GameProfile& profile) {
    return profile.yotd()?level_extents(archive,false):parse_level_archive(archive);
}
unsigned level_model_directory(std::span<const std::uint8_t> archive,const GameProfile& profile) {
    if(!profile.yotd())return level_model_directory(archive);
    // SCUS_944.67 main-scene header: extra scene/gameplay extents precede
    // the Spyro/model directory at +a0, class IDs at +1a0 (bounded by callers).
    if(archive.size()<0xa4)throw FormatError("Missing YOTD model directory");
    return 0xa0;
}
unsigned level_model_directory(std::span<const std::uint8_t> archive) {
    if(read_u32(archive,0x20)==read_u32(archive,0x10))return 0x20;
    if(archive.size()>=0x44 && std::all_of(archive.begin()+0x20,archive.begin()+0x40,[](auto b){return b==0;}))return 0x40;
    throw FormatError("Unknown level model directory layout");
}
bool compact_demo_layout(std::span<const std::uint8_t> archive) {
    if(archive.size()<0x800 || level_model_directory(archive)!=0x20)return false;
    const auto at=read_u32(archive,24),size=read_u32(archive,28);
    if(std::uint64_t(at)+size>archive.size())throw FormatError("Invalid gameplay extent");
    const auto data=archive.subspan(at,size);std::size_t p=0x2c;
    for(unsigned i=0;i<7;++i) {
        const auto n=read_u32(data,p);
        if(n<4 || n%4 || p>data.size() || n>data.size()-p)throw FormatError("Invalid gameplay directory");
        p+=n;
    }
    const auto n=read_u32(data,p),count=read_u32(data,p+4);
    if(n<8 || n>data.size()-p || count>10000 || std::uint64_t(count)*88+8>n)return false;
    for(unsigned i=0;i<count;++i)if(read_u32(data,p+8+i*88)>=data.size())return false;
    return true;
}
} // namespace spyro
