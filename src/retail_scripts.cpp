#include <cstdio>
#include "spyro/retail_scripts.hpp"
#include "spyro/game_build.hpp"
#include "spyro/overlay_vm.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <set>

namespace spyro {
namespace {
constexpr std::uint32_t moby_size=88;
constexpr std::uint32_t level_id_global=0x80066f90,moby_array=0x80066f14,moby_end=0x80066fd4,initial_end_global=0x800670bc,
    dispatcher_hook=0x800670f0,particle_hook=0x800670fc,grid_global=0x80067174,groups_global=0x800670e0,group_flags=0x800682b8,
    active_list=0x800685f0,spyro_origin=0x80069ff0,spyro_heading=0x8006a05c,spyro_mode=0x8006a048,spyro_health=0x8006a248,
    spyro_invulnerable=0x8006a24c,elapsed_ticks=0x80067018,hit_moby=0x8006aba0,spyro_hurt=0x8006a014,flame_probe=0x807ff900,player_probe=0x807ff920,contact_point=0x8006ab7c,spyro_contact=0x8006a130,spyro_state=0x8006a040;
constexpr std::uint32_t loader_hooks=0x8006d960,scene_relocate=0x8006fa98,model_relocate=0x80070018,gameplay_loader=0x8001c734,
    anim_clock=0x80041068,active_builder=0x80040e94,remove_moby=0x8004132c,update_moby=0x80041538,award_spirit=0x8004c3d4,moby_query=0x8004055c,
    object_sound=0x80050648,rand_function=0x80059d2c,overlay_base=0x8006d264;
// Conversations and progression (resident globals, see docs/RETAIL_SCRIPTS.md).
constexpr std::uint32_t game_state=0x800681c8,dialogue_moby=0x80066f78,dialogue_clip_a=0x800670dc,dialogue_clip_b=0x80067120,
    pad_held=0x800683a0,pad_pressed=0x800683a4,pad_released=0x800683a8,level_index=0x80066f54,orb_table=0x80069fd0,
    orb_total=0x8006702c,talisman_table=0x8006b264,talisman_total=0x80067108,gem_total=0x800670cc,purchase_table=0x80064670,
    dialogue_answer=0x80066f48,dialogue_question=0x8006713c,dialogue_next=0x80066fe8,dialogue_reward=0x80066f94,
    dialogue_zoe=0x80066fbc,dialogue_awarded=0x80066ff0,perfect_masks=0x80066e70,dialogue_running=0x80067100,
    // The level's talisman speaker: a speaker class sets 80066f44 when Spyro
    // talks to a moby whose header kind byte is 1 (Glimmer class 1CC,
    // 8007d8xx, after 8003a98c). Line command 1 (8003a0d8) then chooses the
    // talisman line and the reward class; 80016328 state 4 gives it.
    talisman_speaker=0x80066f44;
constexpr std::uint32_t start_dialogue=0x80015be4,advance_line=0x8003a568,zoe_appears=0x8003cafc,close_dialogue=0x800161c8,
    persist=0x8004c214,start_cutscene=0x80019230,cutscene_levels=0x8006323c;
// Camera struct 80067ed0: mode, substate (+4), shot timer (+8); requested mode 80067fec.
constexpr std::uint32_t camera_mode=0x80067ed0,camera_substate=0x80067ed4,camera_timer=0x80067ed8,camera_request=0x80067fec,
    camera_look=0x8001f24c,camera_focus=0x8001f4a4,camera_place=0x8001f524,camera_eye=0x80067eac;
// Solid mobys: class model table, animation blend weights (+40 index),
// collision-triangle damage words, and the moby under Spyro (8002cc88).
constexpr std::uint32_t class_table=0x80068c94,blend_weights=0x80062868,triangle_damage=0x80064668,standing_moby=0x8006a134,airborne_frames=0x8006a0b0;
// Terrain animation: 800225f8(elapsed VBlanks) advances the seven timeline
// directories {count, record list} at 800681d4 (main loop, never a script).
constexpr std::uint32_t terrain_timeline=0x800225f8,terrain_directories=0x800681d4;
// 8001c734 stores the file's seven animation sections out of order:
// section k lands in RAM directory terrain_directory[k].
constexpr std::array<unsigned,7> terrain_directory{0,1,4,2,6,5,3};
// Spyro's head state machine (8002fc20, jump table 80010b0c): head state
// 8006a15c, its frame timer 8006a164, the carried moby 8006a138, the per-frame
// request word 8006a1fc (80031450 copies it to 8006a200 and clears it) and the
// requesting moby 8006a208. 80061370 holds four bytes per Spyro state: may
// flame, may pick up, may keep carrying, may spit.
constexpr std::uint32_t head_state_address=0x8006a15c,head_timer=0x8006a164,carried_moby=0x8006a138,spyro_request=0x8006a1fc,
    request_seen=0x8006a200,request_moby=0x8006a208,state_abilities=0x80061370;
// Frames since the level loaded: 8001c734 zeroes 80067008 and the main loop
// (800159fc) adds one per game frame. Scripts compare it with 60 to finish
// events silently at load instead of filming them (Hurricos' power bolt).
constexpr std::uint32_t level_frames=0x80067008;
// Camera shake request: strength 80068058, frames left 8006805c (the camera update counts it down).
constexpr std::uint32_t camera_shake_strength=0x80068058,camera_shake_ticks=0x8006805c;
constexpr std::array<std::uint32_t,2> memory_lists{0x8006718c,0x80067414};
// 8004c0c8 (Zoe's checkpoint) fills 80067414: +0 valid, +24 origin (floor
// height + 164), +30 heading (12 bits); +40 snapshots the persistent list.
constexpr std::uint32_t checkpoint_record=0x80067414;
std::array<std::uint32_t,5> checkpoint_words(OverlayVm& vm) {
    const auto r=checkpoint_record;
    return {vm.read32(r),vm.read32(r+0x24),vm.read32(r+0x28),vm.read32(r+0x2c),vm.read32(r+0x30)};
}
constexpr unsigned orb_reward_class=0x71,purchase_count=19;
std::uint32_t u32(std::span<const std::uint8_t> d,std::size_t at){std::uint32_t v;std::memcpy(&v,d.data()+at,4);return v;}
std::uint16_t u16(std::span<const std::uint8_t> d,std::size_t at){std::uint16_t v;std::memcpy(&v,d.data()+at,2);return v;}
std::int32_t s32(OverlayVm& vm,std::uint32_t a){return static_cast<std::int32_t>(vm.read32(a));}
// Unlink a moby from the 32x32 spatial grid (8004132c's first half) and
// clear its collision bank so 8004055c ignores it.
void hide_from_queries(OverlayVm& vm,std::uint32_t moby) {
    const auto cell=static_cast<std::int16_t>(vm.read16(moby+0x34));
    if(cell>=0) {
        auto link=vm.read32(grid_global)+static_cast<std::uint32_t>(cell)*4-4;
        for(unsigned guard=0;guard<4096;++guard) {
            const auto next=vm.read32(link+4);
            if(!next)break;
            if(next==moby){vm.write32(link+4,vm.read32(moby+4));break;}
            link=next;
        }
    }
    vm.write16(moby+0x34,0xffff);vm.write32(moby+8,0);
}
unsigned spyro_attack(const Game& game) {
    switch(game.player.motion) {
        case Motion::Charge:case Motion::ChargeJump:return charge_attack;
        // 8006f0a0 (dive states 28/29/2A) sets 8006a128 = 20000 under water.
        case Motion::UnderwaterSwim:case Motion::UnderwaterIdle:case Motion::UnderwaterCharge:return charge_attack;
        case Motion::Supercharge:case Motion::SuperchargeJump:return 0x560000;
        case Motion::Headbash:case Motion::HeadbashLand:return headbash_attack|charge_attack;
        default:return 0;
    }
}
// 8006a048 movement mode, set by the resident state entry 800351d8 and the
// swim overlay: 0 standing, 1 moving on foot, 2 jumping, 3 falling,
// 4 charging (incl. charge jump), 5 gliding (0x780..0x1900 speed clamp),
// 6 flying, 7 hurt, 10/11/12 surface / underwater / underwater charge,
// 0F headbash. Scripts gate on it (Crush starts once Spyro moves).
// Flight is set by the level overlays' own state entries (Crystal Glacier
// 8006ed58 for states 33-35, the speedways 8006ec70): 6 while flying, 9 for
// the attacking boost (attack word 60000), which the PC flight lacks.
// Crystal Glacier's class 50 challenge (controller c6) only starts in mode 6.
unsigned spyro_movement_mode(const Game& game) {
    if(game.player.hurt_ticks && game.player.swim.mode==WaterMode::Dry)return 7;
    switch(game.player.motion) {
        case Motion::Idle:return 0;
        case Motion::Run:case Motion::Slide:case Motion::RollLeft:case Motion::RollRight:case Motion::ChargeRecoil:return 1;
        case Motion::Jump:case Motion::Whirlwind:return 2;
        case Motion::Fall:return 3;
        case Motion::Charge:case Motion::ChargeJump:case Motion::Supercharge:case Motion::SuperchargeJump:return 4;
        case Motion::Glide:case Motion::GlideBrake:case Motion::GlideBoost:case Motion::Hover:return 5;
        case Motion::Superfly:case Motion::SuperflyClimb:case Motion::SuperflyDive:return 6;
        case Motion::SurfaceSwim:case Motion::SurfaceIdle:return 10;
        case Motion::UnderwaterSwim:case Motion::UnderwaterIdle:case Motion::UnderwaterHurt:return 11;
        case Motion::UnderwaterCharge:return 12;
        case Motion::Headbash:case Motion::HeadbashLand:return 0xf;
    }
    return 0;
}
}

// 8006a040 state number (800351d8 cases): 0 idle, 1 run, 5 jump, 6 fall,
// 0C charge, 0D charge bonk, 0F hurt, 10 glide, 15 charge jump, 2E headbash,
// 2C surface swim, 2A underwater, 39 underwater hurt.
static unsigned spyro_state_number(const Game& game) {
    const bool underwater=game.player.swim.mode==WaterMode::Underwater;
    if(game.player.hurt_ticks)return underwater?0x39:0xf;
    switch(game.player.motion) {
        case Motion::Idle:return 0;
        case Motion::Run:case Motion::Slide:case Motion::RollLeft:case Motion::RollRight:return 1;
        case Motion::Jump:case Motion::Whirlwind:return 5;
        case Motion::Fall:return 6;
        case Motion::Charge:case Motion::Supercharge:return 0xc;
        case Motion::ChargeRecoil:return 0xd;
        case Motion::ChargeJump:case Motion::SuperchargeJump:return 0x15;
        case Motion::Glide:case Motion::GlideBrake:case Motion::GlideBoost:case Motion::Hover:return 0x10;
        case Motion::Headbash:case Motion::HeadbashLand:return 0x2e;
        case Motion::SurfaceSwim:case Motion::SurfaceIdle:return 0x2c;
        case Motion::UnderwaterSwim:case Motion::UnderwaterIdle:case Motion::UnderwaterCharge:case Motion::UnderwaterHurt:return 0x2a;
        default:return 0;
    }
}
unsigned retail_level_id(unsigned slot) {
    static const std::map<unsigned,unsigned> ids{{15,10},{17,11},{19,12},{21,13},{23,21},{25,22},{27,23},{29,25},{31,26},{33,30},{35,31},{37,32},
        {39,33},{41,34},{43,35},{45,41},{47,42},{49,43},{51,44},{53,45},{55,46},{57,50},{59,51},{61,52},{63,55},{65,61},{67,62},{69,65},{71,66}};
    const auto found=ids.find(slot);return found==ids.end()?0:found->second;
}
std::shared_ptr<const RetailScriptImage> make_retail_script_image(WadArchive& wad,std::span<const std::uint8_t> executable,unsigned slot) {
    // This bridge uses the verified Spyro 2 US resident globals, loader
    // callbacks and 88-byte moby records. A different game's retail build is
    // not compatible merely because it is not a prerelease.
    if(game_profile(executable).build!=GameBuild::RetailUS)return {};
    const auto id=retail_level_id(slot);
    if(!id)return {};
    auto image=std::make_shared<RetailScriptImage>();
    image->executable.assign(executable.begin(),executable.end());
    image->loader=wad.read(1);image->overlay=wad.read(slot-1);image->archive=wad.read(slot);
    image->slot=slot;image->level_id=id;
    return image;
}
bool native_retail_class(unsigned type) {
    // Classes with native reconstructions (see the type tests across src/):
    // gems, containers and fragments, Sparx and his effects, butterflies,
    // power gates, portal labels, translated enemies and level mechanisms.
    static const std::set<unsigned> native{0x01,0x10,0x2c,0x30,0x37,0x51,0x58,0x64,0x71,0x73,0x77,0x78,0xaa,
        0xc8,0xc9,0xca,0xcb,0xd1,0xd3,0x104,0x116,0x12c,0x132,0x133,0x134,0x136,0x142,0x143,0x144,0x164,0x166,0x169,0x16a,
        0x16c,0x16d,0x16f,0x17b,0x180,0x1aa,0x1b6,0x1bc,0x1ea,0x1ee,0x1f0,0x1f2,0x1f3,0x1fc,0x1fd,0x205,0x206,0x208,0x220,
        0x229,0x22b,0x23b,0x25e,0x25f,0x261,0x263,0x264,0x265,0x26f,0x271,0x281,0x282,0x28b,0x28d,0x290,0x2ac,0x2ad,0x2af,0x2b0,
        0x2c5,0x2c6,0x2c7,0x2c8,0x400,0x405,0x40a};
    return native.contains(type) || (type>=0x1cc && type<=0x1d3);
}
bool level_script_class(unsigned slot,unsigned type) {
    // Cloud Temples' trolls (1EE, overlay 60 800750ac) walk their routes once
    // the freeze gate (header +2C) is open; the freeze shot's 01000000 turns
    // one into an ice block (271, spawned at 8007520c) that follows it, solid
    // through its collision bank, for 300 updates; a charge bonk (state D) or
    // 80000 breaks it into 2A5 shards. Murgen's third bell is reached from it.
    return slot==61 && (type==0x1ee || type==0x271);
}
bool challenge_giver(unsigned slot,unsigned type,int kind) {
    // (WAD slot, class). Colossus 10F: the monks, Brother Kipp's hockey (kind 5
    // against the keeper, kind 8 one on one; the puck is a 153 he spawns).
    // Glimmer 22A: the gem cutters, Kanga (kind 8) and Roo (kind 9) light-the-
    // lamps (their big lamp 40C at header +34).
    // Colossus 193: the Professor's ten statues (1E4). Metropolis' Inventor
    // Droid (251) already runs its code (it has no native conversation).
    // Sunny Beach B6: the turtle advisors; Gromit (kind 3) counts the turtle
    // boxes (group 1) and talks by himself when none is left.
    // Sunny Beach BB: the Master Chef's turtle soup. Skelos Badlands 62:
    // Lumpy's lava lizards, Ooga's bones. Shady Oasis 188: the hippos (Bruno
    // counts the lamp thieves in 80066fec). Mystic Marsh 1FE: Basil (the four
    // spark plug thieves, 1A, count into 80066fec). Robotica Farms 10D: the
    // farmers (Applebee, kind 2, waits for the four crow bugs 17A in state 63).
    // Cloud Temples' 1C7: only Murgen (kind 3, the bells; 80077f14). Bartle
    // (kind 4, the doors and the completion orb) stays a native speaker:
    // running his code stopped his completion talk in the level-exit check.
    static const std::set<std::pair<unsigned,unsigned>> givers{{21,0x10f},{21,0x193},{17,0x22a},{27,0xb6},{27,0xbb},{35,0x62},{47,0x188},{59,0x1fe},{65,0x10d}};
    static const std::map<std::pair<unsigned,unsigned>,int> one_kind{{{61,0x1c7},3}};
    if(const auto only=one_kind.find({slot,type});only!=one_kind.end())return kind==only->second;
    return givers.contains({slot,type});
}
RetailBoot boot_retail_level(OverlayVm& vm,const RetailScriptImage& image) {
    RetailBoot boot;
    const auto& exe=image.executable;
    if(exe.size()<0x800)return {.fault="executable too small"};
    if(game_profile(exe).build!=GameBuild::RetailUS)
        return {.fault="retail script bridge requires the verified Spyro 2 US resident map"};
    const auto t_addr=u32(exe,0x18),t_size=u32(exe,0x1c);
    vm.load(t_addr,std::span(exe).subspan(0x800,std::min<std::size_t>(t_size,exe.size()-0x800)));
    // Hardware/library code (GPU, SPU, CD, pad) and the sound wrappers never
    // execute; the C library rand() does.
    for(std::uint32_t a=0x80050000;a<0x8005f000;a+=4)if(a<rand_function || a>=rand_function+0x30)vm.guard[a]=true;
    vm.write32(level_id_global,image.level_id);
    vm.write32(0x80066f18,retail_particle_base);
    vm.r[29]=retail_stack_top;
    vm.load(overlay_base,image.loader);vm.call(loader_hooks);
    if(!vm.fault.empty())return {.fault="hook installer: "+vm.fault};
    const auto entries=parse_level_archive(image.archive);
    if(entries.size()!=4)return {.fault="level archive layout"};
    const auto archive=std::span<const std::uint8_t>(image.archive);
    vm.load(retail_scene_base,archive.subspan(entries[1].offset,entries[1].size));
    vm.call(scene_relocate,std::array<std::uint32_t,1>{retail_scene_base});
    if(!vm.fault.empty())return {.fault="scene relocation: "+vm.fault};
    // 80070398: model offsets at +40 pair with class IDs at +140. Index zero
    // is Spyro's extra animation bank.
    const auto& models=entries[2];
    vm.load(retail_model_base,archive.subspan(models.offset,models.size));
    const auto directory=level_model_directory(archive);
    for(unsigned i=1;i<63;++i) {
        const auto begin=u32(archive,directory+i*4),end=u32(archive,directory+(i+1)*4);
        if(!end)break;
        if(begin<models.offset || end>models.offset+models.size || end<=begin)continue;
        const unsigned type=u16(archive,directory+0x100+i*2);
        if(type>=0x300)continue;
        const auto base=retail_model_base+(begin-models.offset);
        vm.call(model_relocate,std::array<std::uint32_t,3>{base,type,retail_model_base+(end-models.offset)});
        if(!vm.fault.empty())return {.fault="model relocation: "+vm.fault};
    }
    vm.load(overlay_base,image.overlay);
    const auto& gameplay=entries[3];
    vm.load(retail_gameplay_base,archive.subspan(gameplay.offset,gameplay.size));
    // The loader runs the dispatcher once; its active list must be empty.
    vm.hle[active_builder]=[](OverlayVm& v){v.write32(active_list,0);};
    vm.call(gameplay_loader,std::array<std::uint32_t,1>{retail_gameplay_base});
    vm.hle.erase(active_builder);
    if(!vm.fault.empty())return {.fault="gameplay loader: "+vm.fault};
    boot.mobys=vm.read32(moby_array);boot.dispatcher=vm.read32(dispatcher_hook);
    for(std::uint32_t m=boot.mobys;m<0x80800000 && boot.count<10000;m+=moby_size,++boot.count)
        if(static_cast<std::int8_t>(vm.read8(m+0x48))==-1 && m>=vm.read32(initial_end_global))break;
    if(!boot.dispatcher)boot.fault="no dispatcher";
    return boot;
}

void RetailScripts::boot(Game& game) {
    booted_=true;
    const auto& image=game.level.script_image;
    // Ripto's arena keeps its verified native encounter (ripto.cpp).
    if(!image || !game.object_models() || game.level.wad_slot!=image->slot || image->slot==71 || game.level.ripto_arena){failed_=true;return;}
    auto vm=std::make_shared<OverlayVm>();
    const auto boot=boot_retail_level(*vm,*image);
    if(!boot.fault.empty()){fault=boot.fault;failed_=true;return;}
    mobys_=boot.mobys;dispatcher_=boot.dispatcher;initial_=boot.count;initial_end_=vm->read32(initial_end_global);
    if(initial_!=game.level.objects.size() || initial_>game.retail_objects.size()) {
        fault="moby count "+std::to_string(initial_)+" != "+std::to_string(game.level.objects.size());failed_=true;return;
    }
    std::vector<bool> reserved(initial_);
    const auto reserve=[&](unsigned index){if(index<initial_)reserved[index]=true;};
    // The level's talisman speaker (a conversation with a command-1 line)
    // runs its own class code: that code arms the talisman line (80066f44)
    // on its own condition - a kind byte of 1, 2, 3 or 5, a finished task
    // (Aquaria Towers' linked moby), or an end-of-level event that makes it
    // talk by itself (+5 = 2, Colossus 11A) - before it opens the
    // conversation through 80015be4. Breeze Harbor's Squawk (180) keeps his
    // native engine quest.
    const auto talisman_speaker=[&](const NpcDefinition& npc){
        return npc.object<initial_ && !native_retail_class(npc.type) &&
            std::any_of(npc.lines.begin(),npc.lines.end(),[](const DialogueLine& l){return l.talisman>0;});};
    // Quest speakers whose own code moves the level on also run it. Fracture
    // Hills' six satyrs (class 8A, 80079484) count the ones Spyro has found
    // (the word at header +20; 80066fec = 6 - count) and raise the end
    // plateau a terrain group at a time (80023710(count)), then show its floor
    // (8002365c(6, FC): region 82 under Return Home, hidden at node 0). As
    // native speakers they talked but never ran it, so that floor stayed
    // undrawn over its collision and Spyro stood in mid-air at the end.
    std::set<unsigned> extra_speakers;   // diagnostics: SPYRO_QUEST_SPEAKERS=19,24 (records that run their own class code)
    if(const char* list=std::getenv("SPYRO_QUEST_SPEAKERS"))for(const char* p=list;*p;){char* e=nullptr;const auto v=std::strtoul(p,&e,10);if(e==p)break;extra_speakers.insert(static_cast<unsigned>(v));p=*e==','?e+1:e;}
    // Orb challenge givers run their class code too: it starts the challenge
    // after the talk, spawns and referees it, and talks by itself or waits for
    // Spyro to come back for the reward line (docs/ORB_CHALLENGES.md).
    const auto kind_byte=[&](unsigned object)->int{
        const auto header=vm->read32(mobys_+object*moby_size);
        return header>=0x80000000u && header<0x80800000u?static_cast<int>(vm->read8(header)):-1;
    };
    const auto quest_speaker=[&](const NpcDefinition& npc){return npc.object<initial_ && ((image->slot==51 && npc.type==0x8a) ||
        challenge_giver(image->slot,npc.type,kind_byte(npc.object)) || extra_speakers.contains(npc.object));};
    if(std::erase_if(game.level.npcs,[&](const NpcDefinition& n){return talisman_speaker(n) || quest_speaker(n);}))game.dialogue.reset(game.level);
    for(const auto& npc:game.level.npcs)reserve(npc.object);
    for(const auto& portal:game.level.portals){reserve(portal.object);reserve(portal.gate_object);}
    for(const auto& wind:game.level.whirlwinds)reserve(wind.object);
    for(const auto& transport:game.level.home_transports){reserve(transport.object);reserve(transport.label_object);}
    for(const auto& script:game.level.scripts)reserve(script.object);
    owned_mask_.assign(initial_,0);
    std::set<unsigned> kinds;
    for(unsigned i=0;i<initial_;++i) {
        auto& o=game.retail_objects[i];const auto m=mobys_+i*moby_size;
        // Every class without a native reconstruction runs its original case;
        // dispatchers use both beq trees and jump tables (Skelos Badlands).
        const bool level_class=level_script_class(image->slot,o.type);
        const bool own=(!native_retail_class(o.type) || level_class) && !reserved[i] && !o.enemy && !o.boss_body &&
            !o.powerup_gate && !o.container && !o.butterfly && (!o.scripted_contact || level_class) && !o.pc_actor && vm->read16(m+0x36)==o.type;
        if(!own){hide_from_queries(*vm,m);continue;}
        if(level_class)o.scripted_contact=false;   // its own case takes the queries' attack bits
        owned_mask_[i]=1;owned_.push_back(i);kinds.insert(o.type);o.retail_script=true;
        // Removed by a save or by the campaign before the first tick.
        if((o.collected || o.state<0) && static_cast<std::int8_t>(vm->read8(m+0x48))>=0)vm->call(remove_moby,std::array<std::uint32_t,1>{m});
    }
    classes.assign(kinds.begin(),kinds.end());
    // Native mobys' +48 as the loader left it, before a save's records apply.
    native_states_.assign(initial_,-1);
    for(unsigned i=0;i<initial_;++i)if(!owned_mask_[i])native_states_[i]=vm->read8(mobys_+i*moby_size+0x48);
    checkpoint_seen_=checkpoint_words(*vm);   // the loader's own record is the level start
    if(owned_.empty()){failed_=true;return;}
    // Script sound requests become engine object sounds; frame cues already
    // come from the engine's generic animation-event path (80041068 caller).
    vm->hle[object_sound]=[this](OverlayVm& v) {
        if(v.r[31]==0x80041264 || !current_)return;
        const auto moby=v.arg(0),id=v.arg(1);
        if(id>=128 || moby<0x80000000)return;
        const Vec3 position=retail::to_world({s32(v,moby+0xc),s32(v,moby+0x10),s32(v,moby+0x14)});
        if(current_->sound_events.size()<256)current_->sound_events.push_back({SoundScope::Object,id,v.read16(moby+0x36),position});
    };
    vm->hle[active_builder]=[this](OverlayVm& v){build_active(v);};
    // 80019230(n) starts cutscene n: game state 5 and level id 8006323c[n].
    // Crush's and Gulp's death sequences call it (7 and 11, overlays 30/54)
    // once the talisman-table byte is set. Running it would rewrite the level
    // id the remaining scripts compare (class 1A4 tests 26/46), so the engine
    // only records the request. The cinematic scenes (74, 77) are not played;
    // the travel goes where the original continues: 80019230 itself swaps 7
    // for 9 and 11 for 13 (Autumn Plains 30, Winter Tundra 50) once that home
    // has been visited. The homes' own requests (transporters 5/9/13, 14)
    // run unchanged, as before.
    vm->hle[start_cutscene]=[this](OverlayVm& v) {
        const auto n=v.r[4];last_cutscene=n;++cutscene_requests;
        if(n!=7 && n!=11){v.passthrough=true;return;}
        v.r[2]=0;
        if(const auto scene=v.read8(cutscene_levels+n+2);scene && scene<70)cutscene_scene_=scene;
    };
    // 8004132c walks the moby's grid cell list until it finds the moby and
    // never ends when it is not there (a second removal, a stale cell). Make
    // sure it is found, or unlink it wherever it is and skip the walk.
    vm->hle[remove_moby]=[](OverlayVm& v) {
        const auto m=v.arg(0);v.passthrough=true;
        if(m<0x80000000u || m>=0x80800000u)return;
        const auto cell=static_cast<std::int16_t>(v.read16(m+0x34));
        if(cell<0)return;
        const auto grid=v.read32(grid_global);
        const auto find=[&](std::uint32_t head_link,bool unlink) {
            auto link=head_link;
            for(unsigned guard=0;guard<4096;++guard) {
                const auto next=v.read32(link+4);
                if(!next || next<0x80000000u || next>=0x80800000u)return false;
                if(next==m){if(unlink)v.write32(link+4,v.read32(m+4));return true;}
                link=next;
            }
            return false;
        };
        if(find(grid+static_cast<std::uint32_t>(cell)*4-4,false))return;
        for(std::uint32_t c=0;c<1024;++c)if(find(grid+c*4-4,true))break;
        v.write16(m+0x34,0xffff);
    };
    // 80041538 (a1 bit 2) moves a moby to the grid cell of its position: it
    // walks its old cell's list (+34) to unlink it, endlessly when the moby is
    // not there. Metropolis loses one after scripts are prodded out of order.
    // Before a cell change, make sure the old list holds it: unlink it wherever
    // it is and push it onto that list, so the original walk finds it.
    vm->hle[update_moby]=[this](OverlayVm& v) {
        v.passthrough=true;
        const auto m=v.arg(0);
        if(!(v.arg(1)&2) || m<0x80000000u || m>=0x80800000u)return;
        const auto cell=static_cast<std::int16_t>(v.read16(m+0x34));
        if(cell<0)return;
        const auto fresh=(v.arg(1)&8)?0u:(v.read32(m+0xc)>>13)+((v.read32(m+0x10)>>13)<<5);
        if(fresh==static_cast<std::uint32_t>(cell))return;
        const auto grid=v.read32(grid_global);
        const auto find=[&](std::uint32_t head_link,bool unlink) {
            auto link=head_link;
            for(unsigned guard=0;guard<4096;++guard) {
                const auto next=v.read32(link+4);
                if(!next || next<0x80000000u || next>=0x80800000u)return false;
                if(next==m){if(unlink)v.write32(link+4,v.read32(m+4));return true;}
                link=next;
            }
            return false;
        };
        const auto head=grid+static_cast<std::uint32_t>(cell)*4;
        if(find(head-4,false))return;
        for(std::uint32_t c=0;c<1024;++c)if(find(grid+c*4-4,true))break;
        v.write32(m+4,v.read32(head));v.write32(head,m);++grid_repairs;
    };
    // Shady Oasis: a 187 joins the list on its owner (a2) at 8007ca78: own
    // param+14 = owner header+8 (old head), head = itself, count +4 plus one.
    // A 233 that is hit resets the 187 it holds to state 0 (80076f4c) without
    // the state-99 unlink, so the 187 would join again and close the list into a
    // cycle that 187's and 40f's walks (8007caf4, 80081634) never leave. Unlink
    // it first; the original join then runs unchanged.
    if(image->slot==47)vm->hle[0x8007ca78]=[](OverlayVm& v) {
        v.passthrough=true;
        const auto self=v.r[20],owner=v.r[6];
        const auto valid=[](std::uint32_t a){return a>=0x80000000u && a<0x80800000u;};
        if(!valid(self) || !valid(owner) || !valid(v.read32(owner)))return;
        const auto header=v.read32(owner);
        std::uint32_t previous=0,at=v.read32(header+8);
        for(unsigned guard=0;guard<256 && valid(at) && valid(v.read32(at));++guard) {
            const auto next=v.read32(v.read32(at)+0x14);
            if(at==self) {
                if(previous)v.write32(v.read32(previous)+0x14,next);else v.write32(header+8,next);
                if(static_cast<std::int32_t>(v.read32(header+4))>0)v.write32(header+4,v.read32(header+4)-1);
                return;
            }
            previous=at;at=next;
        }
    };
    // Diagnostics: SPYRO_WATCH=804072fc reports every write to that byte (with
    // the writing instruction); SPYRO_TRACE_PC=8007ca90,80081620 prints the
    // registers each time those instructions run (in any code), then runs them.
    if(const char* watch=std::getenv("SPYRO_WATCH"))vm->watch=static_cast<std::uint32_t>(std::strtoul(watch,nullptr,16));
    if(const char* trace=std::getenv("SPYRO_TRACE_PC"))
        for(const char* at=trace;*at;) {
            char* end=nullptr;const auto pc=static_cast<std::uint32_t>(std::strtoul(at,&end,16));
            if(end==at)break;
            // Chains to a hook already on that address (e.g. the 8004132c guard).
            const auto previous=vm->hle.contains(pc)?vm->hle[pc]:nullptr;
            vm->hle[pc]=[this,pc,previous](OverlayVm& v) {
                std::fprintf(stderr,"trace %08x tick %u ra %08x v0 %08x v1 %08x a0 %08x a1 %08x a2 %08x a3 %08x s0 %08x s1 %08x s2 %08x s3 %08x s4 %08x\n",
                    pc,script_ticks,v.r[31],v.r[2],v.r[3],v.r[4],v.r[5],v.r[6],v.r[7],v.r[16],v.r[17],v.r[18],v.r[19],v.r[20]);
                for(const auto& [name,m]:{std::pair{"s4",v.r[20]},std::pair{"a0",v.r[4]}})
                    if(m>=0x80000000u && m<0x80800000u)std::fprintf(stderr,"      %s class %x state %02x\n",name,v.read16(m+0x36),v.read8(m+0x48));
                if(previous)previous(v);else v.passthrough=true;
            };
            at=*end==','?end+1:end;
        }
    // Spirit awards: observed, then the original runs. Some scripts award their
    // own spirit and stay in the scene (Colossus 11a ends in state 100), so the
    // engine counts the award itself, not only retirement.
    vm->hle[award_spirit]=[this](OverlayVm& v) {
        const auto m=v.arg(0);v.passthrough=true;
        if(!current_ || m<mobys_ || m>=initial_end_ || (m-mobys_)%moby_size)return;
        const auto index=(m-mobys_)/moby_size;
        if(index<current_->retail_objects.size())current_->retail_objects[index].spirit_awarded=true;
    };
    // Camera requests: observed, then the original code runs (RAM stays exact).
    const auto vec=[](OverlayVm& v,std::uint32_t a){return retail::to_world({s32(v,a),s32(v,a+4),s32(v,a+8)});};
    vm->hle[camera_look]=[this,vec](OverlayVm& v) {
        Shot s;s.mode=9;s.eye=vec(v,v.arg(0));s.target=vec(v,v.arg(1));requested_shot_=s;v.passthrough=true;
    };
    vm->hle[camera_focus]=[this](OverlayVm& v) {
        Shot s;s.mode=10;s.moby=v.arg(0);requested_shot_=s;v.passthrough=true;
    };
    vm->hle[camera_place]=[this,vec](OverlayVm& v) {
        if(static_cast<std::int32_t>(v.read32(spyro_health))>=0)warp_={vec(v,v.arg(0)),v.arg(1)&255};
        v.passthrough=true;
    };
    // Particles are presentation; the native renderer has its own effects.
    // Type 0C (the reward / arrival sparkle; Zoe's wand and zap, class 110
    // 80073d40: 800670fc(n, C, moby, 3 or 5)) is drawn natively, spawned as
    // the level's particle code does (Glimmer 80080720, the same in every
    // overlay): particle k sits at the offset table 800644f0[k] (12 bytes)
    // turned by the moby's matrix (+20, 8001b9a4) from its origin (+0C);
    // variant 5 adds up to 256 units of jitter per axis, 6 up to 128; its
    // drift per update is that table entry / 4 - 4 + rand & 7; size 32 + rand
    // & 7 (four units per size); colour 800644d0[variant] (R, G, B bytes),
    // each channel -8 per update until all are 0 (then it is removed). The
    // update (80084e18 etc.) spins it by 8 / 256 of a turn per update; its
    // size does not change.
    if(const auto particles=vm->read32(particle_hook))vm->hle[particles]=[this](OverlayVm& v) {
        v.r[2]=0;
        if(v.arg(1)!=0xc || !current_)return;
        const auto a=v.arg(2);if(a<0x80000000u || a>=0x80800000u)return;
        const bool moby=a>=mobys_ && (a-mobys_)%moby_size==0 && a<0x80700000u;
        const auto at=moby?a+0xc:a;
        const unsigned count=std::clamp<unsigned>(v.arg(0),1,8);   // a0 is the count (401: 800670fc(1, 53, position, velocity))
        const auto variant=v.arg(3);
        const auto colour=variant<8?v.read32(0x800644d0+variant*4):0x2e60a0c0u;
        const unsigned red=colour&0xff,green=(colour>>8)&0xff,blue=(colour>>16)&0xff;
        const unsigned lifetime=std::max(2u,(std::max({red,green,blue})+7)/8*2);   // frames: one update is two
        const auto random=[this]{bolt_seed_=bolt_seed_*1103515245u+12345u;return static_cast<int>((bolt_seed_>>8)&0x7fff);};
        const auto m=[&](unsigned i){return static_cast<std::int64_t>(static_cast<std::int16_t>(v.read16(a+0x20+i*2)));};
        for(unsigned k=0;k<count;++k) {
            const auto entry=0x800644f0u+k*12;
            const std::int64_t ox=s32(v,entry),oy=s32(v,entry+4),oz=s32(v,entry+8);
            retail::IVec3 q{s32(v,at),s32(v,at+4),s32(v,at+8)};
            if(moby) {   // 8001b9a4: IR3 = x, IR1 = -y, IR2 = -z; out = (MAC3, -MAC1, -MAC2) >> 12
                const std::int64_t i1=-oy,i2=-oz,i3=ox;
                q.x+=static_cast<int>((m(6)*i1+m(7)*i2+m(8)*i3)>>12);
                q.y-=static_cast<int>((m(0)*i1+m(1)*i2+m(2)*i3)>>12);
                q.z-=static_cast<int>((m(3)*i1+m(4)*i2+m(5)*i3)>>12);
            } else {q.x+=static_cast<int>(ox);q.y+=static_cast<int>(oy);q.z+=static_cast<int>(oz);}
            if(variant==5){q.x+=256-(random()&0x1ff);q.y+=256-(random()&0x1ff);q.z+=256-(random()&0x1ff);}
            else if(variant==6){q.x+=128-(random()&0xff);q.y+=128-(random()&0xff);q.z+=128-(random()&0xff);}
            const retail::IVec3 drift{static_cast<int>(ox>>2)-4+(random()&7),static_cast<int>(oy>>2)-4+(random()&7),static_cast<int>(oz>>2)-4+(random()&7)};
            const auto p=retail::to_world(q);
            const auto velocity=(retail::to_world({q.x+drift.x,q.y+drift.y,q.z+drift.z})-p)*30.0f;   // per update -> per second
            current_->powers.emit_sparkle(p,velocity,{red/128.0f,green/128.0f,blue/128.0f},lifetime);
            ++script_sparkles;
        }
    };
    // The challenge HUD. The level's hooks 80067154 / 80067158 call the
    // resident counter code: 8005199c (a counter eases toward its value,
    // entering / holding / leaving), 80051d00, 80052498 (the entry bounce),
    // 8005251c (icon and number), 80052690 (icon, number / total) and
    // 80052960 (m:ss.hh). It is arithmetic on RAM and runs as is; only the
    // sprite primitive 800520cc (GPU packet, 8001b390 into the ordering
    // table) is replaced: it records the sprite and hands back a scratch
    // primitive whose CLUT (+0E) and colour (+04) the caller may change.
    for(std::uint32_t a=0x8005199c;a<0x80051d14;a+=4)vm->guard.erase(a);
    for(std::uint32_t a=0x80052498;a<0x80052b88;a+=4)vm->guard.erase(a);
    vm->hle[0x800520cc]=[this](OverlayVm& v) {
        constexpr std::uint32_t prims=0x807f4000;
        const auto prim=prims+static_cast<std::uint32_t>(hud_pending_.size()%64)*0x20;
        v.r[2]=prim;
        const auto desc=v.arg(0);
        if(!hud_capturing_ || desc<0x80000000u || desc>=0x80800000u || hud_pending_.size()>=64)return;
        RetailHudDraw d;d.u=v.read8(desc);d.v=v.read8(desc+1);d.flags=v.read8(desc+2);d.row=v.read8(desc+3);
        d.type=static_cast<std::uint8_t>(v.arg(3));d.width=v.read8(0x800634b8+d.type);d.height=v.read8(0x800634d0+d.type);
        d.x=static_cast<std::int16_t>(v.arg(1));d.y=static_cast<std::int16_t>(v.arg(2));
        d.clut=static_cast<std::uint16_t>((d.flags&3)+d.row*64+0x4020);
        for(std::uint32_t k=0;k<0x20;k+=4)v.write32(prim+k,0);
        v.write8(prim+4,0x80);v.write8(prim+5,0x80);v.write8(prim+6,0x80);v.write16(prim+0xe,d.clut);
        hud_pending_.push_back(d);hud_prims_.push_back(prim);
    };
    vm_=std::move(vm);
    written_.assign(game.retail_objects.size(),{});
    for(const auto i:owned_)written_[i]=game.retail_objects[i].position;
    // Reapply the level's persistent script state from the save, through the
    // retail registry so it stays registered for the next visit.
    auto pending=std::move(pending_memory_);pending_memory_.clear();
    restore_memory(std::move(pending));
    capture_memory();
}
void RetailScripts::restore_memory(std::vector<RetailMemory> records) {
    if(!booted_ || !vm_){pending_memory_=std::move(records);return;}
    auto& vm=*vm_;
    for(const auto& r:records) {
        if(r.address<0x80000000 || r.address>=0x80800000 || (r.size!=1 && r.size!=2 && r.size!=4))continue;
        if(std::find(memory_lists.begin(),memory_lists.end(),r.list)==memory_lists.end())continue;
        if(r.size==1)vm.write8(r.address,static_cast<std::uint8_t>(r.value));
        else if(r.size==2)vm.write16(r.address,static_cast<std::uint16_t>(r.value));
        else vm.write32(r.address,r.value);
        vm.r[29]=retail_stack_top;vm.call(persist,std::array<std::uint32_t,3>{r.address,r.size,r.list},100'000);
    }
    capture_memory();
}
void RetailScripts::capture_memory() {
    if(!vm_)return;
    auto& vm=*vm_;memory_.clear();
    for(const auto list:memory_lists) {
        const auto count=std::min<std::uint32_t>(vm.read32(list+0x240),64);
        for(std::uint32_t k=0;k<count;++k) {
            const auto entry=vm.read32(list+0x40+k*8);
            memory_.push_back({list,(entry&0xffffff)|0x80000000,entry>>24,vm.read32(list+0x44+k*8)});
        }
    }
}
std::uint32_t RetailScripts::peek(std::uint32_t address,unsigned size)const {
    if(!vm_)return 0;
    auto& vm=*vm_;return size==1?vm.read8(address):size==2?vm.read16(address):vm.read32(address);
}
void RetailScripts::poke(std::uint32_t address,std::uint32_t value,unsigned size) {
    if(!vm_)return;
    auto& vm=*vm_;if(size==1)vm.write8(address,static_cast<std::uint8_t>(value));else if(size==2)vm.write16(address,static_cast<std::uint16_t>(value));else vm.write32(address,value);
}
void RetailScripts::set_pad(std::uint32_t held,std::uint32_t pressed){pad_held_=held;pad_pressed_|=pressed|(held&~pad_previous_);}
void RetailScripts::sync_progress_in(Game& game) {
    // The save is the source of truth: orbs (this level's bits and the total),
    // talismans, gems and Moneybags purchases, as the resident code keeps them.
    auto& vm=*vm_;
    const unsigned index=(game.level.wad_slot-15)/2;if(index>=29)return;
    vm.write32(level_index,index);
    vm.write8(orb_table+index,static_cast<std::uint8_t>(game.guide_awards.orbs));
    vm.write32(orb_total,game.player.orbs);
    vm.write8(talisman_table+index,game.guide_awards.talisman || game.guide_awards.boss?1:0);
    vm.write32(talisman_total,static_cast<std::uint32_t>(std::popcount(game.player.talismans)));
    vm.write32(gem_total,game.player.gems);gems_written_=game.player.gems;
    // 80066ff4: set by the reward (80016328 state 4, update 150; an orb in the
    // Perfect mask likewise) when this visit's talisman / completion is given.
    // 8003a0d8's command 1 picks the talisman line only while it is clear, so
    // the speaker's next talk opens on its following line. The PC plays the
    // reward natively (Game::reward_this_visit) and never wrote it: the talisman
    // line sets the speaker's +5 = 2 (talk by itself, command at 8003a804), so
    // the talisman speaker (Glimmer's Twitchy) talked about the talisman again
    // and again after giving it and never let Spyro go.
    vm.write8(0x80066ff4,game.reward_this_visit?1:0);
    for(unsigned i=0;i<purchase_count;++i)vm.write16(purchase_table+i*4+2,(game.purchases>>i)&1);
}
void RetailScripts::sync_progress_out(Game& game) {
    auto& vm=*vm_;
    const unsigned index=(game.level.wad_slot-15)/2;if(index>=29)return;
    const unsigned fresh=vm.read8(orb_table+index)&~game.guide_awards.orbs&0xff;
    for(unsigned bit=0;bit<8;++bit)if((fresh>>bit)&1 && game.award_orb(bit)) {
        ++orbs_awarded;game.custom_notice="ORB COLLECTED - "+std::to_string(game.player.orbs)+" / 64";game.custom_notice_ticks=240;
    }
    const auto talismans=game.player.talismans;
    if(vm.read8(talisman_table+index) && !game.guide_awards.talisman && game.award_talisman()) {
        ++talismans_awarded;
        if(game.player.talismans!=talismans){game.custom_notice="TALISMAN COLLECTED";game.custom_notice_ticks=240;}
    }
    // Crush's and Gulp's deaths set their own talisman-table byte (overlay 30
    // 80073060); the PC save keeps it as the level's boss award.
    if(vm.read8(talisman_table+index) && !game.guide_awards.boss && (game.level.wad_slot==31 || game.level.wad_slot==55)) {
        game.guide_awards.boss=true;++bosses_defeated;
    }
    // Payments: 8003a568 deducts a service's price from 800670cc. Only what a
    // script took off the total last written counts: gems Spyro picked up
    // since (a conversation pauses that write) are not a payment.
    const auto gems=s32(vm,gem_total);
    if(gems>=0 && static_cast<unsigned>(gems)<gems_written_) {
        const auto paid=gems_written_-static_cast<unsigned>(gems);
        if(std::getenv("SPYRO_GEM_TRACE"))std::fprintf(stderr,"gem payment %u (script total %d, last written %u, counter %u) at script tick %u\n",paid,gems,gems_written_,game.player.gems,script_ticks);
        game.player.gems=game.player.gems>paid?game.player.gems-paid:0;gems_written_=static_cast<unsigned>(gems);
    }
    for(unsigned i=0;i<purchase_count;++i)if(vm.read16(purchase_table+i*4+2))game.purchases|=1u<<i;
}
std::optional<unsigned> RetailScripts::dialogue_line() {
    if(!dialogue_moby_)return std::nullopt;
    const auto header=vm_->read32(dialogue_moby_);
    if(header<0x80000000 || header>=0x80800000)return std::nullopt;
    return vm_->read8(header+4);   // 80015be4 copies the line pointer (+2) to the shown line (+4)
}
std::optional<unsigned> RetailScripts::begin_dialogue(Game& game,unsigned object) {
    if(!dialogue_bridge() || object>=initial_ || dialogue_moby_)return std::nullopt;
    auto& vm=*vm_;const auto m=mobys_+object*moby_size;
    const auto header=vm.read32(m);
    if(header<0x80000000 || header>=0x80800000 || vm.read16(m+0x36)!=game.retail_objects[object].type)return std::nullopt;
    sync_progress_in(game);
    // A natively run speaker skips its class code, which marks the talisman
    // speaker before the conversation starts; do what it does.
    // Not Breeze Harbor's Squawk: the native burner quest decides his talisman.
    if(vm.read8(header)==1 && game.retail_objects[object].type!=0x180)vm.write8(talisman_speaker,1);
    vm.r[29]=retail_stack_top;vm.call(start_dialogue,std::array<std::uint32_t,3>{m,0,1},2'000'000);
    if(!vm.fault.empty()){++faults;fault="dialogue start: "+vm.fault;vm.fault.clear();vm.write32(game_state,0);return std::nullopt;}
    dialogue_moby_=m;++dialogues;
    return dialogue_line();
}
std::optional<unsigned> RetailScripts::advance_dialogue(Game& game,int answer) {
    if(!dialogue_moby_)return std::nullopt;
    auto& vm=*vm_;const auto m=dialogue_moby_;
    const auto call=[&](std::uint32_t f,std::initializer_list<std::uint32_t> args) {
        std::vector<std::uint32_t> a(args);vm.r[29]=retail_stack_top;vm.call(f,a,2'000'000);
        if(!vm.fault.empty()){++faults;fault="dialogue: "+vm.fault;vm.fault.clear();}
    };
    const auto again=[&]{call(start_dialogue,{m,vm.read32(dialogue_clip_a),vm.read32(dialogue_clip_b)});return dialogue_line();};
    // 80016328, dialogue state 1: a finished line follows its 29 (next)
    // command; a chosen answer goes through 8003a568's 25 command.
    if(answer<0) {
        if(s32(vm,dialogue_next)!=-1){call(advance_line,{m});return again();}
    } else {
        vm.write32(dialogue_answer,static_cast<std::uint32_t>(answer));vm.write8(dialogue_question,1);
        call(advance_line,{m});
        if(vm.read8(dialogue_question))return again();
    }
    // State 2 (closing): one more advance on the reached line (23 repeat),
    // Zoe's arrival (26), then the reward the opening line selected.
    call(advance_line,{m});
    const auto reward=s32(vm,dialogue_reward);
    if(vm.read8(dialogue_zoe) && !reward)call(zoe_appears,{});
    const unsigned index=(game.level.wad_slot-15)/2;
    if(reward && index<29) {
        // State 3 (orb, class 71): 8003a0d8 already set the orb bit; later
        // worlds' perfect-masks also grant the talisman. State 4: talisman.
        const bool talisman=reward!=static_cast<int>(orb_reward_class) ||
            (index>0x15 && (vm.read8(perfect_masks+index+2)&vm.read8(dialogue_awarded)));
        // 80016328 state 4, update 150 sets 80066ff4 even if the save
        // already owns the talisman. State 3 does the same for a Perfect
        // orb. This is a visit flag, not a newly-awarded flag: on a revisit
        // award_talisman() returns false, but command 1 must stop selecting
        // this line or its command 2B makes the speaker talk forever.
        if(talisman){game.reward_this_visit=true;vm.write8(0x80066ff4,1);}
        if(talisman && !vm.read8(talisman_table+index)) {
            vm.write8(talisman_table+index,1);vm.write32(talisman_total,vm.read32(talisman_total)+1);
        }
    }
    return std::nullopt;
}
void RetailScripts::zoe_reward(unsigned side) {
    if(!vm_ || failed_ || !reward_speaker_)return;
    auto& vm=*vm_;
    const auto speaker=vm.read32(dialogue_moby),camera=vm.read32(0x80066f5c);
    vm.write32(dialogue_moby,reward_speaker_);vm.write32(0x80066f5c,side);
    vm.r[29]=retail_stack_top;vm.call(zoe_appears,{},400'000);
    if(!vm.fault.empty()){++faults;fault="zoe: "+vm.fault;vm.fault.clear();}
    vm.write32(dialogue_moby,speaker);vm.write32(0x80066f5c,camera);
}
void RetailScripts::end_dialogue(Game& game) {
    if(!dialogue_moby_ || !vm_)return;
    auto& vm=*vm_;reward_speaker_=dialogue_moby_;
    const auto speaker=(dialogue_moby_-mobys_)/moby_size;const auto reward=vm.read32(dialogue_reward);
    vm.r[29]=retail_stack_top;vm.call(close_dialogue,{},400'000);
    if(!vm.fault.empty()){vm.fault.clear();}
    vm.write32(game_state,0);vm.write32(dialogue_running,0);
    dialogue_moby_=0;
    sync_progress_out(game);
    // A selected reward plays out on revisits too (80016328 states 3 / 4).
    // 8003a0d8 marks already-owned rewards with 80067164, but does not skip
    // their presentation. sync_progress_out only counts genuinely new ones.
    if(speaker<initial_ && reward && reward<0x400) {
        game.start_reward_show(speaker,reward,reward!=orb_reward_class);game.custom_notice_ticks=0;
    }
    capture_memory();
}
void RetailScripts::build_active(OverlayVm& vm) {
    // 80040e94 restricted to script-owned, unfrozen mobys.
    for(std::uint32_t a=group_flags;a<group_flags+32;a+=4)vm.write32(a,0);
    auto out=active_list;
    // Run captive butterflies before their jars. Otherwise a jar releases
    // its D3 and the same dispatcher's later D3 case immediately eats it
    // into VM-only health/lives, before sync_out can hand it to Sparx.
    // State 2 still runs its original spin/animation; all free food transfers
    // to the native controller at sync_out, before any retail feeding case.
    for(std::uint32_t m=initial_end_;m<initial_end_+512*moby_size;m+=moby_size) {
        const auto state=static_cast<std::int8_t>(vm.read8(m+0x48));
        if(state==-1)break;
        const auto type=vm.read16(m+0x36);
        if(state==2 && (type==0x10 || type==0xd3) && out<active_list+0x7f0){vm.write32(out,m);out+=4;}
    }
    const auto eligible=[&](std::uint32_t m) {
        const auto index=(m-mobys_)/moby_size;
        if(m<initial_end_) {
            if(index>=owned_mask_.size() || !owned_mask_[index])return false;
            const auto& o=current_->retail_objects[index];
            return !o.frozen_ticks && !o.stun_ticks && !o.pc_carried;
        }
        const auto type=vm.read16(m+0x36);
        return type!=0x10 && type!=0xd3;
    };
    const auto spyro=retail::IVec3{s32(vm,spyro_origin),s32(vm,spyro_origin+4),s32(vm,spyro_origin+8)};
    for(std::uint32_t m=mobys_;m<0x80800000;m+=moby_size) {
        const auto state=static_cast<std::int8_t>(vm.read8(m+0x48));
        if(state<0){if(state==-1)break;continue;}
        if(!eligible(m))continue;
        const auto group=static_cast<std::int8_t>(vm.read8(m+0x43));
        bool active=false;
        if(vm.read8(m+0x4d))active=true;
        else {
            vm.write8(m+0x4b,0);
            const auto radius=static_cast<std::int8_t>(vm.read8(m+0x4a));
            if(!radius)continue;
            if(radius<0)active=true;
            else {
                const std::int64_t r=static_cast<std::int64_t>(radius)<<10;
                const std::int64_t dx=s32(vm,m+0xc)-spyro.x,dy=s32(vm,m+0x10)-spyro.y,dz=s32(vm,m+0x14)-spyro.z;
                if(std::abs(dx)>=r || std::abs(dy)>=r || std::abs(dz)>=r)continue;
                const auto q=r>>3;
                active=(dx>>3)*(dx>>3)+(dy>>3)*(dy>>3)+(dz>>3)*(dz>>3)<q*q;
            }
        }
        if(!active)continue;
        vm.write8(m+0x4b,1);
        if(group>=0)vm.write8(group_flags+static_cast<std::uint32_t>(group),1);
        else if(out<active_list+0x7f0){vm.write32(out,m);out+=4;}
    }
    const auto lists=vm.read32(groups_global);
    for(unsigned g=0;g<32 && lists;++g) {
        if(!vm.read8(group_flags+g))continue;
        auto entry=vm.read32(lists+g*4);
        for(unsigned guard=0;guard<512 && entry;++guard) {
            const auto value=static_cast<std::int16_t>(vm.read16(entry));entry+=2;
            const auto m=mobys_+static_cast<std::uint32_t>(value&0x7fff)*moby_size;
            if(static_cast<std::int8_t>(vm.read8(m+0x48))>=0 && eligible(m) && out<active_list+0x7f0){vm.write32(out,m);out+=4;vm.write8(m+0x4b,1);}
            if(value<0)break;
        }
    }
    vm.write32(out,0);
}
void RetailScripts::present_bolts(Game& game,OverlayVm& vm) {
    // 8004b120(segments, a1, colour) takes a free record of the table at
    // 8006b400 (+1C segments, 0 = free; +0 from, +0C to, +18 colour, +20 one
    // random word per interior bend); the caller writes the endpoints each
    // frame and frees it by clearing +1C. 8004b194 draws it, re-rolling each
    // bend every other frame (half the old plus a new one), as for the gates.
    std::vector<GateBolt> bolts;
    const auto field=[](unsigned w,unsigned low,unsigned bits){const int v=static_cast<int>((w>>low)&((1u<<bits)-1));return v>=(1<<(bits-1))?v-(1<<bits):v;};
    for(unsigned k=0;k<8;++k) {
        const auto b=0x8006b400u+k*0x30;const auto segments=vm.read8(b+0x1c);
        if(!segments || segments>4){bolt_live_[k]=0;continue;}
        if(!bolt_live_[k]){bolt_live_[k]=1;for(unsigned j=0;j<3;++j)bolt_words_[k][j]=vm.read16(b+0x20+j*2);}
        GateBolt bolt;bolt.from=retail::to_world({s32(vm,b),s32(vm,b+4),s32(vm,b+8)});bolt.to=retail::to_world({s32(vm,b+0xc),s32(vm,b+0x10),s32(vm,b+0x14)});
        if(std::getenv("SPYRO_BOLT_DEBUG"))   // diagnostics: the bolt's ends against the Zoes (class 110) and Spyro
            for(unsigned i=0;i<initial_ && i<game.retail_objects.size();++i)if(game.retail_objects[i].type==0x110 && length(game.retail_objects[i].position-bolt.from)<4) {
                const auto& z=game.retail_objects[i];const auto m=mobys_+i*moby_size;
                std::fprintf(stderr,"bolt %u tick %u from %.3f %.3f %.3f to %.3f %.3f %.3f | zoe #%u at %.3f %.3f %.3f anim %08x size %u | spyro %.3f %.3f %.3f\n",k,script_ticks,
                    bolt.from.x,bolt.from.y,bolt.from.z,bolt.to.x,bolt.to.y,bolt.to.z,i,z.position.x,z.position.y,z.position.z,vm.read32(m+0x3c),vm.read8(m+0x4f),
                    game.player.position.x,game.player.position.y,game.player.position.z);
            }
        const bool reroll=game.clock.ticks()&2;
        for(unsigned j=0;j<3;++j) {
            auto& word=bolt_words_[k][j];retail::IVec3 bend{field(word,7,8),field(word,2,8),field(word,0,5)*8};
            if(reroll){bolt_seed_=bolt_seed_*1103515245u+12345u;const unsigned fresh=bolt_seed_>>8;
                bend={bend.x/2+field(fresh,8,7),bend.y/2+field(fresh,3,7),bend.z/2+field(fresh,0,5)*4};word=static_cast<std::uint16_t>(fresh);}
            bolt.bends[j]=j+1<segments?bend:retail::IVec3{};
        }
        bolts.push_back(bolt);++script_bolts;
    }
    game.powers.set_script_bolts(std::move(bolts));
}
void RetailScripts::run_hud(Game& game,OverlayVm& vm) {
    // 80067154 / 80067158: installed by every level overlay. Crush and Gulp
    // keep their native boss HUD (HudController::boss), the speedways their
    // native trial HUD and Breeze Harbor its native trolley count.
    const auto slot=game.level.wad_slot;
    if(hud_failed_ || slot==31 || slot==55 || slot==29 || slot==43 || slot==53 || slot==63 || slot==39){hud_draws_.clear();return;}
    const auto valid=[](std::uint32_t a){return a>=0x80010000u && a<0x80800000u && !(a&3);};
    const auto update=vm.read32(0x80067154),draw=vm.read32(0x80067158);
    const auto run=[&](std::uint32_t entry) {
        vm.r[29]=retail_stack_top;vm.call(entry,{},400'000);
        if(!vm.fault.empty()){fault="hud hook "+std::to_string(entry)+": "+vm.fault;vm.fault.clear();hud_failed_=true;return false;}
        return true;
    };
    if(valid(update) && !run(update)){hud_draws_.clear();return;}
    hud_pending_.clear();hud_prims_.clear();hud_capturing_=true;
    const bool drawn=valid(draw) && run(draw);
    hud_capturing_=false;
    if(!drawn){hud_draws_.clear();return;}
    for(std::size_t k=0;k<hud_pending_.size();++k) {
        auto& d=hud_pending_[k];const auto prim=hud_prims_[k];
        d.clut=vm.read16(prim+0xe);d.color=vm.read8(prim+4)|(vm.read8(prim+5)<<8)|(vm.read8(prim+6)<<16);
    }
    hud_draws_=hud_pending_;++hud_ticks;
}
void RetailScripts::advance_restart(Game& game,OverlayVm& vm) {
    // 8001722c, once per game frame: 800698f0 = 0 adds 0x10 to the fade
    // 80066f68 until it passes 0xfe; 1 reloads the level (at least six frames
    // with the CD read) and places Spyro at the checkpoint; 2 subtracts 0x10
    // until it reaches 0, then game state 0 and 80067100 = 0. Spyro's
    // controls are held (8006a1fc = 10000002) throughout.
    auto& r=restart_;++r.ticks;
    if(r.stage==1) {
        r.fade=std::min(1.0f,static_cast<float>(r.ticks*0x10)/255.0f);
        if(r.ticks*0x10>0xfe){r.stage=2;r.ticks=0;r.fade=1;}
        return;
    }
    if(r.stage==2) {
        if(r.ticks==1) {
            // The reload: Spyro at the checkpoint (adopted into the spawn above),
            // facing its heading; the camera returns to its normal mode behind him.
            vm.write32(camera_mode,0);vm.write32(camera_substate,0);vm.write32(camera_request,0);
            shot_={};requested_shot_.reset();warp_.reset();place_={};mode_ticks_=0;
            reload_mobys(game,vm);
            game.restart_at_checkpoint();game.camera.scripted_view.reset();
        }
        if(r.ticks>=6){r.stage=3;r.ticks=0;}
        return;
    }
    r.fade=std::max(0.0f,1-static_cast<float>(r.ticks*0x10)/255.0f);
    if(r.ticks*0x10>=0xff){r={};vm.write32(game_state,0);vm.write32(dialogue_running,0);}
}
// 8001722c's reload (800698f0 = 1): the level's gameplay section is read
// from the disc again (80013918) and the gameplay loader 8001c734 runs on it,
// so every moby starts over from the level file; the loader then applies the
// checkpoint's copy of the persistent list (8004c31c), which is how a script
// carries its progress across the restart (Sunny Beach's Master Chef steps
// his phase back and picks his "one more time" line with 8004c214 before he
// calls 8004c1ac). Without it the caller stayed in its restart state and
// asked again every tick: game state 3 for ever.
void RetailScripts::reload_mobys(Game& game,OverlayVm& vm) {
    const auto& image=game.level.script_image;
    if(!image)return;
    const auto entries=parse_level_archive(image->archive);
    if(entries.size()!=4)return;
    const auto& gameplay=entries[3];
    vm.load(retail_gameplay_base,std::span<const std::uint8_t>(image->archive).subspan(gameplay.offset,gameplay.size));
    vm.hle[active_builder]=[](OverlayVm& v){v.write32(active_list,0);};
    vm.r[29]=retail_stack_top;vm.call(gameplay_loader,std::array<std::uint32_t,1>{retail_gameplay_base},40'000'000);
    vm.hle[active_builder]=[this](OverlayVm& v){build_active(v);};
    if(!vm.fault.empty()){++faults;fault="reload: "+vm.fault;vm.fault.clear();return;}
    ++reloads;
    mobys_=vm.read32(moby_array);initial_end_=vm.read32(initial_end_global);
    // Engine side: script mobys take the reloaded records; the ones the
    // scripts spawned are gone (sync_out retires their slots); native mobys
    // stay out of the scripts' sphere queries as at boot.
    for(unsigned i=0;i<initial_ && i<game.retail_objects.size();++i) {
        const auto m=mobys_+i*moby_size;
        if(i<owned_mask_.size() && owned_mask_[i])mirror(game,game.retail_objects[i],m,true);
        else {hide_from_queries(vm,m);if(i<native_states_.size())native_states_[i]=static_cast<std::int8_t>(vm.read8(m+0x48));}
    }
    for(auto& slot:slots_){if(slot!=UINT32_MAX && game.retail_objects[slot].state>=0)game.retail_objects[slot].state=-1;slot=UINT32_MAX;}
    support_=contact_=standing_=0;touched_.clear();ride_={};
    checkpoint_seen_=checkpoint_words(vm);
    capture_memory();
}
bool RetailScripts::advance_place(Game& game,OverlayVm& vm) {
    // Runs every script tick, also while a conversation is open (the host may
    // talk during the fade): the placement and the fade always finish.
    if(warp_ && !place_.active){place_={};place_.active=true;vm.write32(camera_substate,0);}
    if(!place_.active)return false;
    place_.timer+=2;
    if(!place_.placed) {
        place_.fade=std::min(1.0f,static_cast<float>(place_.timer*0x10)/255.0f);
        if(place_.timer>0x10 && warp_) {
            const auto [origin,heading]=*warp_;warp_.reset();
            game.player.position=game.player.previous_position=origin-retail::origin_above_feet;
            game.player.velocity={};game.player.yaw=retail::native_heading(static_cast<float>(heading<<4));
            // Mode 11 also posts request 10010480 (80021240): bit 80 asks Spyro's
            // update (80030ed4) for the state 8001f524 left in 8006a22c. The givers
            // pass 0, standing on the ground: a superfly flight ends there (Magma
            // Cone's Chedda places a flying Spyro after Party Crashers).
            if(vm.read32(0x8006a22c)==0 && game.powers.flying){game.powers.end_flight();game.player.pitch=game.player.roll=0;}
            game.camera.scripted_view.reset();game.camera.reset(game.level,game.player.position,game.player.yaw);
            shot_.active=false;++warps;place_.placed=true;vm.write32(camera_substate,1);
            sync_in(game);
        }
        return true;
    }
    const int fade=std::clamp((static_cast<int>(place_.timer)-16)*-16+0x100,0,0xff);place_.fade=static_cast<float>(fade)/255.0f;
    if(fade<1) {
        if(vm.read32(camera_mode)==11){vm.write32(camera_mode,shot_.normal_mode==9 || shot_.normal_mode==10?0:shot_.normal_mode);vm.write32(camera_substate,0);}
        if(vm.read32(camera_request)==11)vm.write32(camera_request,0);
        place_={};
    }
    return true;
}
void RetailScripts::direct_camera(Game& game,OverlayVm& vm) {
    // Shakes: scripts set a strength and a frame count, and some wait for the
    // count to run out (Colossus 11a's sequence). The retail camera update
    // counts it down once per frame; the engine camera plays the shake.
    if(const auto left=static_cast<std::int32_t>(vm.read32(camera_shake_ticks));left>0) {
        if(static_cast<std::uint32_t>(left)!=shake_left_)game.camera.shake(vm.read32(camera_shake_strength),static_cast<unsigned>(left)*2);
        shake_left_=static_cast<std::uint32_t>(left-1);vm.write32(camera_shake_ticks,shake_left_);
    } else shake_left_=0;
    // Mode 11 (8001f524; 8001fa58 case 0xB): the screen fades out (0x10 per
    // timer unit, the timer growing by the elapsed VBlanks), then Spyro is
    // placed at the requested origin and heading with the camera behind him,
    // and it fades back in before the normal mode returns. The scripts keep
    // running through the fade: Colossus 11A awards its spirit (state 99)
    // before Spyro arrives beside the monk, who then talks.
    if(advance_place(game,vm))return;
    // The camera update adopts a mode 9/10 request: substate 1 while it
    // travels, 2 once it holds the shot; 0x80 (8001f2e4 with a blend) eases back.
    const auto request=vm.read32(camera_request);
    auto mode=vm.read32(camera_mode);
    if((request==9 || request==10) && mode!=request) {
        Shot next=requested_shot_?*requested_shot_:Shot{};next.mode=request;
        next.normal_mode=shot_.active?shot_.normal_mode:mode;next.active=true;
        auto substate=1u;
        if(request==10) {
            // 800216d8 case 10: flag 2 (80068038) blends from the current view
            // (substate 3); otherwise, from a normal mode, it fades out (0).
            const auto view=game.camera.sample(1);next.from_eye=view.eye;next.from_forward=view.forward;
            substate=(vm.read32(0x80068038)&2)?3u:(mode==9 || mode==10)?vm.read32(camera_substate):0u;
        }
        shot_=next;
        vm.write32(camera_mode,request);vm.write32(camera_substate,substate);vm.write32(camera_timer,0);mode=request;++camera_shots;
    }
    requested_shot_.reset();
    // Any other request (8001f1e4: boss arenas, fixed and follow variants) is
    // adopted the same way; the engine keeps its own camera for these.
    if(request && request!=9 && request!=10 && request!=11 && mode!=request) {
        vm.write32(camera_mode,request);vm.write32(camera_substate,1);vm.write32(camera_request,0);mode=request;mode_ticks_=0;
    }
    if(mode!=9 && mode!=10) {
        if(shot_.active){shot_.active=false;game.camera.scripted_view.reset();}
        if(vm.read32(camera_substate)==1 && ++mode_ticks_>=12)vm.write32(camera_substate,2);
        return;
    }
    if(!shot_.active){shot_={};shot_.mode=mode;shot_.active=true;}
    if(mode==10){play_path(game,vm);return;}
    const auto substate=vm.read32(camera_substate);
    if(substate&0x80) {
        // 8001f2e4 writes 80; the update blends back in its arriving phase (81),
        // then re-enters the normal mode arriving (substate 1, 800213e8) and
        // settles (2). Scripts wait on bit 0 (Colossus 11a's states 61/62).
        if(substate==0x80)vm.write32(camera_substate,0x81);
        if(++shot_.end_ticks>=10) {
            vm.write32(camera_mode,shot_.normal_mode==9 || shot_.normal_mode==10?0:shot_.normal_mode);
            vm.write32(camera_substate,1);vm.write32(camera_request,0);mode_ticks_=0;
            shot_.active=false;game.camera.scripted_view.reset();return;
        }
    } else if(substate==1 && ++shot_.ticks>=12)vm.write32(camera_substate,2);
    else if(substate!=1 && substate!=2)vm.write32(camera_substate,1);
    vm.write32(camera_timer,vm.read32(camera_timer)+1);
    // Present the shot.
    const Vec3 eye=shot_.eye,target=shot_.target;
    if(length(target-eye)>.05f)game.camera.scripted_view=CameraView{eye,normalized(target-eye)};
}
void RetailScripts::play_path(Game& game,OverlayVm& vm) {
    // 8001fa58 case 10. 80068028 points at the path: a key count, then keys
    // of u16 x, y, z (x4, retail units) and s16 roll, pitch, yaw (the
    // cinematic camera layout); 8006802c is the key shown, 80068038 the flags
    // (1 hold the key, 2 blend in from the current view, 4 loop, 8 stay at the
    // end). The timer 80067ed8 grows by the elapsed VBlanks (2) every frame.
    constexpr std::uint32_t path_global=0x80068028,key_global=0x8006802c,flags_global=0x80068038,fade_global=0x80066f68;
    const auto path=vm.read32(path_global),flags=vm.read32(flags_global);
    auto substate=vm.read32(camera_substate);const auto timer=vm.read32(camera_timer);
    const auto count=path>=0x80000000u && path<0x807f0000u?vm.read32(path):0u;
    auto key=std::min(vm.read32(key_global),count?count-1:0u);
    const auto view=[&](std::uint32_t k) {
        const auto at=path+4+k*12;
        CameraView v;v.eye=retail::world_position({float(vm.read16(at))*4,float(vm.read16(at+2))*4,float(vm.read16(at+4))*4});
        const float pitch=static_cast<std::int16_t>(vm.read16(at+8))*retail::angle_to_radians,yaw=static_cast<std::int16_t>(vm.read16(at+10))*retail::angle_to_radians;
        v.forward={std::cos(yaw)*std::cos(pitch),-std::sin(pitch),std::sin(yaw)*std::cos(pitch)};
        return v;
    };
    const auto finish=[&]{
        vm.write32(camera_mode,shot_.normal_mode==9 || shot_.normal_mode==10?0:shot_.normal_mode);
        vm.write32(camera_substate,1);vm.write32(camera_request,0);vm.write32(fade_global,0);mode_ticks_=0;
        shot_.active=false;shot_.fade=0;game.camera.scripted_view.reset();
    };
    if(!count || count>4096){finish();return;}   // not a path: leave the shot
    int fade=0;std::optional<CameraView> shown;
    if(substate==3) {   // blend from the view it started in to the first key
        const float t=static_cast<float>(std::min(timer,0x40u))/0x40;const auto k=view(key);
        shown=CameraView{lerp(shot_.from_eye,k.eye,t),normalized(lerp(shot_.from_forward,k.forward,t))};
        if(timer>0x3f){substate=2;vm.write32(camera_timer,0x40);}
        vm.write32(dialogue_running,1);
    } else if(substate==0) {   // fade out on the old view
        fade=std::min<int>(static_cast<int>(timer)*16,0xff);
        if(timer>0x10)substate=1;
    } else if(substate==1) {   // fade in on the key
        fade=std::clamp((static_cast<int>(timer)-16)*-16+0x100,0,0xff);
        if(fade<1)substate=2;
        shown=view(key);vm.write32(dialogue_running,1);
    } else if(substate==2) {
        vm.write32(dialogue_running,1);
        if(!(flags&1) && ++key>=count) {
            if(flags&4)key=0;
            else {key=count-1;if(!(flags&8)){substate=0x80;vm.write32(camera_timer,0);}}
        }
        shown=view(key);
    } else if(substate==0x80) {   // fade out on the last key, then back behind Spyro
        fade=std::min<int>(static_cast<int>(timer)*16,0xff);
        if(timer<0x11)shown=view(key);
        else {substate=0x81;game.camera.scripted_view.reset();game.camera.reset(game.level,game.player.position,game.player.yaw);}
    } else if(substate==0x81) {
        fade=std::clamp((static_cast<int>(timer)-16)*-16+0x100,0,0xff);vm.write32(dialogue_running,0);
        if(fade<1){shot_.fade=0;finish();return;}
    } else {finish();return;}
    vm.write32(key_global,key);vm.write32(camera_substate,substate);vm.write32(fade_global,static_cast<std::uint32_t>(fade));
    vm.write32(camera_timer,vm.read32(camera_timer)+2);
    shot_.fade=static_cast<float>(fade)/255.0f;
    if(shown)game.camera.scripted_view=*shown;
}
void RetailScripts::sync_in(Game& game) {
    auto& vm=*vm_;
    const auto origin=retail::from_world(game.player.position+retail::origin_above_feet);
    vm.write32(spyro_origin,static_cast<std::uint32_t>(origin.x));vm.write32(spyro_origin+4,static_cast<std::uint32_t>(origin.y));
    vm.write32(spyro_origin+8,static_cast<std::uint32_t>(origin.z));
    const int heading=static_cast<int>(std::lround(retail::original_heading(game.player.yaw)))&4095;
    vm.write32(spyro_heading,static_cast<std::uint32_t>(heading));vm.write8(spyro_origin+0xe,static_cast<std::uint8_t>(heading>>4));
    // 8002a1f0 copies Spyro's rotation words 8006a054 / 58 / 5c into the bytes
    // 80069ffc / fd / fe (>> 4) every frame. A carried thing's spit aims with the
    // pitch byte (Magma Cone's lava bubbles, 91 state 2, overlay 48 8007a628).
    const int pitch=static_cast<int>(std::lround(game.player.pitch/retail::angle_to_radians))&4095;
    vm.write32(0x8006a058,static_cast<std::uint32_t>(pitch));vm.write8(spyro_origin+0xd,static_cast<std::uint8_t>(pitch>>4));
    vm.write32(spyro_mode,spyro_movement_mode(game));vm.write32(spyro_state,spyro_state_number(game));
    vm.write32(spyro_health,game.player.health);vm.write32(spyro_invulnerable,game.player.invulnerable_ticks/2);
    vm.write32(elapsed_ticks,2);
    // 80066eec: the level-entry counter (8001d7b4). It counts during the load
    // fade and is -1 once gameplay runs; the boss HUD hooks (80067154 /
    // 80067158) and the HUD only run while it is negative.
    vm.write32(0x80066eec,0xffffffffu);
    // 8002cc88 counts frames since Spyro last had floor under him (8006a0b0,
    // 0 while grounded). Crystal Glacier's c6 cancels its flying challenge
    // when it reads 0 with Spyro far from its height.
    vm.write32(airborne_frames,game.player.grounded?0u:std::min(game.player.airborne_ticks,0x7fffu));
    // 8006a104: the flag byte of the floor triangle under Spyro (8002cc88 at
    // 8002ce44 copies the floor query's 8006ab94; FF when there is none or
    // it is not special). Scripts read its surface type through 8002b078
    // (& 3F into 800673e8): the Colossus hockey puck (153) only lets Spyro
    // pick it up on ice (type 4).
    {
        std::uint32_t floor_flags=0xff;
        if(game.player.grounded)
            if(const auto hit=game.level.ground_support(game.player.position+retail::origin_above_feet,game.tuning.radius,0.3f);hit && hit->triangle<game.level.triangles.size())
                floor_flags=game.level.triangles[hit->triangle].surface_flags;
        vm.write32(0x8006a104,floor_flags);
    }
    // The camera eye (80067eac/b0/b4). Scripts measure from it: Aquaria
    // Towers' crabs (9b) turn immune (+47 = F8) beyond 0x707 of the camera.
    {
        const auto view=game.camera.sample(1);
        const auto eye=retail::from_world(view.eye);
        vm.write32(camera_eye,static_cast<std::uint32_t>(eye.x));vm.write32(camera_eye+4,static_cast<std::uint32_t>(eye.y));vm.write32(camera_eye+8,static_cast<std::uint32_t>(eye.z));
        // The camera's heading (80067ecc, 12-bit, Spyro's 8006a05c convention).
        // Skelos Badlands' lava toads (26, 800777cc) leap out only at a Spyro
        // they are in front of the camera for (within 28 of its heading >> 4).
        const int yaw=static_cast<int>(std::lround(retail::original_heading(std::atan2(view.forward.x,view.forward.z))))&4095;
        vm.write16(camera_eye+0x20,static_cast<std::uint16_t>(yaw));
    }
    // 8002c0f4 records the moby Spyro collided with this frame (8006a130);
    // lifts, switches and weighted platforms read it.
    vm.write32(spyro_contact,contact_);contact_=0;
    // Solid-moby contacts since the last tick (8003dc64 / 8002cc88).
    vm.write32(standing_moby,standing_);standing_=0;
    for(const auto& [moby,bits]:touched_)if(static_cast<std::int8_t>(vm.read8(moby+0x48))>=0){vm.write32(moby+0x18,vm.read32(moby+0x18)|bits);attacks_seen|=bits;}
    touched_.clear();
    // Native objects are never dispatched, but scripts read their state (the
    // Party Crashers host checks Magma Cone's power gate). Mirror it; a
    // negative state is never written (-1 terminates the moby list).
    // Once the engine removes a native moby (a chest broken, a gem taken, an
    // enemy defeated) its state reads FE, as 8004132c leaves it, so scripts
    // watching it see it go: class 80 gem riders spill a chest's gems when
    // their host's state has bit 80 (Idol Springs' cb chest holds 42).
    // A script that writes a native moby's state starts it: Magma Cone's end
    // trigger (class 401, 8007f8a0) raises the whirlwind (class 400) beside
    // its host by writing 1 there and persisting it (8004c214); kind-2
    // whirlwinds are controlled that way (travel.cpp reads the state).
    for(unsigned i=0;i<initial_ && i<game.retail_objects.size();++i) {
        if(i<owned_mask_.size() && owned_mask_[i])continue;
        auto& o=game.retail_objects[i];const auto m=mobys_+i*moby_size;
        const auto now=static_cast<std::int8_t>(vm.read8(m+0x48));
        if(now<0)continue;
        if(o.type==0x400 && i<native_states_.size() && now!=native_states_[i] && !o.collected && o.state>=0)o.state=now;
        const auto value=o.collected || o.state<0?std::uint8_t(0xfe):static_cast<std::uint8_t>(o.state);
        vm.write8(m+0x48,value);if(i<native_states_.size())native_states_[i]=static_cast<std::int8_t>(value);
    }
    // A script may change a native whirlwind's kind (header +08, which the
    // class-400 case reads every update: 0 always blows, 2 only while its
    // state is set). Cloud Temples' Murgen (1C7 kind 3, 80078134) zeroes the
    // kind of the whirlwind behind him (his header +3C, #10) after the first
    // bell: "use the whirlwind behind me" to reach the last one.
    for(auto& w:game.level.whirlwinds) {
        if(w.object>=initial_ || (w.object<owned_mask_.size() && owned_mask_[w.object]))continue;
        const auto header=vm.read32(mobys_+w.object*moby_size);
        if(header<0x80000000u || header>=0x80800000u)continue;
        const auto kind=vm.read32(header+8);
        if(kind<=3 && kind!=w.kind){w.kind=kind;w.phase=0;w.timer=0;w.active=kind==0;++whirlwind_kinds;}
    }
    // +4D: the moby renderer marks every moby it drew this frame. 80043944
    // first clears it on every live moby, so the loader's value does not last
    // past the first frame. It draws a moby only when its draw distance +4C
    // (signed, in world units; +1 while it was drawn last frame) is positive,
    // the moby is inside that cube around the camera, and it lies in front,
    // within the view. Scripts hide a moby by zeroing +4C (Zoe before she
    // appears, the Foreman Bud that is elsewhere).
    // 80040e94 activates drawn mobys, 8003a98c only lets Spyro talk to a
    // drawn speaker, and NPCs that move between places (Idol Springs'
    // Foreman Bud, class 124) swap only while undrawn.
    const auto view=game.camera.sample(1);
    for(const auto i:owned_) {
        const auto m=mobys_+i*moby_size;const auto& o=game.retail_objects[i];
        const int reach=static_cast<std::int8_t>(vm.read8(m+0x4c));
        bool drawn=false;
        if(reach>0) {
            const float limit=static_cast<float>(reach+(vm.read8(m+0x4d)&3));
            const auto d=o.position-view.eye;const float distance=length(d),depth=dot(d,view.forward);
            drawn=std::abs(d.x)<limit && std::abs(d.y)<limit && std::abs(d.z)<limit && depth<limit && (distance<3 || depth>distance*0.45f);
        }
        vm.write8(m+0x4d,static_cast<std::uint8_t>(drawn));
    }
    const auto push=[&](unsigned index,std::uint32_t m) {
        auto& o=game.retail_objects[index];
        const auto state=static_cast<std::int8_t>(vm.read8(m+0x48));
        if(state<0)return;
        if(o.collected || o.state<0) {
            // Removed natively (elemental defeat, demons): award the spirit the
            // retail way and retire the moby.
            if(m<initial_end_)vm.call(award_spirit,std::array<std::uint32_t,1>{m});
            vm.call(remove_moby,std::array<std::uint32_t,1>{m});++removed;return;
        }
        if(index<written_.size() && length(o.position-written_[index])>1e-4f) {
            const auto p=retail::from_world(o.position);
            vm.write32(m+0xc,static_cast<std::uint32_t>(p.x));vm.write32(m+0x10,static_cast<std::uint32_t>(p.y));vm.write32(m+0x14,static_cast<std::uint32_t>(p.z));
        }
        // Cloud Temples' golden dog doors (1E) ignore attacks: a wizard (1F)
        // opens its paired door by setting the door's +49 (overlay 60,
        // 80076434), and the door's own case then bursts into nine 255
        // fragments. PC: a green fire bomb's heavy-door bit opens them the
        // same original way.
        if(game.level.wad_slot==61 && o.type==0x1e && (o.attack_flags&0x10000000u) && !vm.read8(m+0x49))vm.write8(m+0x49,1);
        if(o.attack_flags){vm.write32(m+0x18,vm.read32(m+0x18)|o.attack_flags);attacks_seen|=o.attack_flags;o.attack_flags=0;o.attack_sourced=false;}
    };
    for(const auto i:owned_)push(i,mobys_+i*moby_size);
    for(std::size_t k=0;k<slots_.size();++k)if(slots_[k]!=UINT32_MAX)push(slots_[k],initial_end_+static_cast<std::uint32_t>(k)*moby_size);
}
void RetailScripts::mirror(Game& game,RetailObject& o,std::uint32_t m,bool fresh) {
    auto& vm=*vm_;
    const auto type=vm.read16(m+0x36);
    if(type!=o.type){o.type=type;fresh=true;}
    const auto position=retail::to_world({s32(vm,m+0xc),s32(vm,m+0x10),s32(vm,m+0x14)});
    const std::array<float,3> rotation{float(vm.read8(m+0x44)),float(vm.read8(m+0x45)),float(vm.read8(m+0x46))};
    if(fresh){o.position=o.previous_position=position;o.rotation=o.previous_rotation=rotation;o.interpolate_position=false;}
    else {begin_object_transform(o,2);o.position=position;o.rotation=rotation;}
    if(o.id<written_.size())written_[o.id]=position;
    const auto state=static_cast<std::int8_t>(vm.read8(m+0x48));
    o.state=state<0?std::int8_t(-1):state;
    o.size=vm.read8(m+0x4f);o.material_color=vm.read32(m+0x54);
    o.script_undrawn=static_cast<std::int8_t>(vm.read8(m+0x4c))<=0;
    o.previous_animation=fresh?ObjectAnimationState{}:o.animation;
    ObjectAnimationState next{vm.read8(m+0x3c),vm.read8(m+0x3d),vm.read8(m+0x3e),vm.read8(m+0x3f),vm.read8(m+0x40),vm.read8(m+0x42),vm.read8(m+0x41)!=0};
    o.model=next.clip;
    const auto* models=game.object_models();
    const auto bank=models->models.find(type);
    if(bank!=models->models.end() && bank->second.animated) {
        const auto& clips=bank->second.animations;
        // With a zero blend the original renderer never reads the next frame;
        // single-frame clips keep a stale +3F index (e.g. Colossus class 117).
        if(next.next_clip<clips.size() && next.next_frame>=clips[next.next_clip].frames.size() && !(next.blend&0xf0)){next.next_clip=next.clip;next.next_frame=next.frame;}
        const bool valid=next.clip<clips.size() && next.next_clip<clips.size() && next.frame<clips[next.clip].frames.size() &&
            next.next_frame<clips[next.next_clip].frames.size();
        if(valid)o.animation=next;
        else if(std::find(invalid_animation.begin(),invalid_animation.end(),type)==invalid_animation.end())invalid_animation.push_back(type);
        if(fresh)o.previous_animation=o.animation;
    }
}
void RetailScripts::sync_out(Game& game) {
    auto& vm=*vm_;
    for(const auto i:owned_)mirror(game,game.retail_objects[i],mobys_+i*moby_size,false);
    // Mobys created by the scripts (projectiles, effects, rewards) live after
    // the authored array; 8004132c marks retired ones -2. The list ends at the
    // state -1 marker: 80066fd4 is the lowest free slot (80041290 allocates
    // there and moves it to the next hole), so live mobys can follow it - the
    // Colossus hockey puck (a 113 spawned in state 99) sits after a hole that
    // Kipp's own spawn left, and was never mirrored or drawn.
    std::uint32_t end=initial_end_;
    while(end<initial_end_+512*moby_size && static_cast<std::int8_t>(vm.read8(end+0x48))!=-1)end+=moby_size;
    if(end>initial_end_ && slots_.size()<(end-initial_end_)/moby_size)slots_.resize((end-initial_end_)/moby_size,UINT32_MAX);
    for(std::size_t k=0;k<slots_.size();++k) {
        const auto m=initial_end_+static_cast<std::uint32_t>(k)*moby_size;
        const auto state=m<end?static_cast<std::int8_t>(vm.read8(m+0x48)):std::int8_t(-2);
        auto& slot=slots_[k];
        if(state<0) {
            // Retired: release the mapping too. The scripts reuse freed slots for
            // new spawns; a stale mapping to the dead engine object would make
            // push() retire the fresh moby behind its owner's back (Shady Oasis
            // 186 then walks a circular 187 chain forever).
            if(slot!=UINT32_MAX && game.retail_objects[slot].state>=0)game.retail_objects[slot].state=-1;
            slot=UINT32_MAX;
            continue;
        }
        const auto type=vm.read16(m+0x36);
        if(type==1 && game.object_models()) {
            // A scripted gem reward: hand it to the native gem system.
            const auto& models=*game.object_models();auto value=static_cast<unsigned>(vm.read8(m+0x50));
            // A gem bursting from a chest (80039334, launched by class 80 gem
            // riders) flies in state 2 with only its variant at +49; its
            // value byte is written when it lands.
            if(state==2 && !value && vm.read8(m+0x49)<5)value=models.gem_values[vm.read8(m+0x49)];
            if(value<models.gem_variants.size()) {
                RetailObject gem;gem.type=1;gem.runtime_spawned=true;gem.script_gem=true;gem.draw_flags=0x18;gem.source_id=UINT32_MAX;gem.held_gem=static_cast<std::uint8_t>(value);
                gem.variant=static_cast<std::uint8_t>(models.gem_variants[value]);
                if(gem.variant<5 && models.gem_values[gem.variant]==value) {
                    gem.model=static_cast<std::uint8_t>(models.gem_meshes[gem.variant]);gem.material_color=models.gem_colors[gem.variant];
                    gem.gem_value=value;gem.state=2;gem.bounces=3;gem.id=static_cast<unsigned>(game.retail_objects.size());
                    gem.position=gem.previous_position=retail::to_world({s32(vm,m+0xc),s32(vm,m+0x10),s32(vm,m+0x14)+256});
                    gem.script_velocity={0,0,140};
                    game.retail_objects.push_back(std::move(gem));++gems;
                }
            }
            vm.call(remove_moby,std::array<std::uint32_t,1>{m});
            continue;
        }
        if((type==0x10 || type==0xd3) && state!=2 && game.object_models() && game.object_models()->models.contains(type)) {
            // Fodder butterflies (and the extra-life 0xD3) belong to Sparx's
            // native feeding code once released. A jar's state-2 D3 must
            // stay in RAM: class 233 keeps its pointer and writes state 0
            // there on a normal hit (80075078..8007509c / 800751a0).
            RetailObject food;food.type=type;food.runtime_spawned=true;food.source_id=UINT32_MAX;food.held_gem=255;food.draw_flags=0x10;
            food.id=slot==UINT32_MAX?static_cast<unsigned>(game.retail_objects.size()):slot;
            const auto source=vm.read8(m+0x3a);if(source<initial_)food.source_id=source;
            food.position=food.previous_position=retail::to_world({s32(vm,m+0xc),s32(vm,m+0x10),s32(vm,m+0x14)});
            food.animation=food.previous_animation=start_object_animation(game.object_models()->models.at(type));
            food.butterfly=ButterflyState{food.position};
            if(slot==UINT32_MAX)game.retail_objects.push_back(std::move(food));
            else {game.retail_objects[slot]=std::move(food);slot=UINT32_MAX;}
            vm.call(remove_moby,std::array<std::uint32_t,1>{m});
            continue;
        }
        if(slot==UINT32_MAX) {
            RetailObject object;object.id=static_cast<unsigned>(game.retail_objects.size());object.type=type;
            object.runtime_spawned=true;object.retail_script=true;object.held_gem=255;object.persistence=255;
            object.scripted_transient=true;
            slot=object.id;game.retail_objects.push_back(std::move(object));written_.resize(game.retail_objects.size());++spawned;
            mirror(game,game.retail_objects[slot],m,true);
        } else {
            auto& o=game.retail_objects[slot];
            mirror(game,o,m,o.state<0);
        }
    }
    present_carried(game);
}
// A carried moby hides itself (a diode's state 1 zeroes +4C) and stays where
// it was picked up; the PlayStation shows nothing in Spyro's mouth. The PC
// draws it there instead, so the player can see what he holds. Only the
// engine copy moves: the RAM moby keeps its place until it is spat out.
void RetailScripts::present_carried(Game& game) {
    auto& vm=*vm_;
    const auto head=vm.read32(head_state_address);
    if(head<3 || head>5)return;
    const auto m=vm.read32(carried_moby);
    unsigned index=UINT32_MAX;
    if(m>=mobys_ && m<initial_end_ && (m-mobys_)%moby_size==0)index=(m-mobys_)/moby_size;
    else if(m>=initial_end_ && (m-initial_end_)%moby_size==0 && (m-initial_end_)/moby_size<slots_.size())index=slots_[(m-initial_end_)/moby_size];
    if(index>=game.retail_objects.size())return;
    auto& o=game.retail_objects[index];
    const auto mouth=game.elements.mouth();
    if(length(mouth-game.player.position)>3)return;
    // Hold the model by its middle, just ahead of the jaws, at 70% so it fits
    // between them (size byte +4F: the renderer scales by 32/size).
    constexpr float held_scale=.7f;
    o.size=static_cast<std::uint8_t>(std::lround(32/held_scale));
    Vec3 middle{};float radius=.2f;
    if(const auto* models=game.object_models())if(const auto bank=models->models.find(o.type);bank!=models->models.end()) {
        Bounds bounds;
        if(bank->second.animated) {
            const auto pose=sample_object_animation(bank->second,o.animation,models->animation_blends);
            for(const auto v:pose.vertices)bounds.add(v*held_scale);
        } else if(o.model<bank->second.meshes.size())for(const auto v:bank->second.meshes[o.model].vertices)bounds.add(v*held_scale);
        if(bounds.max.x>=bounds.min.x){middle=bounds.center();radius=std::max({bounds.max.x-bounds.min.x,bounds.max.y-bounds.min.y,bounds.max.z-bounds.min.z})*.5f;}
    }
    const auto forward=retail::rotate_model({0,0,1},game.player.yaw,0,0);
    o.position=mouth+forward*(radius*.55f)-Vec3{0,radius*.35f,0}-middle;
    o.rotation[2]=static_cast<float>(static_cast<int>(std::lround(retail::original_heading(game.player.yaw)))>>4&255);
    o.script_undrawn=false;
    if(index<written_.size())written_[index]=o.position;
}
// 8003dc64's moby half: a moby whose collision bank (+08) has a triangle
// section (header bits 16..22 = vertex count, 24..31 = section words) is
// solid. Its packed vertices come from the current animation frame (blended
// with the next one by 80062868[+40], as GTE INTPL does), rotated by the
// moby matrix +20..+33 (MVMVA, the result shifted by the clip's +02) and
// placed at +0C. Each triangle word packs three 6-bit vertex indices
// (bits 24, 16, 8) and a flag byte; bit 31 ends the list.
void RetailScripts::build_solids(Game& game) {
    auto& vm=*vm_;auto& level=game.level;
    level.object_triangles.clear();level.object_colliders.clear();solid_flags_.clear();
    const auto valid=[](std::uint32_t a){return a>=0x80000000u && a<0x80800000u;};
    const auto add=[&](unsigned index,std::uint32_t m) {
        if(static_cast<std::int8_t>(vm.read8(m+0x48))<0 || static_cast<std::int16_t>(vm.read16(m+0x34))<0)return;
        const auto bank=vm.read32(m+8);
        if(!valid(bank))return;
        const auto header=vm.read32(bank);const unsigned vertices=(header>>16)&0x7f,words=header>>24;
        if(!vertices || !words || vertices>64)return;
        const auto table=vm.read32(class_table+vm.read16(m+0x36)*4u);
        if(!valid(table))return;
        const auto anim=vm.read32(m+0x3c);
        const auto clip=vm.read32(table+(anim&0xff)*4+0x3c);
        if(!valid(clip))return;
        const auto frame_vertices=[&](std::uint32_t c,unsigned frame){return (vm.read32(c+frame*8+0x24)&0xffff)*2+vm.read32(c+0x20);};
        const auto base=frame_vertices(clip,(anim>>16)&0xff);
        const int blend=vm.read8(blend_weights+vm.read8(m+0x40))<<4;
        std::uint32_t next=0;
        if(blend) {
            const auto next_clip=vm.read32(table+((anim>>8)&0xff)*4+0x3c);
            if(!valid(next_clip))return;
            next=frame_vertices(next_clip,(anim>>24)&0xff);
        }
        if(!valid(base) || (blend && !valid(next)))return;
        const auto shift=static_cast<std::uint32_t>(static_cast<std::int8_t>(vm.read8(clip+2)))&31;
        std::array<std::int64_t,9> r{};
        {
            const auto lo=[&](std::uint32_t a){return std::int64_t(static_cast<std::int16_t>(vm.read16(a)));};
            r={lo(m+0x20),lo(m+0x22),lo(m+0x24),lo(m+0x26),lo(m+0x28),lo(m+0x2a),lo(m+0x2c),lo(m+0x2e),lo(m+0x30)};
        }
        const std::int32_t mx=s32(vm,m+0xc),my=s32(vm,m+0x10),mz=s32(vm,m+0x14);
        std::array<Vec3,64> world{};Bounds bounds;
        const auto unpack=[](std::uint32_t w){return std::array<std::int64_t,3>{static_cast<std::int32_t>(w)>>21,static_cast<std::int32_t>(w<<10)>>21,static_cast<std::int32_t>(w<<20)>>20};};
        const auto scaled=[&](std::int64_t mac){return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(mac))<<shift);};
        for(unsigned v=0;v<vertices;++v) {
            auto [a,b,c]=unpack(vm.read32(base+v*4));
            if(blend) {
                const auto [a2,b2,c2]=unpack(vm.read32(next+v*4));
                a=((a<<12)+blend*(a2-a))>>12;b=((b<<12)+blend*(b2-b))>>12;c=((c<<12)+blend*(c2-c))>>12;
            }
            // MVMVA RT*(IR1,IR2,IR3) with IR1=b, IR2=c, IR3=a.
            const auto mac1=(r[0]*b+r[1]*c+r[2]*a)>>12,mac2=(r[3]*b+r[4]*c+r[5]*a)>>12,mac3=(r[6]*b+r[7]*c+r[8]*a)>>12;
            world[v]=retail::to_world({scaled(mac3)+mx,my-scaled(mac1),mz-scaled(mac2)});
            bounds.add(world[v]);
        }
        Level::ObjectCollider collider{index,m,bounds,static_cast<std::uint32_t>(level.object_triangles.size()),0};
        for(unsigned k=0;k+1<words;++k) {
            const auto w=vm.read32(bank+4+k*4);
            if(w&0x80000000u)break;
            const unsigned ia=(w>>24)&0x3f,ib=(w>>16)&0x3f,ic=(w>>8)&0x3f;
            if(ia>=vertices || ib>=vertices || ic>=vertices)continue;
            Triangle t{world[ia],world[ib],world[ic]};t.source_id=index;
            level.object_triangles.push_back(t);solid_flags_.push_back(static_cast<std::uint8_t>(w&0xff));++collider.count;
        }
        if(!collider.count)return;
        collider.bounds.min=collider.bounds.min-Vec3{.01f,.01f,.01f};collider.bounds.max=collider.bounds.max+Vec3{.01f,.01f,.01f};
        level.object_colliders.push_back(collider);
    };
    for(const auto i:owned_)add(i,mobys_+i*moby_size);
    for(std::size_t k=0;k<slots_.size();++k)if(slots_[k]!=UINT32_MAX)add(slots_[k],initial_end_+static_cast<std::uint32_t>(k)*moby_size);
    solid_mobys=static_cast<unsigned>(level.object_colliders.size());solid_triangles=static_cast<unsigned>(level.object_triangles.size());
}
// Scripted terrain animation (doors, gates, bridges, lifts, lights): adopt
// each RAM timeline record's flags (+1), node (+2) and frames left (+3).
void RetailScripts::sync_terrain(Game& game) {
    auto& vm=*vm_;bool changed=false;
    for(auto& morph:game.level.terrain_morphs) {
        if(!morph.scripted || morph.kind>=7)continue;
        const auto directory=terrain_directories+terrain_directory[morph.kind]*8;
        if(morph.record>=vm.read32(directory))continue;
        const auto r=vm.read32(vm.read32(directory+4)+morph.record*4);
        if(r<0x80000000u || r>=0x80800000u)continue;
        changed|=pose_scene_morph(game.level,morph,vm.read8(r+1),vm.read8(r+2),vm.read8(r+3));
    }
    if(changed){++game.level.terrain_revision;++terrain_changes;}
}
void RetailScripts::terrain_command(std::uint32_t function,std::uint32_t group,std::uint32_t a,std::uint32_t b) {
    auto& vm=*vm_;vm.r[29]=retail_stack_top;
    vm.call(function,std::array<std::uint32_t,3>{group,a,b},100'000);++door_commands;
}
// Native classes whose original handlers move terrain groups (overlay scan
// of 8002365c/80023710 callers; the handlers are shared by the homeworlds):
// - Moneybags 77 (+00 subtype, +28 group, +2C camera record, +3E open node,
//   +48 purchase): paid at load, the group is set to its open node (subtype 4
//   also sets group 9 to node 1C); paid now, a look shot from the record's
//   eye (+0C) to target (+1C), then after 30 ticks the group starts (and
//   group 9 for subtype 4), held 90 ticks.
// - Orb doors 16D (+3C group) and 1B6 (+38 group): the group starts when the
//   door opens (the engine enables the gated portal), at load as well.
// (The scan's other native hits, Skelos C9 and Fracture Hills CA, fall in
// dispatch ranges that span other classes' code; those callers are scripts.)
void RetailScripts::native_doors(Game& game) {
    auto& vm=*vm_;const auto slot=game.level.wad_slot;
    const bool homeworld=slot==15 || slot==33 || slot==57;
    const auto params=[&](unsigned i){return vm.read32(mobys_+i*moby_size);};
    const auto valid=[](std::uint32_t a){return a>=0x80000000u && a<0x80800000u;};
    const auto open_portal=[&](unsigned object)->std::optional<bool> {
        for(const auto& p:game.level.portals)if(p.gate_npc==object)return p.enabled;
        return std::nullopt;
    };
    if(!doors_started_) {
        doors_started_=true;
        for(unsigned i=0;i<initial_ && i<game.retail_objects.size();++i) {
            const auto type=game.retail_objects[i].type;
            if(!valid(params(i)))continue;
            if(homeworld && (type==0x77 || type==0x16d || (type==0x1b6 && slot!=15)))native_doors_.push_back({i});
        }
        for(auto& d:native_doors_) {
            const auto& o=game.retail_objects[d.object];const auto p=params(d.object);
            if(o.type==0x77) {
                const auto purchase=s32(vm,p+0x48),group=s32(vm,p+0x28);
                if(purchase<0 || purchase>=static_cast<std::int32_t>(purchase_count) || !((game.purchases>>purchase)&1))continue;
                d.paid=true;
                if(group!=-1){terrain_command(0x8002365c,group,0xfc,0);terrain_command(0x80023710,group,static_cast<std::uint32_t>(static_cast<std::int16_t>(vm.read16(p+0x3e))),0x40);}
                if(vm.read8(p)==4){terrain_command(0x8002365c,9,0xfc,0);terrain_command(0x80023710,9,0x1c,0x40);}
            }
        }
    }
    for(auto& d:native_doors_) {
        const auto& o=game.retail_objects[d.object];const auto p=params(d.object);
        if(o.type==0x77) {
            const auto purchase=s32(vm,p+0x48),group=s32(vm,p+0x28);
            const auto camera=vm.read32(p+0x2c);
            if(!d.paid && purchase>=0 && purchase<static_cast<std::int32_t>(purchase_count) && ((game.purchases>>purchase)&1)) {
                d.paid=true;
                if(group==-1)continue;
                if(valid(camera)){vm.r[29]=retail_stack_top;vm.call(camera_look,std::array<std::uint32_t,2>{camera+0xc,camera+0x1c},100'000);d.stage=1;d.timer=30;}
                else {terrain_command(0x8002365c,group,0xfc,0);d.stage=0;}
            } else if(d.stage && d.timer && !--d.timer) {
                if(d.stage==1) {
                    if(vm.read8(p)==4)terrain_command(0x8002365c,9,0xfc,0);
                    terrain_command(0x8002365c,group,0xfc,0);
                    if(const auto subtype=vm.read8(p);subtype==4 || subtype==15)game.sound_events.push_back({SoundScope::Object,subtype==4?0x12u:0xfu,0x77,o.position});
                    d.stage=2;d.timer=90;
                } else {vm.r[29]=retail_stack_top;vm.call(0x8001f2e4,std::array<std::uint32_t,1>{0},100'000);d.stage=0;}
            }
        } else if(o.type==0x16d || o.type==0x1b6) {
            const auto group=s32(vm,p+(o.type==0x16d?0x3c:0x38));
            auto open=open_portal(d.object);
            if(!open && o.type==0x16d) {
                // Boss doors with no portal of their own: Elora lets Spyro on to
                // Crush with 6 talismans (subtype 5) and to Gulp with 14
                // (subtype 13); Winter Tundra's subtype 55 asks for 40 orbs
                // (8007684c; the handler compares 80067108 / 8006702c).
                const auto subtype=vm.read8(p);const auto talismans=static_cast<unsigned>(std::popcount(game.player.talismans));
                if(subtype==5)open=talismans>=6;
                else if(subtype==13)open=talismans>=14;
                else if(subtype==55)open=game.player.orbs>=40;
            }
            if(group==-1 || !open || d.paid==*open)continue;
            d.paid=*open;
            if(*open)terrain_command(0x8002365c,group,0xfc,0);
        }
    }
}
// What Spyro's sphere touched among the solid mobys (the level has already
// pushed him out of them). 8003dc64 ORs his attack bits (8006a128) into the
// moby's +18 and hurts through flagged triangles; 8002cc88's floor ray
// records the moby he stands on in 8006a134 (switch plates, lifts, scales).
void RetailScripts::note_solids(Game& game,Vec3 center,float radius,std::uint32_t attack) {
    auto& vm=*vm_;const auto& level=game.level;
    if(level.object_triangles.empty())return;
    const float reach=radius*1.08f+.02f;
    float best=-1e9f;
    level.near_object_triangles(center-Vec3{reach,reach,reach},center+Vec3{reach,reach,reach},[&](std::uint32_t k) {
        const auto& t=level.object_triangles[k];
        const auto p=closest_point(center,t);const auto d=center-p;const float distance=length(d);
        if(distance>reach)return;
        const auto* c=level.object_collider(Level::object_triangle|k);
        if(!c)return;
        ++solid_contacts;
        if(attack)touched_[c->moby]|=attack;
        if(const auto flag=solid_flags_[k];flag && flag<0x80)triangle_hurt_|=vm.read32(triangle_damage+flag*4u);
        if(distance>1e-5f && d.y/distance>0.55f && p.y>best) {
            best=p.y;standing_=c->moby;
            // Barycentric position of the support point, to carry Spyro when
            // the moby moves, turns or animates (lifts, turntables, tilting).
            const auto e1=t.b-t.a,e2=t.c-t.a,w=p-t.a;
            const float d11=dot(e1,e1),d12=dot(e1,e2),d22=dot(e2,e2),dw1=dot(w,e1),dw2=dot(w,e2),den=d11*d22-d12*d12;
            if(std::abs(den)>1e-10f)ride_={c->moby,k-c->first,(d22*dw1-d12*dw2)/den,(d11*dw2-d12*dw1)/den,p,true};
        }
    });
}
bool RetailScripts::attack_query(Vec3 center,float radius,std::uint32_t attack) {
    if(failed_ || !vm_ || !booted_ || !attack)return false;
    auto& vm=*vm_;
    const auto p=retail::from_world(center);
    vm.write32(flame_probe,static_cast<std::uint32_t>(p.x));vm.write32(flame_probe+4,static_cast<std::uint32_t>(p.y));vm.write32(flame_probe+8,static_cast<std::uint32_t>(p.z));
    vm.r[29]=retail_stack_top;vm.write32(retail_stack_top+16,attack);vm.write32(retail_stack_top+20,0);
    const auto radius_units=static_cast<std::uint32_t>(std::lround(radius*retail::units_per_world));
    const auto moby=vm.call(moby_query,std::array<std::uint32_t,4>{flame_probe,radius_units,0,0},400'000);
    if(!vm.fault.empty()){vm.fault.clear();return false;}
    if(moby<0x80000000u || moby>=0x80800000u)return false;
    attacks_seen|=attack;
    return true;
}
Vec3 RetailScripts::resolve_player(Game& game,Vec3 center,float radius,Vec3& velocity,bool& grounded,std::uint32_t attack) {
    if(failed_ || !vm_ || !booted_)return center;
    auto& vm=*vm_;
    note_solids(game,center,radius,attack);
    const auto p=retail::from_world(center);
    vm.write32(player_probe,static_cast<std::uint32_t>(p.x));vm.write32(player_probe+4,static_cast<std::uint32_t>(p.y));vm.write32(player_probe+8,static_cast<std::uint32_t>(p.z));
    vm.r[29]=retail_stack_top;vm.write32(retail_stack_top+16,attack);vm.write32(retail_stack_top+20,0);
    if(attack)attacks_seen|=attack;
    const auto radius_units=static_cast<std::uint32_t>(std::lround(radius*retail::units_per_world));
    const auto moby=vm.call(moby_query,std::array<std::uint32_t,4>{player_probe,radius_units,0,0},400'000);
    if(!vm.fault.empty() || moby<0x80000000 || moby>=0x80800000)return center;
    const auto pushed=retail::to_world({s32(vm,contact_point),s32(vm,contact_point+4),s32(vm,contact_point+8)});
    auto delta=pushed-center;
    // A body landing on Spyro must not press him through the floor: keep only
    // the sideways part of a downward push, and never push through terrain.
    if(delta.y<0 && -delta.y>std::hypot(delta.x,delta.z))delta.y=0;
    if(const auto hit=game.level.raycast(center,delta,length(delta)+radius);hit)
        delta=delta*std::clamp((hit->distance-radius*0.5f)/std::max(length(delta),1e-5f),0.0f,1.0f);
    const auto distance=length(delta);
    if(distance<1e-5f || distance>radius*1.5f)return center;
    const auto pushed_center=center+delta;
    const auto normal=delta/distance;
    contact_=moby;
    const auto inward=dot(velocity,normal);if(inward<0)velocity=velocity-normal*inward;
    if(normal.y>0.5f) {
        grounded=true;
        support_=moby;support_position_=retail::to_world({s32(vm,moby+0xc),s32(vm,moby+0x10),s32(vm,moby+0x14)});
    }
    return pushed_center;
}
// Spyro carries objects in his mouth through his head state (8002fc20).
// Hurricos' diodes (class 34) ask for it: in their state 0, facing Spyro
// from 600..850 units, they post 8006a1fc = 10000001 and themselves in
// 8006a208 while the head state is 0.
// - 0: the request is accepted where the state allows a pickup (80061371):
//   head state 2, the moby in 8006a138 (8002ffd4).
// - 2: Spyro turns to it; after 16 frames the moby is in his mouth (state 1,
//   which hides it) and the head state is 3 (800302f0).
// - 3: carrying. A state that cannot carry (80061372: hurt, underwater,
//   headbash) parks it in 5 until it can again. Circle (or request bit 200)
//   in a state that may spit (80061373) starts the spit, 4 (80030410).
// - 4: after 4 frames the moby is spat out (state 2) and the head is free
//   (800305dc). The diode then flies an arc and homes in on the nearest
//   socket (class 35) near where it would land.
// The engine's flame is the retail head state 1; it is never written here,
// and the engine holds its flame while Spyro carries (carrying()).
void RetailScripts::carry(Game& game,std::uint32_t pressed) {
    auto& vm=*vm_;
    const auto request=vm.read32(spyro_request);
    vm.write32(request_seen,request);vm.write32(spyro_request,0);
    auto head=vm.read32(head_state_address),timer=vm.read32(head_timer);
    const auto state=vm.read32(spyro_state);
    const auto may=[&](unsigned column){return state<0x40 && vm.read8(state_abilities+state*4+column)!=0;};
    const auto valid=[](std::uint32_t a){return a>=0x80000000u && a<0x80800000u;};
    auto carried=vm.read32(carried_moby);
    if(head>=2 && head<=5 && (!valid(carried) || static_cast<std::int8_t>(vm.read8(carried+0x48))<0)) {
        head=0;carried=0;vm.write32(carried_moby,0);
    }
    switch(head) {
    case 0:
        if((request&0x10000001u)==0x10000001u && may(1) && !game.interactions.flame_active() && valid(vm.read32(request_moby))) {
            head=2;timer=0;carried=vm.read32(request_moby);vm.write32(carried_moby,carried);
        }
        break;
    case 2:
        if(timer>=16) {
            game.sound_events.push_back({SoundScope::Global,4,0,{}});
            head=3;timer=0;vm.write8(carried+0x48,1);++pickups;
        }
        break;
    case 3:
        if(!may(2))head=5;
        else if(((pressed&0x20) || (request&0x200)) && may(3)){head=4;timer=0;}
        break;
    case 4:
        if(timer>=4) {
            game.sound_events.push_back({SoundScope::Global,5,0,{}});
            head=0;timer=0;vm.write8(carried+0x48,2);vm.write32(carried_moby,0);++spits;
        }
        break;
    case 5:
        if(may(2))head=3;
        break;
    default:break;
    }
    vm.write32(head_state_address,head);
    // The timer counts frames; a script tick is two.
    vm.write32(head_timer,timer+2);
}
unsigned RetailScripts::head_state()const {
    if(!vm_ || !booted_ || failed_)return 0;
    const auto head=vm_->read32(head_state_address);
    return head<8?head:0u;
}
bool RetailScripts::carrying()const {
    if(!vm_ || !booted_ || failed_)return false;
    const auto head=vm_->read32(head_state_address);
    return head>=2 && head<=5;
}
void RetailScripts::tick(Game& game) {
    if(!booted_)boot(game);
    if(failed_ || !vm_)return;
    for(const auto i:owned_){auto& o=game.retail_objects[i];o.transform_phase=std::min(o.transform_phase+1,o.transform_span);}
    for(const auto slot:slots_)if(slot!=UINT32_MAX){auto& o=game.retail_objects[slot];o.transform_phase=std::min(o.transform_phase+1,o.transform_span);}
    if(game.interactions.flame_step()!=flame_step_) {
        flame_step_=game.interactions.flame_step();
        const auto& points=game.interactions.flame_query_points();
        if(flames_.size()<64)flames_.insert(flames_.end(),points.begin(),points.end());
    }
    if((clock_++&1)!=0)return;
    if(place_.active && (game.dialogue.active() || dialogue_moby_))advance_place(game,*vm_);
    if(game.dialogue.active() || game.scene_locked || dialogue_moby_)return;
    if(restart_.stage){advance_restart(game,*vm_);return;}   // game state 3: the level logic waits
    auto& vm=*vm_;current_=&game;
    vm.write32(level_frames,vm.read32(level_frames)+1);
    sync_in(game);
    sync_progress_in(game);
    vm.write32(pad_held,pad_held_);vm.write32(pad_pressed,pad_pressed_);vm.write32(pad_released,pad_previous_&~pad_held_);
    // Spyro's update runs before the mobys': it answers the requests they
    // posted last tick (a diode asking to be picked up).
    carry(game,pad_pressed_);
    pad_previous_=pad_held_;pad_pressed_=0;
    const auto before=vm.executed;
    // 8002f08c: each advancing flame point queries the moby spheres with
    // radius A0 and attack bit 10000 (the engine's mesh test misses points
    // inside a body, e.g. breathing while pressed against an enemy).
    for(const auto point:flames_) {
        const auto p=retail::from_world(point);
        vm.write32(flame_probe,static_cast<std::uint32_t>(p.x));vm.write32(flame_probe+4,static_cast<std::uint32_t>(p.y));vm.write32(flame_probe+8,static_cast<std::uint32_t>(p.z));
        vm.r[29]=retail_stack_top;vm.write32(retail_stack_top+16,flame_attack);vm.write32(retail_stack_top+20,0);
        vm.call(moby_query,std::array<std::uint32_t,4>{flame_probe,0xa0,0,0},400'000);
    }
    flames_.clear();
    // 80031e4c clears Spyro's hurt word each frame; scripts OR damage bits
    // into it directly (e.g. Idol Springs 11D/126, Colossus 111/118).
    vm.write32(spyro_hurt,0);vm.write32(hit_moby,0);
    vm.r[29]=retail_stack_top;
    vm.call(dispatcher_,{},4'000'000);
    if(!vm.fault.empty()) {
        ++faults;fault=vm.fault;
        {   // Where it was: the PC and the last few PCs, for diagnosis.
            char text[32];std::snprintf(text,sizeof text," at %08x:",vm.pc);fault+=text;
            for(unsigned k=8;k<16;++k){std::snprintf(text,sizeof text," %08x",vm.history[(vm.history_at+k)&15]);fault+=text;}
        }
        if(faults>8){failed_=true;current_=nullptr;return;}
    }
    direct_camera(game,vm);
    // The main loop's terrain timeline: scripts start groups (8002365c
    // flags, 80023710 node) and wait on their nodes; the engine poses them.
    native_doors(game);
    vm.r[29]=retail_stack_top;vm.call(terrain_timeline,std::array<std::uint32_t,1>{2},400'000);
    if(!vm.fault.empty()){++faults;fault="terrain timeline: "+vm.fault;}
    else sync_terrain(game);
    // A Zoe stored a checkpoint: Spyro now returns there when he loses a life.
    if(const auto now=checkpoint_words(vm);now!=checkpoint_seen_) {
        checkpoint_seen_=now;
        if(now[0]==1) {
            game.play_scene.spawn=retail::to_world({static_cast<std::int32_t>(now[1]),static_cast<std::int32_t>(now[2]),
                static_cast<std::int32_t>(now[3])})-retail::origin_above_feet;
            game.respawn_yaw=retail::native_heading(static_cast<float>(now[4]&0xfff));++checkpoints;
        }
    }
    present_bolts(game,vm);
    run_hud(game,vm);
    // 8004c1ac -> 80017164: restart at the checkpoint (game state 3).
    if(vm.read32(game_state)==3 && !restart_.stage){restart_={1,0,0};++checkpoint_restarts;}
    // Spyro's own moby query (8002c0f4) adds the touched collision spheres'
    // damage bits; 8002b0b8 then hurts on mask 1F9.
    const auto scripted=vm.read32(spyro_hurt);
    vm.r[29]=retail_stack_top;vm.write32(retail_stack_top+16,spyro_attack(game));vm.write32(retail_stack_top+20,0);
    vm.call(moby_query,std::array<std::uint32_t,4>{spyro_origin,retail::spyro_origin_units,0,spyro_hurt},400'000);
    const auto hurt=vm.read32(spyro_hurt)|scripted|triangle_hurt_;triangle_hurt_=0;
    instructions+=vm.executed-before;++script_ticks;
    sync_out(game);
    build_solids(game);
    if(ride_.active) {
        // Spyro stood on a solid moby's triangle: carry him with that point.
        ride_.active=false;
        for(const auto& c:game.level.object_colliders)if(c.moby==ride_.moby && ride_.offset<c.count) {
            const auto& t=game.level.object_triangles[c.first+ride_.offset];
            const auto moved=t.a+(t.b-t.a)*ride_.u+(t.c-t.a)*ride_.v-ride_.point;
            if(length(moved)>1e-5f && length(moved)<1.5f && game.player.grounded){game.player.position+=moved;++solid_rides;}
            if(support_==ride_.moby)support_=0;
            break;
        }
    }
    // A script opened a conversation (8003a98c saw triangle, then 80015be4):
    // the engine presents it while the original logic keeps driving it.
    if(vm.read32(game_state)==1) {
        const auto m=vm.read32(dialogue_moby);
        if(m>=mobys_ && m<initial_end_ && (m-mobys_)%moby_size==0) {
            dialogue_moby_=m;++dialogues;
            if(const auto line=dialogue_line())script_dialogue_=std::pair{(m-mobys_)/moby_size,*line};
            else end_dialogue(game);
        } else {vm.write32(game_state,0);vm.write32(dialogue_running,0);}
    }
    // Orbs a script's own opening line set are presented when the
    // conversation ends (80016328 state 3), not before it is shown.
    if(!dialogue_moby_)sync_progress_out(game);
    capture_memory();
    if(support_) {
        // Standing on a moving script moby (lift, raft, platform): ride it.
        const auto now=retail::to_world({s32(vm,support_+0xc),s32(vm,support_+0x10),s32(vm,support_+0x14)});
        const auto moved=now-support_position_;
        if(static_cast<std::int8_t>(vm.read8(support_+0x48))>=0 && length(moved)<1.5f && game.player.grounded)game.player.position+=moved;
        support_=0;
    }
    if(hurt&0x1f9) {
        auto source=vm.read32(hit_moby);
        if(source<0x80000000 || source>=0x80800000) {
            // A script wrote the bits itself: blame the nearest active moby.
            float best=1e9f;source=0;
            for(auto a=active_list;a<active_list+0x7f0 && vm.read32(a);a+=4) {
                const auto m=vm.read32(a);
                const auto d=length(retail::to_world({s32(vm,m+0xc),s32(vm,m+0x10),s32(vm,m+0x14)})-game.player.position);
                if(d<best){best=d;source=m;}
            }
        }
        Vec3 from=game.player.position-Vec3{std::sin(game.player.yaw),0,std::cos(game.player.yaw)};
        if(source)from=retail::to_world({s32(vm,source+0xc),s32(vm,source+0x10),s32(vm,source+0x14)});
        if(game.hurt(from))++hits;
    }
    current_=nullptr;
}
} // namespace spyro
