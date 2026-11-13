#include "spyro/spyro_model.hpp"
#include <numeric>

namespace spyro {
namespace {
using Landmark=std::pair<unsigned,unsigned>;
// Anatomical correspondences for the verified US meshes. These are vertex
// indices, not game geometry. Main clip 12's loop entry is only a bind pose;
// every output frame is driven by the mounted clip 61, at its original time.
const std::vector<Landmark> torso{{11,147},{12,151},{27,136},{28,148},{29,137},{90,121},
    {49,145},{74,139},{50,140},{51,138},{52,142},{53,144},{54,175},{55,174},
    {56,122},{57,124},{91,146},{92,141},{93,152},{94,172},{95,119}};
const std::array<std::vector<Landmark>,5> parts{{
    {{95,119},{96,159},{97,162},{98,164},{107,168},{108,170},{109,171},{110,160},{111,161}},
    {{58,125},{59,129},{60,123},{61,129},{62,127},{64,126},{65,128},{66,128}},
    {{67,133},{68,135},{69,134},{70,135},{63,131},{71,132},{72,130},{73,130}},
    {{30,149},{31,117},{32,4},{33,5},{34,13},{35,14},{36,7},{37,8},{38,9},{39,12},
     {40,13},{41,6},{42,14},{43,176},{44,176},{45,143},{48,142}},
    {{75,150},{76,177},{77,15},{78,17},{79,16},{80,18},{81,18},{82,116},{83,17},
     {84,116},{85,10},{86,11},{87,115},{88,178},{89,178},{46,143},{47,142}}
}};
int part(unsigned i) {
    if(i>=95 && i<=111)return 0;
    if((i>=58 && i<=62) || (i>=64 && i<=66))return 1;
    if(i==63 || (i>=67 && i<=73))return 2;
    if(i>=30 && i<=48)return 3;
    if(i>=75 && i<=89)return 4;
    return -1;
}
struct Basis {
    Vec3 x,y,z;
    Vec3 apply(Vec3 p) const {return x*p.x+y*p.y+z*p.z;}
    Vec3 inverse(Vec3 p) const {return {dot(p,x),dot(p,y),dot(p,z)};}
};
Basis head_basis(std::span<const Vec3> v,unsigned left_base,unsigned right_base,unsigned left_tip,unsigned right_tip) {
    const auto y=normalized(v[left_tip]+v[right_tip]-v[left_base]-v[right_base]);
    auto x=v[right_base]-v[left_base];x=normalized(x-y*dot(x,y));
    if(length(x)<0.9f || length(y)<0.9f)throw FormatError("Degenerate supercharge head landmarks");
    return {x,y,cross(x,y)};
}
Basis wing_basis(std::span<const Vec3> v,unsigned root,unsigned tip,unsigned membrane) {
    const auto z=normalized(v[tip]-v[root]);
    auto x=v[membrane]-v[root];x=normalized(x-z*dot(x,z));
    if(length(x)<0.9f || length(z)<0.9f)throw FormatError("Degenerate supercharge wing landmarks");
    return {x,cross(z,x),z};
}
Basis body_basis(std::span<const Vec3> v) {
    const auto neck=(v[50]+v[51]+v[52]+v[53])*0.25f;
    const auto y=normalized(neck-v[95]);
    auto x=v[12]-v[11];x=normalized(x-y*dot(x,y));
    if(length(x)<0.9f || length(y)<0.9f)throw FormatError("Degenerate supercharge torso landmarks");
    return {x,y,cross(x,y)};
}
// Small, regularized affine moving-least-squares cage. Weights are computed
// once when loading; the resulting 12 vertex frames have no per-frame solve.
struct Binding {std::vector<Landmark> points;std::vector<double> weights;Vec3 residual;};
Binding bind_vertex(unsigned index,std::span<const Vec3> vertices) {
    Binding result;const int region=part(index);
    if(region>=0)result.points=parts[region];
    for(const auto p:torso)if(std::find(result.points.begin(),result.points.end(),p)==result.points.end())result.points.push_back(p);
    const auto point=vertices[index];
    result.weights.resize(result.points.size());
    for(unsigned i=0;i<result.points.size();++i)if(length(point-vertices[result.points[i].first])<1e-7f) {
        result.weights[i]=1;return result;
    }
    std::vector<double> w(result.points.size());double total=0;Vec3 mean{};
    for(unsigned i=0;i<w.size();++i) {
        const auto d=point-vertices[result.points[i].first];const double radius=dot(d,d)+0.000004;
        total+=w[i]=1/(radius*radius);
    }
    for(unsigned i=0;i<w.size();++i){w[i]/=total;mean+=vertices[result.points[i].first]*static_cast<float>(w[i]);}
    double matrix[3][4]{};
    for(unsigned i=0;i<w.size();++i) {
        const auto delta=vertices[result.points[i].first]-mean;const double d[]{delta.x,delta.y,delta.z};
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)matrix[r][c]+=w[i]*d[r]*d[c];
    }
    const auto offset=point-mean;const double rhs[]{offset.x,offset.y,offset.z};
    for(unsigned r=0;r<3;++r){matrix[r][r]+=0.00001;matrix[r][3]=rhs[r];}
    for(unsigned c=0;c<3;++c) {
        unsigned pivot=c;
        for(unsigned r=c+1;r<3;++r)if(std::abs(matrix[r][c])>std::abs(matrix[pivot][c]))pivot=r;
        for(unsigned j=0;j<4;++j)std::swap(matrix[c][j],matrix[pivot][j]);
        const double divisor=matrix[c][c];if(std::abs(divisor)<1e-12)throw FormatError("Singular supercharge deformation cage");
        for(unsigned j=c;j<4;++j)matrix[c][j]/=divisor;
        for(unsigned r=0;r<3;++r)if(r!=c) {
            const double scale=matrix[r][c];for(unsigned j=c;j<4;++j)matrix[r][j]-=scale*matrix[c][j];
        }
    }
    result.residual=point;
    for(unsigned i=0;i<w.size();++i) {
        const auto p=vertices[result.points[i].first],d=p-mean;
        result.weights[i]=w[i]*(1+d.x*matrix[0][3]+d.y*matrix[1][3]+d.z*matrix[2][3]);
        result.residual=result.residual-p*static_cast<float>(result.weights[i]);
    }
    return result;
}
bool blurred_leg(unsigned i) {return i<11 || (i>=13 && i<=24) || (i>=112 && i<=114);}
}
std::optional<Vec3> retail_supercharge_head_point(const SpyroModel& model,std::span<const Vec3> pose,Vec3 bind_point) {
    const auto main=model.animations.find(12);
    if(main==model.animations.end() || pose.size()!=255 || main->second.loop_start>=main->second.frames.size())return {};
    const auto& bind=main->second.frames[main->second.loop_start].vertices;if(bind.size()!=230)return {};
    try {
        // Same neck-socket head transfer as retarget_supercharge below.
        const auto bind_basis=head_basis(bind,204,211,208,215);const auto bind_origin=(bind[50]+bind[51]+bind[52]+bind[53])*0.25f;
        const auto basis=head_basis(pose,229,230,234,228);const auto origin=(pose[140]+pose[138]+pose[142]+pose[144])*0.25f;
        return origin+basis.apply(bind_basis.inverse(bind_point-bind_origin));
    } catch(const FormatError&) {return {};}
}
void retarget_supercharge(SpyroModel& model) {
    const auto& source=model.animations.at(61);const auto& main=model.animations.at(12);
    // Frame zero is still the upright entry pose. Its raised neck is not a
    // suitable bind for the horizontal charging silhouette.
    const auto& bind=main.frames.at(main.loop_start).vertices;
    const auto& wing_bind=main.frames.at(0).vertices;
    if(bind.size()!=230 || source.frames.size()!=12 || source.frames[0].vertices.size()!=255 || source.split_vertex!=203)
        throw FormatError("Unmapped supercharge retarget mesh layout");
    std::array<Binding,115> bindings;
    for(unsigned i=0;i<bindings.size();++i)bindings[i]=bind_vertex(i,bind);
    const auto bind_basis=head_basis(bind,204,211,208,215);
    const auto bind_origin=(bind[50]+bind[51]+bind[52]+bind[53])*0.25f;
    const std::array<Basis,2> wing_bases{wing_basis(wing_bind,58,62,60),wing_basis(wing_bind,67,63,69)};
    // The component containing vertex 4 includes the solid torso/head/front
    // legs. Thirteen disconnected strips form the 123-vertex rear-leg blur.
    std::array<bool,255> body{};body[4]=true;bool changed;
    do {
        changed=false;
        for(const auto& face:source.faces)if(std::any_of(face.vertices.begin(),face.vertices.end(),[&](auto v){return body.at(v);}))
            for(auto v:face.vertices)if(!body[v]){body[v]=true;changed=true;}
    } while(changed);
    std::vector<unsigned> blur;std::array<unsigned,255> remap{};
    for(unsigned i=0;i<body.size();++i)if(!body[i]){remap[i]=230+static_cast<unsigned>(blur.size());blur.push_back(i);}
    if(blur.size()!=123)throw FormatError("Unmapped supercharge leg-blur topology");
    auto result=source;result.frames.clear();result.faces.clear();result.split_vertex=230+static_cast<unsigned>(blur.size());
    for(const auto& face:main.faces)if(std::none_of(face.vertices.begin(),face.vertices.end(),blurred_leg))result.faces.push_back(face);
    unsigned blur_faces=0;
    for(auto face:source.faces)if(!body[face.vertices[0]]) {
        for(auto& v:face.vertices)v=static_cast<std::uint16_t>(remap[v]);
        result.faces.push_back(face);++blur_faces;
    }
    for(const auto& frame:source.frames) {
        if(frame.vertices.size()!=255)throw FormatError("Supercharge frame layout changed");
        const auto& v=frame.vertices;const auto basis=head_basis(v,229,230,234,228);
        // Anchor at the neck socket, not at the old model's horns. This
        // transfers rotation without forcing the newer neck to its length.
        const auto origin=(v[140]+v[138]+v[142]+v[144])*0.25f;
        const auto head=[&](Vec3 p){return origin+basis.apply(bind_basis.inverse(p-bind_origin));};
        const std::array<Basis,2> wings{wing_basis(v,125,127,123),wing_basis(v,133,131,134)};
        SpyroFrame out;out.ticks=frame.ticks;out.attachment=frame.attachment;out.vertices.reserve(result.split_vertex);
        for(unsigned i=0;i<bind.size();++i) {
            Vec3 p;
            const int region=part(i);
            if(region==1 || region==2) {
                const unsigned side=static_cast<unsigned>(region-1),root=side?67:58,source_root=side?133:125;
                // A rigid wing transform preserves all of Spyro 2's membrane
                // lengths. Absolute vertex matching shrank them to Spyro 1.
                p=v[source_root]+wings[side].apply(wing_bases[side].inverse(wing_bind[i]-wing_bind[root]));
            } else if(i>=115)p=head(bind[i]);
            else {
                const auto& binding=bindings[i];p=binding.residual;
                for(unsigned j=0;j<binding.points.size();++j) {
                    const auto [target,source_index]=binding.points[j];
                    const auto q=target>=50 && target<=53?head(bind[target]):v[source_index];
                    p+=q*static_cast<float>(binding.weights[j]);
                }
            }
            if(!finite(p) || length(p)>4)throw FormatError("Invalid retargeted supercharge vertex");
            out.vertices.push_back(p);
        }
        for(auto i:blur)out.vertices.push_back(v[i]);
        result.frames.push_back(std::move(out));
    }
    model.main_model_supercharge=std::move(result);
    model.main_model_supercharge_charge_wings=model.main_model_supercharge;
    unsigned elapsed=0;
    for(auto& frame:model.main_model_supercharge_charge_wings.frames) {
        SpyroPose pose;pose.vertices=frame.vertices;model.charge_wings(pose,elapsed);
        frame.vertices=std::move(pose.vertices);elapsed+=frame.ticks;
    }
    model.supercharge_blur_vertices=static_cast<unsigned>(blur.size());model.supercharge_blur_faces=blur_faces;
}
double SpyroModel::frame_tick(unsigned animation,unsigned frame,float blend) const {
    const auto& clip=animations.at(animation);double tick=0;
    for(unsigned i=0;i<frame;++i)tick+=clip.frames.at(i).ticks;
    return tick+clip.frames.at(frame).ticks*blend;
}
void SpyroModel::charge_wings(SpyroPose& pose,double tick) const {
    const auto& source=animations.at(61);const auto& charge=animations.at(12);
    const auto duration=[](const SpyroAnimation& clip,unsigned first,unsigned last) {
        unsigned total=0;for(unsigned f=first;f<last;++f)total+=clip.frames.at(f).ticks;return total;
    };
    const auto intro=duration(source,0,source.loop_start),cycle=duration(source,source.loop_start,source.loop_end);
    const auto charge_intro=duration(charge,0,charge.loop_start),charge_cycle=duration(charge,charge.loop_start,charge.loop_end);
    if(!cycle || !charge_cycle || pose.vertices.size()!=353)throw FormatError("Invalid charge-wing retarget input");
    const double phase=tick<intro?tick*charge_intro/intro:charge_intro+std::fmod(tick-intro,double(cycle))*charge_cycle/cycle;
    const auto flap=sample(12,phase/60.0);
    const auto from=body_basis(flap.vertices),to=body_basis(pose.vertices);
    // The neck socket belongs to the torso, unlike the old clip's wing roots.
    // Transfer the entire authored wing (root, fingers and membrane), preserving
    // the retail folding/deformation instead of rotating a rigid frame-zero wing.
    const auto neck=[](std::span<const Vec3> v){return (v[50]+v[51]+v[52]+v[53])*.25f;};
    const auto from_origin=neck(flap.vertices),to_origin=neck(pose.vertices);
    for(unsigned i=58;i<=73;++i) {
        pose.vertices[i]=to_origin+to.apply(from.inverse(flap.vertices[i]-from_origin));
        if(!finite(pose.vertices[i]))throw FormatError("Invalid regular-charge wing vertex");
    }
}
} // namespace spyro
