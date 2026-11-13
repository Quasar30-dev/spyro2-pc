// Bounded renderer/phase-transition validation. Damage is injected through
// the normal attack flags; this is a test fixture, not a gameplay recording.
struct RiptoIntegrationCheck {
    unsigned phase{},began{};bool initialized=false;
    // --ripto-reel: two seconds per phase, so one bounded recording covers the
    // Crownworks cutscenes (intro, summon, flight, outro) and phase lighting.
    bool reel=false;
    void tick(Workbench& app) {
        auto& g=*app.game;auto& boss=g.ripto;
        if(!boss.enabled)throw FormatError("Ripto check requires Ripto's Arena or the Backrealms Crownworks encounter");
        const unsigned now=static_cast<unsigned>(g.clock.ticks()),current=static_cast<unsigned>(boss.stage);
        if(!initialized || phase!=current){initialized=true;phase=current;began=now;}
        g.player.invulnerable_ticks=120;
        auto feet=boss.center+Vec3{0,1,-7};
        if(const auto floor=g.level.raycast(feet,{0,-1,0},3))feet=floor->point;
        else throw FormatError("Ripto validation fixture has no standing floor");
        // Repeatedly resetting 0.04 above the floor prevents a single fixed
        // step from reaching support and leaves the fixture in Fall forever.
        g.player.position=g.player.previous_position=feet;g.player.velocity={};
        // Keep this renderer fixture on the arena after victory. Workbench
        // refreshes portal permissions each frame; the core test separately
        // verifies the real return-to-Winter-Tundra transition.
        if(boss.defeated())boss.action_ticks=0;
        if(boss.cinematic!=CrownCinematic::None)began=now;   // each phase's hold starts after its cutscene
        const unsigned hold=reel?120:boss.retail_arena?(boss.stage==RiptoStage::MechaGulp?1900:360):(boss.stage==RiptoStage::Foot?2200:boss.stage==RiptoStage::MechaGulp?4400:1100);
        if(now>began+hold && now%30==0 && boss.stage!=RiptoStage::Defeated && boss.action!=RiptoAction::Transform) {
            boss.damage_cooldown=0;g.retail_objects[boss.actor].attack_flags|=green_bomb_attack;
            if(boss.stage==RiptoStage::MechaGulp && boss.crush<g.retail_objects.size())g.retail_objects[boss.crush].attack_flags|=green_bomb_attack;
        }
    }
};
