#include <cstdio>
#include "spyro/overlay_vm.hpp"
#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace spyro {
namespace {
constexpr std::int32_t sx16(std::uint32_t v){return static_cast<std::int16_t>(v&0xffff);}
}
void OverlayVm::load(std::uint32_t address,std::span<const std::uint8_t> bytes) {
    const auto at=address&0x1fffffff;
    if(at+bytes.size()>ram_size)throw std::runtime_error("OverlayVm load outside RAM");
    std::copy(bytes.begin(),bytes.end(),ram.begin()+at);
}
std::uint8_t* OverlayVm::translate(std::uint32_t a,unsigned size) {
    const auto physical=a&0x1fffffff;
    if(physical+size<=ram_size)return &ram[physical];
    if(physical>=0x1f800000 && physical+size<=0x1f800400)return &scratch[physical-0x1f800000];
    return nullptr;   // hardware registers / unmapped: reads 0, writes ignored
}
std::uint8_t OverlayVm::read8(std::uint32_t a){auto* p=translate(a,1);return p?*p:0;}
std::uint16_t OverlayVm::read16(std::uint32_t a){auto* p=translate(a,2);std::uint16_t v=0;if(p)std::memcpy(&v,p,2);return v;}
std::uint32_t OverlayVm::read32(std::uint32_t a){auto* p=translate(a,4);std::uint32_t v=0;if(p)std::memcpy(&v,p,4);return v;}
void OverlayVm::write8(std::uint32_t a,std::uint8_t v){watched(a,1,v);if(auto* p=translate(a,1))*p=v;}
void OverlayVm::write16(std::uint32_t a,std::uint16_t v){watched(a,2,v);if(auto* p=translate(a,2))std::memcpy(p,&v,2);}
void OverlayVm::write32(std::uint32_t a,std::uint32_t v){watched(a,4,v);if(auto* p=translate(a,4))std::memcpy(p,&v,4);}
void OverlayVm::watched(std::uint32_t a,unsigned size,std::uint32_t v) {
    if(!watch || watch<a || watch>=a+size)return;
    std::fprintf(stderr,"watch %08x <- %x (%u bytes at %08x) pc %08x ra %08x\n",watch,v,size,a,history[(history_at+15)&15],r[31]);
}
std::uint32_t OverlayVm::arg(unsigned i){return i<4?r[4+i]:read32(r[29]+i*4);}
std::uint32_t OverlayVm::call(std::uint32_t entry,std::span<const std::uint32_t> args,std::uint64_t budget) {
    const auto saved_sp=r[29]?r[29]:0x801ff000u;
    r[29]=saved_sp;for(unsigned i=0;i<4;++i)r[4+i]=i<args.size()?args[i]:0;
    r[31]=return_trap;pc=entry;next_pc=entry+4;load_delay_={};next_load_={};fault.clear();
    const auto limit=executed+budget;
    while(pc!=return_trap) {
        if(executed>=limit){fault="instruction budget exhausted";break;}
        if(const auto h=hle.find(pc);h!=hle.end()) {
            passthrough=false;h->second(*this);
            if(!passthrough){pc=r[31];next_pc=pc+4;continue;}
            passthrough=false;   // observed: execute the original entry instruction
            step();if(!fault.empty())break;continue;
        }
        if(guard.contains(pc)){++skipped[pc];r[2]=0;pc=r[31];next_pc=pc+4;continue;}
        if((pc&3) || !translate(pc,4)){fault="bad pc";break;}
        step();
        if(!fault.empty())break;
    }
    r[29]=saved_sp;
    return r[2];
}
void OverlayVm::step() {
    const auto current=pc;const auto w=read32(current);
    history[history_at++&15]=current;
    if(executed_pcs)++(*executed_pcs)[current];
    pc=next_pc;next_pc+=4;++executed;
    next_load_={};
    const unsigned op=w>>26,rs=(w>>21)&31,rt=(w>>16)&31,rd=(w>>11)&31,sa=(w>>6)&31,fn=w&63;
    const std::uint32_t imm=w&0xffff;const std::int32_t simm=sx16(w);
    const auto S=[&]{return r[rs];};const auto T=[&]{return r[rt];};
    const auto branch=[&](bool taken){if(taken)next_pc=pc+(static_cast<std::uint32_t>(simm)<<2);};
    const auto load=[&](unsigned reg,std::uint32_t value){next_load_={reg,value};};
    // A register the delayed load targets still reads its old value here.
    switch(op) {
    case 0x00:
        switch(fn) {
        case 0x00:set(rd,T()<<sa);break;
        case 0x02:set(rd,T()>>sa);break;
        case 0x03:set(rd,static_cast<std::uint32_t>(static_cast<std::int32_t>(T())>>sa));break;
        case 0x04:set(rd,T()<<(S()&31));break;
        case 0x06:set(rd,T()>>(S()&31));break;
        case 0x07:set(rd,static_cast<std::uint32_t>(static_cast<std::int32_t>(T())>>(S()&31)));break;
        case 0x08:next_pc=S();jumps[jumps_at++&15]={current,next_pc};break;
        case 0x09:{const auto target=S();set(rd,current+8);next_pc=target;jumps[jumps_at++&15]={current,target};if(trace)++(*trace)[target];break;}
        case 0x0c:fault="syscall";break;
        case 0x0d:fault="break";break;
        case 0x10:set(rd,hi);break;
        case 0x11:hi=S();break;
        case 0x12:set(rd,lo);break;
        case 0x13:lo=S();break;
        case 0x18:{const auto p=static_cast<std::int64_t>(static_cast<std::int32_t>(S()))*static_cast<std::int32_t>(T());lo=static_cast<std::uint32_t>(p);hi=static_cast<std::uint32_t>(p>>32);break;}
        case 0x19:{const auto p=static_cast<std::uint64_t>(S())*T();lo=static_cast<std::uint32_t>(p);hi=static_cast<std::uint32_t>(p>>32);break;}
        case 0x1a:{
            const auto n=static_cast<std::int32_t>(S()),d=static_cast<std::int32_t>(T());
            if(d==0){hi=static_cast<std::uint32_t>(n);lo=n>=0?0xffffffffu:1u;}
            else if(n==INT32_MIN && d==-1){lo=0x80000000u;hi=0;}
            else{lo=static_cast<std::uint32_t>(n/d);hi=static_cast<std::uint32_t>(n%d);}
            break;
        }
        case 0x1b:{const auto n=S(),d=T();if(!d){hi=n;lo=0xffffffffu;}else{lo=n/d;hi=n%d;}break;}
        case 0x20:case 0x21:set(rd,S()+T());break;
        case 0x22:case 0x23:set(rd,S()-T());break;
        case 0x24:set(rd,S()&T());break;
        case 0x25:set(rd,S()|T());break;
        case 0x26:set(rd,S()^T());break;
        case 0x27:set(rd,~(S()|T()));break;
        case 0x2a:set(rd,static_cast<std::int32_t>(S())<static_cast<std::int32_t>(T())?1:0);break;
        case 0x2b:set(rd,S()<T()?1:0);break;
        default:fault="reserved special";break;
        }
        break;
    case 0x01: {
        const bool ge=(rt&1)!=0,link=(rt&0x1e)==0x10;const bool taken=ge?static_cast<std::int32_t>(S())>=0:static_cast<std::int32_t>(S())<0;
        if(link)set(31,current+8);
        branch(taken);break;
    }
    case 0x02:next_pc=(pc&0xf0000000)|((w&0x3ffffff)<<2);break;
    case 0x03:set(31,current+8);next_pc=(pc&0xf0000000)|((w&0x3ffffff)<<2);if(trace)++(*trace)[next_pc];break;
    case 0x04:branch(S()==T());break;
    case 0x05:branch(S()!=T());break;
    case 0x06:branch(static_cast<std::int32_t>(S())<=0);break;
    case 0x07:branch(static_cast<std::int32_t>(S())>0);break;
    case 0x08:case 0x09:set(rt,S()+static_cast<std::uint32_t>(simm));break;
    case 0x0a:set(rt,static_cast<std::int32_t>(S())<simm?1:0);break;
    case 0x0b:set(rt,S()<static_cast<std::uint32_t>(simm)?1:0);break;
    case 0x0c:set(rt,S()&imm);break;
    case 0x0d:set(rt,S()|imm);break;
    case 0x0e:set(rt,S()^imm);break;
    case 0x0f:set(rt,imm<<16);break;
    case 0x10:   // COP0: status/cause are irrelevant to game logic
        if(rs==0)load(rt,0);
        break;
    case 0x12:
        if(rs&0x10){gte.command(w&0x1ffffff);break;}
        switch(rs) {
        case 0:load(rt,gte.read_data(rd));break;
        case 2:load(rt,gte.read_control(rd));break;
        case 4:gte.write_data(rd,T());break;
        case 6:gte.write_control(rd,T());break;
        default:fault="cop2";break;
        }
        break;
    case 0x20:load(rt,static_cast<std::uint32_t>(static_cast<std::int8_t>(read8(S()+simm))));break;
    case 0x21:load(rt,static_cast<std::uint32_t>(static_cast<std::int16_t>(read16(S()+simm))));break;
    case 0x22:case 0x26: {
        const auto a=S()+simm;const auto aligned=read32(a&~3u);
        auto value=(load_delay_.reg==rt?load_delay_.value:T());const unsigned shift=a&3;
        if(op==0x22){const unsigned bits=(3-shift)*8;value=(value&(0x00ffffffu>>(shift*8)))|(aligned<<bits);}
        else{const unsigned bits=shift*8;value=(value&(0xffffff00u<<((3-shift)*8)))|(aligned>>bits);}
        load(rt,value);break;
    }
    case 0x23:load(rt,read32(S()+simm));break;
    case 0x24:load(rt,read8(S()+simm));break;
    case 0x25:load(rt,read16(S()+simm));break;
    case 0x28:write8(S()+simm,static_cast<std::uint8_t>(T()));break;
    case 0x29:write16(S()+simm,static_cast<std::uint16_t>(T()));break;
    case 0x2a:case 0x2e: {
        const auto a=S()+simm;const auto base=a&~3u;auto mem=read32(base);const auto v=T();const unsigned shift=a&3;
        if(op==0x2a){const unsigned bits=(3-shift)*8;mem=(mem&(0xffffff00u<<(shift*8)))|(v>>bits);}
        else{const unsigned bits=shift*8;mem=(mem&(0x00ffffffu>>((3-shift)*8)))|(v<<bits);}
        write32(base,mem);break;
    }
    case 0x2b:write32(S()+simm,T());break;
    case 0x32:gte.write_data(rt,read32(S()+simm));break;
    case 0x3a:write32(S()+simm,gte.read_data(rt));break;
    default:fault="reserved opcode";break;
    }
    // Retire the previous instruction's load, unless this instruction wrote
    // the same register (set() cleared it); then queue this instruction's load.
    if(load_delay_.reg)r[load_delay_.reg]=load_delay_.value;
    load_delay_=next_load_;
    if(load_delay_.reg==0)load_delay_={};
    r[0]=0;
}
OverlayDispatcher find_dispatcher(std::span<const std::uint8_t> overlay,std::uint32_t base) {
    OverlayDispatcher best;
    const auto word=[&](std::size_t i){std::uint32_t w;std::memcpy(&w,overlay.data()+i*4,4);return w;};
    const std::size_t words=overlay.size()/4;
    for(std::size_t i=0;i<words;++i) {
        const auto w=word(i);
        if((w>>26)!=0x21 || ((w>>16)&31)!=3 || (w&0xffff)!=0x36)continue;   // lh v1,0x36(base)
        OverlayDispatcher candidate;candidate.site=base+static_cast<std::uint32_t>(i*4);candidate.moby_register=(w>>21)&31;
        std::optional<unsigned> li;
        for(std::size_t j=i+1;j<std::min(words,i+4000);++j) {
            const auto x=word(j);const unsigned op=x>>26,rs=(x>>21)&31,rt=(x>>16)&31;const std::int32_t simm=static_cast<std::int16_t>(x&0xffff);
            if(op==0x09 && rs==0 && rt==2)li=x&0xffff;
            else if(op==0x04 && rs==3 && rt==2 && li && !candidate.cases.contains(*li))
                candidate.cases[*li]=base+static_cast<std::uint32_t>(j*4)+4+static_cast<std::uint32_t>(simm*4);
        }
        if(candidate.cases.size()>best.cases.size())best=std::move(candidate);
    }
    if(!best.site)return best;
    for(std::uint32_t a=best.site;a>=base+4 && a>best.site-0x1000;a-=4) {
        const auto w=word((a-base)/4);
        if((w>>16)==0x27bd && static_cast<std::int16_t>(w&0xffff)<=-0x200){best.entry=a;break;}
    }
    return best;
}
YotdMobyHandlers find_yotd_moby_handlers(std::span<const std::uint8_t> overlay,std::uint32_t base) {
    constexpr std::uint64_t table_size=0x400*4;
    if(overlay.size()<16)return {};
    const auto word=[&](std::size_t at){std::uint32_t value;std::memcpy(&value,overlay.data()+at,4);return value;};
    const auto end=word(4),table=word(8),following=word(12);
    if(table<base || std::uint64_t(table)+table_size!=following ||
       std::uint64_t(table)-base+table_size>overlay.size() || end<following ||
       std::uint64_t(end)-base>overlay.size())return {};
    YotdMobyHandlers result;result.table=table;
    for(unsigned type=0;type<0x400;++type) {
        const auto entry=word(std::size_t(table)-base+type*4);
        if(!entry)continue;
        if(entry<base || entry>=table || (entry&3))return {};
        result.updates[type]=entry;
    }
    return result;
}
} // namespace spyro
