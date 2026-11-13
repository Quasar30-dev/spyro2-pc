#include "spyro/savegame.hpp"
#include "spyro/campaign.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace spyro;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void ticks(Game& game,unsigned n,InputState input={}){for(unsigned i=0;i<n;++i)game.advance(FixedClock::step,input);}
Level fixture() {
    Level l;l.wad_slot=17;l.bounds={{-20,0,-20},{20,10,20}};
    l.triangles={{{-20,0,-20},{20,0,20},{20,0,-20}},{{-20,0,-20},{-20,0,20},{20,0,20}}};
    RetailObject npc;npc.type=0x220;npc.position={0,0,1.2f};l.objects={npc};
    NpcDefinition def;def.type=0x220;def.name="Moneybags";def.lines.resize(5);
    auto& offer=def.lines[0];offer.text="Pay ^^^ gems?";offer.yes=1;offer.no=2;offer.purchase=3;offer.insufficient=3;
    def.lines[1].text="Bridge paid";def.lines[1].repeat=4;
    def.lines[2].text="Come back later";def.lines[2].repeat=0;
    def.lines[3].text="Need ^^^ gems";def.lines[3].repeat=0;
    def.lines[4].text="Already open";l.npcs={def};
    l.visual_vertices.push_back({{2,0,0},{1,1,1}});
    TerrainMorph m;m.group=1;m.flags=2;m.remaining=1;m.frames={{{2,0,0}},{{2,1,0}}};m.targets={{0,0}};
    m.steps={{0,2,1,0,0,0,0},{2,2,1,0,0,1,1}};l.terrain_morphs={m};return l;
}
void synthetic() {
    check(!campaign_exit({},14) && !campaign_exit({},73) && !campaign_exit({},95),"Non-campaign bank offered Exit Level");
    bool truncated=false;try{campaign_exit({},17);}catch(const FormatError&){truncated=true;}
    check(truncated,"Truncated destination tables were accepted");
    const auto level=fixture();Scene scene;scene.wad_slot=17;ObjectModels models;models.purchase_prices[3]=100;
    InputState speak,answer,next,cancel;speak.dialogue.interact=true;answer.dialogue.choice=0;next.dialogue.next=true;cancel.dialogue.cancel=true;
    Game poor(level,scene,{},&models);poor.player.gems=99;ticks(poor,2);ticks(poor,1,speak);
    check(poor.dialogue.active() && poor.dialogue.display_text()=="Need 100 gems","Insufficient funds did not select the retail fallback / price");
    ticks(poor,12);ticks(poor,1,answer);check(poor.player.gems==99 && !poor.purchases,"Unaffordable offer deducted gems");
    Game declined(level,scene,{},&models);declined.player.gems=150;ticks(declined,2);ticks(declined,1,speak);ticks(declined,12);
    answer.dialogue.choice=1;ticks(declined,1,answer);
    check(declined.dialogue.line()->text=="Come back later" && declined.player.gems==150 && !declined.purchases,"Declining a purchase changed the wallet or world");
    Game buyer(level,scene,{},&models);buyer.player.gems=150;ticks(buyer,2);ticks(buyer,1,speak);
    check(buyer.dialogue.display_text()=="Pay 100 gems?","Purchase price was not substituted");
    ticks(buyer,1,cancel);check(buyer.player.gems==150 && !buyer.purchases,"Cancelling an offer spent gems");
    ticks(buyer,1,speak);ticks(buyer,12);answer.dialogue.choice=0;ticks(buyer,1,answer);
    check(buyer.player.gems==50 && buyer.purchases==8 && buyer.dialogue.line()->text=="Bridge paid","Accepted purchase did not deduct exactly the authored cost");
    ticks(buyer,20,answer);check(buyer.player.gems==50 && buyer.level.visual_vertices[0].position.y==0,"Held answer charged twice or opened the bridge during speech");
    ticks(buyer,1,next);ticks(buyer,1);
    check(buyer.scene_locked,"Bridge scene did not start after the paid reply");
    const auto start=buyer.player.position;InputState movement;movement.move_z=1;movement.jump_pressed=true;movement.flame_pressed=true;ticks(buyer,20,movement);
    check(length(start-buyer.player.position)<0.001f,"Gameplay input escaped the scripted scene");
    ticks(buyer,90);
    check(!buyer.scene_locked && buyer.level.visual_vertices[0].position.y==1 && level.visual_vertices[0].position.y==0,"Bridge did not finish or changed the source/editor level");
    ticks(buyer,1,speak);check(buyer.dialogue.line() && buyer.dialogue.line()->text=="Already open","Paid service offered to charge again");
    const auto path=std::filesystem::temp_directory_path()/"spyro-campaign-checkpoint.s2save";
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove(p,e);}} cleanup{path};
    GameSave save;save.capture(buyer);save.write(path);Game loaded(level,scene,{},&models);GameSave::read(path).apply(loaded,&models);
    check(loaded.purchases==8 && loaded.player.gems==50 && loaded.level.visual_vertices[0].position.y==1 && !loaded.scene_locked,"Load lost a paid service or replayed its scene");
    GameSave fresh;Game blank(level,scene,{},&models);fresh.capture(blank);fresh.apply(loaded,&models);
    check(!loaded.purchases && loaded.level.visual_vertices[0].position.y==0,"Loading unpaid progress retained the old open bridge");
    auto closed_choice=fixture();closed_choice.npcs[0].lines[0].purchase=-1;closed_choice.npcs[0].lines[0].answer_closes[0]=true;
    Game close(closed_choice,scene,{},&models);ticks(close,2);ticks(close,1,speak);ticks(close,12);ticks(close,1,answer);
    check(!close.dialogue.active(),"Retail high-bit choice target did not close the conversation");
    // Real format-7 layout, before the purchase flag word existed.
    std::ostringstream v7;v7<<0xcdc0917cu<<" 17 10 3 3 0 0 1 0 0 0 0 0 0\n0 1 0 0 0\n1\n17 0 0 0 0 0 0 0\n0\n0\n0\n0\n0\n0\n0 0 0 0 0 0 0 33 0 5952 0 0 0 0 0 0 0\n0\n0 0 0 0 0 0 0 0\n0\n";
    const auto payload=v7.str();{std::ofstream out(path,std::ios::binary);out<<"S2SAVE 7 "<<crc32({reinterpret_cast<const std::uint8_t*>(payload.data()),payload.size()})<<' '<<payload.size()<<'\n'<<payload;}
    const auto old=GameSave::read(path);check(old.gems==10 && !old.purchases && old.levels.at(17).bodies.empty(),"Format-7 save compatibility changed");
}
void retail_checks(const std::filesystem::path& source) {
    AssetStore assets(source);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25");
    const auto archive=wad.read(17);auto level=decode_level(archive,17);const auto models=decode_object_models(archive,exe);initialize_retail_objects(level,models);
    check(models.purchase_prices[3]==100 && models.purchase_prices[4]==500 && models.purchase_prices[6]==1000,"Resident price table is wrong");
    check(level.terrain_morphs.size()==3 && level.bridge_camera && finite((*level.bridge_camera)[0]) && finite((*level.bridge_camera)[1]),"Glimmer bridge tracks / camera missing");
    unsigned near=0,collisions=0;
    for(const auto& morph:level.terrain_morphs) {
        check(morph.frames.size()==7 && morph.steps.size()==70 && morph.steps[59].flags==2,"Bridge timeline does not match retail");
        if(morph.collision)collisions+=static_cast<unsigned>(morph.frames[0].size()/3);else near+=static_cast<unsigned>(morph.frames[0].size());
    }
    check(near==85 && collisions==76,"Bridge near-vertex / collision counts changed");
    const auto money=std::find_if(level.npcs.begin(),level.npcs.end(),[](const auto& npc){return npc.type==0x220;});
    check(money!=level.npcs.end() && money->object==9 && money->lines[0].supported && money->lines[0].purchase==3 && money->lines[0].insufficient==3,"Glimmer Moneybags commands not decoded");
    check(money->lines[5].answers[0]=="Open up!" && money->lines[5].answers[1]=="Maybe later","Authored response labels were replaced by generic Yes/No");
    const auto forest=decode_level(wad.read(15),15);const auto hunter=std::find_if(forest.npcs.begin(),forest.npcs.end(),[](const auto& npc){return npc.type==0x1bc && npc.subtype==6;});
    check(hunter!=forest.npcs.end() && hunter->lines[6].answers[0]=="No" && hunter->lines[6].answers[1]=="Yes","Reversed camera choices were lost");
    Scene scene;scene.wad_slot=17;scene.spawn=level.spawn;Game game(level,scene,{},&models);game.purchases=8;ticks(game,260);
    check(!game.scene_locked && !game.camera.scripted_view,"Bridge cinematic did not release control");
    unsigned changed_vertices=0,changed_triangles=0;
    for(unsigned i=0;i<level.visual_vertices.size();++i)if(length(level.visual_vertices[i].position-game.level.visual_vertices[i].position)>0.01f)++changed_vertices;
    for(unsigned i=0;i<level.triangles.size();++i)if(length(level.triangles[i].a-game.level.triangles[i].a)>0.01f || length(level.triangles[i].b-game.level.triangles[i].b)>0.01f || length(level.triangles[i].c-game.level.triangles[i].c)>0.01f)++changed_triangles;
    check(changed_vertices>100 && changed_triangles>20,"Paid bridge changed its flag without moving its mesh and collision");
    const auto final=game.level;game.restore_scene_progress();
    for(unsigned i=0;i<level.triangles.size();++i)check(length(game.level.triangles[i].a-final.triangles[i].a)<0.00001f,"Restored bridge collision differs from the completed animation");
    std::cout<<"Glimmer bridge: "<<changed_vertices<<" render vertices, "<<changed_triangles<<" collision triangles changed\n";
    unsigned exits=0,doorways=0;
    for(unsigned slot=15;slot<=71;slot+=2) {
        const auto route=campaign_exit(exe,slot);
        if(slot==15 || slot==33 || slot==57){check(!route,"Home world offered Exit Level to itself");continue;}
        check(route.has_value(),"Campaign level has no return destination");++exits;
        const unsigned home=slot<33?15:slot<57?33:57;
        check(route->destination==home,"Retail return table selected the wrong home world");
        const auto home_archive=wad.read(home);const auto destination=decode_level(home_archive,home);
        const auto arrival=campaign_arrival(home_archive,route->source_scene,destination);
        check(finite(arrival.position) && std::isfinite(arrival.yaw),"Return arrival is not finite");
        const auto floor=destination.raycast(arrival.position+Vec3{0,0.1f,0},{0,-1,0},0.2f);
        check(floor.has_value(),"Return arrival has no standing surface");
        if(arrival.doorway){++doorways;check(length(arrival.position-destination.spawn)>1,"Portal arrival used the generic starting point");}
        else check(slot==31 || slot==55 || slot==71,"Ordinary campaign exit has no matching doorway");
    }
    check(exits==26 && doorways==23,"Campaign exit/doorway coverage changed");
    std::cout<<exits<<" campaign exits / "<<doorways<<" authored return doorways verified\n";
}
void trace(const std::filesystem::path& source) {
    AssetStore assets(source);WadArchive wad(assets);auto level=decode_level(wad.read(17),17);
    std::cout<<std::setprecision(9)<<"{\"initial_collision\":[";
    bool comma=false;
    for(const auto& morph:level.terrain_morphs)if(morph.collision)
        for(unsigned i=0;i<morph.frames[0].size()/3;++i) {
            if(comma)std::cout<<',';comma=true;
            const auto& t=level.triangles[morph.first_triangle+i];
            std::cout<<'[';
            for(unsigned j=0;j<3;++j){const auto p=std::array{t.a,t.b,t.c}[j];if(j)std::cout<<',';std::cout<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
            std::cout<<']';
        }
    std::cout<<"]}\n";
    for(auto& morph:level.terrain_morphs)morph.flags&=~3u;
    for(unsigned tick=0;tick<=140;++tick) {
        if(tick)advance_scene_morphs(level);
        std::cout<<"{\"tick\":"<<tick<<",\"near\":[";comma=false;
        for(const auto& morph:level.terrain_morphs)if(!morph.collision) {
            if(comma)std::cout<<',';comma=true;
            std::cout<<'['<<morph.node<<','<<morph.flags<<','<<morph.remaining<<']';
        }
        std::cout<<"]}\n";
    }
}
}
int main(int argc,char** argv){try{if(argc==3 && std::string_view(argv[2])=="--trace"){trace(std::filesystem::u8path(argv[1]));return 0;}synthetic();if(argc>1)retail_checks(std::filesystem::u8path(argv[1]));std::cout<<checks<<" campaign checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"Campaign check "<<checks<<": "<<e.what()<<'\n';return 1;}}
