#pragma once
#include "spyro/objects.hpp"
#include "spyro/audio.hpp"
#include <functional>
#include <optional>
namespace spyro {
struct Level;
struct DialogueLine {
    std::string text;
    std::optional<XaClip> speech;
    int next=-1,repeat=-1,yes=-1,no=-1;
    bool supported=true;
    int camera_side=-1;
    int purchase=-1,insufficient=-1;
    std::array<std::string,2> answers{{"Yes","No"}};
    std::array<bool,2> answer_closes{};
    std::string formatted_text;
    std::array<std::string,2> formatted_answers;
    int level_script=-1;
    int talisman=-1;   // command 1: this line gives the level's talisman (the reward object's class)
    unsigned orb_bits=0;   // commands 2..9: this line gives orb bit (command-2) once its quest flag is set
};
struct NpcDefinition {
    unsigned object{},subtype{},initial{};
    bool automatic=false;
    std::vector<DialogueLine> lines;
    std::string name="Hunter";
    unsigned type=0x1bc,idle_clip=0,talk_clip=1;
    // Its own original script opens the conversation (and sets quest flags
    // first); native proximity only shows the prompt.
    bool script_speaker=false;
};
std::vector<NpcDefinition> decode_npcs(std::span<const std::uint8_t> archive,unsigned slot);
// Any record's conversation table (for script-owned speakers, whose own
// script opens the conversation through 80015be4).
std::optional<NpcDefinition> decode_npc_record(std::span<const std::uint8_t> archive,unsigned slot,unsigned index);
struct DialogueInput {bool interact=false,next=false,cancel=false;int choice=-1;};
struct DialogueEconomy {unsigned* gems=nullptr;std::uint32_t* purchases=nullptr;unsigned orbs{};};
struct NpcProgress {unsigned object{},node{};bool automatic{};};
class NpcInteractions {
public:
    void reset(const Level& level);
    void dismiss(std::vector<RetailObject>& objects){close(objects);}
    void tick(const Level& level,std::vector<RetailObject>& objects,const ObjectModels* models,Vec3 player,float yaw,bool grounded,bool charging,DialogueInput input,DialogueEconomy economy={});
    bool active()const{return active_!=UINT32_MAX;}
    unsigned object()const;
    const DialogueLine* line()const;
    unsigned elapsed()const{return elapsed_;}
    unsigned revision()const{return revision_;}
    unsigned conversations()const{return conversations_;}
    bool nearby()const{return nearby_!=UINT32_MAX;}
    const char* name()const;
    std::string display_text(bool formatted=false)const;
    int camera_side()const{return line()?line()->camera_side:-1;}
    // Hunter's subtype 6 changes the player's camera preference.
    bool active_camera=true;
    std::vector<NpcProgress> progress() const;
    void restore(std::span<const NpcProgress> progress);
    void complete_custom_objective(unsigned object);
    std::optional<std::pair<unsigned,int>> take_level_script(){auto result=level_script_;level_script_.reset();return result;}
    // Original conversation logic (RetailScripts): when set, the opening line,
    // every advance/answer and the close go through the retail functions and
    // these hooks; this class only presents the lines.
    std::function<std::optional<unsigned>(unsigned object)> retail_begin;
    std::function<std::optional<unsigned>(unsigned object,int answer)> retail_advance;
    std::function<void(unsigned object)> retail_close;
    // A conversation a script opened (the speaker may not be a native NPC).
    void begin_external(const NpcDefinition& npc,unsigned line);
    // Finishes text-less lines through the original logic; false when the
    // conversation ended there.
    bool skip_empty_lines(unsigned index);
    bool retail_driven()const{return active() && external_;}
private:
    std::vector<NpcDefinition> npcs_;
    std::vector<unsigned> current_;
    std::vector<bool> automatic_;
    unsigned active_=UINT32_MAX,nearby_=UINT32_MAX,elapsed_{},revision_{},conversations_{};
    bool external_=false;
    std::optional<unsigned> display_price_;
    std::optional<std::pair<unsigned,int>> level_script_;
    void close(std::vector<RetailObject>& objects);
    void select(unsigned node);
};
} // namespace spyro
