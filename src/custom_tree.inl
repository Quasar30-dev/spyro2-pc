// Retail forest canopies share edges across terrain regions. Isolating a region
// exposes those joins. Retain its source geometry and close only foliage seams.
namespace custom_tree {
using Key=std::array<int,3>;
Key key(Vec3 p){return {int(std::lround(p.x*64)),int(std::lround(p.y*64)),int(std::lround(p.z*64))};}
struct Edge {TerrainVertex a,b;};
std::vector<TerrainVertex> isolate(const Level& source,unsigned region) {
    std::vector<TerrainVertex> out;
    for(const auto& v:source.visual_vertices)if(v.source_region==region)out.push_back(v);
    std::map<std::pair<Key,Key>,std::vector<Edge>> edges;
    Bounds foliage;unsigned leaves=0;
    for(const auto& v:out)if(v.material==49){foliage.add(v.position);++leaves;}
    if(!leaves)return out;
    const auto centre=foliage.center(),radii=(foliage.max-foliage.min)*.5f;
    const auto unit=[&](Vec3 p){return Vec3{p.x/std::max(.01f,radii.x),p.y/std::max(.01f,radii.y),p.z/std::max(.01f,radii.z)};};
    const auto surface=[&](Vec3 p){const auto d=p-centre;const auto n=length(unit(d));return n>.001f?centre+d/n:p;};
    const auto emit=[&](TerrainVertex a,TerrainVertex b,TerrainVertex c){
        a.u=(64+.02f*64)/source.texture_width;a.v=(192+.02f*64)/source.texture_height;
        b.u=(64+.98f*64)/source.texture_width;b.v=a.v;
        c.u=(64+.5f*64)/source.texture_width;c.v=(192+.98f*64)/source.texture_height;
        out.insert(out.end(),{a,b,c});
    };
    for(unsigned i=0;i+2<out.size();i+=3)for(unsigned j=0;j<3;++j) {
        const auto a=out[i+j],b=out[i+(j+1)%3];auto ka=key(a.position),kb=key(b.position);
        if(kb<ka)std::swap(ka,kb);edges[{ka,kb}].push_back({a,b});
    }
    std::vector<Edge> boundary;
    for(const auto& [k,e]:edges)if(e.size()==1 && e[0].a.material==49)boundary.push_back(e[0]);
    std::vector<bool> seen(boundary.size());
    for(unsigned seed=0;seed<boundary.size();++seed) {
        if(seen[seed])continue;
        std::vector<unsigned> component{seed};seen[seed]=true;
        for(unsigned cursor=0;cursor<component.size();++cursor) {
            const auto& edge=boundary[component[cursor]];const auto a=key(edge.a.position),b=key(edge.b.position);
            for(unsigned i=0;i<boundary.size();++i)if(!seen[i]) {
                const auto c=key(boundary[i].a.position),d=key(boundary[i].b.position);
                if(a==c || a==d || b==c || b==d){seen[i]=true;component.push_back(i);}
            }
        }
        std::map<Key,Vec3> points;
        for(auto i:component){points[key(boundary[i].a.position)]=boundary[i].a.position;points[key(boundary[i].b.position)]=boundary[i].b.position;}
        Vec3 hub{};for(const auto& [k,p]:points)hub+=p;hub=hub/float(points.size());
        Vec3 normal{};for(auto i:component)normal+=cross(boundary[i].a.position-centre,boundary[i].b.position-centre);
        normal=normalized(normal);if(dot(normal,hub-centre)<0)normal=normal*-1;
        const auto d=unit(hub-centre),n=unit(normal);const float aa=dot(n,n),bb=2*dot(d,n);
        if(aa>.00001f)hub+=normal*std::max(0.f,(-bb+std::sqrt(std::max(0.f,bb*bb-4*aa*(dot(d,d)-1))))/(2*aa));
        for(auto i:component) {
            auto a=boundary[i].a,b=boundary[i].b,h=a;h.position=hub;h.color=(a.color+b.color)*.5f;
            auto ap=a,bp=b;
            for(float t:{1.f/3,2.f/3}) {
                auto an=a,bn=b;const auto av=lerp(a.position,hub,t),bv=lerp(b.position,hub,t);
                an.position=lerp(av,surface(av),.65f);bn.position=lerp(bv,surface(bv),.65f);
                an.color=lerp(a.color,h.color,t);bn.color=lerp(b.color,h.color,t);
                emit(bp,ap,an);emit(bp,an,bn);ap=an;bp=bn;
            }
            emit(bp,ap,h);
        }
    }
    return out;
}
}
