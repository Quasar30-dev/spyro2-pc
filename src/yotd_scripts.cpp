#include "spyro/yotd_scripts.hpp"
#include <cstdlib>
#include <fstream>
#include "spyro/game_build.hpp"
#include "spyro/overlay_vm.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace spyro {
namespace {
constexpr std::uint32_t overlay=0x800742d0,scene=0x80200000,models=0x80300000,gameplay=0x80400000,
    stack=0x807fe000,stride=88,moby_array=0x8006c550,moby_limit=0x8006c5ec,
    active_builder=0x80055680,active_list=0x8006e788,dispatcher=0x8003038c,
    remove_moby=0x80055b18,spyro_origin=0x80070328,camera_origin=0x8006e020,
    moby_query=0x8001ba30,spyro_hurt=0x8007034c;
std::uint32_t word(std::span<const std::uint8_t> b,std::size_t p) {
    if(p>b.size() || b.size()-p<4)throw std::runtime_error("YOTD script buffer is truncated");
    std::uint32_t v;std::memcpy(&v,b.data()+p,4);return v;
}
std::uint16_t half(std::span<const std::uint8_t> b,std::size_t p) {
    if(p>b.size() || b.size()-p<2)throw std::runtime_error("YOTD model class directory is truncated");
    std::uint16_t v;std::memcpy(&v,b.data()+p,2);return v;
}
bool address(std::uint32_t p,std::uint32_t size=1){return p>=0x80000000 && p<=0x80800000-size;}
std::int32_t signed_word(OverlayVm& v,std::uint32_t p){return static_cast<std::int32_t>(v.read32(p));}
Vec3 position(OverlayVm& v,std::uint32_t p){return retail::to_world({signed_word(v,p),signed_word(v,p+4),signed_word(v,p+8)});}
void write_position(OverlayVm& v,std::uint32_t p,Vec3 value){const auto q=retail::from_world(value);v.write32(p,q.x);v.write32(p+4,q.y);v.write32(p+8,q.z);}
bool enemy_class(unsigned type){return type==0xfd || type==0xee;}
}

YotdScripts::YotdScripts(const YotdScripts& other){*this=other;}
YotdScripts& YotdScripts::operator=(const YotdScripts& other) {
    if(this==&other)return *this;
    auto copied=other.vm_?std::make_shared<OverlayVm>(*other.vm_):nullptr;
    instructions=other.instructions;script_ticks=other.script_ticks;faults=other.faults;attacks_seen=other.attacks_seen;
    spawned=other.spawned;removed=other.removed;hits=other.hits;fault=other.fault;
    attempted_=other.attempted_;failed_=other.failed_;clock_=other.clock_;initial_=other.initial_;authored_=other.authored_;
    mobys_=other.mobys_;initial_end_=other.initial_end_;spawn_end_=other.spawn_end_;
    owned_=other.owned_;slots_=other.slots_;reward_owners_=other.reward_owners_;written_=other.written_;
    current_=nullptr;vm_=std::move(copied);
    if(vm_){vm_->trace=nullptr;vm_->executed_pcs=nullptr;vm_->hle.erase(active_builder);bind_hooks();}
    return *this;
}
std::uint32_t YotdScripts::peek(std::uint32_t at,unsigned size)const {
    if(!vm_ || (size!=1 && size!=2 && size!=4) || !address(at,size))return 0;
    return size==1?vm_->read8(at):size==2?vm_->read16(at):vm_->read32(at);
}
void YotdScripts::poke(std::uint32_t at,std::uint32_t value,unsigned size) {
    if(!vm_ || (size!=1 && size!=2 && size!=4) || !address(at,size))return;
    if(size==1)vm_->write8(at,static_cast<std::uint8_t>(value));else if(size==2)vm_->write16(at,static_cast<std::uint16_t>(value));else vm_->write32(at,value);
}
void YotdScripts::bind_hooks() {
    vm_->hle[0x8003babc]=[this](OverlayVm& v) {
        const auto m=v.arg(0),cue=v.arg(1);v.r[2]=0;
        // The native animation-event path already presents this same cue.
        if(v.r[31]>=0x80055854 && v.r[31]<0x80055a7c)return;
        if(current_ && address(m,stride) && cue<128 && current_->sound_events.size()<256)
            current_->sound_events.push_back({SoundScope::Object,cue,v.read16(m+0x36),position(v,m+0xc)});
    };
    // Original reward spawn just returned: v0 is the new gem, s3 its parent.
    vm_->hle[0x80036ad4]=[this](OverlayVm& v) {
        v.passthrough=true;const auto parent=v.r[19],child=v.r[2];
        if(parent<mobys_ || parent>=initial_end_ || (parent-mobys_)%stride)return;
        const auto i=(parent-mobys_)/stride;
        if(std::find(owned_.begin(),owned_.end(),i)==owned_.end())return;
        if(!child || child<initial_end_ || child>=spawn_end_ || (child-initial_end_)%stride){v.fault="YOTD reward allocator returned an invalid moby";return;}
        reward_owners_[child]=i;
    };
}

