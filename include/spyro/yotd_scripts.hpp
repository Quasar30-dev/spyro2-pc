#pragma once
#include "spyro/assets.hpp"
#include "spyro/math.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace spyro {
class Game;class OverlayVm;class WadArchive;struct RetailObject;
// User-provided retail buffers; the engine never embeds original game code.
struct YotdScriptImage {Bytes executable,loader,overlay,archive;unsigned slot{},area{},level_id{};};
std::shared_ptr<const YotdScriptImage> make_yotd_script_image(WadArchive&,std::span<const std::uint8_t>,unsigned slot,unsigned area=0);

// Deliberately separate from the S2 resident ABI. Initially owns only the
// visually and behaviorally verified Sunny Villa Rhynocs (FD and EE).
class YotdScripts {
public:
    YotdScripts()=default;
    YotdScripts(const YotdScripts&);
    YotdScripts& operator=(const YotdScripts&);
    void reset(Game&);
    void tick(Game&);
    bool active()const{return vm_ && !failed_;}
    unsigned owned()const{return static_cast<unsigned>(owned_.size());}
    std::uint32_t peek(std::uint32_t address,unsigned size=4)const;
    void poke(std::uint32_t address,std::uint32_t value,unsigned size=4);
    std::uint32_t moby_address(unsigned object)const{return mobys_+object*88;}
    std::uint64_t instructions{};
    unsigned script_ticks{},faults{},attacks_seen{},spawned{},removed{},hits{};
    std::string fault;
private:
    std::shared_ptr<OverlayVm> vm_;
    bool attempted_=false,failed_=false;
    unsigned clock_{},initial_{},authored_{};   // loader mobys / of them, the authored records
    std::uint32_t mobys_{},initial_end_{},spawn_end_{};
    std::vector<unsigned> owned_,slots_;
    std::unordered_map<std::uint32_t,unsigned> reward_owners_;
    std::vector<Vec3> written_;
    Game* current_=nullptr;
    void boot(Game&);
    void bind_hooks();
    void fail(Game&,std::string);
    void build_active(OverlayVm&);
    void sync_in(Game&);
    void sync_out(Game&);
    void mirror(Game&,RetailObject&,std::uint32_t,bool fresh=false);
};
} // namespace spyro
