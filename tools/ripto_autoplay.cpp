// Unassisted Crownworks fight, headless: RiptoAutoplay plays Cyborg Ripto
// through ordinary input on the real runtime (no damage injection, no
// repositioning, no invulnerability), from the arrival to Return Home.
//   spyro_ripto_autoplay retail-path crownworks.s2world [normal|assist|hard] [tick-limit]
#include "spyro/runtime.hpp"
#include "spyro/custom_level.hpp"
#include "spyro/travel.hpp"
#include "spyro/universe.hpp"
#include "spyro/ripto_autoplay.hpp"
#include <fstream>
#include <iostream>
using namespace spyro;
int main(int argc,char** argv){try {
    if(argc<3)throw FormatError("Usage: spyro_ripto_autoplay retail-path crownworks.s2world [normal|assist|hard] [tick-limit]");
    const std::string mode=argc>3?argv[3]:"normal";const unsigned limit=argc>4?static_cast<unsigned>(std::stoul(argv[4])):60*60*20;
    AssetStore assets(argv[1]);WadArchive wad(assets);const auto exe=assets.read_all("SCUS_944.25"),archive=wad.read(17),boss_archive=wad.read(71);
    auto models=decode_object_models(archive,exe);const auto donor=decode_object_models(boss_archive,exe);
    auto source=decode_level(archive,17);decode_travel(archive,exe,source);
    auto level=load_custom_level(argv[2],wad,source,models,decode_return_portal(archive,exe));
    auto retail=decode_level(boss_archive,71);initialize_retail_objects(retail,donor);
    install_ripto_encounter(level,models,retail,donor);
    const auto crush_archive=wad.read(31);auto crush_models=decode_object_models(crush_archive,exe);
    auto crush_level=decode_level(crush_archive,31);initialize_retail_objects(crush_level,crush_models);
    std::ifstream texture_file(std::filesystem::path(argv[2]).parent_path().parent_path().parent_path().parent_path()/"expansions/backrealms/mecha-crush.rgba",std::ios::binary);
    const Bytes texture{std::istreambuf_iterator<char>(texture_file),std::istreambuf_iterator<char>()};
    install_mecha_crush(level,models,crush_level,crush_models,texture);
    const auto spyro=decode_spyro(wad.read(0),archive,exe,wad.read(29),29,wad.read(15),wad.read(45));
    Scene scene;scene.wad_slot=17;scene.spawn=level.spawn;
    Game g(level,scene,{},&models,&spyro);
    // As Play Cyborg Ripto.cmd: PC upgrades on, elements at tier 2 (breath and shot).
    g.upgrades.enabled=true;for(auto& t:g.upgrades.elements.tiers)t=2;
    g.cheats.boss_difficulty=mode=="assist"?BossDifficulty::Assist:mode=="hard"?BossDifficulty::Hard:BossDifficulty::Normal;
    RiptoAutoplay bot;
    RiptoStage stage=g.ripto.stage;unsigned lives=g.player.lives,deaths=0,last_hits=0,t=0;
    for(;t<limit && !(g.ripto.defeated() && g.level.custom_arena->complete);++t) {
        g.advance(FixedClock::step,bot.input(g));
        if(g.player.lives<lives){++deaths;std::cout<<"  t "<<t/60<<" s: lost a life in "<<g.ripto.phase_name()<<" ("<<g.player.lives<<" left)\n";}
        lives=g.player.lives;
        if(g.ripto.stage!=stage){stage=g.ripto.stage;std::cout<<"  t "<<t/60<<" s: "<<g.ripto.phase_name()<<"\n";}
        if(t%1800==0)std::cout<<"  t "<<t/60<<" s: "<<g.ripto.phase_name()<<" hp "<<g.ripto.health<<" crush "<<g.ripto.crush_health<<" action "<<g.ripto.action_name()
            <<" spyro "<<g.player.position.x<<","<<g.player.position.y<<","<<g.player.position.z<<" health "<<g.player.health<<" hits "<<g.ripto.hits-last_hits<<"\n",last_hits=g.ripto.hits;
    }
    const bool won=g.ripto.defeated() && g.level.custom_arena->complete;
    std::cout<<(won?"WON":"NOT WON")<<" after "<<t/60.0f<<" s ("<<mode<<"): deaths "<<deaths<<", boss hits "<<g.ripto.hits<<", phase restarts "<<g.ripto.phase_restarts
             <<", shots "<<bot.shots<<", flames "<<bot.flames<<", jumps "<<bot.jumps<<", orbs sought "<<bot.orbs_sought<<", cutscenes "<<g.ripto.cinematics_played
             <<", fight orbs collected "<<g.ripto.spyro_orbs<<" (Ripto "<<g.ripto.ripto_orbs<<")"<<"\n";
    return won?0:2;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
