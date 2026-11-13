// Fitted PC expansion armour. Constructed AFTER all pose/IK/secondary tracks,
// so the shells follow the same interpolated joints as the visible player.
// No animation, collision, limb length or wing geometry is modified.
struct BackrealmsArmorBuilder {
    std::vector<RenderVertex>& out;
    Vec3 origin,eye;float yaw,pitch,roll;
    Vec3 world(Vec3 p)const{return origin+retail::rotate_model(p,yaw,pitch,roll);}
    Vec3 light(Vec3 p,Vec3 normal,unsigned material)const {
        normal=normalized(retail::rotate_model(normal,yaw,pitch,roll));
        const auto view=normalized(eye-world(p));
        if(dot(normal,view)<0)normal=normal*-1;
        const auto key=normalized(Vec3{-.5f,.9f,-.6f});
        const float diffuse=.60f+.46f*std::max(0.0f,dot(normal,key));
        const float spec=.62f*std::pow(std::max(0.0f,dot(normal,normalized(key+view))),18.0f);
        const float rim=.13f*std::pow(1-std::abs(dot(normal,view)),3.0f);
        if(material==2)return Vec3{.13f,.16f,.21f}*(diffuse+.2f*spec);
        if(material==1)return Vec3{1.10f,.76f,.29f}*(diffuse+spec+rim);
        return Vec3{.93f,1.02f,1.14f}*(diffuse+spec+rim);
    }
    void vertex(Vec3 p,Vec3 normal,float u,float v,unsigned material) {
        const auto c=light(p,normal,material);out.push_back({world(p),c.x,c.y,c.z,1,u,v,1});
    }
    void quad(const std::array<Vec3,4>& p,const std::array<Vec3,4>& n,float u0,float u1,float v0,float v1,unsigned material) {
        const unsigned ids[]{0,1,2,2,1,3};const float u[]{u0,u1,u0,u1},v[]{v0,v0,v1,v1};
        for(auto k:ids)vertex(p[k],n[k],u[k],v[k],material);
    }
    void flat_quad(Vec3 a,Vec3 b,Vec3 c,Vec3 d,unsigned material) {
        const auto n=normalized(cross(b-a,c-a));quad({a,b,c,d},{n,n,n,n},0,1,0,1,material);
    }
    void collar(Vec3 a,Vec3 b,Vec3 right,float radius_a,float radius_b,unsigned material) {
        const auto axis=normalized(b-a);right=normalized(right-axis*dot(right,axis));const auto up=cross(axis,right);
        for(unsigned i=0;i<10;++i) {
            const float x=i*6.283185307f/10,y=(i+1)*6.283185307f/10;
            const auto n=right*std::cos(x)+up*std::sin(x),m=right*std::cos(y)+up*std::sin(y);
                quad({a+n*radius_a,a+m*radius_a,b+n*radius_b,b+m*radius_b},{n,m,n,m},i/10.0f,(i+1)/10.0f,0,.5f,material);
        }
    }
    void rivet(Vec3 center,Vec3 normal,Vec3 right,float radius) {
        const auto up=normalized(cross(normal,right));right=normalized(cross(up,normal));
        for(unsigned i=0;i<6;++i) {
            const float a=i*6.283185307f/6,b=(i+1)*6.283185307f/6;
            vertex(center+normal*radius*.45f,normal,.5f,.5f,1);
            vertex(center+(right*std::cos(a)+up*std::sin(a))*radius,normal,.5f+.45f*std::cos(a),.5f+.45f*std::sin(a),1);
            vertex(center+(right*std::cos(b)+up*std::sin(b))*radius,normal,.5f+.45f*std::cos(b),.5f+.45f*std::sin(b),1);
        }
    }
    void horn(std::span<const Vec3> p,bool right_side) {
        const unsigned root[2][4]{{130,117,218,219},{118,119,115,116}};
        const unsigned ring[2][3]{{204,209,210},{211,216,217}};
        const unsigned upper[2][3]{{205,206,207},{212,213,214}};
        const auto side=right_side?1u:0u;
        std::array<Vec3,4> c{};
        for(auto v:root[side])c[0]+=p[v]/4;
        for(auto v:ring[side])c[1]+=p[v]/3;
        for(auto v:upper[side])c[2]+=p[v]/3;
        c[3]=p[right_side?215:208];
        const auto tangent=normalized(c[1]-c[0]);
        const auto basis=normalized(p[root[side][0]]-c[0]-tangent*dot(p[root[side][0]]-c[0],tangent));
        std::array<float,4> r{};
        for(auto v:root[side])r[0]=std::max(r[0],length(p[v]-c[0]-tangent*dot(p[v]-c[0],tangent)));
        for(auto v:ring[side])r[1]=std::max(r[1],length(p[v]-c[1]));
        for(auto v:upper[side])r[2]=std::max(r[2],length(p[v]-c[2]));
        r[0]*=1.035f;r[1]*=1.035f;r[2]*=1.035f;r[3]=.0009f;
        // Close the root collar: deleting the coarse retail horn faces must
        // never leave an open dark ring where the circular sleeve meets skin.
        const auto cap_up=cross(tangent,basis);
        for(unsigned i=0;i<10;++i) {
            const float a=i*6.283185307f/10,b=(i+1)*6.283185307f/10;
            vertex(c[0],tangent*-1,.5f,.5f,1);
            vertex(c[0]+(basis*std::cos(a)+cap_up*std::sin(a))*r[0],tangent*-1,.5f+.45f*std::cos(a),.5f+.45f*std::sin(a),1);
            vertex(c[0]+(basis*std::cos(b)+cap_up*std::sin(b))*r[0],tangent*-1,.5f+.45f*std::cos(b),.5f+.45f*std::sin(b),1);
        }
        const auto sample=[&](float t) {
            const float s=std::clamp(t,0.0f,1.0f)*3;const auto k=std::min(2u,static_cast<unsigned>(s));const float f=s-k;
            const auto a=k?c[k-1]:c[0]*2-c[1],b=c[k],d=c[k+1],e=k<2?c[k+2]:c[3]*2-c[2];
            const auto center=(b*2+(d-a)*f+(a*2-b*5+d*4-e)*(f*f)+(b*3-a-d*3+e)*(f*f*f))*.5f;
            return std::pair{center,std::lerp(r[k],r[k+1],f)};
        };
        // Thin dark parting lines separate six fitted steel sleeves. Raised
        // lips make the segmentation survive gameplay distance and motion.
        constexpr unsigned rings=36,sides=10;
        for(unsigned j=0;j<rings;++j) {
            const float t0=j/float(rings),t1=(j+1)/float(rings);
            auto [a,ra]=sample(t0);auto [b,rb]=sample(t1);
            const auto axis=normalized(b-a),u=normalized(basis-axis*dot(basis,axis)),v=cross(axis,u);
            const unsigned material=j<4?1u:(j%6==4?2u:0u);
            if(j%6==5){ra+=.0025f;rb+=.0010f;}
            for(unsigned i=0;i<sides;++i) {
                const float x=i*6.283185307f/sides,y=(i+1)*6.283185307f/sides;
                const auto n=u*std::cos(x)+v*std::sin(x),m=u*std::cos(y)+v*std::sin(y);
                quad({a+n*ra,a+m*ra,b+n*rb,b+m*rb},{n,m,n,m},i/float(sides),(i+1)/float(sides),t0,t1,material);
            }
        }
        const auto [base,rad]=sample(.06f);const auto axis=normalized(c[1]-c[0]),v=cross(axis,basis);
        for(unsigned i=0;i<5;++i) {
            const float angle=i*6.283185307f/5;const auto normal=basis*std::cos(angle)+v*std::sin(angle);
            rivet(base+normal*(rad+.001f),normal,axis,.0039f);
        }
    }
    void foot(std::span<const Vec3> p,unsigned which) {
        struct Landmarks{unsigned heel_a,heel_b,toe_a,toe_b,ankle_a,ankle_b,joint_a,joint_b;};
        constexpr Landmarks m[]{{40,42,37,38,33,34,31,44},{83,87,85,86,78,79,76,89},
            {2,3,0,1,4,5,112,114},{15,16,18,17,14,19,25,26}};
        const auto& k=m[which];const auto heel=(p[k.heel_a]+p[k.heel_b])*.5f,toe=(p[k.toe_a]+p[k.toe_b])*.5f;
        const auto right=normalized(p[k.toe_b]-p[k.toe_a]);
        const auto forward=normalized(toe-heel-right*dot(toe-heel,right)),up=normalized(cross(forward,right));
        const float width=length(p[k.toe_b]-p[k.toe_a])*.55f,len=length(toe-heel);
        if(width<.001f || len<.001f)return;
        const auto point=[&](float x,float h,float z){return heel+right*x+up*h+forward*z;};
        flat_quad(point(-width,.002f,-len*.12f),point(-width,.002f,len),point(width,.002f,-len*.12f),point(width,.002f,len),2);
        // Three overlapping instep plates, a thin sole, and three distinct
        // articulated toe caps. Soles stay ABOVE the original support plane.
        const float profile_x[]{-1,-1,-.73f,0,.73f,1,1};
        const float profile_y[]{.002f,.016f,.041f,.049f,.041f,.016f,.002f};
        for(unsigned j=0;j<6;++j) {
            const auto a=point(profile_x[j]*width*.72f,profile_y[j],-len*.12f),b=point(profile_x[j+1]*width*.72f,profile_y[j+1],-len*.12f);
            vertex(point(0,.004f,-len*.12f),forward*-1,.5f,.5f,2);
            vertex(a,forward*-1,.1f,.9f,2);vertex(b,forward*-1,.9f,.9f,2);
        }
        for(unsigned band=0;band<3;++band) {
            const float z0=(-.12f+band*.29f)*len,z1=z0+len*.36f;
            const float wa=band==0?.72f:band==1?1.0f:.94f,wb=band==0?1.0f:band==1?.98f:.88f;
            const float height=1-band*.18f;
            for(unsigned j=0;j<6;++j) {
                auto a=point(profile_x[j]*width*wa,profile_y[j]*height,z0),b=point(profile_x[j+1]*width*wa,profile_y[j+1]*height,z0);
                auto c=point(profile_x[j]*width*wb,profile_y[j]*height*.85f,z1),d=point(profile_x[j+1]*width*wb,profile_y[j+1]*height*.85f,z1);
                flat_quad(a,b,c,d,0);
                // A narrow recessed edge and silver rolled lip, rather than
                // disconnected grey triangles on the original purple feet.
                flat_quad(c,d,c+forward*.0025f,d+forward*.0025f,2);
            }
        }
        for(int digit=-1;digit<=1;++digit) {
            const float mid=digit*width*.66f,w=width*.285f;
            const float z0=len*.67f,z1=len*1.055f+(digit==0?.009f:.001f);
            const auto a=point(mid-w,.024f,z0),b=point(mid+w,.024f,z0),c=point(mid-w*.50f,.008f,z1),d=point(mid+w*.50f,.008f,z1);
            flat_quad(a,b,c,d,0);flat_quad(c,d,point(mid-w*.65f,.002f,z1),point(mid+w*.65f,.002f,z1),0);
            flat_quad(point(mid-w,.002f,z0),a,point(mid-w*.65f,.002f,z1),c,2);
            flat_quad(b,point(mid+w,.002f,z0),d,point(mid+w*.65f,.002f,z1),2);
        }
        const auto ankle=(p[k.ankle_a]+p[k.ankle_b])*.5f,joint=(p[k.joint_a]+p[k.joint_b])*.5f;
        const auto axis=normalized(joint-ankle);const float cuff=which<2?.018f:.019f;
        collar(ankle+axis*.004f,ankle+axis*.014f,right,cuff+.003f,cuff+.002f,1);
        rivet(ankle+axis*.009f+forward*(cuff+.004f),forward,right,.0037f);
        rivet(point(-width*.8f,.034f,len*.3f),up,right,.0026f);
        rivet(point(width*.8f,.034f,len*.3f),up,right,.0026f);
    }
};
void backrealms_spyro_armor(std::vector<RenderVertex>& out,const SpyroPose& pose,Vec3 origin,Vec3 eye,float yaw,float pitch,float roll,bool blur) {
    if(pose.vertices.size()<230)return;
    BackrealmsArmorBuilder b{out,origin,eye,yaw,pitch,roll};b.horn(pose.vertices,false);b.horn(pose.vertices,true);
    // The purpose-built supercharge blur already replaces the visible feet.
    if(!blur)for(unsigned i=0;i<4;++i)b.foot(pose.vertices,i);
}
bool backrealms_original_horn(const SpyroFace& face) {
    for(unsigned i=0;i<(face.quad?4u:3u);++i)if(face.vertices[i]>=204 && face.vertices[i]<=217)return true;
    return false;
}
