#include "spyro/renderer.hpp"
#include "spyro/game_build.hpp"
#include "spyro/audio_output.hpp"
#include "spyro/savegame.hpp"
#include "spyro/campaign.hpp"
#include "spyro/travel.hpp"
#include "spyro/frontend.hpp"
#include "spyro/title.hpp"
#include "spyro/universe.hpp"
#include "spyro/custom_level.hpp"
#include "spyro/video_capture.hpp"
#include "spyro/multiplayer.hpp"
#include "spyro/upgrade_hud.hpp"
#include "spyro/pc_actors.hpp"
#include "spyro/element_art.hpp"
#include "spyro/gamepad.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "retail_canvas.hpp"
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <mmsystem.h>
#include <Xinput.h>
#include <chrono>
#include <fstream>
#include <memory>
#include <thread>
#include <string>
#include <algorithm>
#include <bit>
#include "desktop_capture_shot.hpp"
#include "../tools/crownworks_swim_route.hpp"
#include "../tools/carry_quest.hpp"
#include "../tools/challenge_pilot.hpp"
#include "desktop_backrealms.hpp"
#include "spyro/powerup_import.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
using namespace spyro;
using Microsoft::WRL::ComPtr;
namespace {
unsigned pending_width=0,pending_height=0;
bool close_requested=false,pad_devices_changed=true;
struct TimerResolution {
    bool active=timeBeginPeriod(1)==TIMERR_NOERROR;
    ~TimerResolution(){if(active)timeEndPeriod(1);}
};
struct ShellApartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~ShellApartment(){if(SUCCEEDED(result))CoUninitialize();}
};
// XInput, loaded at run time (xinput1_4 on Windows 8 and later). The first
// connected controller in any of the four slots plays. Empty slots are only
// rescanned after a device change, or every two seconds while none is found,
// because XInputGetState on an empty slot is slow.
class XInputReader {
public:
    XInputReader() {
        for(const auto* name:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"})if((module_=LoadLibraryW(name)))break;
        if(module_)get_state_=reinterpret_cast<decltype(get_state_)>(GetProcAddress(module_,"XInputGetState"));
    }
    ~XInputReader(){if(module_)FreeLibrary(module_);}
    XInputReader(const XInputReader&)=delete;XInputReader& operator=(const XInputReader&)=delete;
    bool read(PadSample& sample) {
        if(!get_state_)return false;
        XINPUT_STATE state{};
        if(slot_<XUSER_MAX_COUNT && get_state_(slot_,&state)==ERROR_SUCCESS){copy(state,sample);return true;}
        slot_=XUSER_MAX_COUNT;
        const auto now=GetTickCount64();
        if(!pad_devices_changed && now<next_scan_)return false;
        pad_devices_changed=false;next_scan_=now+2000;
        for(DWORD i=0;i<XUSER_MAX_COUNT;++i)if(get_state_(i,&state)==ERROR_SUCCESS){slot_=i;copy(state,sample);return true;}
        return false;
    }
private:
    static void copy(const XINPUT_STATE& state,PadSample& s) {
        const auto& g=state.Gamepad;
        s.buttons=g.wButtons;s.left_trigger=g.bLeftTrigger;s.right_trigger=g.bRightTrigger;
        s.left_x=g.sThumbLX;s.left_y=g.sThumbLY;s.right_x=g.sThumbRX;s.right_y=g.sThumbRY;
    }
    HMODULE module_{};
    DWORD(WINAPI* get_state_)(DWORD,XINPUT_STATE*)=nullptr;
    DWORD slot_=XUSER_MAX_COUNT;
    ULONGLONG next_scan_=0;
};
LRESULT WINAPI window_proc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(ImGui_ImplWin32_WndProcHandler(window,message,w,l))return true;
    if(message==WM_SIZE && w!=SIZE_MINIMIZED){pending_width=LOWORD(l);pending_height=HIWORD(l);return 0;}
    if(message==WM_CLOSE){close_requested=true;return 0;}
    if(message==WM_DEVICECHANGE)pad_devices_changed=true;
    if(message==WM_SYSCOMMAND && (w&0xfff0)==SC_KEYMENU)return 0;
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
std::filesystem::path from_utf8(const char* value) {return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value)));}
std::string utf8(const std::filesystem::path& path) {const auto u=path.u8string();return std::string(reinterpret_cast<const char*>(u.data()),u.size());}
std::string hex_list(const std::vector<unsigned>& values) {std::string out;char text[16];for(const auto v:values){std::snprintf(text,sizeof(text),"%s%x",out.empty()?"":" ",v);out+=text;}return out;}
void checked(HRESULT hr,const char* what) {if(FAILED(hr))throw FormatError(std::string(what)+" failed: "+std::to_string(hr));}
void palette() {
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowRounding=5;s.FrameRounding=4;s.GrabRounding=4;
    s.WindowPadding={12,12};s.FramePadding={8,5};s.ItemSpacing={8,8};
    s.Colors[ImGuiCol_WindowBg]={0.052f,0.061f,0.085f,1};s.Colors[ImGuiCol_Header]={0.22f,0.17f,0.33f,1};
    s.Colors[ImGuiCol_Button]={0.25f,0.18f,0.40f,1};s.Colors[ImGuiCol_ButtonHovered]={0.38f,0.26f,0.56f,1};
    s.Colors[ImGuiCol_CheckMark]={0.7f,0.5f,1,1};s.Colors[ImGuiCol_SliderGrab]={0.7f,0.5f,1,1};
    s.Colors[ImGuiCol_TabSelected]={0.3f,0.22f,0.45f,1};
}
struct Workbench {
    bool backrealms=false,night_presentation=false;
    Bytes ice_tile;     // Winter Tundra frozen-pond tile for elemental ice
    double simulation_ms=0;
    enum class Menu {None,Title,Files,Pause,Options,Source,NewGame,Quit,Guidebook,Sound,Upgrades,UpgradeHud,Destruction,Elements,Levels};
    Menu menu=Menu::None,menu_back=Menu::Title,options_back=Menu::Title;
    bool frontend=false,menu_focus=true,pending_new_game=false;
    // Spyro 3 edition (--spyro3, or whenever a Year of the Dragon copy is
    // mounted): its own window title, menus, PC locations and saves. Spyro 2
    // stays the default edition.
    bool spyro3_edition=false;HWND main_window{};
    bool spyro3()const{return mounted_profile?mounted_profile->yotd():spyro3_edition;}
    const wchar_t* game_title()const{return spyro3()?L"Spyro 3: Year of the Dragon PC":L"Spyro 2 PC";}
    void update_window_title(){if(main_window)SetWindowTextW(main_window,game_title());}
    // YOTD worlds: each home scene opens the group of levels that follow it.
    static unsigned yotd_home(unsigned slot){return slot<115?97:slot<133?115:slot<151?133:151;}
    // Each edition keeps its own PC locations under %LOCALAPPDATA%: Spyro 2 PC
    // or Spyro 3 PC. Mounting the other game switches to its file.
    std::filesystem::path preferences_base;
    std::filesystem::path edition_preferences(bool yotd)const{return preferences_base/(yotd?L"Spyro 3 PC":L"Spyro 2 PC")/L"frontend.cfg";}
    void switch_edition_preferences(bool yotd) {
        if(preferences_base.empty() || edition_preferences(yotd)==preferences_path)return;
        write_preferences();preferences_path=edition_preferences(yotd);FrontendPreferences next;
        try{next=FrontendPreferences::read(preferences_path);}catch(const std::exception&){}   // first use: defaults
        preferences=std::move(next);refresh_saves();
    }
    // The copy an edition last used, else this project's extraction.
    std::filesystem::path edition_copy(bool yotd) {
        if(!preferences_base.empty()) {
            const auto path=edition_preferences(yotd);
            if(path==preferences_path){if(!preferences.source.empty())return preferences.source;}
            else try{const auto other=FrontendPreferences::read(path);if(!other.source.empty())return other.source;}catch(const std::exception&){}
        }
        return yotd?"data-yotd":"data/disc";
    }
    void switch_game(bool yotd) {
        const auto copy=edition_copy(yotd);
        if(!std::filesystem::exists(copy)){menu_back=menu;spyro3_edition=yotd;frontend=true;open_menu(Menu::Source);
            status=yotd?"Choose your Spyro 3: Year of the Dragon copy.":"Choose your Spyro 2 copy.";return;}
        spyro3_edition=yotd;mount(copy);
        if(mounted_profile->yotd()!=yotd)status=yotd?"That copy is not Spyro 3: Year of the Dragon.":"That copy is not Spyro 2.";
        else status=std::string(mounted_profile->name)+" mounted.";
    }
    // YOTD extra areas per main scene, from each archive's header only.
    std::map<unsigned,unsigned> yotd_area_counts;
    void count_yotd_areas() {
        yotd_area_counts.clear();if(!wad || !mounted_profile || !mounted_profile->yotd())return;
        for(const unsigned slot:mounted_profile->levels())try {
            const auto size=wad->entry(slot).size;if(size<0x60)continue;
            const auto head=wad->read(slot,0,0x60);unsigned count=1;
            for(unsigned area=1;area<5;++area) {
                const auto at=0x20u+(area-1)*16;const auto bytes=read_u32(head,at+4),gameplay=read_u32(head,at+12);
                if(!bytes || !gameplay || std::uint64_t(read_u32(head,at))+bytes>size)break;
                ++count;
            }
            yotd_area_counts[slot]=count;
        }catch(const std::exception&){}
    }
    unsigned selected_save=0,save_page=0;
    FrontendPreferences preferences;
    std::filesystem::path preferences_path,pending_save;
    std::string preferences_warning;
    struct RecentSave {std::filesystem::path path;std::optional<GameSave> data;std::string error;};
    std::vector<RecentSave> recent_saves;
    std::unique_ptr<TitleAssets> title;
    Renderer title_renderer;
    SpyroModel title_empty_spyro;
    AudioOutput title_audio;
    double title_ticks=840;
    std::unique_ptr<CinematicAssets> cinematic;
    Renderer cinematic_renderer;
    AudioOutput cinematic_audio;
    double cinematic_ticks=0;
    unsigned cinematic_movie{};
    bool cinematic_playing=false,automatic_cinematics=true;
    bool retail_intro_available=false,retail_outro_available=false;
    std::set<unsigned> cinematic_intros;
    #include "desktop_cinematic.inl"

    std::map<std::string,ImVec2> menu_buttons;
    ImGuiID menu_navigation{};
    std::string notification;
    float notification_seconds=0;
    unsigned workbench_toggles=0;   // F1 presses honoured (the --hide-workbench check)
    // 80071bd4: leaving a level whose talisman (or completion) was given during
    // this visit plays its outro (80073e48(1)) before the home world loads.
    bool travel_outro_played=false;unsigned outros_before_exit=0;
    std::optional<CampaignExit> exit_after_outro;
    bool exit_outro_due()const{
        return game && game->reward_this_visit && level && !level->universe && level->custom_name.empty() && !mounted_profile->yotd() &&
            cinematic_slot(executable,level->wad_slot,true).has_value();
    }
    std::function<void(InputState&)> scripted_input;   // integration checks drive the real controller path
    // The workbench panels <-> a full-window game, while playing. The game
    // keeps running; Esc > Workbench in the pause menu also shows them again.
    void toggle_workbench() {
        frontend=!frontend;focus_play_viewport=playing;++workbench_toggles;
        if(frontend){notification="Workbench hidden. Press F1 (or Esc > Workbench) to show it again.";notification_seconds=5;}
    }
    std::function<std::optional<std::filesystem::path>(bool)> test_save_picker;
    std::unique_ptr<AssetStore> assets;
    std::unique_ptr<WadArchive> wad;
    const GameProfile* mounted_profile=nullptr;
    std::unique_ptr<Level> level;
    std::unique_ptr<SpyroModel> spyro;
    std::optional<SpyroRollBank> july_rolls;
    std::string roll_warning;
    std::optional<RetailPowerupBank> retail_powerups;
    std::optional<YotdDonorBank> yotd_donor;
    unsigned yotd_areas=1,yotd_selected_area=0;
    std::string powerup_warning;
    ObjectModels object_models;
    ObjectPreview object_preview;
    FixedClock object_clock;
    std::vector<RetailObject> preview_objects;
    bool object_studio=false,object_animate=true;
    unsigned object_clip=0;
    float object_frame=0;
    Bytes executable,global_archive,powerup_archive;
    CameraSettings camera_settings;
    Cheats cheats;
    bool unlock_travel=false;
    SpyroPose pose;
    EditorDocument document;
    std::unique_ptr<Game> game;
    Renderer renderer;
    OnlineClient online;
    OnlineGhosts online_ghosts;
    OnlineStatus online_status;
    std::vector<RemoteSpyro> remote_spyros;
    unsigned online_peak_players{};
    char relay_url[2048]{},room_code[64]{};
    AudioOutput audio;
    VideoCapture video;
    CaptureArea capture_area;
    std::filesystem::path photo_request,video_request,video_path;
    bool video_stop=false;
    double capture_seconds=0;
    DialogueInput dialogue_input;
    // The controller plays the game only. Gameplay, dialogue and the pause /
    // skip buttons read `pad` directly; the game's own menus see it through
    // ImGui navigation; the workbench panels never do (ImGui's backend does
    // not poll it).
    GamePad pad;XInputReader xinput;int pad_context=-1;
    void poll_pad(HWND window,ImGuiIO& io,const PadSample* scripted,bool automated) {
        PadSample sample;bool connected=false;
        if(scripted){sample=*scripted;connected=true;}
        else if(!automated && GetForegroundWindow()==window)connected=xinput.read(sample);
        pad.update(sample,connected);
        // A button held while the owner changes (game, its menus, a cinematic)
        // counts only after it is released.
        const int context=menu!=Menu::None?1:cinematic_playing?2:0;
        if(context!=pad_context){pad.hold_back();pad_context=context;}
        // ImGui sees the controller only while the game's menus own the screen
        // (no workbench panel is drawn then), and only to move and activate:
        // Circle backs out and Start resumes through the game's own handling.
        const bool menus=context==1 && pad.connected();
        if(menus){io.ConfigFlags|=ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;}
        else {io.ConfigFlags&=~ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags&=~ImGuiBackendFlags_HasGamepad;}
        static constexpr std::pair<ImGuiKey,std::uint32_t> buttons[]{{ImGuiKey_GamepadDpadUp,pad::up},{ImGuiKey_GamepadDpadDown,pad::down},
            {ImGuiKey_GamepadDpadLeft,pad::left},{ImGuiKey_GamepadDpadRight,pad::right},{ImGuiKey_GamepadFaceDown,pad::a}};
        static constexpr std::pair<ImGuiKey,std::uint32_t> stick[]{{ImGuiKey_GamepadLStickUp,pad::stick_up},{ImGuiKey_GamepadLStickDown,pad::stick_down},
            {ImGuiKey_GamepadLStickLeft,pad::stick_left},{ImGuiKey_GamepadLStickRight,pad::stick_right}};
        for(const auto& [key,bit]:buttons)io.AddKeyEvent(key,menus && pad.down(bit));
        for(const auto& [key,bit]:stick){const float v=menus?pad.stick(bit):0;io.AddKeyAnalogEvent(key,v>.1f,v);}
    }
    GameSave campaign;
    GameSave universe_campaign;
    bool universe_ready=false,capture_offline=false,universe_replay=false,capture_fullscreen=false;
    std::uint64_t universe_seed=2026,universe_world=0;
    unsigned universe_route_target=1,universe_transitions=0;
    std::filesystem::path universe_save_path;
    HudAssets hud;
    HudAssets upgrade_art=upgrade_sprite_assets();
    UpgradeHudDisplay upgrade_display;
    unsigned upgrade_selection=0,element_selection=0;
    std::string element_replay;
    ElementHudAtlas element_hud;
    InterfaceAssets interface_assets;
    unsigned guide_world{},guide_selected{};
    bool guide_detail=false,guide_skills=false;
    unsigned guide_skill_page=0;
    double interface_ticks=0,guide_turn=60;
    std::vector<std::array<ImDrawVert,3>> guide_previous_page;
    bool guide_last_detail=false,guide_last_skills=false;unsigned guide_last_world=UINT32_MAX,guide_last_selected=UINT32_MAX;
    std::map<unsigned,std::vector<RetailObject>> guide_objects;
    struct GuideStats {unsigned gems{},spirits{},orbs{},orb_mask{},flight_tasks{},flight_ticks{};bool visited=false,talisman=false,boss=false;};
    std::array<GuideStats,29> guide_stats{};
    unsigned dialogue_revision=UINT32_MAX,dialogue_page=0,dialogue_selection=0;
    std::uint64_t dialogue_entry_tick=0;
    unsigned dialogue_delay=60;
    float dialogue_scroll=0;
    bool dialogue_revealed=false,dialogue_visible=false,dialogue_choices=false,dialogue_answers_page=false;
    double dialogue_close_age=24;
    RetailText dialogue_layout;
    RetailText dialogue_question;
    std::string dialogue_name;
    std::array<std::string,2> dialogue_answers;
    int dialogue_side=0,dialogue_purchase=-1;
    #include "desktop_interface.inl"
    bool campaign_ready=false,show_hud=true;
    double fps_window{},fps_value{};unsigned fps_frames{};
    unsigned suspend_frames=0;
    std::filesystem::path save_path;
    char source_path[2048]{},project_path[1024]="data/projects/level.s2scene";
    std::string error,status="Choose your retail ISO, CUE/BIN, or extracted folder.";
    bool playing=false,wire=false,scene_dirty=false,collision=false,focus_play_viewport=false;
    bool studio=true,animation_playing=true,retail_loop=true;
    unsigned animation=0;
    float animation_frame=0,animation_speed=1,model_yaw=-0.7f,model_pitch=0.25f,model_distance=1.8f;
    double animation_seconds=0;
    bool replay_inputs=false;
    bool swim_replay=false;
    bool tunnel_replay=false;
    CrownworksSwimRoute tunnel_route;
    unsigned observed_motions=0;
    unsigned interaction_record=UINT32_MAX;
    float capture_orbit{},capture_distance{},capture_height{},capture_orbit_rate{};
    bool interaction_charge=false,interaction_passive=false;
    // Validation: --attack talk walks a conversation through (triangle, then
    // "yes" to every question); --attack-class HEX@FRAME applies the retail
    // superflame bit to every live moby of a class from that frame on.
    // After a paid service (a purchase bit set mid-run) Spyro walks away, as a
    // player would, so the service's cutscene (Moneybags' door) can play.
    bool interaction_talk=false,interaction_approach=false;unsigned talk_open_ticks=0;std::uint32_t talk_purchases=UINT32_MAX;
    // --attack fetch --socket-record N: walk up to a carriable moby (Hurricos'
    // diodes) until Spyro takes it in his mouth, carry it towards the socket
    // record and spit it (Circle) from two units away.
    bool interaction_fetch=false,fetch_spat=false;unsigned socket_record=UINT32_MAX;
    std::vector<std::pair<unsigned,unsigned>> attack_classes;
    bool bridge_replay=false;
    std::string ability_replay,movement_replay;
    Vec3 interaction_direction;
    int selected_slot=15,selected_entity=-1,selected_object=-1;
    std::uint32_t selected_triangle=UINT32_MAX;
    Vec3 camera{},target{};
    float yaw=0.5f,pitch=-0.6f,fly_speed=30;
    double render_ms=0;
    std::filesystem::path mounted_path;
    std::optional<CaptureShot> capture_shot;

    template<class F> void action(F function) {try {function();error.clear();}catch(const std::exception& e){error=e.what();}}

    void connect_online(bool host) {
        if(game && game->playable_boss.active()) {
            game->playable_boss.select(*game,PlayableCharacter::Spyro);
            selected_character=PlayableCharacter::Spyro;
        }
        if(!level || !game)throw FormatError("Mount your retail game before connecting");
        if(!level->custom_name.empty())throw FormatError("Custom level playtests currently run offline");
        if(!playing)play();
        if(host && !room_code[0])strncpy_s(room_code,OnlineClient::room_code().c_str(),_TRUNCATE);
        online.connect(relay_url,room_code,host);
    }

