#pragma once
#include "spyro/objects.hpp"
#include <string>
namespace spyro {
class Game;struct InputState;struct Level;
struct ScriptRoute {unsigned address{},initial{};int direction=1;std::vector<EnemyWaypoint> points;};
enum class ScriptRole { Burner,Fan,Machine,Cannon,CannonTarget,Trolley,Gear,TrackSwitch,TrackObstacle,Worker,
    FlightTarget,Hunter,HunterTarget };
struct ScriptDefinition {
    unsigned object{},type{};ScriptRole role{};
    std::array<unsigned,16> parameters{};
    std::vector<ScriptRoute> routes;
    unsigned category=UINT32_MAX;bool pass_through=false;
};
void decode_level_systems(std::span<const std::uint8_t> archive,Level& level);
struct ScriptProgress {unsigned object{},type{},flags{},ticks{},cursor{};float progress{};};
enum class ChallengeMode { None,Trial,Results,FreeFlight,Hunter,HunterResults,Trolley,TrolleyResults };
class LevelSystems {
public:
    void reset(Game& game,const ObjectModels* models);
    bool before_move(Game& game,InputState& input,const ObjectModels* models);
    void after_move(Game& game,const InputState& input,const ObjectModels* models);
    void start_trial(Game& game,const ObjectModels* models,bool free_flight=false);
    void start_trolley(Game& game,const ObjectModels* models);
    void start_hunter(Game& game,const ObjectModels* models);
    void leave_challenge(Game& game);
    bool speedway()const{return flight_index<4;}
    bool results()const{return mode==ChallengeMode::Results || mode==ChallengeMode::HunterResults || mode==ChallengeMode::TrolleyResults;}
    std::vector<ScriptProgress> checkpoint()const;
    void restore(Game& game,std::span<const ScriptProgress> state,const ObjectModels* models);
    ChallengeMode mode=ChallengeMode::None;
    unsigned flight_index=UINT32_MAX,remaining{},elapsed{},bonus_ticks{},last_bonus{},hunter_count{},hunter_total{},gears{},cannon=UINT32_MAX;
    std::array<unsigned,4> counts{};
    bool success=false;std::string prompt;
    unsigned activations{},cannon_shots{},cannon_hits{},track_switches{},trolley_jumps{},challenge_wins{};
private:
    std::vector<ScriptProgress> state_;
    struct Shot {Vec3 position,previous,velocity;unsigned object=UINT32_MAX,age{};bool trolley=false;};
    std::vector<Shot> shots_;
    unsigned cooldown_{},route_{},point_{},jump_ticks_{},result_ticks_{};
    float route_distance_{},trolley_height_{},trolley_vertical_{};
    Vec3 start_position_{};
    float start_yaw_{};
    bool previous_talking_=false;
    std::vector<ScriptRoute> trolley_routes_;
    bool tick_vehicle(Game& game,InputState& input,const ObjectModels* models);
    void finish_trial(Game& game,bool won);
    void tick_machines(Game& game,const ObjectModels* models);
    void tick_shots(Game& game,const ObjectModels* models);
    void fire_cannon(Game& game,const ObjectModels* models,Vec3 origin,Vec3 direction,bool trolley);
};
} // namespace spyro
