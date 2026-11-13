    void draw_retail_ripto_hud(const RetailCanvas& c) {
        const auto& boss=game->ripto;
        if(!interface_assets.ripto || !boss.engaged || boss.defeated() || game->scene_locked || game->dialogue.active() || game->portal_travel.active())return;
        const auto& assets=*interface_assets.ripto;
        // Overlay 70, 80070b30. 512x240 positions, original entry curve and
        // the vertical 88-pixel yellow health fill (not a horizontal boss bar).
        const float age=std::max(0.f,float(boss.hud_ticks)-1+float(game->clock.alpha()));
        const auto entry=[&](unsigned t){return t>=48?40.f:float(hud.enter_curve[t/2]);};
        const auto frame=static_cast<unsigned>(age);
        const float slide=std::lerp(entry(frame),entry(frame+1),age-frame);
        c.rect(slide,72,12,92,IM_COL32(8,8,8,128));
        const float fill=std::floor(88*std::clamp(boss.health_fraction(),0.f,1.f));
        c.rect(slide+2,162-fill,8,fill,IM_COL32(192,192,32,255));

        const bool ground=boss.stage==RiptoStage::Foot || boss.stage==RiptoStage::MechaGulp;
        const unsigned portrait=boss.ripto_orbs>=3?5:boss.hud_ripto_blink?
            std::min((boss.hud_ticks/2)%5+1,6-((boss.hud_ticks/2)%5+1)):0;
        c.image(assets.portraits[portrait],32,206-slide);
        if(!ground)return; // Final flight retains Ripto's portrait/health only.

        const unsigned blink=boss.hud_spyro_blink>=6?1:boss.hud_spyro_blink>3?7-boss.hud_spyro_blink:boss.hud_spyro_blink;
        c.resident(31,430,207-slide);
        c.resident(32+blink,430+hud.eye_x[blink],207-slide+hud.eye_y[blink]);

        // 80070ef8/80071194: 9 turns through the 256-entry sine table per
        // 128 original frames. Interpolate its 30 Hz samples for 60 fps.
        const float phase=std::fmod(age*9,256.f);
        const auto n=static_cast<unsigned>(phase);
        const float pulse=std::lerp(float(interface_assets.page_sine[64+n]),
            float(interface_assets.page_sine[64+((n+1)&255)]),phase-n)*45/2048;
        const auto row=[&](float x,unsigned count,unsigned color) {
            const unsigned intensity=count>=3?unsigned(std::clamp(int(std::floor(160+pulse)),70,250)):160;
            for(unsigned i=0;i<3;++i) {
                c.image(assets.orb_rim,x+i*40,236-slide);
                c.image(i<count?assets.orbs[std::min(color,2u)][intensity-70]:assets.empty_orb,x+i*40,236-slide);
            }
        };
        row(40,boss.ripto_orbs,boss.ripto_orb_color());
        row(356,boss.spyro_orbs,boss.player_power);
    }
    // Crush (overlay 30, 8006fe80) and Gulp (overlay 54, 8006f198): the
    // portrait at (38,198) with its eye frame at (38,201), and 7 or 10 health
    // pips from x=40 every 20 at y=236, all lifted by 80052498's curve (40
    // once shown). Pips beyond the displayed health use the empty sprites.
    void draw_retail_boss_hud(const RetailCanvas& c) {
        if(!interface_assets.boss || !game->hud.boss_visible() || game->scene_locked || game->dialogue.active() || game->portal_travel.active())return;
        const auto& assets=*interface_assets.boss;
        const float lift=game->hud.boss_offset(interface_assets,game->clock.alpha());
        c.image(assets.frame,38,198-lift);
        c.image(assets.eyes[std::min(game->hud.boss_eye(),3u)],38,201-lift);
        const unsigned pips=game->level.wad_slot==31?7:10,health=game->hud.boss_counter().value;
        for(unsigned i=0;i<pips;++i) {
            const unsigned part=i==0?0:i+1==pips?2:1;
            c.image(i<health?assets.full[part]:assets.empty[part],40.f+i*20,236-lift);
        }
    }