    void set_source(const std::filesystem::path& path) {const auto s=utf8(path);strncpy_s(source_path,s.c_str(),_TRUNCATE);}
    std::filesystem::path& current_save_path(){return level && level->universe?universe_save_path:save_path;}
    GameSave& current_campaign(){return level && level->universe?universe_campaign:campaign;}
    void remember_settings(){if(game){current_campaign().upgrades=game->upgrades;current_campaign().cheats=cheats;current_campaign().upgrade_hud_settings=game->upgrade_hud_settings;current_campaign().destruction_settings=game->destruction_settings;}}
    void remember_game() {remember_settings();if(level && level->source_game==GameBuild::YearOfTheDragonUS && level->source_area)return;if(playing && game && level->custom_name.empty()){if(level->universe){universe_campaign.capture(*game);universe_ready=true;}else{campaign.capture(*game);campaign_ready=true;}}}
    void unlock_upgrades(bool activate_powers) {
        if(!game)return;
        auto& u=game->upgrades;u.enabled=true;u.charge=u.flame=u.flight=u.glide=u.headbash=2;u.headbash_mode=HeadbashMode::Lava;u.carry=u.melee=true;
        u.roll=game->roll_available();unlock_all_elements(*game);
        if(activate_powers){cheats.supercharge=cheats.superflame=cheats.superfly=true;cheats.green_firebomb=false;game->cheats=cheats;}
        remember_settings();focus_play_viewport=playing;
        status=activate_powers?"All abilities ready. J melee, Shift supercharge, F superflame, Space/X superfly, G grab/drop near a container. 1-5 elements, Q shot, L Fury. Turn off Superfly in Cheats to use glide boosts.":
            "All upgrades and elements unlocked. J melee; 1-5 select Fire/Water/Ice/Electricity/Convexity, F breath, Q shot, L Fury. Power upgrades and carrying need their level power or Cheats.";
    }
    void import_roll_source(const std::filesystem::path& path) {
        AssetStore donor(path);const auto donor_exe=donor.read_all(find_game_executable(donor),4*1024*1024);
        WadArchive donor_wad(donor,&game_profile(donor_exe));auto bank=decode_july_rolls(donor_wad.read(0),donor_exe);
        audio.set_july_roll_sound(decode_sound_bank(donor_wad.read(17),donor_exe,17));
        if(spyro){auto candidate=*spyro;import_july_rolls(candidate,bank);*spyro=std::move(candidate);renderer.upload_spyro(*spyro);if(game)game->restore_player_pose();}
        july_rolls=std::move(bank);roll_warning.clear();preferences.july_roll_source=std::filesystem::absolute(path);write_preferences();
        status="July's left and right rolls and original sound imported. Unlock Dodge Roll in Upgrades; Z/V or L1/R1 to use.";
    }
    void choose_roll_source(HWND window) {
        wchar_t path[32768]{};OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=window;picker.lpstrFile=path;picker.nMaxFile=32768;
        picker.lpstrFilter=L"July game copy (*.cue;*.bin;*.iso;*.img;*.exe)\0*.cue;*.bin;*.iso;*.img;*.exe\0\0";
        picker.lpstrTitle=L"Import cut roll from your full July 13, 1999 copy";picker.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(GetOpenFileNameW(&picker)){auto source=std::filesystem::path(path);if(source.extension()==L".EXE" || source.extension()==L".exe")source=source.parent_path();import_roll_source(source);}
    }
    Bytes optional_archive(unsigned slot){return wad->contains(slot)?wad->read(slot):Bytes{};}
    void choose_powerup_source(HWND window) {
        wchar_t path[32768]{};OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=window;picker.lpstrFile=path;picker.nMaxFile=32768;
        picker.lpstrFilter=L"Retail game copy (*.cue;*.bin;*.iso;*.img;SCUS_944.25)\0*.cue;*.bin;*.iso;*.img;SCUS_944.25\0All files\0*.*\0\0";
        picker.lpstrTitle=mounted_profile && mounted_profile->yotd()?L"Choose your Spyro 2 retail donor copy":L"Choose your retail copy for prototype power-ups";picker.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(!GetOpenFileNameW(&picker))return;
        auto source=std::filesystem::path(path);if(source.filename()==L"SCUS_944.25")source=source.parent_path();
        if(mounted_profile && mounted_profile->yotd())yotd_donor=read_yotd_donor(source);
        else retail_powerups=read_retail_powerups(source);
        powerup_warning.clear();preferences.retail_powerup_source=std::filesystem::absolute(source);write_preferences();
        if(level && (mounted_profile->prerelease() || mounted_profile->yotd())){const bool resume=playing;load_level(level->wad_slot);if(resume)play();}
        status=mounted_profile && mounted_profile->yotd()?"Spyro 2 donor powers, HUD and effects ready for YOTD.":"Retail power-ups ready in prototype/demo levels.";
    }
    #include "desktop_spyro3.inl"
    void load_level(unsigned slot,bool remember=true,std::optional<UniverseAddress> generated={},bool resume=true,std::optional<unsigned> arrival_scene={},const std::filesystem::path& custom={}) {
        if(!wad)return;
        if(mounted_profile->yotd()) {
            if(generated || !custom.empty())throw FormatError("YOTD provisional mode loads original YOTD scenes only");
            load_yotd_level(slot,remember);return;
        }
        finish_cinematic();
        if(remember)remember_game();
        const auto archive=wad->read(generated?17:slot);
        auto next=std::make_unique<Level>(decode_level(archive,generated?17:slot));
        if(!generated)decode_travel(archive,executable,*next);
        if(!generated && custom.empty())next->script_image=make_retail_script_image(*wad,executable,slot);
        const auto arrival=arrival_scene?std::optional(campaign_arrival(archive,*arrival_scene,*next)):std::nullopt;
        const auto ripto_archive=optional_archive(71),forest_archive=optional_archive(15);
        auto next_spyro=std::make_unique<SpyroModel>(decode_spyro(global_archive,archive,executable,powerup_archive,29,forest_archive,optional_archive(45),optional_archive(59),
            mounted_profile->prerelease()?Bytes{}:optional_archive(57)));
        if(july_rolls && !next_spyro->animations.contains(10))import_july_rolls(*next_spyro,*july_rolls);
        auto next_objects=decode_object_models(archive,executable);
        const auto ripto_models=ripto_archive.empty()?ObjectModels{}:decode_object_models(ripto_archive,executable);
        const auto forest_models=forest_archive.empty()?ObjectModels{}:decode_object_models(forest_archive,executable);
        const unsigned bomb_models[]{0x2c2,0x146,0x1e2,0x21d},fire_models[]{0x1e3,0x1f8,0x1e1,0x211};
        const auto import_available=[&](const ObjectModels& donor,std::span<const unsigned> types) {
            // Compact discs may deliberately omit these optional donor levels.
            // Keep strict required-model checks when a donor bank is present.
            if(donor.models.empty())return;
            if(!mounted_profile->prerelease()){import_object_models(next_objects,donor,types);return;}
            std::vector<unsigned> available;for(auto type:types)if(donor.models.contains(type))available.push_back(type);
            import_object_models(next_objects,donor,available);
        };
        import_available(ripto_models,bomb_models);import_available(forest_models,fire_models);
        // Elemental extension: the retail Cloud Temples ice shell encases
        // element-frozen enemies; Summer Forest butterflies let demons feed
        // Sparx through his retail path; then the embedded PC demon actors.
        if(!mounted_profile->prerelease()) {
            if(const auto temples=optional_archive(61);!temples.empty()) {
                const auto temple_models=decode_object_models(temples,executable);const unsigned shell[]{0x271};
                if(temple_models.models.contains(0x271))import_object_models(next_objects,temple_models,shell);
            }
            const unsigned food[]{0x10};if(forest_models.models.contains(0x10))import_object_models(next_objects,forest_models,food);
        }
        install_embedded_demons(next_objects);
        if(generated) {
            auto terrain=slot==17?*next:decode_level(wad->read(slot),slot);
            *next=generate_universe(*generated,terrain,*next,next_objects,decode_return_portal(archive,executable));
            universe_seed=generated->seed;universe_world=generated->world;universe_route_target=1;
        }
        if(!custom.empty()) {
            import_custom_level_models(custom,*wad,executable,next_objects);
            *next=load_custom_level(custom,*wad,*next,next_objects,decode_return_portal(archive,executable));
        }
        if(night_presentation)backrealms_night(*next);
        if(backrealms && next->custom_arena) {
            auto boss_level=decode_level(ripto_archive,71);initialize_retail_objects(boss_level,ripto_models);
            install_ripto_encounter(*next,next_objects,boss_level,ripto_models);
            const auto crush_archive=wad->read(31);auto crush_models=decode_object_models(crush_archive,executable);
            auto crush_level=decode_level(crush_archive,31);initialize_retail_objects(crush_level,crush_models);
            std::ifstream texture_file("data/expansions/backrealms/mecha-crush.rgba",std::ios::binary);
            const Bytes texture{std::istreambuf_iterator<char>(texture_file),std::istreambuf_iterator<char>()};
            install_mecha_crush(*next,next_objects,crush_level,std::move(crush_models),texture);
        }
        auto power_effects=decode_powerup_assets(ripto_archive.empty()?archive:ripto_archive);
        if(!forest_archive.empty())power_effects.bubble_particle=decode_powerup_assets(forest_archive).bubble_particle;
        const auto local_effects=decode_powerup_assets(archive);
        power_effects.whirlwind_particle=local_effects.whirlwind_particle;
        if(!local_effects.wing_trail.texture_rgba.empty())power_effects.wing_trail=local_effects.wing_trail;
        if(mounted_profile->prerelease() && retail_powerups)import_retail_powerups(*next_spyro,next_objects,power_effects,*retail_powerups);
        renderer.upload_powerups(power_effects);
        if(!generated && custom.empty())initialize_retail_objects(*next,next_objects);
        auto next_hud=decode_hud(archive,executable);
        auto next_interface=decode_interface(archive,executable,optional_archive(4),slot==71 && custom.empty() && !generated,(slot==31 || slot==55) && custom.empty() && !generated);
        preview_objects=next->objects;object_clock.reset();object_studio=false;
        Scene scene;scene.retail_crc=mounted_profile->crc;scene.wad_slot=slot;scene.spawn=next->spawn;
        renderer.upload_level(*next);
        renderer.upload_spyro(*next_spyro);spyro=std::move(next_spyro);
        if(!mounted_profile->prerelease())import_playable_bosses(next_objects,*wad,executable);
        renderer.upload_objects(next_objects);object_models=std::move(next_objects);
        // Elemental ice uses Winter Tundra's frozen-pond tile (WAD 57 tile 35).
        if(ice_tile.empty() && !mounted_profile->prerelease())if(const auto tundra=optional_archive(57);!tundra.empty()) {
            const auto scene=decode_level(tundra,57);
            if(scene.texture_width>=16*64 && scene.texture_height>=3*64) {
                ice_tile.resize(64*64*4);
                for(unsigned y=0;y<64;++y)std::copy_n(scene.texture_rgba.data()+((std::size_t(2*64+y))*scene.texture_width+3*64)*4,64*4,ice_tile.data()+y*64*4);
                for(std::size_t i=3;i<ice_tile.size();i+=4)ice_tile[i]=255;
            }
        }
        renderer.upload_ice_texture(ice_tile);
        renderer.upload_hud(next_hud);hud=std::move(next_hud);
        renderer.upload_interface(next_interface);interface_assets=std::move(next_interface);
        upload_element_hud();
        upgrade_display.hide();
        dialogue_revision=UINT32_MAX;dialogue_visible=false;dialogue_close_age=24;
        game.reset();level=std::move(next);document.replace(scene);
        game=std::make_unique<Game>(*level,document.scene,camera_settings,&object_models,spyro.get());game->cheats=cheats;game->set_paused(true);playing=false;
        game->portal_travel.unlock=unlock_travel;
        game->playable_boss.set_scale(*game,boss_scale);
        if(!online.status().connected)game->playable_boss.select(*game,selected_character);
        if(generated) {
            if(universe_ready){universe_campaign.apply(*game,&object_models);cheats=universe_campaign.cheats;}
            if(!resume){game->play_scene.spawn=level->spawn;game->respawn();}
        } else if(campaign_ready && custom.empty()){campaign.apply(*game,&object_models);cheats=campaign.cheats;}
        if(arrival) {
            const auto health=game->player.health;
            game->play_scene.spawn=arrival->position;game->respawn();
            game->player.health=health;
            game->player.yaw=game->player.previous_yaw=arrival->yaw;
            game->camera.reset(game->level,game->player.position,arrival->yaw);
        }
        game->upgrades=current_campaign().upgrades;
        game->upgrade_hud_settings=current_campaign().upgrade_hud_settings;
        game->destruction_settings=current_campaign().destruction_settings;
        selected_slot=static_cast<int>(slot);selected_entity=-1;selected_object=-1;selected_triangle=UINT32_MAX;scene_dirty=false;
        audio.load(mounted_path,executable,archive,slot,ripto_archive,forest_archive,game->ripto.enabled,
            mounted_profile->prerelease() && retail_powerups?&retail_powerups->sounds:nullptr);
        if(video.active())audio.begin_capture(capture_offline);
        animation=0;animation_seconds=0;animation_frame=0;
        // cinematic_slot validates the entire executable CRC. Cache these
        // capabilities at load time instead of hashing twice in every UI frame.
        retail_intro_available=!generated && custom.empty() && cinematic_slot(executable,slot,false).has_value();
        retail_outro_available=!generated && custom.empty() && cinematic_slot(executable,slot,true).has_value();
        frame_level();status="Spyro's retail model, textures and animations loaded from your game copy.";
    }
    void open_custom_level(const std::filesystem::path& path) {
        if(!wad)throw FormatError("Mount your retail copy before opening a custom level");
        if(online.status().connected)throw FormatError("Disconnect from the online room before opening a custom level");
        world_returns.clear();   // a fresh trip: no world doors behind it
        load_level(custom_level_bank(path),true,{},true,{},path);play();status=level->custom_name+" - custom level";
    }
    // World doors (plan H1): another custom world through the portal
    // transition. Its Return Home comes back out in front of the door, so
    // nested trips unwind in order.
    std::vector<WorldReturn> world_returns;
    void travel_world(const TravelRequest& request) {
        auto& t=game->portal_travel;
        // Consume once: a failed load must not retry every frame.
        t.phase=PortalTravel::Phase::Idle;t.fade=0;t.cooldown=90;game->camera.scripted_view.reset();
        const auto root=level && !level->custom_path.empty()?level->custom_path.parent_path().parent_path().parent_path():std::filesystem::path("data/custom-levels");
        std::optional<WorldReturn> arrive;
        if(request.world_return) {
            if(world_returns.empty())throw FormatError("Return Home has no world door to go back to");
            arrive=world_returns.back();world_returns.pop_back();
        } else {
            if(!level || level->custom_path.empty())throw FormatError("A world door must stand in a custom world");
            const auto back=t.direction*-1;
            WorldReturn here{std::filesystem::relative(level->custom_path,root).generic_string(),(night_presentation?1u:0u)|(backrealms?2u:0u),
                t.target-t.direction*4.5f,std::atan2(back.x,back.z)};
            if(const auto floor=level->raycast(here.position+Vec3{0,2,0},{0,-1,0},6))here.position=floor->point;
            world_returns.push_back(here);
        }
        const auto path=(root/std::filesystem::u8path(request.world)).lexically_normal();
        if(!std::filesystem::exists(path))throw FormatError("A world door leads to a missing world: "+path.string());
        night_presentation=(request.world_flags&1)!=0;backrealms=(request.world_flags&2)!=0;renderer.backrealms_armor=backrealms;
        const auto health=game->player.health;
        load_level(custom_level_bank(path),true,{},false,{},path);
        game->world_return=world_returns.empty()?std::nullopt:std::optional(world_returns.back());
        playing=true;studio=object_studio=false;game->set_paused(false);
        if(arrive) {
            game->play_scene.spawn=arrive->position;game->respawn();game->player.health=health;
            game->player.yaw=game->player.previous_yaw=arrive->yaw;game->camera.reset(game->level,game->player.position,arrive->yaw);
        }
        begin_portal_arrival(*game,request);remember_game();
        status=level->custom_name+(arrive?" - back through the world door":" - through the world door");
    }
    void choose_custom_level(HWND window) {
        wchar_t path[32768]{};OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=window;
        picker.lpstrFile=path;picker.nMaxFile=32768;picker.lpstrFilter=L"Spyro custom worlds (*.s2world)\0*.s2world\0\0";
        picker.lpstrTitle=L"Open custom level";picker.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(GetOpenFileNameW(&picker))open_custom_level(std::filesystem::path(path));
    }
    void load_universe(UniverseAddress address,bool remember=true,bool resume=false) {
        if(!wad)throw FormatError("Mount your retail copy before exploring the universe");
        if(remember)remember_game();
        if(universe_ready && universe_campaign.universe->seed!=address.seed){universe_campaign={};universe_ready=false;universe_save_path.clear();}
        load_level(universe_source_slot(address),false,address,resume);
        playing=true;studio=object_studio=false;game->set_paused(false);
        status=level->universe->name+" - "+level->universe->biome;
    }
    void mount(const std::filesystem::path& path,unsigned initial_slot=UINT32_MAX) {
        auto next=std::make_unique<AssetStore>(path);
        auto next_executable=next->read_all(find_game_executable(*next),4*1024*1024);
        const auto info=inspect_exe(next_executable);
        const auto& profile=game_profile(info.crc);
        auto next_wad=std::make_unique<WadArchive>(*next,&profile);
        struct PreferencesRestore {Workbench& app;std::filesystem::path path;FrontendPreferences kept;bool committed=false;
            ~PreferencesRestore(){if(!committed){app.preferences_path=path;app.preferences=std::move(kept);}}} restore_preferences{*this,preferences_path,preferences};
        switch_edition_preferences(profile.yotd());
        // Decode before committing a new mount, preserving the existing scene if invalid.
        const auto first_slot=initial_slot==UINT32_MAX?profile.first_level():initial_slot;
        if(!next_wad->contains(first_slot))throw FormatError("Requested initial level is absent from this disc");
        auto next_archive=next_wad->read(first_slot),next_global=next_wad->read(0);
        auto next_powerups=!profile.yotd() && next_wad->contains(29)?next_wad->read(29):Bytes{};
        auto next_level=decode_level(next_archive,first_slot,profile);
        auto next_model=decode_spyro(next_global,next_archive,next_executable);
        auto next_camera=decode_camera_settings(next_executable);
        if(profile.yotd() && !yotd_donor) {
            auto donor=preferences.retail_powerup_source;
            if(donor.empty() && std::filesystem::exists("data/disc/SCUS_944.25"))donor="data/disc";
            if(!donor.empty())try{yotd_donor=read_yotd_donor(donor);powerup_warning.clear();}
                catch(const std::exception& e){powerup_warning=e.what();}
            else powerup_warning="Choose your Spyro 2 retail copy to add PC powers and interface assets.";
        }
        if(profile.prerelease() && !retail_powerups) {
            auto donor=preferences.retail_powerup_source;
            if(donor.empty() && std::filesystem::exists("data/disc/SCUS_944.25"))donor="data/disc";
            if(!donor.empty())try{retail_powerups=read_retail_powerups(donor);powerup_warning.clear();}
                catch(const std::exception& e){powerup_warning=e.what();}
            else powerup_warning="Choose your retail copy to add complete power-ups to this build.";
        }
        if(!july_rolls) {
            auto donor=preferences.july_roll_source;
            if(donor.empty() && std::filesystem::exists("data/builds/july-1999/disc/SPYRO2.EXE"))donor="data/builds/july-1999/disc";
            if(!donor.empty())try{import_roll_source(donor);}catch(const std::exception& e){roll_warning=e.what();}
        }
        remember_game();game.reset();level.reset();wad.reset();assets=std::move(next);wad=std::move(next_wad);
        if(mounted_profile && mounted_profile->crc!=profile.crc){campaign={};campaign_ready=false;save_path.clear();universe_campaign={};universe_ready=false;cinematic_intros.clear();cheats={};}
        mounted_profile=&profile;
        if(profile.prerelease() || profile.yotd())cheats.supercharge_appearance=SuperchargeAppearance::Retail;
        executable=std::move(next_executable);global_archive=std::move(next_global);powerup_archive=std::move(next_powerups);camera_settings=std::move(next_camera);
        mounted_path=std::filesystem::absolute(path).lexically_normal();set_source(mounted_path);load_level(first_slot,false);error.clear();
        update_window_title();count_yotd_areas();spyro3_edition=profile.yotd();restore_preferences.committed=true;
        title.reset();guide_objects.clear();preferences.source=mounted_path;write_preferences();
    }
    void frame_level() {
        if(!level)return;
        object_studio=false;
        target=level->bounds.center();const auto extent=length(level->bounds.max-level->bounds.min);
        camera=target+Vec3{extent*0.48f,extent*0.56f,-extent*0.65f};
        auto dir=normalized(target-camera);yaw=std::atan2(dir.x,dir.z);pitch=std::asin(dir.y);
    }
    void view_spawn() {
        if(!level)return;
        object_studio=false;
        FollowCamera preview(camera_settings);preview.reset(*level,document.scene.spawn,level->spawn_yaw);
        const auto view=preview.sample(1);camera=view.eye;target=camera+view.forward;
        yaw=std::atan2(view.forward.x,view.forward.z);pitch=std::asin(view.forward.y);studio=false;
    }
    void view_object(unsigned index) {
        if(!level || index>=level->objects.size())throw FormatError("Retail object index is outside this level");
        if(playing)stop();studio=false;object_studio=false;selected_object=static_cast<int>(index);
        target=level->objects[index].position+Vec3{0,0.15f,0};float best_clearance=-1;
        // Orbit to an unobstructed inspection angle instead of putting the
        // editor camera inside a wall beside the selected object.
        for(unsigned i=0;i<8;++i) {
            const auto offset=retail::rotate_model({0.9f,0.6f,-1.2f},i*std::numbers::pi_v<float>/4);
            const auto distance=length(offset);const auto direction=offset/distance;
            const auto hit=level->raycast(target,direction,distance);
            const auto clearance=hit?std::max(0.08f,hit->distance-0.08f):distance;
            if(clearance>best_clearance){best_clearance=clearance;camera=target+direction*clearance;}
            if(!hit)break;
        }
        const auto direction=normalized(target-camera);yaw=std::atan2(direction.x,direction.z);pitch=std::asin(direction.y);
    }
    void view_object_model(unsigned type,unsigned clip=0) {
        const auto found=object_models.models.find(type);
        if(found==object_models.models.end() || !found->second.animated || clip>=found->second.animations.size())
            throw FormatError("Animated object model / clip is unavailable in this level");
        if(playing)stop();studio=true;object_studio=true;object_animate=true;object_clip=clip;object_frame=0;object_clock.reset();
        object_preview={};object_preview.object.type=static_cast<std::uint16_t>(type);
        if(level)for(const auto& instance:level->objects)if(instance.type==type) {
            object_preview.object.material_color=instance.material_color;break;
        }
        object_preview.object.rotation[2]=64;object_preview.object.previous_rotation=object_preview.object.rotation;
        object_preview.object.animation=start_object_animation(found->second,clip);
        object_preview.object.previous_animation=object_preview.object.animation;
        object_preview.pose=object_animation_frame(found->second,clip,0);
        if(type==120)object_preview.object.material_color=read_u32(executable,ExeTables(executable)(0x80061adc)+3*4);
        model_distance=std::clamp(found->second.animations[clip].radius*3,0.6f,100.0f);
    }
    PlayableCharacter selected_character=PlayableCharacter::Spyro;
    unsigned boss_scale=3;
    void play() {if(!level)return;remember_settings();audio.speech({});game=std::make_unique<Game>(*level,document.scene,camera_settings,&object_models,spyro.get());
        if(level->universe){if(universe_ready)universe_campaign.apply(*game,&object_models);}else if(campaign_ready && level->custom_name.empty() && !level->source_area)campaign.apply(*game,&object_models);playing=true;studio=false;object_studio=false;
        game->upgrades=current_campaign().upgrades;game->upgrade_hud_settings=current_campaign().upgrade_hud_settings;game->destruction_settings=current_campaign().destruction_settings;
        game->cheats=cheats;focus_play_viewport=true;animation_seconds=0;upgrade_display.hide();
        status=level->custom_name.empty()?"Spyro movement test running at fixed 60 Hz.":level->custom_name+" - playing at fixed 60 Hz";
        game->portal_travel.unlock=unlock_travel;
        game->playable_boss.set_scale(*game,boss_scale);
        if(!online.status().connected)game->playable_boss.select(*game,selected_character);
        if(automatic_cinematics && !mounted_profile->yotd() && !level->universe && level->custom_name.empty() && !cinematic_intros.contains(level->wad_slot) && !campaign.levels.contains(level->wad_slot)) {
            start_cinematic();cinematic_intros.insert(level->wad_slot);
        }
    }
    void stop() {finish_cinematic();remember_game();playing=false;if(game){game->dialogue.dismiss(game->retail_objects);game->set_paused(true);}audio.speech({});status="Game stopped; progress retained for the next play session.";}
    void new_game() {
        cinematic_intros.clear();campaign=GameSave{};campaign_ready=false;save_path.clear();playing=false;cheats={};
        load_level(mounted_profile->yotd()?mounted_profile->first_level():mounted_profile->demo()?27:17,false);play();status="New game started.";
    }
    std::optional<std::filesystem::path> choose_game_save(HWND window,bool writing) {
        if(test_save_picker)return test_save_picker(writing);
        const bool paused_before=!game || game->paused;if(game)game->set_paused(true);audio.update(game.get(),false);
        struct Restore {Workbench& app;bool paused;~Restore(){if(app.game)app.game->set_paused(paused);app.suspend_frames=2;}} restore{*this,paused_before};
        wchar_t path[32768]{};std::filesystem::path initial;
        if(!current_save_path().empty()){initial=current_save_path().parent_path();wcsncpy_s(path,current_save_path().c_str(),_TRUNCATE);}
        else {
            PWSTR folder{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames,0,nullptr,&folder))){initial=std::filesystem::path(folder)/(spyro3()?L"Spyro 3 PC":L"Spyro 2 PC");CoTaskMemFree(folder);}
            if(writing)wcscpy_s(path,level && level->universe?L"Spyro Universe.s2save":spyro3()?L"Spyro 3.s2save":L"Spyro 2.s2save");
        }
        if(!initial.empty() && writing)std::filesystem::create_directories(initial);
        OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=window;picker.lpstrFile=path;picker.nMaxFile=32768;
        picker.lpstrFilter=spyro3()?L"Spyro 3 PC saves (*.s2save)\0*.s2save\0\0":L"Spyro 2 PC saves (*.s2save)\0*.s2save\0\0";picker.lpstrDefExt=L"s2save";
        picker.lpstrTitle=writing?(spyro3()?L"Save Spyro 3 game":L"Save Spyro 2 game"):(spyro3()?L"Load Spyro 3 game":L"Load Spyro 2 game");picker.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();
        picker.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(writing?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        const bool selected=writing?GetSaveFileNameW(&picker)!=0:GetOpenFileNameW(&picker)!=0;
        if(!selected){if(CommDlgExtendedError())throw FormatError("Could not open the PC save picker");return {};}
        return std::filesystem::path(path);
    }
    void save_game_to(const std::filesystem::path& path) {
        if(!playing || !game)throw FormatError("Start playing before saving a game");
        if(level->source_game==GameBuild::YearOfTheDragonUS && level->source_area)throw FormatError("YOTD subarea saving is not supported yet; return to the main area first.");
        if(!level->custom_name.empty())throw FormatError("Custom level playtests do not yet have save slots. Your retail save is unchanged.");
        remember_game();(level->universe?universe_campaign:campaign).write(path);current_save_path()=path;status="Game saved to "+utf8(path);
        remember_save(path);
        notification="Saved: "+utf8(path);notification_seconds=5;
    }
    bool save_game(HWND window,bool choose_location) {
        if(!playing || !game)throw FormatError("Start playing before saving a game");
        if(!level->custom_name.empty())throw FormatError("Custom level playtests do not yet have save slots. Your retail save is unchanged.");
        auto path=current_save_path();if(choose_location || path.empty()){const auto picked=choose_game_save(window,true);if(!picked)return false;path=*picked;}
        save_game_to(path);return true;
    }
    void load_game(const std::filesystem::path& path) {
        if(!wad)throw FormatError("Mount your retail copy before loading a game save");
        auto saved=GameSave::read(path);
        if(saved.retail_crc!=mounted_profile->crc)throw FormatError("This save belongs to a different Spyro build. Mount the matching game copy.");
        // Check saved IDs, conversations and denominations before replacing play.
        const auto archive=wad->read(saved.universe?17:saved.slot);auto check_level=decode_level(archive,saved.universe?17:saved.slot,*mounted_profile);
        const auto check_models=decode_object_models(archive,executable);
        if(saved.universe) {
            const auto theme=decode_level(wad->read(universe_source_slot(*saved.universe)),universe_source_slot(*saved.universe));
            check_level=generate_universe(*saved.universe,theme,check_level,check_models,decode_return_portal(archive,executable));
        }else initialize_retail_objects(check_level,check_models);
        Scene check_scene;check_scene.retail_crc=mounted_profile->crc;check_scene.wad_slot=saved.slot;check_scene.spawn=check_level.spawn;
        Game check_game(check_level,check_scene,camera_settings,&check_models);saved.apply(check_game,&check_models);
        remember_game();
        if(saved.universe) {
            const auto old=universe_campaign;const bool old_ready=universe_ready;universe_campaign=std::move(saved);universe_ready=true;
            try{load_universe(*universe_campaign.universe,false,true);}catch(...){universe_campaign=old;universe_ready=old_ready;throw;}
        } else {
            const auto old=campaign;const bool old_ready=campaign_ready;campaign=std::move(saved);campaign_ready=true;
            try{load_level(campaign.slot,false);}catch(...){campaign=old;campaign_ready=old_ready;throw;}
        }
        playing=true;studio=object_studio=false;game->set_paused(false);current_save_path()=path;suspend_frames=2;
        status="Loaded game from "+utf8(path);
        remember_save(path);
    }
    void load_game_menu(HWND window) {if(const auto path=choose_game_save(window,false))load_game(*path);}
