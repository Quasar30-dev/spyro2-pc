    void finish_cinematic() {
        cinematic_playing=false;cinematic_audio.update(nullptr,false);
        if(game){game->set_paused(!playing || menu!=Menu::None);game->camera.reset_interpolation();}
        focus_play_viewport=playing;
    }
    void start_cinematic(bool outro=false) {
        if(!wad || !level || level->universe)return;
        const auto slot=cinematic_slot(executable,level->wad_slot,outro);if(!slot)return;
        start_cinematic_slots(level->wad_slot,*slot);
    }
    // 80071bd4 on leaving a level after its reward: the level's outro
    // (80073e48(1)), except Glimmer before Summer Forest was visited, which plays
    // story cutscene 4 (80019230(4): level id 8006323c[4] = 72, scene WAD 77,
    // movie 96 + 80064914[31] = WAD 142).
    void start_exit_cinematic() {
        if(level && game && level->wad_slot==17 && !(game->visited_levels&1u)){start_cinematic_slots(77,142);return;}
        start_cinematic(true);
    }
    void start_cinematic_slots(unsigned scene_slot,unsigned movie_slot) {
        const auto slot=std::optional<unsigned>(movie_slot);
        auto next=std::make_unique<CinematicAssets>(decode_cinematic(wad->read(scene_slot),wad->read(*slot),executable,scene_slot));
        cinematic_renderer.upload_level(next->level);cinematic_renderer.upload_objects(next->models);
        title_empty_spyro.animations.try_emplace(0);
        cinematic_audio.load_cinematic(next->sound);
        if(video.active())cinematic_audio.begin_capture(capture_offline);
        cinematic=std::move(next);cinematic_ticks=0;cinematic_movie=*slot;cinematic_playing=true;
        if(game){game->dialogue.dismiss(game->retail_objects);game->set_paused(true);}
        audio.update(game.get(),false);audio.speech({});
        status="Playing the original retail cinematic. Enter or Escape skips.";
    }
    void cinematic_view(double seconds,ImVec2 size,ImVec2 origin) {
        cinematic_renderer.resize(static_cast<unsigned>(std::max(1.0f,size.x)),static_cast<unsigned>(std::max(1.0f,size.y)));
        const auto frame=cinematic->sample(static_cast<float>(cinematic_ticks));Scene scene;scene.entities.clear();
        cinematic_renderer.draw(cinematic->level,scene,frame.eye,frame.eye+frame.forward,nullptr,1,false,title_empty_spyro,{},0,false,false,
            camera_settings.vertical_fov(),UINT32_MAX,cinematic->models,frame.actors,nullptr,nullptr,SuperchargeAppearance::Retail,
            nullptr,nullptr,{1,1,1},{},static_cast<float>(cinematic_ticks),frame.poses,frame.up);
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(cinematic_renderer.image())),size);
        if(!capture_fullscreen)ImGui::GetWindowDrawList()->AddText({origin.x+16,origin.y+size.y-28},IM_COL32(255,255,255,220),"Enter / Escape: skip cinematic");
        // Finish on the following frame so this shot retains its last audio
        // sample in both live playback and offline capture.
        if(menu==Menu::None)cinematic_ticks+=std::min(seconds,0.1)*60;
    }
