#include "spyro/universe.hpp"
#include "spyro/runtime.hpp"
#include <set>
#include <numbers>

namespace spyro {
namespace {
constexpr float pi=std::numbers::pi_v<float>,grid=2;
std::uint64_t mix(std::uint64_t x) {
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
struct Random {
    std::uint64_t state;
    std::uint64_t next(){state+=0x9e3779b97f4a7c15ULL;return mix(state);}
    unsigned pick(unsigned n){return static_cast<unsigned>(next()%n);}
    float range(float lo,float hi){return lo+(hi-lo)*float(next()>>40)/16777216.0f;}
};
constexpr unsigned slots[]{17,33,35,37};
constexpr const char* biomes[]{"Glimmer gardens","Autumn ruins","Badlands mesas","Glacier terraces"};
unsigned theme(UniverseAddress a){return static_cast<unsigned>(mix(a.seed^mix(a.world))%4);}
float horizontal(Vec3 a){return std::hypot(a.x,a.z);}
struct Nearest {float distance=1e30f;Vec3 point;};
Nearest nearest(Vec3 p,std::span<const Vec3> route) {
    Nearest best;
    for(std::size_t i=1;i<route.size();++i) {
        auto d=route[i]-route[i-1];const float length2=d.x*d.x+d.z*d.z;
        const auto v=p-route[i-1];const float t=length2>0?std::clamp((v.x*d.x+v.z*d.z)/length2,0.0f,1.0f):0;
        const auto q=route[i-1]+d*t;const float distance=horizontal(q-p);
        if(distance<best.distance)best={distance,q};
    }
    return best;
}
float noise(Vec3 p,std::uint64_t seed) {
    const auto phase=float(seed&65535)*0.001f;
    return std::sin(p.x*0.21f+phase)*std::cos(p.z*0.17f-phase);
}
float land_margin(Vec3 p,const UniverseLayout& world) {
    float margin=8.5f+noise(p,world.address.seed)*0.7f-nearest(p,world.route).distance;
    for(const auto room:world.route)margin=std::max(margin,13.0f-horizontal(p-room));
    return margin;
}
bool overlaps(const Bounds& a,const Bounds& b,float margin=0) {
    return a.min.x<b.max.x+margin && a.max.x>b.min.x-margin && a.min.z<b.max.z+margin && a.max.z>b.min.z-margin;
}
float route_distance(const Bounds& b,const UniverseLayout& layout) {
    // A containing circle is conservative even when a building is rotated.
    const auto center=b.center();return nearest(center,layout.route).distance-horizontal(b.max-b.min)*0.5f;
}
bool placeable(const Bounds& bounds,const UniverseLayout& layout,float route_clearance) {
    if(route_distance(bounds,layout)<route_clearance)return false;
    const float radius=horizontal(bounds.max-bounds.min)*0.5f;
    if(horizontal(bounds.center()-layout.landing)<layout.landing_radius+radius+1)return false;
    for(const auto& portal:layout.portals)if(horizontal(bounds.center()-portal.center)<portal.radius+radius+2)return false;
    for(const auto& old:layout.footprints)if(overlaps(bounds,old.bounds,0.7f))return false;
    for(const auto x:{bounds.min.x,bounds.max.x})for(const auto z:{bounds.min.z,bounds.max.z})
        if(land_margin({x,0,z},layout)<2)return false;
    return true;
}
float height_at(Vec3 p,const UniverseLayout& layout) {
    const auto path=nearest(p,layout.route);
    float height=path.point.y+noise(p,layout.address.seed^layout.address.world)*std::clamp((path.distance-3.7f)*0.16f,0.0f,0.7f);
    height=std::lerp(layout.landing.y,height,std::clamp((horizontal(p-layout.landing)-8)/2,0.0f,1.0f));
    for(const auto& site:layout.footprints)if(site.structure) {
        const auto& b=site.bounds;
        const float d=std::max({b.min.x-p.x,p.x-b.max.x,b.min.z-p.z,p.z-b.max.z,0.0f});
        height=std::lerp(b.min.y,height,std::clamp((d-0.2f)/2,0.0f,1.0f));
    }
    return height;
}
std::array<unsigned,3> materials(const Level& source) {
    const auto count=source.terrain_materials.size();if(!count)throw FormatError("Universe theme has no retail terrain materials");
    std::vector<double> floor(count),wall(count);std::vector<bool> solid(count,true);
    for(unsigned i=0;i<count;++i) {
        if(source.terrain_materials[i].translucent){solid[i]=false;continue;}
        for(unsigned y=0;y<64 && solid[i];++y)for(unsigned x=0;x<64;++x) {
            const auto at=(std::size_t(i/16*64+y)*source.texture_width+i%16*64+x)*4+3;
            if(at>=source.texture_rgba.size() || !source.texture_rgba[at]){solid[i]=false;break;}
        }
    }
    for(std::size_t i=0;i+2<source.visual_vertices.size();i+=3) {
        const auto& a=source.visual_vertices[i];const auto& b=source.visual_vertices[i+1];const auto& c=source.visual_vertices[i+2];
        if(a.material>=count || !solid[a.material])continue;
        const auto n=cross(b.position-a.position,c.position-a.position);const auto area=length(n);
        if(area<0.001f)continue;
        if(std::abs(n.y)/area>0.85f)floor[a.material]+=area;else wall[a.material]+=area;
    }
    auto choose=[&](const auto& scores,unsigned avoid) {
        unsigned best=UINT32_MAX;double score=-1;
        for(unsigned i=0;i<count;++i)if(solid[i] && i!=avoid && scores[i]>score){best=i;score=scores[i];}
        if(best==UINT32_MAX)throw FormatError("Universe theme has too few opaque materials");return best;
    };
    const auto ground=choose(floor,UINT32_MAX),path=choose(floor,ground),stone=choose(wall,UINT32_MAX);
    // Skelos' other large horizontal surface is its lava artwork. Use its
    // opaque rock material for the safe route, rather than suggesting a hazard.
    return {ground,source.wad_slot==35?stone:path,stone};
}
void triangle(Level& level,Vec3 a,Vec3 b,Vec3 c,unsigned material,Vec3 color,
              std::array<std::array<float,2>,3> uv={{{0,0},{1,0},{0,1}}},bool collision=true) {
    if(length(cross(b-a,c-a))<0.00001f)return;
    if(collision)level.triangles.push_back({a,b,c,static_cast<unsigned>(level.triangles.size())});
    for(unsigned i=0;i<3;++i) {
        const auto p=i==0?a:i==1?b:c;level.bounds.add(p);
        const float u=(material%16*64+0.5f+uv[i][0]*63)/level.texture_width;
        const float v=(material/16*64+0.5f+uv[i][1]*63)/level.texture_height;
        level.visual_vertices.push_back({p,color,u,v,material});
    }
    ++level.terrain_faces;
}
void quad(Level& level,Vec3 a,Vec3 b,Vec3 c,Vec3 d,unsigned material,Vec3 color,bool collision=true) {
    triangle(level,a,b,c,material,color,{{{0,0},{1,0},{0,1}}},collision);
    triangle(level,b,d,c,material,color,{{{1,0},{1,1},{0,1}}},collision);
}
void box(Level& level,const Bounds& b,unsigned material,Vec3 color) {
    const auto a=b.min,z=b.max;
    quad(level,{a.x,z.y,a.z},{a.x,z.y,z.z},{z.x,z.y,a.z},{z.x,z.y,z.z},material,color);
    quad(level,{a.x,a.y,a.z},{z.x,a.y,a.z},{a.x,z.y,a.z},{z.x,z.y,a.z},material,color*0.86f);
    quad(level,{z.x,a.y,z.z},{a.x,a.y,z.z},{z.x,z.y,z.z},{a.x,z.y,z.z},material,color*0.76f);
    quad(level,{a.x,a.y,z.z},{a.x,a.y,a.z},{a.x,z.y,z.z},{a.x,z.y,a.z},material,color*0.9f);
    quad(level,{z.x,a.y,a.z},{z.x,a.y,z.z},{z.x,z.y,a.z},{z.x,z.y,z.z},material,color*0.8f);
}
Bounds object_bounds(const RetailObject& object,const ObjectModels& models,bool all_frames=false) {
    const auto& model=models.models.at(object.type);Bounds b;const auto basis=object_basis(object,1);
    const auto scale=model.animated?object_model_scale(object):1.0f;
    const auto add=[&](std::span<const Vec3> vertices){for(const auto p:vertices)b.add(object.position+basis*(p*scale));};
    if(model.animated) {
        if(all_frames){for(const auto& clip:model.animations)for(const auto& frame:clip.frames)add(frame.vertices);}
        else add(sample_object_animation(model,object.animation,models.animation_blends).vertices);
    } else add(model.meshes.at(object.model).vertices);
    if(all_frames && object.enemy) {
        // Enemies turn to face Spyro. Reserve their full rotating animation
        // envelope, including an extended club, instead of only the idle pose.
        float radius=0;
        for(const auto& clip:model.animations)for(const auto& frame:clip.frames)for(const auto v:frame.vertices)
            radius=std::max(radius,horizontal(v)*scale);
        b.min.x=object.position.x-radius;b.max.x=object.position.x+radius;
        b.min.z=object.position.z-radius;b.max.z=object.position.z+radius;
    }
    return b;
}
RetailObject clone(const Level& source,unsigned type,const ObjectModels& models) {
    const auto found=std::find_if(source.objects.begin(),source.objects.end(),[&](const auto& o){return o.type==type && o.state>=0;});
    if(found==source.objects.end())throw FormatError("Missing curated universe actor");
    auto object=*found;object.position=object.previous_position={};object.rotation=object.previous_rotation={0,0,64};
    object.record_offset=object.data_offset=0;object.record[60]=object.record[61]=object.record[62]=object.record[63]=0;
    object.collected=object.audio_finished=object.runtime_spawned=false;object.state=0;object.attack_flags=0;
    object.interpolate_position=false;object.source_id=UINT32_MAX;
    if(models.models.at(type).animated){object.model=0;object.animation=object.previous_animation=start_object_animation(models.models.at(type),0);}
    if(object.enemy){const auto original=*object.enemy;object.enemy=EnemyState{};object.enemy->kind=original.kind;object.enemy->notice=original.notice;object.enemy->reach=original.reach;}
    if(object.container){object.container->rest_position={};object.container->rest_rotation=object.rotation;object.container->heat=object.container->wobble_age=0;}
    return object;
}
void actor(Level& level,RetailObject object,Vec3 position,const ObjectModels& models,bool objective=false) {
    object.id=static_cast<unsigned>(level.objects.size());
    const auto floor=level.raycast(position+Vec3{0,5,0},{0,-1,0},12);
    if(!floor || floor->normal.y<0.9f)throw FormatError("Universe actor has no level supporting floor");
    object.position=object.previous_position={position.x,floor->point.y,position.z};
    if(object.container)object.container->rest_position=object.position;
    if(object.type!=1) {
        auto b=object_bounds(object,models,true);
        if(object.enemy && object.enemy->kind==EnemyKind::SmallLizard)b.max.z+=2;
        level.universe->footprints.push_back({b,object.id,false});
        if(object.enemy)++level.universe->enemies;
    }
    if(objective)level.universe->objective_gems.push_back(object.id);
    level.objects.push_back(std::move(object));
}
void portal_frame(Level& level,UniversePortal& portal,const Level& source,const ObjectModels& models) {
    auto object=clone(source,0x1aa,models);object.id=static_cast<unsigned>(level.objects.size());
    portal.object=object.id;portal.scale=level.universe->address.space?4.0f:1.0f;
    object.size=level.universe->address.space?8:32;object.state=2;object.draw_flags=0x20;object.material_color=0;
    object.model=0;object.record[61]=0;object.power_target.reset();
    object.animation=object.previous_animation=start_object_animation(models.models.at(0x1aa),0);
    object.position=object.previous_position=portal.center-Vec3{0,portal.radius,0};
    const auto pose=sample_object_animation(models.models.at(0x1aa),object.animation,models.animation_blends);
    for(unsigned i=0;i<5;++i)portal.aperture[i]=object.position+level.universe->return_portal.aperture[i]*portal.scale;
    // Use the arch's actual mesh as collision; the five-sided opening remains
    // empty. Model scale is unchanged on ground worlds (retail size 32).
    const auto& mesh=models.models.at(0x1aa).meshes.at(pose.mesh);
    const auto in_aperture=[&](Vec3 v) {
        const auto local=(v-object.position)/portal.scale;
        if(std::abs(local.z)>0.06f)return false;
        const auto& aperture=level.universe->return_portal.aperture;
        for(unsigned i=0;i<5;++i) {
            const auto a=aperture[i],b=aperture[(i+1)%5],d=b-a;
            if(d.x*(local.y-a.y)-d.y*(local.x-a.x)>0.08f*length(d))return false;
        }
        return true;
    };
    for(const auto& face:mesh.faces) {
        const auto vertex=[&](unsigned k){return object.position+pose.vertices.at(face.vertices[k])*portal.scale;};
        const auto add=[&](unsigned a,unsigned b,unsigned c){
            const auto va=vertex(a),vb=vertex(b),vc=vertex(c);
            // The retail mesh includes the flat opening mask, which is a
            // drawable surface, not solid architecture. Do not seal the exit.
            if(in_aperture(va) && in_aperture(vb) && in_aperture(vc))return;
            level.triangles.push_back({va,vb,vc,static_cast<unsigned>(level.triangles.size())});
        };
        const std::array<Vec3,4> corners{vertex(0),vertex(1),vertex(2),vertex(3)};
        for(const float side:{-1.0f,1.0f}) {
            const auto visible=object_face_view(face,corners,portal.center+Vec3{0,0,side*10});
            if(!visible.count || object_face_pass(face,visible.backface,10)==ObjectPass::Hidden)continue;
            const auto& c=visible.corners;add(c[0],c[1],c[2]);if(visible.count==4)add(c[1],c[3],c[2]);
        }
    }
    level.objects.push_back(std::move(object));
}
void sphere(Level& level,Vec3 center,float radius,unsigned material,Vec3 tint,std::uint64_t seed) {
    const auto vertex=[&](unsigned x,unsigned y){const auto a=x*2*pi/20,b=y*pi/12;return center+Vec3{std::cos(a)*std::sin(b),std::cos(b),std::sin(a)*std::sin(b)}*radius;};
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<20;++x) {
        const auto a=vertex(x,y),b=vertex(x+1,y),c=vertex(x,y+1),d=vertex(x+1,y+1);
        const float lighting=0.48f+0.52f*std::max(0.0f,dot(normalized((a+b+c+d)/4-center),normalized(Vec3{-1,2,-2})));
        const auto mat=(mix(seed+x+y*20)%5==0)?level.universe->materials[2]:material;
        quad(level,a,b,c,d,mat,tint*lighting,false);
    }
}
}
unsigned universe_source_slot(UniverseAddress a){return a.space?17:slots[theme(a)];}
std::string universe_name(UniverseAddress a) {
    constexpr const char* names[]{"Amethyst","Amber","Opal","Silver","Emerald","Twilight","Sapphire","Sunlit"};
    constexpr const char* places[]{"Reach","Gardens","Terraces","Heights","Hollow","Sanctuary","Isles","Crossing"};
    const auto n=mix(a.seed^mix(a.world));return std::string(names[n&7])+" "+places[n>>8&7]+" "+std::to_string(a.world);
}
UniverseLayout plan_universe(UniverseAddress address) {
    UniverseLayout p;p.address=address;p.name=address.space?"The Starways":universe_name(address);p.biome=address.space?"Open space":biomes[theme(address)];
    Random random{mix(address.seed^mix(address.world))};
    if(address.space) {
        p.landing={0,10,0};
        for(unsigned i=0;i<3;++i) {
            auto target=address;target.space=false;target.world+=i;
            p.portals.push_back({{(int(i)-1)*23.0f,10.0f+i*3,48.0f+i*15},1700.0f/512,target,universe_name(target),{0.35f+i*0.2f,0.75f,1-i*0.2f}});
            p.planets.push_back({p.portals.back().center+Vec3{0,0,16},8+i*1.5f,i});
        }
        auto more=address;more.world+=3;
        // Keep the continuation above the central planet, with a clear flight
        // approach and its own visible opening from the arrival position.
        p.portals.push_back({{0,36,100},1700.0f/512,more,"Further stars",{0.8f,0.45f,1}});
        for(unsigned i=0;i<280;++i) {
            auto direction=normalized(Vec3{random.range(-1,1),random.range(-0.7f,1),random.range(-1,1)});
            const auto c=random.range(0.35f,0.95f);
            p.stars.push_back({direction*random.range(230,340),{c*0.8f,c*0.88f,c},random.range(0.12f,0.55f)});
        }
        return p;
    }
    p.landing={0,8,0};p.route.push_back(p.landing);const auto rooms=5+random.pick(3);
    for(unsigned i=1;i<rooms;++i) {
        const auto previous=p.route.back();p.route.push_back({std::clamp(previous.x+random.range(-7,7),-12.0f,12.0f),
            previous.y+(i==1?0:random.range(-0.6f,1.3f)),previous.z+random.range(13,17)});
    }
    auto space=address;space.space=true;
    p.portals.push_back({p.landing+Vec3{0,1700.0f/2048,-6},1700.0f/2048,space,"Return to the Starways",{0.35f,0.65f,1}});
    p.portals.push_back({p.route.back()+Vec3{0,1700.0f/2048,3},1700.0f/2048,space,"Explore new worlds",{0.65f,0.35f,1}});
    for(std::size_t i=1;i+1<p.route.size();++i)for(const float side:{-1.0f,1.0f}) {
        const auto center=p.route[i]+Vec3{side*random.range(8.5f,9.5f),0,random.range(-1.5f,1.5f)};
        const float w=random.range(1.0f,1.7f),d=random.range(1,1.5f),h=random.range(3.2f,6.5f);
        Bounds b{center-Vec3{w,0,d},center+Vec3{w,h,d}};
        if(placeable(b,p,3.6f)){p.footprints.push_back({b,UINT32_MAX,true});++p.structures;}
    }
    return p;
}
Level generate_universe(UniverseAddress address,const Level& source,const Level& actors,const ObjectModels& models,const ReturnPortalAssets& portal) {
    Level level;level.wad_slot=universe_source_slot(address);level.universe=plan_universe(address);
    auto& p=*level.universe;p.return_portal=portal;p.materials=materials(source);level.sky_color=address.space?Vec3{0.012f,0.009f,0.035f}:source.sky_color;
    if(!address.space){level.sky_vertices=source.sky_vertices;level.sky_regions=source.sky_regions;}
    level.texture_width=source.texture_width;level.texture_height=source.texture_height;level.texture_rgba=source.texture_rgba;level.terrain_materials=source.terrain_materials;
    level.spawn=p.landing;level.spawn_yaw=0;
    if(address.space) {
        // A remote floor satisfies the common renderer's collision contract.
        // Space travel itself has no walking floor or invisible blockers.
        level.triangles.push_back({{-400,-200,-400},{400,-200,400},{400,-200,-400},0});
        level.bounds.add({-400,-200,-400});level.bounds.add({400,350,400});
        for(const auto& planet:p.planets) {
            const auto& gate=p.portals[planet.portal];const auto t=theme(gate.destination);
            constexpr Vec3 colors[]{{0.8f,1.1f,0.8f},{1.2f,0.9f,0.8f},{1.2f,0.6f,0.45f},{0.65f,1,1.4f}};
            sphere(level,planet.center,planet.radius,p.materials[0],colors[t],gate.destination.world^address.seed);
        }
    } else {
        Bounds ground;for(auto point:p.route){ground.add(point-Vec3{15,0,15});ground.add(point+Vec3{15,0,15});}
        const int xmin=int(std::floor(ground.min.x/grid)),xmax=int(std::ceil(ground.max.x/grid));
        const int zmin=-8,zmax=int(std::ceil(ground.max.z/grid));
        std::set<std::pair<int,int>> cells;
        for(int z=zmin;z<zmax;++z)for(int x=xmin;x<xmax;++x)if(land_margin({(x+0.5f)*grid,0,(z+0.5f)*grid},p)>0)cells.emplace(x,z);
        const auto point=[&](int x,int z){Vec3 v{x*grid,0,z*grid};v.y=height_at(v,p);return v;};
        for(const auto [x,z]:cells) {
            const auto a=point(x,z),b=point(x,z+1),c=point(x+1,z),d=point(x+1,z+1);
            const auto center=(a+b+c+d)/4;const auto route=nearest(center,p.route);
            const bool pad=horizontal(center-p.landing)<4.6f;
            const auto material=route.distance<2.9f || pad?p.materials[1]:p.materials[0];
            const float light=0.9f+0.12f*noise(center,address.seed);
            quad(level,a,b,c,d,material,{light,light,light});
            const auto skirt=[&](Vec3 left,Vec3 right){quad(level,left,right,{left.x,p.landing.y-7,left.z},{right.x,p.landing.y-7,right.z},p.materials[2],{0.68f,0.67f,0.8f});};
            if(!cells.contains({x-1,z}))skirt(a,b);if(!cells.contains({x+1,z}))skirt(d,c);
            if(!cells.contains({x,z-1}))skirt(c,a);if(!cells.contains({x,z+1}))skirt(b,d);
        }
        for(const auto& site:p.footprints)if(site.structure) {
            const auto& b=site.bounds;box(level,b,p.materials[2],{0.9f,0.85f,1.05f});
            auto cap=b;cap.min.y=cap.max.y-0.3f;
            box(level,cap,p.materials[1],{1.1f,1,1.2f});
        }
        for(auto& gate:p.portals)portal_frame(level,gate,actors,models);
        Random random{mix(address.seed^mix(address.world)^0x41535452414cULL)};
        for(std::size_t i=1;i<p.route.size();++i) {
            const auto from=p.route[i-1],to=p.route[i];const auto distance=length(to-from);
            for(float at=3;at<distance-1;at+=2.7f) {
                auto gem=clone(actors,1,models);gem.held_gem=1;gem.gem_value=0;gem.state=0;
                actor(level,std::move(gem),lerp(from,to,at/distance),models,true);
            }
            if(i+1==p.route.size())continue;
            // Encounters sit beside the through route, never on the landing pad.
            for(unsigned k=0;k<2;++k) {
                auto object=clone(actors,k?0x1f0:0x22b,models);
                bool placed=false;
                for(unsigned attempt=0;attempt<16 && !placed;++attempt) {
                    const float side=k?1.0f:-1.0f;
                    const auto pos=to+Vec3{side*random.range(4.2f,6.5f),0,random.range(-2,2)};
                    object.position=object.previous_position=pos;
                    auto b=object_bounds(object,models,true);if(k)b.max.z+=2;
                    if(!placeable(b,p,2.5f))continue;
                    object.position=object.previous_position={};
                    // A local retreat segment is rebuilt on this world's floor.
                    if(k) {
                        const auto end=pos+Vec3{0,0,2};
                        object.enemy->route={{pos,0},{end,0}};object.enemy->target=1;object.enemy->loop=false;
                    }
                    actor(level,object,pos,models);placed=true;
                }
            }
            auto basket=clone(actors,0xc8,models);basket.held_gem=2;
            for(unsigned attempt=0;attempt<12;++attempt) {
                const auto pos=to+Vec3{random.range(-6,6),0,random.range(-4,4)};basket.position=basket.previous_position=pos;
                if(!placeable(object_bounds(basket,models,true),p,3.1f))continue;
                basket.position=basket.previous_position={};actor(level,basket,pos,models);break;
            }
        }
        initialize_retail_objects(level,models);
        // Route points in an enemy's escape script must follow generated terrain.
        for(auto& object:level.objects)if(object.enemy)for(auto& waypoint:object.enemy->route) {
            if(const auto floor=level.raycast(waypoint.position+Vec3{0,4,0},{0,-1,0},8))waypoint.position.y=floor->point.y;
        }
    }
    if(address.space)for(auto& gate:p.portals)portal_frame(level,gate,actors,models);
    const auto audit=audit_universe(level,&models);
    if(!audit)throw FormatError("Generated world rejected: "+audit.errors.front());
    p.validated=true;return level;
}
UniverseAudit audit_universe(const Level& level,const ObjectModels* models) {
    UniverseAudit result;if(!level.universe){result.errors.push_back("Missing universe layout");return result;}
    const auto& p=*level.universe;
    const auto fail=[&](std::string why){if(result.errors.size()<24)result.errors.push_back(std::move(why));};
    if(!finite(level.spawn) || p.portals.empty())fail("No landing position or return portal");
    for(const auto& portal:p.portals) {
        if(!finite(portal.center) || portal.radius<0.8f || portal.destination.seed!=p.address.seed)fail("Invalid portal connection");
        if(portal.object>=level.objects.size() || level.objects[portal.object].type!=0x1aa)fail("Missing retail return portal");
    }
    for(const auto& v:level.visual_vertices)if(!finite(v.position) || !finite(v.color) || v.material>=level.terrain_materials.size() ||
        !std::isfinite(v.u) || !std::isfinite(v.v) || v.u<0 || v.v<0 || v.u>1 || v.v>1){fail("Invalid generated mesh/material");break;}
    if(p.address.space) {
        for(const auto& planet:p.planets) {
            if(!finite(planet.center) || !std::isfinite(planet.radius) || planet.radius<=0 || planet.portal>=p.portals.size()) {
                fail("Invalid destination planet");continue;
            }
            for(const auto& gate:p.portals) {
                const auto d=gate.center-p.landing;const auto length2=dot(d,d);
                const auto t=length2>0?std::clamp(dot(planet.center-p.landing,d)/length2,0.0f,1.0f):0;
                if(length(planet.center-(p.landing+d*t))<planet.radius+1.8f)fail("Planet obstructs a portal flight approach");
            }
        }
        return result;
    }
    if(p.route.size()<5 || p.enemies==0)fail("World has no complete route or enemies");
    for(std::size_t i=1;i<p.route.size();++i) {
        const auto a=p.route[i-1],b=p.route[i],delta=b-a;const auto side=normalized(Vec3{delta.z,0,-delta.x});
        const auto n=static_cast<unsigned>(std::ceil(length(delta)/0.75f));
        for(unsigned j=0;j<=n;++j)for(const float offset:{-2.0f,0.0f,2.0f}) {
            const auto sample=lerp(a,b,float(j)/n)+side*offset;
            const auto floor=level.raycast(sample+Vec3{0,0.7f,0},{0,-1,0},1.4f);++result.route_samples;
            if(!floor || floor->normal.y<0.9f || std::abs(floor->point.y-sample.y)>0.4f)fail("Route has a hole, steep step or obstruction");
            if(level.raycast(sample+Vec3{0,0.4f,0},{0,1,0},1.7f))fail("Route has insufficient headroom");
        }
    }
    for(unsigned i=0;i<16;++i) {
        const auto sample=p.landing+Vec3{std::cos(i*2*pi/16)*4,0,std::sin(i*2*pi/16)*4};
        const auto floor=level.raycast(sample+Vec3{0,0.3f,0},{0,-1,0},0.6f);
        if(!floor || floor->normal.y<0.98f)fail("Landing pad is not clear and level");
    }
    for(std::size_t i=0;i<p.footprints.size();++i) {
        const auto& site=p.footprints[i];
        if(route_distance(site.bounds,p)<p.route_clearance)fail("Scenery/actor bounds intrude into the through route");
        for(std::size_t j=0;j<i;++j)if(overlaps(site.bounds,p.footprints[j].bounds,0.1f))fail("Generated objects overlap");
    }
    for(const auto& object:level.objects) {
        if(models && !models->models.contains(object.type)){fail("Missing curated object model");continue;}
        const auto floor=level.raycast(object.position+Vec3{0,0.3f,0},{0,-1,0},0.6f);
        if(!floor || std::abs(floor->point.y-object.position.y)>0.08f)fail("Floating or buried actor origin");else ++result.grounded_objects;
        if(object.enemy)for(const auto& waypoint:object.enemy->route)
            if(!level.raycast(waypoint.position+Vec3{0,0.3f,0},{0,-1,0},0.6f))fail("Enemy retreat route leaves the island");
    }
    return result;
}
unsigned universe_objective_progress(const Game& game) {
    if(!game.level.universe)return 0;unsigned n=0;
    for(const auto id:game.level.universe->objective_gems)if(id<game.retail_objects.size() && game.retail_objects[id].collected)++n;
    return n;
}
void tick_universe(Game& game,bool interact) {
    if(!game.level.universe || game.universe_travel)return;
    if(game.universe_portal_cooldown){--game.universe_portal_cooldown;return;}
    const auto& world=*game.level.universe;
    const auto from=game.player.previous_position+retail::origin_above_feet,to=game.player.position+retail::origin_above_feet;
    for(const auto& portal:world.portals) {
        const float before=from.z-portal.center.z,after=to.z-portal.center.z;
        const bool crossed=(before<0)!=(after<0);
        const float t=crossed?std::clamp(before/(before-after),0.0f,1.0f):1;
        const auto at=lerp(from,to,t)-portal.center;
        bool inside=std::hypot(at.x,at.y)<portal.radius-0.15f;
        if(portal.object!=UINT32_MAX) {
            const auto point=lerp(from,to,t);inside=true;
            for(unsigned i=0;i<5;++i) {
                const auto a=portal.aperture[i],b=portal.aperture[(i+1)%5];
                if((b.x-a.x)*(point.y-a.y)-(b.y-a.y)*(point.x-a.x)>0)inside=false;
            }
        }
        if((crossed && inside) || (interact && length(to-portal.center)<portal.radius+1)) {
            game.universe_travel=portal.destination;return;
        }
    }
}
} // namespace spyro
