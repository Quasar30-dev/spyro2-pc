// Provisional YOTD desktop path. Keep S2 scene composition and its donor rules
// unchanged; every source-specific decode below is selected by the CRC profile.
    void load_yotd_level(unsigned slot,bool remember,unsigned area=0) {
        const auto available=mounted_profile->levels();
        if(std::find(available.begin(),available.end(),slot)==available.end())throw FormatError("Unavailable YOTD scene");
        finish_cinematic();if(remember)remember_game();
        const auto archive=wad->read(slot);
        auto next=std::make_unique<Level>(decode_level(archive,slot,*mounted_profile,area));
        next->yotd_script_image=make_yotd_script_image(*wad,executable,slot,area);
        auto next_spyro=std::make_unique<SpyroModel>(decode_spyro(global_archive,archive,executable,{},0,{},{},{},{},area));
        auto next_objects=decode_object_models(archive,executable,area);PowerupAssets effects;
        if(yotd_donor)import_yotd_pc_assets(*next_spyro,next_objects,effects,*yotd_donor);
        if(july_rolls)import_july_rolls(*next_spyro,*july_rolls);
        install_embedded_demons(next_objects);
        initialize_retail_objects(*next,next_objects);
        auto next_hud=yotd_donor?yotd_donor->hud:HudAssets{};
        auto next_interface=yotd_donor?yotd_donor->ui:InterfaceAssets{};
        if(yotd_donor)ice_tile=yotd_donor->ice_tile;
        // The S2 guidebook is not a YOTD campaign directory. Its font and PC
        // menu sprites are useful donor assets; leave campaign entries empty.
        next_interface.levels={};next_interface.order={};next_interface.contents={};next_interface.pages={};
        preview_objects=next->objects;object_clock.reset();object_studio=false;
        Scene scene;scene.retail_crc=mounted_profile->crc;scene.wad_slot=slot;scene.spawn=next->spawn;
        renderer.upload_level(*next);renderer.upload_spyro(*next_spyro);
        renderer.upload_objects(next_objects);renderer.upload_powerups(effects);
        renderer.upload_hud(next_hud);renderer.upload_interface(next_interface);renderer.upload_ice_texture(ice_tile);
        game.reset();level=std::move(next);spyro=std::move(next_spyro);object_models=std::move(next_objects);
        hud=std::move(next_hud);interface_assets=std::move(next_interface);document.replace(scene);upload_element_hud();
        game=std::make_unique<Game>(*level,document.scene,camera_settings,&object_models,spyro.get());
        game->cheats=cheats;game->set_paused(true);playing=false;
        if(campaign_ready && !area)campaign.apply(*game,&object_models);
        game->upgrades=current_campaign().upgrades;game->upgrade_hud_settings=current_campaign().upgrade_hud_settings;
        game->destruction_settings=current_campaign().destruction_settings;
        yotd_areas=level_area_count(archive,*mounted_profile);yotd_selected_area=area;
        selected_slot=static_cast<int>(slot);selected_entity=selected_object=-1;selected_triangle=UINT32_MAX;scene_dirty=false;
        audio.load(mounted_path,executable,archive,slot,{}, {},false,yotd_donor?&yotd_donor->sounds:nullptr,area);
        if(video.active())audio.begin_capture(capture_offline);
        animation=0;animation_seconds=animation_frame=0;upgrade_display.hide();
        dialogue_revision=UINT32_MAX;dialogue_visible=false;dialogue_close_age=24;
        retail_intro_available=retail_outro_available=false;frame_level();
        status=game_level_name(executable,slot)+" - provisional YOTD; "+
            (game->yotd_scripts.active()?"original Sunny Rhynoc scripts; campaign progression inactive.":"native fallback; campaign progression inactive.");
        if(game->yotd_scripts.faults)status+=" VM: "+game->yotd_scripts.fault;
        if(!powerup_warning.empty())status+=" "+powerup_warning;
    }
