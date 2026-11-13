// Convexity terrain cutting (included by destruction.cpp, which provides the
// polygon clip/triangulate helpers and the gameplay protection rules).
//
// A slice removes the part of the level inside a bounded prism on the upper
// side of a cutting plane. The removed block shatters along Voronoi planes
// into 4-8 shards. Each shard is solidified with a back face and fracture
// walls, falls and tumbles under contact physics with friction, then lands
// back into the level as ordinary static collision + visual geometry.
//
// What stays behind depends on what was cut:
// - ground (mostly walkable floor): the hole is packed with a recessed copy of
//   the removed surface plus rim walls, so the level never opens to the void,
//   yet a visible depression the shards' thickness deep marks the cut;
// - walls, towers and other structures: both cut faces are capped, as before.
//
// Each slice is a TerrainDamage record holding every shard's landed pose, so
// saves and the headbash suspend/replay rebuild it exactly. Records without
// shards (saves before version 19) replay the original single-slab cut.
namespace {
std::array<Vec3,3> rotate_basis(const std::array<Vec3,3>& b,Vec3 w) {
    const float angle=length(w);if(angle<1e-7f)return b;
    const auto k=w/angle;const float c=std::cos(angle),s=std::sin(angle);
    const auto r=[&](Vec3 v){return v*c+cross(k,v)*s+k*(dot(k,v)*(1-c));};
    auto x=normalized(r(b[0])),y=r(b[1]);y=normalized(y-x*dot(x,y));
    return {x,y,cross(x,y)};
}
Vec3 rotation_vector(const std::array<Vec3,3>& b) {
    // Matrix with columns b -> axis*angle (Rodrigues inverse).
    const float trace=b[0].x+b[1].y+b[2].z;const float angle=std::acos(std::clamp((trace-1)*.5f,-1.0f,1.0f));
    if(angle<1e-5f)return {};
    Vec3 axis{b[1].z-b[2].y,b[2].x-b[0].z,b[0].y-b[1].x};
    if(length(axis)<1e-6f) {
        // ~180 degrees: the largest diagonal term picks the axis.
        const float xx=(b[0].x+1)*.5f,yy=(b[1].y+1)*.5f,zz=(b[2].z+1)*.5f;
        axis=xx>=yy && xx>=zz?Vec3{std::sqrt(std::max(0.0f,xx)),b[1].x/2,b[2].x/2}:yy>=zz?Vec3{b[0].y/2,std::sqrt(std::max(0.0f,yy)),b[2].y/2}:Vec3{b[0].z/2,b[1].z/2,std::sqrt(std::max(0.0f,zz))};
    }
    return normalized(axis)*angle;
}
Vec3 basis_apply(const std::array<Vec3,3>& b,Vec3 v){return b[0]*v.x+b[1]*v.y+b[2]*v.z;}
struct SliceRegion {
    Vec3 center,n,e1,e2;float radius{},height{},base{};
    std::vector<Plane> planes;
    bool inside(Vec3 p,float margin=0)const {
        const float d=dot(p-center,n);if(d<-margin || d>height+margin)return false;
        const auto q=p-center-n*d;return length(q)<=radius+margin;
    }
};
SliceRegion slice_region(const TerrainDamage& hit) {
    SliceRegion r;r.center=hit.center;r.n=normalized(hit.normal);r.radius=hit.slice_radius;r.height=hit.slice_radius*2.6f;
    r.e1=normalized(cross(r.n,std::abs(r.n.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0}));r.e2=cross(r.n,r.e1);r.base=dot(r.center,r.n);
    r.planes.push_back({r.n*-1,-r.base});                  // above the cut
    r.planes.push_back({r.n,r.base+r.height});              // bounded height
    for(unsigned k=0;k<12;++k) {
        const float a=k*2*std::numbers::pi_v<float>/12;const auto dir=r.e1*std::cos(a)+r.e2*std::sin(a);
        r.planes.push_back({dir,dot(r.center,dir)+r.radius});
    }
    return r;
}
struct Extract {std::vector<Face> retained,inside;std::vector<std::array<TerrainVertex,2>> seam;};
Extract extract(const Face& face,const SliceRegion& region) {
    Extract out;Polygon center(face.begin(),face.end());
    for(const auto& plane:region.planes) {
        triangles(clip(center,plane.n,plane.distance,false),out.retained);
        center=clip(center,plane.n,plane.distance,true);if(center.size()<3){center.clear();break;}
    }
    if(center.size()<3)return out;
    triangles(center,out.inside);
    // Edges lying on the cut plane outline the cross-section for the caps.
    for(std::size_t i=0;i<center.size();++i) {
        const auto& a=center[i];const auto& b=center[(i+1)%center.size()];
        if(std::abs(dot(a.position,region.n)-region.base)<1e-3f && std::abs(dot(b.position,region.n)-region.base)<1e-3f && length(a.position-b.position)>1e-4f)
            out.seam.push_back({a,b});
    }
    return out;
}
// Fan caps over the chained cross-section loops (open chains still fan).
std::vector<Face> caps(std::vector<std::array<TerrainVertex,2>> seam,Vec3 n,Vec3 tint) {
    std::vector<Face> out;
    while(!seam.empty()) {
        std::vector<TerrainVertex> loop{seam.back()[0],seam.back()[1]};seam.pop_back();
        for(bool grew=true;grew && !seam.empty();) {
            grew=false;
            for(std::size_t i=0;i<seam.size();++i) {
                const auto tail=loop.back().position;
                if(length(seam[i][0].position-tail)<2e-3f){loop.push_back(seam[i][1]);seam.erase(seam.begin()+i);grew=true;break;}
                if(length(seam[i][1].position-tail)<2e-3f){loop.push_back(seam[i][0]);seam.erase(seam.begin()+i);grew=true;break;}
            }
        }
        if(loop.size()<3)continue;
        TerrainVertex middle=loop.front();Vec3 sum{},color{};float u{},v{};
        for(const auto& p:loop){sum+=p.position;color+=p.color;u+=p.u;v+=p.v;}
        const float k=1.0f/float(loop.size());middle.position=sum*k;middle.color=color*k;middle.u=u*k;middle.v=v*k;
        middle.source_vertex=middle.source_color=UINT32_MAX;
        for(std::size_t i=0;i+1<loop.size();++i) {
            Face f{middle,loop[i],loop[i+1]};
            // One material per triangle (the renderer batches by material).
            for(auto& q:f){q.color={q.color.x*tint.x,q.color.y*tint.y,q.color.z*tint.z};q.source_vertex=q.source_color=UINT32_MAX;q.material=middle.material;q.source_region=middle.source_region;}
            if(length(cross(f[1].position-f[0].position,f[2].position-f[0].position))<1e-7f)continue;
            // Both windings: the cap reads from above and below.
            if(dot(cross(f[1].position-f[0].position,f[2].position-f[0].position),n)<0)std::swap(f[1],f[2]);
            out.push_back(f);out.push_back({f[0],f[2],f[1]});
        }
    }
    return out;
}
constexpr Vec3 void_cut_tint{.46f,.38f,.58f};
// ---- shattering ------------------------------------------------------------
Face face_of(const Triangle& t){Face f{};f[0].position=t.a;f[1].position=t.b;f[2].position=t.c;return f;}
Triangle triangle_of(const Face& f,Triangle attributes){attributes.a=f[0].position;attributes.b=f[1].position;attributes.c=f[2].position;attributes.disabled=false;return attributes;}
float face_area(const Face& f){return length(cross(f[1].position-f[0].position,f[2].position-f[0].position))*.5f;}
TerrainVertex shaded(TerrainVertex v,Vec3 tint){v.color={v.color.x*tint.x,v.color.y*tint.y,v.color.z*tint.z};v.source_vertex=v.source_color=UINT32_MAX;return v;}
// Spread seeds over the block (farthest-point sampling from a seeded start).
std::vector<Vec3> shard_seeds(const std::vector<Triangle>& faces,unsigned count,unsigned seed) {
    std::vector<Vec3> centres;for(const auto& t:faces)centres.push_back((t.a+t.b+t.c)/3);
    std::vector<Vec3> seeds;if(centres.empty())return seeds;
    seeds.push_back(centres[seed%centres.size()]);
    std::vector<float> nearest(centres.size(),1e30f);
    while(seeds.size()<count) {
        std::size_t far=0;float best=-1;
        for(std::size_t i=0;i<centres.size();++i){nearest[i]=std::min(nearest[i],length(centres[i]-seeds.back()));if(nearest[i]>best){best=nearest[i];far=i;}}
        if(best<.05f)break;
        seeds.push_back(centres[far]);
    }
    return seeds;
}
// The part of a polygon inside seed i's Voronoi cell: straight fracture lines.
Polygon voronoi_cell(Polygon p,const std::vector<Vec3>& seeds,std::size_t i) {
    for(std::size_t j=0;j<seeds.size() && p.size()>=3;++j)if(j!=i) {
        const auto n=seeds[j]-seeds[i];
        p=clip(p,n,(dot(seeds[j],seeds[j])-dot(seeds[i],seeds[i]))*.5f,true);
    }
    return p.size()>=3?p:Polygon{};
}
using EdgeKey=std::pair<std::array<int,3>,std::array<int,3>>;
std::array<int,3> point_key(Vec3 p){return {int(std::lround(p.x*512)),int(std::lround(p.y*512)),int(std::lround(p.z*512))};}
EdgeKey edge_key(Vec3 a,Vec3 b){auto x=point_key(a),y=point_key(b);return x<y?EdgeKey{x,y}:EdgeKey{y,x};}
// Edges used by exactly one face: the open rim of a surface patch.
std::map<EdgeKey,int> edge_uses(const std::vector<Face>& faces) {
    std::map<EdgeKey,int> uses;
    for(const auto& f:faces)for(unsigned e=0;e<3;++e)++uses[edge_key(f[e].position,f[(e+1)%3].position)];
    return uses;
}
// A walled strip between an edge and its inset copy, in both windings.
void wall(std::vector<Face>& out,TerrainVertex a,TerrainVertex b,Vec3 offset_a,Vec3 offset_b,Vec3 tint,bool two_sided) {
    a=shaded(a,tint);b=shaded(b,tint);auto c=b,d=a;c.position=c.position+offset_b;d.position=d.position+offset_a;
    out.push_back({a,b,c});out.push_back({a,c,d});
    if(two_sided){out.push_back({a,c,b});out.push_back({a,d,c});}
}
// A thin shell piece made solid: its back face set `inward` behind it and a
// fracture wall along every open edge. `source` maps each face to its input.
struct Solid {std::vector<Face> faces;std::vector<std::size_t> source;};
template<class Inward>
Solid solidify(const std::vector<Face>& front,Inward inward,Vec3 back_tint,Vec3 wall_tint,bool two_sided) {
    Solid s;const auto uses=edge_uses(front);
    for(std::size_t i=0;i<front.size();++i) {
        const auto& f=front[i];
        s.faces.push_back(f);s.source.push_back(i);
        Face back{f[0],f[2],f[1]};
        for(auto& v:back){v=shaded(v,back_tint);v.position=v.position+inward(v.position);}
        s.faces.push_back(back);s.source.push_back(i);
        for(unsigned e=0;e<3;++e) {
            const auto& a=f[e];const auto& b=f[(e+1)%3];
            if(uses.at(edge_key(a.position,b.position))!=1 || length(a.position-b.position)<.02f)continue;
            const auto before=s.faces.size();
            wall(s.faces,a,b,inward(a.position),inward(b.position),wall_tint,two_sided);
            s.source.insert(s.source.end(),s.faces.size()-before,i);
        }
    }
    return s;
}
// Ground infill: the removed surface pressed `depth` down, with walls down from
// the hole's rim, so the cut leaves a packed depression rather than a hole.
Solid recess(const std::vector<Face>& removed,float depth,Vec3 floor_tint,Vec3 wall_tint,bool two_sided) {
    Solid s;const auto uses=edge_uses(removed);const Vec3 down{0,-depth,0};
    for(std::size_t i=0;i<removed.size();++i) {
        const auto& f=removed[i];Face floor=f;
        for(auto& v:floor){v=shaded(v,floor_tint);v.position=v.position+down;}
        s.faces.push_back(floor);s.source.push_back(i);
        for(unsigned e=0;e<3;++e) {
            const auto& a=f[e];const auto& b=f[(e+1)%3];
            if(uses.at(edge_key(a.position,b.position))!=1 || length(a.position-b.position)<.02f)continue;
            const auto before=s.faces.size();
            wall(s.faces,a,b,down,down,wall_tint,two_sided);
            s.source.insert(s.source.end(),s.faces.size()-before,i);
        }
    }
    return s;
}
// Contact samples spread over a shard's hull (farthest-point), so it can rest
// on any side after tumbling.
std::vector<Vec3> contact_samples(const std::vector<Vec3>& points,unsigned count) {
    std::vector<Vec3> samples;if(points.empty())return samples;
    std::size_t start=0;for(std::size_t i=1;i<points.size();++i)if(points[i].y<points[start].y)start=i;
    samples.push_back(points[start]);std::vector<float> nearest(points.size(),1e30f);
    while(samples.size()<count) {
        std::size_t far=0;float best=-1;
        for(std::size_t i=0;i<points.size();++i){nearest[i]=std::min(nearest[i],length(points[i]-samples.back()));if(nearest[i]>best){best=nearest[i];far=i;}}
        if(best<.08f)break;
        samples.push_back(points[far]);
    }
    return samples;
}
constexpr Vec3 packed_floor_tint{.58f,.55f,.52f},packed_wall_tint{.42f,.39f,.37f},shard_back_tint{.5f,.47f,.45f};
}
float slice_radius(unsigned size){return size<=1?2.0f:size==2?3.0f:4.2f;}
Vec3 TerrainSlab::transform(Vec3 local,float alpha)const {
    const auto o=lerp(previous_offset,offset,alpha);
    std::array<Vec3,3> b{lerp(previous_basis[0],basis[0],alpha),lerp(previous_basis[1],basis[1],alpha),lerp(previous_basis[2],basis[2],alpha)};
    return pivot+o+basis_apply(b,local);
}
bool TerrainDestruction::slice(Game& game,Vec3 mouth,Vec3 hit,Vec3 beam,Vec3 sweep,const ObjectModels* models,Vec3 surface) {
    const auto& s=game.destruction_settings;
    if(!s.void_slice || damage_.size()>=max_impacts || suspended_ || !finite(hit) || !finite(beam))return false;
    auto level_beam=Vec3{beam.x,0,beam.z};level_beam=length(level_beam)>.1f?normalized(level_beam):Vec3{std::sin(game.player.yaw),0,std::cos(game.player.yaw)};
    if(!game.level.sliceable.empty()) {
        // Only inside the world's sliceable volumes. A volume with a fall
        // direction (a pylon) is cut level where the beam struck, as one
        // piece holding everything above the cut, and topples that way.
        const auto volume=std::find_if(game.level.sliceable.begin(),game.level.sliceable.end(),[&](const auto& v){
            const auto& b=v.bounds;return hit.x>=b.min.x-.3f && hit.x<=b.max.x+.3f && hit.y>=b.min.y-.3f && hit.y<=b.max.y+.3f && hit.z>=b.min.z-.3f && hit.z<=b.max.z+.3f;});
        if(volume==game.level.sliceable.end()){++protected_impacts;return false;}
        if(length(volume->fall)>.5f) {
            const auto& b=volume->bounds;
            const float cut=std::clamp(hit.y,b.min.y+.35f,b.max.y-1.2f);
            const Vec3 center{(b.min.x+b.max.x)*.5f,cut,(b.min.z+b.max.z)*.5f};
            const float across=std::hypot(b.max.x-b.min.x,b.max.z-b.min.z)*.5f+.1f;
            const float radius=std::clamp(std::max(across,(b.max.y-cut+.2f)/2.6f),1.0f,6.0f);
            TerrainDamage d{center,{0,1,0},game.player.position-Vec3{0,.15f,0},TerrainAttack::VoidSlice,1,0x5ce1u+unsigned(damage_.size())*7919u,s.protect_gameplay,false};
            d.slice_radius=radius;d.shards=0;   // one whole piece
            slice_push_=volume->fall;
            return carve_slice(game,d,models,false);
        }
    }
    Vec3 n,anchor=hit;
    const bool ground=finite(surface) && std::abs(surface.y)>.55f && hit.y<mouth.y+.3f;
    if(ground && length(sweep)<=.6f) {
        // Held on the ground: a level cut just under the surface lifts out a disc.
        n={0,1,0};anchor=hit-Vec3{0,.06f,0};
    } else if(length(sweep)>.6f) {
        // A swept beam cuts along the plane through the beam and its sweep.
        n=normalized(cross(normalized(beam),normalized(sweep)));if(n.y<0)n=n*-1;
        if(std::abs(n.y)<.3f && dot(n,hit-mouth)<0)n=n*-1;   // near-vertical: the far side falls
    } else n=normalized(Vec3{0,1,0}-level_beam*.18f);     // a held beam: a cut tilted away from Spyro
    // Centre the cut prism inside what the beam struck (a tower, not just its
    // near wall), staying on the cutting plane through the contact point.
    const float radius=slice_radius(s.slice_size);
    auto center=hit+level_beam*(radius*.7f);center=center-n*dot(center-anchor,n);
    TerrainDamage d{center,n,game.player.position-Vec3{0,.15f,0},TerrainAttack::VoidSlice,1,0x5ce1u+unsigned(damage_.size())*7919u,s.protect_gameplay,false};
    d.slice_radius=radius;d.shards=max_shards;   // shattered (the actual count is set by the cut)
    slice_push_=level_beam;
    return carve_slice(game,d,models,false);
}
bool TerrainDestruction::erode(Game& game,Vec3 point,Vec3 n,const ObjectModels* models) {
    const auto& s=game.destruction_settings;
    if(!s.void_erode || damage_.size()>=max_impacts-8 || suspended_ || !finite(point) || !finite(n) || length(n)<.5f)return false;
    unsigned scars=0;for(const auto& h:damage_)scars+=h.fragment;
    if(scars>=24 || std::any_of(damage_.begin(),damage_.end(),[&](const auto& h){return h.fragment && length(h.center-point)<.8f;}))return false;
    TerrainDamage hit{point,normalized(n),game.player.position-Vec3{0,.15f,0},TerrainAttack::Superflame,1,0x3a17u+unsigned(damage_.size())*7919u,s.protect_gameplay,true};
    return carve(game,hit,models,false);
}
bool TerrainDestruction::carve_slice(Game& game,const TerrainDamage& hit,const ObjectModels* models,bool replay) {
    auto& l=game.level;const auto region=slice_region(hit);
    auto guard=protection(game,hit,models,region.radius+region.height);
    if(hit.protect_gameplay) {
        // Never cut away the floor Spyro stands on, a script, an NPC or a portal.
        if(region.inside(hit.safe_feet,.6f) || region.inside(hit.safe_feet+Vec3{0,.6f,0},.6f) || region.inside(l.spawn,1.2f))
            {++protected_impacts;return false;}
        for(const auto& g:guard.guards)if(!g.floor_only && region.inside(g.point,g.radius*.6f)){++protected_impacts;return false;}
    }
    struct CollisionEdit {unsigned index;std::vector<Triangle> faces;};struct VisualEdit {unsigned index;std::vector<Face> faces;};
    std::vector<CollisionEdit> collision;std::vector<VisualEdit> visual;
    std::vector<Triangle> slab_collision;std::vector<TerrainVertex> slab_visual;
    std::vector<std::array<TerrainVertex,2>> collision_seam,visual_seam;
    const float reach=region.radius+region.height;
    const auto nc=l.triangles.size(),nv=l.visual_vertices.size();std::size_t added_c=0,added_v=0;
    const float box=reach+.01f;
    float floor_area=0,cut_area=0;
    for(const auto i:l.collision_candidates(hit.center.x-box,hit.center.z-box,hit.center.x+box,hit.center.z+box)) {
        const auto& t=l.triangles[i];
        if(t.disabled || t.water || !near(t,hit.center,reach) || guard.collision.contains(i))continue;
        auto e=extract(face_of(t),region);if(e.inside.empty())continue;
        for(const auto& face:e.inside) {
            slab_collision.push_back(triangle_of(face,t));
            const float area=face_area(face);cut_area+=area;if(std::abs(normal(t).y)>=.55f)floor_area+=area;
        }
        collision_seam.insert(collision_seam.end(),e.seam.begin(),e.seam.end());
        CollisionEdit edit{i};
        for(const auto& face:e.retained)edit.faces.push_back(triangle_of(face,t));
        if(edit.faces.empty()){auto empty=t;empty.disabled=true;empty.b=empty.c=empty.a;edit.faces.push_back(empty);}
        added_c+=edit.faces.size()-1;collision.push_back(std::move(edit));
    }
    if(slab_collision.empty()){++protected_impacts;return false;}
    for(const auto i:l.visual_candidates(hit.center.x-box,hit.center.z-box,hit.center.x+box,hit.center.z+box)) {
        const Face f{l.visual_vertices[i],l.visual_vertices[i+1],l.visual_vertices[i+2]};const Triangle t{f[0].position,f[1].position,f[2].position};
        if(!near(t,hit.center,reach) || guard.visual.contains(i))continue;
        if(f[0].material<l.terrain_materials.size() && l.terrain_materials[f[0].material].translucent)continue;
        auto e=extract(f,region);if(e.inside.empty())continue;
        for(const auto& face:e.inside)slab_visual.insert(slab_visual.end(),face.begin(),face.end());
        visual_seam.insert(visual_seam.end(),e.seam.begin(),e.seam.end());
        if(e.retained.empty()){auto empty=f;empty[1].position=empty[2].position=empty[0].position;e.retained.push_back(empty);}
        added_v+=e.retained.size()*3-3;visual.push_back({i,std::move(e.retained)});
    }
    if(slab_visual.empty()){++protected_impacts;return false;}
    // Ground (mostly walkable floor) is packed; walls and structures are capped.
    // Saves before version 19 (no shards) always used caps and one slab.
    const bool shattered=hit.shards>0,ground=shattered && floor_area>=cut_area*.5f;
    const float thickness=ground?std::clamp(region.radius*.16f,.3f,.6f):.28f;
    std::vector<Face> removed_collision,removed_visual;
    for(const auto& t:slab_collision)removed_collision.push_back(face_of(t));
    for(std::size_t i=0;i+2<slab_visual.size();i+=3)removed_visual.push_back({slab_visual[i],slab_visual[i+1],slab_visual[i+2]});
    std::vector<Face> collision_caps,visual_caps;Solid infill_collision,infill_visual;
    if(ground) {
        infill_collision=recess(removed_collision,thickness,{1,1,1},{1,1,1},false);
        infill_visual=recess(removed_visual,thickness,packed_floor_tint,packed_wall_tint,true);
    } else {collision_caps=caps(collision_seam,region.n,{1,1,1});visual_caps=caps(visual_seam,region.n,void_cut_tint);}
    const std::size_t new_c=collision_caps.size()+infill_collision.faces.size(),new_v=(visual_caps.size()+infill_visual.faces.size())*3;
    if(added_c+new_c>8000 || added_v+new_v>40000 || nc+added_c+new_c>200000 || nv+added_v+new_v>1100000){++protected_impacts;return false;}
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
    // Seal what stays behind: caps on a cut structure (the stump keeps a lid,
    // the block a base), or the packed depression in cut ground.
    for(const auto& c:collision_caps){Triangle t{c[0].position,c[1].position,c[2].position};l.triangles.push_back(t);slab_collision.push_back(t);}
    for(const auto& c:visual_caps){l.visual_vertices.insert(l.visual_vertices.end(),c.begin(),c.end());slab_visual.insert(slab_visual.end(),c.begin(),c.end());}
    for(std::size_t i=0;i<infill_collision.faces.size();++i)l.triangles.push_back(triangle_of(infill_collision.faces[i],slab_collision[infill_collision.source[i]]));
    for(const auto& f:infill_visual.faces)l.visual_vertices.insert(l.visual_vertices.end(),f.begin(),f.end());
    infill_faces+=static_cast<unsigned>(infill_visual.faces.size());
    damage_.push_back(hit);changed_triangles+=static_cast<unsigned>(collision.size());l.dynamic_terrain=true;++l.terrain_revision;++l.collision_revision;++slices;
    const auto record=static_cast<unsigned>(damage_.size()-1);
    // Objects standing in the cut ride the piece beneath them.
    std::vector<std::pair<unsigned,Vec3>> riders;
    for(unsigned i=0;i<game.retail_objects.size();++i) {
        const auto& o=game.retail_objects[i];
        if(o.collected || o.state<0 || o.pc_actor || o.pc_carried || o.scripted_contact || !region.inside(o.position,.2f))continue;
        riders.push_back({i,o.position});
    }
    const auto finish=[&](TerrainSlab& slab,const std::vector<Triangle>& faces,const std::vector<TerrainVertex>& vertices,unsigned samples) {
        Vec3 sum{};std::size_t n=0;
        for(const auto& t:faces){sum+=t.a+t.b+t.c;n+=3;}
        if(!n)for(const auto& v:vertices){sum+=v.position;++n;}
        slab.pivot=sum/float(std::max<std::size_t>(1,n));
        for(auto t:faces){t.a=t.a-slab.pivot;t.b=t.b-slab.pivot;t.c=t.c-slab.pivot;slab.collision.push_back(t);}
        for(auto v:vertices){v.position=v.position-slab.pivot;slab.visual.push_back(v);slab.radius=std::max(slab.radius,length(v.position));}
        std::vector<Vec3> points;for(const auto& t:slab.collision)for(const auto p:{t.a,t.b,t.c})points.push_back(p);
        if(points.empty())for(const auto& v:slab.visual)points.push_back(v.position);
        slab.samples=contact_samples(points,samples);
    };
    if(!shattered) {
        // The original cut: one rigid slab around the block's centroid.
        TerrainSlab slab;slab.record=record;finish(slab,slab_collision,slab_visual,40);
        for(const auto& [index,p]:riders)slab.riders.push_back({index,p-slab.pivot});
        if(replay) {
            slab.offset=slab.previous_offset=hit.slice_offset;
            slab.basis=slab.previous_basis=rotate_basis(slab.basis,hit.slice_rotation);
            land(game,slab);return true;
        }
        slab.velocity=slice_push_*2.2f+Vec3{0,1.4f,0};slab.spin=cross(Vec3{0,1,0},slice_push_)*1.4f;
        if(game.level.toppling_at(hit.center)) {
            // A toppling pylon: it turns about the stump edge on its fall side
            // (tick_slabs), so it lands across what lies in that direction.
            const auto fall=normalized(Vec3{slice_push_.x,0,slice_push_.z});float reach=0,top=hit.center.y;
            for(const auto& t:slab_collision)for(const auto p:{t.a,t.b,t.c}) {
                top=std::max(top,p.y);
                if(std::abs(p.y-hit.center.y)<.05f)reach=std::max(reach,dot(p-hit.center,fall));
            }
            slab.toppling=true;slab.hinge=Vec3{hit.center.x,hit.center.y,hit.center.z}+fall*reach;
            slab.hinge_axis=normalized(cross(Vec3{0,1,0},fall));slab.hinge_length=std::max(.5f,top-hit.center.y);slab.hinge_speed=.5f;
            slab.velocity=slab.spin={};
        }
        slabs_.push_back(std::move(slab));return true;
    }
    // Shatter along Voronoi planes between seeds spread over the block.
    Bounds bounds;for(const auto& t:slab_collision)for(const auto p:{t.a,t.b,t.c})bounds.add(p);
    const float extent=length(bounds.max-bounds.min);
    const unsigned wanted=std::clamp(4u+unsigned(std::lround(extent/2.2f)),min_shards,max_shards);
    const auto seeds=shard_seeds(slab_collision,std::min(wanted,hit.shards),hit.seed);
    Vec3 block{};for(const auto& t:slab_collision)block+=(t.a+t.b+t.c)/3;block=block/float(slab_collision.size());
    const auto inward=[&](Vec3 p)->Vec3 {
        if(ground)return {0,-thickness,0};
        const auto d=block-p;const float n=length(d);return n>.05f?d*(std::min(thickness,n*.8f)/n):Vec3{};
    };
    std::vector<TerrainSlab> shards;
    for(std::size_t s=0;s<seeds.size();++s) {
        std::vector<Face> front_c,front_v;std::vector<Triangle> sources;
        for(const auto& t:slab_collision) {
            const auto f=face_of(t);std::vector<Face> pieces;triangles(voronoi_cell(Polygon{f.begin(),f.end()},seeds,s),pieces);
            for(const auto& p:pieces){front_c.push_back(p);sources.push_back(t);}
        }
        for(std::size_t i=0;i+2<slab_visual.size();i+=3)
            triangles(voronoi_cell(Polygon{slab_visual.begin()+i,slab_visual.begin()+i+3},seeds,s),front_v);
        if(front_c.empty() && front_v.empty())continue;
        const auto solid_c=solidify(front_c,inward,{1,1,1},{1,1,1},false);
        const auto solid_v=solidify(front_v,inward,shard_back_tint,void_cut_tint,true);
        std::vector<Triangle> faces;for(std::size_t i=0;i<solid_c.faces.size();++i)faces.push_back(triangle_of(solid_c.faces[i],sources[solid_c.source[i]]));
        std::vector<TerrainVertex> vertices;for(const auto& f:solid_v.faces)vertices.insert(vertices.end(),f.begin(),f.end());
        // Shard index = seed index, so a replay re-derives the same seeds even
        // when a cell came out empty.
        TerrainSlab slab;slab.record=record;slab.shard=static_cast<unsigned>(s);
        finish(slab,faces,vertices,24);shards.push_back(std::move(slab));
    }
    if(shards.empty()){++protected_impacts;return false;}
    auto& stored=damage_.back();stored.shards=static_cast<unsigned>(seeds.size());
    if(!replay)stored.shard_poses.assign(seeds.size(),{});
    for(const auto& [index,p]:riders) {
        std::size_t best=0;
        for(std::size_t s=1;s<shards.size();++s)if(length(shards[s].pivot-p)<length(shards[best].pivot-p))best=s;
        shards[best].riders.push_back({index,p-shards[best].pivot});
    }
    shards_made+=static_cast<unsigned>(shards.size());
    if(replay) {
        for(auto& slab:shards) {
            if(slab.shard<hit.shard_poses.size()) {
                slab.offset=slab.previous_offset=hit.shard_poses[slab.shard][0];
                slab.basis=slab.previous_basis=rotate_basis(slab.basis,hit.shard_poses[slab.shard][1]);
            }
            land(game,slab);
        }
        return true;
    }
    // Crumble: the pieces nearest the beam break away first; each is thrown
    // outward from the block, pushed by the beam, and set tumbling.
    unsigned seed=hit.seed^0x9e3779b9u;
    std::vector<std::size_t> order(shards.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return length(shards[a].pivot-hit.center)<length(shards[b].pivot-hit.center);});
    for(std::size_t rank=0;rank<order.size();++rank) {
        auto& slab=shards[order[rank]];slab.hold=static_cast<unsigned>(rank)*4;
        auto out=slab.pivot-block;out.y=0;out=length(out)>.05f?normalized(out):slice_push_;
        // Ground crumbles: a small pop and a tip outward, so the pieces break up
        // around the depression; a cut structure is pushed off by the beam.
        const float lift=ground?.9f+random(seed)*.7f:.6f+random(seed)*.8f,throw_out=ground?.5f+random(seed)*.6f:.9f+random(seed)*1.1f;
        slab.velocity=slice_push_*(ground?.5f+random(seed)*.5f:1.2f+random(seed)*.8f)+out*throw_out+Vec3{0,lift,0};
        slab.spin=Vec3{random(seed)-.5f,random(seed)-.5f,random(seed)-.5f}*(ground?1+random(seed)*1.5f:2+random(seed)*3)+cross(Vec3{0,1,0},out)*(ground?.8f:1.2f);
        slabs_.push_back(std::move(slab));
    }
    for(unsigned i=0;i<28 && dust_.size()<256;++i) {
        const float a=random(seed)*6.2831853f;const auto p=hit.center+(region.e1*std::cos(a)+region.e2*std::sin(a))*region.radius*random(seed);
        dust_.push_back({p,p,Vec3{random(seed)-.5f,.6f+random(seed),random(seed)-.5f},.06f+random(seed)*.08f,0,34+unsigned(random(seed)*20)});
    }
    return true;
}
void TerrainDestruction::land_geometry(Level& l,TerrainSlab& slab) {
    for(auto t:slab.collision){t.a=slab.transform(t.a);t.b=slab.transform(t.b);t.c=slab.transform(t.c);l.triangles.push_back(t);}
    for(auto v:slab.visual){v.position=slab.transform(v.position);l.visual_vertices.push_back(v);}
    if(slab.record<damage_.size()) {
        auto& record=damage_[slab.record];
        if(slab.shard!=UINT32_MAX){if(slab.shard<record.shard_poses.size())record.shard_poses[slab.shard]={slab.offset,rotation_vector(slab.basis)};}
        else {record.slice_offset=slab.offset;record.slice_rotation=rotation_vector(slab.basis);}
        record.slice_landed=true;
    }
    l.dynamic_terrain=true;++l.terrain_revision;++l.collision_revision;++slabs_landed;
}
void TerrainDestruction::land(Game& game,TerrainSlab& slab) {
    land_geometry(game.level,slab);
    for(const auto& [index,local]:slab.riders)if(index<game.retail_objects.size()) {
        auto& o=game.retail_objects[index];const auto p=slab.transform(local);o.position=o.previous_position=p;
        if(o.container)o.container->rest_position=p;
    }
}
void TerrainDestruction::tick_slabs(Game& game) {
    constexpr float dt=1.0f/60,gravity=22,friction=.75f,restitution=.18f;
    // Pieces in flight keep apart instead of passing through one another.
    for(std::size_t i=0;i<slabs_.size();++i)for(std::size_t j=i+1;j<slabs_.size();++j) {
        auto& a=slabs_[i];auto& b=slabs_[j];if(a.hold || b.hold)continue;
        auto d=(b.pivot+b.offset)-(a.pivot+a.offset);const float gap=(a.radius+b.radius)*.42f,n=length(d);
        if(n>=gap || n<1e-4f)continue;
        d=d/n;const float push=(gap-n)*.5f;a.offset=a.offset-d*push;b.offset=b.offset+d*push;
        const float closing=dot(b.velocity-a.velocity,d);
        if(closing<0){a.velocity=a.velocity+d*(closing*.5f);b.velocity=b.velocity-d*(closing*.5f);}
    }
    for(auto& slab:slabs_) {
        slab.previous_offset=slab.offset;slab.previous_basis=slab.basis;++slab.age;
        if(slab.hold){--slab.hold;continue;}   // still clinging on: crumbles off in turn
        if(slab.toppling) {
            // A rod turning about its end: angular acceleration 3g/(2L)·sin θ.
            // It keeps turning until any part would pass into the floor.
            const auto pose=[&](float angle) {
                const auto basis=rotate_basis({{{1,0,0},{0,1,0},{0,0,1}}},slab.hinge_axis*angle);
                const auto arm=slab.hinge-slab.pivot;return std::pair{basis,arm-basis_apply(basis,arm)};
            };
            slab.hinge_speed+=1.5f*gravity/slab.hinge_length*std::max(.08f,std::sin(slab.hinge_angle))*dt;
            const float next=slab.hinge_angle+slab.hinge_speed*dt;
            const auto [basis,offset]=pose(next);
            bool touching=false;
            for(const auto local:slab.samples) {
                const auto p=slab.pivot+offset+basis_apply(basis,local);
                if(std::hypot(p.x-slab.hinge.x,p.z-slab.hinge.z)<.35f && p.y>slab.hinge.y-.08f)continue;   // resting on the stump edge
                const auto ground=game.level.raycast(p+Vec3{0,.6f,0},{0,-1,0},.6f);
                if(ground && ground->normal.y>.2f && ground->point.y>p.y+.01f){touching=true;break;}
            }
            if(!touching){slab.hinge_angle=next;slab.basis=basis;slab.offset=offset;}
            if(touching || slab.hinge_angle>2.3f || slab.age>600) {
                land(game,slab);slab.age=UINT32_MAX;game.camera.shake(2600,14);unsigned seed=slab.record*977u+31;
                for(unsigned i=0;i<32 && dust_.size()<256;++i){const auto p=slab.pivot+slab.offset+Vec3{(random(seed)-.5f)*slab.radius,0,(random(seed)-.5f)*slab.radius};
                    dust_.push_back({p,p,Vec3{random(seed)-.5f,.4f+random(seed),random(seed)-.5f},.1f+random(seed)*.1f,0,46});}
            }
            continue;
        }
        slab.velocity.y-=gravity*dt;
        // Sweep the centre so a fast piece cannot pass through a wall.
        const auto from=slab.pivot+slab.offset,step=slab.velocity*dt;const float travel=length(step);
        if(travel>1e-4f)if(const auto wall=game.level.raycast(from,step/travel,travel+slab.radius*.35f);wall && std::abs(wall->normal.y)<.6f) {
            const float vn=dot(slab.velocity,wall->normal);if(vn<0)slab.velocity=slab.velocity-wall->normal*(vn*(1+restitution));
        }
        slab.offset+=slab.velocity*dt;slab.basis=rotate_basis(slab.basis,slab.spin*dt);
        // Hull contacts against the floor: depenetrate, bounce a little, then
        // Coulomb friction and rolling resistance so pieces settle, not slide.
        const auto com=slab.pivot+slab.offset;float deepest=0;Vec3 normal_sum{};unsigned contacts=0;
        for(const auto local:slab.samples) {
            const auto p=slab.pivot+slab.offset+basis_apply(slab.basis,local);
            const auto ground=game.level.raycast(p+Vec3{0,.7f,0},{0,-1,0},.7f);
            if(!ground || ground->normal.y<.2f)continue;
            const float depth=ground->point.y-p.y;if(depth<-.001f)continue;
            ++contacts;normal_sum+=ground->normal;deepest=std::max(deepest,depth);
            const auto r=p-com;slab.spin+=cross(r,ground->normal)*(std::min(depth,.15f)*.55f);
        }
        if(contacts) {
            const auto n=normalized(normal_sum);
            // Lift straight up out of floors: pushing along a sloped normal
            // would nudge the piece downhill every tick.
            if(n.y>.5f)slab.offset.y+=deepest;else slab.offset+=n*deepest;
            const float vn=dot(slab.velocity,n);
            if(vn<0)slab.velocity=slab.velocity-n*(vn*(1+(vn<-2.5f?restitution:0.0f)));
            auto tangent=slab.velocity-n*dot(slab.velocity,n);const float speed=length(tangent);
            const float grip=friction*(gravity*std::max(n.y,.3f)*dt+std::max(0.0f,-vn))*(contacts>=3?1.6f:1.0f);
            const float left=speed>grip?speed-grip:0.0f;
            slab.velocity=slab.velocity-tangent+(speed>1e-5f && left>.18f?tangent*(left/speed):Vec3{});
            slab.spin=slab.spin*(contacts>=3?.78f:.88f);
            // Still pieces settle; one balanced on a single point waits longer.
            if(length(slab.velocity)<.45f && length(slab.spin)<.6f)slab.rest+=contacts>=2?3:1;else slab.rest=0;
        } else slab.rest=0;
        for(const auto& [index,local]:slab.riders)if(index<game.retail_objects.size()) {
            auto& o=game.retail_objects[index];o.previous_position=o.position;o.position=slab.pivot+slab.offset+basis_apply(slab.basis,local);
        }
        if(slab.rest>24 || slab.age>600 || slab.pivot.y+slab.offset.y<game.level.bounds.min.y-20) {
            // Out of time still in the air: settle straight down onto the floor
            // below rather than freezing in mid-air.
            if(!contacts && slab.age>600) {
                float drop=1e30f;
                for(const auto local:slab.samples) {
                    const auto p=slab.pivot+slab.offset+basis_apply(slab.basis,local);
                    if(const auto below=game.level.raycast(p,{0,-1,0},60))drop=std::min(drop,below->distance);
                }
                if(drop<60)slab.offset.y-=drop;
            }
            land(game,slab);slab.age=UINT32_MAX;
            game.camera.shake(slab.shard==UINT32_MAX?1800:700,10);unsigned seed=slab.record*977u+slab.shard*131u+11;
            for(unsigned i=0;i<(slab.shard==UINT32_MAX?24u:8u) && dust_.size()<256;++i){const auto p=slab.pivot+slab.offset+Vec3{(random(seed)-.5f)*slab.radius,0,(random(seed)-.5f)*slab.radius};
                dust_.push_back({p,p,Vec3{random(seed)-.5f,.4f+random(seed),random(seed)-.5f},.08f+random(seed)*.08f,0,40});}
        }
    }
    std::erase_if(slabs_,[](const auto& s){return s.age==UINT32_MAX;});
}
