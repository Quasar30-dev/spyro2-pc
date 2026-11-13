#pragma once
// Walking a level without teleports. A navigation graph is built over the
// level's own collision floors (a 0.6-unit column grid, several floor layers
// per column), with walk edges between neighbouring floor points and jump /
// glide edges from ledges to lower or nearby floors within Spyro's retail
// glide reach. The autopilot then drives Spyro along an A* path with the
// ordinary inputs only (stick direction, jump, the second jump tap that starts
// a glide) and re-plans when he stops making progress. It never moves him.
//
// Used by tools/level_walk.cpp (headless probe) and the workbench's
// --level-exit-check SLOT --walk (desktop_level_exit_check.inl).
#include "spyro/coordinates.hpp"
#include "spyro/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace level_walk {
using namespace spyro;

struct Node {Vec3 p;int ix{},iz{};bool ledge=false,wall=false;};
struct Edge {unsigned to{};float cost{};std::uint8_t kind{};};   // kind 0 walk, 1 jump/glide
struct Graph {
    float cell=0.6f;Vec3 origin;
    std::vector<Node> nodes;std::vector<std::vector<Edge>> edges;
    std::unordered_map<std::int64_t,std::vector<unsigned>> columns;
    static std::int64_t key(int ix,int iz){return (static_cast<std::int64_t>(ix)<<32)^static_cast<std::uint32_t>(iz);}
    const std::vector<unsigned>* column(int ix,int iz)const{const auto it=columns.find(key(ix,iz));return it==columns.end()?nullptr:&it->second;}
    // The floor point under p (same column or a neighbour), within `rise` above / `drop` below.
    std::optional<unsigned> nearest(Vec3 p,float rise=.6f,float drop=2.5f,int reach=2)const {
        const int cx=static_cast<int>(std::floor((p.x-origin.x)/cell+.5f)),cz=static_cast<int>(std::floor((p.z-origin.z)/cell+.5f));
        std::optional<unsigned> best;float best_d=1e9f;
        for(int dz=-reach;dz<=reach;++dz)for(int dx=-reach;dx<=reach;++dx)if(const auto* c=column(cx+dx,cz+dz))for(const auto n:*c) {
            const auto& q=nodes[n].p;const float dy=q.y-p.y;if(dy>rise || dy< -drop)continue;
            const float d=std::hypot(q.x-p.x,q.z-p.z)+std::abs(dy)*.5f;if(d<best_d){best_d=d;best=n;}
        }
        return best;
    }
};

// Spyro's retail reach (MovementTuning): a jump rises ~0.49 units; a glide
// covers 0x1900 / 0x780 = 3.3 units per unit of height lost.
struct Reach {float step=.6f,jump_up=.75f,jump_flat=2.2f,glide_ratio=3.0f,glide_extra=1.6f,max_glide=14.0f;};

