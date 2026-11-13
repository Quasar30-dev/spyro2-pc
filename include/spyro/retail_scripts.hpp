#pragma once
// Original Spyro 2 US level moby scripts, executed by the overlay interpreter.
// Other executable profiles are rejected before this game's resident map is
// loaded. Spyro 3 shares the CPU but needs a separately verified loader/bridge.
//
// The level's RAM image is booted the way the retail loader does it: the
// executable, overlay 1's hook installer (8006d960), scene relocation
// (8006fa98, which also exposes the collision tables to 800231f4), model
// bank relocation (80070018, filling the class table 80068c94), the level
// overlay, then the gameplay-section loader 8001c734. Every 30 Hz script
// tick runs the overlay's own moby dispatcher (pointer 800670f0), including
// its animation clock 80041068 and the original terrain/collision helpers.
//
// Classes the native engine already reconstructs (gems, containers, NPCs,
// power gates, level systems, translated enemies...) stay native. The rest of
// the dispatcher's classes are "script owned": their RAM moby is the source of
// truth for position, rotation, state, animation and material, mirrored into
// the engine's RetailObject for rendering and gameplay. Engine attacks are
// written to the moby's +18 attack word; Spyro is hurt by the same 8004055c
// sphere query the resident movement code issues.
#include "spyro/assets.hpp"
#include "spyro/math.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace spyro {
class Game;class OverlayVm;class WadArchive;struct RetailObject;
// One 8004c214 persistence record. Scripts and the dialogue code register
// every byte that must outlive the level visit (quest flags, conversation
// line pointers, one-shot triggers) in the level-state lists 8006718c and
// 80067414; the PC save keeps them per level and reapplies them at boot.
struct RetailMemory {std::uint32_t list{},address{},size{},value{};};
struct RetailScriptImage {
    Bytes executable,loader,overlay,archive;
    unsigned slot{},level_id{};
};
// Retail level id (80066f90) of a WAD level slot; 0 when the slot is not a
// gameplay level with an overlay dispatcher.
unsigned retail_level_id(unsigned slot);
std::shared_ptr<const RetailScriptImage> make_retail_script_image(WadArchive& wad,std::span<const std::uint8_t> executable,unsigned slot);
struct RetailBoot {std::uint32_t mobys{},dispatcher{};unsigned count{};std::string fault;};
// A sprite the level's own HUD draw hook (80067158) placed through the
// resident primitive 800520cc(descriptor, x, y, size type): the descriptor's
// four bytes (u, v, flags: bit 7 semi-transparent, bits 0-1 CLUT x; CLUT row),
// the size from 800634b8 / 800634d0[type], and the CLUT and colour the caller
// left in the primitive (a dimmed or recoloured icon).
struct RetailHudDraw {
    std::uint8_t u{},v{},flags{},row{},width{},height{},type{};
    std::int16_t x{},y{};std::uint16_t clut{};std::uint32_t color=0x808080;
};
inline constexpr std::uint32_t retail_scene_base=0x80200000,retail_model_base=0x80300000,retail_gameplay_base=0x80400000,
    retail_particle_base=0x80700000,retail_stack_top=0x807fe000;
RetailBoot boot_retail_level(OverlayVm& vm,const RetailScriptImage& image);
// True when the engine reconstructs this class natively; such mobys never run in the interpreter.
bool native_retail_class(unsigned type);
// A class native elsewhere that runs its original case in this level (WAD
// slot): Cloud Temples' trolls (1EE) and the ice blocks they become (271).
bool level_script_class(unsigned slot,unsigned type);
// An orb challenge giver (WAD slot, class, header kind byte) whose own class
// code runs the challenge; it is not a native speaker (docs/ORB_CHALLENGES.md).
// Some classes hand over only one kind (Cloud Temples' 1C7: Murgen, kind 3).
bool challenge_giver(unsigned slot,unsigned type,int kind=-1);

