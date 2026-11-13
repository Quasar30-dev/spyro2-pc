#pragma once
// Offline trailer authoring uses camera keys and the real player controller.
struct CaptureShot {
    struct Camera {float tick{};spyro::Vec3 eye,target;};
    struct Action {unsigned begin{},end{};bool charge{},jump{},flame{},headbash{},melee{},carry{},throw_object{},special{},interact{},steer{};float move_x{},move_z{};bool roll_left{},roll_right{};};
    unsigned width=1920,height=1080,frames{},camera_mode{},waypoint=1;
    spyro::Vec3 spawn;float yaw{},fov=50;bool hud=false;
    std::vector<spyro::Vec3> route;
    std::vector<Camera> cameras;
    std::vector<Action> actions;
    static CaptureShot read(const std::filesystem::path& file) {
        std::ifstream in(file);CaptureShot s;std::string magic;unsigned version{},n{};
        const auto require=[](bool yes){if(!yes)throw spyro::FormatError("Invalid S2SHOT cinematic capture file");};
        in>>magic>>version>>s.width>>s.height>>s.frames>>s.spawn.x>>s.spawn.y>>s.spawn.z>>s.yaw>>s.hud>>s.fov>>s.camera_mode;
        require(bool(in) && magic=="S2SHOT" && version>=1 && version<=4 && s.width>=320 && s.width<=3840 && s.height>=240 && s.height<=2160 &&
            !(s.width&1) && !(s.height&1) && s.frames>5 && s.frames<=36000 && spyro::finite(s.spawn) && std::isfinite(s.yaw) &&
            std::isfinite(s.fov) && s.fov>=20 && s.fov<=100 && s.camera_mode<=2);
        in>>n;require(bool(in) && n<=4096);s.route.resize(n);
        for(auto& p:s.route){in>>p.x>>p.y>>p.z;require(bool(in) && spyro::finite(p));}
        in>>n;require(bool(in) && n<=4096);s.actions.resize(n);
        for(auto& a:s.actions){
            in>>a.begin>>a.end>>a.charge>>a.jump>>a.flame;if(version>=2)in>>a.headbash;
            if(version>=3)in>>a.melee>>a.carry>>a.throw_object>>a.special>>a.interact>>a.steer>>a.move_x>>a.move_z;
            if(version>=4)in>>a.roll_left>>a.roll_right;
            require(bool(in) && a.begin<a.end && a.end<=s.frames && std::isfinite(a.move_x) && std::isfinite(a.move_z) && std::abs(a.move_x)<=1 && std::abs(a.move_z)<=1);
        }
        in>>n;require(bool(in) && n<=4096 && (!s.camera_mode || n));s.cameras.resize(n);
        float previous=-1;
        for(auto& k:s.cameras) {
            in>>k.tick>>k.eye.x>>k.eye.y>>k.eye.z>>k.target.x>>k.target.y>>k.target.z;
            require(bool(in) && std::isfinite(k.tick) && k.tick>previous && k.tick<=s.frames && spyro::finite(k.eye) &&
                spyro::finite(k.target) && spyro::length(k.eye-k.target)>0.01f);previous=k.tick;
        }
        in>>std::ws;require(in.eof());return s;
    }
    spyro::InputState input(const spyro::Game& game) {
        spyro::InputState out;const auto tick=game.clock.ticks();
        while(waypoint<route.size()) {
            const auto d=route[waypoint]-game.player.position;
            if(std::hypot(d.x,d.z)<0.55f){++waypoint;continue;}
            const auto direction=spyro::normalized(spyro::Vec3{d.x,0,d.z});
            out.move_x=direction.x;out.move_z=direction.z;break;
        }
        for(const auto& a:actions)if(tick>=a.begin && tick<a.end) {
            out.charge|=a.charge;out.jump_pressed|=a.jump && tick==a.begin;out.jump_held|=a.jump;
            out.flame_pressed|=a.flame && tick==a.begin;
            out.headbash_pressed|=a.headbash && tick==a.begin;
            out.melee_pressed|=a.melee && tick==a.begin;out.carry_pressed|=a.carry && tick==a.begin;
            out.throw_pressed|=a.throw_object && tick==a.begin;out.special_pressed|=a.special && tick==a.begin;
            out.dialogue.interact|=a.interact && tick==a.begin;
            out.roll_left_pressed|=a.roll_left && tick==a.begin;out.roll_left_held|=a.roll_left;
            out.roll_right_pressed|=a.roll_right && tick==a.begin;out.roll_right_held|=a.roll_right;
        }
        if(out.charge && (game.player.motion==spyro::Motion::Charge || game.player.motion==spyro::Motion::ChargeJump ||
            game.player.motion==spyro::Motion::Supercharge || game.player.motion==spyro::Motion::SuperchargeJump)) {
            const float heading=std::atan2(out.move_x,out.move_z);
            out.move_x=waypoint<route.size()?std::clamp(std::remainder(heading-game.player.yaw,6.283185307f)*3,-1.0f,1.0f):0;
            out.move_z=0;
        }
        for(const auto& a:actions)if(tick>=a.begin && tick<a.end && a.steer){out.move_x=a.move_x;out.move_z=a.move_z;}
        return out;
    }
    void view(const spyro::Game& game,spyro::Vec3& eye,spyro::Vec3& target)const {
        if(!camera_mode || cameras.empty())return;
        const auto tick=static_cast<float>(game.clock.ticks())+game.clock.alpha();
        const Camera* a=&cameras.front();const Camera* b=a;
        for(const auto& k:cameras){b=&k;if(k.tick>=tick)break;a=&k;}
        float t=b->tick>a->tick?std::clamp((tick-a->tick)/(b->tick-a->tick),0.0f,1.0f):0;
        t=t*t*(3-2*t);eye=spyro::lerp(a->eye,b->eye,t);target=spyro::lerp(a->target,b->target,t);
        if(camera_mode==2){const auto p=spyro::lerp(game.player.previous_position,game.player.position,game.clock.alpha());eye+=p;target+=p;}
    }
};