std::shared_ptr<const YotdScriptImage> make_yotd_script_image(WadArchive& wad,std::span<const std::uint8_t> exe,unsigned slot,unsigned area) {
    if(game_profile(exe).build!=GameBuild::YearOfTheDragonUS || slot!=99 || area!=0)return {};
    auto image=std::make_shared<YotdScriptImage>();image->executable.assign(exe.begin(),exe.end());
    image->loader=wad.read(1);image->overlay=wad.read(slot+1);image->archive=wad.read(slot);
    image->slot=slot;image->area=area;image->level_id=11;return image;
}

void YotdScripts::fail(Game& game,std::string message) {
    ++faults;fault=std::move(message);failed_=true;current_=nullptr;
    std::vector<RetailObject> rewards;
    for(const auto i:owned_)if(i<game.retail_objects.size()) {
        auto& o=game.retail_objects[i];o.retail_script=false;o.script_undrawn=false;
        const auto original=vm_?static_cast<std::int8_t>(vm_->read8(mobys_+i*stride+0x48)):std::int8_t(0);
        if(o.enemy && (o.enemy->defeated || o.state==8 || original==8 || original<0)) {
            o.enemy->defeated=true;o.enemy->phase=EnemyPhase::Dying;if(o.state>=0)++removed;o.state=-1;
            const bool handed_off=std::any_of(game.retail_objects.begin(),game.retail_objects.end(),[&](const auto& q){return q.runtime_spawned && q.type==1 && q.source_id==o.id;});
            const auto* bank=game.object_models();const auto value=o.held_gem;
            if(!handed_off && bank && value!=255 && value<bank->gem_variants.size()) {
                const auto variant=bank->gem_variants[value];
                if(variant<5 && bank->gem_values[variant]==value) {
                    RetailObject gem;gem.source_game=GameBuild::YearOfTheDragonUS;gem.type=gem.source_type=1;gem.runtime_spawned=true;gem.source_id=o.id;
                    gem.held_gem=value;gem.variant=static_cast<std::uint8_t>(variant);gem.gem_value=value;gem.draw_flags=0x18;
                    gem.model=static_cast<std::uint8_t>(bank->gem_meshes[variant]);gem.material_color=bank->gem_colors[variant];
                    gem.position=gem.previous_position=o.position+Vec3{0,1,0};gem.state=2;gem.bounces=3;gem.script_velocity={0,0,140};rewards.push_back(std::move(gem));
                }
            }
        } else if(o.state>=0 && o.enemy)o.state=0;
    }
    for(auto& gem:rewards){gem.id=static_cast<unsigned>(game.retail_objects.size());game.retail_objects.push_back(std::move(gem));++spawned;}
    for(const auto i:slots_)if(i!=UINT32_MAX && i<game.retail_objects.size())game.retail_objects[i].state=-1;
    vm_.reset();
}
void YotdScripts::reset(Game& game) {
    for(const auto i:owned_)if(i<game.retail_objects.size())game.retail_objects[i].retail_script=false;
    *this=YotdScripts{};boot(game);
}
void YotdScripts::boot(Game& game) {
    attempted_=true;
    const auto image=game.level.yotd_script_image;
    if(!image || game.level.source_game!=GameBuild::YearOfTheDragonUS || image->slot!=99 || image->area!=0 || !game.object_models())return;
    try {
        if(game_profile(image->executable).build!=GameBuild::YearOfTheDragonUS)throw std::runtime_error("unverified YOTD executable");
        auto vm=std::make_shared<OverlayVm>();vm->r[29]=stack;
        if(const char* watch=std::getenv("SPYRO_YOTD_WATCH"))vm->watch=static_cast<std::uint32_t>(std::strtoul(watch,nullptr,16));   // diagnostics: report writes to this byte
        const auto& exe=image->executable;const auto load=word(exe,0x18),size=word(exe,0x1c);
        if(exe.size()<0x800 || size>exe.size()-0x800 || !address(load,size))throw std::runtime_error("invalid YOTD executable extent");
        vm->load(load,std::span(exe).subspan(0x800,size));
        // The only boot HLEs are original cache flush and sound reset. No
        // resident gameplay, collision, animation or random function is skipped.
        vm->hle[0x8005e0bc]=[](OverlayVm& v){v.r[2]=0;};
        vm->hle[0x80050b90]=[](OverlayVm& v){v.r[2]=0;};
        const auto call=[&](std::uint32_t entry,std::initializer_list<std::uint32_t> args) {
            const std::vector<std::uint32_t> a(args);vm->r[29]=stack;vm->call(entry,a,4'000'000);
            if(!vm->fault.empty()){char text[64];std::snprintf(text,sizeof text,"YOTD boot %08x at %08x: ",entry,vm->pc);throw std::runtime_error(text+vm->fault);}
        };
        const auto archive=std::span(image->archive);
        const auto section=[&](unsigned index) {
            const auto start=word(archive,index*8),length=word(archive,index*8+4);
            if(start>archive.size() || length>archive.size()-start)throw std::runtime_error("YOTD script section is out of bounds");
            return archive.subspan(start,length);
        };
        if(image->loader.size()>scene-overlay)throw std::runtime_error("YOTD loader exceeds code reservation");
        vm->load(overlay,image->loader);const auto scenery=section(1);
        if(scenery.size()>models-scene)throw std::runtime_error("YOTD scene exceeds VM reservation");
        vm->load(scene,scenery);call(0x80079744,{scene});
        const auto bank_bytes=section(2);const auto model_offset=word(archive,16);
        if(bank_bytes.size()>gameplay-models)throw std::runtime_error("YOTD models exceed VM reservation");
        vm->load(models,bank_bytes);
        for(unsigned i=1;i<63;++i) {
            const auto begin=word(archive,0xa0+i*4),end=word(archive,0xa0+(i+1)*4);if(!end)break;
            const auto type=half(archive,0x1a0+i*2);
            if(type>=1024 || begin<model_offset || end<=begin || end-model_offset>bank_bytes.size())throw std::runtime_error("invalid YOTD model bank extent");
            call(0x8002af9c,{models+begin-model_offset,type,models+end-model_offset});
        }
        if(image->overlay.size()<0x1000 || image->overlay.size()>scene-overlay)throw std::runtime_error("invalid YOTD code bank extent");
        const auto key=word(image->overlay,4);
        if((key&3) || key<overlay+0x1000 || key-overlay>image->overlay.size())throw std::runtime_error("invalid YOTD original decoder boundary");
        vm->load(overlay,image->overlay);vm->write32(0x800722b8,overlay);
        // Enter the proved resident decoder block; its original epilogue
        // restores RA from this caller-frame slot.
        vm->write32(stack+0x14,OverlayVm::return_trap);call(0x8002cf58,{});
        const auto code=std::span(vm->ram).subspan(overlay&0x1fffffff,image->overlay.size());
        // Diagnostics: SPYRO_YOTD_DUMP=file writes the decoded overlay (loaded at 800742d0)
        // for tools/mips_listing.py file:FILE@800742d0.
        if(const char* dump=std::getenv("SPYRO_YOTD_DUMP"))std::ofstream(dump,std::ios::binary).write(reinterpret_cast<const char*>(code.data()),static_cast<std::streamsize>(code.size()));
        const auto handlers=find_yotd_moby_handlers(code,overlay);
        if(!handlers.updates.contains(0xfd) || !handlers.updates.contains(0xee))throw std::runtime_error("YOTD verified enemy callbacks are absent");
        const auto objects=section(3);
        if(objects.size()>0x100000)throw std::runtime_error("YOTD gameplay exceeds VM reservation");
        vm->load(gameplay,objects);vm->write32(0x8006c5bc,image->level_id);vm->write32(0x8006c58c,1);vm->write32(0x8006c638,0x80700000);
        call(0x8002b810,{gameplay});
        mobys_=vm->read32(moby_array);spawn_end_=vm->read32(moby_limit);
        if(!address(mobys_,stride) || !address(spawn_end_) || spawn_end_<=mobys_ || (spawn_end_-mobys_)%stride)throw std::runtime_error("invalid YOTD moby pool");
        while(initial_<4096 && mobys_+initial_*stride<spawn_end_ && vm->read8(mobys_+initial_*stride+0x48)!=255)++initial_;
        initial_end_=mobys_+initial_*stride;
        // The loader (8002b810) builds the authored records first, in table
        // order, then appends its own mobys (Sunny Villa: three gems and two
        // D3s after 149 records). Those extras stay VM-only, like natives.
        authored_=static_cast<unsigned>(game.level.objects.size());
        if(initial_<authored_ || authored_>game.retail_objects.size())
            throw std::runtime_error("YOTD original/native moby counts differ (original "+std::to_string(initial_)+", native "+std::to_string(authored_)+")");
        for(unsigned i=0;i<initial_;++i) {
            const auto m=mobys_+i*stride;
            if(i>=authored_){vm->write32(m+8,0);continue;}
            auto& o=game.retail_objects[i];
            if(vm->read16(m+0x36)!=o.source_type)throw std::runtime_error("YOTD original/native moby order differs at "+std::to_string(i));
            if(enemy_class(o.source_type) && o.enemy && !o.collected && o.state>=0) {owned_.push_back(i);o.retail_script=true;}
            else vm->write32(m+8,0); // native actors stay outside original collision-sphere queries
        }
        if(owned_.empty())return;
        vm_=std::move(vm);written_.resize(game.retail_objects.size());
        for(const auto i:owned_){mirror(game,game.retail_objects[i],mobys_+i*stride,true);written_[i]=game.retail_objects[i].position;}
        bind_hooks();
        instructions=vm_->executed;
    } catch(const std::exception& e){fail(game,e.what());}
}

