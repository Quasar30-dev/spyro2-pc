#include "spyro/game_build.hpp"
#include "spyro/npc.hpp"
#include "spyro/level.hpp"
#include <algorithm>
#include <bit>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void require(View b,std::size_t at,std::size_t bytes){if(at>b.size() || bytes>b.size()-at)throw FormatError("NPC data extends outside gameplay section");}
std::size_t skip(View b,std::size_t at){const auto n=read_u32(b,at);if(n<4 || n%4)throw FormatError("Invalid NPC section chain");require(b,at,n);return at+n;}
std::string text(View data,std::size_t at,bool formatted=false) {
    std::string result;
    for(unsigned i=0;i<8192 && at<data.size();++i,++at) {
        const auto c=data[at];if(!c || c==255)return result;
        if(formatted){result+=static_cast<char>(c);continue;}
        if(c=='@' && at+1<data.size()){++at;continue;}
        if(c=='&'){result+="\n\n";continue;}
        // The retail font's controller glyphs, presented with native PC keys.
        if(c=='<')result+="Space";else if(c=='>')result+="Shift";else if(c==']')result+="F";
        else if(c=='}')result+="Triangle";else if(c>=32 && c<127)result+=static_cast<char>(c);
        else result+=' ';
    }
    throw FormatError("Unterminated NPC text");
}
void request_clip(RetailObject& object,unsigned clip) {
    if(object.animation.next_clip==clip)return;
    object.animation.next_clip=static_cast<std::uint8_t>(clip);object.animation.next_frame=0;object.animation.blend=0xf2;
}
bool dialogue_class(unsigned type) {
    // Shared 16-byte header consumed by 80015be4 / 8003a0d8. Keep an
    // explicit actor list: unrelated parameters can resemble dialogue data.
    // Not listed: speakers whose own script moves them and opens the
    // conversation (Magma Cone's host 13b, Scorch's Bombo 1a6, who runs the
    // flag-tower bomb chase, and the Zoe/talk-trigger class 110, which
    // appears, talks when Spyro arrives or presses triangle through
    // 8003a98c, and stores her checkpoint); they are script owned.
    switch(type) {
        case 0x1bc:case 0x77:case 0x16d:case 0x1b6:
        case 0x22a:case 0x220:case 0x242:case 0x10f:case 0x193:case 0x31:case 0x73:
        case 0x149:case 0x14e:case 0xa1:case 0xbb:case 0xb6:case 0x16c:
        case 0x62:case 0x61:case 0xae:case 0x180:case 0xbe:case 0x138:case 0x205:
        case 0x1b9:case 0x1af:case 0x1b0:case 0x189:case 0x188:
        case 0x14a:case 0xfe:case 0x8d:case 0x8a:case 0x1fb:case 0x1fe:case 0x25c:
        case 0x1c7:case 0x10d:case 0x250:case 0x29f:return true;
        default:return false;
    }
}
}
namespace {
struct NpcTable {View data;std::size_t speech{},speech_count{},records{};unsigned count{};bool silent{};};
NpcTable npc_table(View archive) {
    const auto children=parse_level_archive(archive);
    if(children.size()!=4)throw FormatError("Missing NPC gameplay section");
    NpcTable t;t.data=archive.subspan(children[3].offset,children[3].size);
    const auto& data=t.data;
    std::size_t speech=0x2c;for(unsigned i=0;i<7;++i)speech=skip(data,speech);
    t.silent=compact_demo_layout(archive);
    t.records=t.silent?speech:skip(data,speech);t.speech=speech;t.speech_count=t.silent?0:(t.records-speech-4)/8;
    if(!t.silent && (t.records-speech-4)%8)throw FormatError("Invalid NPC speech table");
    const auto end=skip(data,t.records);t.count=read_u32(data,t.records+4);
    if(std::uint64_t(t.count)*88+8>end-t.records)throw FormatError("Invalid NPC object records");
    return t;
}
// One record's conversation table. any_class: a script asked for it
// (80015be4), so the record is known to carry a dialogue header.
std::optional<NpcDefinition> decode_record(View archive,const NpcTable& table,unsigned i,unsigned slot,bool any_class) {
    const auto& data=table.data;const auto speech=table.speech;const auto speech_count=table.speech_count;const bool silent=table.silent;
    const auto record=table.records+8+i*88;
    const auto type=read_u16(data,record+54);
    if(!any_class && !dialogue_class(type))return std::nullopt;
    // Class 73 is a non-speaking Hurricos Moby in both full prototypes;
    // its parameters are numeric state, not the later dialogue header.
    if(type==0x73 && slot==23 && level_model_directory(archive)==0x20)return std::nullopt;
    const auto param=read_u32(data,record);require(data,param,16);
    // Some variants of a speaking class have no conversation table.
    // Retail uses FF in the last-line field for those dormant actors.
    if(data[param+3]==255)return std::nullopt;
    const unsigned lines=data[param+3]+1;if(lines>32)throw FormatError("Unmapped NPC dialogue layout");
    require(data,param+16,lines*4);
    NpcDefinition npc;npc.object=i;npc.subtype=data[param];npc.initial=data[param+2];npc.automatic=data[param+5]!=0;
    npc.type=type;npc.name=text(data,read_u32(data,param+12));
    if(npc.name.empty())npc.name="Talk";
    if(type==0x180)npc.talk_clip=2; // 80080b40: idle 0, speech 2.
    // Untranslated quest conditions must not drag the player into a
    // conversation. All these actors remain available through E.
    if(type!=0x1bc)npc.automatic=false;
    if(npc.initial>=lines)throw FormatError("Hunter's initial dialogue is outside its table");
    for(unsigned j=0;j<lines;++j) {
        const auto at=read_u32(data,param+16+j*4);require(data,at,1);DialogueLine line;
        const unsigned header=data[at];
        if(header==255){npc.lines.push_back(std::move(line));return std::nullopt;}
        if(header<1 || header>64)throw FormatError("Invalid dialogue command header");require(data,at,header);
        line.text=text(data,at+header);
        line.formatted_text=text(data,at+header,true);
        for(unsigned p=1;p<header;) {
            const unsigned command=data[at+p];unsigned n=2;
            if(command==0x23)n=3;else if(command==0x25)n=5;else if(command==0x26 || command==0x2a || command==0x2b)n=1;
            if(n>header-p)throw FormatError("Truncated NPC dialogue command");
            const auto arg=[&](unsigned k){return static_cast<int>(data[at+p+k])-1;};
            if(command==1)line.talisman=data[at+p+1];   // 8003a0d8 command 1: the talisman line and its reward class
            if(command>=2 && command<=9)line.orb_bits|=1u<<(command-2);   // commands 2..9: orb bit (command-2), gated by quest flag 80067148
            if(command==0x28) {
                const int voice=arg(1);
                if(voice>=0 && static_cast<unsigned>(voice)<speech_count) {
                    const auto v=speech+4+voice*8;
                    line.speech=XaClip{read_u32(data,v+4),read_u16(data,v+2),read_u16(data,v),1};
                }else if(voice!=254 && !silent)throw FormatError("NPC voice ID outside speech table");
            }else if(command==0x2c)line.camera_side=arg(1);
            else if(command==0x23)line.repeat=arg(1);
            else if(command==0x29)line.next=arg(1);
            else if(command==0x25) {
                line.yes=arg(1)&127;line.no=arg(2)&127;
                line.answer_closes={arg(1)>127,arg(2)>127};
                if(data[at+p+3]!=255) {
                    line.purchase=arg(3);line.insufficient=arg(4);
                    // Only take payment for a service with its world script.
                    const bool bridge=slot==17 && type==0x220 && line.purchase==3;
                    const bool portal=type==0x77 && ((slot==33 && (line.purchase==10 || line.purchase==12)) || (slot==57 && line.purchase==18));
                    if(!bridge && !portal)line.supported=false;
                }
            }else if(command==0x2d && ((slot==39 && type==0x180) || (slot==29 && type==0x16c) || (slot==43 && type==0x205))) {
                line.level_script=data[at+p+1];
            }else if(slot==39 && type==0x180 && (command==1 || command==2 || command==3 || command==0x26)) {
                if(command==1)line.level_script=1000;
            }else if(command==0x24 && type==0x16d && ((slot==15 && npc.subtype==9) || (slot==33 && npc.subtype==11))) {
                // The travel system owns these portal/whirlwind activations.
            }else if(command!=0x27 && command!=0x2d && command!=0x2a && command!=0x2b)line.supported=false;
            p+=n;
        }
        for(int target:{line.next,line.repeat,line.yes,line.no})if(target>=static_cast<int>(lines))line.supported=false;
        if(line.purchase>=19 || line.insufficient>=static_cast<int>(lines))line.supported=false;
        if(line.yes>=0 && line.no>=0) {
            // Retail choices are the final two ampersand-separated strings.
            // They are positional answers: Hunter's active-camera prompt
            // deliberately uses No, then Yes.
            const auto last=line.text.rfind("\n\n");
            const auto previous=last==std::string::npos?std::string::npos:line.text.rfind("\n\n",last-1);
            if(previous!=std::string::npos) {
                line.answers={line.text.substr(previous+2,last-previous-2),line.text.substr(last+2)};
                line.text.resize(previous);
            }
            const auto raw_last=line.formatted_text.rfind('&');
            const auto raw_previous=raw_last==std::string::npos?std::string::npos:line.formatted_text.rfind('&',raw_last-1);
            if(raw_previous!=std::string::npos) {
                line.formatted_answers={line.formatted_text.substr(raw_previous+1,raw_last-raw_previous-1),line.formatted_text.substr(raw_last+1)};
                line.formatted_text.resize(raw_previous);
            }
        }
        npc.lines.push_back(std::move(line));
    }
    return npc;
}
}
std::vector<NpcDefinition> decode_npcs(View archive,unsigned slot) {
    if(slot==95)return {};
    const auto table=npc_table(archive);
    std::vector<NpcDefinition> result;
    for(unsigned i=0;i<table.count;++i)if(auto npc=decode_record(archive,table,i,slot,false))result.push_back(std::move(*npc));
    return result;
}
std::optional<NpcDefinition> decode_npc_record(View archive,unsigned slot,unsigned index) {
    if(slot==95)return std::nullopt;
    const auto table=npc_table(archive);
    if(index>=table.count)return std::nullopt;
    try{return decode_record(archive,table,index,slot,true);}catch(const FormatError&){return std::nullopt;}
}
void NpcInteractions::reset(const Level& level) {
    npcs_=level.npcs;current_.clear();automatic_.clear();
    for(const auto& npc:npcs_){current_.push_back(npc.initial);automatic_.push_back(npc.automatic);}
    active_=nearby_=UINT32_MAX;elapsed_=conversations_=0;level_script_.reset();++revision_;
}
std::vector<NpcProgress> NpcInteractions::progress()const {
    std::vector<NpcProgress> result;
    for(unsigned i=0;i<npcs_.size();++i)result.push_back({npcs_[i].object,current_[i],automatic_[i]});
    return result;
}
void NpcInteractions::restore(std::span<const NpcProgress> saved) {
    for(const auto& entry:saved) {
        const auto it=std::find_if(npcs_.begin(),npcs_.end(),[&](const auto& npc){return npc.object==entry.object;});
        if(it==npcs_.end() || entry.node>=it->lines.size())throw FormatError("Save refers to an unknown conversation");
    }
    for(const auto& entry:saved) {
        const auto i=std::find_if(npcs_.begin(),npcs_.end(),[&](const auto& npc){return npc.object==entry.object;})-npcs_.begin();
        current_[i]=entry.node;automatic_[i]=entry.automatic;
    }
    active_=nearby_=UINT32_MAX;elapsed_=0;++revision_;
}
unsigned NpcInteractions::object()const{return active()?npcs_[active_].object:UINT32_MAX;}
void NpcInteractions::complete_custom_objective(unsigned object) {
    for(unsigned i=0;i<npcs_.size();++i)if(npcs_[i].object==object && npcs_[i].lines.size()>1)current_[i]=1;
}
const DialogueLine* NpcInteractions::line()const{return active()?&npcs_[active_].lines[current_[active_]]:nullptr;}
const char* NpcInteractions::name()const {
    const auto i=active()?active_:nearby_;return i<npcs_.size()?npcs_[i].name.c_str():"NPC";
}
std::string NpcInteractions::display_text(bool formatted)const {
    const auto* current=line();if(!current)return {};
    auto result=formatted && !current->formatted_text.empty()?current->formatted_text:current->text;
    if(display_price_)for(auto at=result.find('^');at!=std::string::npos;at=result.find('^',at)) {
        const auto end=result.find_first_not_of('^',at);
        const auto value=std::to_string(*display_price_);
        result.replace(at,(end==std::string::npos?result.size():end)-at,value);at+=value.size();
    }
    return result;
}
void NpcInteractions::select(unsigned node){current_[active_]=node;elapsed_=0;++revision_;}
void NpcInteractions::close(std::vector<RetailObject>& objects) {
    const auto speaker=object();
    if(active() && speaker<objects.size())request_clip(objects[speaker],npcs_[active_].idle_clip);
    const bool external=external_;external_=false;
    active_=UINT32_MAX;elapsed_=0;++revision_;
    if(external && retail_close)retail_close(speaker);
}
bool NpcInteractions::skip_empty_lines(unsigned index) {
    // A line without text shows nothing in the original box; it still has to
    // be finished through 8003a568 (its next line, Zoe, the reward, then the
    // close the script waits for), e.g. Cloud Temples' Chronos.
    for(unsigned guard=0;guard<8;++guard) {
        const auto& lines=npcs_[index].lines;const auto line=current_[index];
        if(line<lines.size() && !lines[line].text.empty())return true;
        const auto next=retail_advance?retail_advance(npcs_[index].object,-1):std::nullopt;
        if(!next || *next>=lines.size())break;
        current_[index]=*next;
    }
    const auto object=npcs_[index].object;
    const bool external=external_;external_=false;active_=UINT32_MAX;
    if(external && retail_close)retail_close(object);
    return false;
}
void NpcInteractions::begin_external(const NpcDefinition& npc,unsigned line) {
    auto it=std::find_if(npcs_.begin(),npcs_.end(),[&](const auto& known){return known.object==npc.object;});
    if(it==npcs_.end()){npcs_.push_back(npc);current_.push_back(npc.initial);automatic_.push_back(false);it=npcs_.end()-1;}
    const auto index=static_cast<unsigned>(it-npcs_.begin());
    if(line>=npcs_[index].lines.size()) {
        external_=false;if(retail_close)retail_close(npc.object);return;
    }
    active_=index;current_[index]=line;external_=true;display_price_.reset();
    if(!skip_empty_lines(index))return;
    elapsed_=0;++revision_;++conversations_;
}
void NpcInteractions::tick(const Level& level,std::vector<RetailObject>& objects,const ObjectModels* models,Vec3 player,float yaw,bool grounded,bool charging,DialogueInput input,DialogueEconomy economy) {
    for(const auto& npc:npcs_)if(npc.object<objects.size())objects[npc.object].previous_rotation=objects[npc.object].rotation;
    nearby_=UINT32_MAX;
    if(active()) {
        if(object()>=objects.size() || objects[object()].state<0){close(objects);return;}
        ++elapsed_;auto& object=objects[this->object()];
        object.previous_rotation=object.rotation;
        const auto delta=player-object.position;const float target=retail::original_heading(std::atan2(delta.x,delta.z))/16;
        object.rotation[2]+=std::clamp(std::remainder(target-object.rotation[2],256.0f),-4.0f,4.0f);
        const auto* current=line();
        const bool speaking=!current->speech || elapsed_<current->speech->sectors*60/150;
        if(models) {const auto model=models->models.find(object.type);
            const auto clip=speaking?npcs_[active_].talk_clip:npcs_[active_].idle_clip;
            if(model!=models->models.end() && clip<model->second.animations.size())request_clip(object,clip);}
        if(input.cancel){close(objects);return;}
        if(elapsed_<12)return; // One key press cannot enter and leave a conversation.
        if(external_ && current->yes>=0 && current->no>=0) {
            // Retail: the answer goes through 8003a568 (payments included).
            if(input.choice==0 || input.choice==1) {
                const auto next=retail_advance?retail_advance(this->object(),input.choice):std::nullopt;
                if(next && *next<npcs_[active_].lines.size() && !npcs_[active_].lines[*next].text.empty()){select(*next);return;}
                if(current->level_script>=0)level_script_=std::pair{this->object(),current->level_script};
                close(objects);
            }
        } else if(external_ && (input.next || input.interact)) {
            const auto next=retail_advance?retail_advance(this->object(),-1):std::nullopt;
            if(next && *next<npcs_[active_].lines.size() && !npcs_[active_].lines[*next].text.empty()){select(*next);return;}
            const auto index=active_,node=current_[index];
            if(current->level_script>=0)level_script_=std::pair{this->object(),current->level_script};
            if(npcs_[index].type==0x1bc && npcs_[index].subtype==6){if(node==1)active_camera=true;else if(node==4)active_camera=false;}
            close(objects);
        } else if(current->yes>=0 && current->no>=0 && current->supported) {
            if(input.choice==0 || input.choice==1) {
                auto target=static_cast<unsigned>(input.choice?current->no:current->yes);
                const bool close_after=current->answer_closes[input.choice];
                if(input.choice==0 && current->purchase>=0) {
                    if(!models || !economy.gems || !economy.purchases || (current->purchase==3 && level.terrain_morphs.empty()))return;
                    const auto cost=models->purchase_prices.at(current->purchase),bit=1u<<current->purchase;
                    if(!(*economy.purchases&bit)) {
                        if(*economy.gems<cost){select(static_cast<unsigned>(current->insufficient));return;}
                        *economy.gems-=cost;*economy.purchases|=bit;
                    }
                }
                select(target);if(close_after){if(line()->level_script>=0)level_script_=std::pair{this->object(),line()->level_script};close(objects);}
            }
        } else if(input.next || input.interact) {
            if(current->next>=0 && current->supported)select(static_cast<unsigned>(current->next));
            else {
                const auto index=active_,node=current_[index];
                if(current->level_script>=0)level_script_=std::pair{this->object(),current->level_script};
                if(npcs_[index].type==0x1bc && npcs_[index].subtype==6){if(node==1)active_camera=true;else if(node==4)active_camera=false;}
                if(current->repeat>=0 && current->supported)current_[index]=static_cast<unsigned>(current->repeat);
                close(objects);
            }
        }
        return;
    }
    if(!grounded || charging)return;
    float best=2.5f;
    for(unsigned i=0;i<npcs_.size();++i) {
        const auto& npc=npcs_[i];if(npc.object>=objects.size())continue;const auto& object=objects[npc.object];
        if(object.state<0 || object.collected)continue;
        const auto delta=object.position-player;
        const float high=std::max(std::abs(delta.x),std::abs(delta.z)),low=std::min(std::abs(delta.x),std::abs(delta.z));
        const float distance=high+low*0.375f;
        const auto feet=object.position.y-std::bit_cast<std::int16_t>(read_u16(object.record,56))/retail::units_per_world;
        if(distance>=best || player.y<feet-0.05f || player.y>feet+800/retail::units_per_world)continue;
        const auto target=object.position+Vec3{0,0.6f,0},arm=target-player;
        if(const auto hit=level.raycast(player,arm,length(arm));hit && hit->distance<length(arm)-0.05f)continue;
        nearby_=i;best=distance;
    }
    if(nearby_==UINT32_MAX || npcs_[nearby_].script_speaker)return;
    const auto& npc=npcs_[nearby_];
    const auto delta=objects[npc.object].position-player;
    const float angle=std::abs(std::remainder(std::atan2(delta.x,delta.z)-yaw,2*std::numbers::pi_v<float>));
    if(!input.interact && !(automatic_[nearby_] && angle<std::numbers::pi_v<float>/2))return;
    active_=nearby_;automatic_[active_]=false;
    display_price_.reset();
    for(const auto& portal:level.portals)if(portal.gate_npc==npc.object && portal.required_orbs) {
        display_price_=portal.required_orbs;
        if(npc.lines.size()>1 && current_[active_]==0 && economy.orbs>=portal.required_orbs)current_[active_]=1;
    }
    if(models)for(const auto& offer:npc.lines)if(offer.purchase>=0 && offer.purchase<19) {
        display_price_=models->purchase_prices[offer.purchase];break;
    }
    if(level.wad_slot==17 && npc.type==0x220 && economy.purchases && (*economy.purchases&(1u<<3)) && npc.lines.size()>4)current_[active_]=4;
    if(npc.type==0x77 && economy.purchases)for(const auto& offer:npc.lines)
        if(offer.supported && offer.purchase>=0 && (*economy.purchases&(1u<<offer.purchase)))current_[active_]=npc.lines.size()>4?4:1;
    if(npc.type==0x1bc && npc.subtype==6 && npc.lines.size()>=7)current_[active_]=active_camera?3:0;
    const auto& offer=npc.lines[current_[active_]];
    if(offer.supported && offer.purchase>=0 && offer.insufficient>=0 && models && economy.gems && economy.purchases &&
       !(*economy.purchases&(1u<<offer.purchase)) && *economy.gems<models->purchase_prices[offer.purchase])
        current_[active_]=static_cast<unsigned>(offer.insufficient);
    // With the original logic available, 80015be4/8003a0d8 choose the opening
    // line from the saved progress (and grant finished tasks' rewards).
    if(retail_begin)if(const auto line=retail_begin(npc.object)) {
        if(*line<npc.lines.size()){current_[active_]=*line;external_=true;}
        else if(retail_close)retail_close(npc.object);
    }
    if(npc.lines[current_[active_]].text.empty()) {
        if(external_){if(!skip_empty_lines(active_))return;}
        else {active_=UINT32_MAX;return;}
    }
    elapsed_=0;++revision_;++conversations_;
}
} // namespace spyro
