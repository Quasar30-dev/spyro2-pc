#include "spyro/campaign.hpp"
#include "spyro/game_build.hpp"
#include "spyro/coordinates.hpp"
#include <bit>
#include <numbers>

namespace spyro {
std::optional<CampaignExit> campaign_exit(std::span<const std::uint8_t> exe,unsigned slot) {
    // 80019928(-1): resident 80064B84 maps the 29 campaign scene indices
    // to home-world IDs. 80064994 translates those indices to scene IDs.
    if(slot<15 || slot>71 || !(slot&1))return {};
    const ExeTables tables(exe);
    const auto ids=tables(0x80064994),homes=tables(0x80064b84);constexpr unsigned count=29;
    if(!tables.profile.physical_slot(slot))return {};
    if(exe.size()<homes+count)throw FormatError("Missing campaign destination tables");
    const auto index=(slot-15)/2;const auto home=exe[homes+index];
    if(home!=10 && home!=30 && home!=50)throw FormatError("Unknown campaign home destination");
    if(exe[ids+index]==home)return {};
    for(unsigned i=0;i<count;++i)if(exe[ids+i]==home && tables.profile.physical_slot(15+i*2))return CampaignExit{15+i*2,exe[ids+index]};
    if(tables.profile.demo())return {};
    throw FormatError("Campaign home destination has no scene bank");
}
CampaignArrival campaign_arrival(std::span<const std::uint8_t> archive,unsigned source_scene,const Level& level) {
    CampaignArrival arrival{level.spawn,level.spawn_yaw,false};
    // 80071BD4 finds the doorway whose +42 destination is the departed
    // scene ID. Portal records follow the collision and sky sections.
    std::size_t at=read_u32(archive,8),end=at+read_u32(archive,12);
    if(end>archive.size())throw FormatError("Portal scene extent outside archive");
    const auto skip=[&](std::size_t p) {
        if(p>end || end-p<4)throw FormatError("Missing portal section");
        const auto n=read_u32(archive,p);
        if(n<4 || n>end-p)throw FormatError("Invalid portal section extent");
        return p+n;
    };
    for(unsigned i=0;i<6;++i)at=skip(at);
    at=skip(at+12);at=skip(at);const auto limit=skip(at);
    const auto count=read_u32(archive,at+4);
    if(limit-at<8 || count>256 || count*68>limit-at-8)throw FormatError("Invalid campaign portal directory");
    for(unsigned i=0;i<count;++i) {
        const auto record=at+8+i*68;
        if(archive[record+66]!=source_scene)continue;
        const auto point=[&](unsigned offset){return Vec3{float(std::bit_cast<int>(read_u32(archive,record+offset))),
            float(std::bit_cast<int>(read_u32(archive,record+offset+4))),float(std::bit_cast<int>(read_u32(archive,record+offset+8)))};};
        const auto a=point(0),b=point(48),top=point(24);
        const float angle=std::atan2(b.y-a.y,b.x-a.x)+std::numbers::pi_v<float>/2;
        // The retail fly-in starts behind the doorway and faces angle+180
        // while crossing it. Place a grounded arrival on that facing side;
        // projecting the airborne starting point can land outside the map.
        const auto origin=retail::world_position(top+Vec3{-std::cos(angle)*2048,-std::sin(angle)*2048,-1152});
        // Its complete fly-in animation/camera remains separate work.
        const auto floor=level.raycast(origin+Vec3{0,2,0},{0,-1,0},12);
        if(!floor || floor->normal.y<0.45f)throw FormatError("No supported arrival floor beside the campaign portal");
        arrival.position=floor->point;
        arrival.yaw=retail::native_heading((angle+std::numbers::pi_v<float>)/retail::angle_to_radians);
        arrival.doorway=true;return arrival;
    }
    // Boss arenas use dedicated gates instead of these ordinary portal
    // records. Their current return uses the home's authored starting point.
    return arrival;
}
} // namespace spyro