void YotdScripts::build_active(OverlayVm& vm) {
    // Filter the original builder's result; preserve its distance/group
    // decisions, while only audited actor classes enter the frame dispatcher.
    auto out=active_list;bool terminated=false;
    for(unsigned n=0;n<511;++n) {
        const auto m=vm.read32(active_list+n*4);if(!m){terminated=true;break;}
        if(m<mobys_ || m>=spawn_end_ || (m-mobys_)%stride){vm.fault="invalid YOTD active moby";return;}
        const auto index=(m-mobys_)/stride;
        bool own=std::find(owned_.begin(),owned_.end(),index)!=owned_.end();
        if(m>=initial_end_) {
            const auto k=(m-initial_end_)/stride;
            own=k<slots_.size() && slots_[k]!=UINT32_MAX;
        }
        if(own){vm.write32(out,m);out+=4;}
    }
    vm.write32(out,0);if(!terminated)vm.fault="unterminated YOTD active list";
}
void YotdScripts::sync_in(Game& game) {
    auto& vm=*vm_;write_position(vm,spyro_origin,game.player.position+retail::origin_above_feet);
    const auto heading=static_cast<int>(std::lround(retail::original_heading(game.player.yaw)))&4095;
    vm.write8(spyro_origin+0xe,static_cast<std::uint8_t>(heading>>4));
    vm.write32(spyro_origin+0x6c,heading);vm.write32(spyro_origin+0x44,retail::spyro_origin_units);
    write_position(vm,camera_origin,game.camera.sample(1).eye);vm.write32(0x8006c648,2);
    for(unsigned i=0;i<authored_;++i) {
        auto& o=game.retail_objects[i];const auto m=mobys_+i*stride;
        if(o.state<0 || o.collected) {
            if(vm.read8(m+0x48)<128){vm.r[29]=stack;vm.call(remove_moby,std::array<std::uint32_t,1>{m},200'000);if(!vm.fault.empty())return;}
            continue;
        }
        if(!o.retail_script)continue;
        if(o.id<written_.size() && length(o.position-written_[o.id])>1e-4f)write_position(vm,m+0xc,o.position);
        const auto view=game.camera.sample(1);const auto d=o.position-view.eye;
        const auto reach=static_cast<std::int8_t>(vm.read8(m+0x4c));
        const float limit=static_cast<float>(reach+(vm.read8(m+0x4d)&3)),distance=length(d),depth=dot(d,view.forward);
        const bool drawn=reach>0 && !o.hidden && std::abs(d.x)<limit && std::abs(d.y)<limit && std::abs(d.z)<limit && depth<limit && (distance<3 || depth>distance*.45f);
        vm.write8(m+0x4d,static_cast<std::uint8_t>(drawn || o.attack_flags!=0));
        if(o.attack_flags){vm.write32(m+0x18,vm.read32(m+0x18)|o.attack_flags);attacks_seen|=o.attack_flags;o.attack_flags=0;o.attack_sourced=false;}
    }
}
void YotdScripts::mirror(Game& game,RetailObject& o,std::uint32_t m,bool fresh) {
    auto& vm=*vm_;const auto p=position(vm,m+0xc);
    const std::array<float,3> rotation{float(vm.read8(m+0x44)),float(vm.read8(m+0x45)),float(vm.read8(m+0x46))};
    if(fresh){o.position=o.previous_position=p;o.rotation=o.previous_rotation=rotation;o.interpolate_position=false;}
    else {begin_object_transform(o,2);o.position=p;o.rotation=rotation;}
    if(o.id<written_.size())written_[o.id]=p;
    const auto state=static_cast<std::int8_t>(vm.read8(m+0x48));if(state<0 && o.state>=0)++removed;o.state=state<0?std::int8_t(-1):state;
    if(o.enemy && (state==8 || state<0)){o.enemy->defeated=true;o.enemy->phase=EnemyPhase::Dying;}
    o.size=vm.read8(m+0x4f);o.material_color=vm.read32(m+0x54);o.script_undrawn=static_cast<std::int8_t>(vm.read8(m+0x4c))<=0;
    const ObjectAnimationState next{vm.read8(m+0x3c),vm.read8(m+0x3d),vm.read8(m+0x3e),vm.read8(m+0x3f),vm.read8(m+0x40),vm.read8(m+0x42),vm.read8(m+0x41)!=0};
    const auto bank=game.object_models()->models.find(o.type);
    if(bank!=game.object_models()->models.end() && bank->second.animated) {
        auto checked=next;const auto& clips=bank->second.animations;
        if(checked.next_clip<clips.size() && checked.next_frame>=clips[checked.next_clip].frames.size() && !(checked.blend&0xf0)){checked.next_clip=checked.clip;checked.next_frame=checked.frame;}
        if(checked.clip>=clips.size() || checked.next_clip>=clips.size() || checked.frame>=clips[checked.clip].frames.size() || checked.next_frame>=clips[checked.next_clip].frames.size())throw std::runtime_error("YOTD original animation exceeds the native model bank");
        o.previous_animation=fresh?checked:o.animation;o.animation=checked;o.model=checked.clip;
    }
}
void YotdScripts::sync_out(Game& game) {
    auto& vm=*vm_;for(const auto i:owned_)mirror(game,game.retail_objects[i],mobys_+i*stride);
    unsigned used=0;
    while(initial_end_+used*stride<spawn_end_ && vm.read8(initial_end_+used*stride+0x48)!=255)++used;
    if(used>512)throw std::runtime_error("YOTD spawn pool exceeds the bridge limit");
    slots_.resize(std::max<std::size_t>(slots_.size(),used),UINT32_MAX);
    for(unsigned k=0;k<slots_.size();++k) {
        const auto m=initial_end_+k*stride;const auto state=k<used?static_cast<std::int8_t>(vm.read8(m+0x48)):std::int8_t(-2);auto& slot=slots_[k];
        if(state<0){if(slot!=UINT32_MAX)game.retail_objects[slot].state=-1;slot=UINT32_MAX;continue;}
        const auto type=vm.read16(m+0x36);const auto* banks=game.object_models();
        if(type==1) {
            const auto variant=vm.read8(m+0x49);
            if(variant>=banks->gem_values.size() || variant>=5)throw std::runtime_error("unmapped YOTD original gem variant");
            const auto value=banks->gem_values[variant];
            const auto owner=reward_owners_.find(m);
            if(owner==reward_owners_.end())throw std::runtime_error("YOTD reward has no verified authored parent");
            const auto parent=owner->second;
            if(parent>=game.retail_objects.size() || !game.retail_objects[parent].enemy)throw std::runtime_error("invalid YOTD reward parent");
            game.retail_objects[parent].enemy->defeated=true;
            RetailObject gem;gem.source_game=GameBuild::YearOfTheDragonUS;gem.type=gem.source_type=1;gem.runtime_spawned=true;gem.script_gem=false;gem.draw_flags=0x18;
            gem.held_gem=static_cast<std::uint8_t>(value);gem.variant=variant;gem.gem_value=value;gem.model=static_cast<std::uint8_t>(banks->gem_meshes[variant]);gem.material_color=banks->gem_colors[variant];
            gem.id=static_cast<unsigned>(game.retail_objects.size());gem.position=gem.previous_position=position(vm,m+0xc);gem.state=2;gem.bounces=3;gem.script_velocity={0,0,140};
            gem.source_id=game.retail_objects[parent].id;reward_owners_.erase(owner);
            game.retail_objects.push_back(std::move(gem));++spawned;
            vm.r[29]=stack;vm.call(remove_moby,std::array<std::uint32_t,1>{m},200'000);if(!vm.fault.empty())throw std::runtime_error(vm.fault);
            continue;
        }
        if(!banks->models.contains(type))continue; // particle-only/logic records have no renderer bank
        if(slot==UINT32_MAX) {
            RetailObject o;o.id=static_cast<unsigned>(game.retail_objects.size());o.type=o.source_type=type;o.source_game=GameBuild::YearOfTheDragonUS;
            o.runtime_spawned=true;o.retail_script=true;o.scripted_transient=true;o.held_gem=o.persistence=255;slot=o.id;
            game.retail_objects.push_back(std::move(o));written_.resize(game.retail_objects.size());++spawned;mirror(game,game.retail_objects[slot],m,true);
        } else mirror(game,game.retail_objects[slot],m);
    }
}
void YotdScripts::tick(Game& game) {
    if(!attempted_)boot(game);if(!active())return;
    for(const auto i:owned_){auto& o=game.retail_objects[i];o.transform_phase=std::min(o.transform_phase+1,o.transform_span);}
    if((clock_++&1) || game.dialogue.active() || game.scene_locked)return;
    current_=&game;auto& vm=*vm_;const auto before=vm.executed;
    try {
        sync_in(game);if(!vm.fault.empty())throw std::runtime_error(vm.fault);
        vm.write32(spyro_hurt,0);vm.write32(0x80071930,0);
        vm.r[29]=stack;vm.call(active_builder,{},400'000);if(!vm.fault.empty())throw std::runtime_error(vm.fault);
        build_active(vm);if(!vm.fault.empty())throw std::runtime_error(vm.fault);
        vm.hle[active_builder]=[](OverlayVm&){};
        vm.r[29]=stack;vm.call(dispatcher,{},4'000'000);vm.hle.erase(active_builder);
        if(!vm.fault.empty())throw std::runtime_error(vm.fault);
        // Original player collision caller 800420e4..8004210c passes this hurt
        // word and six-argument sphere-query ABI. Native Spyro movement stays
        // in charge; the original animated moby spheres supply contact damage.
        const bool charging=game.player.motion==Motion::Charge || game.player.motion==Motion::ChargeJump || game.player.motion==Motion::Supercharge || game.player.motion==Motion::SuperchargeJump;
        vm.r[29]=stack;vm.write32(stack+16,charging?charge_attack:0);vm.write32(stack+20,0);
        vm.call(moby_query,std::array<std::uint32_t,4>{spyro_origin,retail::spyro_origin_units,0,spyro_hurt},400'000);
        if(!vm.fault.empty())throw std::runtime_error(vm.fault);
        const auto hurt=vm.read32(spyro_hurt);
        if(hurt&0x1f9) {
            float nearest=1e9f;Vec3 from=game.player.position;const auto hit=vm.read32(0x80071930);
            if(hit>=mobys_ && hit<spawn_end_ && (hit-mobys_)%stride==0)from=position(vm,hit+0xc);
            else for(const auto i:owned_){const auto m=mobys_+i*stride;if(vm.read8(m+0x48)>=128)continue;const auto p=position(vm,m+0xc);const auto d=length(p-game.player.position);if(d<nearest){nearest=d;from=p;}}
            if(game.hurt(from))++hits;
        }
        sync_out(game);instructions+=vm.executed-before;++script_ticks;current_=nullptr;
    } catch(const std::exception& e){instructions+=vm.executed-before;char text[64];std::snprintf(text,sizeof text,"YOTD tick at %08x: ",vm.pc);fail(game,text+std::string(e.what()));}
}
} // namespace spyro
