#include "spyro/runtime.hpp"
#include "spyro/spyro_model.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <limits>

using namespace spyro;
static unsigned checks=0;
void check(bool good, const char* message) { ++checks; if (!good) throw std::runtime_error(message); }
template<class F> void rejects(F f) { bool threw=false;try {f();}catch(const FormatError&){threw=true;}check(threw,"Expected format rejection"); }
void put(Bytes& b,std::size_t at,std::uint32_t value) { for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::uint8_t>(value>>(8*i)); }
bool near(float a,float b,float eps=0.001f) {return std::abs(a-b)<eps;}
Level floor_level() {
    Level l; l.triangles={{{-500,0,-500},{500,0,500},{500,0,-500},0},{{-500,0,-500},{-500,0,500},{500,0,500},1}};
    for (auto t:l.triangles) {l.bounds.add(t.a);l.bounds.add(t.b);l.bounds.add(t.c);} l.spawn={0,0.01f,0};return l;
}
int main() {
    try {
        Bytes wad(64);put(wad,0,24);put(wad,4,16);put(wad,8,40);put(wad,12,0);put(wad,16,40);put(wad,20,24);
        auto entries=parse_wad(wad,wad.size());check(entries.size()==3 && entries[1].size==0,"Zero-length slot lost");
        auto bad=wad;put(bad,16,36);rejects([&]{parse_wad(bad,bad.size());});
        bad=wad;put(bad,20,100);rejects([&]{parse_wad(bad,bad.size());});
        rejects([&]{read_u32(wad,std::numeric_limits<std::size_t>::max());});
        Bytes object_archive(240);
        for(unsigned i=0;i<4;++i){put(object_archive,i*8,32+i*4);put(object_archive,i*8+4,i==3?196:4);}
        for(unsigned i=0;i<8;++i)put(object_archive,88+i*4,4);
        put(object_archive,120,96);put(object_archive,124,1);put(object_archive,128,172);
        put(object_archive,140,1024);put(object_archive,144,2048);put(object_archive,148,3072);
        put(object_archive,182,1);object_archive[208]=2;object_archive[234]=173;
        auto records=decode_object_instances(object_archive,15);
        check(records.size()==1 && records[0].type==1 && records[0].held_gem==2 &&
            records[0].position.x==1 && records[0].position.y==3 && records[0].position.z==2 &&
            records[0].gem_parameters[18]==173,"Retail object record offsets / coordinate basis changed");
        auto broken_object=object_archive;put(broken_object,124,2);rejects([&]{decode_object_instances(broken_object,15);});
        broken_object=object_archive;put(broken_object,128,196);rejects([&]{decode_object_instances(broken_object,15);});
        broken_object=object_archive;put(broken_object,88,0);rejects([&]{decode_object_instances(broken_object,15);});
        Bytes mob_bank(172);put(mob_bank,0,1);put(mob_bank,52,172);put(mob_bank,56,128);put(mob_bank,60,64);
        mob_bank[64]=2;mob_bank[65]=1;mob_bank[66]=1;mob_bank[68]=3;
        put(mob_bank,76,16);put(mob_bank,80,0);put(mob_bank,84,12);put(mob_bank,96,28);
        put(mob_bank,100,0x82020000);put(mob_bank,104,1u<<9);
        put(mob_bank,108,0x82030003);put(mob_bank,112,3u<<9);
        put(mob_bank,128,8);put(mob_bank,132,0x00010202);put(mob_bank,136,8);put(mob_bank,140,0x00ffffff);
        put(mob_bank,148,1u<<21);put(mob_bank,152,2u<<10);
        put(mob_bank,156,2);put(mob_bank,162,0x04000003);mob_bank[167]=2;
        std::array<std::uint32_t,125> mob_codebook{};
        const auto decode_mob=[&](const Bytes& bytes) {
            ObjectModel model;model.animated=true;model.animation_count=1;decode_object_animations(model,bytes,mob_codebook);return model;
        };
        const auto mob=decode_mob(mob_bank);const auto& mob_frames=mob.animations[0].frames;
        check(mob_frames.size()==2 && mob_frames[0].ticks==2 && mob_frames[1].ticks==4,"Moby 8-byte frame dimensions / timing");
        const auto mv=mob_frames[1].vertices[2]*1024;
        check(near(mv.x,-68) && near(mv.y,2) && near(mv.z,68),"Moby absolute / relative / codebook decode or GTE packing scale changed");
        check(mob.meshes[0].faces[0].vertices==std::array<unsigned,4>{0,1,2,2},"Moby indices must use full bytes");
        const auto half=object_animation_frame(mob,0,0.5f);
        check(near(half.vertices[0].z,32.0f/1024),"Moby frame interpolation");
        auto patched_bank=mob_bank;patched_bank.resize(188);put(patched_bank,108,0x02030003);
        put(patched_bank,172,4);put(patched_bank,176,0x81000004);put(patched_bank,180,0x01000202);
        const auto patched_mob=decode_mob(patched_bank);
        check(patched_mob.meshes.size()==2 && patched_mob.meshes[1].faces[0].vertices[0]==1 &&
            patched_mob.meshes[0].faces[0].vertices[0]==0,"Per-frame face patch mutated another frame");
        auto bad_mob=patched_bank;put(bad_mob,176,0x8100000c);rejects([&]{decode_mob(bad_mob);});
        bad_mob=mob_bank;put(bad_mob,108,0x82010003);rejects([&]{decode_mob(bad_mob);});
        bad_mob=mob_bank;put(bad_mob,132,0x00ff0202);rejects([&]{decode_mob(bad_mob);});
        bad_mob=mob_bank;put(bad_mob,136,1u<<24);rejects([&]{decode_mob(bad_mob);});
        bad_mob=mob_bank;bad_mob[161]=250;rejects([&]{decode_mob(bad_mob);});
        auto mob_state=start_object_animation(mob);for(unsigned i=0;i<6;++i)tick_object_animation(mob,mob_state);
        check(mob_state.frame==0 && mob_state.next_frame==1 && mob_state.blend==0x10,"Moby startup transition timing");
        tick_object_animation(mob,mob_state);tick_object_animation(mob,mob_state);
        check(mob_state.frame==1 && mob_state.next_frame==0 && mob_state.blend==0x30 && (mob_state.flags&2),"Moby loop boundary timing");
        RetailObject scaled_mob;scaled_mob.size=64;
        check(near(object_model_scale(scaled_mob),0.5f),"Original Moby reciprocal instance scale");
        ObjectFace body_face;body_face.flags=0x20;scaled_mob.material_color=0x080000ff;
        const auto tint=animated_object_color(scaled_mob,body_face,{0,0,1});
        check(near(tint.x,0.5f) && near(tint.z,0.5f),"Moby body DPCS tint must preserve palette contribution");
        body_face.quad=true;body_face.animated=true;body_face.flags=0x28;
        check(animated_object_color(scaled_mob,body_face,{0,0,1}).z==1 &&
            object_face_pass(body_face,true,1)==ObjectPass::Hidden,"Quad color bits were interpreted as triangle material / two-sided flags");
        body_face.quad=false;
        check(object_face_pass(body_face,true,1)==ObjectPass::Opaque,"Two-sided animated wing triangles were hidden");
        auto mob_world=floor_level();RetailObject animated_instance;animated_instance.type=42;
        animated_instance.animation=start_object_animation(mob);animated_instance.previous_animation=animated_instance.animation;
        mob_world.objects.push_back(animated_instance);ObjectModels mob_models;mob_models.models.emplace(42,mob);
        auto phased_world=mob_world;phased_world.objects[0].record[62]=1;phased_world.objects[0].record[63]=1;
        initialize_retail_objects(phased_world,mob_models);
        check(phased_world.objects[0].animation.frame==1 && phased_world.objects[0].animation.next_frame==1,
            "Retail authored object animation phases were reset to zero");
        Scene mob_scene;mob_scene.spawn=mob_world.spawn;ObjectAnimationState reference_state;
        for(const int rate:{30,60,144}) {
            Game replay(mob_world,mob_scene,{},&mob_models);
            for(int frame=0;frame<rate*2;++frame)replay.advance(1.0/rate,{});
            if(rate==30)reference_state=replay.retail_objects[0].animation;
            check(reference_state==replay.retail_objects[0].animation,"Moby playback changed with presentation cadence");
            replay.set_paused(true);replay.advance(1,{});
            check(reference_state==replay.retail_objects[0].animation && reference_state==replay.retail_objects[0].previous_animation,
                "Pausing advanced or rewound object animation");
        }
        bad=wad;put(bad,0,0xfffffff8);rejects([&]{parse_wad(bad,bad.size());});
        const std::string crc_text="123456789";
        check(crc32(std::span(reinterpret_cast<const std::uint8_t*>(crc_text.data()),crc_text.size()))==0xcbf43926,"CRC32 mismatch");
        Bytes tri(12);put(tri,0,640|(511u<<14)|(1u<<23));put(tri,4,1280|(2u<<14)|(510u<<23));put(tri,8,192|(3u<<16)|(4u<<24));
        auto t=decode_collision_triangle(tri,9);
        check(near(t.a.x,10) && near(t.a.y,3) && near(t.a.z,20),"Retail coordinate conversion");
        check(near(t.b.x,639.0f/64) && near(t.c.z,1278.0f/64) && t.source_id==9,"Collision signed deltas");
        const auto local=retail::model_position({2048,1024,-356});
        const auto facing_x=retail::rotate_model(local,retail::native_heading(0));
        check(near(facing_x.x,2) && near(facing_x.y,-356.0f/1024) && near(facing_x.z,1),
            "Spyro's local model is mirrored or uses a different world scale");
        const auto facing_y=retail::rotate_model(local,retail::native_heading(1024));
        check(near(facing_y.x,-1) && near(facing_y.z,2),"Retail heading quarter turn reversed");
        check(near(facing_x.y+retail::spyro_origin_height,0),"Retail model origin does not put the reference foot on the floor");
        const auto packed_static=decode_static_vertex(0x00200000);
        check(near(packed_static.x,-2) && near(packed_static.y,2.0f/1024) && near(packed_static.z,2.0f/1024),
            "Static vertex lost overlapping sign bit or the GTE halfword borrow");
        const auto negative_static=decode_static_vertex(0xffffffff);
        check(near(negative_static.x,-2.0f/1024) && near(negative_static.y,4.0f/1024) && near(negative_static.z,-2.0f/1024),
            "Static vertex signed expansion differs from MIPS shift / ADD sequence");
        check(gem_touching_spyro({},Vec3{372,356,372}/1024) && !gem_touching_spyro({},Vec3{373,356,373}/1024),
            "Gem pickup must use retail approximate distance, not a sphere");
        check(gem_touching_spyro({},Vec3{511,511,0}/1024) && !gem_touching_spyro({},Vec3{512,511,0}/1024),
            "Gem horizontal pickup boundary is not 512 units exclusive");
        check(gem_touching_spyro({},Vec3{0,-383,0}/1024) && !gem_touching_spyro({},Vec3{0,-384,0}/1024) &&
            !gem_touching_spyro({},Vec3{0,512,0}/1024),"Gem pickup lost asymmetric vertical bounds");
        check(!object_backface({-1,1,10},{1,1,10},{0,-1,10},{}) &&
            object_backface({-1,1,10},{0,-1,10},{1,1,10},{}),"Shiny gem face orientation reverses GTE NCLIP sign");
        ObjectFace shiny;shiny.shiny=true;shiny.flags=3;
        shiny.quad=true;
        const std::array<Vec3,4> gem_cap{{{-1,1,35},{-1,1,-33},{33,1,16},{33,1,-16}}};
        const auto cap=object_face_view(shiny,gem_cap,{0,10,0});
        check(cap.count==4 && !cap.backface && object_face_view(shiny,gem_cap,{0,-10,0}).backface,
            "Gem cap quads must use the recovered two-NCLIP front-face convention");
        shiny.quad=false;
        check(object_face_pass(shiny,false,3.9f)==ObjectPass::Additive &&
            object_face_pass(shiny,true,3.9f)==ObjectPass::Opaque,"Near gem lost its additive front / opaque back passes");
        check(object_face_pass(shiny,false,4.0f)==ObjectPass::Opaque &&
            object_face_pass(shiny,true,4.0f)==ObjectPass::Hidden,"Shiny object distance threshold differs from half-unit TRZ");
        ObjectModels gem_material;gem_material.shiny_colors[2][0]={8.0f/255,72.0f/255,16.0f/255};
        gem_material.shiny_colors[2][1]={0,1,32.0f/255};RetailObject green;green.material_color=2;
        shiny.shiny_normal={0,0,-128};ObjectBasis identity{{1,0,0},{0,1,0},{0,0,1}};
        const auto lit=shiny_object_color(gem_material,green,shiny,identity,{1,0,0},{0,1,0},{0,0,1},false);
        check(near(lit.y,(72+255*0.375f+32)/255),"Gem light must retain the retail base, specular gain and highlight");
        shiny.shiny_normal={0,0,128};
        const auto away=shiny_object_color(gem_material,green,shiny,identity,{1,0,0},{0,1,0},{0,0,1},false);
        check(near(away.y,0) && near(away.x,8.0f/255),"Signed GTE light gain must be applied before RGB saturation");
        CameraSettings camera_settings;
        const auto y_scale=1/std::tan(camera_settings.vertical_fov()/2),x_scale=y_scale/(4.0f/3);
        check(near(256*(1+0.5f/4*x_scale),298.625f),"Native 4:3 horizontal projection differs from GTE H=341");
        check(near(120*(1-0.25f/4*y_scale),106.6796875f),"Native vertical projection lost the GTE 320/512 correction");
        auto world=floor_level();auto hit=world.raycast({0,5,0},{0,-1,0},10);
        auto gem_world=world;RetailObject loose;loose.type=1;loose.gem_value=2;loose.gem_age=256;loose.state=1;
        RetailObject container=loose;container.type=16;container.held_gem=5;
        gem_world.objects={loose,container};Scene gem_scene;gem_scene.spawn={0,0,0};
        Game gem_game(gem_world,gem_scene);gem_game.advance(FixedClock::step,{});
        check(gem_game.player.gems==2 && gem_game.retail_objects[0].collected && !gem_game.retail_objects[1].collected,
            "Retail pickup collected a container or used its held-gem value");
        gem_game.advance(FixedClock::step,{});gem_game.respawn();
        check(gem_game.player.gems==2 && !gem_world.objects[0].collected,"Retail pickup repeated or mutated authoring data");
        for(int rate:{30,60,144}) {
            Game replay(gem_world,gem_scene);for(int frame=0;frame<rate;++frame)replay.advance(1.0/rate,{});
            check(replay.player.gems==2 && replay.retail_objects[0].collected,"Retail gem collection depends on presentation rate");
        }
        ObjectModels companion_models;companion_models.models.emplace(120,mob);
        companion_models.sine=camera_settings.sine;companion_models.cosine=camera_settings.cosine;
        check(companion_models.math.angle(1,0)==0 && companion_models.math.angle(0,1)==64 &&
            companion_models.math.angle(-1,0)==128 && companion_models.math.angle(0,-1)==192 &&
            companion_models.math.angle(-1,-1)==160,"Retail byte-angle octants / wrapping");
        check(companion_models.math.length({3,4,12})==13 && companion_models.math.length({-3,-4,-12})==13 &&
            companion_models.math.length({30000,40000,0})==50000 && companion_models.math.length({0,0,0})==0,
            "Retail square-root normalization or integer refinement");
        check(retail::approach_angle(254,2,10)==2 && retail::approach_angle(1,250,4)==253,
            "Sparx yaw must take the shortest byte-angle path without overshoot");
        auto companion_world=world;companion_world.spawn_yaw=retail::native_heading(0);
        Scene companion_scene;Game follow_game(companion_world,companion_scene,{},&companion_models);
        const auto sparx_index=*follow_game.sparx.object_index();
        check(follow_game.retail_objects.size()==1 && follow_game.retail_objects[sparx_index].type==120 &&
            retail::from_world(follow_game.retail_objects[sparx_index].position)==retail::IVec3{-1000,0,1356} &&
            follow_game.retail_objects[sparx_index].animation.next_frame==1,"Retail Sparx spawn position / first wing transition");
        follow_game.advance(3*FixedClock::step,{});
        const auto& following=follow_game.retail_objects[sparx_index];
        check(retail::from_world(following.position)==retail::IVec3{-900,-34,1088},
            "Sparx quarter-distance step lost original RNG offsets or truncation toward zero");
        const auto full_step=following.position-following.previous_position;
        check(length(object_position(following,1)-object_position(following,0)-full_step*0.5f)<0.00001f,
            "Sparx transform should interpolate across two native ticks");
        const auto half_position=object_position(following,1);
        follow_game.advance(FixedClock::step,{});
        check(length(object_position(follow_game.retail_objects[sparx_index],0)-half_position)<0.00001f,
            "Sparx rendering jumped at the intermediate 60 Hz tick");
        follow_game.player.health=1;follow_game.advance(FixedClock::step,{});
        check(follow_game.retail_objects[sparx_index].material_color==0x0a009000,"Sparx did not use the retail health tint");
        follow_game.player.health=0;follow_game.advance(FixedClock::step,{});
        check(follow_game.retail_objects[sparx_index].state<0 && !follow_game.sparx.target_id(),"Sparx survived zero health");
        follow_game.respawn();
        check(follow_game.retail_objects.size()==1 && follow_game.player.health==3 && companion_world.objects.empty(),
            "Respawn duplicated Sparx or changed the editor's object records");
        const auto target_test=[&](retail::IVec3 gem_position,bool falling,bool obstructed) {
            auto target_world=companion_world;RetailObject gem=loose;gem.id=17;gem.position=retail::to_world(gem_position);
            std::vector<RetailObject> objects{gem};SparxCompanion companion;
            if(obstructed) {
                target_world.triangles.push_back({{0,0,-4},{0,4,4},{0,0,4},2});
                target_world.triangles.push_back({{0,0,-4},{0,4,-4},{0,4,4},3});
            }
            companion.reset(objects,&companion_models,retail::origin_above_feet,companion_world.spawn_yaw,3);
            companion.tick(objects,&companion_models,target_world,retail::origin_above_feet,companion_world.spawn_yaw,{},false,falling,3);
            return companion.target_id().has_value();
        };
        check(target_test({2061,0,0},false,false) && !target_test({2062,0,0},false,false),"Sparx horizontal pickup boundary");
        check(target_test({1000,0,639},false,false) && !target_test({1000,0,640},false,false) &&
            target_test({1000,0,1000},true,false),"Sparx pickup height must account for Spyro's original origin and falling state");
        check(!target_test({1000,0,0},false,true),"Sparx targeted a gem through terrain");
        auto flight_world=companion_world;RetailObject flying=loose;flying.id=17;flying.state=3;flying.gem_age=0;
        flying.position=flying.gem_flight_origin=retail::to_world({-1500,0,0});flying.gem_spin={-7,1,7};
        flight_world.objects.push_back(flying);Game flight_game(flight_world,companion_scene,{},&companion_models);
        flight_game.advance(FixedClock::step,{});
        check(retail::from_world(flight_game.retail_objects[0].position)==retail::IVec3{-1400,0,101} &&
            flight_game.retail_objects[0].rotation[0]==249 && flight_game.player.gems==0,
            "Gem flight must preserve the original integer path, sine arc and deferred award");
        flight_game.advance(14*FixedClock::step,{});flight_game.advance(14*FixedClock::step,{});
        check(flight_game.retail_objects[0].collected && flight_game.player.gems==2 &&
            flight_game.sparx.delivered_gems()==1,"Flying gem was not awarded at the recovered pickup gate");
        RetailObject distant=loose;distant.id=37;distant.position=retail::to_world({-1500,0,0});
        auto collection_world=companion_world;collection_world.objects={distant};
        Vec3 sparx_end;ObjectAnimationState sparx_animation;
        for(int rate:{30,60,144}) {
            Game collection(collection_world,companion_scene,{},&companion_models);
            bool deferred=false;
            for(int frame=0;frame<rate*4;++frame) {
                collection.advance(1.0/rate,{});
                if(collection.retail_objects[0].state==3 && !collection.retail_objects[0].collected)deferred|=collection.player.gems==0;
            }
            const auto& companion=collection.retail_objects[*collection.sparx.object_index()];
            check(collection.sparx.attracted_gems()==1 && collection.sparx.delivered_gems()==1 &&
                collection.player.gems==2 && deferred,"Sparx approach / flight did not collect exactly once outside direct-touch range");
            check(collection.sparx.script_steps()==120,"Sparx script ran at presentation rate instead of original cadence");
            if(rate==30) {sparx_end=companion.position;sparx_animation=companion.animation;}
            else check(length(companion.position-sparx_end)<0.00001f && companion.animation==sparx_animation,
                "Sparx following or wings depend on presentation cadence");
            collection.set_paused(true);const auto stopped=companion.position;collection.advance(1,{});
            check(length(object_position(companion,0)-stopped)<0.00001f && length(companion.position-stopped)<0.00001f,
                "Pausing rewound or advanced Sparx's transform");
            collection.respawn();
            check(collection.retail_objects.size()==2 && collection.player.gems==2 && collection.retail_objects[0].collected &&
                !collection_world.objects[0].collected,"Sparx collection or respawn mutated source records / duplicated awards");
        }
        check(hit && near(hit->distance,5) && hit->normal.y>0.99f,"Raycast floor");
        check(!world.raycast({0,5,0},{0,1,0},10),"Raycast wrong direction");
        FollowCamera follow(camera_settings);follow.reset(world,{0,0,0},retail::native_heading(0));
        auto camera_view=follow.sample(1);
        check(near(camera_view.eye.x,-2483.0f/1024) && near(camera_view.eye.y,977.0f/1024) && near(camera_view.eye.z,0),
            "Retail follow preset / spherical world offset mismatch");
        check(dot(camera_view.forward,normalized(retail::origin_above_feet-camera_view.eye))>0.9999f,
            "Camera looks at an arbitrary offset instead of the retail origin");
        const int yaw_steps[]={-2045,-2040,-2033},yaw_velocity[]={768,1389,1890};
        for(unsigned i=0;i<3;++i) {
            follow.tick(world,{0,0,0},retail::native_heading(1024),0);
            check(follow.parameters()[0]==yaw_steps[i] && follow.velocity()[0]==yaw_velocity[i],
                "Camera spring diverges from hand-calculated MIPS 60 Hz steps");
        }
        follow.reset(world,{0,0,0},retail::native_heading(0));
        follow.tick(world,{0,0,0},retail::native_heading(0),12);
        check(follow.state()==2 && follow.parameters()[1]==161 && follow.parameters()[2]==2552 && follow.parameters()[3]==-1,
            "Charge camera must approach its recovered radius, elevation and pitch bias");
        auto wall=world;wall.triangles.push_back({{-10,0,-1},{10,10,-1},{10,0,-1},2});
        wall.triangles.push_back({{-10,0,-1},{-10,10,-1},{10,10,-1},3});
        follow.reset(wall,{0,0,0},0);
        for(unsigned i=0;i<30;++i)follow.tick(wall,{0,0,0},0,0);
        check(follow.obstructed() && follow.sample(1).eye.z>-1,"Follow camera passed through a blocking wall");
        check(length(follow.sample(1).eye-retail::origin_above_feet)>1.0f,"Backed against a wall, the camera must rise over Spyro rather than press into him");
        float end_position=0;Vec3 end_camera;
        for (int rate:{30,60,144}) {
            Scene scene;scene.spawn={0,0.01f,0}; Game game(world,scene);
            for(int frame=0;frame<rate*10;++frame) game.advance(1.0/rate,{0,1,false,false,false,false});
            check(game.clock.ticks()==600,"60 Hz simulation depends on render rate");
            check(game.player.grounded && std::abs(game.player.position.y)<0.01f,"Player fell through floor");
            if(rate==30){end_position=game.player.position.z;end_camera=game.camera.sample(1).eye;}
            else {
                check(near(end_position,game.player.position.z,0.00001f),"Movement depends on render cadence");
                check(length(end_camera-game.camera.sample(1).eye)<0.00001f,"Camera follow depends on render cadence");
            }
        }
        Scene scene;scene.spawn={0,0.01f,0};scene.entities.push_back({1,EntityKind::Gem,{0,0.5f,0},5});
        Vec3 relative_end,relative_camera;
        for(int rate:{30,60,144}) {
            Game relative(world,scene);InputState steering;steering.move_x=0.5f;steering.move_z=1;steering.camera_relative=true;
            for(int frame=0;frame<rate*4;++frame)relative.advance(1.0/rate,steering);
            if(rate==30){relative_end=relative.player.position;relative_camera=relative.camera.sample(1).eye;}
            else check(length(relative.player.position-relative_end)<0.00001f &&
                length(relative.camera.sample(1).eye-relative_camera)<0.00001f,
                "Camera-relative steering depends on presentation cadence");
        }
        auto low_ceiling=world;
        low_ceiling.triangles.push_back({{-10,0.8f,-10},{10,0.8f,10},{10,0.8f,-10},2});
        low_ceiling.triangles.push_back({{-10,0.8f,-10},{-10,0.8f,10},{10,0.8f,10},3});
        Game small_passage(low_ceiling,scene);
        for(unsigned i=0;i<30;++i)small_passage.advance(FixedClock::step,{});
        check(small_passage.player.grounded && near(small_passage.player.position.y,0),
            "Spyro's original sphere should fit a passage taller than 712 retail units");
        Game game(world,scene);game.advance(1.0/60,{});check(game.player.gems==5,"Pickup not collected");
        game.advance(1.0/60,{});check(game.player.gems==5 && !scene.entities[0].collected,"Pickup repeated or authoring scene mutated");
        for(unsigned i=0;i<4;++i)game.advance(FixedClock::step,{}); // Settle the 0.01-unit spawn gap under retail gravity.
        game.advance(1.0/240,{0,0,false,true,true,false});check(game.player.grounded,"Partial frame should not tick");
        game.advance(3.0/240,{0,0,false,true,false,false});check(game.player.velocity.y>=game.tuning.jump_speed,"Lost jump edge between simulation ticks");
        const auto before=game.player.position;game.set_paused(true);game.advance(5,{0,1});
        check(length(game.player.position-before)==0,"Pause changed simulation");game.step_once();check(game.player.position.y>before.y,"Single step did not advance");
        FixedClock clock;check(clock.advance(10,[]{})==15,"Long stall catch-up is unbounded");check(clock.dropped_seconds()>9,"Dropped time not reported");
        check(motion_animation(Motion::Run)==2 && motion_animation(Motion::GlideBrake)==24,
            "Full-speed traversal / glide braking selected the wrong retail clip");
        Vec3 super_end;unsigned smoke_count=0;
        for(int rate:{30,60,144}) {
            Scene charge_scene;charge_scene.spawn={0,0,0};Game powered(world,charge_scene);
            powered.cheats.supercharge=true;powered.advance(FixedClock::step,{});
            InputState charge;charge.charge=true;
            for(int frame=0;frame<rate*2;++frame)powered.advance(1.0/rate,charge);
            check(powered.player.motion==Motion::Supercharge && motion_animation(powered.player.motion)==61 && powered.player.position.z>23,
                "Supercharge should propel Spyro without forward input and select the retail mesh");
            const auto smoke=powered.charge_smoke.sample();
            check(!smoke.empty() && powered.charge_smoke.emitted()==60,"Supercharge smoke was not emitted at the original script cadence");
            if(rate==30){super_end=powered.player.position;smoke_count=powered.charge_smoke.emitted();}
            else check(length(powered.player.position-super_end)<0.00001f && powered.charge_smoke.emitted()==smoke_count,
                "Supercharge movement or smoke depends on render cadence");
            powered.set_paused(true);const auto still=powered.charge_smoke.sample(0)[0].position;
            powered.advance(2,charge);check(length(powered.charge_smoke.sample(1)[0].position-still)<0.00001f,"Pause advanced supercharge smoke");
            powered.set_paused(false);charge.jump_pressed=true;powered.advance(FixedClock::step,charge);
            check(powered.player.motion==Motion::SuperchargeJump && motion_animation(powered.player.motion)==25,
                "Supercharge jump did not select the original jump animation");
            powered.advance(FixedClock::step,{});
            check(powered.player.motion!=Motion::Supercharge && powered.player.motion!=Motion::SuperchargeJump,"Releasing charge kept the power-up active");
            for(int i=0;i<40;++i)powered.advance(FixedClock::step,{});
            check(powered.charge_smoke.sample().empty(),"Supercharge smoke did not expire after emission stopped");
            powered.respawn();check(powered.charge_smoke.emitted()==0 && powered.charge_smoke.sample().empty(),"Respawn retained supercharge effects");
        }
        Scene air_scene;air_scene.spawn={0,20,0};Vec3 free_glide_end,braked_glide_end;
        for(int rate:{30,60,144}) {
            Game free_glide(world,air_scene),braked_glide(world,air_scene);
            free_glide.player.velocity.y=braked_glide.player.velocity.y=-1;
            InputState held;InputState braking;braking.move_z=-1;
            for(int frame=0;frame<rate*2;++frame) {
                held.jump_pressed=braking.jump_pressed=frame==0;
                free_glide.advance(1.0/rate,held);braked_glide.advance(1.0/rate,braking);
            }
            check(free_glide.player.motion==Motion::Glide && free_glide.player.position.z>9 &&
                near(free_glide.player.velocity.z,free_glide.tuning.glide_speed),"Gliding still requires forward input");
            check(braked_glide.player.motion==Motion::GlideBrake && braked_glide.player.position.z>0 &&
                braked_glide.player.position.z<free_glide.player.position.z*0.6f && near(braked_glide.player.yaw,0),
                "Down must slow forward glide without turning Spyro around");
            if(rate==30) {free_glide_end=free_glide.player.position;braked_glide_end=braked_glide.player.position;}
            else check(length(free_glide.player.position-free_glide_end)<0.00001f &&
                length(braked_glide.player.position-braked_glide_end)<0.00001f,"Glide movement depends on render cadence");
            for(int frame=0;frame<rate;++frame)braked_glide.advance(1.0/rate,held);
            check(braked_glide.player.motion==Motion::Glide && near(braked_glide.player.velocity.z,braked_glide.tuning.glide_speed),
                "Releasing Down did not resume free forward glide");
            held.move_x=-1;
            for(int frame=0;frame<rate/2;++frame)braked_glide.advance(1.0/rate,held);
            check(braked_glide.player.velocity.x< -1 && braked_glide.player.yaw<0,"Glide steering requires forward input");
        }
        SpyroModel transition_model;transition_model.transitions.fill(1);
        for(unsigned blend=0;blend<256;++blend)transition_model.blends[blend]=static_cast<std::uint8_t>(
            (blend>>4)?std::min(255u,(blend&15)*256/((blend>>4)+1)):(blend&15)*16);
        for(unsigned id:{0u,1u,2u,5u,6u,12u,16u,22u,24u}) {
            SpyroAnimation animation;animation.id=id;animation.loop_start=id==6?4:0;
            animation.loop_end=id==6?8:4;animation.transition_end=id==6?12:4;
            for(unsigned frame=0;frame<(id==6?12u:4u);++frame)
                animation.frames.push_back({{{id*20.0f+frame*4,0,0}},4});
            transition_model.animations.emplace(id,std::move(animation));
        }
        transition_model.transitions[6*62]=5;
        SpyroAnimator animator;animator.reset(&transition_model,0);
        for(unsigned tick=0;tick<2;++tick)animator.tick(0);
        const auto outgoing_pose=animator.sample();animator.tick(2);
        check(animator.state().clip==0 && animator.state().next_clip==2 && animator.state().blend==0x71 &&
            near(animator.sample(0).vertices[0].x,outgoing_pose.vertices[0].x),"Movement transition snapped or lost the previous pose");
        for(unsigned tick=0;tick<7;++tick)animator.tick(2);
        check(animator.state().clip==2 && animator.state().frame==0 && animator.state().next_frame==1 &&
            near(animator.sample(1).vertices[0].x,40),"Retail eight-VBlank transition timing changed");
        animator.tick(16);animator.tick(24);animator.tick(2);
        check(finite(animator.sample(0.5f).vertices[0]) && animator.requested()==2,"Interrupted transitions produced an invalid pose");
        animator.reset(&transition_model,6);for(unsigned tick=0;tick<30;++tick)animator.tick(6);
        animator.tick(0);check(animator.state().transition==4 && animator.state().next_clip==6 && animator.state().next_frame==8,
            "Long-fall landing skipped the retail exit animation");
        bool saw_exit=false;
        for(unsigned tick=0;tick<40;++tick) {animator.tick(0);saw_exit|=animator.state().clip==6 && animator.state().frame>=8;}
        check(saw_exit && animator.state().clip==0 && !animator.state().transition,"Fall exit did not complete into idle");
        animator.reset(&transition_model,6);animator.tick(6);animator.tick(0);
        check(animator.state().next_clip==0 && animator.state().next_frame==1 && !animator.state().transition,
            "Short-fall landing incorrectly used the long-fall exit");
        SpyroAnimationState reference_animation;
        for(int rate:{30,60,144}) {
            animator.reset(&transition_model,0);FixedClock animation_clock;bool poses_finite=true;
            for(int frame=0;frame<rate*4;++frame) {
                animation_clock.advance(1.0/rate,[&] {
                    const unsigned ids[]={0,2,5,16,24,6,0,12};
                    animator.tick(ids[std::min<std::uint64_t>(7,animation_clock.ticks()/30)]);
                });
                poses_finite&=finite(animator.sample(animation_clock.alpha()).vertices[0]);
            }
            check(poses_finite,"Non-finite pose during movement transition replay");
            if(rate==30)reference_animation=animator.state();
            else check(reference_animation==animator.state(),"Animation transitions depend on presentation cadence");
            animator.reset_interpolation();
            check(near(animator.sample(0).vertices[0].x,animator.sample(1).vertices[0].x),"Pausing rewound the player animation");
        }
        SpyroModel model;SpyroAnimation clip;clip.id=0;clip.loop_start=1;clip.loop_end=3;
        clip.frames={{{{0,0,0}},8},{{{8,0,0}},4},{{{16,0,0}},4}};model.animations.emplace(0,clip);
        check(near(model.sample(0,4.0/60).vertices[0].x,4),"Retail keyframe duration ignored");
        check(near(model.sample(0,10.0/60).vertices[0].x,12),"Animation interpolation failed");
        check(model.sample(0,16.0/60).frame==1,"Animation loop replays intro");
        check(near(model.frame(0,1.5f).vertices[0].x,12),"Fractional animation scrub failed");
        clip.id=31;clip.loop_start=clip.loop_end=2;model.animations.emplace(31,clip);
        check(model.sample(31,120).frame==2,"Terminal pose did not hold");
        rejects([&]{model.sample(0,std::numeric_limits<double>::infinity());});
        rejects([&]{model.frame(0,std::numeric_limits<float>::quiet_NaN());});
        EditorDocument editor;editor.scene=scene;auto changed=scene;changed.spawn.x=3;editor.edit(changed);
        check(editor.undo() && editor.scene.spawn.x==0,"Editor undo failed");check(editor.redo() && editor.scene.spawn.x==3,"Editor redo failed");
        auto duplicate=scene;duplicate.entities.push_back(duplicate.entities.front());rejects([&]{duplicate.validate();});
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path=std::filesystem::temp_directory_path()/("spyro_core_"+std::to_string(stamp)+".s2scene");
        scene.save(path);auto loaded=Scene::load(path);check(loaded.entities.size()==1 && loaded.entities[0].value==5,"Scene round trip");
        changed.save(path);check(Scene::load(path).spawn.x==3,"Scene replacement failed");std::filesystem::remove(path);
        std::cout<<"Passed "<<checks<<" core checks\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}
}
