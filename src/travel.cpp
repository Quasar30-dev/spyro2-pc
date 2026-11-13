#include "spyro/travel.hpp"
#include "spyro/game_build.hpp"
#include "spyro/campaign.hpp"
#include <bit>
#include <numbers>

namespace spyro {
namespace {
constexpr float unit=1/retail::units_per_world,pi=std::numbers::pi_v<float>;
Vec3 point(std::span<const std::uint8_t> b,std::size_t p) {
    return retail::world_position({float(std::bit_cast<int>(read_u32(b,p))),float(std::bit_cast<int>(read_u32(b,p+4))),float(std::bit_cast<int>(read_u32(b,p+8)))});
}
Vec3 centre(const CampaignPortal& p){Vec3 c{};for(const auto v:p.aperture)c+=v;return c/5;}
Vec3 normal(const CampaignPortal& p){return normalized(cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]));}
float horizontal(Vec3 p){return std::hypot(p.x,p.z);}
float angle_delta(float a,float b){return std::remainder(a-b,2*pi);}
void start_travel(Game& g,TravelRequest request,Vec3 target,Vec3 direction) {
    auto& t=g.portal_travel;t.phase=PortalTravel::Phase::Departing;t.request=request;t.ticks=0;t.fade=0;
    t.start=g.player.position;t.target=target;t.direction=direction;t.yaw=g.player.yaw;t.camera_start=g.camera.sample(1).eye;
    g.dialogue.dismiss(g.retail_objects);g.powers.end_flight();g.player.air={};g.player.jump={};g.whirlwind={};
    g.interactions.stop_flame();g.flame_action.reset();g.player.velocity={};
}
}
std::optional<unsigned> campaign_slot(std::span<const std::uint8_t> exe,unsigned scene) {
    const ExeTables tables(exe);const auto ids=tables(0x80064994);
    if(exe.size()<ids+29)throw FormatError("Missing campaign scene table");
    for(unsigned i=0;i<29;++i)if(exe[ids+i]==scene && tables.profile.physical_slot(15+i*2))return 15+i*2;
    return {};
}
std::vector<WhirlwindParticle> whirlwind_particles(const Whirlwind& wind,float alpha,bool streaks) {
    std::vector<WhirlwindParticle> result;
    if(!wind.active || !(wind.height>0) || !(wind.radius>0))return result;
    // Summer Forest 8007E100 / 8007CDB8: the sprite's random initial
    // age controls its horizontal phase, independently of its vertical age.
    // Each original script update spans two native 60 Hz ticks.
    const double now=(double(wind.ticks)+std::clamp(alpha,0.0f,1.0f))*0.5;
    const double interval=streaks?1.0:2.0;
    const float rise=(streaks?192.0f:64.0f)*unit;
    const auto last=std::uint64_t(std::floor(now/interval));
    const auto count=std::uint64_t(std::ceil(wind.height/rise/interval))+1;
    const auto first=last>=count?last-count+1:0;
    const auto random=[](std::uint64_t birth,unsigned object,unsigned lane) {
        std::uint64_t x=birth+0x9e3779b97f4a7c15ULL*(std::uint64_t(object)+1)+lane*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;
        return std::uint32_t(x^(x>>31));
    };
    result.reserve(std::size_t(count));
    for(auto birth=first;birth<=last;++birth) {
        const float age=float(now-birth*interval),height=age*rise;
        if(height>=wind.height)continue;
        Vec3 offset{};
        if(streaks) {
            // 8007F788..8007F7EC: random byte heading, radius uniformly
            // chosen from half the authored radius through its full radius.
            const float heading=(random(birth,wind.object,1)&255)*16*retail::angle_to_radians;
            const auto native_radius=unsigned(std::lround(wind.radius/unit));
            const float radius=(native_radius/2+random(birth,wind.object,2)%(native_radius-native_radius/2+1))*unit;
            offset={std::cos(heading)*radius,height,std::sin(heading)*radius};
        } else {
            const float phase=float(random(birth,wind.object,0)&15)+std::max(0.0f,age-1);
            const float heading=phase*64*retail::angle_to_radians;
            const float radius=std::min(wind.radius,phase*64*unit);
            offset={std::cos(heading)*radius,height,std::sin(heading)*radius};
            if(age<1) {
                const Vec3 initial{(int(random(birth,wind.object,1)&126)-63)*4*unit,0,
                                   (int(random(birth,wind.object,2)&126)-63)*4*unit};
                offset.x=initial.x+(offset.x-initial.x)*age;
                offset.z=initial.z+(offset.z-initial.z)*age;
            }
        }
        result.push_back({offset,streaks?std::min(384*unit,wind.height-height):0,birth});
    }
    return result;
}
void decode_travel(std::span<const std::uint8_t> archive,std::span<const std::uint8_t> exe,Level& level) {
    level.portals.clear();level.home_transports.clear();level.whirlwinds.clear();
    if(level.wad_slot<15 || level.wad_slot>71 || !(level.wad_slot&1))return;
    const ExeTables tables(exe);
    level.campaign_scene=exe[tables(0x80064994)+(level.wad_slot-15)/2];
    // Prototype home worlds retain unused Return Home actors. A model in the
    // object table is not an active portal: only an authored, bound aperture
    // below may reveal it. Keep IDs intact for scripts and saved progress.
    for(auto& o:level.objects)if(o.type==0x1aa)o.hidden=true;
    if(compact_demo_layout(archive))return; // This demo has no authored portals or home transports.
    const auto data=archive.subspan(read_u32(archive,8),read_u32(archive,12));
    const auto skip=[&](std::size_t p){const auto n=read_u32(data,p);if(n<4 || p>data.size() || n>data.size()-p)throw FormatError("Invalid portal section extent");return p+n;};
    std::size_t at=0;for(unsigned i=0;i<6;++i)at=skip(at);at=skip(at+12);at=skip(at);const auto end=skip(at);
    const auto count=read_u32(data,at+4);if(count>256 || end-at<8+count*68)throw FormatError("Invalid portal directory");
    const auto get=[&](std::size_t p){if(p<at || p>end || end-p<4)throw FormatError("Portal artwork outside section");return read_u32(data,p);};
    for(unsigned i=0;i<count;++i) {
        CampaignPortal p;p.index=i;const auto r=at+8+i*68;
        p.visibility=data[r+64];p.two_sided=data[r+65]!=0;p.scene_id=data[r+66];
        const bool home=level.campaign_scene%10==0;
        auto target=campaign_slot(exe,p.scene_id);
        if(!home){const auto exit=campaign_exit(exe,level.wad_slot);target=exit?std::optional(exit->destination):std::nullopt;}
        if(!target)continue;p.destination=*target;
        for(unsigned k=0;k<5;++k)p.aperture[k]=point(data,r+k*12);
        p.enabled=p.visibility!=0;
        if(length(p.aperture[4]-p.aperture[0])<0.01f) {
            if(home)continue;
            const auto it=std::find_if(level.objects.begin(),level.objects.end(),[](const auto& o){return o.type==0x1aa;});
            if(it==level.objects.end())continue;
            p.object=it->id;p.return_home=true;p.enabled=false;p.opening=p.rise=0;
            const auto yaw=retail::native_heading(it->rotation[2]*16);
            for(unsigned k=0;k<5;++k) {
                const auto raw=point(exe,tables(0x800631f0)+k*12);
                const Vec3 local{-raw.z,raw.y,raw.x};
                p.aperture[k]=it->position+retail::rotate_model(local,yaw);
            }
        }
        const auto picture=at+get(r+60);
        for(unsigned layer=0;layer<2;++layer) {
            const auto base=picture+layer*60;const auto nv=get(base),nc=get(base+8),nf=get(base+16);
            if(nv>1024 || nc>1024 || nf>10000 || !nv || !nc)throw FormatError("Invalid portal picture counts");
            const auto vertices=base+get(base+4),colors=base+get(base+12),faces=base+get(base+20);
            for(unsigned face=0;face<nf;++face) {
                const auto indices=get(faces+face*8),palette=get(faces+face*8+4);
                for(unsigned k=0;k<3;++k) {
                    const auto vi=indices>>(20-k*10)&1023,ci=(palette&0x80000000)?palette>>20&1023:palette>>(20-k*10)&1023;
                    if(vi>=nv || ci>=nc)throw FormatError("Portal artwork index outside its table");
                    const auto xy=std::bit_cast<int>(get(vertices+vi*4))>>5;const auto rgb=get(colors+ci*4);
                    p.picture.push_back({{float(static_cast<std::int16_t>(xy))/512,(228-float(xy>>16))/216,float(layer)},
                        {float(rgb&255)/255,float(rgb>>8&255)/255,float(rgb>>16&255)/255}});
                }
            }
        }
        level.portals.push_back(std::move(p));
    }
    for(const auto& p:level.portals)if(p.return_home)level.objects.at(p.object).hidden=false;
    const auto gameplay=archive.subspan(read_u32(archive,24),read_u32(archive,28));
    for(auto& p:level.portals)if(!p.return_home) {
        // The controller's +00 is the level-name index, not an object mesh.
        // Bind by destination AND proximity so unused home-world labels do
        // not invent portals, especially in the July prototype.
        const auto name_index=(p.destination-15)/2;
        const RetailObject* label=nullptr;float closest=2.0f;
        for(const auto& o:level.objects)if(o.type==0x40a && read_u32(gameplay,o.data_offset)==name_index) {
            const auto distance=horizontal(o.position-centre(p));
            if(distance<closest){closest=distance;label=&o;}
        }
        if(!label)continue;
        const auto address=read_u32(exe,tables(0x800649b4)+name_index*4);
        if(!address)continue;
        for(auto q=tables.native(address);q<exe.size() && exe[q];++q) {
            if(p.label.size()>=256)throw FormatError("Unterminated portal name");
            p.label+=char(exe[q]);
        }
        p.label_origin=label->position;p.label_heading=label->rotation[2]*16;
        p.label_two_sided=read_u32(gameplay,label->data_offset+4)!=0;
    }
    for(const auto& o:level.objects) {
        if(o.type==0x400) {
            const auto height=read_u32(gameplay,o.data_offset),radius=read_u32(gameplay,o.data_offset+4),kind=read_u32(gameplay,o.data_offset+8);
            if(height<3072 || height>100000 || !radius || radius>16384 || kind>3)throw FormatError("Unsupported whirlwind parameters");
            level.whirlwinds.push_back({o.id,kind,0,0,0,height*unit,radius*unit,kind==0 || o.state>0});
        } else if(o.type==0x23b) {
            const auto target=read_u32(gameplay,o.data_offset+4),arrival=read_u32(gameplay,o.data_offset+8);
            const auto destination=tables.profile.prerelease()?campaign_slot(exe,target):
                (target<29?std::optional(15+target*2):std::nullopt);
            if(!destination || arrival>=32)throw FormatError("Invalid home transporter destination");
            level.home_transports.push_back({o.id,*destination,arrival,read_u32(gameplay,o.data_offset)});
        }
    }
    // Moneybags' shared class 77 handler uses +48 purchase, +4C portal,
    // +4E visibility and +44 barrier Moby (80077008 / 8007728C).
    for(const auto& o:level.objects)if(o.type==0x77) {
        const auto a=o.data_offset;const auto subtype=gameplay[a];
        if(subtype!=12 && subtype!=14 && subtype!=52)continue;
        const auto index=read_u16(gameplay,a+0x4c);
        if(tables.profile.build==GameBuild::July1999 && index==0xffff)continue;
        const auto found=std::find_if(level.portals.begin(),level.portals.end(),[&](auto& p){return p.index==index;});
        if(found==level.portals.end())throw FormatError("Moneybags portal index is unmapped");
        found->purchase=read_u32(gameplay,a+0x48);found->gate_object=read_u32(gameplay,a+0x44);
        if(found->purchase>=19 || found->gate_object>=level.objects.size())throw FormatError("Invalid purchased portal gate");
    }
    for(const auto& o:level.objects) {
        const auto a=o.data_offset;if(o.type!=0x16d && o.type!=0x1b6)continue;
        const auto subtype=gameplay[a];unsigned index=UINT32_MAX,orbs=0,barrier=UINT32_MAX;bool ripto=false;
        if(o.type==0x16d && (subtype==9 || subtype==11 || subtype==50)) {
            index=read_u16(gameplay,a+(tables.profile.build==GameBuild::July1999?0x48:0x4c));barrier=read_u32(gameplay,a+0x40);
            if(tables.profile.build==GameBuild::July1999 && index==0xffff)continue;
            orbs=subtype==9?3:subtype==11?6:0;ripto=subtype==50;
            if(subtype==11)for(auto& w:level.whirlwinds)if(w.object==read_u32(gameplay,a+0x38))w.required_orbs=6;
        } else if(o.type==0x1b6 && (subtype==51 || subtype==54)) {
            index=read_u32(gameplay,a+0x40);barrier=read_u32(gameplay,a+0x48);orbs=subtype==51?15:25;
        }
        if(index==UINT32_MAX)continue;
        const auto found=std::find_if(level.portals.begin(),level.portals.end(),[&](auto& p){return p.index==index;});
        if(found==level.portals.end())throw FormatError("Orb-gated portal index is unmapped");
        found->required_orbs=orbs;found->gate_object=barrier;found->gate_npc=o.id;found->require_ripto=ripto;
    }
}
bool portal_contains(const CampaignPortal& p,Vec3 v,float margin) {
    const auto n=normal(p);if(length(n)<0.5f)return false;
    float lo=1e9f,hi=-1e9f;
    for(unsigned i=0;i<5;++i) {const auto a=p.aperture[i],b=p.aperture[(i+1)%5];const auto side=dot(cross(b-a,v-a),n)/std::max(length(b-a),0.001f);lo=std::min(lo,side);hi=std::max(hi,side);}
    return lo>=-margin || hi<=margin;
}
bool portal_crossed(const CampaignPortal& p,Vec3 from,Vec3 to) {
    if(!p.enabled || p.opening<0.95f)return false;
    const auto n=normal(p),c=centre(p);const float a=dot(from-c,n),b=dot(to-c,n);
    if(std::abs(a-b)<1e-6f || a*b>0)return false;
    return portal_contains(p,lerp(from,to,std::clamp(a/(a-b),0.0f,1.0f)),-0.04f);
}
std::vector<PortalVertex> portal_picture(const CampaignPortal& p,Vec3 eye) {
    return portal_picture(p,eye,normalized(centre(p)-eye),{0,1,0},1.027f,4/3.0f);
}
std::vector<PortalVertex> portal_picture(const CampaignPortal& p,Vec3 eye,Vec3 forward,Vec3 up,float vertical_fov,float aspect) {
    std::vector<PortalVertex> result;if(!p.enabled || p.opening<=0)return result;
    const auto c=centre(p),n=normal(p);if(length(n)<0.5f)return result;
    const float plane=dot(c-eye,n);if(std::abs(plane)<0.02f)return result;
    if(!p.two_sided && plane>0)return result;
    forward=normalized(forward);const auto right=normalized(cross(up,forward));up=cross(forward,right);
    const float sy=std::tan(vertical_fov/2),sx=sy*aspect;
    // 8004C66C projects the aperture, then 800296D0 clips the original
    // screen artwork. Clip at the near plane before dividing by depth.
    std::vector<Vec3> aperture;
    for(auto v:p.aperture)aperture.push_back(c+(v-c)*p.opening);
    std::vector<Vec3> visible;
    for(unsigned i=0;i<aperture.size();++i) {
        const auto a=aperture[i],b=aperture[(i+1)%aperture.size()];
        const float da=dot(a-eye,forward)-0.05f,db=dot(b-eye,forward)-0.05f;
        if(da>=0)visible.push_back(a);if((da<0)!=(db<0))visible.push_back(lerp(a,b,da/(da-db)));
    }
    aperture.clear();Vec3 screen_c{};
    for(auto v:visible){const auto d=v-eye;const float z=dot(d,forward);const Vec3 s{dot(d,right)/(z*sx),dot(d,up)/(z*sy),0};aperture.push_back(s);screen_c+=s;}
    if(aperture.size()<3)return result;screen_c=screen_c/float(aperture.size());
    const auto outside=[&](unsigned axis,float sign){return std::all_of(aperture.begin(),aperture.end(),[&](Vec3 v){return (axis?v.y:v.x)*sign>1;});};
    if(outside(0,1) || outside(0,-1) || outside(1,1) || outside(1,-1))return result;
    for(std::size_t t=0;t<p.picture.size();t+=3) {
        std::vector<PortalVertex> polygon;
        for(unsigned k=0;k<3;++k){const auto& v=p.picture[t+k];polygon.push_back({{v.position.x*2-1,(v.position.y*216+12)/120-1,0},v.color});}
        for(unsigned i=0;i<aperture.size() && !polygon.empty();++i) {
            const auto a=aperture[i],b=aperture[(i+1)%aperture.size()];
            const float sign=cross(b-a,screen_c-a).z>=0?1.0f:-1.0f;
            const auto side=[&](Vec3 v){return cross(b-a,v-a).z*sign;};std::vector<PortalVertex> clipped;
            for(std::size_t j=0;j<polygon.size();++j){const auto v=polygon[j],w=polygon[(j+1)%polygon.size()];const float sv=side(v.position),sw=side(w.position);
                if(sv>=-1e-6f)clipped.push_back(v);if((sv<0)!=(sw<0)){const float f=sv/(sv-sw);clipped.push_back({lerp(v.position,w.position,f),lerp(v.color,w.color,f)});}}
            polygon=std::move(clipped);
        }
        for(std::size_t j=1;j+1<polygon.size();++j)for(const auto k:{std::size_t(0),j,j+1}) {
            auto v=polygon[k];const auto ray=forward+right*(v.position.x*sx)+up*(v.position.y*sy);
            v.position=eye+ray*(plane/dot(ray,n))*0.9999f;result.push_back(v);
        }
    }
    return result;
}
std::vector<RetailObject> portal_letters(const Level& level,Vec3 eye,float ticks) {
    std::vector<RetailObject> letters;
    for(const auto& p:level.portals) {
        if(!p.enabled || p.label.empty() || horizontal(eye-p.label_origin)>=9.0f)continue;
        float heading=p.label_heading;
        const float toward=std::atan2(eye.z-p.label_origin.z,eye.x-p.label_origin.x)/retail::angle_to_radians;
        const float delta=std::remainder(toward-heading,4096.0f);
        if(std::abs(delta)>52*16) {
            if(!p.label_two_sided)continue;
            heading+=2048;
        }
        // 8007664C..80076948: 38-angle-unit character steps on a 3400
        // unit arc, centred 2500 units behind the authored controller.
        unsigned count=0;for(unsigned i=0;i<p.label.size();++i){if(p.label[i]=='@' && i+1<p.label.size())++i;++count;}
        if(!count)continue;
        float offset=-19.0f*(count-1);unsigned ordinal=0;
        for(unsigned i=0;i<p.label.size();++i,++ordinal,offset+=38) {
            unsigned char c=p.label[i];unsigned mesh=26;float vertical=0;
            if(c==' ')continue;
            if(c=='@' && i+1<p.label.size()) {
                c=p.label[++i];const auto lower=unsigned(c|32);
                mesh=(lower=='a' || lower=='b')?27:lower=='g'?28:lower=='i'?29:lower=='t'?30:lower=='u'?31:26;
                vertical=144*unit;
            } else if(c>='a' && c<='z')mesh=c-'a';
            else if(c>='A' && c<='Z')mesh=c-'A';
            else if(c=='.'){mesh=32;vertical=-64*unit;}
            else if(c=='-')mesh=33;
            else vertical=64*unit;
            const float a=(heading+offset)*retail::angle_to_radians,h=heading*retail::angle_to_radians;
            RetailObject letter;letter.type=0x229;letter.model=std::uint8_t(mesh);letter.material_color=2;letter.held_gem=255;
            letter.position=letter.previous_position=p.label_origin+Vec3{(std::cos(a)*3400-std::cos(h)*2500)*unit,
                (std::cos(offset*retail::angle_to_radians)*1024-496)*unit+vertical,(std::sin(a)*3400-std::sin(h)*2500)*unit};
            // Class 229's original gentle yaw sway, sampled continuously.
            letter.rotation[2]=letter.previous_rotation[2]=(heading+offset+2048)/16+
                24*std::cos((ticks*.5f+ordinal)*64*retail::angle_to_radians);
            letters.push_back(letter);
        }
    }
    return letters;
}
void update_travel(Game& g) {
    if(g.level.universe)return;
    for(auto& p:g.level.portals) {
        if(p.return_home) {
            // Return Home appears once the level's talisman/reward flag is set.
            // Already-active retail objects also remain active on restore.
            // Class 1AA (Glimmer 80075024, Mystic Marsh 800773f4) tests the level's
            // talisman-table byte (8006b264 + level). Winter Tundra sets it with the
            // orb in the level's "Perfect" mask (80066e72 + level); saves from before
            // that byte was kept fall back on the mask (bit 0 without the scripts).
            const unsigned index=(g.level.wad_slot-15)/2;
            const unsigned perfect=g.overlay_scripts.dialogue_bridge()?g.overlay_scripts.peek(0x80066e72+index,1):1u;
            // The reward sets the byte at update 150 (80016328): the arch waits for it.
            const bool rewarding=g.reward_show.active() && g.reward_show.ticks<300;
            const auto& o=g.retail_objects[p.object];p.enabled=g.portal_travel.unlock || (g.guide_awards.talisman && !rewarding) ||
                (g.level.campaign_scene>=50 && index>0x15 && (g.guide_awards.orbs&perfect)) || o.state==2;
            // Existing completion flags instantiate an already-raised arch;
            // a newly completed level raises it 50 units per script update.
            if(p.enabled)p.rise=g.clock.ticks()<30?1.0f:std::min(1.0f,p.rise+1/80.0f);
            else p.rise=0;
            const float distance=horizontal(g.player.position-o.position);
            const float target=p.enabled && p.rise>=1 && distance<(p.opening>0?8.0f:7.0f)?1.0f:0.0f;
            p.opening+=std::clamp(target-p.opening,-0.0625f,0.0625f);
        } else if(!p.world.empty()) {
            // A world door (custom door mode 2) opens once enough expansion
            // emblems are held; near a sealed one, say what it needs.
            p.enabled=g.portal_travel.unlock || g.world_emblems>=p.world_emblems;
            if(!p.enabled && length(g.player.position-centre(p))<5 && g.custom_notice_ticks<30) {
                g.custom_notice="The crown gate is sealed - "+std::to_string(g.world_emblems)+" / "+std::to_string(p.world_emblems)+" emblems";g.custom_notice_ticks=60;
            }
        } else if(p.purchase!=UINT32_MAX || p.required_orbs || !p.visibility) {
            p.enabled=g.portal_travel.unlock || (p.purchase!=UINT32_MAX && (g.purchases&(1u<<p.purchase))) ||
                (p.required_orbs && g.player.orbs>=p.required_orbs) || (p.require_ripto && g.ripto_defeated);
            if(p.gate_object<g.retail_objects.size())g.retail_objects[p.gate_object].state=p.enabled?0:1;
        }
    }
    for(auto& h:g.level.home_transports) {
        h.active=g.portal_travel.unlock || (g.visited_levels&(1u<<((h.destination-15)/2)));
        if(horizontal(g.player.position-g.retail_objects[h.object].position)>0x501*unit)h.armed=true;
    }
    for(auto& w:g.level.whirlwinds) {
        if(g.powers.flying){w.active=false;continue;}
        auto& o=g.retail_objects[w.object];const auto d=g.player.position+retail::origin_above_feet-o.position;
        const auto distance=horizontal(d);++w.ticks;
        if(w.kind==0)w.active=true;
        else if(w.kind==2)w.active=g.portal_travel.unlock || o.state>0 || (w.required_orbs && g.player.orbs>=w.required_orbs);
        else {
            if(w.timer)--w.timer;
            if(w.phase==0 && std::abs(d.y)<1 && ((w.kind==1 && distance<w.radius+1) || (w.kind==3 && distance>w.radius+2 && distance<8))) {w.phase=1;w.timer=w.kind==1?60:0;}
            if(w.phase==1 && !w.timer){w.phase=2;w.active=true;}
            if(w.phase==2 && (g.whirlwind.index==unsigned(&w-g.level.whirlwinds.data()) || (w.kind==3 && distance>10))) {w.phase=3;w.timer=60;}
            if(w.phase==3 && g.whirlwind.index!=unsigned(&w-g.level.whirlwinds.data()) && !w.timer){w.phase=0;w.active=false;}
        }
    }
}
void detect_portal_entry(Game& g) {
    auto& t=g.portal_travel;if(g.level.universe || !t.allowed || t.active() || g.dialogue.active() || g.scene_locked || g.whirlwind.index!=UINT32_MAX)return;
    if(t.cooldown){--t.cooldown;return;}
    const auto from=g.player.previous_position+retail::origin_above_feet,to=g.player.position+retail::origin_above_feet;
    for(const auto& p:g.level.portals)if(portal_crossed(p,from,to)) {
        auto direction=normal(p);if(dot(to-from,direction)<0)direction=direction*-1;
        TravelRequest request{p.destination,g.level.campaign_scene};
        // World doors load another custom world; a world's Return Home goes
        // back through the door it was entered by.
        if(!p.world.empty()){request.world=p.world;request.world_flags=p.world_flags;}
        else if(p.return_home && g.world_return){request.world=g.world_return->world;request.world_flags=g.world_return->flags;request.world_return=true;}
        const auto c=centre(p);start_travel(g,request,c+direction*2-retail::origin_above_feet,direction);return;
    }
    for(const auto& h:g.level.home_transports) {
        if(!h.active || !h.armed)continue;
        const auto& o=g.retail_objects[h.object];const auto d=to-o.position;
        // 23B tests 0x380 lateral and 0x201..0x800 vertical units.
        if(horizontal(d)<0x380*unit && d.y>0x201*unit && d.y<2.0f) {
            const float yaw=retail::native_heading(o.rotation[2]*16);start_travel(g,{h.destination,g.level.campaign_scene,h.arrival,true},o.position+Vec3{0,1.6f,0}-retail::origin_above_feet,retail::rotate_model({0,0,1},yaw));return;
        }
    }
}
void start_scripted_travel(Game& g,unsigned destination) {
    if(g.level.universe || !g.portal_travel.allowed || g.portal_travel.active())return;
    const Vec3 forward{std::sin(g.player.yaw),0,std::cos(g.player.yaw)};
    start_travel(g,{destination,g.level.campaign_scene},g.player.position+forward*2+Vec3{0,1.5f,0},forward);
}
bool advance_portal_travel(Game& g) {
    auto& t=g.portal_travel;if(!t.active())return false;
    if(t.phase==PortalTravel::Phase::Loading)return true;
    ++t.ticks;const bool arrival=t.phase==PortalTravel::Phase::Arriving;
    const unsigned duration=arrival?40:t.request.transporter?88:32;
    const float f=std::min(1.0f,float(t.ticks)/duration);
    if(t.request.transporter && !arrival) {
        g.player.position=lerp(t.start,t.target,std::min(1.0f,float(t.ticks)/30));
        g.player.yaw+=std::min(26.0f,float(t.ticks)/2+2)*16*retail::angle_to_radians;
    } else {
        g.player.position=lerp(t.start,t.target,f);
        const float target=std::atan2(t.direction.x,t.direction.z);g.player.yaw+=std::clamp(angle_delta(target,g.player.yaw),-0.18f,0.18f);
    }
    g.player.velocity=(g.player.position-g.player.previous_position)*60;g.player.grounded=false;g.player.airborne_ticks=4;
    g.player.motion=Motion::Glide;++g.player.motion_ticks;g.player.pitch*=0.9f;g.player.roll*=0.9f;
    g.animation.tick(16);g.secondary.tick(0,g.animation.state(),true);
    const auto eye=lerp(t.camera_start,t.target-t.direction*3+Vec3{0,1.1f,0},f);
    g.camera.scripted_view=CameraView{eye,normalized(g.player.position+retail::origin_above_feet-eye)};
    g.camera.tick(g.level,g.player.position,g.player.yaw,16);
    t.fade=arrival?std::max(0.0f,1-float(t.ticks)/8):std::clamp(float(t.ticks)-(duration-8),0.0f,8.0f)/8;
    g.hud.tick(g.level_gems(),g.spirits(),g.player.lives,false,true);
    if(t.ticks>=duration) {
        if(arrival){t.phase=PortalTravel::Phase::Idle;t.cooldown=90;t.fade=0;g.camera.scripted_view.reset();g.player.air.gliding=true;g.player.air.glide_started=true;g.player.air.glide_ceiling=g.player.position.y;g.player.velocity=t.direction*g.tuning.glide_speed;}
        else t.phase=PortalTravel::Phase::Loading;
    }
    return true;
}
void begin_portal_arrival(Game& g,const TravelRequest& request) {
    auto& t=g.portal_travel;const bool unlock=t.unlock;t={};t.unlock=unlock;t.request=request;t.phase=PortalTravel::Phase::Arriving;t.fade=1;
    g.powers.end_flight();g.player.air={};g.player.jump={};g.whirlwind={};
    Vec3 target=g.player.position;float yaw=g.player.yaw;
    if(request.transporter) {
        const auto it=std::find_if(g.level.home_transports.begin(),g.level.home_transports.end(),[&](auto h){return h.arrival==request.arrival;});
        if(it==g.level.home_transports.end())throw FormatError("Home transporter has no authored arrival");
        const auto& o=g.retail_objects[it->object];yaw=retail::native_heading(o.rotation[2]*16);
        t.direction=retail::rotate_model({0,0,1},yaw);t.start=o.position+Vec3{0,0x6f4*unit,0}-retail::origin_above_feet;
        const auto approach=o.position+t.direction*4+Vec3{0,4,0};
        if(const auto floor=g.level.raycast(approach,{0,-1,0},12))target=floor->point+Vec3{0,0.35f,0};else target=o.position+t.direction*4;
    } else {
        const auto it=std::find_if(g.level.portals.begin(),g.level.portals.end(),[&](auto& p){return p.scene_id==request.source_scene && !p.return_home;});
        t.direction=retail::rotate_model({0,0,1},yaw);
        t.start=it==g.level.portals.end()?target-t.direction*1.2f+Vec3{0,1.2f,0}:centre(*it)-t.direction*1.5f;
        target+=Vec3{0,0.25f,0};
    }
    t.target=target;t.yaw=yaw;t.camera_start=t.start-t.direction*3+Vec3{0,1,0};
    g.player.position=g.player.previous_position=t.start;g.player.yaw=g.player.previous_yaw=yaw;g.player.motion=Motion::Glide;
    g.player.grounded=false;g.player.velocity={};g.restore_player_pose();g.camera.reset(g.level,t.start,yaw);
}
bool move_whirlwind(Game& g) {
    auto& ride=g.whirlwind;auto& p=g.player;if(g.level.universe)return false;
    if(ride.cooldown)--ride.cooldown;
    if(ride.index==UINT32_MAX && !ride.cooldown && !g.dialogue.active() && !g.scene_locked && !p.hurt_ticks && p.swim.mode==WaterMode::Dry && !g.powers.flying) {
        for(unsigned i=0;i<g.level.whirlwinds.size();++i) {
            const auto& w=g.level.whirlwinds[i];const auto& o=g.retail_objects[w.object];const auto d=p.position+retail::origin_above_feet-o.position;
            if(!w.active || horizontal(d)>=w.radius || d.y< -1 || d.y+3>=w.height)continue;
            ride.index=i;ride.ticks=0;ride.start_height=p.position.y;ride.start_yaw=retail::original_heading(p.yaw);
            ride.rise_span=(w.height-d.y)*1024-2048;
            ride.turn_delta=std::remainder(o.rotation[2]*16-ride.start_yaw-ride.rise_span-1024,4096.0f);ride.turning=false;
            p.velocity={};p.air={};p.jump={};p.slide={};break;
        }
    }
    if(ride.index==UINT32_MAX)return false;
    const auto& w=g.level.whirlwinds.at(ride.index);const auto& o=g.retail_objects[w.object];++ride.ticks;
    const float rise=(p.position.y-ride.start_height)*1024;
    const float left=(o.position.y+w.height-p.position.y-retail::spyro_origin_height)*1024;
    float original_yaw=ride.start_yaw;
    Vec3 target=o.position;float max_horizontal=0x400*MovementTuning::velocity_unit;
    if(rise<1024) {p.velocity.y=std::min(0x12c0*MovementTuning::velocity_unit,p.velocity.y+0x80*MovementTuning::velocity_unit);original_yaw+=rise*rise/2048;}
    else if(left>1024) {
        if(!ride.turning){ride.turning=true;ride.start_yaw+=512;}
        p.velocity.y=std::min(0x12c0*MovementTuning::velocity_unit,p.velocity.y+0x80*MovementTuning::velocity_unit);
        original_yaw=ride.start_yaw+ride.turn_delta*(rise-1024)/std::max(1.0f,ride.rise_span)+(rise-1024);
    } else {
        p.velocity.y=left*0x12c0/1024*MovementTuning::velocity_unit;
        const auto heading=retail::native_heading(o.rotation[2]*16);
        target+=retail::rotate_model({0,0,w.radius+1},heading);max_horizontal=0x800*MovementTuning::velocity_unit;
        original_yaw=o.rotation[2]*16-left*left/2048;
    }
    auto d=target-p.position;d.y=0;const float distance=length(d);const auto velocity=distance>0?d*(std::min(distance*60/64,max_horizontal)/distance):Vec3{};
    p.velocity.x=velocity.x;p.velocity.z=velocity.z;p.yaw=retail::native_heading(original_yaw);p.pitch=p.roll=0;
    p.grounded=false;p.airborne_ticks=4;
    if((left<1024 && horizontal(p.position-o.position)>w.radius) || ride.ticks>2400) {
        ride.index=UINT32_MAX;ride.cooldown=60;p.air.gliding=true;p.air.glide_started=true;p.air.glide_ceiling=p.position.y;
        p.velocity=retail::rotate_model({0,0,g.tuning.glide_speed},retail::native_heading(o.rotation[2]*16));
    }
    return true;
}
}
