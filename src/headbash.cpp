#include "spyro/headbash.hpp"
#include "spyro/runtime.hpp"
#include <set>
#include <numbers>
#include <unordered_map>

namespace spyro {
const char* headbash_mode_name(HeadbashMode mode) {
    switch(mode){case HeadbashMode::Crater:return "Crater shockwave";case HeadbashMode::Lava:return "Lava apocalypse";default:return "Normal headbash";}
}
namespace {
float smooth(float value){value=std::clamp(value,0.0f,1.0f);return value*value*(3-2*value);}
float horizontal(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.z-b.z);}
bool nearby(const Triangle& t,Vec3 c,float r) {
    return std::min({t.a.x,t.b.x,t.c.x})<=c.x+r && std::max({t.a.x,t.b.x,t.c.x})>=c.x-r &&
        std::min({t.a.z,t.b.z,t.c.z})<=c.z+r && std::max({t.a.z,t.b.z,t.c.z})>=c.z-r &&
        std::min({t.a.y,t.b.y,t.c.y})<=c.y+1 && std::max({t.a.y,t.b.y,t.c.y})>=c.y-1;
}
bool floor(const Triangle& t){return std::abs(normalized(cross(t.b-t.a,t.c-t.a)).y)>.55f;}
struct Guard {Vec3 center;float radius;};
struct SampleKey {int x,y,z;bool operator==(const SampleKey&)const=default;};
struct SampleHash {std::size_t operator()(SampleKey k)const{return std::size_t(unsigned(k.x))*73856093u^std::size_t(unsigned(k.y))*19349663u^std::size_t(unsigned(k.z))*83492791u;}};
struct Field {
    HeadbashStamp stamp;float range,depth;
    std::vector<Triangle> protected_faces;
    std::vector<Guard> guards;
    mutable std::unordered_map<SampleKey,float,SampleHash> samples;
    float evaluate(Vec3 p)const {
        const float d=horizontal(p,stamp.center);
        // A whole, unchanged island, with a smooth shoulder on both rims.
        // Include one cell diagonal: interpolated faces must not cut across
        // the protected circle even when all three corners lie outside it.
        float w=smooth((d-HeadbashTerrain::safe_radius-.62f)/.65f)*smooth((range-d)/.95f);
        w*=smooth((.8f-std::abs(p.y-stamp.center.y))/.35f);
        if(w<=0)return 0;
        for(const auto& g:guards) {
            if(std::abs(p.y-g.center.y)>2.5f)continue;
            w=std::min(w,smooth((horizontal(p,g.center)-g.radius)/.55f));if(w<=0)return 0;
        }
        for(const auto& t:protected_faces) {
            constexpr float margin=.67f;
            if(p.x<std::min({t.a.x,t.b.x,t.c.x})-margin || p.x>std::max({t.a.x,t.b.x,t.c.x})+margin ||
               p.y<std::min({t.a.y,t.b.y,t.c.y})-margin || p.y>std::max({t.a.y,t.b.y,t.c.y})+margin ||
               p.z<std::min({t.a.z,t.b.z,t.c.z})-margin || p.z>std::max({t.a.z,t.b.z,t.c.z})+margin)continue;
            w=std::min(w,smooth((length(p-closest_point(p,t))-.12f)/.55f));if(w<=0)return 0;
        }
        return w;
    }
    float weight(Vec3 p)const {
        const SampleKey key{int(std::lround(p.x*4096)),int(std::lround(p.y*4096)),int(std::lround(p.z*4096))};
        if(const auto it=samples.find(key);it!=samples.end())return it->second;
        const float value=evaluate(p);samples.emplace(key,value);return value;
    }
};
TerrainVertex mix(const TerrainVertex& a,const TerrainVertex& b,float f) {
    auto v=a;v.position=lerp(a.position,b.position,f);v.color=lerp(a.color,b.color,f);
    v.u=std::lerp(a.u,b.u,f);v.v=std::lerp(a.v,b.v,f);
    v.source_vertex=v.source_color=UINT32_MAX;return v;
}
using Polygon=std::vector<TerrainVertex>;
Polygon clip(const Polygon& in,unsigned axis,float boundary,bool above) {
    Polygon out;if(in.empty())return out;
    const auto coordinate=[&](const TerrainVertex& v){return axis?v.position.z:v.position.x;};
    auto a=in.back();float da=(coordinate(a)-boundary)*(above?1.0f:-1.0f);
    for(const auto& b:in) {
        const float db=(coordinate(b)-boundary)*(above?1.0f:-1.0f);
        if((da>=0)!=(db>=0))out.push_back(mix(a,b,da/(da-db)));
        if(db>=0)out.push_back(b);a=b;da=db;
    }
    return out;
}
using Face=std::array<TerrainVertex,3>;
void triangulate(const Polygon& polygon,std::vector<Face>& out) {
    for(std::size_t i=1;i+1<polygon.size();++i)
        if(length(cross(polygon[i].position-polygon[0].position,polygon[i+1].position-polygon[0].position))>1e-7f)
            out.push_back({polygon[0],polygon[i],polygon[i+1]});
}
// Clip only the local footprint into a shared world grid. Original UVs and
// colours interpolate at cuts. Large PS1 polygons gain real crater geometry;
// unrelated geometry and animated-array indices are never reordered.
std::vector<Face> tessellate(Face face,const Field& field) {
    constexpr float grid=.42f;
    const float x0=std::floor((field.stamp.center.x-field.range)/grid)*grid;
    const float z0=std::floor((field.stamp.center.z-field.range)/grid)*grid;
    const float x1=std::ceil((field.stamp.center.x+field.range)/grid)*grid;
    const float z1=std::ceil((field.stamp.center.z+field.range)/grid)*grid;
    Polygon center(face.begin(),face.end());std::vector<Face> result;
    triangulate(clip(center,0,x0,false),result);center=clip(center,0,x0,true);
    triangulate(clip(center,0,x1,true),result);center=clip(center,0,x1,false);
    triangulate(clip(center,1,z0,false),result);center=clip(center,1,z0,true);
    triangulate(clip(center,1,z1,true),result);center=clip(center,1,z1,false);
    if(center.empty())return result;
    Bounds box;for(const auto& v:center)box.add(v.position);
    const float begin_x=std::max(x0,std::floor(box.min.x/grid)*grid),end_x=std::min(x1,std::ceil(box.max.x/grid)*grid);
    for(float x=begin_x;x<end_x-.001f;x+=grid) {
        auto column=clip(clip(center,0,x,true),0,x+grid,false);if(column.empty())continue;
        Bounds span;for(const auto& v:column)span.add(v.position);
        const float begin_z=std::max(z0,std::floor(span.min.z/grid)*grid),end_z=std::min(z1,std::ceil(span.max.z/grid)*grid);
        for(float z=begin_z;z<end_z-.001f;z+=grid)triangulate(clip(clip(column,1,z,true),1,z+grid,false),result);
    }
    return result;
}
std::array<Vec3,3> points(const Face& face){return {face[0].position,face[1].position,face[2].position};}
void lava_surface(Face face,const std::array<float,3>& weights,unsigned stamp,std::vector<HeadbashLava>& result) {
    // Cut the iso-contour instead of dropping whole boundary triangles. This
    // keeps a continuous shoreline around the island and protected supports.
    constexpr float threshold=.36f;Polygon polygon;
    for(unsigned i=0;i<3;++i)face[i].color.x=weights[i];
    auto a=face.back();float da=a.color.x-threshold;
    for(auto b:face) {
        const float db=b.color.x-threshold;
        if((da>=0)!=(db>=0))polygon.push_back(mix(a,b,da/(da-db)));
        if(db>=0)polygon.push_back(b);a=b;da=db;
    }
    std::vector<Face> cut;triangulate(polygon,cut);
    for(const auto& f:cut)result.push_back({points(f),stamp});
}
bool lava_contains(const HeadbashLava& lava,Vec3 p,float& height) {
    const auto a=lava.vertices[0],b=lava.vertices[1],c=lava.vertices[2];
    if(p.x<std::min({a.x,b.x,c.x}) || p.x>std::max({a.x,b.x,c.x}) || p.z<std::min({a.z,b.z,c.z}) || p.z>std::max({a.z,b.z,c.z}))return false;
    const float det=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);if(std::abs(det)<1e-8f)return false;
    const float u=((b.z-c.z)*(p.x-c.x)+(c.x-b.x)*(p.z-c.z))/det;
    const float v=((c.z-a.z)*(p.x-c.x)+(a.x-c.x)*(p.z-c.z))/det;
    if(u<0 || v<0 || u+v>1)return false;height=u*a.y+v*b.y+(1-u-v)*c.y;return true;
}
}
float HeadbashTerrain::wave_radius(float alpha)const {
    if(wave_stamp_>=stamps_.size())return 0;
    return radius(stamps_[wave_stamp_].mode)*std::clamp((float(wave_ticks_)-1+alpha)/42,0.0f,1.0f);
}
bool HeadbashTerrain::burning(Vec3 p)const {
    for(const auto& l:lava_) {
        if(stamps_[l.stamp].cooled)continue;
        if(revealing_ && l.stamp==wave_stamp_ && horizontal(p,stamps_[l.stamp].center)>wave_radius())continue;
        float y;if(lava_contains(l,p,y) && p.y>=y-.25f && p.y<y+.22f)return true;
    }
    return false;
}
bool HeadbashTerrain::carve(Game& game,HeadbashStamp stamp,const ObjectModels* models,bool replay) {
    auto& level=game.level;const float r=radius(stamp.mode);
    // Do not deform an earlier crater again or let another blast eat its island.
    for(unsigned i=0;i<stamps_.size();++i)if(horizontal(stamps_[i].center,stamp.center)<.35f && std::abs(stamps_[i].center.y-stamp.center.y)<.3f && stamps_[i].mode==stamp.mode) {
        wave_stamp_=i;wave_ticks_=replay?60:1;revealing_=false;return false;
    }
    if(stamps_.size()>=max_stamps || level.triangles.size()>180000 || level.visual_vertices.size()>1000000)return false;
    Field field{stamp,r,stamp.mode==HeadbashMode::Lava?1.45f:.8f};
    std::set<unsigned> protected_collision,protected_visual;
    for(const auto& morph:level.terrain_morphs) {
        if(morph.collision && !morph.frames.empty())for(unsigned i=0;i<morph.frames.front().size()/3;++i)protected_collision.insert(morph.first_triangle+i);
        else for(const auto& [output,vertex]:morph.targets){(void)vertex;protected_visual.insert(output/3*3);}
    }
    for(const auto& motion:level.terrain_translations) {
        protected_collision.insert(motion.triangles.begin(),motion.triangles.end());
        for(const auto i:level.translation_vertices(motion))if(i%3==0)protected_visual.insert(i);
    }
    const auto protect_mesh=[&](const auto& mesh) {
        for(unsigned i=0;i<mesh.triangles;++i)protected_collision.insert(mesh.triangle+i);
        for(unsigned i=0;i<mesh.vertices;i+=3)protected_visual.insert(mesh.vertex+i);
    };
    for(const auto& piece:level.homeworld_pieces)for(const auto& mesh:piece.meshes)protect_mesh(mesh);
    if(level.custom_arena)for(const auto& piece:level.custom_arena->pieces)for(const auto& mesh:piece.meshes)protect_mesh(mesh);
    // Only faces near the stamp are consulted (edits within r, guards within r+1).
    const float guard_reach=r+1.01f;
    for(const auto i:level.collision_candidates(stamp.center.x-guard_reach,stamp.center.z-guard_reach,stamp.center.x+guard_reach,stamp.center.z+guard_reach)) {
        const auto& t=level.triangles[i];
        if(t.disabled || t.water || t.surface_type!=UINT32_MAX || t.portal!=UINT32_MAX || protected_collision.contains(i) || !floor(t)) {
            protected_collision.insert(i);if(nearby(t,stamp.center,r+1))field.protected_faces.push_back(t);
        }
    }
    // Protect visible animated faces too, even when their collision linkage is
    // not decoded yet. Authored headbash/puzzle contacts keep their own handlers.
    for(auto i:protected_visual)if(i+2<level.visual_vertices.size()) {
        const auto& v=level.visual_vertices;Triangle t{v[i].position,v[i+1].position,v[i+2].position};
        if(nearby(t,stamp.center,r+1))field.protected_faces.push_back(t);
    }
    field.guards.push_back({level.spawn,1.0f});
    for(const auto& previous:stamps_)field.guards.push_back({previous.center,radius(previous.mode)+.1f});
    std::set<unsigned> gameplay_objects;
    for(const auto& script:level.scripts)gameplay_objects.insert(script.object);
    for(const auto& npc:level.npcs)gameplay_objects.insert(npc.object);
    for(const auto& portal:level.portals) {
        gameplay_objects.insert(portal.object);gameplay_objects.insert(portal.gate_object);
        Vec3 center{};for(auto p:portal.aperture)center+=p/5;
        field.guards.push_back({center,1.2f});
    }
    for(const auto& wind:level.whirlwinds)gameplay_objects.insert(wind.object);
    for(const auto& transport:level.home_transports)gameplay_objects.insert(transport.object);
    for(const auto& o:level.objects) {
        if(!gameplay_objects.contains(o.id) && (o.type==1 || o.enemy || is_breakable(o.type) || is_heavy_container(o.type)))continue;
        float guard=.65f;
        if(models)if(const auto it=models->models.find(o.type);it!=models->models.end()) {
            Bounds bounds;const float scale=it->second.animated?object_model_scale(o):1;
            for(const auto& mesh:it->second.meshes)for(auto v:mesh.vertices)bounds.add(v*scale);
            if(finite(bounds.min) && finite(bounds.max))guard=std::clamp(std::hypot(bounds.max.x-bounds.min.x,bounds.max.z-bounds.min.z)*.5f,.65f,6.0f);
        }
        if(horizontal(o.position,stamp.center)<r+guard+1)field.guards.push_back({o.position,guard});
    }
    for(const auto& objective:level.custom_objectives)for(auto p:objective.checkpoints)field.guards.push_back({p,.9f});
    const auto triangle_count=level.triangles.size(),visual_count=level.visual_vertices.size();
    struct CollisionEdit {unsigned index;std::vector<Triangle> faces;};
    struct VisualEdit {unsigned index;std::vector<Face> faces;};
    std::vector<CollisionEdit> collision;std::vector<VisualEdit> visual;std::vector<HeadbashLava> lava,shock;
    const unsigned stamp_index=static_cast<unsigned>(stamps_.size());
    std::size_t added_collision=0,added_visual=0;
    const float edit_reach=r+.01f;
    for(const auto i:level.collision_candidates(stamp.center.x-edit_reach,stamp.center.z-edit_reach,stamp.center.x+edit_reach,stamp.center.z+edit_reach)) {
        if(i>=triangle_count)break;
        const auto& t=level.triangles[i];if(protected_collision.contains(i) || !nearby(t,stamp.center,r))continue;
        Face f{};f[0].position=t.a;f[1].position=t.b;f[2].position=t.c;
        const auto pieces=tessellate(f,field);CollisionEdit edit{i};bool changed=false;
        for(auto face:pieces) {
            std::array<float,3> weights{};
            for(unsigned k=0;k<3;++k){weights[k]=field.weight(face[k].position);face[k].position.y-=field.depth*weights[k];changed|=weights[k]>.001f;}
            auto next=t;next.a=face[0].position;next.b=face[1].position;next.c=face[2].position;edit.faces.push_back(next);
            if(visual_count==0) {
                if(*std::max_element(weights.begin(),weights.end())>.001f)shock.push_back({points(face),stamp_index});
                if(stamp.mode==HeadbashMode::Lava)lava_surface(face,weights,stamp_index,lava);
            }
        }
        if(changed){added_collision+=edit.faces.size()-1;collision.push_back(std::move(edit));}
    }
    if(collision.empty()){++protected_impacts;return false;}
    for(const auto i:level.visual_candidates(stamp.center.x-edit_reach,stamp.center.z-edit_reach,stamp.center.x+edit_reach,stamp.center.z+edit_reach)) {
        if(i+2>=visual_count)break;
        const auto& v=level.visual_vertices;
        Triangle t{v[i].position,v[i+1].position,v[i+2].position};
        if(protected_visual.contains(i) || !floor(t) || !nearby(t,stamp.center,r))continue;
        if(v[i].material<level.terrain_materials.size() && level.terrain_materials[v[i].material].translucent)continue;
        auto pieces=tessellate({v[i],v[i+1],v[i+2]},field);bool changed=false;
        for(auto& face:pieces) {
            std::array<float,3> weights{};
            for(unsigned k=0;k<3;++k) {
                auto& point=face[k];const float w=weights[k]=field.weight(point.position);point.position.y-=field.depth*w;
                point.color=point.color*(1-.38f*w);changed|=w>.001f;
            }
            if(*std::max_element(weights.begin(),weights.end())>.001f)shock.push_back({points(face),stamp_index});
            if(stamp.mode==HeadbashMode::Lava)lava_surface(face,weights,stamp_index,lava);
        }
        if(changed){added_visual+=pieces.size()*3-3;visual.push_back({i,std::move(pieces)});}
    }
    // Commit atomically after budget checks; a saturated terrain layer still
    // performs the normal gameplay headbash without damaging source geometry.
    if(added_collision>12000 || added_visual>50000 || triangle_count+added_collision>200000 || visual_count+added_visual>1100000)return false;
    if(stamps_.empty()){initial_triangles_=triangle_count;initial_visual_=visual_count;}
    for(const auto& edit:collision) {
        if(edit.index<initial_triangles_)original_triangles_.try_emplace(edit.index,level.triangles[edit.index]);
        level.triangles[edit.index]=edit.faces.front();level.triangles.insert(level.triangles.end(),edit.faces.begin()+1,edit.faces.end());
    }
    for(const auto& edit:visual) {
        auto& v=level.visual_vertices;
        if(edit.index<initial_visual_)original_visual_.try_emplace(edit.index,std::array{v[edit.index],v[edit.index+1],v[edit.index+2]});
        for(unsigned k=0;k<3;++k)v[edit.index+k]=edit.faces.front()[k];
        for(std::size_t j=1;j<edit.faces.size();++j)v.insert(v.end(),edit.faces[j].begin(),edit.faces[j].end());
    }
    stamps_.push_back(stamp);lava_.insert(lava_.end(),lava.begin(),lava.end());
    shock_.insert(shock_.end(),shock.begin(),shock.end());
    changed_triangles+=static_cast<unsigned>(collision.size());++level.terrain_revision;++level.collision_revision;
    wave_stamp_=stamp_index;wave_ticks_=replay?60:1;revealing_=!replay;
    // Keep loose collectibles and breakables resting on their changed support.
    for(auto& o:game.retail_objects)if(!o.collected && o.state>=0 && !o.pc_carried && !(replay && o.runtime_spawned) && (o.type==1 || o.container || o.enemy)) {
        const float dy=field.depth*field.weight(o.position);if(dy<=0)continue;
        o.position.y-=dy;o.previous_position.y-=dy;
        if(o.container)o.container->rest_position.y-=dy;
    }
    return true;
}
bool HeadbashTerrain::impact(Game& game,const ObjectModels* models) {
    const auto& u=game.upgrades;
    if(!u.enabled || !u.headbash || u.headbash_mode==HeadbashMode::Normal)return false;
    game.destruction.suspend(game.level);
    struct Resume {Game& game;const ObjectModels* models;~Resume(){game.destruction.resume(game,models);}} resume{game,models};
    const auto mode=u.headbash_mode==HeadbashMode::Lava && u.headbash>=2?HeadbashMode::Lava:HeadbashMode::Crater;
    // Ground contact is mandatory. Headbashing a crate in midair must not dig
    // an unrelated floor below it; special contacts still receive the base hit.
    const auto hit=game.level.raycast(game.player.position+Vec3{0,.2f,0},{0,-1,0},.6f);
    if(!hit || std::abs(hit->point.y-game.player.position.y)>.25f)return false;
    if(mode==HeadbashMode::Lava)for(unsigned i=0;i<stamps_.size();++i)
        if(stamps_[i].mode==HeadbashMode::Crater && horizontal(stamps_[i].center,hit->point)<.35f && std::abs(stamps_[i].center.y-hit->point.y)<.3f) {
            auto records=stamps_;records[i].mode=HeadbashMode::Lava;records[i].cooled=false;records[i].element=Element::Fire;
            auto objects=game.retail_objects;std::vector<std::pair<unsigned,float>> supported;
            for(unsigned j=0;j<objects.size();++j) {
                const auto& o=objects[j];if(o.pc_carried || o.collected || o.state<0 || !(o.type==1 || o.container || o.enemy))continue;
                if(horizontal(o.position,hit->point)>radius(mode))continue;
                if(const auto ground=game.level.raycast(o.position+Vec3{0,.2f,0},{0,-1,0},.6f))supported.push_back({j,ground->point.y});
            }
            restore(game,records,models);game.retail_objects=std::move(objects);
            for(const auto& [j,old_height]:supported) {
                auto& o=game.retail_objects[j];
                if(const auto ground=game.level.raycast(o.position+Vec3{0,.2f,0},{0,-1,0},3)) {
                    const float dy=ground->point.y-old_height;o.position.y+=dy;o.previous_position.y+=dy;
                    if(o.container)o.container->rest_position.y+=dy;
                }
            }
            wave_stamp_=i;wave_ticks_=1;revealing_=true;return true;
        }
    return carve(game,{hit->point,mode},models,false);
}
bool HeadbashTerrain::stamp(Game& game,Vec3 feet,HeadbashMode mode,const ObjectModels* models) {
    if(mode==HeadbashMode::Normal)return false;
    game.destruction.suspend(game.level);
    struct Resume {Game& game;const ObjectModels* models;~Resume(){game.destruction.resume(game,models);}} resume{game,models};
    const auto hit=game.level.raycast(feet+Vec3{0,.2f,0},{0,-1,0},.6f);
    if(!hit || std::abs(hit->point.y-feet.y)>.25f)return false;
    return carve(game,{hit->point,mode},models,false);
}
unsigned HeadbashTerrain::apply_element(Vec3 center,float reach,Element element) {
    unsigned changed=0;
    for(unsigned i=0;i<stamps_.size();++i) {
        auto& s=stamps_[i];
        if(horizontal(s.center,center)>reach+radius(s.mode)*.5f || std::abs(s.center.y-center.y)>2.5f)continue;
        if(element==Element::Water || element==Element::Ice) {
            if(s.mode==HeadbashMode::Lava && !s.cooled){s.cooled=true;s.element=element;++changed;}
        } else if(element==Element::Convexity) {
            if(s.element!=Element::Convexity){s.element=Element::Convexity;++changed;}
        } else if(element==Element::Fire) {
            // Fire re-melts cooled stone and replays the molten reveal.
            if(s.mode==HeadbashMode::Lava) {
                if(s.cooled || s.element!=Element::Fire){s.cooled=false;s.element=Element::Fire;++changed;}
                wave_stamp_=i;wave_ticks_=1;revealing_=true;
            }
        }
    }
    return changed;
}
void HeadbashTerrain::tick(Game& game,const ObjectModels* models) {
    if(wave_ticks_ && wave_ticks_<90)++wave_ticks_;
    const auto touching=[&](Vec3 center,float body_radius) {
        const float reach=body_radius+.025f;
        for(const auto& patch:lava_) {
            if(stamps_[patch.stamp].cooled)continue;
            const Triangle t{patch.vertices[0],patch.vertices[1],patch.vertices[2]};
            if(center.x+reach<std::min({t.a.x,t.b.x,t.c.x}) || center.x-reach>std::max({t.a.x,t.b.x,t.c.x}) ||
               center.y+reach<std::min({t.a.y,t.b.y,t.c.y}) || center.y-reach>std::max({t.a.y,t.b.y,t.c.y}) ||
               center.z+reach<std::min({t.a.z,t.b.z,t.c.z}) || center.z-reach>std::max({t.a.z,t.b.z,t.c.z}))continue;
            const auto contact=closest_point(center,t);
            if(revealing_ && patch.stamp==wave_stamp_ && horizontal(contact,stamps_[patch.stamp].center)>wave_radius())continue;
            if(length(center-contact)<=reach)return true;
        }
        return false;
    };
    // Use actual sphere contact, including the steep crater shoulders. Testing
    // the vertical feet coordinate alone misses contact on sloped lava.
    if(!game.dialogue.active() && !game.scene_locked && touching(game.player.position+retail::origin_above_feet,game.tuning.radius))
        game.hurt(game.player.position-Vec3{0,.5f,0});
    if(!models || game.dialogue.active() || game.scene_locked)return;
    for(auto& o:game.retail_objects)if(o.enemy && !o.enemy->defeated && o.state>=0 && touching(o.position+Vec3{0,.25f,0},.25f))
        o.attack_flags|=headbash_attack|charge_attack;
}
void HeadbashTerrain::clear(Level& level) {
    if(!stamps_.empty()) {
        for(const auto& [i,t]:original_triangles_)level.triangles.at(i)=t;
        for(const auto& [i,face]:original_visual_)for(unsigned k=0;k<3;++k)level.visual_vertices.at(i+k)=face[k];
        level.triangles.resize(initial_triangles_);level.visual_vertices.resize(initial_visual_);++level.terrain_revision;++level.collision_revision;
    }
    *this={};
}
void HeadbashTerrain::restore(Game& game,std::span<const HeadbashStamp> stamps,const ObjectModels* models) {
    game.destruction.suspend(game.level);
    // The caller may hold this object's own checkpoint span.
    const std::vector<HeadbashStamp> copy(stamps.begin(),stamps.end());clear(game.level);
    for(const auto& stamp:copy)carve(game,stamp,models,true);
    game.destruction.resume(game,models);
}
} // namespace spyro
