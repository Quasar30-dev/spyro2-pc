// Walks a campaign level from its start with ordinary inputs only (no
// teleports): tools/level_walk.hpp builds a navigation graph over the level's
// collision floors and drives Spyro along an A* path to a goal, re-planning
// when he stops making progress. Headless; prints the trip and writes a
// top-down map (graph by height, planned path, the trip actually walked).
// Usage: spyro_level_walk <retail data> <slot> [goal x y z] [--map file.bmp] [--ticks N] [--radius R]
// Without a goal the level's talisman / completion speaker (nearest to Return Home) is the goal.
#include "spyro/assets.hpp"
#include "spyro/npc.hpp"
#include "spyro/retail_scripts.hpp"
#include "spyro/travel.hpp"
#include "spyro/campaign.hpp"
#include "spyro/camera.hpp"
#include "level_walk.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>

using namespace spyro;
namespace {
void write_map(const std::string& file,const level_walk::Graph& g,const std::vector<unsigned>& path,const std::vector<Vec3>& trip,Vec3 goal,const std::vector<bool>& reachable) {
    float x0=1e9f,x1=-1e9f,z0=1e9f,z1=-1e9f,y0=1e9f,y1=-1e9f;
    for(const auto& n:g.nodes){x0=std::min(x0,n.p.x);x1=std::max(x1,n.p.x);z0=std::min(z0,n.p.z);z1=std::max(z1,n.p.z);y0=std::min(y0,n.p.y);y1=std::max(y1,n.p.y);}
    const float scale=4/g.cell*.6f;const int w=static_cast<int>((x1-x0)*scale)+8,h=static_cast<int>((z1-z0)*scale)+8;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w)*h*3,20);
    std::cout<<"map "<<file<<": pixel = (x - "<<x0<<", z - "<<z0<<") * "<<scale<<" + 4; grid lines every 10 units (brighter every 50)"<<std::endl;
    for(int X=0;X<w;++X)for(int Z=0;Z<h;++Z) {
        const float x=(X-4)/scale+x0,z=(Z-4)/scale+z0;
        const bool vx=std::fmod(std::abs(x),10.0f)<1/scale,vz=std::fmod(std::abs(z),10.0f)<1/scale;
        if(vx || vz){const bool major=(vx && std::fmod(std::abs(x),50.0f)<1/scale) || (vz && std::fmod(std::abs(z),50.0f)<1/scale);auto* q=&img[(static_cast<std::size_t>(Z)*w+X)*3];q[0]=q[1]=q[2]=major?90:45;}
    }
    const auto put=[&](float x,float z,std::uint8_t r,std::uint8_t gg,std::uint8_t b,int size) {
        const int px=static_cast<int>((x-x0)*scale)+4,pz=static_cast<int>((z-z0)*scale)+4;
        for(int dz=-size;dz<=size;++dz)for(int dx=-size;dx<=size;++dx) {
            const int X=px+dx,Z=pz+dz;if(X<0||Z<0||X>=w||Z>=h)continue;
            auto* p=&img[(static_cast<std::size_t>(Z)*w+X)*3];p[0]=b;p[1]=gg;p[2]=r;
        }
    };
    for(unsigned i=0;i<g.nodes.size();++i) {   // reachable from the start: green by height; not: blue-grey
        const auto& n=g.nodes[i];const auto v=static_cast<std::uint8_t>(60+180*(n.p.y-y0)/std::max(1.0f,y1-y0));
        if(i<reachable.size() && reachable[i])put(n.p.x,n.p.z,v/3,v,v/2,1);else put(n.p.x,n.p.z,v/3,v/3,v,1);
    }
    for(const auto k:path)put(g.nodes[k].p.x,g.nodes[k].p.z,255,40,40,1);
    for(const auto& p:trip)put(p.x,p.z,255,255,255,0);
    put(goal.x,goal.z,255,255,0,4);
    if(!trip.empty())put(trip.front().x,trip.front().z,0,160,255,4);
    std::ofstream out(file,std::ios::binary);const int row=(w*3+3)&~3;const std::uint32_t size=54+row*h;
    const auto u32=[&](std::uint32_t v){out.write(reinterpret_cast<const char*>(&v),4);};const auto u16=[&](std::uint16_t v){out.write(reinterpret_cast<const char*>(&v),2);};
    out.write("BM",2);u32(size);u32(0);u32(54);u32(40);u32(static_cast<std::uint32_t>(w));u32(static_cast<std::uint32_t>(-h));u16(1);u16(24);u32(0);u32(row*h);u32(2835);u32(2835);u32(0);u32(0);
    std::vector<char> pad(row-w*3,0);
    for(int z=0;z<h;++z){out.write(reinterpret_cast<const char*>(&img[static_cast<std::size_t>(z)*w*3]),w*3);out.write(pad.data(),static_cast<std::streamsize>(pad.size()));}
}
void keep_alive(Game& g){g.player.health=std::max(g.player.health,3u);g.player.lives=std::max(g.player.lives,4u);g.player.invulnerable_ticks=std::max(g.player.invulnerable_ticks,4u);}
}
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"Usage: spyro_level_walk <retail data> <slot> [x y z] [--map file.bmp] [--ticks N] [--radius R]\n";return 2;}
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
        const unsigned slot=std::stoul(argv[2]);std::optional<Vec3> goal;std::string map;unsigned limit=36000;float radius=1.5f;std::optional<unsigned> arrival_scene;
        for(int i=3;i<argc;++i) {
            const std::string a=argv[i];
            if(a=="--map" && i+1<argc)map=argv[++i];
            else if(a=="--ticks" && i+1<argc)limit=std::stoul(argv[++i]);
            else if(a=="--radius" && i+1<argc)radius=std::stof(argv[++i]);
            else if(a=="--arrival" && i+1<argc)arrival_scene=std::stoul(argv[++i]);
            else if(a=="--portals") {   // the level's portals: centre, aperture normal, destination, scene, and the campaign arrival from each scene
                const auto archive=wad.read(slot);Level level=decode_level(archive,slot);decode_travel(archive,exe,level);
                for(const auto& p:level.portals) {
                    Vec3 c{};for(const auto v:p.aperture)c+=v;c=c/5;const auto n=normalized(cross(p.aperture[1]-p.aperture[0],p.aperture[2]-p.aperture[0]));
                    std::printf("portal dest %u scene %u return %d centre %.2f %.2f %.2f normal %.2f %.2f %.2f",p.destination,p.scene_id,p.return_home?1:0,c.x,c.y,c.z,n.x,n.y,n.z);
                    if(!p.return_home)try {
                        const auto arrival=campaign_arrival(archive,p.scene_id,level);
                        std::printf(" arrival %.2f %.2f %.2f yaw-dir %.2f %.2f side %.2f",arrival.position.x,arrival.position.y,arrival.position.z,std::sin(arrival.yaw),std::cos(arrival.yaw),dot(arrival.position-c,n));
                    }catch(const std::exception& e){std::printf(" arrival: %s",e.what());}
                    std::printf("\n");
                }
                return 0;
            }
            else if(a=="--triangles" && i+4<argc) {   // list the collision triangles whose centre lies in an XZ box
                const float bx0=std::stof(argv[i+1]),bz0=std::stof(argv[i+2]),bx1=std::stof(argv[i+3]),bz1=std::stof(argv[i+4]);i+=4;
                const auto level=decode_level(wad.read(slot),slot);
                for(const auto& t:level.triangles) {
                    const Vec3 c=(t.a+t.b+t.c)/3;if(c.x<bx0 || c.x>bx1 || c.z<bz0 || c.z>bz1)continue;
                    const auto n=normalized(cross(t.b-t.a,t.c-t.a));
                    std::printf("tri c %.2f %.2f %.2f n %.2f %.2f %.2f y %.2f..%.2f surface %d portal %d water %d\n",c.x,c.y,c.z,n.x,n.y,n.z,
                        std::min({t.a.y,t.b.y,t.c.y}),std::max({t.a.y,t.b.y,t.c.y}),static_cast<int>(t.surface_type),static_cast<int>(t.portal),t.water?1:0);
                }
                return 0;
            }
            else if(i+2<argc){goal=Vec3{std::stof(argv[i]),std::stof(argv[i+1]),std::stof(argv[i+2])};i+=2;}
        }
        const auto archive=wad.read(slot);
        Level level=decode_level(archive,slot);decode_travel(archive,exe,level);auto models=decode_object_models(archive,exe);
        initialize_retail_objects(level,models);level.script_image=make_retail_script_image(wad,exe,slot);
        Scene scene;scene.wad_slot=slot;scene.spawn=level.spawn;Game g(level,scene,decode_camera_settings(exe),&models,nullptr);
        for(unsigned t=0;t<4;++t){keep_alive(g);g.advance(FixedClock::step,{});}
        if(arrival_scene) {   // the grounded home-world arrival from a level (campaign_arrival), then the camera over 240 ticks
            const auto a=campaign_arrival(archive,*arrival_scene,g.level);
            g.play_scene.spawn=a.position;g.respawn();g.player.yaw=g.player.previous_yaw=a.yaw;g.camera.reset(g.level,g.player.position,a.yaw);
            for(unsigned t=0;t<=240;++t) {
                if(t%60==0) {
                    const auto v=g.camera.sample(1);const auto s=g.player.position+retail::origin_above_feet;
                    std::printf("t %u spyro %.2f %.2f %.2f eye %.2f %.2f %.2f distance %.2f obstructed %d\n",t,s.x,s.y,s.z,v.eye.x,v.eye.y,v.eye.z,length(v.eye-s),g.camera.obstructed()?1:0);
                }
                keep_alive(g);g.advance(FixedClock::step,{});
            }
            return 0;
        }
        if(std::getenv("CAMERA_PROFILES")) {   // 8006163c: conversation views 0..2, reward views 3 / 4 (yaw, pitch, radius, ...)
            const auto settings=decode_camera_settings(exe);
            for(unsigned i=0;i<settings.conversation.size();++i) {
                std::printf("view %u:",i);for(const auto value:settings.conversation[i])std::printf(" %d",value);std::printf("\n");
            }
            return 0;
        }
        if(const char* list=std::getenv("OBJECTS")) {   // OBJECTS=a,b,c: class, position, RAM state and the first header bytes
            for(const char* p=list;*p;) {
                const unsigned i=std::strtoul(p,const_cast<char**>(&p),10);if(*p==',')++p;
                if(i>=g.retail_objects.size())continue;
                const auto& o=g.retail_objects[i];const auto& v=g.overlay_scripts;const auto m=v.moby_address(i);const auto h=v.peek(m);
                std::printf("object %u moby %08x type %x spirit %u at %.2f %.2f %.2f state %u/%u script %d header",i,m,o.type,o.record[83]&127u,o.position.x,o.position.y,o.position.z,v.peek(m+0x48,1),v.peek(m+0x49,1),o.retail_script?1:0);
                if(h>=0x80000000u && h<0x80800000u)for(unsigned b=0;b<0x48;b+=4)std::printf(" %08x",v.peek(h+b));
                std::printf("\n");
            }
            return 0;
        }
        if(const char* under=std::getenv("UNDER")) {   // UNDER=x,z[,ticks]: the collision floor and the drawn faces at that point
            float x{},z{};unsigned ticks=0;std::sscanf(under,"%f,%f,%u",&x,&z,&ticks);
            for(unsigned t=0;t<ticks;++t){keep_alive(g);g.advance(FixedClock::step,{});}
            for(float top=80;top>-40;) {
                const auto hit=g.level.raycast({x,top,z},{0,-1,0},top+60);if(!hit)break;
                const auto id=hit->triangle;bool morph=false,moved=false;
                if(!(id&Level::object_triangle)) {
                    for(const auto& m:g.level.terrain_morphs)if(m.collision && m.frames.size() && id>=m.first_triangle && id<m.first_triangle+m.frames[0].size()/3)morph=true;
                    for(const auto& tr:g.level.terrain_translations)if(std::find(tr.triangles.begin(),tr.triangles.end(),id)!=tr.triangles.end())moved=true;
                }
                std::printf("floor y %.3f triangle %u%s source %u normal.y %.2f surface %d morph %d translated %d\n",hit->point.y,id&~Level::object_triangle,
                    (id&Level::object_triangle)?" (object)":"",(id&Level::object_triangle)?0u:g.level.triangles[id].source_id,hit->normal.y,static_cast<int>(hit->surface_type),morph?1:0,moved?1:0);
                top=hit->point.y-.05f;
            }
            const auto& vv=g.level.visual_vertices;
            for(std::size_t i=0;i+2<vv.size();i+=3) {
                const auto a=vv[i].position,b=vv[i+1].position,c=vv[i+2].position;
                const float d=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);if(std::abs(d)<1e-9f)continue;
                const float w0=((b.z-c.z)*(x-c.x)+(c.x-b.x)*(z-c.z))/d,w1=((c.z-a.z)*(x-c.x)+(a.x-c.x)*(z-c.z))/d,w2=1-w0-w1;
                if(w0<0 || w1<0 || w2<0)continue;
                std::printf("drawn face %zu y %.3f region %u material %u\n",i/3,w0*a.y+w1*b.y+w2*c.y,vv[i].source_region,vv[i].material);
            }
            return 0;
        }
        if(const char* box=std::getenv("BOX")) {   // BOX=index,offset: stand in the box at header+offset (centre x,y,z) and trace
            unsigned index{},offset{};std::sscanf(box,"%u,%x",&index,&offset);
            auto& v=g.overlay_scripts;const auto m=v.moby_address(index);const auto h=v.peek(m);
            const auto centre=retail::to_world({static_cast<int>(v.peek(h+offset)),static_cast<int>(v.peek(h+offset+4)),static_cast<int>(v.peek(h+offset+8))});
            const auto floor=g.level.raycast(centre+Vec3{0,2,0},{0,-1,0},8);if(!floor)throw FormatError("no floor");
            g.player.position=g.player.previous_position=floor->point+Vec3{0,.01f,0};g.player.velocity={};
            for(unsigned t=0;t<3600;++t) {
                keep_alive(g);InputState in;if(g.dialogue.active())in.dialogue.next=(t%30)==0;g.advance(FixedClock::step,in);
                if(t%60==0 || (t>600 && t<760 && t%2==0))std::printf("t %u state %u/%u cam %u/%u req %u timer %u 80067100 %u gs %u restart %d f44 %u spirits %u dialogue %d warps %u\n",t,v.peek(m+0x48,1),v.peek(m+0x49,1),
                    v.peek(0x80067ed0),v.peek(0x80067ed4),v.peek(0x80067fec),v.peek(0x80067ed8),v.peek(0x80067100),v.peek(0x800681c8),v.restarting()?1:0,v.peek(0x80066f44,1),g.spirits(),g.dialogue.active()?1:0,v.warps,static_cast<int>(static_cast<std::int8_t>(v.peek(m+0x4a,1))),static_cast<int>(static_cast<std::int8_t>(v.peek(m+0x4c,1))),v.peek(m+0x4d,1));
            }
            return 0;
        }
        if(const char* list=std::getenv("TALK_TO")) {   // TALK_TO=a,b,c: stand before each, press E, read the conversation through
            auto& v=g.overlay_scripts;
            const auto region_y=[&](unsigned region){float lo=1e9f,hi=-1e9f;for(const auto& q:g.level.visual_vertices)if(q.source_region==region){lo=std::min(lo,q.position.y);hi=std::max(hi,q.position.y);}return std::pair{lo,hi};};
            for(const char* p=list;*p;) {
                const unsigned i=std::strtoul(p,const_cast<char**>(&p),10);if(*p==',')++p;
                const auto o=g.retail_objects[i];const float yaw=retail::native_heading(o.rotation[2]*16);
                bool placed=false;
                for(float spread:{0.0f,0.6f,-0.6f,1.2f,-1.2f,2.0f,-2.0f,3.14f}) {
                    const Vec3 d{std::sin(yaw+spread),0,std::cos(yaw+spread)};
                    const auto f=g.level.raycast(o.position+d*1.3f+Vec3{0,2,0},{0,-1,0},4);
                    if(!f || std::abs(f->point.y-o.position.y)>1)continue;
                    g.player.position=g.player.previous_position=f->point+Vec3{0,.01f,0};g.player.velocity={};g.player.yaw=g.player.previous_yaw=std::atan2(-d.x,-d.z);placed=true;break;
                }
                // Flame first (the encased satyrs wait for an attack, +18 & 30000), then talk.
                if(std::getenv("FLAME"))for(unsigned t=0;t<240 && !g.dialogue.active();++t){keep_alive(g);InputState in;in.flame_pressed=(t%30)==0;g.advance(FixedClock::step,in);}
                std::printf("  flamed %u\n",i);
                if(std::getenv("CHARGE"))for(unsigned t=0;t<90 && !g.dialogue.active();++t) {   // charge into it
                    keep_alive(g);InputState in;auto d=o.position-g.player.position;d.y=0;const float l=length(d);
                    if(l>.05f){in.move_x=d.x/l;in.move_z=d.z/l;}in.charge=t<45;g.advance(FixedClock::step,in);
                }
                std::printf("  charged %u\n",i);
                unsigned t=0;for(;t<400 && !g.dialogue.active();++t){keep_alive(g);InputState in;in.dialogue.interact=(t%8)==0;g.advance(FixedClock::step,in);}
                const bool talked=g.dialogue.active();if(talked)std::printf("  dialogue with %u\n",g.dialogue.object());
                for(unsigned k=0;k<3000 && g.dialogue.active();++k){keep_alive(g);InputState in;if(k%30==29){const auto* line=g.dialogue.line();if(line && line->yes>=0 && line->no>=0)in.dialogue.choice=0;else in.dialogue.next=true;}g.advance(FixedClock::step,in);}
                for(unsigned k=0;k<600;++k){keep_alive(g);g.advance(FixedClock::step,{});}
                const auto h=v.peek(v.moby_address(i));const auto counter=h>=0x80000000u?v.peek(v.peek(h+0x20)):0;
                const auto [lo,hi]=region_y(82);
                std::printf("talk %u placed %d talked %d state %u/%u counter %u 80066fec %u region82 y %.2f..%.2f\n",i,placed?1:0,talked?1:0,v.peek(v.moby_address(i)+0x48,1),v.peek(v.moby_address(i)+0x49,1),counter,v.peek(0x80066fec),lo,hi);
            }
            if(const char* walk=std::getenv("WALK_END")) {
                // WALK_END=x0,z0,x1,z1[,x2,z2...]: let the scene settle, then walk the
                // polyline with the stick only; each grounded tick, a drawn terrain
                // face must lie under Spyro within 0.3 of his feet (no mid-air).
                for(unsigned k=0;k<1500;++k){keep_alive(g);InputState in;if(g.dialogue.active())in.dialogue.next=(k%30)==29;g.advance(FixedClock::step,in);}
                std::vector<Vec3> points;for(const char* p=walk;*p;){const float x=std::strtof(p,const_cast<char**>(&p));if(*p==',')++p;const float z=std::strtof(p,const_cast<char**>(&p));if(*p==',')++p;points.push_back({x,0,z});}
                const auto drawn_under=[&](Vec3 feet) {   // highest drawn face at or just below the feet
                    float best=-1e9f;const auto& vv=g.level.visual_vertices;
                    for(std::size_t q=0;q+2<vv.size();q+=3) {
                        const auto a=vv[q].position,b=vv[q+1].position,c=vv[q+2].position;
                        const float d=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);if(std::abs(d)<1e-9f)continue;
                        const float w0=((b.z-c.z)*(feet.x-c.x)+(c.x-b.x)*(feet.z-c.z))/d,w1=((c.z-a.z)*(feet.x-c.x)+(a.x-c.x)*(feet.z-c.z))/d,w2=1-w0-w1;
                        if(w0<0 || w1<0 || w2<0)continue;
                        const float y=w0*a.y+w1*b.y+w2*c.y;if(y<=feet.y+.3f)best=std::max(best,y);
                    }
                    return best;
                };
                if(const auto f=g.level.raycast(points[0]+Vec3{0,std::getenv("WALK_Y")?std::strtof(std::getenv("WALK_Y"),nullptr):60.0f,0},{0,-1,0},100)){g.player.position=g.player.previous_position=f->point+Vec3{0,.01f,0};g.player.velocity={};}
                unsigned grounded=0,supported=0,worst_tick=0;float worst=0;
                for(std::size_t leg=1;leg<points.size();++leg)for(unsigned k=0;k<600;++k) {
                    auto d=points[leg]-g.player.position;d.y=0;if(length(d)<.4f)break;
                    keep_alive(g);InputState in;in.move_x=d.x/length(d);in.move_z=d.z/length(d);if(g.dialogue.active())in.dialogue.next=(k%30)==29;
                    g.advance(FixedClock::step,in);
                    if(!g.player.grounded)continue;
                    ++grounded;const float gap=g.player.position.y-drawn_under(g.player.position);
                    if(gap<.3f)++supported;else if(gap>worst){worst=gap;worst_tick=grounded;}
                    if(grounded%30==0)std::printf("  walk %.2f %.2f %.2f drawn gap %.3f\n",g.player.position.x,g.player.position.y,g.player.position.z,gap);
                }
                std::printf("walk: grounded ticks %u, on a drawn face %u, worst gap %.2f (grounded tick %u)\n",grounded,supported,worst,worst_tick);
            }
            return 0;
        }
        if(std::getenv("TRANSLATIONS")) {   // terrain translation groups and morphs: regions, collision triangles, offsets
            for(const auto& t:g.level.terrain_translations) {
                std::printf("translation group %u offset %.2f %.2f %.2f regions",t.group,t.offset.x,t.offset.y,t.offset.z);
                for(const auto r:t.regions)std::printf(" %u",r);
                std::printf(" triangles %zu\n",t.triangles.size());
            }
            for(const auto& m:g.level.terrain_morphs) {
                std::printf("morph group %u region %u collision %d scripted %d frames %zu flags %x node %u frame y:",m.group,m.region,m.collision?1:0,m.scripted?1:0,m.frames.size(),m.flags,m.node);
                for(const auto& f:m.frames){float lo=1e9f,hi=-1e9f;for(const auto& q:f){lo=std::min(lo,q.y);hi=std::max(hi,q.y);}std::printf(" [%.2f..%.2f]",lo,hi);}
                float lo=1e9f,hi=-1e9f;
                if(!m.collision)for(const auto& q:g.level.visual_vertices)if(q.source_region==m.region){lo=std::min(lo,q.position.y);hi=std::max(hi,q.position.y);}
                std::printf(" now [%.2f..%.2f]\n",lo,hi);
            }
            return 0;
        }
        if(const char* near=std::getenv("NEAR")) {   // NEAR=x,y,z,r: objects within r, with class, model, visibility and state
            float x{},y{},z{},r{};std::sscanf(near,"%f,%f,%f,%f",&x,&y,&z,&r);
            for(unsigned i=0;i<g.retail_objects.size();++i) {
                const auto& o=g.retail_objects[i];if(length(o.position-Vec3{x,y,z})>r)continue;
                const auto m=g.overlay_scripts.moby_address(i);
                std::printf("near %u type %x at %.2f %.2f %.2f model %d hidden %d state %d ram %u draw %d drawn %u script %d\n",i,o.type,o.position.x,o.position.y,o.position.z,
                    models.models.contains(o.type)?1:0,o.hidden?1:0,o.state,g.overlay_scripts.peek(m+0x48,1),static_cast<int>(static_cast<std::int8_t>(g.overlay_scripts.peek(m+0x4c,1))),g.overlay_scripts.peek(m+0x4d,1),o.retail_script?1:0);
                if(const auto it=models.models.find(o.type);it!=models.models.end() && !it->second.animations.empty() && !it->second.animations[0].frames.empty()) {
                    Vec3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
                    for(const auto& q:it->second.animations[0].frames[0].vertices){lo=Vec3{std::min(lo.x,q.x),std::min(lo.y,q.y),std::min(lo.z,q.z)};hi=Vec3{std::max(hi.x,q.x),std::max(hi.y,q.y),std::max(hi.z,q.z)};}
                    std::printf("   model clips %zu frames %zu bounds %.2f..%.2f %.2f..%.2f %.2f..%.2f anim clip %u frame %u\n",it->second.animations.size(),it->second.animations[0].frames.size(),lo.x,hi.x,lo.y,hi.y,lo.z,hi.z,o.animation.clip,o.animation.frame);
                }
            }
            return 0;
        }
        if(const char* type=std::getenv("FIND_TYPE")) {   // FIND_TYPE=11a: every object of that class
            const unsigned t=std::strtoul(type,nullptr,16);
            for(unsigned i=0;i<g.retail_objects.size();++i)if(g.retail_objects[i].type==t)std::printf("%u ",i);
            std::printf("\n");return 0;
        }
        if(!goal) {
            const auto exit=std::find_if(g.level.portals.begin(),g.level.portals.end(),[](const auto& p){return p.return_home;});
            float best=1e9f;
            for(unsigned i=0;i<g.retail_objects.size();++i) {
                std::optional<NpcDefinition> npc;try{npc=decode_npc_record(archive,slot,i);}catch(const std::exception&){}
                if(!npc)continue;
                for(const auto& line:npc->lines)if(line.talisman>0 || line.orb_bits) {
                    const float d=exit!=g.level.portals.end()?length(g.retail_objects[i].position-g.retail_objects[exit->object].position):0;
                    if(d<best){best=d;goal=g.retail_objects[i].position;}break;
                }
            }
        }
        if(!goal)throw FormatError("no goal");
        level_walk::Walker w;w.graph=level_walk::build(g.level,w.reach);
        unsigned edges=0,jumps=0;for(const auto& e:w.graph.edges){edges+=static_cast<unsigned>(e.size());for(const auto& x:e)jumps+=x.kind;}
        const Vec3 target=*goal;w.goal_point=target;
        w.goal=[target,radius](const level_walk::Node& n){return std::hypot(n.p.x-target.x,n.p.z-target.z)<radius && std::abs(n.p.y-target.y)<1.5f;};
        std::cout<<"slot "<<slot<<" nodes "<<w.graph.nodes.size()<<" edges "<<edges<<" jump-edges "<<jumps<<" start "<<g.player.position.x<<','<<g.player.position.y<<','<<g.player.position.z
                 <<" goal "<<target.x<<','<<target.y<<','<<target.z<<std::endl;
        std::vector<bool> reachable(w.graph.nodes.size());unsigned reached_nodes=0;
        if(const auto start=w.graph.nearest(g.player.position,.8f,3.0f,3)) {
            std::vector<unsigned> stack{*start};reachable[*start]=true;
            while(!stack.empty()){const auto n=stack.back();stack.pop_back();++reached_nodes;for(const auto& e:w.graph.edges[n])if(!reachable[e.to]){reachable[e.to]=true;stack.push_back(e.to);}}
        }
        std::cout<<"reachable from the start "<<reached_nodes<<std::endl;
        {   // Why the reachable area ends: blocked neighbour steps on its border.
            std::map<std::string,unsigned> reasons;std::map<std::string,Vec3> where;
            for(unsigned a=0;a<w.graph.nodes.size();++a)if(reachable[a]) {
                const auto& na=w.graph.nodes[a];
                for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)if(dx||dz)if(const auto* c=w.graph.column(na.ix+dx,na.iz+dz))for(const auto b:*c) {
                    if(reachable[b])continue;const auto& nb=w.graph.nodes[b];const float dy=nb.p.y-na.p.y;std::string why;
                    if(dy>w.reach.step)why="step "+std::to_string(static_cast<int>(dy*10))+"/10";
                    else if(dy< -1.2f)continue;
                    else if(const auto d=nb.p-na.p;const auto hit=g.level.raycast(na.p+Vec3{0,.35f,0},normalized(d),length(d))) {
                        if(const auto* c2=g.level.object_collider(hit->triangle))why="object "+std::to_string(c2->object)+" type "+std::to_string(g.retail_objects[c2->object].type);
                        else why="terrain";
                    } else why="?";
                    ++reasons[why];where.try_emplace(why,na.p);
                }
            }
            for(const auto& [why,n]:reasons)std::cout<<"  border: "<<why<<" x"<<n<<" e.g. "<<where[why].x<<','<<where[why].y<<','<<where[why].z<<std::endl;
        }
        const bool planned=w.plan(g);
        std::cout<<"planned "<<planned<<" path "<<w.path.size()<<std::endl;
        std::vector<Vec3> trip;bool reached=false;
        for(unsigned t=0;t<limit && planned;++t) {
            keep_alive(g);
            if(g.dialogue.active()){InputState in;in.dialogue.next=(t%30)==0;g.advance(FixedClock::step,in);continue;}
            const auto in=w.step(g);g.advance(FixedClock::step,in);
            if(t%4==0)trip.push_back(g.player.position);
            if(std::hypot(g.player.position.x-target.x,g.player.position.z-target.z)<radius && std::abs(g.player.position.y-target.y)<1.5f){reached=true;break;}
            if(w.path.empty())break;
            if(t%600==0)std::cout<<"  t "<<t<<" at "<<g.player.position.x<<','<<g.player.position.y<<','<<g.player.position.z<<" node "<<w.at<<"/"<<w.path.size()
                                  <<" left "<<w.remaining(g)<<" replans "<<w.replans<<" jumps "<<w.jumps<<" glides "<<w.glides<<std::endl;
        }
        std::cout<<"reached "<<reached<<" ticks "<<w.ticks<<" replans "<<w.replans<<" stuck "<<w.stuck_events<<" jumps "<<w.jumps<<" glides "<<w.glides
                 <<" lives-lost "<<0<<" at "<<g.player.position.x<<','<<g.player.position.y<<','<<g.player.position.z<<"\n"<<w.log<<std::endl;
        if(!map.empty())write_map(map,w.graph,w.path,trip,target,reachable);
        return reached?0:1;
    } catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<"\n";return 1;}
}
