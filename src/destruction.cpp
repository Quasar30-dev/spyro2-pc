#include "spyro/destruction.hpp"
#include "spyro/runtime.hpp"
#include <set>
#include <numbers>

namespace spyro {
namespace {
using Face=std::array<TerrainVertex,3>;
using Polygon=std::vector<TerrainVertex>;
float smooth(float x){x=std::clamp(x,0.0f,1.0f);return x*x*(3-2*x);}
float axis(Vec3 p,unsigned a){return a==0?p.x:a==1?p.y:p.z;}
Vec3 normal(const Triangle& t){return normalized(cross(t.b-t.a,t.c-t.a));}
bool near(const Triangle& t,Vec3 p,float r) {
    return p.x+r>=std::min({t.a.x,t.b.x,t.c.x}) && p.x-r<=std::max({t.a.x,t.b.x,t.c.x}) &&
        p.y+r>=std::min({t.a.y,t.b.y,t.c.y}) && p.y-r<=std::max({t.a.y,t.b.y,t.c.y}) &&
        p.z+r>=std::min({t.a.z,t.b.z,t.c.z}) && p.z-r<=std::max({t.a.z,t.b.z,t.c.z});
}
TerrainVertex mix(const TerrainVertex& a,const TerrainVertex& b,float t) {
    auto v=a;v.position=lerp(a.position,b.position,t);v.color=lerp(a.color,b.color,t);
    v.u=std::lerp(a.u,b.u,t);v.v=std::lerp(a.v,b.v,t);v.source_vertex=v.source_color=UINT32_MAX;return v;
}
Polygon clip(const Polygon& in,Vec3 n,float distance,bool inside) {
    Polygon out;if(in.empty())return out;
    auto a=in.back();float da=(distance-dot(a.position,n))*(inside?1.0f:-1.0f);
    for(const auto& b:in) {
        const float db=(distance-dot(b.position,n))*(inside?1.0f:-1.0f);
        if((da>=0)!=(db>=0))out.push_back(mix(a,b,std::clamp(da/(da-db),0.0f,1.0f)));
        if(db>=0)out.push_back(b);a=b;da=db;
    }return out;
}
void triangles(const Polygon& p,std::vector<Face>& result) {
    for(std::size_t i=1;i+1<p.size();++i)
        if(length(cross(p[i].position-p[0].position,p[i+1].position-p[0].position))>1e-7f)
            result.push_back({p[0],p[i],p[i+1]});
}
struct Plane {Vec3 n;float distance;};
std::vector<Plane> volume(Vec3 center,float radius) {
    std::vector<Plane> planes;
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
        if(!x && !y && !z)continue;const auto n=normalized(Vec3{float(x),float(y),float(z)});
        planes.push_back({n,dot(center,n)+radius});
    }return planes;
}
struct Guard {Vec3 point;float radius;bool floor_only=false;};
struct Protection {
    std::set<unsigned> collision,visual;
    std::vector<Triangle> faces;
    std::vector<Guard> guards;
    std::vector<std::array<Vec3,2>> seams;
    Vec3 safe_feet;bool protect_feet=false;
    float weight(Vec3 point,Vec3 n)const {
        float w=1;
        if(protect_feet && std::abs(n.y)>.55f)w=std::min(w,smooth((length(point-safe_feet)-1.03f)/.35f));
        for(const auto& g:guards)if(!g.floor_only || std::abs(n.y)>.55f)
            w=std::min(w,smooth((length(point-g.point)-g.radius-.48f)/.35f));
        for(const auto& f:faces)if(std::abs(dot(n,normal(f)))>.95f)
            w=std::min(w,smooth((length(point-closest_point(point,f))-.48f)/.35f));
        for(const auto& edge:seams) {
            const auto d=edge[1]-edge[0];const auto nearest=edge[0]+d*std::clamp(dot(point-edge[0],d)/dot(d,d),0.0f,1.0f);
            w=std::min(w,smooth((length(point-nearest)-.46f)/.35f));
        }
        return w;
    }
};
void seam_edges(Protection& p,const Triangle& source,const Triangle& neighbor) {
    const auto n=normal(neighbor);const float distance=dot(neighbor.a,n);
    for(unsigned edge=0;edge<3;++edge) {
        const auto a=edge==0?source.a:edge==1?source.b:source.c,b=edge==0?source.b:edge==1?source.c:source.a;
        if(std::abs(dot(a,n)-distance)>.004f || std::abs(dot(b,n)-distance)>.004f)continue;
        float lo=0,hi=1;
        for(unsigned side=0;side<3;++side) {
            const auto x=side==0?neighbor.a:side==1?neighbor.b:neighbor.c,y=side==0?neighbor.b:side==1?neighbor.c:neighbor.a;
            auto outside=normalized(cross(y-x,n));if(dot((neighbor.a+neighbor.b+neighbor.c)/3-x,outside)>0)outside=outside*-1;
            const float da=dot(a-x,outside)-.002f,db=dot(b-x,outside)-.002f;
            if(da>0 && db>0){hi=-1;break;}
            if(da>0)lo=std::max(lo,da/(da-db));else if(db>0)hi=std::min(hi,da/(da-db));
        }
        if(hi>lo && length((b-a)*(hi-lo))>.02f) {
            const std::array<Vec3,2> segment{lerp(a,b,lo),lerp(a,b,hi)};
            const bool duplicate=std::any_of(p.seams.begin(),p.seams.end(),[&](const auto& e){return (length(e[0]-segment[0])<.004f && length(e[1]-segment[1])<.004f) || (length(e[0]-segment[1])<.004f && length(e[1]-segment[0])<.004f);});
            if(!duplicate)p.seams.push_back(segment);
        }
    }
}
Protection protection(const Game& game,const TerrainDamage& hit,const ObjectModels* models,float r) {
    const auto& l=game.level;Protection p;
    const auto ranges=[&](const auto& meshes){for(const auto& m:meshes){for(unsigned k=0;k<m.triangles;++k)p.collision.insert(m.triangle+k);for(unsigned k=0;k<m.vertices;k+=3)p.visual.insert(m.vertex+k);}};
    for(const auto& piece:l.homeworld_pieces)ranges(piece.meshes);
    if(l.custom_arena)for(const auto& piece:l.custom_arena->pieces)ranges(piece.meshes);
    for(const auto& m:l.terrain_morphs) {
        if(m.collision && !m.frames.empty())for(unsigned i=0;i<m.frames.front().size()/3;++i)p.collision.insert(m.first_triangle+i);
        for(const auto& [output,input]:m.targets){(void)input;p.visual.insert(output/3*3);}
    }
    for(const auto& m:l.terrain_translations) {
        p.collision.insert(m.triangles.begin(),m.triangles.end());
        for(const auto i:l.translation_vertices(m))if(i%3==0)p.visual.insert(i);
    }
    // Callers only consult triangles within r of the hit (faces within r+1).
    const float reach=r+1.01f;
    // A world with sliceable volumes (SLICEABLE 1) lets the Void Beam cut
    // only inside them; everything else is protected.
    if(hit.attack==TerrainAttack::VoidSlice && !l.sliceable.empty()) {
        const auto inside=[&](Vec3 p){return std::any_of(l.sliceable.begin(),l.sliceable.end(),[&](const auto& v){
            return p.x>=v.bounds.min.x && p.x<=v.bounds.max.x && p.y>=v.bounds.min.y && p.y<=v.bounds.max.y && p.z>=v.bounds.min.z && p.z<=v.bounds.max.z;});};
        for(const auto i:l.collision_candidates(hit.center.x-reach,hit.center.z-reach,hit.center.x+reach,hit.center.z+reach)) {
            const auto& t=l.triangles[i];if(!inside((t.a+t.b+t.c)/3))p.collision.insert(i);
        }
        for(const auto i:l.visual_candidates(hit.center.x-reach,hit.center.z-reach,hit.center.x+reach,hit.center.z+reach))
            if(i+2<l.visual_vertices.size() && !inside((l.visual_vertices[i].position+l.visual_vertices[i+1].position+l.visual_vertices[i+2].position)/3))p.visual.insert(i);
    }
    for(const auto i:l.collision_candidates(hit.center.x-reach,hit.center.z-reach,hit.center.x+reach,hit.center.z+reach)) {
        const auto& t=l.triangles[i];
        // A horizontal blast opens a wall, rather than punching holes in the
        // walkable floor beneath the opening. Preserve the same support faces
        // in collision and rendering; direct floor impacts still deform it.
        const bool wall_support=hit.protect_gameplay && std::abs(hit.normal.y)<.45f &&
            std::abs(normal(t).y)>.7f && closest_point(hit.center,t).y<=hit.center.y+.1f;
        if(t.water || t.portal!=UINT32_MAX || wall_support || (hit.protect_gameplay && t.surface_type!=UINT32_MAX) || p.collision.contains(i)) {
            p.collision.insert(i);if(near(t,hit.center,r+1))p.faces.push_back(t);
        }
    }
    if(!hit.protect_gameplay)return p;
    p.guards.push_back({l.spawn,1.2f});
    // A blast must not remove the floor directly below its owner.
    p.safe_feet=hit.safe_feet;p.protect_feet=true;
    for(const auto& s:game.headbash.stamps())p.guards.push_back({s.center,HeadbashTerrain::radius(s.mode)+.1f,true});
    std::set<unsigned> objects;
    for(const auto& s:l.scripts)objects.insert(s.object);
    for(const auto& n:l.npcs)objects.insert(n.object);
    for(const auto& w:l.whirlwinds)objects.insert(w.object);
    for(const auto& h:l.home_transports)objects.insert(h.object);
    for(const auto& portal:l.portals) {
        objects.insert(portal.object);objects.insert(portal.gate_object);Vec3 center{};
        for(auto v:portal.aperture)center+=v/5;p.guards.push_back({center,1.2f});
    }
    for(const auto& o:l.objects)if(objects.contains(o.id) || o.powerup_gate) {
        float size=.8f;
        if(models)if(const auto m=models->models.find(o.type);m!=models->models.end() && !m->second.meshes.empty()) {
            Bounds bounds;for(auto v:m->second.meshes.front().vertices)bounds.add(v*object_model_scale(o));
            if(finite(bounds.min) && finite(bounds.max))size=std::clamp(length(bounds.max-bounds.min)*.5f,.8f,3.0f);
        }
        if(length(o.position-hit.center)<r+size+1)p.guards.push_back({o.position,size});
    }
    for(const auto& o:l.custom_objectives)for(auto v:o.checkpoints)p.guards.push_back({v,.9f});
    return p;
}
float dent_weight(Vec3 p,const TerrainDamage& hit,float r) {
    const auto d=p-hit.center;const float depth=dot(d,hit.normal);
    return smooth((r-length(d-hit.normal*depth))/.4f)*smooth((.65f-std::abs(depth))/.3f);
}
std::vector<Face> dent(Face face,const TerrainDamage& hit,float r,const Protection& protection,bool& changed,float depth=0) {
    // Shared world grid on the dominant surface plane preserves joins and
    // the original UV interpolation across differently triangulated meshes.
    const auto n=hit.normal,surface_n=normal({face[0].position,face[1].position,face[2].position});unsigned drop=0;if(std::abs(n.y)>std::abs(n.x))drop=1;if(std::abs(n.z)>std::abs(axis(n,drop)))drop=2;
    const unsigned x=(drop+1)%3,y=(drop+2)%3;constexpr float grid=.32f;
    Vec3 ex{},ey{};if(x==0)ex.x=1;else if(x==1)ex.y=1;else ex.z=1;
    if(y==0)ey.x=1;else if(y==1)ey.y=1;else ey.z=1;
    const float x0=std::floor((axis(hit.center,x)-r)/grid)*grid,x1=std::ceil((axis(hit.center,x)+r)/grid)*grid;
    const float y0=std::floor((axis(hit.center,y)-r)/grid)*grid,y1=std::ceil((axis(hit.center,y)+r)/grid)*grid;
    Polygon center(face.begin(),face.end());std::vector<Face> result;
    triangles(clip(center,ex,x0,true),result);center=clip(center,ex,x0,false);
    triangles(clip(center,ex,x1,false),result);center=clip(center,ex,x1,true);
    triangles(clip(center,ey,y0,true),result);center=clip(center,ey,y0,false);
    triangles(clip(center,ey,y1,false),result);center=clip(center,ey,y1,true);
    const float ax0=std::max(x0,std::floor(std::min({axis(face[0].position,x),axis(face[1].position,x),axis(face[2].position,x)})/grid)*grid);
    const float ax1=std::min(x1,std::ceil(std::max({axis(face[0].position,x),axis(face[1].position,x),axis(face[2].position,x)})/grid)*grid);
    const float by0=std::max(y0,std::floor(std::min({axis(face[0].position,y),axis(face[1].position,y),axis(face[2].position,y)})/grid)*grid);
    const float by1=std::min(y1,std::ceil(std::max({axis(face[0].position,y),axis(face[1].position,y),axis(face[2].position,y)})/grid)*grid);
    for(float a=ax0;a<ax1-.001f;a+=grid) {
        auto column=clip(clip(center,ex,a,false),ex,a+grid,true);if(column.empty())continue;
        for(float b=by0;b<by1-.001f;b+=grid) {
            const auto cell=clip(clip(column,ey,b,false),ey,b+grid,true);
            // Use one world-aligned diagonal even when visible/collision source
            // triangles disagree. Every new polygon lies on one linear field.
            triangles(clip(cell,ex-ey,a-b,true),result);
            triangles(clip(cell,ex-ey,a-b,false),result);
        }
    }
    // Each world-grid corner is shared by many clipped vertices. Calculate its
    // protected weight once rather than repeating seam/guard queries per face.
    const int first_x=int(std::floor(x0/grid))-1,first_y=int(std::floor(y0/grid))-1;
    const int columns=int(std::ceil(x1/grid))-first_x+2,rows=int(std::ceil(y1/grid))-first_y+2;
    std::vector<float> weights(columns*rows,-1);
    const auto at=[&](int ax,int by) {
            if(ax<first_x || by<first_y || ax>=first_x+columns || by>=first_y+rows)return 0.0f;
            auto& weight=weights[(by-first_y)*columns+ax-first_x];if(weight>=0)return weight;
            auto q=hit.center+ex*(ax*grid-axis(hit.center,x))+ey*(by*grid-axis(hit.center,y));
            const float correction=dot(q-hit.center,n)/axis(n,drop);
            if(drop==0)q.x-=correction;else if(drop==1)q.y-=correction;else q.z-=correction;
            return weight=dent_weight(q,hit,r)*protection.weight(q,surface_n);
    };
    const auto field=[&](Vec3 p) {
        const int a=int(std::floor(axis(p,x)/grid)),b=int(std::floor(axis(p,y)/grid));
        const float u=std::clamp((axis(p,x)-a*grid)/grid,0.0f,1.0f),v=std::clamp((axis(p,y)-b*grid)/grid,0.0f,1.0f);
        const float lo=at(a,b),hi=at(a+1,b+1);
        const float value=u>=v?lo*(1-u)+at(a+1,b)*(u-v)+hi*v:lo*(1-v)+at(a,b+1)*(v-u)+hi*u;
        return value*smooth((.65f-std::abs(dot(p-hit.center,n)))/.3f);
    };
    for(auto& f:result)for(auto& v:f) {
        const float w=field(v.position);changed|=w>.001f;v.position=v.position-hit.normal*((depth>0?depth:.16f+.07f*hit.strength)*w);v.color=v.color*(1-.28f*w);
    }
    return result;
}
struct Cut {std::vector<Face> retained,removed;};
void shield_cut(Cut& result,const Protection& protection,const Triangle& surface,bool keep_geometry=true) {
    const auto n=normal(surface);
    std::vector<std::vector<Plane>> masks;
    if(protection.protect_feet && std::abs(n.y)>.55f && near(surface,protection.safe_feet,.65f))masks.push_back(volume(protection.safe_feet,.55f));
    for(const auto& g:protection.guards)if((!g.floor_only || std::abs(n.y)>.55f) && near(surface,g.point,g.radius+.1f))masks.push_back(volume(g.point,g.radius));
    for(const auto& f:protection.faces)if(std::abs(dot(n,normal(f)))>.95f) {
        const auto fn=normal(f);std::vector<Plane> prism{{fn,dot(f.a,fn)+.025f},{fn*-1,-dot(f.a,fn)+.025f}};
        for(unsigned edge=0;edge<3;++edge) {
            const auto a=edge==0?f.a:edge==1?f.b:f.c,b=edge==0?f.b:edge==1?f.c:f.a;
            auto outside=normalized(cross(b-a,fn));if(dot((f.a+f.b+f.c)/3-a,outside)>0)outside=outside*-1;
            prism.push_back({outside,dot(a,outside)+.025f});
        }
        masks.push_back(std::move(prism));
    }
    // Preserve the same protected world volumes in both meshes, regardless of
    // their source tessellation. Keeping whole source faces would leave ghosts.
    for(const auto& mask:masks) {
        std::vector<Face> removed;
        for(const auto& face:result.removed) {
            Polygon inside(face.begin(),face.end());
            for(const auto& plane:mask){triangles(clip(inside,plane.n,plane.distance,false),removed);inside=clip(inside,plane.n,plane.distance,true);if(inside.empty())break;}
            if(keep_geometry)triangles(inside,result.retained);
        }
        result.removed=std::move(removed);if(result.removed.empty())break;
    }
}
bool cavity(const Level& level,const Triangle& face,const TerrainDamage& hit,float r) {
    auto n=normal(face);if(dot(n,hit.normal)<0)n=n*-1;
    const auto inner=hit.center-n*(r*1.2f+.15f);
    // Thin retail shells often bound solid cliff/ground masses with no mesh
    // inside. Excavate a textured interior there; real rooms stay open.
    if(std::abs(n.y)>.55f)return !level.raycast(inner,n*-1,std::max(4.0f,r*3));
    return !level.raycast(inner+Vec3{0,.2f,0},{0,-1,0},std::max(4.0f,r*3));
}
Cut cut(Face face,const TerrainDamage& hit,const std::vector<Plane>& planes) {
    Cut result;Polygon center(face.begin(),face.end());
    for(const auto& plane:planes) {triangles(clip(center,plane.n,plane.distance,false),result.retained);center=clip(center,plane.n,plane.distance,true);if(center.empty())break;}
    if(center.size()<3)return result;
    triangles(center,result.removed);
    // Expose a textured, collidable rim on thin PS1 building shells.
    auto n=normalized(cross(face[1].position-face[0].position,face[2].position-face[0].position));
    if(dot(n,hit.normal)<0)n=n*-1;
    for(unsigned i=0;i<center.size();++i) {
        const auto a=center[i],b=center[(i+1)%center.size()];
        const bool edge=std::any_of(planes.begin(),planes.end(),[&](const auto& p){return std::abs(dot(a.position,p.n)-p.distance)<.001f && std::abs(dot(b.position,p.n)-p.distance)<.001f;});
        if(!edge || length(a.position-b.position)<.001f)continue;
        auto c=b,d=a;c.position=c.position-n*.16f;d.position=d.position-n*.16f;c.color=c.color*.5f;d.color=d.color*.5f;
        result.retained.push_back({a,b,c});result.retained.push_back({a,c,d});
    }
    return result;
}
float random(unsigned& seed){seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.0f;}
Face chip(Face f,unsigned& seed) {
    // Choose only the visible fragments we will simulate. Subdividing every
    // discarded polygon into hundreds of unused chips stalls large blasts.
    for(unsigned depth=0;depth<4;++depth) {
        if(std::max({length(f[0].position-f[1].position),length(f[1].position-f[2].position),length(f[2].position-f[0].position)})<.65f)break;
        const auto a=mix(f[0],f[1],.5f),b=mix(f[1],f[2],.5f),c=mix(f[2],f[0],.5f);
        switch(unsigned(random(seed)*4)){case 0:f={f[0],a,c};break;case 1:f={a,f[1],b};break;case 2:f={c,b,f[2]};break;default:f={a,b,c};break;}
    }
    return f;
}
TerrainDebris chunk(const Face& face,Vec3 impulse,unsigned& seed) {
    TerrainDebris d;d.position=d.previous=(face[0].position+face[1].position+face[2].position)/3;
    const auto n=normalized(cross(face[1].position-face[0].position,face[2].position-face[0].position));
    Face front=face,back=face;for(unsigned i=0;i<3;++i) {front[i].position=front[i].position-d.position;back[i].position=front[i].position-n*.12f;back[i].color=back[i].color*.6f;d.radius=std::max(d.radius,length(front[i].position));}
    d.vertices.insert(d.vertices.end(),front.begin(),front.end());d.vertices.insert(d.vertices.end(),{back[2],back[1],back[0]});
    for(unsigned i=0;i<3;++i){const unsigned j=(i+1)%3;d.vertices.insert(d.vertices.end(),{front[i],back[i],back[j],front[i],back[j],front[j]});}
    d.velocity=impulse*(1.2f+random(seed)*2.3f)+Vec3{(random(seed)-.5f)*3,1+random(seed)*3,(random(seed)-.5f)*3};
    d.spin={(random(seed)-.5f)*7,(random(seed)-.5f)*7,(random(seed)-.5f)*7};d.radius=std::max(.05f,d.radius+.12f);return d;
}
void crack_mesh(const Level& l,const TerrainDamage& hit,float r,std::vector<std::array<Vec3,3>>& out) {
    auto x=normalized(cross(hit.normal,std::abs(hit.normal.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0}));const auto y=cross(hit.normal,x);unsigned seed=hit.seed;
    for(unsigned arm=0;arm<7;++arm) {
        const float angle=arm*2*std::numbers::pi_v<float>/7+(random(seed)-.5f)*.3f;
        const auto direction=x*std::cos(angle)+y*std::sin(angle),side=cross(direction,hit.normal);
        std::optional<Vec3> previous;
        for(unsigned step=0;step<=5;++step) {
            const float distance=r*(.12f+step*.13f);const auto point=hit.center+direction*distance+side*((random(seed)-.5f)*.09f);
            const auto ray=l.raycast(point+hit.normal,hit.normal*-1,2.5f);
            if(!ray || std::abs(dot(ray->normal,hit.normal))<.75f){previous.reset();continue;}
            const auto next=ray->point+hit.normal*.006f;
            if(previous){const auto width=side*(.007f+.004f*hit.strength);out.push_back({*previous-width,*previous+width,next+width});out.push_back({*previous-width,next+width,next-width});}
            previous=next;
        }
    }
}
}
#include "terrain_slice.inl"
bool TerrainDamage::valid()const{
    if(!(finite(center) && finite(normal) && finite(safe_feet) && std::max({std::abs(center.x),std::abs(center.y),std::abs(center.z),std::abs(safe_feet.x),std::abs(safe_feet.y),std::abs(safe_feet.z)})<100000 &&
         length(normal)>.99f && length(normal)<1.01f && static_cast<unsigned>(attack)<=3 && strength>=1 && strength<=3))return false;
    if(attack!=TerrainAttack::VoidSlice)return true;
    if(shards>TerrainDestruction::max_shards || (shards && shard_poses.size()!=shards))return false;
    for(const auto& pose:shard_poses)if(!finite(pose[0]) || !finite(pose[1]) || length(pose[0])>=1000 || length(pose[1])>=7)return false;
    return slice_radius>=1 && slice_radius<=6 && finite(slice_offset) && finite(slice_rotation) && length(slice_offset)<1000 && length(slice_rotation)<7;
}
float TerrainDestruction::radius(const TerrainDamage& hit) {return (hit.attack==TerrainAttack::Supercharge?1.05f:hit.attack==TerrainAttack::Superflame?1.2f:2.0f)*(1+.2f*(hit.strength-1))*(hit.fragment?.45f:1);}
bool TerrainDestruction::carve(Game& game,const TerrainDamage& hit,const ObjectModels* models,bool replay) {
    if(hit.attack==TerrainAttack::VoidSlice)return carve_slice(game,hit,models,replay);
    auto& l=game.level;const float r=radius(hit);
    auto protected_mesh=protection(game,hit,models,r);
    std::vector<Triangle> authored;std::vector<Vec3> authored_normals;
    const auto count=damage_.empty()?l.triangles.size():initial_triangles_;
    // Unmodified faces come from the grid; modified ones from their originals.
    auto authored_ids=l.collision_candidates(hit.center.x-r-1.01f,hit.center.z-r-1.01f,hit.center.x+r+1.01f,hit.center.z+r+1.01f);
    for(const auto& [i,t]:original_triangles_)if(near(t,hit.center,r+1))authored_ids.push_back(i);
    std::sort(authored_ids.begin(),authored_ids.end());authored_ids.erase(std::unique(authored_ids.begin(),authored_ids.end()),authored_ids.end());
    for(const auto i:authored_ids) {
        if(i>=count)break;
        const auto old=original_triangles_.find(i);const auto& t=old==original_triangles_.end()?l.triangles[i]:old->second;
        const auto n=normal(t);
        if(!t.disabled && !t.water && length(n)>.5f && near(t,hit.center,r+1)){authored.push_back(t);authored_normals.push_back(n);}
    }
    for(unsigned i=0;i<authored.size() && protected_mesh.seams.size()<256;++i)for(unsigned j=i+1;j<authored.size() && protected_mesh.seams.size()<256;++j)
        if(std::abs(dot(authored_normals[i],authored_normals[j]))<.65f){seam_edges(protected_mesh,authored[i],authored[j]);seam_edges(protected_mesh,authored[j],authored[i]);}
    const bool charge=hit.attack==TerrainAttack::Supercharge;const auto planes=volume(hit.center,r);
    std::map<std::array<int,3>,bool> cavity_cache;
    const auto hollow=[&](const Triangle& t) {
        auto n=normal(t);if(dot(n,hit.normal)<0)n=n*-1;
        const std::array<int,3> key{int(std::lround(n.x*1024)),int(std::lround(n.y*1024)),int(std::lround(n.z*1024))};
        if(const auto it=cavity_cache.find(key);it!=cavity_cache.end())return it->second;
        return cavity_cache.emplace(key,cavity(l,t,hit,r)).first->second;
    };
    const auto edit_face=[&](const Face& face,const Triangle& t,bool& modified) {
        if(charge)return Cut{dent(face,hit,r,protected_mesh,modified),{}};
        auto edit=cut(face,hit,planes);if(edit.removed.empty())return edit;
        const bool fill=hollow(t);bool eroded=false;
        if(fill) {
            auto interior=hit;interior.normal=normal(t);if(dot(interior.normal,hit.normal)<0)interior.normal=interior.normal*-1;
            const float offset=dot(hit.center-t.a,interior.normal);interior.center=hit.center-interior.normal*offset;
            const float range=std::sqrt(std::max(.04f,r*r-offset*offset));
            // Tessellate and displace the existing shell. Every interior vertex
            // remains inside its original UV chart, including continuous floors.
            edit.retained=dent(face,interior,range,protected_mesh,eroded,std::max(.15f,r*.7f-std::abs(offset)*.35f));
        }
        shield_cut(edit,protected_mesh,t,!fill);modified=!edit.removed.empty() && (!fill || eroded);return edit;
    };
    struct CollisionEdit {unsigned index;std::vector<Triangle> faces;};struct VisualEdit {unsigned index;std::vector<Face> faces;};
    std::vector<CollisionEdit> collision;std::vector<VisualEdit> visual;std::vector<Face> removed;
    const auto nc=l.triangles.size(),nv=l.visual_vertices.size();std::size_t added_c=0,added_v=0;
    const float edit_reach=r+.21f;
    for(const auto i:l.collision_candidates(hit.center.x-edit_reach,hit.center.z-edit_reach,hit.center.x+edit_reach,hit.center.z+edit_reach)) {
        const auto& t=l.triangles[i];if(t.disabled || !near(t,hit.center,r+.2f) || protected_mesh.collision.contains(i))continue;
        if(charge && std::abs(dot(normal(t),hit.normal))<.85f)continue;
        Face f{};f[0].position=t.a;f[1].position=t.b;f[2].position=t.c;
        bool modified=false;auto edit=edit_face(f,t,modified);
        if(!modified)continue;CollisionEdit result{i};
        for(const auto& face:edit.retained){auto next=t;next.a=face[0].position;next.b=face[1].position;next.c=face[2].position;result.faces.push_back(next);}
        if(result.faces.empty()){auto empty=t;empty.disabled=true;empty.b=empty.c=empty.a;result.faces.push_back(empty);}
        added_c+=result.faces.size()-1;collision.push_back(std::move(result));
    }
    if(collision.empty()){++protected_impacts;return false;}
    for(const auto i:l.visual_candidates(hit.center.x-edit_reach,hit.center.z-edit_reach,hit.center.x+edit_reach,hit.center.z+edit_reach)) {
        const Face f{l.visual_vertices[i],l.visual_vertices[i+1],l.visual_vertices[i+2]};const Triangle t{f[0].position,f[1].position,f[2].position};
        if(!near(t,hit.center,r+.2f) || protected_mesh.visual.contains(i))continue;
        if(f[0].material<l.terrain_materials.size() && l.terrain_materials[f[0].material].translucent)continue;
        if(charge && std::abs(dot(normal(t),hit.normal))<.85f)continue;
        bool modified=false;auto edit=edit_face(f,t,modified);if(!modified)continue;
        if(edit.retained.empty()){auto empty=f;empty[1].position=empty[2].position=empty[0].position;edit.retained.push_back(empty);}
        added_v+=edit.retained.size()*3-3;visual.push_back({i,std::move(edit.retained)});
        removed.insert(removed.end(),edit.removed.begin(),edit.removed.end());
    }
    if(added_c>6000 || added_v>30000 || nc+added_c>200000 || nv+added_v>1100000)return false;
    std::vector<std::pair<unsigned,float>> supports;
    for(unsigned i=0;i<game.retail_objects.size();++i) {
        const auto& o=game.retail_objects[i];
        if(o.pc_carried || o.collected || o.state<0 || !(o.type==1 || o.container || o.enemy) || length(o.position-hit.center)>r+1)continue;
        const auto old=original_supports_.find(i);const float from=old==original_supports_.end()?o.position.y:std::max(o.position.y,old->second.height);
        if(const auto ground=l.raycast({o.position.x,from+.2f,o.position.z},{0,-1,0},from-o.position.y+.65f);ground && ground->normal.y>.55f)supports.push_back({i,ground->point.y});
    }
    if(original_triangles_.empty() && original_visual_.empty()){initial_triangles_=nc;initial_visual_=nv;}
    for(const auto& e:collision) {
        if(e.index<initial_triangles_)original_triangles_.try_emplace(e.index,l.triangles[e.index]);
        l.triangles[e.index]=e.faces.front();l.triangles.insert(l.triangles.end(),e.faces.begin()+1,e.faces.end());
    }
    for(const auto& e:visual) {
        if(e.index<initial_visual_)original_visual_.try_emplace(e.index,Face{l.visual_vertices[e.index],l.visual_vertices[e.index+1],l.visual_vertices[e.index+2]});
        for(unsigned k=0;k<3;++k)l.visual_vertices[e.index+k]=e.faces.front()[k];
        for(unsigned j=1;j<e.faces.size();++j)l.visual_vertices.insert(l.visual_vertices.end(),e.faces[j].begin(),e.faces[j].end());
    }
    damage_.push_back(hit);changed_triangles+=static_cast<unsigned>(collision.size());l.dynamic_terrain=true;++l.terrain_revision;++l.collision_revision;
    for(const auto& [i,height]:supports) {
        auto& o=game.retail_objects[i];
        const auto old=original_supports_.find(i);const float from=old==original_supports_.end()?o.position.y:std::max(o.position.y,old->second.height);
        if(const auto ground=l.raycast({o.position.x,from+.2f,o.position.z},{0,-1,0},r+4+from-o.position.y)) {
            const auto& support=original_supports_.try_emplace(i,Support{height,o.position.y-height}).first->second;
            const float dy=ground->point.y+support.offset-o.position.y;o.position.y+=dy;o.previous_position.y+=dy;
            if(o.container)o.container->rest_position.y+=dy;
        }
    }
    if(charge)crack_mesh(l,hit,r,cracks_);
    if(!replay) {
        unsigned seed=hit.seed;const unsigned count=removed.empty()?0u:24u;
        for(unsigned i=0;i<count && debris_.size()<max_debris;++i){debris_.push_back(chunk(chip(removed[i*removed.size()/count],seed),hit.normal,seed));++chunks_emitted;}
        // Charge chips use the local textured surface even when no hole is cut.
        std::vector<Face> chips;
        if(charge)for(const auto& edit:visual)for(const auto& face:edit.faces)
            if(length((face[0].position+face[1].position+face[2].position)/3-hit.center)<r &&
               std::max({length(face[0].position-face[1].position),length(face[1].position-face[2].position),length(face[2].position-face[0].position)})<.8f)chips.push_back(face);
        if(!chips.empty())for(unsigned i=0;i<6 && debris_.size()<max_debris;++i) {
            const auto& face=chips[(i*7)%chips.size()];auto small=face;
            const auto c=(face[0].position+face[1].position+face[2].position)/3;
            for(auto& v:small)v.position=c+(v.position-c)*.3f;
            debris_.push_back(chunk(small,hit.normal,seed));++chunks_emitted;
        }
        for(unsigned i=0;i<20 && dust_.size()<256;++i) {
            const auto p=hit.center+hit.normal*.08f;
            dust_.push_back({p,p,hit.normal*(.8f+random(seed))+Vec3{random(seed)-.5f,random(seed)*1.5f,random(seed)-.5f},.045f+random(seed)*.07f,0,30+unsigned(random(seed)*20)});
        }
    }
    return true;
}
bool TerrainDestruction::impact(Game& game,Vec3 point,Vec3 n,TerrainAttack attack,const ObjectModels* models,bool fragment) {
    const auto& settings=game.destruction_settings;
    if(!settings.enabled || damage_.size()>=max_impacts || suspended_ ||
        (attack==TerrainAttack::Supercharge?!settings.charge:attack==TerrainAttack::Superflame?!settings.flame:!settings.bomb))return false;
    if(!finite(point) || !finite(n) || length(n)<.5f)return false;n=normalized(n);
    TerrainDamage hit{point,n,game.player.position-Vec3{0,.15f,0},attack,settings.strength,0x51a2u+unsigned(damage_.size())*7919u,settings.protect_gameplay,fragment};
    return carve(game,hit,models,false);
}
unsigned TerrainDestruction::emit_chips(Game& game,Vec3 center,float radius,unsigned count,unsigned seed) {
    const auto& v=game.level.visual_vertices;std::vector<Face> faces;
    for(const auto i:game.level.visual_candidates(center.x-radius*3,center.z-radius*3,center.x+radius*3,center.z+radius*3)) {
        if(faces.size()>=48)break;
        const auto c=(v[i].position+v[i+1].position+v[i+2].position)/3;
        if(std::abs(c.x-center.x)>radius*3 || std::abs(c.z-center.z)>radius*3 || std::abs(c.y-center.y)>1.2f)continue;
        if(v[i].material<game.level.terrain_materials.size() && game.level.terrain_materials[v[i].material].translucent)continue;
        if(std::abs(normalized(cross(v[i+1].position-v[i].position,v[i+2].position-v[i].position)).y)<.55f)continue;
        faces.push_back({v[i],v[i+1],v[i+2]});
    }
    if(faces.empty())return 0;
    unsigned emitted=0;
    for(unsigned k=0;k<count && debris_.size()<max_debris;++k,++emitted) {
        auto small=chip(faces[(k*7+seed)%faces.size()],seed);
        const auto c=(small[0].position+small[1].position+small[2].position)/3;
        const float a=random(seed)*6.2831853f,r=random(seed)*radius;
        const Vec3 at=center+Vec3{std::sin(a)*r,.05f,std::cos(a)*r};
        for(auto& p:small)p.position=at+(p.position-c)*.32f;
        debris_.push_back(chunk(small,{0,1,0},seed));++chunks_emitted;
    }
    for(unsigned i=0;i<10 && dust_.size()<256;++i) {
        const auto p=center+Vec3{0,.06f,0};
        dust_.push_back({p,p,Vec3{random(seed)-.5f,.8f+random(seed)*1.2f,random(seed)-.5f},.05f+random(seed)*.06f,0,28+unsigned(random(seed)*16)});
    }
    return emitted;
}
void TerrainDestruction::tick(Game& game) {
    tick_slabs(game);
    // Debris collides with the level through its collision grid: a moving
    // piece only tests the triangles around it (no per-tick region copy).
    for(auto& d:debris_) {
        d.previous=d.position;d.previous_rotation=d.rotation;d.velocity.y-=16.0f/60;
        const unsigned steps=std::max(1u,static_cast<unsigned>(std::ceil(length(d.velocity)/(60*d.radius*.75f))));
        for(unsigned step=0;step<steps;++step) {
            const auto before=d.velocity;bool ground=false;const auto proposed=d.position+d.velocity/(60.0f*steps);
            d.position=game.level.resolve_sphere(proposed,d.radius,d.velocity,ground);
            const auto offset=d.position-proposed;
            if(length(offset)>.00001f){const auto n=normalized(offset);const float speed=dot(before,n);if(speed<0)d.velocity=d.velocity-n*(speed*.25f);d.velocity.x*=.9f;d.velocity.z*=.9f;d.spin=d.spin*.9f;}
        }
        d.rotation+=d.spin/60;++d.age;
    }
    std::erase_if(debris_,[&](const auto& d){return d.age>=d.lifetime || d.position.y<game.level.bounds.min.y-10;});
    for(auto& p:dust_){p.previous=p.position;p.velocity=p.velocity*.96f;p.position+=p.velocity/60;p.radius+=.002f;++p.age;}
    std::erase_if(dust_,[](const auto& p){return p.age>=p.lifetime;});
}
void TerrainDestruction::suspend(Level& l) {
    if(suspended_)return;
    // Falling slabs land where they are, so the replay can rebuild them.
    for(auto& slab:slabs_)land_geometry(l,slab);
    slabs_.clear();
    if(!original_triangles_.empty() || !original_visual_.empty()) {
        l.triangles.resize(initial_triangles_);l.visual_vertices.resize(initial_visual_);
        for(const auto& [i,t]:original_triangles_)l.triangles.at(i)=t;
        for(const auto& [i,f]:original_visual_)for(unsigned k=0;k<3;++k)l.visual_vertices.at(i+k)=f[k];
        ++l.terrain_revision;++l.collision_revision;
    }
    original_triangles_.clear();original_visual_.clear();cracks_.clear();suspended_=true;
}
void TerrainDestruction::resume(Game& game,const ObjectModels* models) {
    if(!suspended_)return;const auto records=damage_;damage_.clear();suspended_=false;changed_triangles=0;
    for(const auto& hit:records)carve(game,hit,models,true);refresh_supports(game);
}
void TerrainDestruction::clear(Level& l){suspend(l);damage_.clear();debris_.clear();dust_.clear();cracks_.clear();original_supports_.clear();slabs_.clear();suspended_=false;changed_triangles=chunks_emitted=protected_impacts=slices=slabs_landed=shards_made=infill_faces=0;}
void TerrainDestruction::clear(Game& game) {
    suspend(game.level);refresh_supports(game);clear(game.level);
}
void TerrainDestruction::refresh_supports(Game& game) {
    for(const auto& [i,support]:original_supports_)if(i<game.retail_objects.size()) {
        auto& o=game.retail_objects[i];if(o.collected || o.state<0 || o.pc_carried)continue;
        const float from=std::max(o.position.y,support.height);
        if(const auto ground=game.level.raycast({o.position.x,from+.2f,o.position.z},{0,-1,0},from-o.position.y+8)) {
            const float dy=ground->point.y+support.offset-o.position.y;o.position.y+=dy;o.previous_position.y+=dy;
            if(o.container)o.container->rest_position.y+=dy;
        }
    }
}
void TerrainDestruction::restore(Game& game,std::span<const TerrainDamage> records,const ObjectModels* models) {
    if(records.size()>max_impacts || std::any_of(records.begin(),records.end(),[](const auto& r){return !r.valid();}))throw FormatError("Invalid terrain destruction checkpoint");
    const std::vector<TerrainDamage> copy(records.begin(),records.end());clear(game.level);
    for(const auto& hit:copy)carve(game,hit,models,true);
}
} // namespace spyro
