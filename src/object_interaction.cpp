#include "spyro/object_interaction.hpp"
#include "spyro/enemy.hpp"
#include "spyro/powerups.hpp"
#include "spyro/spyro3.hpp"
#include <bit>

namespace spyro {
namespace {
constexpr float unit=1/retail::units_per_world;
bool bottle_protected(const RetailObject& object,std::span<const RetailObject> objects) {
    if(!object.container)return true;
    const auto link=read_u32(object.container->parameters,0);
    if(link==UINT32_MAX)return true;
    const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& other){return other.id==link;});
    return found==objects.end() || found->state!=0;
}
int trig(const std::array<std::int16_t,256>& table,unsigned angle) {
    const auto index=(angle>>4)&255;
    return table[index]+((table[(index+1)&255]-table[index])*static_cast<int>(angle&15)>>4);
}
std::optional<ObjectContact> terrain_contact(const Level& level,Vec3 center,float radius) {
    std::optional<ObjectContact> result;
    for(const auto index:level.collision_candidates(center.x-radius,center.z-radius,center.x+radius,center.z+radius)) {
        const auto& triangle=level.triangles[index];
        if(center.y+radius<std::min({triangle.a.y,triangle.b.y,triangle.c.y}) || center.y-radius>std::max({triangle.a.y,triangle.b.y,triangle.c.y}))continue;
        const auto delta=center-closest_point(center,triangle);const auto distance=length(delta);
        if(distance>=radius || (result && result->penetration>=radius-distance))continue;
        auto normal=distance>0.000001f?delta/distance:normalized(cross(triangle.b-triangle.a,triangle.c-triangle.a));
        result=ObjectContact{normal,radius-distance};
    }
    return result;
}
void align_gem(RetailObject& object,Vec3 normal) {
    if(normal.y<0)normal=normal*-1;
    constexpr float byte_angles=128/std::numbers::pi_v<float>;
    object.ground_angles={-std::atan2(normal.z,std::hypot(normal.x,normal.y))*byte_angles,-std::atan2(normal.x,normal.y)*byte_angles};
    if(std::abs(object.ground_angles[0])>=1)object.rotation[2]=0;
}
}
std::optional<ObjectContact> container_contact(const RetailObject& object,const ObjectModels& models,Vec3 center,float radius) {
    if(object.boss_body && object.state>=0 && !object.collected && !object.hidden) {
        const auto& body=*object.boss_body;
        const auto axis=object.position+Vec3{0,std::clamp(center.y-object.position.y,body.bottom,body.top),0};
        auto delta=center-axis;const auto distance=length(delta),overlap=radius+body.radius-distance;
        if(overlap<=0)return {};
        return ObjectContact{distance>1e-6f?delta/distance:Vec3{1,0,0},overlap};
    }
    if((!is_breakable(object.type) && !is_heavy_container(object.type) && !is_butterfly_source(object.type) && !object.enemy && !object.power_target && !object.scripted_contact) || object.state<0 || object.collected || object.hidden ||
       object.pc_carried || (object.enemy && object.enemy->defeated))return {};
    const auto bank=models.models.find(object.type);
    if(bank==models.models.end())return {};
    if(!bank->second.cylinder) {
        if((!object.power_target && !object.enemy && !is_heavy_container(object.type) && !is_butterfly_source(object.type) && !object.scripted_contact) || bank->second.meshes.empty())return {};
        const auto& model=bank->second;
        const auto scale=model.animated?object_model_scale(object):1.0f;
        const float broad_radius=model.animated?std::max(model.animations.at(object.animation.clip).radius,model.animations.at(object.animation.next_clip).radius):model.meshes.at(object.model).radius;
        if(length(center-object.position)>broad_radius*scale+radius)return {};
        const auto pose=model.animated?sample_object_animation(model,object.animation,models.animation_blends):ObjectPose{};
        const auto& mesh=model.meshes[model.animated?pose.mesh:object.model];
        const auto& vertices=model.animated?pose.vertices:mesh.vertices;
        const auto basis=object_basis(object,1);std::optional<ObjectContact> contact;
        for(const auto& face:mesh.faces) {
            const unsigned corners[]{0,1,2,1,3,2};
            for(unsigned tri=0;tri<(face.quad?2u:1u);++tri) {
                Vec3 p[3];for(unsigned i=0;i<3;++i)p[i]=object.position+basis*(vertices.at(face.vertices[corners[tri*3+i]])*scale);
                const auto delta=center-closest_point(center,Triangle{p[0],p[1],p[2]});const auto distance=length(delta);
                if(distance>=radius || (contact && contact->penetration>=radius-distance))continue;
                contact=ObjectContact{distance>0.000001f?delta/distance:normalized(cross(p[1]-p[0],p[2]-p[0])),radius-distance};
            }
        }
        return contact;
    }
    const auto& cylinder=*bank->second.cylinder;
    const auto d=retail::from_world(center)-retail::from_world(object.position);
    const auto square=[](int n){return static_cast<std::int64_t>(n)*n;};
    const int r=static_cast<int>(std::lround(radius*retail::units_per_world));
    if(square(d.x)+square(d.y)+square(d.z)>=square(r+static_cast<int>(cylinder.broad_radius)))return {};
    if(square(d.x)+square(d.y)>=square(r+static_cast<int>(cylinder.radius)))return {};
    // The retail cap test uses 11/16 of the querying sphere's radius as a
    // vertical margin, then checks a full sphere at either endpoint.
    const int margin=(r>>1)+(r>>3)+(r>>4);int height=0;unsigned shape_radius=cylinder.radius;
    if(d.z+margin<cylinder.bottom) {height=d.z-cylinder.bottom;shape_radius=cylinder.cap_radius;}
    else if(d.z-margin>cylinder.top) {height=d.z-cylinder.top;shape_radius=cylinder.cap_radius;}
    if(square(d.x)+square(d.y)+square(height)>=square(r+static_cast<int>(shape_radius)))return {};
    const auto delta=retail::to_world({d.x,d.y,height});const auto distance=length(delta);
    return ObjectContact{distance>0.000001f?delta/distance:Vec3{1,0,0},(r+shape_radius)*unit-distance};
}
bool container_accepts_attack(const RetailObject& object,std::span<const RetailObject> objects,std::uint32_t flags) {
    if(object.state<0 || !flags)return false;
    if(object.source_game==GameBuild::YearOfTheDragonUS && !object.container)return false;
    if(object.type==0xc8)return true;
    if(object.type==0xc9)return (flags&0x160000)!=0;
    // 800736e4: headbash is added only for CA. CB retains the common
    // 140000 mask (supercharge / powered flame), including normal immunity.
    if(object.type==0xca)return (flags&0x1c0000)!=0;
    if(object.type==0xcb)return (flags&0x140000)!=0;
    if(object.type==0xd1)return (flags&0xa0000)!=0 || ((flags&flame_attack) && !bottle_protected(object,objects));
    return false;
}
Vec3 ObjectInteractions::resolve_player(std::span<RetailObject> objects,const ObjectModels* models,Vec3 center,float radius,
                                        Vec3& velocity,bool& grounded,bool charging,std::uint32_t charging_flags) {
    if(!models)return center;
    for(auto& object:objects)if(const auto contact=container_contact(object,*models,center,radius)) {
        if(charging) {
            object.attack_flags|=charging_flags;
            if(!object.boss_body && (container_accepts_attack(object,objects,charging_flags) || enemy_accepts_attack(object,charging_flags) || power_target_accepts(object,charging_flags) || is_butterfly_source(object.type) || object.scripted_transient))continue;
        }
        center+=contact->normal*contact->penetration;
        const auto inward=dot(velocity,contact->normal);if(inward<0)velocity=velocity-contact->normal*inward;
        if(contact->normal.y>0.5f)grounded=true;
    }
    return center;
}
void ObjectInteractions::attack_sphere(std::span<RetailObject> objects,const ObjectModels& models,Vec3 center,float radius,std::uint32_t flags) {
    for(auto& object:objects)if(container_contact(object,models,center,radius))object.attack_flags|=flags;
}
void ObjectInteractions::start_flame() {
    if(flame_active_)return;
    flame_active_=true;flame_following_=true;flame_age_=0;flames_={};flame_pose_={};previous_flame_pose_={};flame_pattern_=random_.next()&1;
}
FlamePose ObjectInteractions::sample_flame(float alpha) const {
    const float blend=((phase_&1)?0.0f:1.0f)/2+std::clamp(alpha,0.0f,1.0f)/2;
    FlamePose result;result.base=lerp(previous_flame_pose_.base,flame_pose_.base,blend);
    result.yaw=previous_flame_pose_.yaw+std::remainder(flame_pose_.yaw-previous_flame_pose_.yaw,2*std::numbers::pi_v<float>)*blend;
    for(unsigned i=0;i<8;++i) {
        const auto a=previous_flame_pose_.streams[i],b=flame_pose_.streams[i];
        // Two streams are born on successive VBlanks of each script update.
        // A retired stream finishes its final interpolated interval once.
        result.streams[i].age=a.age>0?a.age+2*blend:(b.age>0?std::max(0.0f,b.age-2+2*blend):0);
        result.streams[i].cutoff=std::min(a.cutoff,b.cutoff);
    }
    return result;
}
void ObjectInteractions::flame_tick(std::span<RetailObject> objects,const ObjectModels& models,const Level& level,
                                    Vec3 origin,float yaw,Vec3 attachment) {
    previous_flame_pose_=flame_pose_;flame_points_.clear();flame_query_points_.clear();++flame_step_;
    if(!flame_active_)return;
    std::array<bool,8> emitted{};
    for(unsigned elapsed=0;elapsed<2;++elapsed,++flame_age_)if(flame_age_<8) {
        const auto index=models.flame.order[flame_pattern_*8+flame_age_];
        if(index>=flames_.size())throw FormatError("Invalid retail flame emission order");
        flames_[index]={2-elapsed,false,origin};emitted[index]=true;flame_pose_.streams[index].cutoff=32;
    }
    if(flame_following_) {
        flame_yaw_=yaw;flame_offset_=retail::rotate_model(attachment+models.flame.offsets[std::min(flame_age_/2,7u)],yaw);
    }
    const auto base=origin+flame_offset_;bool active=false;
    flame_pose_.base=base;flame_pose_.yaw=flame_yaw_;
    if(flame_age_==2) {previous_flame_pose_.base=base;previous_flame_pose_.yaw=flame_yaw_;}
    for(unsigned i=0;i<8;++i) {
        auto& flame=flames_[i];if(!flame.age)continue;
        if(!emitted[i])flame.age+=2;
        if(flame.age>=(i<4?30u:36u)) {flame.age=0;flame_pose_.streams[i].age=0;continue;}
        flame_pose_.streams[i].age=static_cast<float>(flame.age);
        active=true;const auto& path=models.flame.curves[i].points;
        if(flame.stopped || flame.age+1>=path.size())continue;
        const auto next=base+retail::rotate_model(path[flame.age+1].position,flame_yaw_);
        const auto line=next-flame.position;const auto distance=length(line);
        if(const auto hit=level.raycast(flame.position,line,distance);hit && hit->distance<distance) {
            // 8002f4f4 queries the moby spheres at the advanced point before the
            // terrain test (8003ce38) stops the stream, so a script moby with a
            // collision bank (Skelos Badlands' pool toads) is still reached by the
            // retail query (RetailScripts, flame_query_points).
            flame_query_points_.push_back(next);
            flame.position=hit->point;flame.stopped=true;flame_pose_.streams[i].cutoff=flame.age;continue;
        }
        flame.position=next;flame_points_.push_back(next);flame_query_points_.push_back(next);
        for(auto& object:objects)if(container_contact(object,models,next,160*unit)) {
            object.attack_flags|=flame_attack;flame.stopped=true;flame_pose_.streams[i].cutoff=flame.age;
        }
    }
    if(!active && flame_age_>=8)flame_active_=false;
}
void ObjectInteractions::release(std::vector<RetailObject>& pending,const RetailObject& parent,std::span<RetailObject> objects,
                                  const ObjectModels& models,const Level& level,Vec3 origin,Vec3 velocity,bool charging) {
    // 8007c180 supplies shared fragment initialization; the three break
    // handlers choose the actual class, mesh and effect flags.
    const bool heavy=is_heavy_container(parent.type);
    const unsigned kind=heavy?0x134:parent.type==0xc8?0x132:(parent.type==0xc9?0x133:0x136);
    const auto fragment_bank=models.models.find(kind);
    if(fragment_bank==models.models.end() || fragment_bank->second.meshes.size()<2)
        throw FormatError("Container is missing its retail fragment model");
    const auto start=retail::from_world(parent.position);
    // Summer Forest 8007374c..800737dc: both metal chest classes use
    // 16 class-134 fragments, amplitude 144, eight of each mesh, material 1.
    const int amplitude=heavy?144:parent.type==0xc8?112:100;
    for(unsigned i=0;i<(heavy?16u:12u);++i) {
        RetailObject fragment;fragment.type=static_cast<std::uint16_t>(kind);fragment.source_id=parent.id;
        fragment.runtime_spawned=true;fragment.draw_flags=0x20;
        const auto azimuth=random_.next()&4095,elevation=random_.next()&2047;
        const auto component=[&](int value) {return static_cast<int>(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value)));};
        fragment.script_velocity={component(static_cast<int>((static_cast<std::int64_t>(trig(models.cosine,elevation))*trig(models.cosine,azimuth)*amplitude)>>24)),
            component(static_cast<int>((static_cast<std::int64_t>(trig(models.cosine,elevation))*trig(models.sine,azimuth)*amplitude)>>24)),
            trig(models.sine,elevation)*amplitude>>12};
        if(charging && (parent.attack_flags&charge_attack)) {
            const auto step=retail::from_world(velocity/60);fragment.script_velocity.x+=step.x;fragment.script_velocity.y+=step.y;
        }
        const auto v=fragment.script_velocity;
        fragment.position=fragment.previous_position=retail::to_world(start+retail::IVec3{v.x*4,v.y*4,v.z*4});
        fragment.rotation={0,static_cast<float>(random_.next()&255),static_cast<float>(random_.next()&255)};
        fragment.previous_rotation=fragment.rotation;
        for(auto& spin:fragment.gem_spin)spin=static_cast<int>(random_.next()&31)-16;
        fragment.fragment_floor=start.z-64;fragment.lifetime=50+random_.next()%31;
        if(heavy) {fragment.model=i<8?0:1;fragment.variant=i<8?5:4;fragment.material_color=1;}
        else if(parent.type==0xc8) {fragment.model=i<5?0:1;fragment.variant=i<5?3:0;}
        else if(parent.type==0xc9) {fragment.model=i<5?0:1;fragment.variant=i<5?5:4;}
        else {fragment.model=i<6?0:1;fragment.variant=i<6?(bottle_protected(parent,objects)?9:3):0;}
        pending.push_back(std::move(fragment));
    }
    if(parent.source_game==GameBuild::YearOfTheDragonUS)for(auto& child:objects)
        if(child.source_type==0x3f3 && child.source_id==parent.id && !child.collected) {
            pending.push_back(make_yotd_reward_gem(child,models,origin,charging,random_));
            child.collected=true;child.state=-1;++released_;
        }
    if(parent.held_gem==255)return;
    if(parent.held_gem>=models.gem_variants.size())throw FormatError("Invalid container gem denomination");
    RetailObject gem;gem.type=1;gem.runtime_spawned=true;gem.draw_flags=0x18;gem.source_id=parent.id;
    gem.held_gem=parent.held_gem;gem.variant=static_cast<std::uint8_t>(models.gem_variants[gem.held_gem]);
    if(gem.variant>=5 || models.gem_values[gem.variant]!=gem.held_gem)throw FormatError("Unmapped container gem denomination");
    gem.model=static_cast<std::uint8_t>(models.gem_meshes[gem.variant]);gem.material_color=models.gem_colors[gem.variant];
    gem.gem_value=models.gem_values[gem.variant];gem.state=2;gem.bounces=3;
    const auto position=start+retail::IVec3{0,0,256};gem.position=gem.previous_position=retail::to_world(position);
    if(charging && models.math.length(position-retail::from_world(origin))<2560) {
        gem.state=3;gem.gem_flight_origin=gem.position;
        for(auto& spin:gem.gem_spin)spin=static_cast<int>(random_.next()&14)-7;
    } else {
        auto target=start;
        for(unsigned attempt=0;attempt<4;++attempt) {
            const auto candidate=start+retail::IVec3{-512+static_cast<int>(random_.next()&1023),-512+static_cast<int>(random_.next()&1023),1024};
            const auto floor=level.raycast(retail::to_world(candidate),{0,-1,0},2048*unit);
            if(!floor || floor->point.y<=0 || std::abs(floor->normal.y)<std::cos(24*16*retail::angle_to_radians))continue;
            bool blocked=false;
            for(const auto& other:objects)if(other.id!=parent.id && container_contact(other,models,floor->point,200*unit)) {blocked=true;break;}
            if(!blocked) {target=retail::from_world(floor->point);break;}
        }
        // 80039334 solves the integer flight duration with launch speed 140
        // and gravity 10, then divides horizontal displacement toward zero.
        int z=position.z,speed=140,ticks=0;
        do {z+=speed;speed-=10;++ticks;}while(speed>0 || z>target.z);
        gem.script_velocity=(target-position)/ticks;gem.script_velocity.z=140;
    }
    ++released_;pending.push_back(std::move(gem));
}
bool ObjectInteractions::smash_carried(std::vector<RetailObject>& objects,unsigned index,const ObjectModels& models,
                                      const Level& level,Vec3 origin,Vec3 velocity) {
    if(index>=objects.size())return false;
    const auto parent=objects[index];
    if((parent.source_game==GameBuild::YearOfTheDragonUS && !parent.container) || parent.collected || parent.state<0 || (!is_breakable(parent.type) && !is_heavy_container(parent.type)))return false;
    std::vector<RetailObject> pending;release(pending,parent,objects,models,level,origin,velocity,false);
    objects[index].pc_carried=false;objects[index].attack_flags=0;objects[index].state=-1;++broken_;
    std::uint32_t id=0;for(const auto& object:objects)id=std::max(id,object.id+1);
    for(auto& object:pending){object.id=id++;objects.push_back(std::move(object));}
    return true;
}
void ObjectInteractions::gem_tick(RetailObject& gem,const ObjectModels& models,const Level& level,Vec3 origin,unsigned& total) {
    begin_object_transform(gem,2);gem.gem_age+=2;
    auto step=gem.script_velocity;const int distance=models.math.length(step);
    if(distance>216)step=retail::rescale(step,distance,216);
    auto next=retail::from_world(gem.position)+step;
    if(gem.script_velocity.z>=-215)gem.script_velocity.z-=10;
    if(next.z<1024) {gem.collected=true;total+=gem.gem_value;return;}
    const auto center=retail::to_world(next+retail::IVec3{0,0,240});
    if(const auto contact=terrain_contact(level,center,240*unit)) {
        next=retail::from_world(center+contact->normal*contact->penetration)-retail::IVec3{0,0,240};
        if(!gem.bounces) {
            if(const auto floor=level.raycast(retail::to_world(next+retail::IVec3{0,0,240}),{0,-1,0},1024*unit);
               floor && floor->point.y>(next.z-160)*unit && std::abs(floor->normal.y)>std::cos(24*16*retail::angle_to_radians)) {
                next=retail::from_world(floor->point);gem.state=1;align_gem(gem,floor->normal);
            } else ++gem.bounces;
        }
        if(gem.bounces) {
            // 8001bbac reflects only incoming velocity. Quantize the normal
            // before the same integer dot / division / shift operations.
            const auto n=retail::from_world(contact->normal*4);const retail::IVec3 q{n.x>>4,n.y>>4,n.z>>4};
            auto& v=gem.script_velocity;const int inward=-(v.x*q.x+v.y*q.y+v.z*q.z),norm=q.x*q.x+q.y*q.y+q.z*q.z;
            if(inward>=0 && norm) {
                const int gain=inward*64/norm;v={v.x+((q.x*gain)>>6)*2,v.y+((q.y*gain)>>6)*2,v.z+((q.z*gain)>>6)*2};
                --gem.bounces;v.x=(v.x>>3)+static_cast<int>(random_.next()&63)-32;
                v.y=(v.y>>3)+static_cast<int>(random_.next()&63)-32;v.z=(v.z>>2)+static_cast<int>(random_.next()&15);
            }
        }
    }
    gem.position=retail::to_world(next);
    gem.rotation[0]=gem.ground_angles[0]+(models.cosine[gem.gem_phase]>>7);
    gem.rotation[1]=gem.ground_angles[1]+(models.sine[gem.gem_phase]>>7);gem.gem_phase=(gem.gem_phase+4)&255;
    if(gem.gem_age>=30 && gem_touching_spyro(gem.position,origin)) {gem.collected=true;total+=gem.gem_value;}
}
void ObjectInteractions::fragment_tick(RetailObject& fragment,const Level& level) {
    if(!fragment.lifetime || (!(fragment.variant&1) && retail::from_world(fragment.position).z<=fragment.fragment_floor)) {
        fragment.state=-1;return;
    }
    begin_object_transform(fragment,2);auto& v=fragment.script_velocity;
    if(fragment.variant&1)if(const auto contact=terrain_contact(level,fragment.position,256*unit)) {
        const auto velocity=retail::to_world(v);const auto inward=dot(velocity,contact->normal);
        if(inward<0)v=retail::from_world(velocity-contact->normal*(inward*1.75f));
    }
    auto next=retail::from_world(fragment.position);next.x+=v.x;next.y+=v.y;
    v.z=std::max(-128,v.z-((fragment.variant&8)?3:6));next.z+=v.z;
    fragment.position=retail::to_world(next);
    for(unsigned i=0;i<3;++i)fragment.rotation[i]=static_cast<float>((static_cast<int>(fragment.rotation[i])+fragment.gem_spin[i])&255);
    --fragment.lifetime;
}
void ObjectInteractions::tick(std::vector<RetailObject>& objects,const ObjectModels* models,const Level& level,Vec3 origin,
                              Vec3 velocity,float yaw,Vec3 attachment,bool charging,unsigned& total) {
    if(!models)return;
    const bool script_tick=(phase_++&1)==0;
    // Carry/throw owns its 60 Hz transform. Advancing it here a second time
    // makes the object lead Spyro's interpolated paws, especially in turns.
    for(auto& object:objects)if(!object.pc_carried && (is_breakable(object.type) || is_heavy_container(object.type) || is_container_fragment(object.type) || (object.type==1 && object.state==2)))
        object.transform_phase=std::min(object.transform_phase+1,object.transform_span);
    if(!script_tick)return;
    flame_tick(objects,*models,level,origin,yaw,attachment);
    std::vector<RetailObject> pending;
    for(auto& object:objects) {
        if(object.state<0 || object.collected || object.pc_carried)continue;
        if(is_container_fragment(object.type)) {fragment_tick(object,level);continue;}
        if(object.type==1 && object.state==2) {gem_tick(object,*models,level,origin,total);continue;}
        if((!is_breakable(object.type) && !is_heavy_container(object.type)) || !object.container)continue;
        begin_object_transform(object,2);auto& data=*object.container;
        if(data.wobble_age) {
            data.wobble_age+=2;
            if(data.wobble_age>=64) {data.wobble_age=0;object.position=data.rest_position;object.rotation=data.rest_rotation;}
            else {
                const auto index=data.wobble_age&~1u;const int roll=data.wobble[index],pitch=data.wobble[index+1];
                object.rotation[0]=data.rest_rotation[0]+roll;object.rotation[1]=data.rest_rotation[1]+pitch;
                object.position.y=data.rest_position.y+(std::abs(roll)+std::abs(pitch))*6*unit;
            }
        }
        if(container_accepts_attack(object,objects,object.attack_flags)) {
            release(pending,object,objects,*models,level,origin,velocity,charging);
            object.state=-1;++broken_;
        } else if(object.type==0xc9 || is_heavy_container(object.type)) {
            if(data.heat || (object.attack_flags&flame_attack)) {
                data.heat=(object.attack_flags&flame_attack)?std::min(data.heat+16,63u):data.heat-(data.heat>33?2:1);
                object.material_color=0x30000080+(data.heat>>2)*0x1000000;
            }
            if((object.attack_flags&0x30000) && !data.wobble_age) {
                data.wobble_age=1;data.rest_position=object.position;data.rest_rotation=object.rotation;
            }
        } else if(object.type==0xd1 && bottle_protected(object,objects)) {
            if(!object.variant)object.variant=static_cast<std::uint8_t>(random_.next());
            const auto angle=(phase_*2+object.variant)&255;
            object.rotation[0]=static_cast<float>(models->cosine[angle]>>9);object.rotation[1]=static_cast<float>(models->sine[angle]>>9);
        }
        object.attack_flags=0;
    }
    if(!pending.empty()) {
        std::uint32_t id=0;for(const auto& object:objects) {
            if(object.id==UINT32_MAX)throw FormatError("No free runtime object ID");id=std::max(id,object.id+1);
        }
        for(auto& object:pending) {if(id==UINT32_MAX)throw FormatError("No free runtime object ID");object.id=id++;objects.push_back(std::move(object));}
    }
}
} // namespace spyro