inline bool clear(const Level& level,Vec3 a,Vec3 b) {
    const auto d=b-a;const float l=length(d);if(l<1e-4f)return true;
    return !level.raycast(a,d/l,l);
}
inline Graph build(const Level& level,const Reach& reach={},float cell=.6f) {
    Graph g;g.cell=cell;
    Vec3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
    for(const auto& t:level.triangles)for(const auto& v:{t.a,t.b,t.c}){lo=Vec3{std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi=Vec3{std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
    g.origin={lo.x,0,lo.z};
    // Floor samples: every walkable triangle rasterised onto the grid.
    std::unordered_map<std::int64_t,std::vector<float>> heights;
    for(const auto& t:level.triangles) {
        if(t.water || t.disabled)continue;
        const auto n=normalized(cross(t.b-t.a,t.c-t.a));if(std::abs(n.y)<.62f)continue;
        const float x0=std::min({t.a.x,t.b.x,t.c.x}),x1=std::max({t.a.x,t.b.x,t.c.x}),z0=std::min({t.a.z,t.b.z,t.c.z}),z1=std::max({t.a.z,t.b.z,t.c.z});
        const int i0=static_cast<int>(std::ceil((x0-g.origin.x)/cell)),i1=static_cast<int>(std::floor((x1-g.origin.x)/cell));
        const int k0=static_cast<int>(std::ceil((z0-g.origin.z)/cell)),k1=static_cast<int>(std::floor((z1-g.origin.z)/cell));
        for(int i=i0;i<=i1;++i)for(int k=k0;k<=k1;++k) {
            const float x=g.origin.x+i*cell,z=g.origin.z+k*cell;
            // Barycentric in XZ.
            const float d=(t.b.z-t.c.z)*(t.a.x-t.c.x)+(t.c.x-t.b.x)*(t.a.z-t.c.z);if(std::abs(d)<1e-9f)continue;
            const float w0=((t.b.z-t.c.z)*(x-t.c.x)+(t.c.x-t.b.x)*(z-t.c.z))/d,w1=((t.c.z-t.a.z)*(x-t.c.x)+(t.a.x-t.c.x)*(z-t.c.z))/d,w2=1-w0-w1;
            if(w0< -1e-4f || w1< -1e-4f || w2< -1e-4f)continue;
            heights[Graph::key(i,k)].push_back(w0*t.a.y+w1*t.b.y+w2*t.c.y);
        }
    }
    for(auto& [k,hs]:heights) {
        std::sort(hs.begin(),hs.end());
        const int ix=static_cast<int>(k>>32),iz=static_cast<int>(static_cast<std::int32_t>(k&0xffffffff));
        float last=-1e9f;
        for(const float h:hs) {
            if(h-last<.35f)continue;last=h;
            const Vec3 p{g.origin.x+ix*cell,h,g.origin.z+iz*cell};
            // Headroom for Spyro (his origin is 0.35 above his feet).
            if(const auto up=level.raycast(p+Vec3{0,.05f,0},{0,1,0},.75f);up)continue;
            Node node{p,ix,iz};
            Vec3 v{};bool ground=false;const auto c=p+Vec3{0,.4f,0};
            if(length(level.resolve_sphere(c,.3f,v,ground)-c)>.08f)node.wall=true;
            g.columns[k].push_back(static_cast<unsigned>(g.nodes.size()));g.nodes.push_back(node);
        }
    }
    g.edges.resize(g.nodes.size());
    // Walk edges: neighbouring columns, a step up or a short drop, no wall between.
    for(unsigned a=0;a<g.nodes.size();++a) {
        const auto& na=g.nodes[a];unsigned neighbours=0;
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)if((dx||dz))if(const auto* c=g.column(na.ix+dx,na.iz+dz))for(const auto b:*c) {
            const auto& nb=g.nodes[b];const float dy=nb.p.y-na.p.y;
            if(dy>reach.step || dy< -1.2f)continue;
            if(!clear(level,na.p+Vec3{0,.35f,0},nb.p+Vec3{0,.35f,0}))continue;
            if(dy>=-reach.step)++neighbours;
            const float d=length(nb.p-na.p);
            g.edges[a].push_back({b,d*(nb.wall?1.6f:1.0f)+(dy< -reach.step?.5f:0),0});
        }
        g.nodes[a].ledge=neighbours<8;
    }
    // Jump / glide edges from ledges: along 24 headings, the nearest landing
    // (and the farthest one) within reach that is not simply walkable ground
    // around the ledge, with a clear arc.
    for(unsigned a=0;a<g.nodes.size();++a) {
        const auto& na=g.nodes[a];if(!na.ledge)continue;
        for(unsigned h=0;h<24;++h) {
            const float angle=h*6.2831853f/24;const Vec3 dir{std::cos(angle),0,std::sin(angle)};
            // Only off the ledge: the first step that way must not be level floor.
            {
                const auto ahead=na.p+dir*cell*1.2f;
                const int ix=static_cast<int>(std::floor((ahead.x-g.origin.x)/cell+.5f)),iz=static_cast<int>(std::floor((ahead.z-g.origin.z)/cell+.5f));
                bool floor=false;if(const auto* c=g.column(ix,iz))for(const auto b:*c)if(std::abs(g.nodes[b].p.y-na.p.y)<reach.step)floor=true;
                if(floor)continue;
            }
            std::optional<unsigned> nearest,farthest;
            for(float hd=1.0f;hd<=reach.max_glide;hd+=cell) {
                const auto at=na.p+dir*hd;
                const int ix=static_cast<int>(std::floor((at.x-g.origin.x)/cell+.5f)),iz=static_cast<int>(std::floor((at.z-g.origin.z)/cell+.5f));
                const auto* c=g.column(ix,iz);if(!c)continue;
                for(const auto b:*c) {
                    const auto& nb=g.nodes[b];const float dy=nb.p.y-na.p.y;
                    if(dy>reach.jump_up)continue;
                    const float allowed=dy>-.1f?reach.jump_flat:reach.glide_extra+reach.glide_ratio*(-dy);
                    if(hd>allowed)continue;
                    if(!nearest)nearest=b;else farthest=b;
                }
            }
            for(const auto b:{nearest,farthest})if(b) {
                const auto& nb=g.nodes[*b];
                const auto top=na.p+Vec3{0,.85f,0},land=nb.p+Vec3{0,.5f,0};
                const auto mid=Vec3{(top.x+land.x)/2,std::max(top.y,land.y)+.1f,(top.z+land.z)/2};
                if(!clear(level,top,mid) || !clear(level,mid,land))continue;
                g.edges[a].push_back({*b,std::hypot(nb.p.x-na.p.x,nb.p.z-na.p.z)*1.3f+2.0f,1});
            }
        }
    }
    return g;
}
inline std::vector<unsigned> astar(const Graph& g,unsigned from,const std::function<bool(const Node&)>& goal,Vec3 goal_point,
                                   const std::unordered_set<std::uint64_t>& banned={}) {
    std::vector<float> cost(g.nodes.size(),1e30f);std::vector<unsigned> parent(g.nodes.size(),UINT32_MAX);
    using Item=std::pair<float,unsigned>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> open;
    cost[from]=0;open.push({length(g.nodes[from].p-goal_point),from});
    while(!open.empty()) {
        const auto [f,n]=open.top();open.pop();
        if(goal(g.nodes[n])) {
            std::vector<unsigned> path;for(auto k=n;k!=UINT32_MAX;k=parent[k])path.push_back(k);
            std::reverse(path.begin(),path.end());return path;
        }
        if(f-length(g.nodes[n].p-goal_point)>cost[n]+1e-3f)continue;
        for(const auto& e:g.edges[n]) {
            if(banned.contains((static_cast<std::uint64_t>(n)<<32)|e.to))continue;
            const float c=cost[n]+e.cost;
            if(c<cost[e.to]){cost[e.to]=c;parent[e.to]=n;open.push({c+length(g.nodes[e.to].p-goal_point),e.to});}
        }
    }
    return {};
}

// The autopilot. step() returns the input for this tick; it only reads the game.
struct Walker {
    Graph graph;Reach reach;
    std::function<bool(const Node&)> goal;Vec3 goal_point;
    std::vector<unsigned> path;std::size_t at{};
    std::unordered_set<std::uint64_t> banned;
    unsigned ticks{},since_progress{},replans{},jumps{},glides{},stuck_events{};
    float best_left=1e9f;Vec3 last_position;
    // Jump / glide edge in flight.
    unsigned air_ticks{};bool airborne_edge=false;Vec3 landing;
    std::string log;
    bool plan(const Game& g) {
        const auto start=graph.nearest(g.player.position,.8f,3.0f,3);
        if(!start)return false;
        path=astar(graph,*start,goal,goal_point,banned);at=0;++replans;best_left=1e9f;since_progress=0;
        return !path.empty();
    }
    bool done(const Game& g)const{
        if(path.empty())return false;
        const auto& p=graph.nodes[path.back()].p;return length(g.player.position-p)<.9f;
    }
    float remaining(const Game& g)const {
        if(path.empty() || at>=path.size())return 0;
        float d=length(graph.nodes[path[at]].p-g.player.position);
        for(auto k=at;k+1<path.size();++k)d+=length(graph.nodes[path[k+1]].p-graph.nodes[path[k]].p);
        return d;
    }
    static std::uint8_t edge_kind(const Graph& gr,unsigned a,unsigned b){for(const auto& e:gr.edges[a])if(e.to==b)return e.kind;return 0;}
    InputState step(const Game& g) {
        InputState in;++ticks;
        if(path.empty() || at>=path.size())return in;
        const auto pos=g.player.position;
        // Advance along the path: skip nodes already reached (horizontally close, similar height).
        while(at+1<path.size()) {
            const auto& p=graph.nodes[path[at]].p;
            if(std::hypot(p.x-pos.x,p.z-pos.z)<.55f && std::abs(p.y-pos.y)<.7f && g.player.grounded)++at;
            else break;
        }
        const auto target=graph.nodes[path[at]].p;
        const bool jump_edge=at>0 && edge_kind(graph,path[at-1],path[at])==1;
        // Progress watchdog.
        const float left=remaining(g);
        if(left<best_left-.2f){best_left=left;since_progress=0;}else ++since_progress;
        if(since_progress>240) {
            ++stuck_events;
            if(at>0)banned.insert((static_cast<std::uint64_t>(path[at-1])<<32)|path[at]);
            log+="stuck at "+std::to_string(pos.x)+","+std::to_string(pos.y)+","+std::to_string(pos.z)+"; ";
            plan(g);return in;
        }
        auto d=target-pos;d.y=0;const float dist=length(d);
        if(dist>1e-3f){in.move_x=d.x/dist;in.move_z=d.z/dist;}
        // A jump / glide edge: run at the ledge, jump at it, glide once falling.
        if(jump_edge) {
            const auto& from=graph.nodes[path[at-1]].p;
            if(g.player.grounded) {
                airborne_edge=false;
                const float to_ledge=std::hypot(from.x-pos.x,from.z-pos.z);
                if(to_ledge<.6f || std::hypot(target.x-pos.x,target.z-pos.z)<std::hypot(target.x-from.x,target.z-from.z)-.3f) {
                    in.jump_pressed=in.jump_held=true;++jumps;airborne_edge=true;air_ticks=0;
                }
            } else {
                ++air_ticks;in.jump_held=true;
                // Second tap: glide (after the apex, when the landing is not close).
                const bool need_glide=target.y<pos.y-.2f ? std::hypot(target.x-pos.x,target.z-pos.z)>1.2f : std::hypot(target.x-pos.x,target.z-pos.z)>.8f;
                if(air_ticks==14 && need_glide && !g.player.air.gliding){in.jump_pressed=true;++glides;}
            }
        }
        return in;
    }
};
} // namespace level_walk
