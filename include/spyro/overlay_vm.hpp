#pragma once
// Headless R3000 + GTE interpreter for running the original level overlay
// moby scripts against an emulated RAM image (retail executable + level
// overlay + level gameplay data). Hardware, rendering, sound and engine
// services are not emulated: calls into chosen resident functions are routed
// to native high-level handlers (HLE) registered by address.
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace spyro {
class Gte {
public:
    std::array<std::uint32_t,32> data{},control{};
    std::uint32_t read_data(unsigned r);
    void write_data(unsigned r,std::uint32_t v);
    std::uint32_t read_control(unsigned r)const{return control[r];}
    void write_control(unsigned r,std::uint32_t v);
    void command(std::uint32_t op);
private:
    std::int64_t mac(unsigned i,std::int64_t v);
    std::int32_t ir(unsigned i,std::int64_t v,bool lm);
    void push_sz(std::int64_t z);
    void push_sxy(std::int64_t x,std::int64_t y);
    void push_rgb(std::int64_t r,std::int64_t g,std::int64_t b);
    std::int64_t a_check(unsigned i,std::int64_t v);
    void rtp(unsigned vector,bool last,int sf,bool lm);
    void ncds(unsigned v,int sf,bool lm,bool depth,bool color);
    void mvmva(std::uint32_t op);
    std::uint32_t flags{};
};

class OverlayVm {
public:
    static constexpr std::uint32_t ram_size=0x800000;   // 8 MB: the retail 2 MB map plus room for level data
    static constexpr std::uint32_t return_trap=0x80000080;   // ra sentinel: call finished
    std::vector<std::uint8_t> ram=std::vector<std::uint8_t>(ram_size);
    std::array<std::uint8_t,1024> scratch{};
    std::array<std::uint32_t,32> r{};
    std::uint32_t pc{},next_pc{},hi{},lo{};
    Gte gte;
    std::uint64_t executed{};
    // HLE: called instead of the code at the address; it reads a0..a3 /
    // stack arguments and writes v0. Returning normally resumes at ra.
    using Handler=std::function<void(OverlayVm&)>;
    std::unordered_map<std::uint32_t,Handler> hle;
    // Set by a handler to observe a call only: the original code then runs.
    bool passthrough=false;
    // Unknown resident calls (no HLE, address in the guard set) are counted
    // and skipped with v0=0 instead of executing hardware-touching code.
    std::unordered_map<std::uint32_t,std::uint64_t> skipped;
    std::unordered_map<std::uint32_t,bool> guard;   // addresses that must never execute natively
    std::unordered_map<std::uint32_t,std::uint64_t>* trace=nullptr;   // optional: counts of jal/jalr targets
    std::unordered_map<std::uint32_t,std::uint64_t>* executed_pcs=nullptr;   // optional: per-PC execution counts (probes)

    void load(std::uint32_t address,std::span<const std::uint8_t> bytes);
    std::uint8_t read8(std::uint32_t a);
    std::uint16_t read16(std::uint32_t a);
    std::uint32_t read32(std::uint32_t a);
    void write8(std::uint32_t a,std::uint8_t v);
    void write16(std::uint32_t a,std::uint16_t v);
    void write32(std::uint32_t a,std::uint32_t v);
    // Call `entry` with up to four register arguments; runs until it returns.
    std::uint32_t call(std::uint32_t entry,std::span<const std::uint32_t> args={},std::uint64_t budget=20'000'000);
    std::uint32_t arg(unsigned i);   // a0..a3, then the caller's stack slots
    std::string fault;
    std::array<std::uint32_t,16> history{};unsigned history_at{};   // recent PCs, for fault reports
    std::uint32_t watch{};   // diagnostics: report every write touching this byte (0: off)
    void watched(std::uint32_t address,unsigned size,std::uint32_t value);
    std::array<std::pair<std::uint32_t,std::uint32_t>,16> jumps{};unsigned jumps_at{};   // recent register jumps (from,to)
private:
    struct PendingLoad {unsigned reg=0;std::uint32_t value{};};
    PendingLoad load_delay_,next_load_;
    std::uint8_t* translate(std::uint32_t a,unsigned size);
    void set(unsigned reg,std::uint32_t v){if(reg)r[reg]=v;if(load_delay_.reg==reg)load_delay_.reg=0;}
    void step();
};
// Locate a level overlay's moby dispatcher: the `lh v1,0x36(base)` class load
// followed by the largest `li v0,CLASS / beq v1,v0,CASE` tree, and the
// function entry (nearest preceding `addiu sp,sp,-N` with N >= 0x200).
struct OverlayDispatcher {std::uint32_t site{},entry{};unsigned moby_register{};std::unordered_map<unsigned,std::uint32_t> cases;};
OverlayDispatcher find_dispatcher(std::span<const std::uint8_t> overlay,std::uint32_t base=0x8006d264);
// YOTD uses a direct per-class update table, not the S2 dispatcher's BEQ
// tree. Inspect only an overlay after its original resident XOR loader has
// run; reject a malformed or still-encoded table as a whole.
struct YotdMobyHandlers {std::uint32_t table{};std::unordered_map<unsigned,std::uint32_t> updates;};
YotdMobyHandlers find_yotd_moby_handlers(std::span<const std::uint8_t> overlay,std::uint32_t base);
} // namespace spyro
