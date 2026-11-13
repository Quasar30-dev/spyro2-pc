#include "spyro/extensions.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/retail_math.hpp"
#include <numbers>
#include <span>

namespace spyro {
namespace {
struct Limb {
    std::array<unsigned,2> root,joint,ankle,tip,heel;
    std::vector<unsigned> upper,lower,foot;
};
// Anatomical landmarks on the verified 230-vertex main mesh. Shared torso,
// wing, tail and secondary-head vertices are deliberately outside the limbs.
const std::array<Limb,4> limbs{{
    {{30,49},{31,44},{33,34},{37,38},{40,42},{31,44},{32,43},{33,34,35,36,37,38,39,40,41,42}},
    {{75,74},{76,89},{78,79},{85,86},{83,87},{76,89},{77,88},{78,79,80,81,82,83,84,85,86,87}},
    {{112,114},{8,9},{2,3},{0,1},{2,3},{4,5,6,7,8,9,10},{},{0,1,2,3}},
    {{25,26},{23,24},{15,16},{17,18},{15,16},{13,14,19,20,21,22,23,24},{},{15,16,17,18}}
}};
Vec3 rotate_between(Vec3 p,Vec3 from,Vec3 to) {
    from=normalized(from);to=normalized(to);const auto axis=cross(from,to);const float c=dot(from,to);
    if(c< -0.999f) {
        const auto perpendicular=normalized(cross(from,std::abs(from.x)<0.8f?Vec3{1,0,0}:Vec3{0,1,0}));
        return perpendicular*(2*dot(perpendicular,p))-p;
    }
    return p+cross(axis,p)+cross(axis,cross(axis,p))*(1/std::max(0.0001f,1+c));
}
Vec3 midpoint(std::span<const Vec3> vertices,const std::array<unsigned,2>& pair) {return (vertices[pair[0]]+vertices[pair[1]])*0.5f;}
Vec3 pole(unsigned i) {return {i%2?0.3f:-0.3f,i<2?-0.08f:0.06f,-0.14f};}
// Ankle targets are constrained to the two-bone reach, never by scaling the
// leg. Stable outward bend directions stop knees folding through each other.
Vec3 reachable(Vec3 root,Vec3 wanted,float upper,float lower,unsigned i,const CarryGrip* plane=nullptr,float sole=0) {
    const float far=upper+lower-0.00001f,near=std::abs(upper-lower)+0.00001f;
    auto delta=wanted-root;float distance=length(delta);
    auto direction=distance>0.00001f?delta/distance:normalized(pole(i));
    auto goal=root+direction*std::clamp(distance,near,far);
    if(plane) {
        const auto n=plane->normal;const float height=dot(plane->point-root,n)+carry_grip_clearance-sole;
        if(dot(goal-root,n)<height) {
            auto tangent=goal-root-n*dot(goal-root,n);
            const float reach=std::sqrt(std::max(0.0f,far*far-height*height));
            if(length(tangent)>reach)tangent=normalized(tangent)*reach;
            goal=root+n*height+tangent;
        }
    }
    return goal;
}
void bend_limb(SpyroPose& pose,std::span<const Vec3> original,unsigned i,Vec3 goal,Vec3 sole_normal,Vec3 contact_normal) {
    const auto& limb=limbs[i];const auto root=midpoint(original,limb.root),joint=midpoint(original,limb.joint),ankle=midpoint(original,limb.ankle);
    const float upper=length(joint-root),lower=length(ankle-joint);
    const auto delta=goal-root;const float distance=std::max(0.00001f,length(delta));const auto direction=delta/distance;
    const float along=std::clamp((upper*upper-lower*lower+distance*distance)/(2*distance),-upper,upper);
    auto bend=pole(i)-direction*dot(pole(i),direction);
    if(length(bend)<0.0001f)bend=cross(direction,{0,1,0});
    const auto elbow=root+direction*along+normalized(bend)*std::sqrt(std::max(0.0f,upper*upper-along*along));
    for(auto v:limb.upper)pose.vertices[v]=root+rotate_between(original[v]-root,joint-root,elbow-root);
    for(auto v:limb.lower)pose.vertices[v]=elbow+rotate_between(original[v]-joint,ankle-joint,goal-elbow);
    for(auto v:limb.foot)pose.vertices[v]=goal+rotate_between(original[v]-ankle,sole_normal,contact_normal);
}
}
void pose_carry_grip(SpyroPose& pose,const std::array<CarryGrip,4>& targets,float blend) {
    if(pose.vertices.size()!=230)return;
    blend=std::clamp(blend,0.0f,1.0f);const auto original=pose.vertices;
    for(unsigned i=0;i<limbs.size();++i) {
        const auto& limb=limbs[i];const auto& contact=targets[i];
        const auto root=midpoint(original,limb.root),joint=midpoint(original,limb.joint),ankle=midpoint(original,limb.ankle),tip=midpoint(original,limb.tip);
        auto normal=normalized(cross(tip-midpoint(original,limb.heel),original[limb.tip[1]]-original[limb.tip[0]]));
        if(normal.y<0)normal=normal*-1;
        const auto aligned=normalized(lerp(normal,contact.normal,blend));
        const auto rotated_tip=rotate_between(tip-ankle,normal,aligned);
        auto wanted=lerp(tip,contact.point,blend)-rotated_tip;float sole=1e30f;
        for(auto v:limb.foot)sole=std::min(sole,dot(rotate_between(original[v]-ankle,normal,aligned),contact.normal));
        // Constrain every paw vertex, not just the tip midpoint. Since the
        // complete paw lies above a supporting plane, its edges/faces do too.
        wanted+=contact.normal*std::max(0.0f,carry_grip_clearance-dot(wanted-contact.point,contact.normal)-sole);
        const auto goal=reachable(root,wanted,length(joint-root),length(ankle-joint),i,&contact,sole);
        bend_limb(pose,original,i,goal,normal,aligned);
    }
}
void pose_melee(SpyroPose& pose,unsigned combo,float phase,const SpyroModel* model) {
    if(pose.vertices.size()!=230 || phase<=0 || phase>=1)return;
    const auto incoming=pose;
    const auto smooth=[](float t){t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t);};
    const auto track=[&](std::initializer_list<std::pair<float,float>> keys) {
        auto a=keys.begin();for(auto b=a+1;b!=keys.end();a=b++)
            if(phase<=b->first)return std::lerp(a->second,b->second,smooth((phase-a->first)/(b->first-a->first)));
        return a->second;
    };
    const float weight=smooth(phase/0.14f)*(1-smooth((phase-0.70f)/0.30f));
    const bool spin=combo==2;const float side=combo==0?-1.0f:1.0f;
    // The mounted charge posture lowers his neck; the flame head supplies
    // the open jaw. Retain its recovered attachment rather than stretching
    // the face or inventing a replacement mesh.
    if(model && model->animations.contains(12) && model->animations.contains(7)) {
        const auto charge=model->frame(12,float(model->animations.at(12).loop_start));
        if(charge.vertices.size()==230) {
            for(unsigned i=0;i<230;++i)pose.vertices[i]=lerp(pose.vertices[i],charge.vertices[i],0.55f);
            pose.attachment=lerp(pose.attachment,charge.attachment,0.55f);
        }
        const auto head=model->frame(7,3);
        if(head.vertices.size()==230)for(unsigned i=115;i<230;++i)
            pose.vertices[i]=lerp(pose.vertices[i],head.vertices[i]-head.attachment+pose.attachment,0.8f);
    }
    const float wind=track({{0,0},{0.22f,1},{0.36f,0},{1,0}});
    const float strike=track({{0,0},{0.22f,0},{0.36f,1},{0.58f,0.8f},{1,0}});
    const float yaw=spin?track({{0,0},{0.20f,-0.65f},{0.35f,1.45f},{0.52f,4.8f},{0.66f,6.4f},{0.78f,2*std::numbers::pi_v<float>},{1,2*std::numbers::pi_v<float>}}):
        side*track({{0,0},{0.22f,-0.60f},{0.36f,0.22f},{0.54f,0.92f},{0.70f,0.6f},{1,0}});
    const float pitch=spin?track({{0,0},{0.2f,-0.24f},{0.4f,0.10f},{0.64f,-0.18f},{1,0}}):
        -0.18f*wind+(combo==1?0.3f:-0.10f)*strike;
    const float roll=spin?track({{0,0},{0.24f,-0.15f},{0.44f,0.3f},{0.66f,-0.12f},{1,0}}):side*(0.13f*wind-0.2f*strike);
    const float lift=spin?track({{0,0},{0.2f,-0.025f},{0.36f,0.27f},{0.50f,0.23f},{0.70f,0},{0.77f,-0.025f},{1,0}}):
        -0.018f*wind+(combo==1?0.09f:0.035f)*strike;
    const auto body=pose.vertices;
    for(unsigned i=0;i<4;++i) {
        const auto& limb=limbs[i];const auto root=midpoint(body,limb.root),joint=midpoint(body,limb.joint),ankle=midpoint(body,limb.ankle);
        Vec3 wanted=ankle;auto normal=Vec3{0,1,0};
        if(spin)wanted=root+Vec3{i%2?0.10f:-0.10f,-0.055f,i<2?0.09f:-0.06f};
        else if(i==combo) {
            const Vec3 chamber=root+Vec3{side*0.14f,combo==1?-0.07f:0.08f,-0.10f};
            const Vec3 hit=root+Vec3{side*0.025f,combo==1?0.13f:0.01f,0.19f};
            const Vec3 follow=root+Vec3{-side*0.13f,combo==1?0.17f:-0.01f,0.09f};
            if(phase<0.22f)wanted=lerp(ankle,chamber,smooth(phase/0.22f));
            else if(phase<0.36f)wanted=lerp(chamber,hit,smooth((phase-0.22f)/0.14f));
            else if(phase<0.54f)wanted=lerp(hit,follow,smooth((phase-0.36f)/0.18f));
            else wanted=lerp(follow,ankle,smooth((phase-0.54f)/0.46f));
            normal=normalized(Vec3{side*0.3f,0.3f,0.9f});
        } else if(i<2)wanted=root+Vec3{-side*0.04f,-0.085f,0.075f};
        const auto goal=reachable(root,wanted,length(joint-root),length(ankle-joint),i);
        bend_limb(pose,body,i,goal,{0,1,0},normal);
    }
    // Counterbalancing wings are rigid at their roots; three tail joints
    // coil, then whip behind the strike rather than rotating like a prop.
    constexpr std::array<unsigned,8> left{58,59,60,61,62,64,65,66},right{67,68,69,70,63,71,72,73};
    for(unsigned wing=0;wing<2;++wing) {
        const auto& ids=wing?right:left;const auto root=pose.vertices[ids[0]];const float sign=wing?1.0f:-1.0f;
        for(auto v:ids)pose.vertices[v]=root+retail::rotate_model(pose.vertices[v]-root,0,-0.12f*strike,sign*(spin?-0.55f:0.55f)*strike);
    }
    const float tail_turn=spin?0.4f*wind-0.4f*strike:side*(-0.45f*wind+0.55f*strike);
    for(unsigned segment=0;segment<3;++segment) {
        const auto root=pose.vertices[95+segment];const unsigned end=segment==0?112:segment==1?110:108;
        for(unsigned v=96+segment;v<end;++v)pose.vertices[v]=root+retail::rotate_model(pose.vertices[v]-root,tail_turn*0.5f,-0.09f*strike,0);
    }
    const Vec3 pivot{0,-0.18f,-0.09f};
    for(auto& v:pose.vertices)v=pivot+retail::rotate_model(v-pivot,yaw,pitch,roll)+Vec3{0,lift,0};
    // Blend only the entry/exit. Keep the fast middle of the action sharp.
    for(unsigned i=0;i<230;++i)pose.vertices[i]=lerp(incoming.vertices[i],pose.vertices[i],weight);
    pose.attachment=lerp(incoming.attachment,pivot+retail::rotate_model(pose.attachment-pivot,yaw,pitch,roll)+Vec3{0,lift,0},weight);
}
} // namespace spyro
