#include "spyro/multiplayer.hpp"
#include "spyro/universe.hpp"
#include <iomanip>
#include <locale>
#include <numbers>

namespace spyro {
bool OnlineState::valid()const {
    return peer<4 && world.slot>=15 && world.slot<=95 && (world.slot&1) && (!world.address.space || world.universe) &&
        (world.universe?world.slot==universe_source_slot(world.address):world.address.seed==0 && world.address.world==0) &&
        finite(position) && std::abs(position.x)<100000 && std::abs(position.y)<100000 && std::abs(position.z)<100000 &&
        std::isfinite(yaw) && std::isfinite(pitch) && std::isfinite(roll) && std::abs(yaw)<1000 && std::abs(pitch)<10 && std::abs(roll)<10 &&
        clip<100 && std::isfinite(frame) && frame>=0 && frame<512 && appearance<3 && combo<3 && combo_tick<30 && form<2 && element<element_count;
}
OnlineWorld online_world(const Level& level) {
    OnlineWorld world;world.slot=level.wad_slot;world.universe=level.universe.has_value();
    if(level.universe)world.address=level.universe->address;else world.address={0,0,false};return world;
}
std::string encode_online_state(const OnlineState& s) {
    if(!s.valid())throw FormatError("Invalid online player state");
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
    out<<s.peer<<' '<<s.sequence<<' '<<s.world.slot<<' '<<s.world.universe<<' '<<s.world.address.seed<<' '<<s.world.address.world<<' '<<s.world.address.space<<' '
       <<s.position.x<<' '<<s.position.y<<' '<<s.position.z<<' '<<s.yaw<<' '<<s.pitch<<' '<<s.roll<<' '<<s.clip<<' '<<s.frame<<' '<<s.appearance<<' '<<s.combo<<' '<<s.combo_tick<<' '<<s.form<<' '<<s.element<<'\n';
    return out.str();
}
OnlineState decode_online_state(std::istream& in) {
    OnlineState s;in>>s.peer>>s.sequence>>s.world.slot>>s.world.universe>>s.world.address.seed>>s.world.address.world>>s.world.address.space
        >>s.position.x>>s.position.y>>s.position.z>>s.yaw>>s.pitch>>s.roll>>s.clip>>s.frame>>s.appearance>>s.combo>>s.combo_tick>>s.form>>s.element;
    if(!in || !s.valid())throw FormatError("Relay returned an invalid player state");return s;
}
std::vector<OnlineState> OnlineGhosts::update(const OnlineStatus& status,const OnlineWorld& world,double seconds) {
    if(!status.connected){ghosts_.clear();return {};}
    const auto sample=[](const Ghost& ghost) {
        const float blend=std::clamp(ghost.age/0.1f,0.0f,1.0f);auto s=ghost.next;
        s.position=lerp(ghost.previous.position,s.position,blend);
        s.yaw=ghost.previous.yaw+std::remainder(s.yaw-ghost.previous.yaw,2*std::numbers::pi_v<float>)*blend;
        s.pitch=std::lerp(ghost.previous.pitch,s.pitch,blend);s.roll=std::lerp(ghost.previous.roll,s.roll,blend);
        if(s.clip==ghost.previous.clip && s.frame>=ghost.previous.frame)s.frame=std::lerp(ghost.previous.frame,s.frame,blend);
        return s;
    };
    std::set<unsigned> alive;std::vector<OnlineState> result;
    for(const auto& s:status.players) {
        if(s.peer==status.self || !(s.world==world))continue;alive.insert(s.peer);
        auto [it,added]=ghosts_.try_emplace(s.peer,Ghost{s,s,0});auto& ghost=it->second;
        if(!added && s.sequence>ghost.next.sequence){ghost.previous=sample(ghost);ghost.next=s;ghost.age=0;}
        ghost.age+=static_cast<float>(std::clamp(seconds,0.0,0.1));result.push_back(sample(ghost));
    }
    std::erase_if(ghosts_,[&](const auto& entry){return !alive.contains(entry.first);});return result;
}
} // namespace spyro