class RetailScripts {
public:
    void tick(Game& game);
    // 8002c0f4's mode-0 query: push Spyro's collision sphere out of solid
    // script mobys (doors, gates, platforms, bodies). Returns the new centre.
    // `attack` is Spyro's live attack word (8006a128): ORed into the +18 of
    // the mobys his collision sphere touches, as the movement queries do.
    Vec3 resolve_player(Game& game,Vec3 center,float radius,Vec3& velocity,bool& grounded,std::uint32_t attack=0);
    // A powered shot (superflame fireball, green bomb) touching script mobys:
    // the moby-sphere query 8004055c at its centre with its attack word, which
    // ORs the bits into the +18 of each moby it touches (as the retail shot
    // mobys' own queries do). Returns true when it touched a solid moby.
    bool attack_query(Vec3 center,float radius,std::uint32_t attack);
    bool active()const{return booted_ && vm_!=nullptr;}
    // Spyro holds a moby in his mouth (head state 2..5, 8006a15c): Circle
    // spits it instead of flaming.
    bool carrying()const;
    unsigned head_state()const;   // 8006a15c (0 when the scripts are not running)
    unsigned owned()const{return static_cast<unsigned>(owned_.size());}

    // ---- Progression and conversations (original logic, native presentation).
    // Controller state for the scripts' pad words 800683a0/a4/a8 (held bits
    // and this frame's presses, libpad layout: 10 triangle, 20 circle,
    // 40 cross, 80 square).
    void set_pad(std::uint32_t held,std::uint32_t pressed);
    // A conversation the engine starts with a native NPC: runs 80015be4 (and
    // its 8003a0d8 opening-line selection, which also hands out orbs and
    // talismans for finished tasks). Returns the line to show.
    std::optional<unsigned> begin_dialogue(Game& game,unsigned object);
    // A conversation a script started itself (8003a98c + 80015be4): (object, line).
    std::optional<std::pair<unsigned,unsigned>> take_script_dialogue(){auto r=script_dialogue_;script_dialogue_.reset();return r;}
    // Player finished the shown line (answer -1) or chose an answer (0 yes, 1 no):
    // 8003a568 advances the conversation (Moneybags payments included). The
    // next line, or nothing when the conversation ends (rewards are granted).
    std::optional<unsigned> advance_dialogue(Game& game,int answer);
    void end_dialogue(Game& game);
    bool dialogue_bridge()const{return booted_ && vm_!=nullptr && !failed_;}
    // Persistent script state of this level visit, and records to reapply at boot.
    const std::vector<RetailMemory>& memory()const{return memory_;}
    void restore_memory(std::vector<RetailMemory> records);
    unsigned orbs_awarded{},talismans_awarded{},dialogues{},bosses_defeated{};
    // A boss's death sequence asked for its outro cutscene (80019230: Crush 7,
    // Gulp 11). The engine does not play the cinematic; this is the scene id
    // the original continues in, for the engine to travel to.
    std::optional<unsigned> take_cutscene_scene(){auto r=cutscene_scene_;cutscene_scene_.reset();return r;}
    // 8004c1ac: a script commits the checkpoint it just stored (8004c0c8) and
    // restarts the level there - 80017164 sets game state 3, and 8001722c
    // fades to black (+10 per frame), reloads the level at the checkpoint
    // with its persistent state, and fades back in before game state 0.
    // Magma Cone's host (13B) does it once the end whirlwind is up, then talks
    // by itself; Colossus 10F, Sunny Beach BB, Zephyr C5, Scorch 1B9, Mystic
    // Marsh 25C and Dragon Shores call it too. The engine keeps the level
    // (the persistent bytes are what the reload would reapply) and places
    // Spyro at the checkpoint without taking a life.
    // Spyro's controls are held (8006a1fc 10000002) while the level restarts
    // or a script places him (mode 11).
    bool restarting()const{return restart_.stage!=0 || place_.active;}
    // The screen fade (80066f68): a checkpoint restart, or a camera path's
    // fade out / in (mode 10 substates 0, 1, 80, 81).
    float screen_fade()const{return std::max({restart_.fade,shot_.active?shot_.fade:0.0f,place_.fade});}
    unsigned checkpoint_restarts{},reloads{};
    // The level's challenge HUD (timers, goal and item counters): its overlay
    // hooks 80067154 (update) and 80067158 (draw) run each script tick with the
    // resident counter code; the sprites they draw, for the engine to present.
    const std::vector<RetailHudDraw>& hud_draws()const{return hud_draws_;}
    unsigned hud_ticks{};
    unsigned script_bolts{},script_sparkles{};   // diagnostics: bolt records drawn, type-0C sparkles emitted
    unsigned cutscene_requests{},last_cutscene=UINT32_MAX;
    // Diagnostics: read the level's RAM (tests and probes).
    std::uint32_t peek(std::uint32_t address,unsigned size=4)const;
    void poke(std::uint32_t address,std::uint32_t value,unsigned size=4);   // tests: set up a RAM condition
    std::uint32_t moby_address(unsigned object)const{return mobys_+object*88;}
    // 80016328 update 140 of a reward: 8003cafc places Zoe beside Spyro, from
    // the reward's camera side (80066f5c: 3 orb, 4 talisman).
    void zoe_reward(unsigned side);
    std::uint32_t reward_speaker_=0;   // the last reward's speaker moby (80066f78 at the time)
    // Diagnostics (also reported by the workbench JSON report).
    std::uint64_t instructions{};
    unsigned script_ticks{},hits{},spawned{},removed{},gems{},faults{},camera_shots{},warps{},checkpoints{};
    unsigned rejected_dialogues{},rejected_speaker=UINT32_MAX,rejected_line{};   // script conversations the engine could not present
    unsigned solid_mobys{},solid_triangles{},solid_contacts{},solid_rides{};   // collision-bank triangles (Level::object_triangles)
    unsigned terrain_changes{};   // script ticks that re-posed scripted terrain animation (doors, gates, bridges)
    unsigned door_commands{};     // terrain group commands issued for native classes (Moneybags, orb doors, containers)
    unsigned whirlwind_kinds{};   // native whirlwinds whose kind (header +08) a script changed (Cloud Temples' Murgen)
    unsigned grid_repairs{};      // mobys put back into their grid cell list before 80041538 walked it
    unsigned pickups{},spits{};   // mobys Spyro took into his mouth / spat out (Hurricos' diodes)
    std::uint32_t attacks_seen{};   // OR of engine attack bits delivered to script mobys
    std::string fault;
    std::vector<unsigned> classes;   // owned classes, sorted
    std::vector<unsigned> invalid_animation;   // classes whose RAM animation state did not fit the native bank
private:
    std::shared_ptr<OverlayVm> vm_;
    bool booted_=false,failed_=false;
    std::uint32_t mobys_{},dispatcher_{},initial_end_{};
    unsigned initial_{},clock_{};
    std::vector<unsigned> owned_;           // engine indices of initial owned mobys (== RAM index)
    std::vector<std::uint8_t> owned_mask_;  // per RAM index of the initial array
    std::optional<unsigned> cutscene_scene_;
    unsigned gems_written_{};   // 800670cc as last written from Spyro's counter
    std::vector<unsigned> slots_;           // spawned RAM slot (from initial_) -> engine index, or UINT32_MAX
    std::vector<Vec3> written_;             // last position mirrored per engine index
    Game* current_=nullptr;                 // valid while the interpreter runs a tick
    std::vector<Vec3> flames_;              // engine flame points awaiting the retail flame query
    unsigned flame_step_{};
    std::uint32_t support_{},contact_{};    // script moby Spyro stood on / touched since the last script tick
    Vec3 support_position_;
    // Solid mobys: collision-bank triangles (8003dc64's moby half) rebuilt
    // into Level::object_triangles after every script tick.
    void build_solids(Game& game);
    void sync_terrain(Game& game);
    // Terrain groups that native classes' original handlers drive (Moneybags
    // doors, orb doors, containers opening passages): their handlers never
    // run in the interpreter, so their 8002365c/80023710 calls are issued here.
    void native_doors(Game& game);
    void terrain_command(std::uint32_t function,std::uint32_t group,std::uint32_t a,std::uint32_t b);
    struct NativeDoor {unsigned object{},stage{},timer{};bool paid=false;};
    std::vector<NativeDoor> native_doors_;bool doors_started_=false;
    void note_solids(Game& game,Vec3 center,float radius,std::uint32_t attack);
    std::vector<std::uint8_t> solid_flags_;            // per object triangle: retail flag byte (1..7F hurt via 80064668)
    std::map<std::uint32_t,std::uint32_t> touched_;    // moby -> Spyro's attack bits while touching its triangles
    std::uint32_t standing_{},triangle_hurt_{};        // 8006a134 (the moby Spyro stands on) and flagged-triangle damage
    struct Ride {std::uint32_t moby{},offset{};float u{},v{};Vec3 point;bool active=false;} ride_;
    std::uint32_t pad_held_{},pad_pressed_{},pad_previous_{};
    std::uint32_t dialogue_moby_{};          // 80066f78 while a conversation runs
    std::optional<std::pair<unsigned,unsigned>> script_dialogue_;
    // Script camera shots. The retail camera update (800210e8 ...) is not run;
    // the requests 8001f24c (mode 9 look), 8001f4a4 (mode 10 focus a moby),
    // 8001f2e4 (end) and 8001f524 (mode 11 place Spyro) run natively and the
    // director below advances 80067ed0/80067ed4 the way that update does,
    // while the engine camera presents the shot.
    struct Shot {std::uint32_t mode{},moby{},normal_mode{};Vec3 eye,target;unsigned ticks{},end_ticks{};bool active=false;
        Vec3 from_eye,from_forward;float fade{};};   // mode 10: the view it blends from (flag 2), and the fade 80066f68 / 255
    // Mode 10 (8001f4a4): the level's camera path (80068028: count, then
    // 12-byte keys) played by the retail camera update 8001fa58.
    void play_path(Game& game,OverlayVm& vm);
    Shot shot_;std::optional<Shot> requested_shot_;
    unsigned mode_ticks_{};   // other requested modes (8001f1e4: arenas, fixed views) settling
    std::optional<std::pair<Vec3,unsigned>> warp_;   // 8001f524: Spyro's origin, 0..255 heading
    struct Place {bool active=false,placed=false;unsigned timer{};float fade{};} place_;   // mode 11's fade out, placement and fade in
    // 8004b120's bolt table (8006b400, eight 0x30-byte records): the bend words
    // of each live record, re-rolled as 8004b194 does; presented natively.
    std::array<std::array<std::uint16_t,3>,8> bolt_words_{};std::array<std::uint8_t,8> bolt_live_{};std::uint32_t bolt_seed_=0x2b1d3e5u;
    void present_bolts(Game& game,OverlayVm& vm);
    bool advance_place(Game& game,OverlayVm& vm);
    std::array<std::uint32_t,5> checkpoint_seen_{};   // 80067414 as last adopted
    struct Restart {unsigned stage{},ticks{};float fade{};} restart_;   // game state 3 (8001722c): 1 fade out, 2 reload, 3 fade in
    void advance_restart(Game& game,OverlayVm& vm);
    void reload_mobys(Game& game,OverlayVm& vm);   // 8001722c's reload: the gameplay loader runs again
    // Native mobys' +48 as last written into RAM: a script that writes another
    // value (Magma Cone's 401 starting its whirlwind) changed the native state.
    std::vector<std::int16_t> native_states_;
    std::uint32_t shake_left_{};   // 8006805c as last counted down
    std::vector<RetailHudDraw> hud_draws_,hud_pending_;std::vector<std::uint32_t> hud_prims_;
    bool hud_capturing_=false,hud_failed_=false;
    void run_hud(Game& game,OverlayVm& vm);
    void direct_camera(Game& game,OverlayVm& vm);
    void carry(Game& game,std::uint32_t pressed);
    void present_carried(Game& game);
    std::vector<RetailMemory> memory_,pending_memory_;
    void boot(Game& game);
    void sync_progress_in(Game& game);
    void sync_progress_out(Game& game);
    void capture_memory();
    std::optional<unsigned> dialogue_line();
    void build_active(OverlayVm& vm);
    void sync_in(Game& game);
    void sync_out(Game& game);
    void mirror(Game& game,RetailObject& object,std::uint32_t moby,bool fresh);
};
} // namespace spyro