#include "desktop_menu.inl"
    #include "desktop_upgrades.inl"
    #include "desktop_ripto_hud.inl"
    // Element HUD atlas: icons, meter emblems and the mounted power-meter parts
    // with element-coloured outlines (rebuilt whenever the interface reloads).
    void upload_element_hud() {
        const bool mounted=interface_assets.rgba.size()==std::size_t(interface_assets.width)*interface_assets.height*4;
        const std::array<HudSprite,5> parts{interface_assets.resident[22],interface_assets.resident[23],interface_assets.resident[24],interface_assets.resident[25],interface_assets.resident[26]};
        element_hud=element_hud_atlas(mounted?&interface_assets:nullptr,mounted?&parts:nullptr);
        if(element_hud.valid()){HudAssets art;art.width=element_hud.width;art.height=element_hud.height;art.rgba=element_hud.rgba;renderer.upload_element_icons(art);}
    }
    // Optional frame-rate counter (Game menu / F3): presented frames averaged
    // over half-second windows, drawn top right in the retail font.
    void count_frame(double seconds) {
        fps_window+=seconds;++fps_frames;
        if(fps_window>=0.5){fps_value=fps_frames/fps_window;fps_window=0;fps_frames=0;}
    }
    void draw_fps() {
        if(!preferences.show_fps || !renderer.interface_image() || interface_assets.font[0x30].colors[0].width==0)return;
        const auto& io=ImGui::GetIO();ImVec2 origin{0,0},size=io.DisplaySize;
        const bool whole=capture_fullscreen || frontend || menu!=Menu::None;
        if(!whole && capture_area.width>1 && capture_area.height>1){origin={float(capture_area.x),float(capture_area.y)};size={float(capture_area.width),float(capture_area.height)};}
        RetailCanvas canvas(interface_assets,ImGui::GetForegroundDrawList(),static_cast<ImTextureID>(reinterpret_cast<intptr_t>(renderer.interface_image())),origin,size);
        const auto fps=static_cast<unsigned>(std::lround(fps_value));
        // Retail text palette 800637d4: 4 green, 1 yellow, 5 red.
        canvas.right(std::to_string(fps)+" FPS",506,3,fps>=55?4u:fps>=30?1u:5u);
    }
    // The play view's vertical field of view. The PC default keeps the PS1's
    // full 4:3 display (240 lines at H 341, 80011d24, with the 320/512 vertical
    // correction of 8001c2f8) and widens with the window. The PS1 FOV option
    // shows the picture the PS1 actually draws: 512 columns by the 216-line
    // draw region (80011bbc), 73.7 x 53.8 degrees, fitted to the window: a
    // wider window keeps the PS1 vertical FOV (and sees more at the sides), a
    // narrower one keeps its horizontal FOV.
    float view_fov(float aspect)const {
        if(!preferences.ps1_fov)return camera_settings.vertical_fov();
        const float half_v=216/(2*camera_settings.focal_length*camera_settings.vertical_correction),half_h=256/camera_settings.focal_length;
        return 2*std::atan(std::max(half_v,half_h/std::max(aspect,0.1f)));
    }
    bool force_hud_recall=false;
    void draw_hud(ImVec2 origin,ImVec2 size) {
        if(!show_hud || !game || !renderer.hud_image()){upgrade_display.hide();return;}
        const bool retail_boss=game->ripto.enabled && game->ripto.retail_arena;
        // PC: the counters scale with the window height (one retail pixel per
        // 1/320 of it, 0.8 wide). PS1 HUD size: the original's proportions, one
        // retail line per 1/240 of the height and its 4:3 pixel width (512
        // columns over a 4:3 frame, 0.625), at the retail x positions in that
        // centred frame: gems 40 (8005251c, called at 80053f5c), the life
        // portrait 208 (80052b88), spirits 408 (80053fa8). The y positions are
        // the retail ones in both (the entry curve 80052498 less 16 / 24).
        const bool ps1=preferences.ps1_hud;
        const float scale=ps1?size.y/240.0f:std::clamp(size.y/320.0f,0.7f,3.0f),sx=ps1?scale*0.625f:scale*0.8f;
        const float frame=origin.x+(size.x-512*sx)/2;
        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
        const auto sprite=[&](unsigned id,float x,float y){const auto& s=hud.sprites.at(id);
            draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(renderer.hud_image())),{x,y},{x+s.width*sx,y+s.height*scale},
                {s.x/float(hud.width),s.y/float(hud.height)},{(s.x+s.width)/float(hud.width),(s.y+s.height)/float(hud.height)});};
        const auto offset=[&](HudPart part,int delay=0){return (game->hud.offset(part,hud,game->clock.alpha(),delay)-40)*scale;};
        const auto number=[&](HudPart part,float x,float y){int digit=0;
            for(const auto c:std::to_string(game->hud.counter(part).value)){sprite(c-'0',x,y+offset(part,-3*++digit));x+=24*sx;}};
        const float top=origin.y+24*scale;const auto tick=game->clock.ticks();
        if(!retail_boss && game->hud.visible(HudPart::Gems)) {
            const float gem_x=ps1?frame+40*sx:origin.x+16*sx;
            sprite(11+(tick/4)%6,gem_x,top+offset(HudPart::Gems));number(HudPart::Gems,gem_x+40*sx,top+2*scale);
        }
        if(!retail_boss && game->hud.visible(HudPart::Lives)) {
            const float life=ps1?frame+208*sx:origin.x+size.x*0.48f-24*sx,y=top-8*scale+offset(HudPart::Lives);
            const auto blink=game->hud.eyes();sprite(31,life,y);sprite(32+blink,life+hud.eye_x[blink]*sx,y+hud.eye_y[blink]*scale);
            number(HudPart::Lives,life+56*sx,top+2*scale);
        }
        if(!retail_boss && game->hud.visible(HudPart::Spirits)) {
            const float right=ps1?frame+408*sx:origin.x+size.x-112*sx;
            sprite(19+(tick/4)%3,right,top+offset(HudPart::Spirits));number(HudPart::Spirits,right+40*sx,top+2*scale);
        }
        if(!game->dialogue.active() && game->ripto.cinematic==CrownCinematic::None && game->hud.power_visible() && renderer.interface_image()) {
            // Resident 80052d84: original crystal, caps and six flashing cells.
            RetailCanvas canvas(interface_assets,draw,static_cast<ImTextureID>(reinterpret_cast<intptr_t>(renderer.interface_image())),origin,size);
            const float x=game->hud.power_x(hud,game->clock.alpha()),y=selected_slot==67?52.0f:64.0f;
            canvas.resident(26,x+2,y);canvas.resident(27,x+4,y+2);canvas.resident(22,x,y+16);
            const int cells=game->hud.power_fill()>0?static_cast<int>(game->hud.power_fill()*6):-1;
            for(int i=0;i<6;++i)canvas.resident(i<cells || (i==cells && cells>=0 && (tick&15)<8)?25:23,x,y+88-i*12);
            canvas.resident(24,x,y+100);
        }
        // Elemental HUD: the retail power crystal with element-coloured outlines,
        // the element emblem on its orb and a purple Fury strip beneath.
        if(game->upgrades.enabled && renderer.interface_image() && !game->dialogue.active() && !game->portal_travel.active() && game->ripto.cinematic==CrownCinematic::None &&
           game->upgrade_hud_settings.mode!=UpgradeHudMode::Off && (std::any_of(game->upgrades.elements.tiers.begin()+1,game->upgrades.elements.tiers.end(),[](unsigned t){return t>0;}) ||
            game->upgrades.elements.tier(Element::Fire)>1)) {
            RetailCanvas c(interface_assets,draw,static_cast<ImTextureID>(reinterpret_cast<intptr_t>(renderer.interface_image())),origin,size);
            const auto& e=game->upgrades.elements;const auto eq=static_cast<unsigned>(e.equipped);
            // The retail crystal in its own colours; only its outline takes the
            // element colour, and the element emblem replaces the superwing.
            const auto el=[&](const HudSprite& s,float sx0,float sy0) {
                if(!s.width || !renderer.element_icon_image())return;
                draw->AddImage(reinterpret_cast<ImTextureID>(renderer.element_icon_image()),c.point(sx0,sy0),c.point(sx0+s.width,sy0+s.height),
                    {float(s.x)/element_hud.width,float(s.y)/element_hud.height},{float(s.x+s.width)/element_hud.width,float(s.y+s.height)/element_hud.height});
            };
            // The emblem identifies the element, so no icon column sits above; the
            // crystal stands where the retail power meter would (y 56).
            const bool retail_meter=game->hud.power_visible();   // a gate power meter shares this corner
            const float x=retail_meter?446:478,y=56;const auto& part=element_hud.parts[eq];
            if(element_hud.meter) {
                el(part[4],x+2,y);
                const auto& emblem=element_hud.emblems[eq];el(emblem,x+2+(24-float(emblem.width))/2,y+(16-float(emblem.height))/2);
                el(part[0],x,y+16);
                const int cells=static_cast<int>(e.energy/100*6);
                for(int i=0;i<6;++i)el(part[i<cells?3:1],x,y+88-i*12);
                el(part[2],x,y+100);
            }
            const float fury=e.fury/100;
            c.rect(x-8,y+118,30,5,IM_COL32(30,10,44,230));c.rect(x-7,y+119,28*fury,3,fury>=1 && (tick&16)?IM_COL32(230,170,255,255):IM_COL32(150,60,220,255));
            if(game->elements.dark_fury())c.centered("DARK SPYRO "+std::to_string(game->elements.dark_ticks()/60+1),256,48,1,true,.75f);
        }
        // The level's own challenge HUD (timers, goal and item counters): the
        // sprites its overlay draw hook placed this script tick, in retail
        // screen units, decoded from the level's HUD page on first use.
        std::vector<std::pair<const HudSprite*,ImVec2>> challenge_hud;
        if(renderer.interface_image() && !retail_boss && !game->dialogue.active() && !game->portal_travel.active() && game->ripto.cinematic==CrownCinematic::None) {
            for(const auto& d:game->overlay_scripts.hud_draws()) {
                const auto& sprite=scene_sprite(interface_assets,d.u,d.v,d.clut,d.width,d.height,d.color,(d.flags&0x80)!=0);
                challenge_hud.push_back({&sprite,ImVec2{float(d.x),float(d.y)}});
            }
            if(interface_assets.changed){renderer.upload_interface(interface_assets);interface_assets.changed=false;}
        }
        if(renderer.interface_image()) {
            const auto& s=game->systems;
            RetailCanvas c(interface_assets,draw,static_cast<ImTextureID>(reinterpret_cast<intptr_t>(renderer.interface_image())),origin,size);
            for(const auto& [sprite,at]:challenge_hud)c.image(*sprite,at.x,at.y);
            const bool cutscene=game->ripto.cinematic!=CrownCinematic::None;
            if(cutscene) {
                // Crownworks cutscenes: letterbox bars, a caption, and the skip
                // prompt once this cutscene has been seen.
                const float open=std::min(1.0f,game->ripto.cinematic_ticks/20.0f);
                c.rect(0,0,512,28*open,IM_COL32(0,0,0,255));c.rect(0,240-28*open,512,28*open,IM_COL32(0,0,0,255));
                if(const char* caption=game->ripto.cinematic_caption();caption && *caption)c.centered(caption,256,219,1,true,.75f);
                if(game->ripto.cinematic_skippable())c.centered("JUMP: SKIP",466,8,0,true,.55f);
            }
            if(!retail_boss && !cutscene && game->custom_notice_ticks && !game->dialogue.active() && !game->portal_travel.active())
                c.centered(game->custom_notice,256,36,1,true,.8f);
            if(!cutscene && !game->dialogue.active() && PlayableBoss::at_switch(*game))
                c.centered("E / TRIANGLE: SWITCH CHARACTER",256,199,1,true,.65f);
            if(!cutscene && !game->dialogue.active() && game->playable_boss.active())
                c.centered(std::string(playable_character_name(game->playable_boss.character()))+"   F / CIRCLE: RANGED   J / D-PAD UP: CLOSE",256,211,1,true,.62f);
            draw_retail_boss_hud(c);
            if(retail_boss)draw_retail_ripto_hud(c);
            else if(!cutscene && game->ripto.enabled && game->ripto.engaged && !game->ripto.defeated() && !game->dialogue.active() && !game->portal_travel.active()) {
                const auto& boss=game->ripto;
                const bool duo=boss.stage==RiptoStage::MechaGulp && boss.crush!=UINT32_MAX;
                const bool finale=boss.action==RiptoAction::Finale;
                // Phase-aware bars (6.5.5): red Ripto, amber/blue mechs, violet
                // sky, and the white crown-core window in the finale.
                const unsigned phase=finale?4:static_cast<unsigned>(boss.stage)+1;
                if(boss.crownworks())c.centered(finale?"FINALE":"PHASE "+std::to_string(phase)+" / 3",256,176,0,true,.55f);
                if(boss.crownworks() && boss.difficulty!=BossDifficulty::Normal)
                    c.centered(boss.difficulty==BossDifficulty::Assist?"ASSIST":"HARD",470,176,0,true,.55f);
                if(duo) {
                    c.centered(boss.gulp_frozen?"MECHA-GULP (FROZEN)":"MECHA-GULP",182,188,1,true,.65f);c.centered(boss.crush_frozen?"MECHA-CRUSH (FROZEN)":"MECHA-CRUSH",330,188,1,true,.65f);
                    for(unsigned i=0;i<2;++i){const float x=118+i*148;c.rect(x,200,128,9,IM_COL32(189,139,55,255));c.rect(x+2,202,124,5,IM_COL32(39,14,41,255));
                        c.rect(x+2,202,124*(i?boss.crush_health/60.f:boss.health_fraction()),5,i?IM_COL32(65,154,220,255):IM_COL32(230,150,30,255));}
                }else if(finale) {
                    c.centered(boss.phase_name(),256,188,1,true,.8f);
                    c.rect(145,200,222,9,IM_COL32(189,139,55,255));c.rect(147,202,218,5,IM_COL32(39,14,41,255));
                    c.rect(147,202,218*float(boss.finale_ticks)/RiptoBoss::finale_window,5,(tick&8)?IM_COL32(255,255,255,255):IM_COL32(200,200,215,255));
                }else {
                c.centered(boss.phase_name(),256,188,1,true,.8f);
                c.rect(145,200,222,9,IM_COL32(189,139,55,255));c.rect(147,202,218,5,IM_COL32(39,14,41,255));
                c.rect(147,202,218*boss.health_fraction(),5,boss.stage==RiptoStage::Bird && boss.crownworks()?IM_COL32(160,70,230,255):IM_COL32(209,40,42,255));
                }
                // Relay icons in the two-mech phase only, where the relays act.
                if(duo && game->level.custom_arena) {
                    const auto& a=*game->level.custom_arena;
                    const std::pair<const char*,bool> relays[]{{"SHIELD",a.boss_shield},{"REPAIR",a.boss_repair},{"OVERDRIVE",a.boss_overdrive}};
                    for(unsigned i=0;i<3;++i){const float x=176+i*80;
                        c.rect(x,222,8,8,relays[i].second?IM_COL32(255,190,60,255):IM_COL32(70,70,80,255));
                        c.centered(relays[i].second?relays[i].first:"BROKEN",x+36,223,0,true,.5f);}
                }
                if(boss.stage!=RiptoStage::Bird)c.centered("YOUR ORBS "+std::to_string(boss.spyro_orbs)+" / 3   RIPTO "+std::to_string(boss.ripto_orbs)+" / 3",256,211,0,true,.65f);
            }
            if(!retail_boss) {
            if(s.mode==ChallengeMode::Trial || s.mode==ChallengeMode::Hunter) {
                const unsigned seconds=(s.remaining+59)/60;const auto timer=std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);
                if(s.mode==ChallengeMode::Trial)c.centered(timer,256,12,0,false,1.3f);
                if(s.bonus_ticks)c.centered("+"+std::to_string(s.last_bonus/60),310,18,1);
                if(s.mode==ChallengeMode::Trial)for(unsigned i=0;i<4;++i)c.text(interface_assets.flight_labels[s.flight_index][i]+"  "+std::to_string(s.counts[i])+"/8",18,136+i*18,0,true,0.7f);
                else c.centered(std::to_string(s.hunter_count)+" / "+std::to_string(s.hunter_total),256,38);
            }
            if(s.mode==ChallengeMode::Trolley)c.centered("GEARS  "+std::to_string(s.gears)+" / 50",256,16);
            if(!s.prompt.empty())c.centered(s.prompt,256,218,0,true,0.65f);
            if(s.results()) {
                c.rect(58,34,396,164,IM_COL32(22,8,52,218));c.centered(s.success?"CHALLENGE COMPLETE!":"TRY AGAIN?",256,48,1);
                if(s.mode==ChallengeMode::Results)for(unsigned i=0;i<4;++i){c.text(interface_assets.flight_labels[s.flight_index][i],84,76+i*17,0,true,0.8f);c.text(std::to_string(s.counts[i])+" / 8",348,76+i*17,0,true,0.8f);}
                else c.centered(s.mode==ChallengeMode::TrolleyResults?"GEARS  "+std::to_string(s.gears)+" / 50":std::to_string(s.hunter_count)+" / "+std::to_string(s.hunter_total),256,100);
                c.centered(s.mode==ChallengeMode::Results?"1: RETRY   2: FREE FLIGHT   3: EXIT":"1: RETRY    2: LEAVE",256,160,0,true,0.72f);
                c.centered("ENTER TO RETRY",256,180,1,true,0.6f);
            }
            }
        }
        if(retail_boss || game->ripto.cinematic!=CrownCinematic::None)upgrade_display.hide();else draw_upgrade_hud(origin,size);
        draw->PopClipRect();
    }
    void play_in_water() {
        if(!level || !level->surface_swimming)throw FormatError("This scene has no surface-swimming animation bank");
        std::optional<Vec3> location;float best=1e30f;
        for(const auto& triangle:level->triangles)if(triangle.water) {
            if(length(cross(triangle.b-triangle.a,triangle.c-triangle.a))<1)continue;
            const auto center=(triangle.a+triangle.b+triangle.c)/3;
            if(level->raycast(center-Vec3{0,0.001f,0},{0,-1,0},1.5f))continue;
            const float distance=length(center-level->spawn);
            if(distance<best){location=center;best=distance;}
        }
        if(!location)throw FormatError("No pool with enough clearance for swimming was found");
        play();game->play_scene.spawn=*location+Vec3{0,0.1f,0};game->respawn();
        float clearance=-1,heading=0;
        for(unsigned i=0;i<16;++i) {
            const float angle=i*2*std::numbers::pi_v<float>/16;
            const Vec3 direction{std::sin(angle),0,std::cos(angle)};
            const auto obstruction=level->raycast(*location-Vec3{0,0.7f,0},direction,8);
            const float distance=obstruction?obstruction->distance:8;
            if(distance>clearance){clearance=distance;heading=angle;}
        }
        game->player.yaw=game->player.previous_yaw=heading;
        game->camera.reset(*level,game->player.position,heading);status="Swimming test: Shift dives, Space swims, WASD steers underwater.";
    }
    void play_beside_object(unsigned index) {
        if(!level || index>=level->objects.size())throw FormatError("Object is outside this level");
        auto position=level->objects[index].position;std::optional<Vec3> spawn;float best=-1;
        const auto& selected=level->objects[index];const bool large=selected.power_target && selected.power_target->scenery;
        float distance=selected.enemy?2.2f:interaction_approach?7.0f:1.25f;
        // Floating objects (Zoe hovers about 1.5 above her perch) are measured
        // from the floor under them.
        if(!large)if(const auto under=level->raycast(position+Vec3{0,.2f,0},{0,-1,0},3.0f))position.y=under->point.y;
        if(large) {
            const auto& bank=object_models.models.at(selected.type);
            const auto sample=sample_object_animation(bank,selected.animation,object_models.animation_blends);Bounds bounds;
            for(const auto vertex:sample.vertices)bounds.add(selected.position+object_rotation(vertex*object_model_scale(selected),selected));
            position=bounds.center();position.y=bounds.min.y;
            distance=std::max(bounds.max.x-bounds.min.x,bounds.max.z-bounds.min.z)*0.5f+2.0f;
        }
        // An approach starts on the speaker's own floor, several units out (Zoe
        // ignores a Spyro already within 4 units when she wakes): try a few
        // distances and sixteen headings until one has a clear walk in.
        const std::vector<float> distances=interaction_approach && !large?std::vector<float>{7,9,5.5f,11,4.6f}:std::vector<float>{distance};
        const unsigned headings=interaction_approach && !large?16:8;
        for(const float reach:distances) {
        if(spawn)break;
        distance=reach;
        for(unsigned i=0;i<headings;++i) {
            const float angle=i*2*std::numbers::pi_v<float>/headings;const Vec3 direction{std::sin(angle),0,std::cos(angle)};
            const auto candidate=position-direction*distance+Vec3{0,large?5.0f:1.0f,0};
            const auto floor=level->raycast(candidate,{0,-1,0},large?12.0f:2.0f);
            if(!floor || std::abs(floor->point.y-position.y)>(large?3.0f:interaction_approach?.6f:0.5f))continue;
            const auto origin=floor->point+retail::origin_above_feet,line=position+retail::origin_above_feet-origin;
            if(const auto hit=level->raycast(origin,line,length(line));hit && hit->distance<length(line)-0.01f)continue;
            const auto behind=direction*-2.5f+Vec3{0,0.6f,0};const auto obstruction=level->raycast(origin,behind,length(behind));
            float clearance=obstruction?obstruction->distance:length(behind);
            if(interaction_talk) {
                // Speakers' triangle checks (8003a98c) want Spyro in front of them.
                const float heading=retail::native_heading(selected.rotation[2]*16);
                clearance+=4*dot(direction,Vec3{-std::sin(heading),0,-std::cos(heading)});
            }
            if(clearance>best) {spawn=floor->point;interaction_direction=direction;best=clearance;}
        }
        }
        if(!spawn)throw FormatError("No clear standing position beside this object");
        play();game->play_scene.spawn=*spawn;game->respawn();
        game->player.yaw=game->player.previous_yaw=std::atan2(interaction_direction.x,interaction_direction.z);
        game->camera.reset(*level,game->player.position,game->player.yaw);status="Playing beside selected object.";
    }
    void browse(HWND window) {
        wchar_t path[2048]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;
        ofn.lpstrFilter=L"Retail disc image\0*.cue;*.bin;*.iso\0All files\0*.*\0";ofn.lpstrFile=path;ofn.nMaxFile=2048;
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&ofn)) {set_source(path);mount(path);}
    }
    void widgets(HWND window,double seconds) {
        online_status=online.status();online_peak_players=std::max(online_peak_players,static_cast<unsigned>(online_status.players.size()));
        if(game)game->portal_travel.allowed=!online_status.connected || online_status.hosting;
        if(menu==Menu::None && game && game->portal_travel.phase==PortalTravel::Phase::Loading && !game->portal_travel.request.world.empty()) {
            const auto request=game->portal_travel.request;
            action([&]{travel_world(request);});
            seconds=0;
        }
        if(menu==Menu::None && game && !cinematic_playing && game->portal_travel.phase==PortalTravel::Phase::Loading &&
           !travel_outro_played && exit_outro_due()) {
            travel_outro_played=true;++outros_before_exit;action([&]{start_exit_cinematic();});seconds=0;
        }
        if(exit_after_outro && !cinematic_playing && menu==Menu::None) {
            const auto exit=*exit_after_outro;exit_after_outro.reset();
            action([&]{load_level(exit.destination,true,{},false,exit.source_scene);playing=true;studio=object_studio=false;game->set_paused(false);remember_game();
                status="Returned to the home world. Level progress retained.";});
            seconds=0;
        }
        if(menu==Menu::None && game && !cinematic_playing && game->portal_travel.phase==PortalTravel::Phase::Loading) {
            const auto request=game->portal_travel.request;travel_outro_played=false;
            action([&]{
                // Consume once: a failed mount must not retry every frame.
                game->player.position=game->player.previous_position=game->portal_travel.start;
                game->portal_travel.phase=PortalTravel::Phase::Idle;game->portal_travel.fade=0;
                game->portal_travel.cooldown=90;game->camera.scripted_view.reset();
                load_level(request.destination,true,{},false,request.transporter?std::nullopt:std::optional(request.source_scene));
                playing=true;studio=object_studio=false;game->set_paused(false);begin_portal_arrival(*game,request);remember_game();
                status="Arriving through the retail portal. Campaign progress retained.";
            });
            seconds=0;
        }
        if(menu==Menu::None && online_status.connected && !online_status.hosting && level) {
            const auto host=std::find_if(online_status.players.begin(),online_status.players.end(),[&](const auto& p){return p.peer==online_status.host;});
            if(host!=online_status.players.end() && !(host->world==online_world(*level)))action([&]{
                if(host->world.universe)load_universe(host->world.address);
                else {load_level(host->world.slot);play();}
                seconds=0;
            });
        }
        if(menu==Menu::None && game && game->universe_travel) {
            const auto destination=*game->universe_travel;game->universe_travel.reset();
            if(online_status.connected && !online_status.hosting)status="The host chooses the next destination.";
            else action([&]{load_universe(destination);++universe_transitions;});
            seconds=0;
        }
        auto& io=ImGui::GetIO();bool ask_new_game=false;
        if(!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)) {
            const bool keys=!io.WantTextInput;
            const bool escape=keys && ImGui::IsKeyPressed(ImGuiKey_Escape,false);
            // The controller: Start pauses and resumes, Circle backs out of the
            // game's menus, Cross / Circle / Start skip a cinematic. In play
            // Circle flames; it never pauses.
            const bool start=pad.pressed(pad::start),circle=pad.pressed(pad::b);
            if(cinematic_playing && menu==Menu::None && (escape || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter,false)) || pad.pressed(pad::a|pad::b|pad::start)))finish_cinematic();
            else if(menu!=Menu::None && (escape || circle || (start && menu==Menu::Pause)))action([&]{menu_return();});
            else if(menu==Menu::None && playing && (start || (escape && !game->dialogue.active())))action([&]{open_menu(Menu::Pause);});
            if(keys && menu==Menu::None && playing && ImGui::IsKeyPressed(ImGuiKey_F5,false))action([&]{save_game(window,false);});
            if(keys && ImGui::IsKeyPressed(ImGuiKey_F9,false))action([&]{open_menu(Menu::Files);});
            if(keys && ImGui::IsKeyPressed(ImGuiKey_F3,false)){preferences.show_fps=!preferences.show_fps;write_preferences();}
            // F1 minimises the workbench panels to a full-window game and
            // brings them back (Esc > Workbench restores them as well).
            if(keys && menu==Menu::None && playing && level && ImGui::IsKeyPressed(ImGuiKey_F1,false))toggle_workbench();
        }
        const auto choose_capture=[&](bool recording) {
            if(recording && video.active()){video_stop=true;return;}
            const bool paused_before=!game || game->paused;if(game)game->set_paused(true);audio.update(game.get(),false);
            wchar_t path[32768]{};wcscpy_s(path,recording?L"Spyro 2 clip.mp4":L"Spyro 2 photo.bmp");
            const auto initial=std::filesystem::absolute("data/captures");std::filesystem::create_directories(initial);
            OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=window;picker.lpstrFile=path;picker.nMaxFile=32768;
            picker.lpstrInitialDir=initial.c_str();picker.lpstrFilter=recording?L"MP4 video (*.mp4)\0*.mp4\0\0":L"Bitmap photo (*.bmp)\0*.bmp\0\0";
            picker.lpstrDefExt=recording?L"mp4":L"bmp";picker.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
            const bool selected=GetSaveFileNameW(&picker)!=0;
            if(game)game->set_paused(paused_before);suspend_frames=2;
            if(selected){if(recording)video_request=path;else photo_request=path;}
        };
        if(!io.WantTextInput && level) {
            if(ImGui::IsKeyPressed(ImGuiKey_F10,false))action([&]{choose_capture(false);});
            if(ImGui::IsKeyPressed(ImGuiKey_F11,false))action([&]{choose_capture(true);});
        }
        if(menu!=Menu::None){menu_widgets(window,seconds);return;}
        if(suspend_frames){seconds=0;--suspend_frames;}
        if(frontend || capture_fullscreen){viewport(seconds);
            if(frontend && (notification_seconds>0 || !error.empty())) {
                notification_seconds-=static_cast<float>(seconds);
                ImGui::SetNextWindowPos({20,io.DisplaySize.y-88});ImGui::SetNextWindowSize({std::min(io.DisplaySize.x-40,740.0f),68});
                ImGui::SetNextWindowBgAlpha(0.86f);
                ImGui::Begin("Game message",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
                ImGui::TextWrapped("%s",(error.empty()?notification:error).c_str());ImGui::End();
            }return;}
        if(ImGui::BeginMainMenuBar()) {
            ImGui::TextColored(spyro3()?ImVec4{1,0.78f,0.36f,1}:ImVec4{0.77f,0.59f,1,1},spyro3()?"SPYRO 3: YEAR OF THE DRAGON  /  PC WORKBENCH":"SPYRO 2  /  PC WORKBENCH");
            if(ImGui::BeginMenu("Game")) {
                if(ImGui::MenuItem("Main menu"))action([&]{frontend=true;open_menu(Menu::Title);});
                if(ImGui::BeginMenu("Switch game")) {
                    const bool yotd=mounted_profile && mounted_profile->yotd();
                    if(ImGui::MenuItem("Spyro 2: Ripto's Rage!",nullptr,wad && !yotd))action([&]{switch_game(false);});
                    if(ImGui::MenuItem("Spyro 3: Year of the Dragon",nullptr,wad && yotd))action([&]{switch_game(true);});
                    ImGui::EndMenu();
                }
                if(mounted_profile && mounted_profile->yotd() && ImGui::MenuItem("Choose level..."))action([&]{menu_back=Menu::Title;frontend=true;open_menu(Menu::Levels);});
                if(ImGui::MenuItem("New game",nullptr,false,wad!=nullptr))ask_new_game=true;
                if(ImGui::MenuItem("Save game","F5",false,playing && level->custom_name.empty()))action([&]{save_game(window,false);});
                if(ImGui::MenuItem("Save game as...",nullptr,false,playing && level->custom_name.empty()))action([&]{save_game(window,true);});
                if(ImGui::MenuItem("Load game...","F9"))action([&]{open_menu(Menu::Files);});
                if(ImGui::BeginMenu("Playable character",playing && game && !online.status().connected)) {
                    for(const auto character:{PlayableCharacter::Spyro,PlayableCharacter::Crush,PlayableCharacter::Gulp,PlayableCharacter::Ripto}) {
                        if(ImGui::MenuItem(playable_character_name(character),nullptr,game->playable_boss.character()==character,
                            game->playable_boss.available(character,game->object_models()))) {
                            game->playable_boss.select(*game,character);
                            selected_character=character;
                        }
                    }
                    ImGui::Separator();
                    constexpr const char* scales[]{"1/3 scale","2/3 scale","3/3 scale"};
                    for(unsigned i=0;i<3;++i)if(ImGui::MenuItem(scales[i],nullptr,boss_scale==i+1)) {
                        boss_scale=i+1;
                        game->playable_boss.set_scale(*game,boss_scale);
                    }
                    ImGui::EndMenu();
                }
                if(ImGui::MenuItem("Open custom level...",nullptr,false,wad!=nullptr && !mounted_profile->yotd()))action([&]{choose_custom_level(window);});   // Spyro 2 worlds
                ImGui::Separator();if(ImGui::MenuItem("Pause",nullptr,game && game->paused,playing))game->set_paused(!game->paused);
                if(ImGui::MenuItem("Hide workbench (full-window game)","F1",false,playing && level!=nullptr))toggle_workbench();
                ImGui::MenuItem("Show gameplay HUD",nullptr,&show_hud);
                if(ImGui::MenuItem("Show FPS counter","F3",preferences.show_fps)){preferences.show_fps=!preferences.show_fps;write_preferences();}
                {   // PC options (also in the pause menu's Options), saved with the preferences
                    int delay=static_cast<int>(preferences.camera_recentre_seconds);ImGui::SetNextItemWidth(120);
                    if(ImGui::SliderInt("Camera recentre delay (s, 0 = never)",&delay,0,10))preferences.camera_recentre_seconds=static_cast<unsigned>(delay);
                    if(ImGui::IsItemDeactivatedAfterEdit())write_preferences();
                    if(ImGui::MenuItem("PS1 FOV (wide windows keep its vertical view)",nullptr,preferences.ps1_fov)){preferences.ps1_fov=!preferences.ps1_fov;write_preferences();}
                    if(ImGui::MenuItem("PS1 HUD size",nullptr,preferences.ps1_hud)){preferences.ps1_hud=!preferences.ps1_hud;write_preferences();}
                }
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Online",game!=nullptr)) {
                ImGui::TextUnformatted("Internet exploration / early preview");
                ImGui::SetNextItemWidth(420);ImGui::InputText("Relay HTTPS address",relay_url,sizeof(relay_url));
                ImGui::SetNextItemWidth(310);ImGui::InputText("Room code",room_code,sizeof(room_code));
                if(ImGui::MenuItem("Host a room"))action([&]{connect_online(true);});
                if(ImGui::MenuItem("Join this room"))action([&]{connect_online(false);});
                if(ImGui::MenuItem("Copy room code",nullptr,false,room_code[0]!=0))ImGui::SetClipboardText(room_code);
                if(ImGui::MenuItem("New room code"))strncpy_s(room_code,OnlineClient::room_code().c_str(),_TRUNCATE);
                if(ImGui::MenuItem("Disconnect"))online.disconnect();
                ImGui::Separator();ImGui::TextWrapped("%s",online_status.message.c_str());
                ImGui::Text("Players: %zu / 4",online_status.players.size());
                ImGui::TextWrapped("The host chooses the world. This first version shares animated players and travel; enemies, pickups and combat remain local to each player.");
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Upgrades",game!=nullptr)) {
                auto& u=game->upgrades;
                if(ImGui::MenuItem("Open upgrade page"))open_menu(Menu::Upgrades);
                if(ImGui::Checkbox("Enable upgrade progression",&u.enabled)) {if(!u.enabled)game->abilities.reset(*game);focus_play_viewport=playing;}
                ImGui::TextWrapped("This switch enables earning and spending points. Buy abilities below, or unlock them immediately for testing.");
                if(ImGui::MenuItem("Unlock all and activate powers"))unlock_upgrades(true);
                if(ImGui::MenuItem("Unlock all for testing"))unlock_upgrades(false);
                ImGui::TextWrapped("Gem value collected while enabled also earns upgrade points. Your gem total is kept.");
                ImGui::Text("Upgrade points: %u",u.points);ImGui::Separator();
                if(ImGui::MenuItem("Import July roll animations..."))action([&]{choose_roll_source(window);});
                if(!roll_warning.empty())ImGui::TextWrapped("%s",roll_warning.c_str());
                const auto item=[&](const char* label,Upgrade kind,unsigned tier){const auto price=u.cost(kind);
                    const unsigned icon=kind==Upgrade::Glide?3u:kind==Upgrade::Carry?4u:kind==Upgrade::Melee?5u:static_cast<unsigned>(kind);
                    const auto p=ImGui::GetCursorScreenPos();ImGui::Dummy({26,26});draw_upgrade_icon(ImGui::GetWindowDrawList(),icon,p,{p.x+26,p.y+26});ImGui::SameLine();
                    const auto text=std::string(label)+" ["+std::to_string(tier)+(kind==Upgrade::Carry || kind==Upgrade::Melee || kind==Upgrade::Roll?"/1]":"/2]")+
                        (price?" - "+std::to_string(price)+" points":" - unlocked");
                    if(ImGui::MenuItem(text.c_str(),nullptr,false,u.enabled && price && u.points>=price && (kind!=Upgrade::Roll || game->roll_available()))) {
                        u.buy(kind);focus_play_viewport=playing;status="Upgrade purchased. "+std::string(label);
                    }};
                item(u.charge?"Red supercharge / 160% speed":"Yellow supercharge / 130% speed",Upgrade::Supercharge,u.charge);
                item(u.flame?"Superflame splits into three":"Superflame homes on enemies",Upgrade::Superflame,u.flame);
                item(u.flight?"Superfly / 150% speed":"Superfly / 125% speed",Upgrade::Superfly,u.flight);
                item(u.glide?"Glide / two wing boosts":"Glide / one wing boost",Upgrade::Glide,u.glide);
                item("Aerial carry and throw",Upgrade::Carry,u.carry);item("Three-hit melee combo",Upgrade::Melee,u.melee);
                item(u.headbash?"Headbash / lava apocalypse":"Headbash / crater shockwave",Upgrade::Headbash,u.headbash);
                item("July dodge roll / Z and V",Upgrade::Roll,u.roll);
                if(!game->roll_available())ImGui::TextWrapped("Dodge Roll requires an imported full July build.");
                if(u.headbash)for(auto mode:{HeadbashMode::Normal,HeadbashMode::Crater,HeadbashMode::Lava})
                    if(ImGui::MenuItem(headbash_mode_name(mode),nullptr,u.headbash_mode==mode,mode!=HeadbashMode::Lava || u.headbash>=2))u.headbash_mode=mode;
                ImGui::Separator();ImGui::TextColored({.8f,.6f,1,1},"Elemental powers");
                auto& el=u.elements;
                for(unsigned k=0;k<element_count;++k) {
                    const auto element=static_cast<Element>(k);const auto c=element_color(element);
                    const auto p=ImGui::GetCursorScreenPos();ImGui::Dummy({20,20});
                    ImGui::GetWindowDrawList()->AddCircleFilled({p.x+10,p.y+10},8,IM_COL32(int(c.x*255),int(c.y*255),int(c.z*255),255));ImGui::SameLine();
                    const auto price=u.element_cost(element);const auto tier=el.tier(element);
                    const auto label=std::string(element_name(element))+" ["+std::to_string(tier)+"/3]"+(price?" - next "+std::to_string(price)+" points":" - complete")+
                        "##buy-element-"+std::to_string(k);
                    if(ImGui::MenuItem(label.c_str(),nullptr,false,element_purchasable(*game,element) && u.points>=price)) {
                        u.buy_element(element,convexity_milestone(*game));focus_play_viewport=playing;status=std::string(element_name(element))+" tier purchased.";
                    }
                }
                ImGui::TextWrapped("Tiers: 1 breath, 2 shot, 3 elemental headbash + Fury. Convexity needs Water, Ice and Electricity plus Ripto defeated (or all four elements mastered).");
                if(ImGui::MenuItem("Unlock every element (testing)")){u.enabled=true;unlock_all_elements(*game);remember_settings();focus_play_viewport=playing;status="All five elements unlocked at tier 3 with full Fury.";}
                {
                    int equipped=static_cast<int>(el.equipped);
                    for(unsigned k=0;k<element_count;++k){if(k)ImGui::SameLine();ImGui::BeginDisabled(!el.owns(static_cast<Element>(k)));
                        if(ImGui::RadioButton(element_name(static_cast<Element>(k)),&equipped,static_cast<int>(k)))el.equipped=static_cast<Element>(k);ImGui::EndDisabled();}
                }
                ImGui::Text("Energy %.0f / 100   Fury %.0f / 100",el.energy,el.fury);
                if(ImGui::MenuItem("Fill energy and Fury (testing)",nullptr,false,u.enabled)){el.energy=el.fury=100;}
                if(ImGui::Checkbox("Dark Spyro form (free cosmetic)",&cheats.dark_spyro)){game->cheats=cheats;remember_settings();focus_play_viewport=playing;}
                ImGui::SameLine();if(ImGui::Checkbox("Black smoke",&cheats.dark_smoke)){game->cheats=cheats;remember_settings();}
                ImGui::TextWrapped("F breath (hold to continue), Q shot, L Fury, 1-5 select. Controller: hold B (Circle) to breathe, RT shot, LT Fury, D-pad left / right select. Dark Spyro runs faster, jumps higher and glides/swims faster.");
                ImGui::Separator();ImGui::Text("Melee: %s | Carry: %s",u.enabled && u.melee?"ready (J)":"locked",u.enabled && u.carry?"unlocked (G)":"locked");
                ImGui::Text("Base powers: charge %s / flame %s / flight %s",game->powers.charge_enabled(*game)?"ready":"inactive",
                    game->powers.flame_enabled(*game) && !cheats.green_firebomb?"ready":"inactive",game->powers.fly_enabled(*game)?"ready":"inactive");
                ImGui::Separator();ImGui::TextWrapped("G: grab / drop while superflying. Shift+G: throw. J: melee; press again during a strike to chain the combo. Power upgrades still need the original power or its cheat.");
                ImGui::TextWrapped("Tap Space or X again while gliding to use a wing boost. Land to recharge; at most two per glide.");
                remember_settings();
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Universe",wad!=nullptr && !mounted_profile->yotd() && (!online_status.connected || online_status.hosting))) {   // built from Spyro 2 banks
                ImGui::TextDisabled("Procedural worlds / preview");ImGui::SetNextItemWidth(220);
                ImGui::InputScalar("Seed",ImGuiDataType_U64,&universe_seed);ImGui::SetNextItemWidth(220);
                ImGui::InputScalar("World",ImGuiDataType_U64,&universe_world);
                if(ImGui::MenuItem("Land on this world"))action([&]{load_universe({universe_seed,universe_world,false});});
                if(ImGui::MenuItem("Fly the Starways"))action([&]{load_universe({universe_seed,universe_world,true});});
                if(level && level->universe) {
                    ImGui::Separator();ImGui::Text("%s",level->universe->name.c_str());
                    ImGui::Text("Visited worlds: %zu",universe_campaign.universe_worlds.size());
                    if(ImGui::BeginMenu("Revisit a world",!universe_campaign.universe_worlds.empty())) {
                        unsigned shown=0;
                        for(auto it=universe_campaign.universe_worlds.rbegin();it!=universe_campaign.universe_worlds.rend() && shown<30;++it,++shown) {
                            const UniverseAddress address{universe_campaign.universe->seed,it->first,false};
                            if(ImGui::MenuItem(universe_name(address).c_str())){action([&]{load_universe(address);});break;}
                        }
                        ImGui::EndMenu();
                    }
                    if(ImGui::MenuItem("Return to retail campaign"))action([&]{load_level(campaign_ready?campaign.slot:15);play();});
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();ImGui::TextDisabled("60 FPS TARGET");
            ImGui::SameLine(ImGui::GetWindowWidth()-260);ImGui::Text("%.1f fps  |  %.2f ms",io.Framerate,1000.0f/io.Framerate);
            if(ImGui::BeginMenu("Capture")) {
                if(ImGui::MenuItem("Take photo...","F10",false,level!=nullptr))action([&]{choose_capture(false);});
                if(ImGui::MenuItem(video.active()?"Stop and save video":"Record video...","F11",false,level!=nullptr))action([&]{choose_capture(true);});
                ImGui::TextDisabled("60 fps MP4 with game sound");ImGui::EndMenu();
            }
            if(video.active())ImGui::TextColored({1,0.3f,0.25f,1},"REC  %.1f s",video.frames()/60.0);
            ImGui::EndMainMenuBar();
        }
        if(ask_new_game)ImGui::OpenPopup("Start a new game?");
        if(ImGui::BeginPopupModal("Start a new game?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            seconds=0;ImGui::TextUnformatted("Start again? Save first to keep your current progress.");
            if(ImGui::Button("Start new game")){action([&]{new_game();});ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        const auto dock=ImGui::DockSpaceOverViewport(0,ImGui::GetMainViewport());
        if(!ImGui::DockBuilderGetNode(dock)->IsSplitNode()) {
            ImGui::DockBuilderRemoveNode(dock);ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dock,ImGui::GetMainViewport()->WorkSize);
            auto middle=dock;const auto left=ImGui::DockBuilderSplitNode(middle,ImGuiDir_Left,0.22f,nullptr,&middle);
            const auto right=ImGui::DockBuilderSplitNode(middle,ImGuiDir_Right,0.28f,nullptr,&middle);
            const auto bottom=ImGui::DockBuilderSplitNode(middle,ImGuiDir_Down,0.22f,nullptr,&middle);
            ImGui::DockBuilderDockWindow("Retail data",left);ImGui::DockBuilderDockWindow("Viewport",middle);
            ImGui::DockBuilderDockWindow("Inspector",right);ImGui::DockBuilderDockWindow("Session",bottom);
            ImGui::DockBuilderFinish(dock);
        }
        ImGui::Begin("Retail data");
        {   // Which game the workbench runs: each keeps its own copy, saves and settings.
            const bool yotd=mounted_profile?mounted_profile->yotd():spyro3_edition;
            ImGui::SeparatorText("Game");
            if(ImGui::RadioButton("Spyro 2",!yotd) && yotd)action([&]{switch_game(false);});ImGui::SameLine();
            if(ImGui::RadioButton("Spyro 3: Year of the Dragon",yotd) && !yotd)action([&]{switch_game(true);});
            if(yotd && wad && ImGui::Button("Spyro 3 front end"))action([&]{if(level && !playing)play();frontend=true;open_menu(Menu::Title);});
        }
        ImGui::TextWrapped(spyro3()?"Mount your Spyro 3: Year of the Dragon copy":"Mount your game copy");ImGui::SetNextItemWidth(-1);ImGui::InputText("##source",source_path,sizeof(source_path));
        if(ImGui::Button("Mount")) action([&]{mount(from_utf8(source_path));});ImGui::SameLine();if(ImGui::Button("Browse..."))action([&]{browse(window);});
        ImGui::TextDisabled("ISO / CUE / BIN / extracted folder");
        if(assets) {ImGui::Separator();ImGui::TextWrapped("%s",assets->description().c_str());ImGui::Text("%zu disc files  |  %zu WAD slots",assets->files().size(),wad->entries().size());}
        if(wad) {
            ImGui::SeparatorText("Level archives");
            if(ImGui::BeginChild("levels",{0,0},ImGuiChildFlags_Borders)) {
                for(unsigned slot:mounted_profile->levels()) {
                    char label[160];
                    if(mounted_profile->yotd()) {
                        // Grouped by world; a level's extra areas sit under it.
                        if(slot==97 || slot==115 || slot==133 || slot==151 || slot==169) {
                            auto world=slot==169?std::string("Bonus"):game_level_name(executable,slot);
                            if(world.size()>5 && world.ends_with(" Home"))world.resize(world.size()-5);
                            ImGui::SeparatorText(world.c_str());
                        }
                        const auto areas=yotd_area_counts.contains(slot)?yotd_area_counts.at(slot):1u;
                        snprintf(label,sizeof(label),"%s%s##wad%u",game_level_name(executable,slot).c_str(),areas>1?(" ("+std::to_string(areas-1)+(areas==2?" extra area)":" extra areas)")).c_str():"",slot);
                        const bool current=selected_slot==static_cast<int>(slot);
                        if(ImGui::Selectable(label,current && level && !level->source_area))action([&]{load_level(slot);});
                        if(current)for(unsigned a=1;a<areas;++a) {
                            snprintf(label,sizeof(label),"    Area %u##area%u-%u",a,slot,a);
                            if(ImGui::Selectable(label,level && level->source_area==a))action([&,a]{load_yotd_level(slot,true,a);});
                        }
                    } else {
                        snprintf(label,sizeof(label),"WAD %03u    %.2f MiB",slot,wad->entry(slot).size/1048576.0f);
                        if(ImGui::Selectable(label,selected_slot==static_cast<int>(slot)))action([&]{load_level(slot);});
                    }
                }
            }ImGui::EndChild();
        }
        ImGui::End();
        ImGui::Begin("Inspector");
        if(level) {
            if(mounted_profile->yotd() && yotd_areas>1) {
                char current[32];snprintf(current,sizeof(current),yotd_selected_area?"Subarea %u":"Main area",yotd_selected_area);
                if(ImGui::BeginCombo("YOTD area",current)) {
                    for(unsigned a=0;a<yotd_areas;++a){char label[32];snprintf(label,sizeof(label),a?"Subarea %u":"Main area",a);if(ImGui::Selectable(label,yotd_selected_area==a))action([&]{load_yotd_level(level->wad_slot,true,a);});}
                    ImGui::EndCombo();
                }
            }
            if(ImGui::Checkbox("Model studio",&studio)) {object_studio=false;if(studio && playing)stop();}
            if(!object_studio) {
            if(!level->universe && level->custom_name.empty()) {
                if(retail_intro_available && ImGui::Button("Play retail intro"))action([&]{start_cinematic();});
                if(retail_outro_available && ImGui::Button("Play retail outro"))action([&]{start_cinematic(true);});
                if(cinematic_playing && ImGui::Button("Skip cinematic"))finish_cinematic();
            }
            ImGui::SeparatorText("Spyro / retail animation");
            const auto& clip=spyro->animations.at(animation);
            ImGui::Text("%zu vertices  |  %zu faces",clip.frames.front().vertices.size(),clip.faces.size());
            ImGui::Text("%zu clips  |  %u texture palettes",spyro->animations.size(),spyro->texture_tiles);
            char current_clip[80];snprintf(current_clip,sizeof(current_clip),"%02u %s%s",animation,animation_name(animation),clip.level_specific?" (level)":"");
            if(ImGui::BeginCombo("Clip",current_clip)) {
                for(const auto& [id,candidate]:spyro->animations) {
                    char label[80];snprintf(label,sizeof(label),"%02u %s / %zu frames%s",id,animation_name(id),candidate.frames.size(),candidate.level_specific?" / level":"");
                    if(ImGui::Selectable(label,id==animation)) {animation=id;animation_seconds=0;animation_frame=0;}
                }ImGui::EndCombo();
            }
            if(clip.imported_from_slot)ImGui::TextDisabled("Loaded from retail scene bank %u",clip.imported_from_slot);
            ImGui::Checkbox("Animate",&animation_playing);ImGui::SameLine();ImGui::Checkbox("Retail loop",&retail_loop);
            ImGui::SliderFloat("Speed",&animation_speed,0.05f,2.0f,"%.2fx");
            if(ImGui::SliderFloat("Frame",&animation_frame,0,static_cast<float>(spyro->animations.at(animation).frames.size()-1),"%.2f"))animation_playing=false;
            ImGui::Text("Pose %u -> %u / blend %.2f",pose.frame,pose.next_frame,pose.blend);
            ImGui::TextDisabled("Drag right mouse to orbit; wheel to zoom.");
            }
            if(object_studio || ImGui::CollapsingHeader("Animated object models")) {
                if(object_studio)ImGui::SeparatorText("Animated object models");
                char current[64];snprintf(current,sizeof(current),"%03X%s",object_preview.object.type,object_preview.object.type==120?" / Sparx":"");
                if(ImGui::BeginCombo("Object model",object_studio?current:"Choose model")) {
                    for(const auto& [type,bank]:object_models.models)if(bank.animated) {
                        char label[80];snprintf(label,sizeof(label),"%03X%s / %zu clips",type,type==120?" Sparx":"",bank.animations.size());
                        if(ImGui::Selectable(label,object_studio && type==object_preview.object.type))action([&]{view_object_model(type);});
                    }
                    ImGui::EndCombo();
                }
                if(object_studio) {
                    const auto& bank=object_models.models.at(object_preview.object.type);
                    char selected[40];snprintf(selected,sizeof(selected),"Clip %u",object_clip);
                    if(ImGui::BeginCombo("Object clip",selected)) {
                        for(unsigned i=0;i<bank.animations.size();++i) {
                            char label[64];snprintf(label,sizeof(label),"%u / %zu frames",i,bank.animations[i].frames.size());
                            if(ImGui::Selectable(label,i==object_clip))view_object_model(object_preview.object.type,i);
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::Checkbox("Play object animation",&object_animate);
                    if(ImGui::SliderFloat("Object frame",&object_frame,0,static_cast<float>(bank.animations[object_clip].frames.size()-1),"%.2f"))object_animate=false;
                    ImGui::Text("%zu vertices / %zu frames",object_preview.pose.vertices.size(),bank.animations[object_clip].frames.size());
                    ImGui::TextDisabled("Original frame timing / 60 Hz playback");
                    ImGui::TextDisabled("Right mouse to orbit / wheel to zoom");
                    if(ImGui::Button("Return to Spyro")){object_studio=false;model_distance=1.8f;}
                }
            }
            ImGui::SeparatorText("Level / scene");
            ImGui::Text("WAD %03u",level->wad_slot);ImGui::Text("%zu collision triangles",level->triangles.size());
            ImGui::Text("%u visual faces / %u regions",level->terrain_faces,level->terrain_regions);
            ImGui::Text("%zu retail objects / %zu model classes",level->objects.size(),object_models.models.size());
            ImGui::Checkbox("Show collision mesh",&collision);ImGui::Separator();
            if(!playing) {if(ImGui::Button("Play movement test"))play();}
            else {if(ImGui::Button("Stop"))stop();ImGui::SameLine();if(ImGui::Button(game->paused?"Resume":"Pause"))game->set_paused(!game->paused);
                if(game->paused) {ImGui::SameLine();if(ImGui::Button("Step"))game->step_once();}}
            if(level->surface_swimming && ImGui::Button("Play in water"))action([&]{play_in_water();});
            if(ImGui::Button("Frame level")){studio=false;frame_level();}ImGui::SameLine();ImGui::Checkbox("Wireframe",&wire);
            if(ImGui::Button("View spawn"))view_spawn();
            if(ImGui::CollapsingHeader("Audio",ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Mute",&audio.muted);
                ImGui::SliderFloat("Music",&audio.music_volume,0,1,"%.2f");
                ImGui::SliderFloat("Effects",&audio.effects_volume,0,1,"%.2f");
                ImGui::SliderFloat("Voice",&audio.voice_volume,0,1,"%.2f");
                ImGui::TextWrapped("%s",audio.status.c_str());
            }
            if(ImGui::CollapsingHeader("Cheats",ImGuiTreeNodeFlags_DefaultOpen)) {
                if(mounted_profile->prerelease() || mounted_profile->yotd()) {
                    if(ImGui::Button("Choose retail power-up source..."))action([&]{choose_powerup_source(window);});
                    ImGui::TextWrapped("%s",mounted_profile->yotd()?(yotd_donor?"Spyro 2 donor powers and PC interface installed.":powerup_warning.c_str()):(retail_powerups?"Retail power-ups installed for this prototype/demo.":powerup_warning.c_str()));
                }
                const auto power=[&](const char* label,bool& value,bool available){ImGui::BeginDisabled(!available);const bool changed=ImGui::Checkbox(label,&value);ImGui::EndDisabled();return changed;};
                bool changed=power("Supercharge in any level",cheats.supercharge,game->has_animation(61));
                changed|=power("Superflame in any level",cheats.superflame,game->has_object_model(0x1e3));
                changed|=power("Superfly in any level",cheats.superfly,game->has_animation(33));
                changed|=power("Ripto green fire bomb in any level",cheats.green_firebomb,game->has_object_model(0x2c2));
                if(ImGui::Checkbox("Unlock portals, elevators and abilities (session)",&unlock_travel))game->portal_travel.unlock=unlock_travel;
                ImGui::TextWrapped("Travel override opens quest-gated routes without awarding gems, orbs or talismans. Turn it off to restore progression gates.");
                ImGui::TextWrapped("F fires. Green bombs take priority over superflame and break heavy doors. Space jumps; tap again in the air to start Superfly, then tap to flap. W dives, S climbs, A/D steer, Shift ends flight.");
                int appearance=static_cast<int>(cheats.supercharge_appearance);
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled(spyro->main_model_supercharge.frames.empty());
                if(ImGui::Combo("##supercharge_model",&appearance,"Original supercharge mesh\0Spyro 2 main model / random wings\0Spyro 2 main model / regular charge flaps\0")) {
                    cheats.supercharge_appearance=static_cast<SuperchargeAppearance>(appearance);changed=true;
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped("Hold Shift to supercharge. Space jumps. Release Shift to stop.");
                changed|=ImGui::Checkbox("Dark Spyro (cosmetic, boosted movement)",&cheats.dark_spyro);
                {int d=static_cast<int>(cheats.boss_difficulty);
                 if(ImGui::Combo("Crownworks difficulty",&d,"Normal\0Assist (longer tells, full health each phase)\0Hard (overdrive always on)\0")){cheats.boss_difficulty=static_cast<BossDifficulty>(d);changed=true;}}
                changed|=ImGui::Checkbox("Crownworks cutscenes",&cheats.boss_cinematics);
                ImGui::BeginDisabled(!cheats.dark_spyro);changed|=ImGui::Checkbox("Dark Spyro black smoke",&cheats.dark_smoke);ImGui::EndDisabled();
                game->cheats=cheats;if(changed){remember_settings();focus_play_viewport=playing;}
            }
            if(ImGui::CollapsingHeader("Terrain destruction")) {
                auto& d=game->destruction_settings;
                ImGui::Checkbox("Dynamic destruction",&d.enabled);
                ImGui::Checkbox("Supercharge dents and cracks",&d.charge);
                ImGui::Checkbox("Superflame removes chunks",&d.flame);
                ImGui::Checkbox("Green bombs remove chunks",&d.bomb);
                ImGui::Checkbox("Protect gameplay surfaces and supports",&d.protect_gameplay);
                int strength=static_cast<int>(d.strength);if(ImGui::SliderInt("Impact strength",&strength,1,3))d.strength=static_cast<unsigned>(strength);
                ImGui::SeparatorText("Convexity breath");
                ImGui::Checkbox("Void Beam slices terrain (held 0.6 s)",&d.void_slice);
                ImGui::Checkbox("Void Beam scars terrain",&d.void_erode);
                int size=static_cast<int>(d.slice_size);if(ImGui::SliderInt("Cut size (2 / 3 / 4.2 units)",&size,1,3))d.slice_size=static_cast<unsigned>(size);
                ImGui::TextWrapped("Hold the Void Beam on a wall or tower to cut it: the part above the cut falls and lands as solid ground. Sweep the beam while holding to angle the cut. Independent of Dynamic destruction.");
                if(ImGui::Button("Restore current terrain"))game->destruction.clear(*game);
                ImGui::TextWrapped("Changes are saved per level. Turning this off stops new damage; Restore removes this level's destruction. Headbash craters remain.");
                remember_settings();
            }
            if(ImGui::CollapsingHeader("Camera / world scale")) {
                const auto& p=game->camera.parameters();
                ImGui::Text("Retail scale: 1:1 model / terrain");
                ImGui::Text("1024 retail units = 1 world unit");
                ImGui::Text("Origin above feet: %d units",retail::spyro_origin_units);
                ImGui::Text("Camera radius: %d units",p[2]);
                ImGui::Text("Elevation: %.2f deg",p[1]*360.0f/4096);
                ImGui::Text("Vertical FOV: %.2f deg",camera_settings.vertical_fov()*180/std::numbers::pi_v<float>);
                if(playing && game->camera.obstructed())ImGui::TextDisabled("Camera adjusted for nearby geometry");
            }
            if(ImGui::CollapsingHeader("Retail objects")) {
                ImGui::BeginChild("object_records",{0,150},ImGuiChildFlags_Borders);
                ImGuiListClipper clipper;clipper.Begin(static_cast<int>(level->objects.size()));
                while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
                    const auto& object=level->objects[i];char label[90];
                    if(object.type==1)snprintf(label,sizeof(label),"%03u  Gem / value %u",object.id,object.gem_value);
                    else snprintf(label,sizeof(label),"%03u  Class %03X",object.id,object.type);
                    if(ImGui::Selectable(label,selected_object==i))selected_object=i;
                }
                ImGui::EndChild();
                if(selected_object>=0 && static_cast<std::size_t>(selected_object)<level->objects.size()) {
                    const auto& object=level->objects[selected_object];
                    ImGui::Text("Record %u / class %03X / mesh %u",object.id,object.type,object.model);
                    ImGui::Text("Position %.2f  %.2f  %.2f",object.position.x,object.position.y,object.position.z);
                    if(object.type==1)ImGui::Text("Gem value %u / material %u",object.gem_value,object.material_color);
                    const auto found=object_models.models.find(object.type);
                    if(found==object_models.models.end())ImGui::TextDisabled("No model bank for this record");
                    else if(found->second.animated) {
                        ImGui::Text("%zu animation clips",found->second.animations.size());
                        if(ImGui::Button("Inspect model clips"))view_object_model(object.type,object.model);
                    }
                    else ImGui::Text("%zu static mesh variants",found->second.meshes.size());
                    if(ImGui::Button("View object"))view_object(static_cast<unsigned>(selected_object));
                    if(is_breakable(object.type) && ImGui::Button("Play beside container"))action([&]{play_beside_object(static_cast<unsigned>(selected_object));});
                    if(is_butterfly_source(object.type) && ImGui::Button("Play beside animal"))action([&]{play_beside_object(static_cast<unsigned>(selected_object));});
                    if(std::any_of(level->npcs.begin(),level->npcs.end(),[&](const auto& npc){return npc.object==static_cast<unsigned>(selected_object);}) &&
                       ImGui::Button("Play beside NPC"))action([&]{play_beside_object(static_cast<unsigned>(selected_object));});
                    if(object.enemy) {
                        if(ImGui::Button("Play beside enemy"))action([&]{play_beside_object(static_cast<unsigned>(selected_object));});
                        const auto& current=playing?game->retail_objects[selected_object]:object;
                        ImGui::Text("%s | %zu route points",enemy_phase_name(current),object.enemy->route.size());
                    }
                    ImGui::TextDisabled("Retail records are read-only.");
                }
            }
            ImGui::SeparatorText("Scene project");
            if(level->universe)ImGui::TextWrapped("Use Game > Save game to keep this universe. Generated terrain editing is not available yet.");
            if(!level->custom_name.empty())ImGui::TextWrapped("Edit this custom world in Blender and export its .s2world file.");
            ImGui::BeginDisabled(level->universe.has_value() || !level->custom_name.empty());
            ImGui::SetNextItemWidth(-1);ImGui::InputText("##project",project_path,sizeof(project_path));
            if(ImGui::Button("Save")) action([&]{document.scene.save(from_utf8(project_path));scene_dirty=false;status="Scene project saved.";});
            ImGui::SameLine();if(ImGui::Button("Load")) action([&]{
                auto scene=Scene::load(from_utf8(project_path));
                if(scene.retail_crc!=mounted_profile->crc || !wad->contains(scene.wad_slot) || scene.wad_slot%2==0)throw FormatError("Scene belongs to another game build or an unavailable level");
                load_level(scene.wad_slot);document.replace(scene);game=std::make_unique<Game>(*level,scene,camera_settings,&object_models,spyro.get());game->upgrades=current_campaign().upgrades;game->cheats=cheats;game->set_paused(true);status="Scene project loaded.";
            });
            if(ImGui::Button("Undo")){document.undo();scene_dirty=true;}ImGui::SameLine();if(ImGui::Button("Redo")){document.redo();scene_dirty=true;}
            if(scene_dirty)ImGui::TextColored({1,0.75f,0.3f,1},"Unsaved scene changes");
            ImGui::Text("Spawn: %.2f  %.2f  %.2f",document.scene.spawn.x,document.scene.spawn.y,document.scene.spawn.z);
            if(ImGui::Button("Set spawn on selected triangle") && selected_triangle<level->triangles.size()) {
                auto next=document.scene;const auto& tri=level->triangles[selected_triangle];next.spawn=(tri.a+tri.b+tri.c)/3+Vec3{0,0.03f,0};document.edit(next);scene_dirty=true;
            }
            if(ImGui::Button("Add test gem at spawn")) {
                auto next=document.scene;unsigned id=1;for(auto& e:next.entities)id=std::max(id,e.id+1);
                next.entities.push_back({id,EntityKind::Gem,next.spawn+Vec3{1,0.7f,0},1});document.edit(next);scene_dirty=true;
            }
            for(std::size_t i=0;i<document.scene.entities.size();++i) {
                char label[64];snprintf(label,sizeof(label),"Gem / marker %u",document.scene.entities[i].id);
                if(ImGui::Selectable(label,selected_entity==static_cast<int>(i)))selected_entity=static_cast<int>(i);
            }
            if(selected_entity>=0 && static_cast<std::size_t>(selected_entity)<document.scene.entities.size()) {
                auto next=document.scene;auto& e=next.entities[selected_entity];
                if(ImGui::DragFloat3("Position",&e.position.x,0.1f)) {document.edit(next);scene_dirty=true;}
                if(ImGui::Button("Delete selected")) {next.entities.erase(next.entities.begin()+selected_entity);document.edit(next);selected_entity=-1;scene_dirty=true;}
            }
            if(selected_triangle!=UINT32_MAX)ImGui::Text("Selected retail triangle: %u",selected_triangle);
            ImGui::EndDisabled();
        } else ImGui::TextWrapped("Mount a retail game copy to inspect its archives and geometry.");
        ImGui::End();
        ImGui::Begin("Session");
        ImGui::TextWrapped("%s",status.c_str());
        if(!error.empty()) {ImGui::TextColored({1,0.4f,0.4f,1},"%s",error.c_str());if(ImGui::Button("Dismiss"))error.clear();}
        if(game && playing) {ImGui::Text("Gems %u    Lives %u    State %s    Simulation ticks %llu",game->player.gems,game->player.lives,motion_name(game->player.motion),game->clock.ticks());
            ImGui::Text("Position %.2f, %.2f, %.2f    60 Hz simulation    Dropped time %.3f s",game->player.position.x,game->player.position.y,game->player.position.z,game->clock.dropped_seconds());
            if(game->sparx.object_index())ImGui::Text("Sparx: %s    Health %u    Gems delivered %u",
                !game->player.health?"Absent":game->sparx.target_id()?"Collecting":"Following",game->player.health,game->sparx.delivered_gems());}
        ImGui::TextWrapped("WASD move / Shift charge / Space or X: jump, tap again to glide / H: hover while gliding, headbash in air, hold while standing to look around / B: headbash / F flame / E talk / C center camera / R respawn");
        ImGui::TextWrapped("Controller (plays the game only, whichever panel has focus): left stick move / A jump, glide / X charge / B flame / Y talk, hover, headbash, hold while standing to look / "
            "right stick camera / LB RB roll / RT shot / LT Fury / D-pad left right element, up melee, down carry (X + down throws) / RS click center camera / Back HUD / Start pause.%s",
            pad.connected()?"  Connected.":"  None connected.");
        if(game && playing && game->player.swim.mode!=WaterMode::Dry)
            ImGui::TextWrapped("Water: WASD paddles on the surface; Shift dives, Space jumps out. Underwater: A/D turn, W dives, S rises, Space swims, Shift charges.");
        ImGui::TextDisabled("Editor: right mouse + WASD fly, Q/E down/up, Shift fast, left click select triangle.");
        ImGui::TextWrapped("F5 saves; F9 loads; F3 FPS counter; Tab recalls the HUD; hold T for Sparx's gem hint. Mobys without a native reconstruction run their original level scripts in the overlay interpreter.");
        if(game && game->overlay_scripts.active())ImGui::Text("Retail scripts: %u mobys, %u spawned, %u hits on Spyro%s",game->overlay_scripts.owned(),game->overlay_scripts.spawned,game->overlay_scripts.hits,
            game->overlay_scripts.faults?"  (interpreter faults)":"");
        ImGui::End();
        viewport(seconds);
    }
    void viewport(double seconds) {
        const bool full_view=capture_fullscreen || frontend || menu!=Menu::None;
        const bool controls=menu==Menu::None && !suspend_frames;
        if(controls && focus_play_viewport && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)) {
            ImGui::SetNextWindowFocus();focus_play_viewport=false;
        }
        if(full_view) {
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
        }
        ImGui::Begin("Viewport",nullptr,full_view?(ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings):0);
        if(!level) {ImGui::TextDisabled("Retail level viewport");ImGui::End();if(full_view)ImGui::PopStyleVar(2);return;}
        const auto size=ImGui::GetContentRegionAvail();const auto origin=ImGui::GetCursorScreenPos();
        capture_area={static_cast<unsigned>(std::max(0.0f,origin.x)),static_cast<unsigned>(std::max(0.0f,origin.y)),
            static_cast<unsigned>(std::max(1.0f,size.x)),static_cast<unsigned>(std::max(1.0f,size.y))};
        if(cinematic_playing && cinematic_ticks>=cinematic->duration)finish_cinematic();
        if(cinematic_playing){cinematic_view(seconds,size,origin);ImGui::End();if(full_view)ImGui::PopStyleVar(2);return;}
        renderer.resize(static_cast<unsigned>(std::max(1.0f,size.x)),static_cast<unsigned>(std::max(1.0f,size.y)));
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(renderer.image())),size);
        const bool hovered=ImGui::IsItemHovered();const bool focused=ImGui::IsWindowFocused();auto& io=ImGui::GetIO();
        if(studio) {
            if(hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                model_yaw-=io.MouseDelta.x*0.008f;model_pitch=std::clamp(model_pitch+io.MouseDelta.y*0.008f,-1.2f,1.4f);
            }
            if(hovered)model_distance=std::clamp(model_distance*std::exp(-io.MouseWheel*0.15f),0.6f,object_studio?100.0f:12.0f);
            camera={std::sin(model_yaw)*std::cos(model_pitch)*model_distance,std::sin(model_pitch)*model_distance,
                    std::cos(model_yaw)*std::cos(model_pitch)*model_distance};target={0,0,0};
        } else if(playing) {
            InputState input;
            if(controls && focused && !io.WantTextInput) {
                input.move_x=static_cast<float>(ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow))-
                    static_cast<float>(ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow));
                input.move_z=static_cast<float>(ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow))-
                    static_cast<float>(ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow));
                input.charge=io.KeyShift;
                input.jump_held=ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_X);
                input.jump_pressed=ImGui::IsKeyPressed(ImGuiKey_Space,false) || ImGui::IsKeyPressed(ImGuiKey_X,false);
                input.special_pressed=ImGui::IsKeyPressed(ImGuiKey_H,false);
                input.look_held=ImGui::IsKeyDown(ImGuiKey_H);   // Triangle: look mode while standing
                input.headbash_pressed=ImGui::IsKeyPressed(ImGuiKey_B,false);
                input.roll_left_pressed=ImGui::IsKeyPressed(ImGuiKey_Z,false);
                input.roll_right_pressed=ImGui::IsKeyPressed(ImGuiKey_V,false);
                input.roll_left_held=ImGui::IsKeyDown(ImGuiKey_Z);
                input.roll_right_held=ImGui::IsKeyDown(ImGuiKey_V);
                input.cycle_character=!online.status().connected && ImGui::IsKeyPressed(ImGuiKey_F6,false);
                input.flame_pressed=ImGui::IsKeyPressed(ImGuiKey_F,false);if(ImGui::IsKeyPressed(ImGuiKey_R,false))game->respawn();
                // Elemental powers: hold F to keep breathing, Q shot, L Fury,
                // 1-5 select outside dialogue (the controller: GamePad::play).
                input.flame_held=ImGui::IsKeyDown(ImGuiKey_F);
                input.shot_pressed=ImGui::IsKeyPressed(ImGuiKey_Q,false);
                input.fury_pressed=ImGui::IsKeyPressed(ImGuiKey_L,false);
                input.fury_held=ImGui::IsKeyDown(ImGuiKey_L);
                if(!game->dialogue.active())for(int k=0;k<5;++k)if(ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1+k),false))input.element_select=k;
                const bool grab=ImGui::IsKeyPressed(ImGuiKey_G,false);input.carry_pressed=grab && !io.KeyShift;input.throw_pressed=grab && io.KeyShift;
                input.dialogue.interact=ImGui::IsKeyPressed(ImGuiKey_E,false);
                input.dialogue.next=ImGui::IsKeyPressed(ImGuiKey_Enter,false);
                input.dialogue.cancel=ImGui::IsKeyPressed(ImGuiKey_Escape,false);
                if(ImGui::IsKeyPressed(ImGuiKey_1,false))input.dialogue.choice=0;
                if(ImGui::IsKeyPressed(ImGuiKey_2,false))input.dialogue.choice=1;
                if(ImGui::IsKeyPressed(ImGuiKey_3,false))input.dialogue.choice=2;
                input.center_camera=ImGui::IsKeyPressed(ImGuiKey_C,false);
                input.gem_hint=ImGui::IsKeyDown(ImGuiKey_T);
                if(input.gem_hint)input.roll_left_pressed=input.roll_right_pressed=input.roll_left_held=input.roll_right_held=input.shot_pressed=false;
            }
            // An ability shortcut still works after closing an inspector or
            // upgrade menu; typing into a field or open popup owns the key.
            if(controls && !io.WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)) {
                input.melee_pressed=ImGui::IsKeyPressed(ImGuiKey_J,false);
                const bool grab=ImGui::IsKeyPressed(ImGuiKey_G,false);input.carry_pressed=grab && !io.KeyShift;input.throw_pressed=grab && io.KeyShift;
                if(input.melee_pressed && !game->playable_boss.active() && (!game->upgrades.enabled || !game->upgrades.melee))status="Melee is locked. Buy it in Upgrades, or choose Unlock all and activate powers.";
                if(grab && (!game->upgrades.enabled || !game->upgrades.carry))status="Carrying is locked. Buy it in Upgrades, or choose Unlock all and activate powers.";
                else if(grab && !game->powers.flying)status="Carrying needs active Superfly. Enable it in Cheats, jump with Space/X, then tap again in the air. G approaches and grabs a nearby container.";
            }
            input.recall_hud=!io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_Tab);
            // The controller plays whichever workbench panel has focus: it has
            // no other use outside the game's own menus.
            if(controls)pad.play(input);
            if(replay_inputs) {
                const auto tick=game->clock.ticks()%360;
                input.move_z=tick>=60 && tick<300?1.0f:0.0f;input.charge=tick>=120 && tick<180;
                input.jump_pressed=tick==180 || tick==210;input.jump_held=tick>=180 && tick<255;input.flame_pressed=tick==80;
                if(cheats.supercharge) {input.jump_pressed=tick==145;input.jump_held=tick>=145 && tick<147;}
                if(cheats.green_firebomb || cheats.superflame)input.flame_pressed=tick%45==15;
                // Superfly: jump, a second press in the air takes off, then a flap a second.
                if(cheats.superfly) {input.charge=false;input.jump_pressed=tick==15 || tick==35 || (tick>=90 && tick%60==30);input.jump_held=false;input.move_z=tick<70?-0.5f:0;input.move_x=tick>120?0.25f:0;}
                if(tick>=220 && tick<255)input.move_z=tick>=240?-1.0f:0.0f;
            }
            // Resolve the camera basis inside each simulation tick, including
            // catch-up ticks, so controls do not depend on presentation rate.
            input.camera_relative=true;
            if(swim_replay) {
                const auto tick=game->clock.ticks();input={};
                if(tick>=60 && tick<110) {
                    input.move_x=std::sin(game->player.yaw);input.move_z=std::cos(game->player.yaw);
                }
                input.charge=(tick>=120 && tick<123) || (tick>=360 && tick<395);
                input.jump_held=(tick>=300 && tick<360) || (tick>=540 && tick<700);
                if(tick>=540 && tick<700)input.move_z=-1;
                if((cheats.superflame || cheats.green_firebomb) && (tick==180 || tick==240))input.flame_pressed=true;
            }
            if(interaction_record!=UINT32_MAX) {
                const auto tick=game->clock.ticks();input={};
                const bool enemy_replay=level->objects[interaction_record].enemy.has_value() || game->retail_objects[interaction_record].retail_script;
                if(interaction_charge && tick>=60 && tick<(enemy_replay?120u:72u)) {
                    input.move_x=interaction_direction.x;input.move_z=interaction_direction.z;input.charge=true;
                    if(enemy_replay) {
                        const auto& enemy_target=game->retail_objects[interaction_record];
                        const auto delta=enemy_target.position-game->player.position;const auto heading=std::atan2(delta.x,delta.z);
                        if(tick==60){input.move_x=std::sin(heading);input.move_z=std::cos(heading);}
                        else {input.move_x=std::clamp(std::remainder(heading-game->player.yaw,2*std::numbers::pi_v<float>)*4,-1.0f,1.0f);input.move_z=1;}
                        if(enemy_target.state<0 || enemy_target.collected)input={};
                    }
                }
                if(!interaction_charge && !interaction_passive && tick==60)input.flame_pressed=true;
                if(!interaction_charge && !interaction_passive && enemy_replay && tick>=40 && tick<240) {
                    // Script-driven enemies move: keep facing, close to flame range, breathe every half second.
                    const auto& enemy_target=game->retail_objects[interaction_record];
                    const auto delta=enemy_target.position-game->player.position;const auto heading=std::atan2(delta.x,delta.z);
                    const auto distance=std::hypot(delta.x,delta.z);
                    if(distance>1.1f){input.move_x=std::sin(heading)*.6f;input.move_z=std::cos(heading)*.6f;}
                    else if(std::abs(std::remainder(heading-game->player.yaw,2*std::numbers::pi_v<float>))>.15f){input.move_x=std::sin(heading)*.05f;input.move_z=std::cos(heading)*.05f;}
                    input.flame_pressed=tick%30==0;
                    // Validation only: hits would cancel the breath input being measured.
                    game->player.invulnerable_ticks=std::max(game->player.invulnerable_ticks,2u);
                    if(enemy_target.state<0 || enemy_target.collected)input={};
                }
                if(ability_replay=="melee"){
                    input.flame_pressed=false;input.melee_pressed=tick==60 || tick==75 || tick==102;
                    if(tick>=10 && tick<27){input.move_x=interaction_direction.x;input.move_z=interaction_direction.z;}
                }
                if(ability_replay=="carry" || ability_replay=="carry-throw" || ability_replay=="carry-inspect") {
                    input.flame_pressed=false;input.move_z=tick<45?-0.25f:0;
                    input.carry_pressed=tick==1 || (tick==70 && ability_replay=="carry");input.throw_pressed=tick==70 && ability_replay=="carry-throw";
                }
                if(std::any_of(level->npcs.begin(),level->npcs.end(),[&](const auto& npc){return npc.object==interaction_record;})) {input.flame_pressed=false;input.dialogue.interact=tick==60 && !game->dialogue.active();}
                if(interaction_talk) {
                    input={};game->player.invulnerable_ticks=std::max(game->player.invulnerable_ticks,2u);
                    if(talk_purchases==UINT32_MAX)talk_purchases=game->purchases;
                    const bool paid=game->purchases!=talk_purchases;
                    if(!game->dialogue.active())input.dialogue.interact=tick>=40 && tick%8==0 && !paid;
                    // Approach: walk up to the speaker first, as a player would (Zoe
                    // ignores a Spyro who was already beside her when she woke).
                    if(interaction_approach && !game->dialogue.active() && interaction_record<game->retail_objects.size()) {
                        auto d=game->retail_objects[interaction_record].position-game->player.position;d.y=0;
                        if(tick>=30 && length(d)>1.1f){d=normalized(d);input.move_x=d.x*.6f;input.move_z=d.z*.6f;}
                    }
                    else if(++talk_open_ticks>=90 && tick%15==0) {
                        // Let each line open and type out before answering, as a reader would.
                        talk_open_ticks=0;
                        const auto* line=game->dialogue.line();
                        if(line && line->yes>=0 && line->no>=0)input.dialogue.choice=0;else input.dialogue.next=true;
                    }
                }
                if(interaction_fetch && interaction_record<game->retail_objects.size() && socket_record<game->retail_objects.size()) {
                    input={};game->player.invulnerable_ticks=std::max(game->player.invulnerable_ticks,2u);
                    const auto toward=[&](Vec3 at,float stop,float speed) {
                        auto d=at-game->player.position;d.y=0;const float n=length(d);
                        if(n<=stop)return true;
                        input.move_x=d.x/n*speed;input.move_z=d.z/n*speed;return false;
                    };
                    const auto head=game->overlay_scripts.peek(0x8006a15c);
                    if(tick>=150 && !fetch_spat) {
                        if(head==0)toward(game->retail_objects[interaction_record].position,.75f,.4f);
                        else if(head==3 && toward(game->retail_objects[socket_record].position,2.0f,.6f)){input.flame_pressed=true;fetch_spat=true;}
                    }
                }
                for(const auto [type,from]:attack_classes)if(tick>=from && (tick-from)%30==0)
                    for(auto& o:game->retail_objects)if(o.type==type && o.state>=0 && !o.collected)o.attack_flags|=superflame_attack;
                if(bridge_replay){input.dialogue.choice=(tick==180 || tick==240 || tick==300)?0:-1;input.dialogue.next=tick==420;}
            }
            if(!movement_replay.empty()) {
                input={};const auto tick=game->clock.ticks();
                if(movement_replay=="aerial") {
                    input.jump_pressed=tick==1 || tick==60 || tick==108 || tick==160;
                    input.special_pressed=tick==205;input.headbash_pressed=tick==232;
                } else if(movement_replay=="headbash") {
                    input.jump_pressed=tick==30;input.headbash_pressed=tick==44;
                } else if(movement_replay=="charge-jump" || movement_replay=="jump-charge") {
                    // Ordinary gameplay inputs with all charge powerups off.
                    input.charge=tick>=(movement_replay=="charge-jump"?30u:70u) && tick<120;
                    input.jump_pressed=tick==60;input.jump_held=tick>=60 && tick<120;
                }
            }
            if(!element_replay.empty()) {
                // Elemental capture driver: real inputs through the controller.
                input={};const auto tick=game->clock.ticks();
                if(tick==2) {
                    float best=14;std::optional<Vec3> aim;
                    for(const auto& o:game->retail_objects)if(element_target(o) && (o.enemy || o.power_target)){const float d=length(o.position-game->player.position);if(d<best){best=d;aim=o.position;}}
                    // --interaction-record names the target (a crate, a door) explicitly.
                    if(interaction_record<game->retail_objects.size())aim=game->retail_objects[interaction_record].position;
                    if(aim){const auto d=*aim-game->player.position;game->player.yaw=game->player.previous_yaw=std::atan2(d.x,d.z);}
                }
                if(element_replay=="breath"){input.flame_pressed=tick==40 || tick==170;input.flame_held=(tick>=40 && tick<130) || (tick>=170 && tick<215);}
                else if(element_replay=="shot")input.shot_pressed=tick==40 || tick==100 || tick==160 || tick==220;
                else if(element_replay=="headbash"){input.jump_pressed=tick==40 || tick==170;input.jump_held=(tick>=40 && tick<52) || (tick>=170 && tick<182);input.headbash_pressed=tick==54 || tick==184;}
                else if(element_replay=="fury"){if(tick==30)game->upgrades.elements.fury=100;input.fury_pressed=tick==40;input.fury_held=tick>=40 && tick<200;if(game->elements.dark_fury()){input.move_z=tick>120 && tick<220?1.0f:0;input.flame_pressed=tick==240;input.flame_held=tick>=240 && tick<300;}}
                else if(element_replay=="melee")input.melee_pressed=tick==40 || tick==55 || tick==82;
                else if(element_replay=="demons"){input.jump_pressed=tick==40;input.jump_held=tick>=40 && tick<52;input.headbash_pressed=tick==54;
                    input.shot_pressed=tick==140;input.flame_pressed=tick==200;input.flame_held=tick>=200 && tick<260;}
                else if(element_replay=="tour") {
                    const unsigned local=tick%300;input.element_select=local==1?static_cast<int>((tick/300)%element_count):-1;
                    if(local==2)game->upgrades.elements.energy=100;
                    input.flame_pressed=local==20;input.flame_held=local>=20 && local<80;input.shot_pressed=local==110;
                    input.jump_pressed=local==160;input.jump_held=local>=160 && local<172;input.headbash_pressed=local==174;
                }
                else if(element_replay=="slice") {
                    // Face the nearest wall within 8 units and hold the Void Beam on it.
                    if(tick==3) {
                        float best=8;std::optional<float> heading;const auto mouth=game->player.position+Vec3{0,.55f,0};
                        for(unsigned k=0;k<32;++k){const float a=k*std::numbers::pi_v<float>/16;
                            if(const auto wall=game->level.raycast(mouth,{std::sin(a),0,std::cos(a)},best);wall && std::abs(wall->normal.y)<.5f){best=wall->distance;heading=a;}}
                        if(heading)game->player.yaw=game->player.previous_yaw=*heading;
                    }
                    input.flame_pressed=tick==40 || tick==190;input.flame_held=(tick>=40 && tick<120) || (tick>=190 && tick<270);
                }
                else if(element_replay=="slice-ground") {
                    // Validation: the Void Beam's ground cut 3.2 units ahead (as when
                    // it meets a slope); the camera then watches the pieces settle.
                    // Protected ground (special surfaces, guards) is refused, so try
                    // a few spots around Spyro, nearest the way he faces first.
                    const auto cut=[&](float yaw,float distance) {
                        const Vec3 ahead{std::sin(yaw),0,std::cos(yaw)};const auto mouth=game->player.position+Vec3{0,.55f,0};
                        const auto ground=game->level.raycast(game->player.position+ahead*distance+Vec3{0,1.5f,0},{0,-1,0},4);
                        if(!ground || !game->destruction.slice(*game,mouth,ground->point,normalized(ground->point-mouth),{},&object_models,ground->normal))return false;
                        game->player.yaw=game->player.previous_yaw=yaw;game->camera.reset(*level,game->player.position,yaw);return true;
                    };
                    if(tick==40)[&]{for(const float turn:{0.0f,.6f,-.6f,1.2f,-1.2f,3.14f})for(const float distance:{3.2f,4.5f,6.0f})
                        if(cut(game->player.yaw+turn,distance))return;}();
                }
                else if(element_replay=="dark-run"){input.move_z=tick>=30 && tick<260?1.0f:0;input.charge=tick>=150 && tick<200;
                    input.jump_pressed=tick==90 || tick==210;input.jump_held=(tick>=90 && tick<120) || (tick>=210 && tick<240);}
            }
            input.dialogue.interact|=dialogue_input.interact;input.dialogue.next|=dialogue_input.next;input.dialogue.cancel|=dialogue_input.cancel;
            if(universe_replay && level->universe) {
                input={};const auto& world=*level->universe;input.camera_relative=false;
                if(!world.address.space) {
                    while(universe_route_target<world.route.size() && length(world.route[universe_route_target]-game->player.position)<0.6f)++universe_route_target;
                    const auto goal=universe_route_target<world.route.size()?world.route[universe_route_target]:world.route.back()+Vec3{0,0,6};
                    const auto d=normalized(Vec3{goal.x-game->player.position.x,0,goal.z-game->player.position.z});input.move_x=d.x;input.move_z=d.z;
                }else {
                    const auto d=world.portals[1].center-game->player.position-retail::origin_above_feet;
                    const auto heading=std::atan2(d.x,d.z),flight_pitch=std::atan2(d.y,std::hypot(d.x,d.z));
                    input.move_x=std::clamp(std::remainder(heading-game->player.yaw,2*std::numbers::pi_v<float>)*4,-1.0f,1.0f);
                    input.move_z=std::clamp((game->player.pitch-flight_pitch)*4,-1.0f,1.0f);
                }
            }
            // A fetch replay keeps its own input and films through the shot's cameras.
            if(capture_shot && !interaction_fetch)input=capture_shot->input(*game);
            if(tunnel_replay)input=tunnel_route.input(*game);
            if(dialogue_input.choice>=0)input.dialogue.choice=dialogue_input.choice;dialogue_input={};
            if(!controls)input={};
            if(scripted_input)scripted_input(input);
            if(controls)filter_dialogue_input(input.dialogue);
            const auto simulation_start=std::chrono::steady_clock::now();
            game->camera.recentre_delay=preferences.camera_recentre_seconds*60;
            if(force_hud_recall)input.recall_hud=true;   // --recall-hud: hold Tab for the whole run (HUD photos)
            game->advance(seconds,input);
            simulation_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-simulation_start).count();
            selected_character=game->playable_boss.character();
            const auto view=game->camera.sample(game->clock.alpha());
            camera=view.eye;target=view.eye+view.forward;
            observed_motions|=1u<<static_cast<unsigned>(game->player.motion);
        } else {
            auto forward=Vec3{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
            auto right=normalized(cross(Vec3{0,1,0},forward));
            if(hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                yaw+=io.MouseDelta.x*0.004f;pitch=std::clamp(pitch-io.MouseDelta.y*0.004f,-1.5f,1.5f);
                Vec3 motion{};
                if(ImGui::IsKeyDown(ImGuiKey_W))motion+=forward;if(ImGui::IsKeyDown(ImGuiKey_S))motion+=forward*-1;
                if(ImGui::IsKeyDown(ImGuiKey_D))motion+=right;if(ImGui::IsKeyDown(ImGuiKey_A))motion+=right*-1;
                if(ImGui::IsKeyDown(ImGuiKey_E))motion.y+=1;if(ImGui::IsKeyDown(ImGuiKey_Q))motion.y-=1;
                camera+=normalized(motion)*(fly_speed*static_cast<float>(std::min(seconds,0.1))*(io.KeyShift?3.0f:1.0f));
            }
            forward={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};target=camera+forward;
            if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const auto up=normalized(cross(forward,right));const float tangent=std::tan(camera_settings.vertical_fov()/2);
                const float nx=((io.MousePos.x-origin.x)/std::max(size.x,1.0f)*2-1)*tangent*size.x/std::max(size.y,1.0f);
                const float ny=(1-(io.MousePos.y-origin.y)/std::max(size.y,1.0f)*2)*tangent;
                if(const auto hit=level->raycast(camera,forward+right*nx+up*ny,1500,true);hit && hit->triangle<level->triangles.size())selected_triangle=hit->triangle;
            }
        }
        if(playing) {
            // The game advances the retail transition state once per VBlank;
            // rendering samples its two states without restarting either clip.
            animation=game->animation.state().clip;
            animation_seconds=(game->player.motion_ticks+game->clock.alpha())*FixedClock::step;
            pose=game->animation.sample(game->clock.alpha(),cheats.supercharge_appearance);
            pose=spyro->secondary_track(animation,std::move(pose),game->secondary.sample(game->clock.alpha(),cheats.supercharge_appearance),cheats.supercharge_appearance);
            if(game->abilities.attacking() && pose.vertices.size()!=230){animation=0;pose=spyro->frame(0,0);}
            game->abilities.pose(pose,game->clock.alpha(),game.get(),spyro.get());
            {   // Look mode's head turn (800308d4), between ticks.
                const auto& head=game->player.head;const float a=game->clock.alpha();
                spyro->turn_head(pose,animation,std::lerp(float(head.previous_yaw),float(head.yaw),a)*retail::angle_to_radians,
                    std::lerp(float(head.previous_pitch),float(head.pitch),a)*retail::angle_to_radians,cheats.supercharge_appearance);
            }
            OnlineState state;state.world=online_world(*level);state.position=game->player.position;state.yaw=game->player.yaw;
            state.pitch=game->player.pitch;state.roll=game->player.roll;state.clip=animation;state.frame=pose.frame+pose.blend;
            state.appearance=static_cast<unsigned>(cheats.supercharge_appearance);state.combo=game->abilities.combo;state.combo_tick=game->abilities.combo_tick;
            state.form=game->elements.dark(*game)?1:0;state.element=static_cast<unsigned>(game->upgrades.elements.equipped);
            online.publish(state);remote_spyros.clear();
            for(const auto& remote:online_ghosts.update(online_status,state.world,seconds)) {
                if(!spyro->animations.contains(remote.clip))continue;
                const auto appearance=static_cast<SuperchargeAppearance>(remote.appearance);
                const auto& clip=spyro->clip(remote.clip,appearance);if(clip.frames.empty() || remote.frame>=clip.frames.size())continue;
                RemoteSpyro ghost;ghost.peer=remote.peer;ghost.clip=remote.clip;ghost.appearance=appearance;
                ghost.dark=remote.form==1;ghost.element=static_cast<Element>(remote.element);
                ghost.player.position=remote.position;ghost.player.yaw=remote.yaw;ghost.player.pitch=remote.pitch;ghost.player.roll=remote.roll;
                ghost.pose=spyro->frame(remote.clip,remote.frame,appearance);
                Abilities gesture;gesture.combo=remote.combo;gesture.combo_tick=remote.combo_tick;gesture.pose(ghost.pose,1,nullptr,spyro.get());
                remote_spyros.push_back(std::move(ghost));
            }
        } else if(animation_playing) {
            animation_seconds+=std::min(seconds,0.25)*animation_speed;
            pose=spyro->sample(animation,animation_seconds,retail_loop,cheats.supercharge_appearance);animation_frame=pose.frame+pose.blend;
        } else pose=spyro->frame(animation,animation_frame,cheats.supercharge_appearance);
        if(!playing) {
            object_clock.advance(seconds,[&]{
                tick_object_animations(preview_objects,object_models);
                if(object_studio && object_animate) {
                    object_preview.object.previous_animation=object_preview.object.animation;
                    tick_object_animation(object_models.models.at(object_preview.object.type),object_preview.object.animation);
                }
            });
            if(studio && object_studio) {
                const auto& bank=object_models.models.at(object_preview.object.type);
                if(object_animate) {
                    object_preview.pose=sample_object_animation(bank,object_preview.object.animation,object_models.animation_blends);
                    const auto previous=sample_object_animation(bank,object_preview.object.previous_animation,object_models.animation_blends);
                    for(unsigned i=0;i<object_preview.pose.vertices.size();++i)
                        object_preview.pose.vertices[i]=lerp(previous.vertices[i],object_preview.pose.vertices[i],object_clock.alpha());
                    object_frame=static_cast<float>(object_preview.object.animation.frame);
                } else object_preview.pose=object_animation_frame(bank,object_clip,object_frame);
            }
        }
        const auto begin=std::chrono::steady_clock::now();
        auto view_eye=camera,view_target=target;
        if(playing && (capture_orbit!=0 || capture_distance>0 || capture_height!=0 || capture_orbit_rate!=0)) {
            const auto pivot=lerp(game->player.previous_position,game->player.position,game->clock.alpha())+retail::origin_above_feet;
            const float orbit=capture_orbit+capture_orbit_rate*(static_cast<float>(game->clock.ticks())+game->clock.alpha())/60;
            auto offset=retail::rotate_model(camera-pivot,orbit);
            if(capture_distance>0)offset=normalized(offset)*capture_distance;
            offset.y+=capture_height;view_eye=pivot+offset;
            view_target=pivot+retail::rotate_model(target-pivot,orbit);
        }
        if(playing && capture_shot)capture_shot->view(*game,view_eye,view_target);
        renderer.element_game=playing?game.get():nullptr;renderer.dark_spyro_preview=studio && cheats.dark_spyro;
        renderer.draw(playing?game->level:*level,playing?game->play_scene:document.scene,view_eye,view_target,playing?&game->player:nullptr,
            playing?game->clock.alpha():1,wire,*spyro,pose,animation,studio,collision,
            playing && capture_shot?capture_shot->fov*std::numbers::pi_v<float>/180:studio?std::numbers::pi_v<float>/3:view_fov(size.x/std::max(size.y,1.0f)),selected_triangle,object_models,
            playing?game->retail_objects:preview_objects,studio && object_studio?&object_preview:nullptr,
            playing?&game->interactions:nullptr,cheats.supercharge_appearance,playing?&game->charge_smoke:nullptr,
            playing?&game->powers:nullptr,playing?game->powers.tint(*game):Vec3{1,1,1},playing?std::span<const RemoteSpyro>(remote_spyros):std::span<const RemoteSpyro>{},
            playing?static_cast<float>(game->clock.ticks())+game->clock.alpha():static_cast<float>(object_clock.ticks()),{},{0,1,0},playing?&game->sparx:nullptr,playing?&game->headbash:nullptr,playing?&game->ripto:nullptr,playing?&game->destruction:nullptr);
        if(playing && menu==Menu::None)draw_hud(origin,size);
        if(playing && menu==Menu::None && online_status.connected) {
            auto* draw=ImGui::GetWindowDrawList();
            const auto label="Online: "+std::to_string(online_status.players.size())+" / 4";
            draw->AddText({origin.x+size.x-150,origin.y+12},IM_COL32(210,235,255,255),label.c_str());
            const auto forward=normalized(view_target-view_eye),right=normalized(cross(Vec3{0,1,0},forward)),up=cross(forward,right);
            for(const auto& remote:remote_spyros) {
                const auto d=remote.player.position+Vec3{0,1.3f,0}-view_eye;const auto depth=dot(d,forward);if(depth<0.1f)continue;
                const auto scale=size.y/(2*std::tan(view_fov(size.x/std::max(size.y,1.0f))/2)*depth);
                const auto text="Player "+std::to_string(remote.peer+1);
                draw->AddText({origin.x+size.x/2+dot(d,right)*scale-ImGui::CalcTextSize(text.c_str()).x/2,origin.y+size.y/2-dot(d,up)*scale},IM_COL32(220,240,255,255),text.c_str());
            }
        }
        if(playing && menu==Menu::None && show_hud && level->universe) {
            const auto& world=*level->universe;auto* draw=ImGui::GetWindowDrawList();
            draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
            const auto progress=universe_objective_progress(*game);
            const std::string objective=world.address.space?"A/D turn   W/S dive/rise   Shift boost   Fly through a gate":
                "Optional: collect the trail gems  "+std::to_string(progress)+" / "+std::to_string(world.objective_gems.size())+(progress==world.objective_gems.size()?"  Complete!":"");
            const float width=std::min(size.x-24,ImGui::CalcTextSize(objective.c_str()).x+24);
            draw->AddRectFilled({origin.x+12,origin.y+size.y-62},{origin.x+12+width,origin.y+size.y-10},IM_COL32(12,9,28,210),6);
            draw->AddText({origin.x+24,origin.y+size.y-54},IM_COL32(230,206,255,255),world.name.c_str());
            draw->AddText({origin.x+24,origin.y+size.y-32},IM_COL32(245,230,194,255),objective.c_str());
            for(const auto& portal:world.portals) {
                const float distance=length(game->player.position+retail::origin_above_feet-portal.center);
                if(world.address.space) {
                    const auto forward=normalized(view_target-view_eye),right=normalized(cross(Vec3{0,1,0},forward)),up=cross(forward,right);
                    const auto d=portal.center+Vec3{0,portal.radius+1,0}-view_eye;const float depth=dot(d,forward);
                    if(depth>0.1f) {
                        const float scale=size.y/(2*std::tan(view_fov(size.x/std::max(size.y,1.0f))/2)*depth);
                        const float x=origin.x+size.x/2+dot(d,right)*scale,y=origin.y+size.y/2-dot(d,up)*scale;
                        const auto portal_width=ImGui::CalcTextSize(portal.name.c_str()).x;
                        draw->AddText({x-portal_width/2+1,y+1},IM_COL32(0,0,0,255),portal.name.c_str());
                        draw->AddText({x-portal_width/2,y},IM_COL32(215,230,255,255),portal.name.c_str());
                    }
                }
                if(distance<portal.radius+3) {
                    const auto label=portal.name+"  [E]";
                    draw->AddText({origin.x+(size.x-ImGui::CalcTextSize(label.c_str()).x)/2,origin.y+size.y*0.7f},IM_COL32(242,230,255,255),label.c_str());
                }
            }
            draw->PopClipRect();
        }
        if(playing && menu==Menu::None)draw_dialogue(origin,size,seconds);
        if(const float fade=playing?std::max(game->portal_travel.fade,game->overlay_scripts.screen_fade()):0.0f;fade>0)
            ImGui::GetWindowDrawList()->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(0,0,0,int(fade*255)));
        render_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        ImGui::End();
        if(full_view)ImGui::PopStyleVar(2);
    }
};
#include "desktop_menu_check.inl"
#include "desktop_interface_check.inl"
#include "desktop_upgrade_check.inl"
#include "desktop_portal_check.inl"
#include "desktop_world_check.inl"
#include "desktop_level_exit_check.inl"
#include "desktop_challenge_check.inl"
#include "desktop_pad_check.inl"
#include "desktop_ripto_tells.inl"
#include "desktop_ripto_check.inl"
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    unsigned smoke_frames=0,slot=15,initial_animation=0,initial_object=UINT32_MAX;bool begin_play=false,level_view=false,replay=false,paced=false,swim_replay=false,show_fps=false;int ps1_fov=-1,ps1_hud=-1,camera_recentre=-1;bool recall_hud_flag=false;
    bool unlock_elements=false,dark_spyro=false,dark_smoke=true;int equip_element=-1;std::string element_replay;
    bool spyro3_edition=false,menu_levels=false;   // --spyro3: the Spyro 3 launcher's edition
    std::optional<BossDifficulty> boss_difficulty;
    float initial_pose=-1,initial_object_pose=-1;unsigned initial_object_model=UINT32_MAX,initial_object_clip=0;std::optional<std::array<float,3>> studio_view;
    std::string cinematic_request;
    bool backrealms=false,night_presentation=false,ripto_check=false,ripto_reel=false,tunnel_replay=false,terrain_destruction=false,protect_destruction=true;
    unsigned destruction_strength=1;
    std::optional<RiptoStage> capture_boss_stage;
    std::filesystem::path custom_level_path;
    std::optional<CaptureShot> capture_shot;
    unsigned interaction_record=UINT32_MAX;bool interaction_charge=false,interaction_passive=false,interaction_talk=false,interaction_approach=false;
    bool interaction_fetch=false,carry_quest_setup=false;unsigned socket_record=UINT32_MAX;
    std::vector<std::pair<unsigned,unsigned>> attack_classes;
    bool bridge_replay=false;
    float capture_orbit=0,capture_distance=0,capture_height=0,capture_orbit_rate=0;
    unsigned capture_health=UINT32_MAX,start_gems=UINT32_MAX,start_orbs=UINT32_MAX;float capture_music_volume=0.5f;bool open_gates=false,unlock_travel=false;
    unsigned upgrade_tier=0;std::string ability_replay,movement_replay;bool upgrade_workbench_check=false,upgrade_hud_check=false;
    std::string online_url,online_room;bool online_host=false;float online_offset=0;
    bool mute_audio=false,headless=false,capture_fullscreen=false,no_capture=false,hide_capture_hud=false,require_visible_footing=false;
    unsigned capture_width_pixels{},capture_height_pixels{};
    unsigned hide_workbench_frame=0;   // --hide-workbench N: press F1 at frame N (minimise check)
    unsigned level_exit_slot=0;        // --level-exit-check SLOT: the end-of-level flow (desktop_level_exit_check.inl)
    std::string challenge_name;        // --challenge-check NAME: an orb challenge played by its autopilot (desktop_challenge_check.inl)
    bool main_menu=false,menu_files=false,menu_check=false,workbench_mode=false,interface_check=false,portal_check=false,world_door_check=false,pad_check=false;
    std::string interface_page;
    float menu_time=840;
    unsigned yotd_area=0;
    std::filesystem::path menu_profile,retail_powerup_source;
    bool use_universe=false,universe_space=false,universe_replay=false;
    std::uint64_t universe_seed=2026,universe_world=0;
    Cheats cheats;
    std::filesystem::path game_path,capture="data/validation/viewport.bmp",sequence,ui_capture,load_save,save_on_exit,video_output,ripto_tells;
    bool imgui_ready=false,backend_ready=false;
    HWND window=nullptr;
    try {
        int count=0;auto** args=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!args)throw FormatError("Cannot read command line");
        for(int i=1;i<count;++i) {
            const std::wstring arg=args[i];
            if(arg==L"--game" && i+1<count)game_path=args[++i];
            else if(arg==L"--main-menu")main_menu=true;
            else if(arg==L"--menu-levels")menu_levels=true;   // captures: open the Spyro 3 level select
            else if(arg==L"--menu-files"){main_menu=true;menu_files=true;}
            else if(arg==L"--menu-check"){main_menu=true;menu_check=true;}
            else if(arg==L"--interface-check")interface_check=true;
            else if(arg==L"--portal-check")portal_check=true;
            else if(arg==L"--world-door-check")world_door_check=true;
            else if(arg==L"--pad-check")pad_check=true;
            else if(arg==L"--ripto-check")ripto_check=true;
            else if(arg==L"--ripto-reel")ripto_check=ripto_reel=true;
            else if(arg==L"--ripto-tells" && i+1<count){ripto_check=true;ripto_tells=args[++i];}
            else if(arg==L"--tunnel-replay"){tunnel_replay=true;begin_play=true;}
            else if(arg==L"--interface-page" && i+1<count)interface_page=utf8(args[++i]);
            else if(arg==L"--menu-profile" && i+1<count)menu_profile=args[++i];
            else if(arg==L"--menu-time" && i+1<count)menu_time=std::stof(args[++i]);
            else if(arg==L"--workbench")workbench_mode=true;
            else if(arg==L"--hide-workbench" && i+1<count)hide_workbench_frame=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--level-exit-check" && i+1<count){level_exit_slot=static_cast<unsigned>(std::stoul(args[++i]));begin_play=true;}
            else if(arg==L"--challenge-check" && i+1<count){challenge_name=utf8(std::filesystem::path(args[++i]));begin_play=true;}
            else if(arg==L"--spyro3")spyro3_edition=true;
            else if(arg==L"--retail-powerups" && i+1<count)retail_powerup_source=args[++i];
            else if(arg==L"--load-save" && i+1<count)load_save=args[++i];
            else if(arg==L"--save-game" && i+1<count)save_on_exit=args[++i];
            else if(arg==L"--mute")mute_audio=true;
            else if(arg==L"--headless"){headless=true;begin_play=true;}
            else if(arg==L"--no-capture")no_capture=true;
            else if(arg==L"--capture-fullscreen")capture_fullscreen=true;
            else if(arg==L"--hide-hud")hide_capture_hud=true;
            else if(arg==L"--gems" && i+1<count)start_gems=static_cast<unsigned>(std::stoul(args[++i]));   // gems in hand (Moneybags payments)
            else if(arg==L"--orbs" && i+1<count)start_orbs=std::min(64u,static_cast<unsigned>(std::stoul(args[++i])));   // orbs held (orb doors)
            else if(arg==L"--open-gates")open_gates=true;
            else if(arg==L"--unlock-travel")unlock_travel=true;   // the session's "Unlock portals" option (sealed world doors included)
            else if(arg==L"--capture-size" && i+2<count){capture_width_pixels=std::stoul(args[++i]);capture_height_pixels=std::stoul(args[++i]);}
            else if(arg==L"--cinematic-shot" && i+1<count)capture_shot=CaptureShot::read(args[++i]);
            else if(arg==L"--backrealms")backrealms=true;
            else if(arg==L"--night")night_presentation=true;
            else if(arg==L"--universe" && i+1<count){use_universe=true;universe_seed=std::stoull(args[++i]);}
            else if(arg==L"--world" && i+1<count)universe_world=std::stoull(args[++i]);
            else if(arg==L"--space")universe_space=true;
            else if(arg==L"--universe-replay")universe_replay=true;
            else if(arg==L"--video" && i+1<count)video_output=args[++i];
            else if(arg==L"--require-visible-footing")require_visible_footing=true;
            else if(arg==L"--frames" && i+1<count)smoke_frames=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--smoke" && i+1<count)smoke_frames=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--slot" && i+1<count)slot=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--yotd-area" && i+1<count)yotd_area=std::stoul(args[++i]);
            else if(arg==L"--custom-level" && i+1<count){custom_level_path=args[++i];begin_play=true;}
            else if(arg==L"--capture" && i+1<count)capture=args[++i];
            else if(arg==L"--play")begin_play=true;
            else if(arg==L"--level-view")level_view=true;
            else if(arg==L"--fps")show_fps=true;
            else if(arg==L"--recall-hud")recall_hud_flag=true;
            else if(arg==L"--ps1-fov")ps1_fov=1;else if(arg==L"--pc-fov")ps1_fov=0;   // this session only
            else if(arg==L"--ps1-hud")ps1_hud=1;else if(arg==L"--pc-hud")ps1_hud=0;
            else if(arg==L"--camera-recentre" && i+1<count)camera_recentre=static_cast<int>(std::clamp(std::stoul(args[++i]),0ul,10ul));
            else if(arg==L"--replay")replay=true;
            else if(arg==L"--swim-replay")swim_replay=true;
            else if(arg==L"--paced")paced=true;
            else if(arg==L"--terrain-destruction")terrain_destruction=true;
            else if(arg==L"--unprotected-destruction")protect_destruction=false;
            else if(arg==L"--destruction-strength" && i+1<count)destruction_strength=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--superflame")cheats.superflame=true;
            else if(arg==L"--superfly")cheats.superfly=true;
            else if(arg==L"--green-firebomb")cheats.green_firebomb=true;
            else if(arg==L"--elements")unlock_elements=true;
            else if(arg==L"--element" && i+1<count){const auto name=utf8(args[++i]);equip_element=-1;
                for(unsigned k=0;k<element_count;++k){std::string n=element_name(static_cast<Element>(k));std::transform(n.begin(),n.end(),n.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
                    if(n==name || n.rfind(name,0)==0 || name==std::to_string(k))equip_element=static_cast<int>(k);}
                if(equip_element<0)throw FormatError("Use --element fire|water|ice|electricity|convexity");}
            else if(arg==L"--dark-spyro"){dark_spyro=true;cheats.dark_spyro=true;}
            else if(arg==L"--boss-difficulty" && i+1<count) {
                const std::wstring v=args[++i];
                if(v==L"normal")boss_difficulty=BossDifficulty::Normal;else if(v==L"assist")boss_difficulty=BossDifficulty::Assist;
                else if(v==L"hard")boss_difficulty=BossDifficulty::Hard;else throw FormatError("--boss-difficulty needs normal, assist or hard");
            }
            else if(arg==L"--no-dark-smoke"){dark_smoke=false;cheats.dark_smoke=false;}
            else if(arg==L"--no-boss-cinematics")cheats.boss_cinematics=false;
            else if(arg==L"--element-replay" && i+1<count)element_replay=utf8(args[++i]);
            else if(arg==L"--upgrade-tier" && i+1<count)upgrade_tier=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--upgrade-workbench-check")upgrade_workbench_check=true;
            else if(arg==L"--upgrade-hud-check")upgrade_hud_check=true;
            else if(arg==L"--ability-replay" && i+1<count)ability_replay=utf8(args[++i]);
            else if(arg==L"--movement-replay" && i+1<count)movement_replay=utf8(args[++i]);
            else if(arg==L"--online-host" && i+1<count){online_url=utf8(args[++i]);online_host=true;}
            else if(arg==L"--online-join" && i+1<count){online_url=utf8(args[++i]);online_host=false;}
            else if(arg==L"--online-room" && i+1<count)online_room=utf8(args[++i]);
            else if(arg==L"--online-offset" && i+1<count)online_offset=std::stof(args[++i]);
            else if(arg==L"--supercharge" && i+1<count) {
                const std::wstring appearance=args[++i];
                if(appearance!=L"retail" && appearance!=L"main" && appearance!=L"main-flap")throw FormatError("--supercharge expects retail, main or main-flap");
                cheats.supercharge=true;cheats.supercharge_appearance=appearance==L"main-flap"?SuperchargeAppearance::MainModelChargeWings:
                    appearance==L"main"?SuperchargeAppearance::MainModel:SuperchargeAppearance::Retail;
            }
            else if(arg==L"--sequence" && i+1<count)sequence=args[++i];
            else if(arg==L"--capture-ui" && i+1<count)ui_capture=args[++i];
            else if(arg==L"--camera-orbit" && i+1<count)capture_orbit=std::stof(args[++i]);
            else if(arg==L"--camera-distance" && i+1<count)capture_distance=std::stof(args[++i]);
            else if(arg==L"--capture-boss-stage" && i+1<count) {
                const std::wstring phase=args[++i];
                if(phase==L"foot")capture_boss_stage=RiptoStage::Foot;
                else if(phase==L"mecha-gulp")capture_boss_stage=RiptoStage::MechaGulp;
                else if(phase==L"bird")capture_boss_stage=RiptoStage::Bird;
                else throw FormatError("Capture boss stage must be foot, mecha-gulp or bird");
            }
            else if(arg==L"--camera-height" && i+1<count)capture_height=std::stof(args[++i]);
            else if(arg==L"--capture-health" && i+1<count)capture_health=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--capture-music-volume" && i+1<count)capture_music_volume=std::stof(args[++i]);
            else if(arg==L"--camera-orbit-rate" && i+1<count)capture_orbit_rate=std::stof(args[++i]);
            else if(arg==L"--animation" && i+1<count)initial_animation=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--object" && i+1<count)initial_object=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--cinematic" && i+1<count){cinematic_request=utf8(args[++i]);if(cinematic_request!="intro" && cinematic_request!="outro")throw FormatError("Use --cinematic intro or outro");}
            else if(arg==L"--interaction-record" && i+1<count)interaction_record=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--socket-record" && i+1<count)socket_record=static_cast<unsigned>(std::stoul(args[++i]));
            // Validation: before a fetch replay, play the rest of Hurricos' diode
            // quest unfilmed (tools/carry_quest.hpp), leaving this diode and socket.
            else if(arg==L"--carry-quest")carry_quest_setup=true;
            else if(arg==L"--bridge-replay")bridge_replay=true;
            else if(arg==L"--attack-class" && i+1<count) {
                const std::wstring spec=args[++i];const auto at=spec.find(L'@');
                attack_classes.push_back({static_cast<unsigned>(std::stoul(spec.substr(0,at),nullptr,16)),at==std::wstring::npos?0u:static_cast<unsigned>(std::stoul(spec.substr(at+1)))});
            }
            else if(arg==L"--attack" && i+1<count) {
                const std::wstring attack=args[++i];
                if(attack!=L"charge" && attack!=L"flame" && attack!=L"none" && attack!=L"talk" && attack!=L"approach" && attack!=L"fetch")
                    throw FormatError("--attack expects charge, flame, none, talk, approach or fetch");
                interaction_charge=attack==L"charge";interaction_passive=attack==L"none" || attack==L"talk" || attack==L"approach" || attack==L"fetch";
                interaction_talk=attack==L"talk" || attack==L"approach";interaction_approach=attack==L"approach" || attack==L"fetch";interaction_fetch=attack==L"fetch";
            }
            else if(arg==L"--object-model" && i+1<count)initial_object_model=static_cast<unsigned>(std::stoul(args[++i],nullptr,0));
            else if(arg==L"--object-clip" && i+1<count)initial_object_clip=static_cast<unsigned>(std::stoul(args[++i]));
            else if(arg==L"--object-pose" && i+1<count)initial_object_pose=std::stof(args[++i]);
            else if(arg==L"--pose" && i+1<count)initial_pose=std::stof(args[++i]);
            else if(arg==L"--studio-view" && i+3<count){const float yaw=std::stof(args[++i]),pitch=std::stof(args[++i]),distance=std::stof(args[++i]);studio_view=std::array<float,3>{yaw,pitch,distance};}
            else {LocalFree(args);throw FormatError("Usage: spyro_workbench --game <ISO|CUE|BIN|folder> [--slot ID] [--play] [--smoke N --capture file.bmp]");}
        }
        LocalFree(args);
        if(capture_shot){smoke_frames=capture_shot->frames;begin_play=true;headless=true;capture_fullscreen=true;}
        if(capture_boss_stage && (!smoke_frames || ripto_check || !load_save.empty() || !save_on_exit.empty()))
            throw FormatError("Boss phase setup requires an offline capture without validation fixtures or saves");
        if(no_capture && (!sequence.empty() || !video_output.empty() || !ui_capture.empty()))throw FormatError("--no-capture cannot be combined with photo sequences or video output");
        if(tunnel_replay && (!smoke_frames || custom_level_path.empty() || ripto_check || swim_replay || replay || capture_shot || !load_save.empty() || !movement_replay.empty() || !ability_replay.empty()))
            throw FormatError("Tunnel replay needs a bounded Crownworks capture without other replays");
        if(portal_check && (smoke_frames<700 || game_path.empty() || interface_check || menu_check || !load_save.empty()))throw FormatError("Portal check needs --game, at least 700 frames and no other integration check/save");
        if(ripto_check && (smoke_frames<(ripto_reel?2400u:4200u) || !load_save.empty() || !save_on_exit.empty() || menu_check || portal_check || interface_check || upgrade_hud_check))
            throw FormatError("Ripto check needs a Ripto encounter, 4200 bounded frames (2400 for the reel) and no other integration check/save");
        if(headless && !smoke_frames)smoke_frames=600;
        if(bridge_replay) {
            if(!smoke_frames || use_universe || !load_save.empty() || replay || swim_replay || !movement_replay.empty() || !ability_replay.empty() || !cinematic_request.empty())
                throw FormatError("Bridge replay requires a bounded standalone gameplay capture");
            slot=17;interaction_record=9;interaction_passive=true;
        }
        if(main_menu)begin_play=false;
        if(interface_check && (smoke_frames<1300 || game_path.empty() || menu_check || !load_save.empty() || interaction_record!=UINT32_MAX || replay))throw FormatError("Interface check needs --game, 1300 frames, and no other replay");
        if(upgrade_hud_check && (smoke_frames<1130 || game_path.empty() || interface_check || menu_check || portal_check || !load_save.empty()))throw FormatError("Upgrade HUD check needs --game, at least 1130 frames and no other integration check/save");
        if(!interface_page.empty() && (!smoke_frames || (interface_page!="pause" && interface_page!="contents" && interface_page!="level" && interface_page!="upgrades" && interface_page!="destruction" && interface_page!="elements")))throw FormatError("Interface page expects pause, contents, level, upgrades, elements, or destruction in a bounded capture");
        if(!std::isfinite(menu_time) || menu_time<0 || menu_time>100000)throw FormatError("Invalid title capture time");
        if(menu_check && (smoke_frames<240 || game_path.empty() || !menu_profile.empty() || replay || use_universe || !load_save.empty()))
            throw FormatError("Menu check needs --game, at least 240 frames, and no profile or replay");
        if((universe_space || universe_replay || universe_world) && !use_universe)throw FormatError("--world, --space and --universe-replay require --universe <seed>");
        if(universe_replay && !smoke_frames)throw FormatError("--universe-replay requires --frames or --headless");
        if(use_universe && (swim_replay || replay || interaction_record!=UINT32_MAX || !load_save.empty()))throw FormatError("Universe generation cannot combine with a retail replay or --load-save");
        if(smoke_frames>(challenge_name.empty()?10000u:40000u))throw FormatError("Smoke test frame count must be at most 10000 (40000 for a challenge check)");
        if(!challenge_name.empty() && (smoke_frames<600 || game_path.empty() || level_exit_slot || portal_check || world_door_check || !load_save.empty() || !custom_level_path.empty()))
            throw FormatError("Challenge check needs --game, at least 600 frames and no other integration check, save or custom level");
        if(level_exit_slot && (smoke_frames<3000 || game_path.empty() || portal_check || world_door_check || !load_save.empty() || !custom_level_path.empty()))
            throw FormatError("Level exit check needs --game, at least 3000 frames and no other integration check, save or custom level");
        if(hide_workbench_frame && (!smoke_frames || hide_workbench_frame+2>=smoke_frames || main_menu))throw FormatError("--hide-workbench N needs a bounded run longer than N+2 frames");
        if(upgrade_workbench_check && (smoke_frames<150 || use_universe || !load_save.empty() || !ability_replay.empty() || !movement_replay.empty() || replay || swim_replay || interaction_record!=UINT32_MAX))
            throw FormatError("Upgrade workbench check needs at least 150 bounded frames and no other replay/save/world");
        if(smoke_frames && game_path.empty() && !main_menu)throw FormatError("Smoke test requires --game");
        if(capture_fullscreen && !smoke_frames)throw FormatError("--capture-fullscreen requires --frames or --headless");
        if(capture_health!=UINT32_MAX && (!smoke_frames || capture_health>3))throw FormatError("--capture-health requires a bounded capture and health 0..3");
        if(!std::isfinite(capture_music_volume) || capture_music_volume<0 || capture_music_volume>1 || (capture_music_volume!=0.5f && !smoke_frames))
            throw FormatError("--capture-music-volume requires a bounded capture and volume 0..1");
        if(!save_on_exit.empty() && !smoke_frames)throw FormatError("--save-game requires a bounded --smoke run");
        if((replay || swim_replay || paced || !sequence.empty() || !ui_capture.empty()) && !smoke_frames)
            throw FormatError("Validation options --replay, --paced, --sequence and --capture-ui require --smoke");
        if(interaction_record!=UINT32_MAX && (!smoke_frames || replay))throw FormatError("Container validation requires --smoke and cannot combine with --replay");
        if(!std::isfinite(capture_orbit) || std::abs(capture_orbit)>180 || (capture_orbit!=0 && !smoke_frames))
            throw FormatError("--camera-orbit requires --smoke and an angle from -180 to 180 degrees");
        if(!std::isfinite(capture_distance) || capture_distance<0 || capture_distance>100 ||
           !std::isfinite(capture_height) || std::abs(capture_height)>50 || !std::isfinite(capture_orbit_rate) || std::abs(capture_orbit_rate)>180 ||
           ((capture_distance>0 || capture_height!=0 || capture_orbit_rate!=0) && !smoke_frames))
            throw FormatError("Capture camera needs --frames, distance 0..100 and height -50..50 world units");
        TimerResolution timer_resolution;
        ShellApartment shell_apartment;
        ImGui_ImplWin32_EnableDpiAwareness();
        WNDCLASSEXW wc{sizeof(wc),CS_CLASSDC,window_proc,0,0,instance,nullptr,nullptr,nullptr,nullptr,L"Spyro2PCWorkbench",nullptr};
        if(!RegisterClassExW(&wc))throw FormatError("Cannot register workbench window");
        if(capture_width_pixels && (!smoke_frames || capture_width_pixels<320 || capture_width_pixels>3840 || capture_height_pixels<240 || capture_height_pixels>2160 || (capture_width_pixels&1) || (capture_height_pixels&1)))throw FormatError("Capture size requires bounded frames and even dimensions in 320..3840 by 240..2160");
        RECT capture_window{0,0,capture_shot?static_cast<LONG>(capture_shot->width):capture_width_pixels?static_cast<LONG>(capture_width_pixels):1424,capture_shot?static_cast<LONG>(capture_shot->height):capture_width_pixels?static_cast<LONG>(capture_height_pixels):921};
        const bool sized_capture=capture_shot.has_value() || capture_width_pixels;
        if(sized_capture)checked(AdjustWindowRectEx(&capture_window,WS_OVERLAPPEDWINDOW,FALSE,0)?S_OK:E_FAIL,"Size cinematic viewport");
        window=CreateWindowW(wc.lpszClassName,spyro3_edition?L"Spyro 3: Year of the Dragon PC":L"Spyro 2 PC",WS_OVERLAPPEDWINDOW,100,80,sized_capture?capture_window.right-capture_window.left:1440,sized_capture?capture_window.bottom-capture_window.top:960,nullptr,nullptr,instance,nullptr);
        if(!window)throw FormatError("Cannot create workbench window");
        DXGI_SWAP_CHAIN_DESC swap{};swap.BufferCount=2;swap.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;swap.OutputWindow=window;swap.SampleDesc.Count=1;swap.Windowed=TRUE;swap.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> chain;
        D3D_FEATURE_LEVEL feature{};const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
        bool hardware=true;
        auto hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,requested,2,D3D11_SDK_VERSION,&swap,&chain,&device,&feature,&context);
        if(FAILED(hr)) {hardware=false;hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,requested,2,D3D11_SDK_VERSION,&swap,&chain,&device,&feature,&context);}
        checked(hr,"Create D3D11 device");
        ComPtr<ID3D11RenderTargetView> backbuffer;
        const auto create_backbuffer=[&] {ComPtr<ID3D11Texture2D> texture;checked(chain->GetBuffer(0,IID_PPV_ARGS(&texture)),"Get swap-chain buffer");
            checked(device->CreateRenderTargetView(texture.Get(),nullptr,&backbuffer),"Create swap-chain target");};
        create_backbuffer();
        IMGUI_CHECKVERSION();ImGui::CreateContext();imgui_ready=true;
        auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_DockingEnable;   // gamepad: Workbench::poll_pad
        io.IniFilename=nullptr;palette();
        if(!ImGui_ImplWin32_Init(window) || !ImGui_ImplDX11_Init(device.Get(),context.Get()))throw FormatError("Cannot initialize ImGui backends");
        backend_ready=true;
        Workbench app;app.renderer.initialize(device.Get(),context.Get());
        app.spyro3_edition=spyro3_edition;app.main_window=window;
        app.renderer.upload_upgrades(app.upgrade_art);
        app.upload_element_hud();
        app.title_renderer.initialize(device.Get(),context.Get());
        app.cinematic_renderer.initialize(device.Get(),context.Get());app.automatic_cinematics=!smoke_frames;
        app.preferences_path=menu_profile;
        if(menu_check)app.preferences_path=std::filesystem::absolute(capture.parent_path()/
            ("menu-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))/"frontend.cfg");
        if(!smoke_frames && app.preferences_path.empty()) {
            PWSTR path{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path))) {
                // Each edition remembers its own copy, saves and donor.
                app.preferences_base=std::filesystem::path(path);
                app.preferences_path=app.edition_preferences(spyro3_edition);CoTaskMemFree(path);
            }
        }
        if(!app.preferences_path.empty()) {
            try{app.preferences=FrontendPreferences::read(app.preferences_path);}
            catch(const std::exception& e){app.preferences_warning=e.what();}
        }
        if(show_fps)app.preferences.show_fps=true;   // --fps: this session only
        if(ps1_fov>=0)app.preferences.ps1_fov=ps1_fov==1;if(ps1_hud>=0)app.preferences.ps1_hud=ps1_hud==1;if(camera_recentre>=0)app.preferences.camera_recentre_seconds=static_cast<unsigned>(camera_recentre);app.force_hud_recall=recall_hud_flag;
        if(game_path.empty() && (!smoke_frames || !menu_profile.empty())) {
            game_path=app.preferences.source;
            if(game_path.empty() && !smoke_frames && spyro3_edition && std::filesystem::exists("data-yotd/SCUS_944.67"))game_path="data-yotd";
            if(game_path.empty() && !smoke_frames && !spyro3_edition && std::filesystem::exists("data/disc/SCUS_944.25"))game_path="data/disc";
        }
        app.cheats=cheats;
        if(!retail_powerup_source.empty()) {
            app.retail_powerups=read_retail_powerups(retail_powerup_source);
            app.preferences.retail_powerup_source=std::filesystem::absolute(retail_powerup_source);
        }
        app.backrealms=backrealms;app.night_presentation=night_presentation;app.renderer.backrealms_armor=backrealms;app.unlock_travel=unlock_travel;
        app.replay_inputs=replay;
        app.swim_replay=swim_replay;
        app.universe_replay=universe_replay;app.capture_offline=smoke_frames!=0;
        app.capture_fullscreen=capture_fullscreen;
        app.capture_orbit=capture_orbit*std::numbers::pi_v<float>/180;
        app.capture_distance=capture_distance;app.capture_height=capture_height;
        app.capture_orbit_rate=capture_orbit_rate*std::numbers::pi_v<float>/180;
        if(!game_path.empty()) {if(smoke_frames)app.mount(game_path,slot==15?UINT32_MAX:slot);else app.action([&]{app.mount(game_path,slot==15?UINT32_MAX:slot);});}
        if(yotd_area && app.wad){if(!app.mounted_profile->yotd())throw FormatError("--yotd-area requires Year of the Dragon");app.load_yotd_level(app.level->wad_slot,false,yotd_area);}
        if(!custom_level_path.empty())app.open_custom_level(custom_level_path);
        if(app.spyro) {
            if(!app.spyro->animations.contains(initial_animation))throw FormatError("Animation is unavailable in this level");
            app.animation=initial_animation;app.studio=!level_view;
            if(initial_pose>=0){app.animation_frame=initial_pose;app.animation_playing=false;}
            if(studio_view){app.model_yaw=(*studio_view)[0]*std::numbers::pi_v<float>/180;app.model_pitch=std::clamp((*studio_view)[1]*std::numbers::pi_v<float>/180,-1.2f,1.4f);app.model_distance=std::clamp((*studio_view)[2],0.6f,12.0f);}
        }
        if(begin_play && app.level)app.play();
        if(tunnel_replay) {
            if(!app.game || app.game->level.water_volumes.empty())throw FormatError("Tunnel replay requires the flooded Crownworks network");
            app.tunnel_replay=true;app.game->play_scene.spawn={-26,3.52f,-15.3f};app.game->respawn();
            app.game->player.yaw=app.game->player.previous_yaw=0;
            app.game->camera.reset(app.game->level,app.game->player.position,0);
        }
        if(capture_shot) {
            if(!app.game || !app.playing || !load_save.empty() || replay || swim_replay || use_universe || (interaction_record!=UINT32_MAX && !interaction_fetch))
                throw FormatError("Cinematic shots require a mounted level and cannot combine with saves or other replays");
            app.capture_shot=std::move(capture_shot);app.show_hud=app.capture_shot->hud;
            // A fetch replay (--attack fetch) starts beside its record and only uses the shot's cameras.
            if(!interaction_fetch) {
                app.game->play_scene.spawn=app.capture_shot->spawn;app.game->respawn();
                app.game->player.yaw=app.game->player.previous_yaw=app.capture_shot->yaw;
                app.game->camera.reset(*app.level,app.game->player.position,app.game->player.yaw);
            }
        }
        if(!load_save.empty())app.load_game(load_save);
        if(use_universe)app.load_universe({universe_seed,universe_world,universe_space});
        app.audio.muted=mute_audio;
        app.title_audio.muted=mute_audio;
        if(swim_replay)app.play_in_water();
        if(capture_boss_stage) {
            if(!app.game)throw FormatError("Capture boss phase requires a mounted encounter");
            app.game->ripto.prepare_capture_stage(*app.game,app.object_models,*capture_boss_stage);
        }
        if(!cinematic_request.empty())app.start_cinematic(cinematic_request=="outro");
        if(interaction_record!=UINT32_MAX) {
            app.interaction_record=interaction_record;app.interaction_charge=interaction_charge;app.interaction_passive=interaction_passive;
            app.interaction_talk=interaction_talk;app.interaction_approach=interaction_approach;app.attack_classes=attack_classes;
            app.interaction_fetch=interaction_fetch;app.socket_record=socket_record;
            if(interaction_fetch && socket_record==UINT32_MAX)throw FormatError("--attack fetch needs --socket-record");
            app.play_beside_object(interaction_record);
            if(carry_quest_setup) {
                // With --attack fetch the record is the diode left for the socket
                // record; otherwise it is the robot left to defeat on camera.
                auto& game=*app.game;
                const bool fetch=interaction_fetch;
                const auto report=carry_quest::power_sockets(game,[&](InputState in) {
                    game.player.health=std::max(game.player.health,3u);game.player.invulnerable_ticks=std::max(game.player.invulnerable_ticks,2u);
                    game.advance(FixedClock::step,in);
                },fetch?std::set<unsigned>{socket_record}:std::set<unsigned>{},fetch?std::set<unsigned>{interaction_record}:std::set<unsigned>{},
                    std::getenv("SPYRO_QUEST_VERBOSE")!=nullptr,fetch?std::set<unsigned>{}:std::set<unsigned>{interaction_record});
                game.sound_events.clear();
                const auto at=game.retail_objects[interaction_record].position;
                auto away=fetch?at-game.retail_objects[socket_record].position:Vec3{0,0,-1};away.y=0;
                carry_quest::park(game,at+(length(away)>.05f?normalized(away):Vec3{0,0,1})*(fetch?3.0f:2.2f),at);
                std::fprintf(stderr,"carry quest: %u/%u sockets powered, %u robots left, %u rounds\n",report.powered,report.sockets,report.robots_alive,report.rounds);
            }
        }
        if(bridge_replay){app.bridge_replay=true;app.game->player.gems=150;}
        if(start_gems!=UINT32_MAX) {
            if(!app.playing || !app.game)throw FormatError("--gems requires gameplay");
            app.game->player.gems=start_gems;
        }
        if(start_orbs!=UINT32_MAX) {
            if(!app.playing || !app.game)throw FormatError("--orbs requires gameplay");
            app.game->player.orbs=start_orbs;
        }
        if(open_gates) {
            // Filming setup: every power-up gate needs no spirits (kind 10 still needs its gems and orbs).
            if(!app.playing || !app.game)throw FormatError("--open-gates requires gameplay");
            for(auto& o:app.game->retail_objects)if(o.powerup_gate)o.powerup_gate->requirement=0;
        }
        if(capture_health!=UINT32_MAX) {
            if(!app.playing || !app.game)throw FormatError("--capture-health requires gameplay");
            app.game->player.health=capture_health;
            app.game->sparx.reset(app.game->retail_objects,&app.object_models,app.game->player.position+retail::origin_above_feet,app.game->player.yaw,capture_health);
        }
        app.audio.music_volume=capture_music_volume;
        if(initial_object!=UINT32_MAX)app.view_object(initial_object);
        if(initial_object_model!=UINT32_MAX) {
            app.view_object_model(initial_object_model,initial_object_clip);
            if(initial_object_pose>=0){app.object_frame=initial_object_pose;app.object_animate=false;}
        }
        if(upgrade_tier>2)throw FormatError("Upgrade tier must be 0, 1 or 2");
        if(upgrade_tier && app.game){auto& u=app.game->upgrades;u.enabled=true;u.charge=u.flame=u.flight=u.glide=u.headbash=upgrade_tier;u.headbash_mode=upgrade_tier==2?HeadbashMode::Lava:HeadbashMode::Crater;u.carry=u.melee=true;u.roll=app.game->roll_available();}
        if(unlock_elements || equip_element>=0 || !element_replay.empty()) {
            if(!app.game)throw FormatError("Elemental options need a mounted level");
            auto& u=app.game->upgrades;u.enabled=true;unlock_all_elements(*app.game);
            if(equip_element>=0)u.elements.equipped=static_cast<Element>(equip_element);
            if(element_replay=="melee")u.melee=true;
            // Headbash is a learned move in the campaign (Moneybags, purchase 6):
            // replays that headbash start with it learned.
            if(element_replay=="headbash" || element_replay=="tour" || element_replay=="demons"){u.headbash=std::max(u.headbash,1u);app.game->purchases|=1u<<6;}
            app.remember_settings();
        }
        if(!element_replay.empty() && (!smoke_frames || (element_replay!="breath" && element_replay!="shot" && element_replay!="headbash" && element_replay!="fury" &&
            element_replay!="melee" && element_replay!="tour" && element_replay!="demons" && element_replay!="dark-run" && element_replay!="slice" && element_replay!="slice-ground")))
            throw FormatError("Element replay needs --frames and breath, shot, headbash, fury, melee, tour, demons, dark-run, slice or slice-ground");
        if((element_replay=="demons" || element_replay=="slice" || element_replay=="slice-ground") && app.game)app.game->upgrades.elements.equipped=Element::Convexity;
        app.element_replay=element_replay;
        if(boss_difficulty)app.cheats.boss_difficulty=*boss_difficulty;
        if(app.game){app.cheats.dark_spyro=dark_spyro || app.cheats.dark_spyro;app.cheats.dark_smoke=dark_smoke && app.cheats.dark_smoke;app.game->cheats=app.cheats;}
        if(destruction_strength<1 || destruction_strength>3)throw FormatError("Destruction strength must be 1..3");
        if(terrain_destruction && app.game){auto& d=app.game->destruction_settings;d.enabled=true;d.strength=destruction_strength;d.protect_gameplay=protect_destruction;app.remember_settings();}
        if(upgrade_workbench_check) {
            // Exercise the same preview, menu action, Play, Stop and level
            // switch paths as the desktop. CLI tier setup happens after Play
            // and would not detect a lost pre-play selection.
            const auto require=[](bool condition,const char* why){if(!condition)throw FormatError(why);};
            app.campaign={};app.campaign_ready=false;app.playing=false;app.load_level(slot,false);
            app.unlock_upgrades(true);app.game->upgrades.points=123;app.play();
            require(app.game->upgrades.enabled && app.game->upgrades.melee && app.game->upgrades.carry &&
                app.game->upgrades.charge==2 && app.game->upgrades.flame==2 && app.game->upgrades.flight==2 && app.game->upgrades.glide==2 &&
                app.game->upgrades.points==123,"Play discarded the preview's upgrade selection");
            app.stop();app.game->upgrades.enabled=false;app.game->upgrades.charge=1;app.play();
            require(!app.game->upgrades.enabled && app.game->upgrades.charge==1 && app.game->upgrades.points==123,
                "Play restored stale upgrade settings after Stop");
            app.stop();auto& u=app.game->upgrades;u.enabled=true;u.glide=0;u.points=75;
            require(u.buy(Upgrade::Glide) && u.buy(Upgrade::Glide),"Stopped workbench cannot buy upgrades");
            app.load_level(slot==45?15:45);app.play();
            require(app.game->upgrades.enabled && app.game->upgrades.glide==2 && !app.game->upgrades.points && app.game->upgrades.melee,
                "Level change discarded upgrades purchased while stopped");
            app.stop();app.load_level(slot);app.play();
            require(app.game->powers.charge_enabled(*app.game) && app.game->powers.flame_enabled(*app.game) && app.game->powers.fly_enabled(*app.game),
                "Unlock-and-activate did not retain the required base powers");
        }
        if(!ability_replay.empty() && (!smoke_frames || !upgrade_tier ||
            (ability_replay!="melee-empty" && (interaction_record==UINT32_MAX ||
             (ability_replay!="melee" && ability_replay!="carry" && ability_replay!="carry-throw" && ability_replay!="carry-inspect")))))
            throw FormatError("Ability replay needs --upgrade-tier, --frames and a supported replay; targeted replays also need --interaction-record");
        app.ability_replay=ability_replay;
        if(!movement_replay.empty()) {
            if(!smoke_frames || !ability_replay.empty() || interaction_record!=UINT32_MAX ||
               (movement_replay!="aerial" && movement_replay!="headbash" && movement_replay!="sand" && movement_replay!="charge-jump" && movement_replay!="jump-charge"))
                throw FormatError("Movement replay needs a bounded capture and aerial, headbash, sand, charge-jump or jump-charge");
            app.movement_replay=movement_replay;
            if(movement_replay=="headbash" || movement_replay=="aerial")app.game->purchases|=1u<<6;   // learned move (see above)
            if(movement_replay=="charge-jump" || movement_replay=="jump-charge")app.cheats.supercharge=app.game->cheats.supercharge=false;
            if(movement_replay=="aerial")app.game->player.position.y+=12;
            if(movement_replay=="sand") {
                const Triangle* chosen=nullptr;float best=-1;
                for(const auto& triangle:app.level->triangles) {
                    if(triangle.surface_type!=10)continue;
                    auto normal=normalized(cross(triangle.b-triangle.a,triangle.c-triangle.a));if(normal.y<0)normal=normal*-1;
                    if(normal.y<=0.55f || normal.y>=0.96f)continue;
                    const auto center=(triangle.a+triangle.b+triangle.c)/3;
                    const float area=length(cross(triangle.b-triangle.a,triangle.c-triangle.a));
                    const float score=center.y+std::min(area,8.0f);
                    if(score>best){best=score;chosen=&triangle;}
                }
                if(!chosen)throw FormatError("No authored sliding sand in this level");
                auto normal=normalized(cross(chosen->b-chosen->a,chosen->c-chosen->a));if(normal.y<0)normal=normal*-1;
                app.game->player.position=(chosen->a+chosen->b+chosen->c)/3+normal*(app.game->tuning.radius+0.001f)-retail::origin_above_feet;
                app.game->player.yaw=app.game->player.previous_yaw=std::atan2(normal.x,normal.z);
            }
            app.game->player.previous_position=app.game->player.position;
            app.game->camera.reset(*app.level,app.game->player.position,app.game->player.yaw);
        }
        if(ability_replay=="carry" || ability_replay=="carry-throw" || ability_replay=="carry-inspect") {
            app.cheats.superfly=app.game->cheats.superfly=true;app.game->powers.flying=true;
            app.game->player.position.y+=1.6f;app.game->player.previous_position=app.game->player.position;app.game->player.grounded=false;
            app.game->camera.reset(*app.level,app.game->player.position,app.game->player.yaw);
        }
        {std::ifstream saved_relay("data/online/relay-url.txt");std::string url;if(std::getline(saved_relay,url))strncpy_s(app.relay_url,url.c_str(),_TRUNCATE);}
        if(!online_url.empty()) {
            if(!std::isfinite(online_offset) || std::abs(online_offset)>5)throw FormatError("Online spawn offset must be within five units");
            app.game->player.position.x+=online_offset;app.game->player.previous_position=app.game->player.position;
            strncpy_s(app.relay_url,online_url.c_str(),_TRUNCATE);strncpy_s(app.room_code,online_room.c_str(),_TRUNCATE);app.connect_online(online_host);
        }
        app.video_request=video_output;
        if(hide_capture_hud)app.show_hud=false;
        app.frontend=main_menu || (!smoke_frames && !workbench_mode);
        if(main_menu || (!smoke_frames && !workbench_mode && !begin_play && load_save.empty() && !use_universe)) {
            app.action([&]{
                if(app.spyro3() && app.level && !app.playing)app.play();   // Sunrise Spring stands behind the Spyro 3 menu
                app.open_menu(menu_files?Workbench::Menu::Files:Workbench::Menu::Title);
                if(menu_levels && app.spyro3()){app.menu_back=Workbench::Menu::Title;app.open_menu(Workbench::Menu::Levels);}});app.title_ticks=menu_time;
        }
        MenuIntegrationCheck menu_validation;if(menu_check)menu_validation.setup(app);
        InterfaceIntegrationCheck interface_validation;if(interface_check)interface_validation.setup(app,capture);
        UpgradeIntegrationCheck upgrade_validation;if(upgrade_hud_check)upgrade_validation.setup(app,capture);
        PortalIntegrationCheck portal_validation;if(portal_check)portal_validation.setup(app);
        WorldDoorCheck world_validation;if(world_door_check)world_validation.setup(app);
        LevelExitCheck exit_validation;if(level_exit_slot)exit_validation.setup(app,level_exit_slot,capture);
        ChallengeCheck challenge_validation;if(!challenge_name.empty())challenge_validation.setup(app,challenge_name,capture);
        PadIntegrationCheck pad_validation;pad_validation.folder=capture.parent_path();
        if(pad_check && (smoke_frames<520 || !workbench_mode || !app.playing || menu_check || interface_check || portal_check || world_door_check || ripto_check))
            throw FormatError("Controller check needs --workbench --play, at least 520 frames and no other integration check");
        RiptoIntegrationCheck ripto_validation;ripto_validation.reel=ripto_reel;
        RiptoTellCapture tell_capture;tell_capture.folder=ripto_tells;
        if(!interface_page.empty()) {
            app.play();app.open_menu(Workbench::Menu::Pause);
            if(interface_page=="upgrades")app.open_menu(Workbench::Menu::Upgrades);
            else if(interface_page=="destruction")app.open_menu(Workbench::Menu::Destruction);
            else if(interface_page=="elements"){app.open_menu(Workbench::Menu::Upgrades);app.open_menu(Workbench::Menu::Elements);}
            else if(interface_page!="pause"){app.guide_selected=(app.level->wad_slot-15)/2;app.guide_world=app.guide_selected<9?0:app.guide_selected<21?1:2;app.guide_detail=interface_page=="level";app.open_menu(Workbench::Menu::Guidebook);}
        }
        if(!smoke_frames) {ShowWindow(window,SW_SHOWDEFAULT);UpdateWindow(window);}
        Level capture_ground;unsigned capture_ground_revision=UINT32_MAX,unsupported_grounded_frames=0;
        const auto update_capture_ground=[&] {
            if(!app.game || capture_ground_revision==app.game->level.terrain_revision)return;
            capture_ground.triangles.clear();const auto& v=app.game->level.visual_vertices;
            for(unsigned i=0;i+2<v.size();i+=3)capture_ground.triangles.push_back({v[i].position,v[i+1].position,v[i+2].position});
            capture_ground_revision=app.game->level.terrain_revision;
        };
        if(require_visible_footing) {
            if(!smoke_frames || !app.game || !app.playing)throw FormatError("Visible footing validation requires bounded gameplay");
            update_capture_ground();const auto feet=app.game->player.position;
            const auto physics=app.game->level.raycast(feet+Vec3{0,.4f,0},{0,-1,0},1);
            const auto shown=capture_ground.raycast(feet+Vec3{0,.4f,0},{0,-1,0},1);
            if(!physics || !shown || std::abs(physics->point.y-shown->point.y)>.05f)
                throw FormatError("Capture spawn has no matching visible and collision floor");
        }
        auto previous=std::chrono::steady_clock::now();auto deadline=previous;
        const auto start=previous;
        unsigned frames=0,max_work_frame=0,over_budget_frames=0,grounded_capture_frames=0,fall_capture_frames=0;bool done=false;double max_frame_ms=0,total_work_ms=0;
        std::vector<double> frame_work;
        double simulation_work=0,render_work=0,widget_work=0,audio_work=0,present_work=0;
        std::array<double,6> render_stages{};
        while(!done) {
            const auto frame_start=std::chrono::steady_clock::now();
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);if(message.message==WM_QUIT)done=true;}
            if(done)break;
            if(close_requested) {
                close_requested=false;
                if(!app.playing){done=true;break;}
                app.menu_back=app.menu==Workbench::Menu::None?Workbench::Menu::Pause:app.menu;app.open_menu(Workbench::Menu::Quit);
            }
            if(!smoke_frames && IsIconic(window)) {
                if(app.playing && app.menu==Workbench::Menu::None)app.open_menu(Workbench::Menu::Pause);
                if(app.game)app.game->set_paused(true);app.audio.update(app.game.get(),false);app.title_audio.update(nullptr,false);app.cinematic_audio.update(nullptr,false);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));previous=std::chrono::steady_clock::now();continue;
            }
            if(pending_width && pending_height) {
                context->OMSetRenderTargets(0,nullptr,nullptr);backbuffer.Reset();
                checked(chain->ResizeBuffers(0,pending_width,pending_height,DXGI_FORMAT_UNKNOWN,0),"Resize swap chain");
                pending_width=pending_height=0;create_backbuffer();
            }
            const auto now=std::chrono::steady_clock::now();
            const bool warming_video=smoke_frames && !video_output.empty() && !app.video.active() && !app.video_request.empty();
            const double delta=warming_video?0:smoke_frames?FixedClock::step:std::chrono::duration<double>(now-previous).count();previous=now;
            if(hide_workbench_frame && app.game)io.AddKeyEvent(ImGuiKey_F1,frames==hide_workbench_frame);   // the real key path
            if((ability_replay=="melee-empty" || upgrade_workbench_check) && app.game) {
                const auto tick=app.game->clock.ticks();io.AddKeyEvent(ImGuiKey_J,tick==30 || tick==45 || tick==73);
            }
            ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();if(smoke_frames)io.DeltaTime=static_cast<float>(FixedClock::step);
            if(menu_check)menu_validation.input(frames,app,io);
            if(interface_check)interface_validation.input(frames,app,io);
            if(upgrade_hud_check)upgrade_validation.input(frames,app,io);
            {const auto scripted=pad_validation.sample(frames);app.poll_pad(window,io,pad_check?&scripted:nullptr,smoke_frames!=0);}
            ImGui::NewFrame();
            if(portal_check)portal_validation.tick(app);
            if(world_door_check)world_validation.tick(app);
            if(level_exit_slot)exit_validation.tick(app);
            if(!challenge_name.empty())challenge_validation.tick(app);
            if(pad_check)pad_validation.tick(frames,app);
            if(ripto_check)ripto_validation.tick(app);
            if(!ripto_tells.empty())tell_capture.view(app);
            const auto widget_start=std::chrono::steady_clock::now();
            app.simulation_ms=app.render_ms=0;app.widgets(window,delta);app.count_frame(delta);app.draw_fps();
            const auto audio_start=std::chrono::steady_clock::now();
            simulation_work+=app.simulation_ms;render_work+=app.render_ms;
            for(unsigned i=0;i<6;++i)render_stages[i]+=app.renderer.stage_ms[i];
            widget_work+=std::chrono::duration<double,std::milli>(audio_start-widget_start).count();
            app.audio.update(app.game.get(),app.menu==Workbench::Menu::None && !app.cinematic_playing && app.playing && app.game && !app.game->paused);
            if(app.game && app.playing){grounded_capture_frames+=app.game->player.grounded;fall_capture_frames+=app.game->player.motion==Motion::Fall;
                if(require_visible_footing && app.game->player.grounded){update_capture_ground();
                    unsupported_grounded_frames+=!capture_ground.ground_support(app.game->player.position+retail::origin_above_feet,app.game->tuning.radius,.08f);}}
            app.cinematic_audio.muted=app.audio.muted;app.cinematic_audio.music_volume=app.audio.voice_volume;
            app.cinematic_audio.update(nullptr,app.cinematic_playing && app.menu==Workbench::Menu::None);
            app.title_audio.muted=app.audio.muted;app.title_audio.music_volume=app.audio.music_volume;
            app.title_audio.update(nullptr,app.title_visible() && app.title!=nullptr);
            audio_work+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-audio_start).count();
            ImGui::Render();const float clear[]={0.04f,0.04f,0.06f,1};auto* target=backbuffer.Get();
            context->OMSetRenderTargets(1,&target,nullptr);context->ClearRenderTargetView(target,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            const auto capture_frame=[&] {
                if(app.video_stop){app.audio.end_capture();app.title_audio.end_capture();app.cinematic_audio.end_capture();app.video.finish();app.video_stop=false;app.status="Video saved: "+utf8(app.video_path);}
                if(frames>=5 && !app.video_request.empty()) {
                    if(app.level)app.audio.begin_capture(smoke_frames!=0);
                    if(app.title)app.title_audio.begin_capture(smoke_frames!=0);
                    if(app.cinematic_playing)app.cinematic_audio.begin_capture(smoke_frames!=0);
                    try{app.video.start(app.video_request,app.capture_area.width,app.capture_area.height);}
                    catch(...){app.audio.end_capture();app.title_audio.end_capture();app.cinematic_audio.end_capture();app.video_request.clear();throw;}
                    app.video_path=app.video_request;app.video_request.clear();app.capture_seconds=0;
                }
                if(!app.video.active() && app.photo_request.empty())return;
                ComPtr<ID3D11Texture2D> screen;checked(chain->GetBuffer(0,IID_PPV_ARGS(&screen)),"Get capture buffer");
                if(!app.photo_request.empty()) {
                    VideoCapture::photo(app.photo_request,device.Get(),context.Get(),screen.Get(),app.capture_area);
                    app.status="Photo saved: "+utf8(app.photo_request);app.photo_request.clear();
                }
                if(app.video.active()) {
                    if(!smoke_frames && !app.suspend_frames)app.capture_seconds+=std::min(delta,0.25);
                    const unsigned wanted=smoke_frames?app.video.frames()+1:static_cast<unsigned>(app.capture_seconds*60)+1;
                    while(app.video.frames()<wanted)app.video.frame(device.Get(),context.Get(),screen.Get(),app.capture_area,
                        app.title_visible()?app.title_audio.capture_frame():app.cinematic_playing?app.cinematic_audio.capture_frame():app.audio.capture_frame());
                }
            };
            if(smoke_frames)capture_frame();
            else if(app.video.active() || app.video_stop || !app.video_request.empty() || !app.photo_request.empty())app.action(capture_frame);
            if(!no_capture && (!ui_capture.empty() || main_menu) && frames+1==smoke_frames) {
                ComPtr<ID3D11Texture2D> screen;checked(chain->GetBuffer(0,IID_PPV_ARGS(&screen)),"Get UI capture buffer");
                app.renderer.save_bmp(ui_capture.empty()?capture:ui_capture,screen.Get());
            }
            const auto present_start=std::chrono::steady_clock::now();
            checked(chain->Present(0,0),"Present frame");
            present_work+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-present_start).count();
            const double work=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame_start).count();
            if(work>max_frame_ms){max_frame_ms=work;max_work_frame=frames+1;}
            if(work>FixedClock::step*1000)++over_budget_frames;
            total_work_ms+=work;if(smoke_frames)frame_work.push_back(work);++frames;
            if(!sequence.empty() && frames%2==0) {
                char name[48];snprintf(name,sizeof(name),"frame_%05u.bmp",frames);
                app.renderer.save_bmp(sequence/name);
            }
            if(!ripto_tells.empty()){tell_capture.capture(app);if(tell_capture.saved.size()>=8)break;}   // all eight warnings filmed
            if(smoke_frames && ((!video_output.empty() && app.video.frames()>=smoke_frames) || (video_output.empty() && frames>=smoke_frames)))break;
            if(!challenge_name.empty() && challenge_validation.finished && app.photo_request.empty())break;   // the challenge is over and its last photo saved
            if(!smoke_frames || paced) {
                deadline+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(FixedClock::step));
                if(deadline<std::chrono::steady_clock::now())deadline=std::chrono::steady_clock::now();
                std::this_thread::sleep_until(deadline);
            }
        }
        if(app.video.active()){app.audio.end_capture();app.title_audio.end_capture();app.cinematic_audio.end_capture();app.video.finish();}
        if(smoke_frames) {
            if(ripto_check && ripto_tells.empty() && (!app.game->ripto.defeated() || app.game->ripto.transitions!=3 ||
                (app.game->ripto.retail_arena?!app.game->guide_awards.boss:!app.game->level.custom_arena->complete)))
                throw FormatError("Ripto native check did not render and complete all three phases");
            if(portal_check)portal_validation.require(portal_validation.completed==5,"Not all five campaign transitions completed");
            if(world_door_check)world_validation.require(world_validation.completed==2,"the crown gate round trip did not complete");
            if(level_exit_slot)exit_validation.require(exit_validation.finished,"the level exit did not complete in the bounded frames");
            if(!challenge_name.empty()) {
                challenge_validation.require(challenge_validation.finished,"the challenge did not finish in the bounded frames (step "+std::to_string(challenge_validation.pilot->index)+")");
                challenge_validation.require(challenge_validation.pilot->failure.empty(),"step failed: "+challenge_validation.pilot->failure);
                challenge_validation.require(challenge_validation.awarded,"the orb was not awarded");
            }
            if(pad_check)pad_validation.require(pad_validation.finished,"the controller script did not finish");
            if(hide_workbench_frame && (app.workbench_toggles!=1 || !app.frontend))throw FormatError("F1 did not minimise the workbench");
            if(!app.error.empty())throw FormatError("Smoke test encountered UI error: "+app.error);
            if(upgrade_workbench_check && app.game->abilities.melee_swings!=3)
                throw FormatError("Workbench upgrades survived Play, but real J input failed to trigger all three melee strikes");
            if(!save_on_exit.empty()) {app.current_save_path()=save_on_exit;app.save_game(window,false);}
            if(!no_capture && !main_menu){if(app.cinematic_playing)app.cinematic_renderer.save_bmp(capture);else app.renderer.save_bmp(capture);}
            std::sort(frame_work.begin(),frame_work.end());
            const auto p95=frame_work.empty()?0:frame_work[std::min(frame_work.size()-1,static_cast<std::size_t>(frame_work.size()*0.95))];
            auto report=capture;report.replace_extension("json");std::ofstream out(report);
            if(!app.level)out<<"{\"frames\":"<<frames<<",\"source_picker\":true,\"main_menu\":true}\n";
            else {
            out<<"{\n";
            if(app.mounted_profile->yotd())out<<"  \"yotd_area\": "<<app.level->source_area
               <<",\n  \"yotd_collision_fallback\": "<<(app.level->collision_from_terrain?"true":"false")
               <<",\n  \"yotd_spawn_projected\": "<<(app.level->spawn_projected?"true":"false")
               <<",\n  \"yotd_scripts_active\": "<<(app.game->yotd_scripts.active()?"true":"false")
               <<",\n  \"yotd_script_actors\": "<<app.game->yotd_scripts.owned()
               <<",\n  \"yotd_script_ticks\": "<<app.game->yotd_scripts.script_ticks
               <<",\n  \"yotd_script_instructions\": "<<app.game->yotd_scripts.instructions
               <<",\n  \"yotd_script_attacks\": "<<app.game->yotd_scripts.attacks_seen
               <<",\n  \"yotd_script_spawned\": "<<app.game->yotd_scripts.spawned
               <<",\n  \"yotd_script_removed\": "<<app.game->yotd_scripts.removed
               <<",\n  \"yotd_script_faults\": "<<app.game->yotd_scripts.faults
               <<",\n  \"yotd_script_fault\": "<<std::quoted(app.game->yotd_scripts.fault)<<",\n";
            out<<"  \"frames\": "<<frames<<",\n  \"hardware_renderer\": "<<(hardware?"true":"false")
               <<",\n  \"main_menu\": "<<(app.menu!=Workbench::Menu::None?"true":"false")
               <<",\n  \"title_actors\": "<<(app.title?app.title->models.models.size():0)
               <<",\n  \"title_ticks\": "<<app.title_ticks
               <<",\n  \"title_music_frames\": "<<app.title_audio.music_frames
               <<",\n  \"menu_check\": "<<(menu_check?"true":"false")
               <<",\n  \"menu_assertions\": "<<menu_validation.assertions
               <<",\n  \"interface_assertions\": "<<interface_validation.assertions
               <<",\n  \"upgrade_hud_assertions\": "<<upgrade_validation.assertions
               <<",\n  \"pad_assertions\": "<<pad_validation.assertions<<",\n  \"world_door_assertions\": "<<world_validation.assertions<<",\n  \"world_door_trips\": "<<world_validation.completed
               <<",\n  \"level_exit\": {\"slot\": "<<exit_validation.slot<<", \"home\": "<<exit_validation.home<<", \"speaker\": "<<exit_validation.speaker
               <<", \"finished\": "<<(exit_validation.finished?"true":"false")<<", \"simulated_end\": "<<(exit_validation.simulated?"true":"false")
               <<", \"outro\": "<<(exit_validation.outro_seen?"true":"false")<<", \"talks\": "<<exit_validation.talks
               <<", \"end_trigger\": "<<(exit_validation.trigger==UINT32_MAX?-1:static_cast<int>(exit_validation.trigger))<<", \"checkpoint_restarts\": "<<exit_validation.restarts<<", \"quest_freed\": "<<exit_validation.freed
               <<", \"assertions\": "<<exit_validation.assertions<<"}"
               <<",\n  \"challenge\": {\"name\": \""<<challenge_name<<"\", \"finished\": "<<(challenge_validation.finished?"true":"false")<<", \"awarded\": "<<(challenge_validation.awarded?"true":"false")
               <<", \"steps\": "<<(challenge_validation.pilot?challenge_validation.pilot->index:0u)<<", \"ticks\": "<<(challenge_validation.pilot?challenge_validation.pilot->total:0u)
               <<", \"fixtures\": "<<(challenge_validation.pilot?challenge_validation.pilot->fixtures:0u)<<", \"photos\": "<<challenge_validation.photos.size()
               <<", \"hud_ticks\": "<<(app.game?app.game->overlay_scripts.hud_ticks:0u)<<"}"<<",\n  \"portal_assertions\": "<<portal_validation.assertions<<",\n  \"portal_transitions\": "<<portal_validation.completed
               <<",\n  \"wad_slot\": "<<app.level->wad_slot<<",\n  \"triangles\": "<<app.level->triangles.size()
               <<",\n  \"sky_regions\": "<<app.level->sky_regions.size()
               <<",\n  \"sky_triangles\": "<<app.level->sky_vertices.size()/3
               <<",\n  \"rendered_sky_regions\": "<<app.renderer.rendered_sky_regions()
               <<",\n  \"rendered_sky_triangles\": "<<app.renderer.rendered_sky_triangles()
               <<",\n  \"spyro_vertices\": "<<app.pose.vertices.size()<<",\n  \"spyro_animation\": "<<app.animation
               <<",\n  \"spyro_clips\": "<<app.spyro->animations.size()<<",\n  \"spyro_frame\": "<<app.pose.frame
               <<",\n  \"spyro_pitch_degrees\": "<<app.game->player.pitch*180/std::numbers::pi_v<float>
               <<",\n  \"supercharge_enabled\": "<<(app.cheats.supercharge?"true":"false")
               <<",\n  \"upgrades_enabled\": "<<(app.game->upgrades.enabled?"true":"false")
               <<",\n  \"upgrade_workbench_check\": "<<(upgrade_workbench_check?"true":"false")
               <<",\n  \"upgrade_charge_tier\": "<<app.game->upgrades.charge
               <<",\n  \"upgrade_flame_tier\": "<<app.game->upgrades.flame
               <<",\n  \"upgrade_flight_tier\": "<<app.game->upgrades.flight
               <<",\n  \"upgrade_glide_tier\": "<<app.game->upgrades.glide
               <<",\n  \"upgrade_melee_unlocked\": "<<(app.game->upgrades.melee?"true":"false")
               <<",\n  \"upgrade_carry_unlocked\": "<<(app.game->upgrades.carry?"true":"false")
               <<",\n  \"online_connected\": "<<(app.online_status.connected?"true":"false")
               <<",\n  \"online_peak_players\": "<<app.online_peak_players
               <<",\n  \"online_exchanges\": "<<app.online_status.exchanges
               <<",\n  \"online_remote_spyros\": "<<app.remote_spyros.size()
               <<",\n  \"upgrade_points\": "<<app.game->upgrades.points
               <<",\n  \"upgrade_charge_flames\": "<<app.game->powers.upgrade_flames
               <<",\n  \"homing_ticks\": "<<app.game->powers.homing_ticks
               <<",\n  \"split_flames\": "<<app.game->powers.split_flames
               <<",\n  \"carried_object\": "<<app.game->abilities.carried
               <<",\n  \"carried_pickups\": "<<app.game->abilities.pickups
               <<",\n  \"carried_throws\": "<<app.game->abilities.throws
               <<",\n  \"carried_drops\": "<<app.game->abilities.drops
               <<",\n  \"carried_smashes\": "<<app.game->abilities.smashes
               <<",\n  \"melee_hits\": "<<app.game->abilities.melee_hits
               <<",\n  \"melee_swings\": "<<app.game->abilities.melee_swings
               <<",\n  \"glide_boosts\": "<<app.game->player.glide_boosts
               <<",\n  \"hover_actions\": "<<app.game->player.hover_actions
               <<",\n  \"terrain_destruction_enabled\": "<<(app.game->destruction_settings.enabled?"true":"false")
               <<",\n  \"terrain_impacts\": "<<app.game->destruction.damage().size()
               <<",\n  \"terrain_changed_triangles\": "<<app.game->destruction.changed_triangles
               <<",\n  \"terrain_chunks_emitted\": "<<app.game->destruction.chunks_emitted
               <<",\n  \"terrain_live_debris\": "<<app.game->destruction.debris().size()
               <<",\n  \"terrain_crack_triangles\": "<<app.game->destruction.cracks().size()
               <<",\n  \"charge_wall_impacts\": "<<app.game->player.charge_impacts
               <<",\n  \"headbash_impacts\": "<<app.game->player.headbash_impacts
               <<",\n  \"element_equipped\": \""<<element_name(app.game->upgrades.elements.equipped)<<"\""
               <<",\n  \"element_energy\": "<<app.game->upgrades.elements.energy<<",\n  \"element_fury\": "<<app.game->upgrades.elements.fury
               <<",\n  \"element_breaths\": "<<app.game->elements.breaths<<",\n  \"element_shots\": "<<app.game->elements.shots_fired
               <<",\n  \"element_waves\": "<<app.game->elements.waves<<",\n  \"element_wave_hits\": "<<app.game->elements.wave_hits
               <<",\n  \"element_furies\": "<<app.game->elements.furies<<",\n  \"element_fury_blasts\": "<<app.game->elements.fury_blasts<<",\n  \"element_freezes\": "<<app.game->elements.freezes
               <<",\n  \"element_shatters\": "<<app.game->elements.shatters<<",\n  \"element_stuns\": "<<app.game->elements.stuns
               <<",\n  \"element_chains\": "<<app.game->elements.chains<<",\n  \"element_soaks\": "<<app.game->elements.soaks
               <<",\n  \"element_cooled_lava\": "<<app.game->elements.cooled_lava<<",\n  \"element_status_hits\": "<<app.game->elements.status_hits
               <<",\n  \"element_floods\": "<<app.game->elements.floods<<",\n  \"element_pockets\": "<<app.game->elements.pockets<<",\n  \"element_crate_hits\": "<<app.game->elements.crates
               <<",\n  \"void_slices\": "<<app.game->destruction.slices<<",\n  \"void_slabs_landed\": "<<app.game->destruction.slabs_landed<<",\n  \"void_shards\": "<<app.game->destruction.shards_made<<",\n  \"void_infill_faces\": "<<app.game->destruction.infill_faces<<",\n  \"void_protected\": "<<app.game->destruction.protected_impacts<<",\n  \"void_erosions\": "<<app.game->elements.erosions
               <<",\n  \"element_melee_trail_samples\": "<<app.game->elements.melee_trail_samples
               <<",\n  \"retail_scripts_active\": "<<(app.game->overlay_scripts.active()?"true":"false")<<",\n  \"retail_script_mobys\": "<<app.game->overlay_scripts.owned()
               <<",\n  \"retail_script_ticks\": "<<app.game->overlay_scripts.script_ticks<<",\n  \"retail_script_instructions\": "<<app.game->overlay_scripts.instructions
               <<",\n  \"retail_script_hits\": "<<app.game->overlay_scripts.hits<<",\n  \"retail_script_spawned\": "<<app.game->overlay_scripts.spawned
               <<",\n  \"retail_script_removed\": "<<app.game->overlay_scripts.removed<<",\n  \"retail_script_gems\": "<<app.game->overlay_scripts.gems
               <<",\n  \"retail_script_attacks\": "<<app.game->overlay_scripts.attacks_seen<<",\n  \"retail_script_faults\": "<<app.game->overlay_scripts.faults<<",\n  \"retail_script_fault\": \""<<app.game->overlay_scripts.fault<<"\""
               <<",\n  \"retail_script_target_state\": "<<(app.interaction_record<app.game->retail_objects.size()?int(app.game->retail_objects[app.interaction_record].state):-99)
               <<",\n  \"retail_script_camera_shots\": "<<app.game->overlay_scripts.camera_shots<<",\n  \"retail_script_warps\": "<<app.game->overlay_scripts.warps<<",\n  \"retail_script_checkpoints\": "<<app.game->overlay_scripts.checkpoints
               <<",\n  \"retail_script_pickups\": "<<app.game->overlay_scripts.pickups<<",\n  \"retail_script_spits\": "<<app.game->overlay_scripts.spits
               <<",\n  \"retail_script_dialogues\": "<<app.game->overlay_scripts.dialogues<<",\n  \"orbs\": "<<app.game->player.orbs
               <<",\n  \"retail_script_classes\": \""<<hex_list(app.game->overlay_scripts.classes)<<"\",\n  \"retail_script_invalid_animation\": \""<<hex_list(app.game->overlay_scripts.invalid_animation)<<"\""
               <<",\n  \"dark_spyro\": "<<(app.game->elements.dark(*app.game)?"true":"false")<<",\n  \"dark_fury_entries\": "<<app.game->elements.dark_entries
               <<",\n  \"dark_smoke_emitted\": "<<app.game->elements.smoke.emitted()
               <<",\n  \"demons_summoned\": "<<app.game->demons.summoned<<",\n  \"demons_active\": "<<app.game->demons.active()
               <<",\n  \"demon_kills\": "<<app.game->demons.kills<<",\n  \"demon_refunds\": "<<app.game->demons.refunded
               <<",\n  \"demon_projectiles_blocked\": "<<app.game->demons.projectiles_blocked<<",\n  \"demon_hits_absorbed\": "<<app.game->demons.hits_absorbed
               <<",\n  \"demon_containers_opened\": "<<app.game->demons.containers_opened<<",\n  \"demon_gems_fetched\": "<<app.game->demons.gems_fetched
               <<",\n  \"demon_butterflies\": "<<app.game->demons.butterflies
               <<",\n  \"rendered_element_effects\": "<<app.renderer.rendered_element_effects()<<",\n  \"rendered_shockwave_triangles\": "<<app.renderer.rendered_shockwave_triangles()
               <<",\n  \"rendered_demon_eruptions\": "<<app.renderer.rendered_demon_eruptions()<<",\n  \"rendered_dark_spyro\": "<<(app.renderer.rendered_dark_spyro()?"true":"false")
               <<",\n  \"rendered_dark_smoke\": "<<app.renderer.rendered_dark_smoke()<<",\n  \"rendered_eye_glows\": "<<app.renderer.rendered_eye_glows()
               <<",\n  \"rendered_melee_trails\": "<<app.renderer.rendered_melee_trails()
               <<",\n  \"headbash_craters\": "<<app.game->headbash.stamps().size()
               <<",\n  \"headbash_changed_triangles\": "<<app.game->headbash.changed_triangles
               <<",\n  \"headbash_lava_triangles\": "<<app.game->headbash.lava().size()
               <<",\n  \"ripto_enabled\": "<<(app.game->ripto.enabled?"true":"false")
               <<",\n  \"ripto_phase_check\": "<<(ripto_check?"true":"false")
               <<",\n  \"ripto_phase\": \""<<app.game->ripto.phase_name()<<"\""
               <<",\n  \"crown_cutscenes_played\": "<<app.game->ripto.cinematics_played<<",\n  \"crown_cutscenes_seen\": "<<app.game->ripto.cinematics_seen
               <<",\n  \"crown_light_changes\": "<<app.game->ripto.light_changes<<",\n  \"crown_lit_frames\": "<<app.renderer.crown_lit_frames()
               <<",\n  \"music_changes\": "<<app.audio.music_changes
               <<",\n  \"world_doors\": "<<std::count_if(app.game->level.portals.begin(),app.game->level.portals.end(),[](const auto& p){return !p.world.empty();})
               <<",\n  \"world_doors_open\": "<<std::count_if(app.game->level.portals.begin(),app.game->level.portals.end(),[](const auto& p){return !p.world.empty() && p.enabled;})
               <<",\n  \"world_return_depth\": "<<app.world_returns.size()<<",\n  \"world_return_home\": "<<(app.game->world_return?"true":"false")
               <<",\n  \"sliceable_volumes\": "<<app.game->level.sliceable.size()<<",\n  \"terrain_slices\": "<<app.game->destruction.slices
               <<",\n  \"boss_difficulty\": "<<static_cast<unsigned>(app.game->ripto.difficulty)
               <<",\n  \"ripto_finale_ticks\": "<<app.game->ripto.finale_ticks<<",\n  \"ripto_mech_freezes\": "<<app.game->ripto.mech_freezes<<",\n  \"ripto_chain_hits\": "<<app.game->ripto.chain_hits
               <<",\n  \"ripto_action\": \""<<app.game->ripto.action_name()<<"\""
               <<",\n  \"ripto_health\": "<<app.game->ripto.health
               <<",\n  \"ripto_hits\": "<<app.game->ripto.hits
               <<",\n  \"ripto_attacks\": "<<app.game->ripto.attacks
               <<",\n  \"ripto_damage_to_spyro\": "<<app.game->ripto.damage_to_spyro
               <<",\n  \"ripto_transitions\": "<<app.game->ripto.transitions
               <<",\n  \"crush_health\": "<<app.game->ripto.crush_health
               <<",\n  \"crush_attacks\": "<<app.game->ripto.crush_attacks
               <<",\n  \"crush_hits\": "<<app.game->ripto.crush_hits
               <<",\n  \"crownworks_lance\": "<<app.game->ripto.attack_counts[6]
               <<",\n  \"crownworks_nova\": "<<app.game->ripto.attack_counts[7]
               <<",\n  \"crownworks_bombard\": "<<app.game->ripto.attack_counts[8]
               <<",\n  \"crownworks_dive\": "<<app.game->ripto.attack_counts[9]
               <<",\n  \"crownworks_combo\": "<<app.game->ripto.attack_counts[10]
               <<",\n  \"slide_ticks\": "<<app.game->player.slide_ticks
               <<",\n  \"video_frames\": "<<app.video.frames()<<",\n  \"video_fps\": 60"
               <<",\n  \"universe\": "<<(app.level->universe?"true":"false")
               <<",\n  \"universe_seed\": \""<<(app.level->universe?app.level->universe->address.seed:0)<<"\""
               <<",\n  \"universe_world\": \""<<(app.level->universe?app.level->universe->address.world:0)<<"\""
               <<",\n  \"universe_space\": "<<(app.level->universe && app.level->universe->address.space?"true":"false")
               <<",\n  \"universe_validated\": "<<(app.level->universe && app.level->universe->validated?"true":"false")
               <<",\n  \"universe_enemies\": "<<(app.level->universe?app.level->universe->enemies:0)
               <<",\n  \"universe_objective\": "<<universe_objective_progress(*app.game)
               <<",\n  \"universe_transitions\": "<<app.universe_transitions
               <<",\n  \"powered_shots\": "<<app.game->powers.shots_fired<<",\n  \"green_bomb_explosions\": "<<app.game->powers.bombs_exploded
               <<",\n  \"superflame_explosions\": "<<app.game->powers.flame_explosions
               <<",\n  \"underwater_explosions\": "<<app.game->powers.water_explosions
               <<",\n  \"power_effects_spawned\": "<<app.game->powers.visuals_spawned
               <<",\n  \"rendered_power_effects\": "<<app.renderer.rendered_power_effects()
               <<",\n  \"retail_powerups_imported\": "<<((app.mounted_profile->prerelease() && app.retail_powerups) || (app.mounted_profile->yotd() && app.yotd_donor)?"true":"false")
               <<",\n  \"wing_tip_vertices\": ["<<app.spyro->wing_tips[0]<<","<<app.spyro->wing_tips[1]<<"]"
               <<",\n  \"wing_trail_points\": ["<<app.game->powers.wings()[0].size()<<","<<app.game->powers.wings()[1].size()<<"]"
               <<",\n  \"unbound_return_portals_visible\": "<<std::count_if(app.game->retail_objects.begin(),app.game->retail_objects.end(),[&](const auto& o){return o.type==0x1aa && !o.hidden && std::none_of(app.game->level.portals.begin(),app.game->level.portals.end(),[&](const auto& p){return p.return_home && p.object==o.id;});})
               <<",\n  \"power_targets_destroyed\": "<<app.game->powers.targets_destroyed<<",\n  \"power_gates_activated\": "<<app.game->powers.gates_activated
               <<",\n  \"supercharge_main_model\": "<<(app.cheats.supercharge_appearance!=SuperchargeAppearance::Retail?"true":"false")
               <<",\n  \"supercharge_charge_wings\": "<<(app.cheats.supercharge_appearance==SuperchargeAppearance::MainModelChargeWings?"true":"false")
               <<",\n  \"supercharge_blur_vertices\": "<<app.spyro->supercharge_blur_vertices
               <<",\n  \"rendered_charge_smoke\": "<<app.renderer.rendered_charge_smoke()
               <<",\n  \"emitted_charge_smoke\": "<<app.game->charge_smoke.emitted()
               <<",\n  \"spyro_head_clip\": "<<app.game->secondary.state().pose.clip
               <<",\n  \"spyro_head_next_clip\": "<<app.game->secondary.state().pose.next_clip
               <<",\n  \"spyro_head_frame\": "<<app.game->secondary.state().pose.frame
               <<",\n  \"spyro_head_mode\": "<<app.game->secondary.state().mode
               <<",\n  \"flame_action\": "<<app.game->flame_action.action()
               <<",\n  \"flame_action_age\": "<<app.game->flame_action.age()
               <<",\n  \"observed_motion_mask\": "<<app.observed_motions
               <<",\n  \"grounded_capture_frames\": "<<grounded_capture_frames
               <<",\n  \"visible_footing_checked\": "<<(require_visible_footing?"true":"false")
               <<",\n  \"unsupported_grounded_frames\": "<<unsupported_grounded_frames
               <<",\n  \"fall_capture_frames\": "<<fall_capture_frames
               <<",\n  \"tunnel_replay_stage\": "<<app.tunnel_route.stage<<",\n  \"tunnel_replay_waypoint\": "<<app.tunnel_route.waypoint
               <<",\n  \"audio_music_track\": "<<app.audio.track<<",\n  \"audio_music_frames\": "<<app.audio.music_frames
               <<",\n  \"audio_voice_frames\": "<<app.audio.voice_frames<<",\n  \"audio_samples\": "<<app.audio.sample_count
               <<",\n  \"audio_effects_played\": "<<app.audio.effects_played
               <<",\n  \"audio_roll_effects_played\": "<<app.audio.roll_effects_played
               <<",\n  \"audio_music_samples_played\": "<<app.audio.music_samples_played
               <<",\n  \"audio_voice_samples_played\": "<<app.audio.voice_samples_played
               <<",\n  \"npc_conversations\": "<<app.game->dialogue.conversations()
               <<",\n  \"npc_dialogue_active\": "<<(app.game->dialogue.active()?"true":"false")
               <<",\n  \"campaign_purchases\": "<<app.game->purchases
               <<",\n  \"scene_locked\": "<<(app.game->scene_locked?"true":"false")
               <<",\n  \"terrain_revision\": "<<app.game->level.terrain_revision
               <<",\n  \"mechanism_activations\": "<<app.game->systems.activations
               <<",\n  \"cannon_shots\": "<<app.game->systems.cannon_shots
               <<",\n  \"trolley_gears\": "<<app.game->systems.gears
               <<",\n  \"challenge_mode\": "<<static_cast<unsigned>(app.game->systems.mode)
               <<",\n  \"challenge_remaining\": "<<app.game->systems.remaining
               <<",\n  \"challenge_wins\": "<<app.game->systems.challenge_wins
               <<",\n  \"power_meter_visible\": "<<(app.game->hud.power_visible()?"true":"false")
               <<",\n  \"retail_objects\": "<<app.level->objects.size()
               <<",\n  \"rendered_static_objects\": "<<app.renderer.rendered_objects()
               <<",\n  \"rendered_animated_objects\": "<<app.renderer.rendered_animated_objects()
               <<",\n  \"rendered_character_shadows\": "<<app.renderer.rendered_character_shadows()
               <<",\n  \"rendered_shadow_triangles\": "<<app.renderer.rendered_shadow_triangles()
               <<",\n  \"rendered_flame_streams\": "<<app.renderer.rendered_flame_streams()
               <<",\n  \"rendered_flame_triangles\": "<<app.renderer.rendered_flame_triangles()
               <<",\n  \"object_preview_class\": "<<(app.object_studio?app.object_preview.object.type:0)
               <<",\n  \"object_preview_clip\": "<<app.object_clip
               <<",\n  \"object_preview_frame\": "<<app.object_frame
               <<",\n  \"static_model_classes\": "<<std::count_if(app.object_models.models.begin(),app.object_models.models.end(),[](const auto& entry){return !entry.second.animated;})
               <<",\n  \"loose_gems\": "<<std::count_if(app.level->objects.begin(),app.level->objects.end(),[](const auto& object){return object.type==1;})
               <<",\n  \"collected_retail_gems\": "<<std::count_if(app.game->retail_objects.begin(),app.game->retail_objects.end(),[](const auto& object){return object.collected;})
               <<",\n  \"gem_total\": "<<app.game->player.gems
               <<",\n  \"level_gems\": "<<app.game->level_gems()
               <<",\n  \"spirits\": "<<app.game->spirits()
               <<",\n  \"spirits_launched\": "<<app.game->powers.spirits_launched
               <<",\n  \"spirits_in_flight\": "<<app.game->powers.spirit_flights().size()
               <<",\n  \"spirits_arrived\": "<<app.game->powers.spirits_arrived
               <<",\n  \"gates_activated\": "<<app.game->powers.gates_activated
               <<",\n  \"gate_bolts\": "<<app.game->powers.gate_bolts().size()
               <<",\n  \"gate_crackles\": "<<app.game->powers.gate_crackles().size()
               <<",\n  \"gate_sparkles\": "<<std::count_if(app.game->powers.particles().begin(),app.game->powers.particles().end(),[](const auto& p){return p.kind==PowerParticleKind::GateGlow;})
               <<",\n  \"held_sounds\": "<<app.game->powers.held_sounds().size()
               <<",\n  \"boss_fight\": "<<(static_cast<std::int32_t>(app.game->overlay_scripts.peek(0x80066eec,4))<0)
               <<",\n  \"boss_health\": "<<app.game->overlay_scripts.peek(0x80066fec,4)
               <<",\n  \"boss_hud_visible\": "<<app.game->hud.boss_visible()
               <<",\n  \"boss_hud_value\": "<<app.game->hud.boss_counter().value
               <<",\n  \"health\": "<<app.game->player.health
               <<",\n  \"mapped_enemies\": "<<std::count_if(app.game->retail_objects.begin(),app.game->retail_objects.end(),[](const auto& object){return object.enemy.has_value();})
               <<",\n  \"water_mode\": "<<static_cast<unsigned>(app.game->player.swim.mode)
               <<",\n  \"water_height\": "<<app.game->player.swim.height
               <<",\n  \"water_triangles\": "<<app.level->water_triangles
               <<",\n  \"rendered_transparent_terrain_triangles\": "<<app.renderer.rendered_transparent_terrain_triangles()
               <<",\n  \"player_speed\": "<<length(app.game->player.velocity)
               <<",\n  \"swim_speed\": "<<app.game->player.swim.speed
               <<",\n  \"player_pitch\": "<<app.game->player.pitch
               <<",\n  \"player_position\": ["<<app.game->player.position.x<<','<<app.game->player.position.y<<','<<app.game->player.position.z<<']'
               <<",\n  \"broken_containers\": "<<app.game->interactions.broken_containers()
               <<",\n  \"heated_vases\": "<<std::count_if(app.game->retail_objects.begin(),app.game->retail_objects.end(),[](const auto& object){return object.type==0xc9 && object.container && object.container->heat>0;})
               <<",\n  \"released_container_gems\": "<<app.game->interactions.released_gems()
               <<",\n  \"active_fragments\": "<<std::count_if(app.game->retail_objects.begin(),app.game->retail_objects.end(),[](const auto& object){return is_container_fragment(object.type) && object.state>=0;})
               <<",\n  \"bouncing_gems\": "<<std::count_if(app.game->retail_objects.begin(),app.game->retail_objects.end(),[](const auto& object){return object.type==1 && object.state==2 && !object.collected;})
               <<",\n  \"sparx_spawned\": "<<(app.game->sparx.object_index()?"true":"false")
               <<",\n  \"sparx_script_steps\": "<<app.game->sparx.script_steps()
               <<",\n  \"sparx_attracted_gems\": "<<app.game->sparx.attracted_gems()
               <<",\n  \"sparx_delivered_gems\": "<<app.game->sparx.delivered_gems()
               <<",\n  \"sparx_emitted_sparkles\": "<<app.game->sparx.emitted_sparkles()
               <<",\n  \"sparx_butterflies_eaten\": "<<app.game->sparx.butterflies_eaten()
               <<",\n  \"sparx_feeding_starts\": "<<app.game->sparx.feeding_starts()
               <<",\n  \"sparx_rendered_sparkles\": "<<app.renderer.rendered_sparx_sparkles()
               <<",\n  \"sparx_rendered_glows\": "<<app.renderer.rendered_sparx_glows()
               <<",\n  \"retail_units_per_world_unit\": "<<retail::units_per_world
               <<",\n  \"spyro_origin_height_retail\": "<<retail::spyro_origin_units
               <<",\n  \"camera_radius_retail\": "<<app.game->camera.parameters()[2]
               <<",\n  \"camera_elevation_retail\": "<<app.game->camera.parameters()[1]
               <<",\n  \"camera_obstructed\": "<<(app.game->camera.obstructed()?"true":"false")
               <<",\n  \"vertical_fov_degrees\": "<<app.view_fov(float(app.capture_area.width)/std::max(1u,app.capture_area.height))*180/std::numbers::pi_v<float>
               <<",\n  \"simulation_ticks\": "<<app.game->clock.ticks()<<",\n  \"mean_cpu_work_ms\": "<<total_work_ms/std::max(1u,frames)
               <<",\n  \"max_cpu_work_ms\": "<<max_frame_ms
               <<",\n  \"max_cpu_work_frame\": "<<max_work_frame
               <<",\n  \"p95_cpu_work_ms\": "<<p95
               <<",\n  \"cpu_frames_over_budget\": "<<over_budget_frames
               <<",\n  \"mean_simulation_ms\": "<<simulation_work/std::max(1u,frames)
               <<",\n  \"mean_render_ms\": "<<render_work/std::max(1u,frames)
               <<",\n  \"mean_render_stages_ms\": ["<<render_stages[0]/frames<<","<<render_stages[1]/frames<<","<<render_stages[2]/frames<<","<<render_stages[3]/frames<<","<<render_stages[4]/frames<<","<<render_stages[5]/frames<<"]"
               <<",\n  \"mean_widgets_ms\": "<<widget_work/std::max(1u,frames)
               <<",\n  \"mean_audio_ms\": "<<audio_work/std::max(1u,frames)
               <<",\n  \"mean_present_ms\": "<<present_work/std::max(1u,frames)
               <<",\n  \"elapsed_seconds\": "
               <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"\n}\n";
            }
            if(!out)throw FormatError("Could not save smoke test report");
        }
        ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();backend_ready=false;ImGui::DestroyContext();imgui_ready=false;
        DestroyWindow(window);window=nullptr;UnregisterClassW(wc.lpszClassName,instance);return 0;
    } catch(const std::exception& e) {
        std::filesystem::create_directories("data/validation");std::ofstream("data/validation/workbench-error.txt")<<e.what()<<'\n';
        if(backend_ready){ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();}
        if(imgui_ready)ImGui::DestroyContext();
        if(!smoke_frames)MessageBoxA(window,e.what(),"Spyro 2 workbench",MB_ICONERROR|MB_OK);
        if(window)DestroyWindow(window);return 1;
    }
}
