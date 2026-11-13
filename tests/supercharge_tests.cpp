#include "spyro/spyro_model.hpp"
#include <iostream>

using namespace spyro;
namespace {
unsigned checks=0;
void check(bool yes,const char* why){++checks;if(!yes)throw FormatError(why);}
bool wing(unsigned i){return i>=58 && i<=73;}
Vec3 relative_to_body(std::span<const Vec3> v,Vec3 p) {
    const auto forward=normalized((v[50]+v[51]+v[52]+v[53])*0.25f-v[95]);
    auto right=v[12]-v[11];right=normalized(right-forward*dot(right,forward));
    return {dot(p,right),dot(p,forward),dot(p,cross(right,forward))};
}
void exact_wings(const SpyroPose& actual,const SpyroPose& charge) {
    const auto neck=[](const auto& v){return (v[50]+v[51]+v[52]+v[53])*.25f;};
    for(unsigned i=58;i<=73;++i) {
        const auto want=relative_to_body(charge.vertices,charge.vertices[i]-neck(charge.vertices));
        const auto got=relative_to_body(actual.vertices,actual.vertices[i]-neck(actual.vertices));
        check(length(want-got)<2e-6f,"Full retail flap differs: root/fingers/membrane or residual random motion");
    }
}
void verify(const SpyroModel& model) {
    constexpr auto random=SuperchargeAppearance::MainModel,flap=SuperchargeAppearance::MainModelChargeWings;
    const auto& old=model.clip(61,random);const auto& added=model.clip(61,flap);
    const auto& original=model.animations.at(61);const auto& charge=model.animations.at(12);
    check(&model.clip(61)==&original,"Default supercharge changed");
    check(&model.clip(12,flap)==&charge,"Wing option changed regular charge");
    check(&model.clip(61,random)==&model.main_model_supercharge,"Old random-wing option was replaced");
    check(added.frames.size()==old.frames.size() && added.loop_start==old.loop_start && added.loop_end==old.loop_end,
        "New variant changed native animation clock");
    check(added.faces.size()==old.faces.size() && added.split_vertex==353 && model.supercharge_blur_vertices==123,
        "New variant lost main-model or leg-blur geometry");
    for(unsigned f=0;f<old.faces.size();++f) {
        const auto& a=added.faces[f];const auto& b=old.faces[f];
        check(a.vertices==b.vertices && a.uv==b.uv && a.clut==b.clut && a.page==b.page && a.atlas_tile==b.atlas_tile &&
              a.quad==b.quad && a.translucent==b.translucent && a.blend_mode==b.blend_mode,"Wing option changed materials/topology");
        for(unsigned i=0;i<4;++i)check(length(a.colors[i]-b.colors[i])==0,"Wing option changed face colours");
    }
    double charge_intro=0,charge_cycle=0,super_cycle=0,elapsed=0;
    for(unsigned i=0;i<charge.loop_start;++i)charge_intro+=charge.frames[i].ticks;
    for(unsigned i=charge.loop_start;i<charge.loop_end;++i)charge_cycle+=charge.frames[i].ticks;
    for(unsigned i=old.loop_start;i<old.loop_end;++i)super_cycle+=old.frames[i].ticks;
    check(old.loop_start==0,"Unmapped supercharge intro in fixture");
    float min_height=10,max_height=-10;unsigned changed_frames=0;
    for(unsigned f=0;f<old.frames.size();++f) {
        const auto& a=added.frames[f];const auto& b=old.frames[f];
        check(a.ticks==b.ticks && a.ticks==original.frames[f].ticks && a.vertices.size()==b.vertices.size() &&
              length(a.attachment-b.attachment)==0,"New wings changed timing/attachment/vertex layout");
        for(unsigned i=0;i<a.vertices.size();++i) {
            check(finite(a.vertices[i]),"Invalid flap vertex");
            if(!wing(i))check(length(a.vertices[i]-b.vertices[i])==0,"Wing variant moved body, neck or leg blur");
        }
        const auto source=model.sample(12,(charge_intro+elapsed*charge_cycle/super_cycle)/60);
        SpyroPose authored;authored.vertices=a.vertices;exact_wings(authored,source);
        for(const auto indices:{std::vector<unsigned>{58,59,60,61,62,64,65,66},std::vector<unsigned>{63,67,68,69,70,71,72,73}})
            for(auto i:indices)for(auto j:indices)
                check(std::abs(length(a.vertices[i]-a.vertices[j])-length(source.vertices[i]-source.vertices[j]))<1e-5f,
                    "Wing variant shrank or stretched Spyro 2's wings");
        for(const auto [root,tip]:{std::pair{58u,62u},std::pair{67u,63u}}) {
            const auto expected=normalized(relative_to_body(source.vertices,source.vertices[tip]-source.vertices[root]));
            const auto actual=normalized(relative_to_body(a.vertices,a.vertices[tip]-a.vertices[root]));
            check(dot(actual,expected)>0.99999f,"Wing stroke differs from mounted regular-charge motion");
            min_height=std::min(min_height,actual.z);max_height=std::max(max_height,actual.z);
        }
        if(length(a.vertices[62]-b.vertices[62])>0.01f)++changed_frames;
        elapsed+=a.ticks;
    }
    check(changed_frames>8 && max_height-min_height>0.7f,"Regular-charge variant has no full wing stroke");
    check(model.charge_smoke.texture_rgba.size()==32*32*4,"Retail smoke missing");
    // Check every membrane vertex between source keyframes, not just the tip
    // directions at the twelve baked supercharge frames (the old blind spot).
    for(unsigned t=0;t<768;++t) {
        const double tick=double(t)/16;
        const auto a=model.sample(61,tick/60,true,flap);
        exact_wings(a,model.sample(12,(charge_intro+tick*charge_cycle/super_cycle)/60));
        const auto random_pose=model.sample(61,tick/60,true,random);
        for(unsigned i=0;i<a.vertices.size();++i)if(!wing(i))
            check(length(a.vertices[i]-random_pose.vertices[i])==0,"Sampling wings modified body/neck/blur");
        const auto studio=model.frame(61,float(tick/4),flap);
        exact_wings(studio,model.sample(12,(charge_intro+tick*charge_cycle/super_cycle)/60));
    }
    SpyroAnimator flap_clock;flap_clock.reset(&model,61);
    for(unsigned tick=0;tick<192;++tick) {
        const auto before=flap_clock.state();flap_clock.tick(61);const auto after=flap_clock.state();
        double from=model.frame_tick(61,before.frame,model.blends[before.blend]/256.0f);
        double to=model.frame_tick(61,after.frame,model.blends[after.blend]/256.0f);if(to<from)to+=super_cycle;
        for(float alpha:{0.0f,.13f,.5f,.93f,1.0f}) {
            const double phase=std::fmod(from+(to-from)*alpha,super_cycle);
            exact_wings(flap_clock.sample(alpha,flap),model.sample(12,(charge_intro+phase*charge_cycle/super_cycle)/60));
        }
    }
    // Exercise the loop seam repeatedly and all entry/exit layout switches at
    // several presentation phases. No extra wing clock may drift or restart.
    for(double phase:{0.0,0.013,0.2,0.399,0.799}) {
        const auto a=model.sample(61,phase,true,flap),b=model.sample(61,phase+super_cycle*10/60,true,flap);
        for(unsigned i=0;i<a.vertices.size();++i)check(length(a.vertices[i]-b.vertices[i])<1e-5f,"Flap phase drifts across loops");
    }
    const auto before=model.sample(61,super_cycle/60-0.00001,true,flap),after=model.sample(61,super_cycle/60+0.00001,true,flap);
    check(length(before.vertices[62]-after.vertices[62])<0.001f,"Flap snaps at the loop seam");
    SpyroAnimator animator;animator.reset(&model);
    SpyroSecondaryAnimator head;head.reset(&model,animator.state());
    for(unsigned requested:{12u,61u,25u,61u,0u,61u,12u}) {
        for(unsigned tick=0;tick<120;++tick) {
            animator.tick(requested);head.tick(0,animator.state(),true);
            for(const auto appearance:{SuperchargeAppearance::Retail,random,flap})for(float alpha:{0.0f,0.5f,1.0f}) {
                const auto pose=model.secondary_track(animator.state().clip,animator.sample(alpha,appearance),head.sample(alpha,appearance),appearance);
                for(const auto p:pose.vertices)check(finite(p),"Variant produced invalid transition/interpolation");
                for(const auto& face:model.clip(animator.state().clip,appearance).faces)for(auto i:face.vertices)
                    check(i<pose.vertices.size(),"Variant sampled with wrong face layout");
            }
        }
        check(animator.state().clip==requested && animator.state().next_clip==requested,"Supercharge transition did not finish");
    }
    std::cout<<"Complete regular-charge membrane/root motion at subframe phases; unchanged body/neck/123 blur vertices; stable loops and transitions\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw FormatError("Usage: spyro_supercharge_tests <ISO|CUE|BIN|folder>");
        AssetStore assets(std::filesystem::u8path(argv[1]));WadArchive wad(assets);
        const auto global=wad.read(0),exe=assets.read_all("SCUS_944.25"),powerup=wad.read(29);
        for(unsigned slot:{17u,29u,39u}) {
            std::cout<<"Scene "<<slot<<": ";verify(decode_spyro(global,wad.read(slot),exe,powerup,29));
        }
        std::cout<<checks<<" supercharge checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
