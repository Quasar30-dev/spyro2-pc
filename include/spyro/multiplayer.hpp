#pragma once
#include "spyro/runtime.hpp"
#include <memory>
#include <sstream>

namespace spyro {
struct OnlineWorld {
    unsigned slot=15;
    bool universe=false;
    UniverseAddress address;
    bool operator==(const OnlineWorld&)const=default;
};
struct OnlineState {
    unsigned peer{},clip{},appearance{},combo{},combo_tick{};
    // S2NET 2: Dark Spyro form (0 normal, 1 dark) and equipped element (0..4).
    unsigned form{},element{};
    std::uint64_t sequence{};
    OnlineWorld world;
    Vec3 position;
    float yaw{},pitch{},roll{},frame{};
    bool valid()const;
};
OnlineWorld online_world(const Level& level);
std::string encode_online_state(const OnlineState& state);
OnlineState decode_online_state(std::istream& input);
struct OnlineStatus {
    bool connected=false,hosting=false;
    unsigned self{},host{},exchanges{};
    std::string message="Offline",room;
    std::vector<OnlineState> players;
};
class OnlineClient {
public:
    OnlineClient();~OnlineClient();
    void connect(const std::string& url,const std::string& room,bool host);
    void disconnect();
    void publish(const OnlineState& state);
    OnlineStatus status()const;
    static std::string room_code();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
class OnlineGhosts {
public:
    std::vector<OnlineState> update(const OnlineStatus& status,const OnlineWorld& world,double seconds);
private:
    struct Ghost {OnlineState previous,next;float age{};};
    std::map<unsigned,Ghost> ghosts_;
};
} // namespace spyro
