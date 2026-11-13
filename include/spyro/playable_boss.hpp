#pragma once
#include "spyro/objects.hpp"

namespace spyro {
class Game;
struct InputState;
enum class PlayableCharacter { Spyro, Crush, Gulp, Ripto };
const char* playable_character_name(PlayableCharacter character);
void import_playable_bosses(ObjectModels& models,WadArchive& wad,std::span<const std::uint8_t> executable);

class PlayableBoss {
public:
    PlayableCharacter character()const{return character_;}
    bool active()const{return character_!=PlayableCharacter::Spyro;}
    bool available(PlayableCharacter character,const ObjectModels* models)const;
    bool select(Game& game,PlayableCharacter character);
    void set_scale(Game& game,unsigned thirds);
    unsigned scale()const{return scale_;}
    void reset();
    void filter_input(Game& game,InputState& input,bool blocked);
    void tick(Game& game,bool blocked);
    static bool at_switch(const Game& game);
    float run_scale(bool sprint)const;
    float camera_scale()const;
    const RetailObject& body()const{return body_;}
    struct Shot {
        RetailObject body;
        Vec3 velocity;
        float gravity{},radius{};
        unsigned age{};
        std::uint32_t attack{};
    };
    struct Wave {Vec3 origin;unsigned age{};};
    std::span<const Shot> shots()const{return shots_;}
    std::span<const Wave> waves()const{return waves_;}
private:
    enum class Attack { None, Close, Ranged };
    PlayableCharacter character_=PlayableCharacter::Spyro;
    Attack attack_=Attack::None;
    unsigned attack_tick_{};
    unsigned scale_=3;
    bool sprint_{};
    bool pose_ready_{};
    RetailObject body_;
    std::vector<Shot> shots_;
    std::vector<Wave> waves_;
};
}
