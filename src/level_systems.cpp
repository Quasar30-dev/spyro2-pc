#include "spyro/game_build.hpp"
#include "spyro/runtime.hpp"
#include <bit>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void bounds(View b,std::size_t at,std::size_t n){if(at>b.size() || n>b.size()-at)throw FormatError("Level script data outside gameplay section");}
ScriptRoute route(View b,unsigned at) {
    bounds(b,at,12);ScriptRoute out;out.address=at;
    const auto n=read_u16(b,at);out.initial=read_u16(b,at+2);out.direction=std::bit_cast<std::int16_t>(read_u16(b,at+10));
    if(!n || n>2048 || out.initial>=n || std::abs(out.direction)!=1)throw FormatError("Invalid authored challenge route");
    bounds(b,at+12,std::size_t(n)*16);
    for(unsigned i=0;i<n;++i){const auto p=at+12+i*16;out.points.push_back({retail::to_world({std::bit_cast<int>(read_u32(b,p)),std::bit_cast<int>(read_u32(b,p+4)),std::bit_cast<int>(read_u32(b,p+8))}),std::bit_cast<int>(read_u32(b,p+12))});}
    return out;
}
}
void decode_level_systems(View archive,Level& level) {
    const unsigned slot=level.wad_slot;
    if(slot!=39 && slot!=29 && slot!=43 && slot!=53 && slot!=63)return;
    const auto children=parse_level_archive(archive);
    const auto d=archive.subspan(children.at(3).offset,children.at(3).size);
    for(auto& o:level.objects) {
        ScriptDefinition s;s.object=o.id;s.type=o.type;const auto q=o.data_offset;
        for(unsigned i=0;i<16 && q+i*4+4<=d.size();++i)s.parameters[i]=read_u32(d,q+i*4);
        std::vector<unsigned> paths;
        if(slot==39) {
            switch(o.type) {
            case 0x17b:s.role=ScriptRole::Burner;o.scripted_contact=true;break;
            case 0x17d:s.role=ScriptRole::Fan;break;
            case 0x405:s.role=ScriptRole::Machine;break;
            case 0x10e:s.role=ScriptRole::Cannon;break;
            case 0x16f:s.role=ScriptRole::CannonTarget;o.scripted_contact=true;break;
            case 0x173:s.role=ScriptRole::Trolley;for(unsigned i=1;i<=7;++i)paths.push_back(s.parameters[i]);break;
            case 0x20:s.role=ScriptRole::Gear;o.scripted_transient=true;break;
            case 0x10c:s.role=ScriptRole::TrackSwitch;o.scripted_contact=true;break;
            case 0x172:case 0x178:case 0x1df:case 0x171:case 0x16e:s.role=ScriptRole::TrackObstacle;o.scripted_contact=true;o.scripted_transient=true;break;
            case 0x180:s.role=ScriptRole::Worker;break;
            default:continue;
            }
        }else {
            s.role=ScriptRole::FlightTarget;
            if(slot==29) {
                if(o.type==0x142){if(s.parameters[0])s.category=0;else s.role=ScriptRole::HunterTarget;s.pass_through=true;}
                else if(o.type==0x143){s.category=1;s.pass_through=true;}
                else if(o.type==0x144){s.category=2;paths.push_back(s.parameters[0]);}
                else if(o.type==0x164){s.category=3;paths.push_back(s.parameters[0]);}
                else if(o.type==0x166){s.role=ScriptRole::Hunter;paths.push_back(s.parameters[0]);}
                else if(o.type==0x16c)s.role=ScriptRole::Worker;
                else continue;
            }else if(slot==43) {
                if(o.type==0x206)s.category=0;
                else if(o.type==0x64)s.category=1;
                else if(o.type==0x3a)s.category=2;
                else if(o.type==0x208){s.category=3;s.pass_through=true;}
                else if(o.type==0x51){s.role=ScriptRole::Hunter;paths.push_back(s.parameters[0]);}
                else if(o.type==0x37){s.role=ScriptRole::HunterTarget;if(s.parameters[0])paths.push_back(s.parameters[0]);}
                else if(o.type==0x205)s.role=ScriptRole::Worker;
                else continue;
            }else if(slot==53) {
                if(o.type==0x265){s.category=0;paths.push_back(s.parameters[0]);}
                else if(o.type==0x28d){s.category=1;s.pass_through=true;}
                else if(o.type==0x25e)s.category=2;
                else if(o.type==0x25f)s.category=3;
                else if(o.type==0x28b)s.role=ScriptRole::Hunter;
                else if(o.type==0x290){s.role=ScriptRole::HunterTarget;s.pass_through=true;}
                else continue;
            }else {
                if(o.type==0x263)s.category=0;
                else if(o.type==0x2c8){s.category=1;s.pass_through=true;}
                else if(o.type==0x261){s.category=2;paths.push_back(s.parameters[0]);}
                else if(o.type==0x264){s.category=3;paths.push_back(s.parameters[0]);}
                else if(o.type==0x2c6){s.role=ScriptRole::Hunter;paths.push_back(s.parameters[0]);}
                else if(o.type==0x2c7)s.role=ScriptRole::HunterTarget;
                else if(o.type==0x2c5)s.role=ScriptRole::Worker;
                else continue;
            }
            if(s.role==ScriptRole::HunterTarget || s.role==ScriptRole::FlightTarget) {
                o.scripted_transient=true;o.scripted_contact=!s.pass_through;
                o.hidden=s.role==ScriptRole::HunterTarget;
            }
        }
        for(auto p:paths){if(!p)throw FormatError("Missing challenge route");s.routes.push_back(route(d,p));}
        level.scripts.push_back(std::move(s));
    }
    if(slot==53) {
        const auto target=std::find_if(level.scripts.begin(),level.scripts.end(),[](const auto& s){return s.role==ScriptRole::HunterTarget;});
        const auto hunter=std::find_if(level.scripts.begin(),level.scripts.end(),[](const auto& s){return s.role==ScriptRole::Hunter;});
        if(target!=level.scripts.end() && hunter!=level.scripts.end())hunter->routes.push_back(route(d,target->parameters[1]));
    }
}
std::vector<ScriptProgress> LevelSystems::checkpoint()const {
    std::vector<ScriptProgress> out;
    for(const auto& s:state_)if(s.flags && (s.type==0x17b || s.type==0x16f || s.type==0x405))out.push_back(s);
    return out;
}
void LevelSystems::restore(Game& g,std::span<const ScriptProgress> saved,const ObjectModels*) {
    for(auto& t:g.level.terrain_translations){const auto offset=t.offset;translate_scene_group(g.level,t.group,offset*-1);}
    if(g.level.wad_slot==39)for(unsigned group:{1u,2u,3u,4u,7u,9u})set_scene_morphs(g.level,group,false);
    for(const auto& s:saved) {
        const auto it=std::find_if(state_.begin(),state_.end(),[&](const auto& p){return p.object==s.object && p.type==s.type;});
        if(it==state_.end() || (s.type!=0x17b && s.type!=0x16f && s.type!=0x405))throw FormatError("Saved mechanism does not exist in this scene");
        *it=s;auto& o=g.retail_objects.at(s.object);o.state=s.type==0x16f?-1:1;o.collected=s.type==0x16f;o.variant=1;
        if(s.type==0x17b || s.type==0x405) {
            const auto def=std::find_if(g.level.scripts.begin(),g.level.scripts.end(),[&](const auto& d){return d.object==s.object;});
            if(s.type==0x17b) {
                for(auto& m:g.level.terrain_morphs)if(m.group==def->parameters[4]){m.node=std::min(2u,static_cast<unsigned>(m.steps.size()-1));m.remaining=m.steps[m.node].duration;m.flags=m.steps[m.node].flags&~3u;}
                ++activations;
            }else set_scene_morphs(g.level,def->parameters[1],true);
            if(s.type==0x405)translate_scene_group(g.level,def->parameters[2],{0,s.progress,0});
        }
    }
}
} // namespace spyro
