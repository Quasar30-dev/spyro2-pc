#include "spyro/interface.hpp"
#include "spyro/npc.hpp"
#include "spyro/savegame.hpp"
#include <chrono>
#include <iostream>
#include <set>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw FormatError(why);}
}
int main(int argc,char** argv) {
    try {
        InterfaceAssets font;for(auto& g:font.font)g.descriptor=0x800; // Eight-pixel synthetic glyphs.
        font.font[' '].descriptor=255;font.font['@'].descriptor=255;
        const auto plain=layout_retail_text(font,"ABC");check(plain.width==24 && plain.glyphs.size()==3,"Font width ignores per-character advances");
        const auto color=layout_retail_text(font,"A@4BC");check(color.width==24 && color.glyphs[0].color==0 && color.glyphs[1].color==4,"Colour control consumes width or fails to persist");
        const auto wrap=layout_retail_text(font,"AB CD EF",0,35);check(wrap.lines==3 && wrap.glyphs[2].x==0 && wrap.glyphs[2].y==12,"Word wrapping splits or misplaces a word");
        const auto hard=layout_retail_text(font,"A\\B&C\nD");check(hard.lines==4 && hard.height==45,"Hard paragraph breaks were lost");
        font.escapes[0]='e';font.escapes[1]=static_cast<std::uint8_t>(128+4*2);font.font[4].descriptor=0xe600;
        const auto accent=layout_retail_text(font,"A@eB");check(accent.width==16 && accent.glyphs[1].x==2 && accent.glyphs[1].y==-2,"Combining accent changes advance or baseline");
        check(retail_pc_controls("< > { }")=="Space Shift F H","Retail controls are not mapped to PC inputs");
        check(retail_pc_controls("< > { }",true)=="A X B Y","Retail controls are not mapped to the controller's buttons");
        GuideLevel level{11,400,14,3,"Glimmer"};check(guide_completion(level,0,0,false)==0 && guide_completion(level,1,0,false)==1,"Progress percentage truncates initial progress");
        check(guide_completion(level,400,3,true)==100 && guide_completion(level,400,0,false)==26,"Retail gem/orb/talisman weighting is wrong");
        Level scene_level;scene_level.wad_slot=17;Scene scene;scene.wad_slot=17;Game game(scene_level,scene);
        check(game.award_orb(2) && !game.award_orb(2) && game.player.orbs==1,"Objective reward can be claimed twice");
        check(game.award_talisman() && !game.award_talisman() && game.player.talismans==1,"Talisman reward is not idempotent");
        check(game.award_skill_point(1) && !game.award_skill_point(1),"Skill point can be claimed twice");
        game.complete_flight_task(4,4200);game.complete_flight_task(4,4300);game.guide_awards.boss=true;
        // A script-released extra-life fly has no source animal: saving must
        // skip it rather than write a save that cannot be read back.
        RetailObject fly;fly.type=0xd3;fly.runtime_spawned=true;fly.source_id=UINT32_MAX;fly.butterfly=ButterflyState{Vec3{}};game.retail_objects.push_back(fly);
        GameSave saved;saved.capture(game);
        check(!saved.levels.empty(),"The live scene was not captured");
        for(const auto& [id,level]:saved.levels){(void)id;check(level.butterflies.empty(),"A butterfly without a source animal was saved");}
        const auto path=std::filesystem::temp_directory_path()/("spyro-interface-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s2save");
        saved.write(path);const auto restored=GameSave::read(path);std::filesystem::remove(path);
        Game resumed(scene_level,scene);restored.apply(resumed,nullptr);
        check(resumed.guide_awards.orbs==4 && resumed.guide_awards.talisman && resumed.guide_awards.boss && resumed.skill_points==2 && resumed.guide_awards.flight_tasks==16 && resumed.guide_awards.flight_ticks==4200,"PC save lost guidebook rewards or best flight time");
        check(!resumed.award_orb(2) && resumed.player.orbs==1,"Reloading permits a duplicate orb reward");
        if(argc>1) {
            AssetStore store(argv[1]);WadArchive wad(store);auto exe=store.read_all("SCUS_944.25"),bank=wad.read(17),book=wad.read(4);
            const auto ui=decode_interface(bank,exe,book);
            check(!ui.ripto,"Scene-local Ripto HUD leaked into another level");
            const auto boss_ui=decode_interface(wad.read(71),exe,book,true);
            check(boss_ui.ripto.has_value(),"Retail Ripto HUD bank is absent");
            const auto& boss=*boss_ui.ripto;
            check(boss.orb_rim.width==40 && boss.orb_rim.height==24 && boss.portraits[0].width==48 && boss.portraits[0].height==30,"Ripto HUD uses the wrong sprite dimensions");
            const auto energy=[&](const HudSprite& s,unsigned channel) {
                unsigned total=0;for(unsigned y=0;y<s.height;++y)for(unsigned x=0;x<s.width;++x){const auto p=((s.y+y)*boss_ui.width+s.x+x)*4;if(boss_ui.rgba[p+3])total+=boss_ui.rgba[p+channel];}return total;
            };
            for(unsigned color=0;color<3;++color) {
                check(energy(boss.orbs[color][90],color)>0 && energy(boss.orbs[color][90],(color+1)%3)==0,"Orb HUD did not retain its retail colour modulation");
                check(energy(boss.orbs[color][180],color)>energy(boss.orbs[color][90],color) && energy(boss.orbs[color][90],color)>energy(boss.orbs[color][0],color),"Powered orb pulse clips or fails to brighten");
            }
            for(const auto& portrait:boss.portraits)check(energy(portrait,0)>1000 && portrait.y+portrait.height<boss_ui.height,"Ripto portrait is blank or outside its atlas");
            check(ui.levels[1].name=="Glimmer" && ui.levels[1].gems==400 && ui.levels[1].spirits==14 && ui.levels[1].orbs==3,"Guidebook metadata differs from executable");
            check(ui.levels[22].name=="Mystic Marsh" && ui.flight_labels[0][2]=="Boats","Wrong level/flight name table");
            check(ui.pause[5].width==176 && ui.contents[0].width==160 && ui.pages[1][0].width==192,"Missing original guidebook heading or illustration");
            const auto sprite=[&](const HudSprite& s){if(!s.width)return;check(s.x>0 && s.y>0 && s.x+s.width<ui.width && s.y+s.height<ui.height,"Atlas sprite extends outside its pixels");
                for(unsigned y=0;y<s.height;++y){const auto edge=((s.y+y)*ui.width+s.x)*4,guard=edge-4;check(std::equal(ui.rgba.begin()+edge,ui.rgba.begin()+edge+4,ui.rgba.begin()+guard),"Tile gutter does not duplicate its edge");}
            };
            for(const auto& g:ui.font)for(const auto& s:g.colors)sprite(s);
            for(const auto& s:ui.resident)sprite(s);for(const auto& s:ui.pause)sprite(s);for(const auto& s:ui.contents)sprite(s);for(const auto& page:ui.pages)for(const auto& s:page)sprite(s);
            for(const auto& l:ui.levels)for(unsigned i=0;i<l.orbs;++i)check(l.orb_bits[i]<8,"Guidebook orb order exceeds saved retail flags");
            for(unsigned table=0;table<6;++table)for(const auto& stamp:ui.layouts[table])check(stamp.sprite<(table<2?ui.pause.size():ui.resident.size()),"Original page layout refers to an undecoded border sprite");
            std::set<unsigned> order;for(const auto i:ui.order){check(i<29,"Guidebook order exceeds level list");order.insert(i);}check(order.size()==29,"Guidebook order repeats or drops a level");
            check(ui.dialogue_enter.front()==-416 && ui.dialogue_exit.back()==-422,"Dialogue animation curve decoded from the wrong table");
            const auto flat=project_book_point(ui,500,200,0),edge=project_book_point(ui,500,200,64);
            check(flat[0]==500 && flat[1]==200 && edge[0]==256 && edge[1]>200,"Book page projection does not match retail spine/depth");
            unsigned formatted=0,choices=0;
            for(unsigned slot=15;slot<=93;slot+=2)for(const auto& npc:decode_npcs(wad.read(slot),slot))for(const auto& line:npc.lines) {
                if(line.formatted_text.empty())continue;
                const auto t=layout_retail_text(ui,retail_pc_controls(line.formatted_text),0,434);
                for(const auto& g:t.glyphs)check(g.x>=-15 && g.x<434 && g.y>=-8 && g.color<14,"Retail dialogue escapes or wrapping leave invalid glyph geometry");
                ++formatted;if(line.formatted_text.find('@')!=std::string::npos)check(line.text.find('@')==std::string::npos,"Formatted dialogue replaced the existing plain-text API");
                if(line.yes>=0 && line.no>=0){++choices;check(!line.formatted_answers[0].empty() && !line.formatted_answers[1].empty(),"Original answer text was not retained");}
            }
            check(formatted>400 && choices>20,"Retail dialogue coverage unexpectedly incomplete");
            bool rejected=false;try{decode_interface(bank,exe,std::span(book).first(0xbfff));}catch(const FormatError&){rejected=true;}check(rejected,"Truncated guidebook bank accepted");
            std::cout<<formatted<<" dialogue passages, "<<choices<<" choice passages, 32 book banks\n";
            for(int i=2;i<argc;++i) {
                const auto older=GameSave::read(std::filesystem::u8path(argv[i]));
                const auto upgraded_path=std::filesystem::temp_directory_path()/("spyro-save-upgrade-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s2save");
                older.write(upgraded_path);const auto upgraded=GameSave::read(upgraded_path);std::filesystem::remove(upgraded_path);
                check(upgraded.gems==older.gems && upgraded.purchases==older.purchases && upgraded.levels.size()==older.levels.size() && upgraded.slot==older.slot && upgraded.orbs==older.orbs && upgraded.skill_points==older.skill_points,"Older retail save lost progress when upgraded to format 9");
            }
        }
        std::cout<<checks<<" interface checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
