#include "spyro/runtime.hpp"
#include <iostream>
#include <algorithm>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool good,const char* why) {++checks;if(!good)throw std::runtime_error(why);}
void put(Bytes& b,unsigned at,unsigned word) {for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::uint8_t>(word>>(i*8));}
Level floor_level() {
    Level level;level.triangles={{{-100,8,-100},{100,8,100},{100,8,-100},0},{{-100,8,-100},{-100,8,100},{100,8,100},1}};
    for(const auto& t:level.triangles) {level.bounds.add(t.a);level.bounds.add(t.b);level.bounds.add(t.c);}return level;
}
RetailObject container(unsigned type=0xc8) {
    RetailObject object;object.type=static_cast<std::uint16_t>(type);object.id=17;object.held_gem=5;
    object.position=object.previous_position={0,8,0};auto& data=object.container.emplace();
    data.rest_position=object.position;data.wobble[2]=3;data.wobble[3]=-2;
    if(type==0xd1)std::fill_n(data.parameters.begin(),4,255);
    return object;
}
ObjectModels fixture_models() {
    ObjectModels models;CameraSettings camera;models.sine=camera.sine;models.cosine=camera.cosine;
    models.gem_values={1,2,5,10,25};models.gem_meshes={0,1,2,3,4};
    for(unsigned i=0;i<5;++i)models.gem_variants[models.gem_values[i]]=i;
    models.models[1].meshes.resize(5);
    models.models[0xc8].cylinder=ObjectCylinder{672,320,320,1,128,320};
    models.models[0xc9].cylinder=ObjectCylinder{864,256,256,1,128,600};
    models.models[0xd1].cylinder=ObjectCylinder{992,320,320,1,80,640};
    for(auto kind:{0x132,0x133,0x136})models.models[kind].meshes.resize(2);
    for(unsigned i=0;i<8;++i) {
        models.flame.order[i]=models.flame.order[i+8]=static_cast<std::uint8_t>(i);
        models.flame.offsets[i]={0,0,0.2f};
        for(unsigned j=0;j<(i<4?22u:28u);++j)models.flame.curves[i].points.push_back({{0,0,j*0.08f},0});
    }
    for(unsigned kind=0;kind<2;++kind)for(unsigned age=1;age<=(kind?36u:30u);++age) {
        const float head=static_cast<float>(std::min(age+1,kind?29u:22u));
        models.flame.envelopes[kind].push_back({head,std::min(head-2,20.0f),0});
    }
    models.flame.tip_colors={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1},Vec3{1,1,0}};
    models.flame.allowed.fill(1);models.flame.allowed[12]=0;
    return models;
}
}
int main() {
    try {
        Bytes bank(56);put(bank,8,32);put(bank,32,21);put(bank,36,0x01400301);
        put(bank,40,0x01400080);put(bank,44,0x01400080);put(bank,48,320);
        const auto shape=decode_container_collision(bank,false);
        check(shape.broad_radius==672 && shape.bottom==128 && shape.top==320 && shape.radius==320,"Retail cylinder fields / scale");
        put(bank,8,UINT32_MAX);bool rejected=false;try{decode_container_collision(bank,false);}catch(const FormatError&){rejected=true;}
        check(rejected,"Out-of-bank collision pointer accepted");
        auto models=fixture_models();auto basket=container();
        FlamePose flame_pose;flame_pose.base={1,2,3};flame_pose.streams[0].age=1;
        auto visual=flame_geometry(models.flame,flame_pose,models.sine,models.cosine);
        check(visual.streams==1 && visual.tips.size()==12 && visual.trails.empty(),"Flame birth must draw a colored solid tip");
        check(length(visual.tips[0].position-Vec3{1,2,3.16f})<0.00001f &&
            length(visual.tips[2].position-Vec3{1,2+80.0f/1024,3})<0.00001f && visual.tips[2].color.y==1,
            "Flame tip lost its original 80-unit cross section / vertex colors");
        flame_pose.streams[0].age=3;visual=flame_geometry(models.flame,flame_pose,models.sine,models.cosine);
        check(visual.trails.size()==6 && std::abs(visual.trails[0].position.x-(1+94.0f/1024))<0.00001f &&
            std::abs(visual.trails[2].position.x-(1+16.0f/1024))<0.00001f,"Flame ribbon must taper from 94 to 16 retail units");
        check(visual.trails[0].color.x==1 && visual.trails[2].color.x==118.0f/128 &&
            visual.trails[2].v==10.5f/128 && visual.trails[1].u==31.5f/32,"Flame trail texture addressing / GPU color modulation");
        flame_pose.streams[0].age=3.5f;visual=flame_geometry(models.flame,flame_pose,models.sine,models.cosine);
        check(std::abs(visual.tips[0].position.z-3.36f)<0.00001f,"Flame geometry still steps at 30 Hz");
        flame_pose.streams[0]={3,1};visual=flame_geometry(models.flame,flame_pose,models.sine,models.cosine);
        check(visual.tips.empty() && visual.trails.size()==6 && visual.trails[0].v==5.5f/128 &&
            std::abs(visual.trails[0].position.x-(1+16.0f/1024))<0.00001f,"Impact must trim the head, narrow the ribbon and advance its UV fade");
        flame_pose.streams[0].cutoff=0;visual=flame_geometry(models.flame,flame_pose,models.sine,models.cosine);
        check(visual.streams==0 && visual.tips.empty() && visual.trails.empty(),"Clipped flame survived after its tail passed the impact");
        flame_pose={};flame_pose.streams[4].age=24;models.flame.curves[4].prefix={{0,0,-0.08f},0};
        models.flame.envelopes[1][23]={25,24,0};
        visual=flame_geometry(models.flame,flame_pose,models.sine,models.cosine);
        check(visual.trails.size()==72 && std::abs(visual.trails.back().position.z+0.08f)<0.00001f,
            "Long flame envelope must safely preserve its preceding retail record");
        models.flame.envelopes[1][23].span=26;rejected=false;
        try{flame_geometry(models.flame,flame_pose,models.sine,models.cosine);}catch(const FormatError&){rejected=true;}
        check(rejected,"Flame geometry accepted an out-of-bank sample");
        models=fixture_models();
        check(container_contact(basket,models,basket.position+Vec3{419,200,0}/1024,100.0f/1024).has_value() &&
            !container_contact(basket,models,basket.position+Vec3{420,200,0}/1024,100.0f/1024),"Cylinder horizontal exclusive boundary");
        check(container_contact(basket,models,basket.position+Vec3{0,739,0}/1024,100.0f/1024).has_value() &&
            !container_contact(basket,models,basket.position+Vec3{0,740,0}/1024,100.0f/1024),"Rounded cap endpoint / world scale");
        std::vector<RetailObject> objects{basket,container(0xc9),container(0xd1)};
        check(!container_accepts_attack(objects[0],objects,0) && container_accepts_attack(objects[0],objects,flame_attack),"Basket attack mask");
        check(!container_accepts_attack(objects[1],objects,flame_attack) && container_accepts_attack(objects[1],objects,charge_attack),"Vase must resist flame and accept charge");
        check(!container_accepts_attack(objects[2],objects,flame_attack) && container_accepts_attack(objects[2],objects,charge_attack),"Protected bottle attack mask");
        objects[0].id=205;objects[2].container->parameters[0]=205;
        std::fill(objects[2].container->parameters.begin()+1,objects[2].container->parameters.begin()+4,0);
        check(container_accepts_attack(objects[2],objects,flame_attack),"Bottle did not follow its authored object link");
        objects[0].state=1;check(!container_accepts_attack(objects[2],objects,flame_attack),"Bottle link state was ignored");
        auto level=floor_level();level.objects={basket};Scene scene;scene.spawn={0,8,-0.55f};
        Game walking(level,scene,{},&models);InputState walk;walk.move_z=1;
        for(unsigned i=0;i<60;++i)walking.advance(FixedClock::step,walk);
        check(walking.interactions.broken_containers()==0 && walking.player.position.z<-0.6f,"Walking destroyed or passed through a container");
        Game charge(level,scene,{},&models);walk.charge=true;charge.advance(FixedClock::step,walk);
        check(charge.interactions.broken_containers()==1 && charge.interactions.released_gems()==1 && charge.retail_objects[0].state<0,"Charge did not break the authored basket");
        const auto gem_index=charge.retail_objects.size()-1;
        check(charge.retail_objects[gem_index].type==1 && charge.retail_objects[gem_index].state==3 &&
            charge.retail_objects[gem_index].source_id==17 && charge.player.gems==0,"Charge release must fly first and retain the parent ID");
        check(std::count_if(charge.retail_objects.begin(),charge.retail_objects.end(),[](const auto& o){return o.type==0x132;})==12,"Basket did not spawn its 12 retail fragment objects");
        for(unsigned i=0;i<180;++i)charge.advance(FixedClock::step,{});
        check(charge.player.gems==5 && charge.retail_objects[gem_index].collected,"Released gem did not award its actual denomination exactly once");
        check(std::none_of(charge.retail_objects.begin(),charge.retail_objects.end(),[](const auto& o){return is_container_fragment(o.type)&&o.state>=0;}),"Container fragments never expired");
        charge.respawn();check(charge.player.gems==5 && charge.retail_objects[0].state<0 && level.objects[0].state==0,"Respawn repeated a container reward or mutated the authoring scene");
        ObjectInteractions script;objects={container(0xc9)};unsigned total=0;
        ObjectInteractions fire_visual;std::vector<RetailObject> empty;
        fire_visual.start_flame();fire_visual.tick(empty,&models,level,{0,10,0},{},0,{},false,total);
        const auto born=fire_visual.sample_flame(1);
        fire_visual.tick(empty,&models,level,{0,10,0},{},0,{},false,total);
        const auto later=fire_visual.sample_flame(1);
        check(born.streams[0].age==1 && later.streams[0].age==2 && born.streams[1].age==0 && later.streams[1].age==1,
            "Successive flame emissions do not interpolate across both presentation frames");
        fire_visual.reset_transients();visual=flame_geometry(models.flame,fire_visual.sample_flame(1),models.sine,models.cosine);
        check(!fire_visual.flame_active() && visual.streams==0,"Respawn retained stale flame geometry");
        script.attack_sphere(objects,models,objects[0].position+Vec3{0,0.2f,0},0.2f,flame_attack);
        script.tick(objects,&models,level,{5,8,5},{},0,{},false,total);
        check(objects.size()==1 && objects[0].container->wobble_age==1 && objects[0].container->heat==16 && objects[0].material_color==0x34000080,"Flame-resistant vase lost its heat / wobble response");
        script.tick(objects,&models,level,{5,8,5},{},0,{},false,total);script.tick(objects,&models,level,{5,8,5},{},0,{},false,total);
        check(objects[0].rotation[0]==3 && objects[0].rotation[1]==-2 && std::abs(objects[0].position.y-(8+30.0f/1024))<0.0001f,"Vase wobble did not use its authored table");
        for(unsigned i=0;i<70;++i)script.tick(objects,&models,level,{5,8,5},{},0,{},false,total);
        check(objects[0].container->wobble_age==0 && objects[0].container->heat==0 && objects[0].position.y==8,"Vase wobble / heat did not decay");
        objects={basket};ObjectInteractions dropped;
        dropped.attack_sphere(objects,models,basket.position+Vec3{0,0.2f,0},0.2f,flame_attack);
        dropped.tick(objects,&models,level,{5,8,5},{},0,{},false,total);
        const auto dropped_index=objects.size()-1;
        check(objects[dropped_index].state==2 && objects[dropped_index].bounces==3 && objects[dropped_index].script_velocity.z==140,"Flame release lost its launch speed / pickup gate");
        bool bounced=false;for(unsigned i=0;i<240;++i) {
            dropped.tick(objects,&models,level,{5,8,5},{},0,{},false,total);
            bounced|=objects[dropped_index].bounces<3;
        }
        check(bounced && objects[dropped_index].state==1 && std::abs(objects[dropped_index].position.y-8)<0.001f && !objects[dropped_index].collected,
            "Released gem failed to bounce and settle on terrain");
        Scene flame_scene;flame_scene.spawn={0,8,-1};Vec3 reference;
        for(int rate:{30,60,144}) {
            Game fire(level,flame_scene,{},&models);
            unsigned visible=0;
            for(int frame=0;frame<rate*2;++frame) {
                InputState input;input.flame_pressed=frame==0;fire.advance(1.0/rate,input);
                visible+=flame_geometry(models.flame,fire.interactions.sample_flame(fire.clock.alpha()),models.sine,models.cosine).streams>0?1:0;
            }
            check(visible>0 && flame_geometry(models.flame,fire.interactions.sample_flame(1),models.sine,models.cosine).streams==0,
                "Flame presentation failed to emit / expire at a supported display rate");
            check(fire.interactions.broken_containers()==1 && fire.interactions.released_gems()==1,"Retail flame trajectories did not hit a basket");
            const auto position=fire.retail_objects.back().position;
            if(rate==30)reference=position;else check(length(position-reference)<0.00001f,"Container / gem scripts depend on presentation cadence");
        }
        auto wall_level=level;wall_level.triangles.push_back({{-2,8,-0.6f},{2,12,-0.6f},{2,8,-0.6f},2});
        wall_level.triangles.push_back({{-2,8,-0.6f},{-2,12,-0.6f},{2,12,-0.6f},3});
        Game blocked(wall_level,flame_scene,{},&models);
        for(unsigned i=0;i<120;++i) {InputState input;input.flame_pressed=i==0;blocked.advance(FixedClock::step,input);}
        check(blocked.interactions.broken_containers()==0,"Flame travelled through terrain");
        std::cout<<checks<<" interaction checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"Interaction check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
