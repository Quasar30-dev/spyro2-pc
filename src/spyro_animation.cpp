#include "spyro/spyro_model.hpp"
#include <algorithm>

namespace spyro {
void SpyroAnimator::reset(const SpyroModel* model,unsigned animation) {
    model_=model;state_={};requested_=animation;requested_ticks_=previous_ticks_=fraction_=0;
    if(model_) {
        const auto& clip=model_->animations.at(animation);
        state_.clip=state_.next_clip=state_.completed_clip=animation;
        state_.next_frame=clip.frames.size()>1?1:0;
        state_.blend=(clip.frames[0].ticks-1)<<4;
    }
    previous_=state_;
}
void SpyroAnimator::advance(bool outro,unsigned speed_step) {
    bool advance_frame=false;
    if(!outro && speed_step) {
        // 800347a4 / 8002e3f8: walking can use a distance-driven clock.
        if(state_.blend>=16)fraction_=model_->blends.at(state_.blend);
        fraction_+=speed_step;advance_frame=fraction_>255;
        if(advance_frame)fraction_-=256;
        state_.blend=fraction_>>4;
    } else if((state_.blend&15)==(state_.blend>>4)) {
        advance_frame=true;
        state_.blend=(model_->animations.at(state_.next_clip).frames.at(state_.next_frame).ticks-1)<<4;
    } else ++state_.blend;
    if(!advance_frame)return;
    state_.clip=state_.next_clip;state_.frame=state_.next_frame++;
    const auto& clip=model_->animations.at(state_.next_clip);
    if(outro) {
        if(!clip.transition_end || clip.transition_end>clip.frames.size())throw FormatError("Invalid Spyro animation exit window");
        if(state_.next_frame>=clip.transition_end-1) {
            state_.next_frame=0;state_.blend=0x71;state_.transition=0;
            state_.next_clip=state_.completed_clip=requested_;
        }
    } else if(state_.next_frame>=clip.loop_end)state_.next_frame=clip.loop_start;
}
void SpyroAnimator::transition() {
    const auto code=model_->transitions.at(state_.completed_clip*62+requested_);
    if(state_.transition==4 && (code==4 || code==5 || requested_==state_.completed_clip)) {advance(true);return;}
    if(requested_==state_.completed_clip)return;
    const auto promote=[&] {
        if(model_->blends.at(state_.blend)>95) {state_.clip=state_.next_clip;state_.frame=state_.next_frame;}
    };
    switch(code) {
        case 1:
            promote();state_.next_frame=0;state_.blend=0x71;break;
        case 2: {
            const auto& clip=model_->animations.at(requested_);
            state_.clip=requested_;state_.frame=0;state_.next_frame=requested_==26?0:std::min(1u,static_cast<unsigned>(clip.frames.size()-1));
            state_.blend=requested_==26?0:(clip.frames[0].ticks-1)<<4;break;
        }
        case 3:
            if(model_->blends.at(state_.blend)>95) {
                state_.frame=state_.next_frame++;
                const auto& clip=model_->animations.at(state_.clip);
                if(state_.next_frame>=clip.loop_end)state_.next_frame=clip.loop_start;
            }
            state_.blend=0x31;break;
        case 4: {
            const auto& clip=model_->animations.at(state_.clip);
            if(state_.clip==state_.next_clip && clip.transition_end==clip.loop_end &&
               state_.frame+1==clip.transition_end && state_.next_frame==clip.loop_start) {
                // A button can be released on the loop's final frame. Its
                // queued endpoint already wraps; blend out of the end pose
                // instead of treating that wrapped endpoint as an outro.
                state_.next_frame=0;state_.blend=0x71;
                state_.transition=0;state_.next_clip=state_.completed_clip=requested_;
            } else {state_.transition=4;advance(true);}
            return;
        }
        case 5:
            promote();state_.blend=0x71;
            if(state_.completed_clip!=6 || previous_ticks_>=24) {
                state_.next_frame=model_->animations.at(state_.clip).loop_end;
                state_.transition=4;return;
            }
            state_.next_frame=1;break;
        case 10:
            promote();state_.next_frame=model_->animations.at(requested_).loop_start;state_.blend=0x71;break;
        default:throw FormatError("Unimplemented Spyro transition opcode "+std::to_string(code));
    }
    state_.transition=0;state_.completed_clip=state_.next_clip=requested_;
}
void SpyroAnimator::tick(unsigned requested,unsigned speed_step) {
    if(!model_)return;
    if(!model_->animations.contains(requested))throw FormatError("Movement refers to missing retail Spyro animation "+
        std::to_string(requested)+" ("+animation_name(requested)+"), previous clip "+std::to_string(state_.clip));
    previous_=state_;
    if(requested_!=requested) {previous_ticks_=requested_ticks_;requested_ticks_=0;requested_=requested;}
    else ++requested_ticks_;
    if(state_.transition || state_.completed_clip!=requested_)transition();
    else advance(false,speed_step);
}
SpyroPose SpyroModel::pose(const SpyroAnimationState& state,SuperchargeAppearance appearance) const {
    const auto& a=clip(state.clip,appearance).frames.at(state.frame);
    const auto& b=clip(state.next_clip,appearance).frames.at(state.next_frame);
    if(a.vertices.size()!=b.vertices.size())throw FormatError("Incompatible Spyro transition vertex layouts");
    SpyroPose result;result.frame=state.frame;result.next_frame=state.next_frame;
    result.blend=blends.at(state.blend)/256.0f;result.attachment=lerp(a.attachment,b.attachment,result.blend);
    result.vertices.reserve(a.vertices.size());
    for(unsigned i=0;i<a.vertices.size();++i)result.vertices.push_back(lerp(a.vertices[i],b.vertices[i],result.blend));
    if(state.clip==61 && state.next_clip==61 && appearance==SuperchargeAppearance::MainModelChargeWings)
        charge_wings(result,frame_tick(61,state.frame,result.blend));
    return result;
}
SpyroPose SpyroAnimator::sample(float alpha,SuperchargeAppearance appearance) const {
    if(!model_)return {};
    alpha=std::clamp(alpha,0.0f,1.0f);auto result=model_->pose(state_,appearance);
    if(alpha<1) {
        const auto previous=model_->pose(previous_,appearance);
        if(previous.vertices.size()!=result.vertices.size()) {
            // Supercharge uses a distinct 255-vertex mesh. Opcode 2 cuts to
            // that layout; a presentation blend cannot pair its vertices.
            if(model_->transitions.at(previous_.completed_clip*62+state_.completed_clip)==2)return result;
            throw FormatError("Incompatible Spyro presentation poses");
        }
        for(unsigned i=0;i<result.vertices.size();++i)result.vertices[i]=lerp(previous.vertices[i],result.vertices[i],alpha);
        result.attachment=lerp(previous.attachment,result.attachment,alpha);
    }
    if(state_.clip==61 && state_.next_clip==61 && appearance==SuperchargeAppearance::MainModelChargeWings) {
        double tick=model_->frame_tick(61,state_.frame,model_->blends.at(state_.blend)/256.0f);
        if(alpha<1 && previous_.clip==61 && previous_.next_clip==61) {
            const double before=model_->frame_tick(61,previous_.frame,model_->blends.at(previous_.blend)/256.0f);
            if(tick<before) {
                const auto& clip=model_->animations.at(61);
                for(unsigned f=clip.loop_start;f<clip.loop_end;++f)tick+=clip.frames[f].ticks;
            }
            tick=before+(tick-before)*alpha;
        }
        // Re-evaluate the authored membrane at presentation time. Interpolating
        // twelve baked wing poses aliases the eleven-pose regular-charge loop.
        model_->charge_wings(result,tick);
    }
    return result;
}
void SpyroSecondaryAnimator::reset(const SpyroModel* model,const SpyroAnimationState& primary) {
    model_=model;state_={primary,0,6};previous_=state_;
}
void SpyroSecondaryAnimator::loop() {
    auto& p=state_.pose;
    if((p.blend&15)!=(p.blend>>4)) {++p.blend;return;}
    const auto& clip=model_->animations.at(p.next_clip);
    p.blend=(clip.frames.at(p.next_frame).ticks-1)<<4;
    p.clip=p.next_clip;p.frame=p.next_frame++;
    if(p.next_frame>=clip.loop_end)p.next_frame=clip.loop_start;
}
void SpyroSecondaryAnimator::transition(unsigned action,const SpyroAnimationState& primary) {
    auto& p=state_.pose;
    const auto follow=[&] {p=primary;state_.mode=6;};
    const auto promote=[&] {p.clip=p.next_clip;p.frame=p.next_frame;};
    if(state_.mode!=6 && state_.mode!=0) {
        if((p.blend&15)==(p.blend>>4)) {
            const auto& clip=model_->animations.at(p.next_clip);
            p.blend=(clip.frames.at(p.next_frame).ticks-1)<<4;
            switch(state_.mode) {
                case 1:promote();state_.mode=9;break;
                case 4:
                case 7:
                    if(!clip.transition_end || clip.transition_end>clip.frames.size())throw FormatError("Invalid secondary exit window");
                    promote();++p.next_frame;
                    if(p.next_frame>=clip.transition_end-1) {
                        if(state_.mode==4)state_.mode=9;
                        else {
                            p.blend=0x71;state_.mode=8;p.next_clip=primary.next_clip;p.next_frame=primary.next_frame;
                            state_.action=action;
                        }
                    }
                    break;
                case 8:follow();state_.action=action;break;
                default:throw FormatError("Unmapped secondary animation mode");
            }
        } else ++p.blend;
    }
    if(state_.mode==9 && state_.action==action) {
        switch(model_->secondary_transitions.at(state_.action*9)) {
            case 6:
                if(p.next_clip==primary.next_clip)follow();
                else {p.next_clip=primary.next_clip;p.blend=0x71;state_.mode=1;p.next_frame=primary.next_frame;}
                break;
            case 0:
                if(p.next_clip==model_->secondary_clips.at(state_.action)) {state_.mode=0;p.blend=0x71;loop();}
                else {promote();p.next_clip=model_->secondary_clips.at(state_.action);p.blend=0x71;p.next_frame=0;state_.mode=1;}
                break;
            case 7:++p.next_frame;state_.mode=7;break;
            default:throw FormatError("Unmapped secondary continuation opcode");
        }
    }
    if(state_.action==action)return;
    switch(model_->secondary_transitions.at(state_.action*8+action)) {
        case 4:
            if(p.next_clip==model_->secondary_clips.at(state_.action))state_.mode=4;
            break;
        case 1:
            if(model_->blends.at(p.blend)>95)promote();
            p.next_clip=model_->secondary_clips.at(action);p.next_frame=0;p.blend=0x71;state_.mode=1;
            break;
        case 6:follow();break;
        default:throw FormatError("Unmapped secondary transition opcode");
    }
    state_.action=action;
}
void SpyroSecondaryAnimator::tick(unsigned action,const SpyroAnimationState& primary,bool force_follow) {
    if(!model_)return;
    if(action>=model_->secondary_clips.size())throw FormatError("Invalid Spyro secondary action");
    previous_=state_;
    if(force_follow) {state_={primary,action,6};return;}
    if(state_.action==action && state_.mode==0)loop();
    else if(state_.action==action && state_.mode==6)state_.pose=primary;
    else transition(action,primary);
}
SpyroPose SpyroSecondaryAnimator::sample(float alpha,SuperchargeAppearance appearance) const {
    if(!model_)return {};
    alpha=std::clamp(alpha,0.0f,1.0f);auto result=model_->pose(state_.pose,appearance);
    if(alpha<1) {
        const auto previous=model_->pose(previous_.pose,appearance);
        if(previous.vertices.size()!=result.vertices.size()) {
            if(state_.mode==6 && model_->transitions.at(previous_.pose.clip*62+state_.pose.clip)==2)return result;
            throw FormatError("Incompatible secondary presentation poses");
        }
        for(unsigned i=0;i<result.vertices.size();++i)result.vertices[i]=lerp(previous.vertices[i],result.vertices[i],alpha);
        result.attachment=lerp(previous.attachment,result.attachment,alpha);
    }
    return result;
}
} // namespace spyro
