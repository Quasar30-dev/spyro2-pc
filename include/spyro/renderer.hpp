#pragma once
#include "spyro/spyro_dark.hpp"
#include "spyro/runtime.hpp"
#include "spyro/spyro_model.hpp"
#include "spyro/hud.hpp"
#include <d3d11.h>
#include <wrl/client.h>

namespace spyro {
struct RenderVertex { Vec3 position; float r{},g{},b{},a=1; float u{},v{},textured{}; };
struct ObjectPreview {RetailObject object;ObjectPose pose;};
struct RemoteSpyro {Player player;SpyroPose pose;unsigned clip{},peer{};SuperchargeAppearance appearance=SuperchargeAppearance::Retail;bool dark=false;Element element=Element::Fire;};
// Pixel shader Light buffer: low, mid, high, sky tints; core (centre, radius);
// core colour (rgb, strength); bands (lower/terrace and terrace/walk heights);
// then the low, mid, high and sky casts.
struct LightConstants {std::array<std::array<float,4>,11> values{};};
class Renderer {
public:
    bool backrealms_armor=false; // Optional trailer / expansion presentation.
    bool character_shadows=true; // Also permits a controlled renderer A/B test.
    // Optional elemental/demon/Dark Spyro presentation for the playing Game.
    // Set per frame by the workbench; null in the studio and editor views.
    const Game* element_game=nullptr;
    bool dark_spyro_preview=false;   // model studio preview of the dark palette
    std::array<double,6> stage_ms{}; // terrain, objects, player, effects, portals, transparency
    void initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void upload_level(const Level& level);
    void upload_spyro(const SpyroModel& model);
    void upload_objects(const ObjectModels& models);
    // Winter Tundra's frozen-pond tile (WAD 57 tile 35, 64x64 RGBA) for
    // elemental ice; empty clears it (the frost base still draws).
    void upload_ice_texture(const Bytes& rgba);
    void upload_hud(const HudAssets& hud);
    void upload_interface(const HudAssets& assets);
    void upload_upgrades(const HudAssets& assets);
    ID3D11ShaderResourceView* upgrade_image()const{return upgrade_texture_.Get();}
    void upload_element_icons(const HudAssets& assets);
    ID3D11ShaderResourceView* element_icon_image()const{return element_icon_texture_.Get();}
    ID3D11ShaderResourceView* interface_image()const{return interface_texture_.Get();}
    void upload_powerups(const PowerupAssets& assets);
    ID3D11ShaderResourceView* hud_image()const{return hud_texture_.Get();}
    void resize(unsigned width,unsigned height);
    void draw(const Level& level, const Scene& scene, Vec3 eye, Vec3 target, const Player* player,
              float alpha, bool wireframe, const SpyroModel& model, const SpyroPose& pose,
              unsigned animation, bool studio, bool collision, float vertical_fov,
              std::uint32_t selected_triangle, const ObjectModels& object_models,
              std::span<const RetailObject> retail_objects,const ObjectPreview* object_preview=nullptr,
              const ObjectInteractions* interactions=nullptr,SuperchargeAppearance appearance=SuperchargeAppearance::Retail,
              const ChargeSmoke* charge_smoke=nullptr,const Powerups* powers=nullptr,Vec3 spyro_tint={1,1,1},std::span<const RemoteSpyro> remotes={},float material_ticks=0,
              std::span<const ObjectPose> object_poses={},Vec3 camera_up={0,1,0},const SparxCompanion* sparx=nullptr,const HeadbashTerrain* headbash=nullptr,const RiptoBoss* ripto=nullptr,const TerrainDestruction* destruction=nullptr);
    ID3D11ShaderResourceView* image() const { return color_view_.Get(); }
    unsigned width() const { return width_; } unsigned height() const { return height_; }
    unsigned rendered_objects() const { return rendered_objects_; }
    unsigned rendered_sky_regions() const {return rendered_sky_regions_;}
    unsigned rendered_sky_triangles() const {return rendered_sky_triangles_;}
    unsigned rendered_animated_objects() const { return rendered_animated_objects_; }
    unsigned rendered_character_shadows() const {return rendered_character_shadows_;}
    unsigned rendered_shadow_triangles() const {return rendered_shadow_triangles_;}
    unsigned rendered_flame_streams() const { return rendered_flame_streams_; }
    unsigned rendered_flame_triangles() const { return rendered_flame_triangles_; }
    unsigned rendered_charge_smoke() const {return rendered_charge_smoke_;}
    unsigned rendered_sparx_sparkles() const {return rendered_sparx_sparkles_;}
    unsigned rendered_sparx_glows() const {return rendered_sparx_glows_;}
    unsigned rendered_power_effects() const {return rendered_power_effects_;}
    unsigned rendered_transparent_terrain_triangles() const {return rendered_transparent_terrain_triangles_;}
    unsigned rendered_element_effects() const {return rendered_element_effects_;}
    unsigned rendered_demon_eruptions() const {return rendered_demon_eruptions_;}
    bool rendered_dark_spyro() const {return rendered_dark_spyro_;}
    unsigned rendered_dark_smoke() const {return rendered_dark_smoke_;}
    unsigned rendered_eye_glows() const {return rendered_eye_glows_;}
    unsigned rendered_melee_trails() const {return rendered_melee_trails_;}
    unsigned rendered_shockwave_triangles() const {return rendered_shockwave_triangles_;}
    void save_bmp(const std::filesystem::path& path, ID3D11Texture2D* source=nullptr);
private:
    void initialize_character_shadows();
    void draw_character_shadows(const Level&,Vec3,const Player*,float,const SpyroModel&,const SpyroPose&,unsigned,
        const ObjectModels&,std::span<const RetailObject>,SuperchargeAppearance,std::span<const ObjectPose>,
        std::span<const RemoteSpyro>,const RiptoBoss*,bool);
    void enable_character_shadows(bool enabled);
    Microsoft::WRL::ComPtr<ID3D11VertexShader> shadow_vs_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shadow_depth_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> shadow_depth_view_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shadow_depth_resource_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadow_constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> shadow_sampler_;
    static constexpr unsigned shadow_map_size_=4096;
    std::array<float,20> shadow_values_{};
    unsigned rendered_character_shadows_{},rendered_shadow_triangles_{};
    void upload_splats(const Level& level);
    void draw_splats(const Level& level,Vec3 eye,Vec3 focus,Vec3 up,float fov);
    Microsoft::WRL::ComPtr<ID3D11VertexShader> splat_vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> splat_ps_,splat_depth_ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> splat_data_,splat_order_,splat_camera_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> splat_data_view_,splat_order_view_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> splat_depth_blend_;
    std::vector<std::pair<unsigned,unsigned>> splat_visible_;
    std::vector<unsigned> splat_sorted_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> splat_surface_buffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> splat_surface_texture_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> splat_surface_sampler_;
    unsigned splat_surface_vertices_{};
    void draw_vertices(std::span<const RenderVertex> vertices);
    void upload_level_geometry(const Level& level);
    void update_level_geometry(const Level& level);
    const Level* geometry_source_=nullptr;
    unsigned geometry_revision_{};
    std::vector<RenderVertex> terrain_cache_,collision_cache_;
    std::vector<unsigned> transparent_sources_;
    ID3D11Device* device_{}; ID3D11DeviceContext* context_{};
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> sky_vs_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> portal_vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> sky_ps_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> matrix_,level_buffer_,collision_buffer_,dynamic_buffer_,light_;
    unsigned lit_draws_{};
public:
    unsigned crown_lit_frames()const{return lit_draws_;}   // frames drawn with a Crownworks phase light
private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> sky_buffer_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> solid_,wire_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> transparent_depth_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> additive_blend_;
    std::array<Microsoft::WRL::ComPtr<ID3D11BlendState>,4> retail_blends_;
    struct TransparentTerrainTriangle {
        std::array<RenderVertex,3> vertices;
        Vec3 center;
        unsigned blend_mode{};
    };
    std::vector<TransparentTerrainTriangle> transparent_terrain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> color_,depth_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> color_view_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> spyro_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> backrealms_armor_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> level_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> object_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> flame_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> charge_smoke_texture_,ice_texture_;
    unsigned geometry_collision_revision_{};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hud_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> interface_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> upgrade_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> whirlwind_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> wing_texture_,power_particle_texture_,power_fire_texture_,power_bubble_texture_,power_sparkle_texture_,power_gate_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> element_sprite_texture_,dark_spyro_texture_,element_icon_texture_;
    unsigned rendered_element_effects_{},rendered_demon_eruptions_{},rendered_dark_smoke_{},rendered_eye_glows_{},rendered_melee_trails_{},rendered_shockwave_triangles_{};
    bool rendered_dark_spyro_=false;
    DarkSpyroKeep dark_keep_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> point_sampler_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_view_;
    unsigned width_{},height_{},level_vertices_{},collision_vertices_{};
    unsigned rendered_objects_{};
    unsigned rendered_sky_regions_{},rendered_sky_triangles_{};
    unsigned rendered_animated_objects_{};
    unsigned rendered_flame_streams_{},rendered_flame_triangles_{};
    unsigned rendered_charge_smoke_{};
    unsigned rendered_sparx_sparkles_{},rendered_sparx_glows_{};
    unsigned rendered_power_effects_{};
    unsigned rendered_transparent_terrain_triangles_{};
};
} // namespace spyro
