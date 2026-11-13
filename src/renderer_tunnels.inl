// Crownworks flooded machinery uses the mounted Summer Forest bubble sprite.
// Positions are evaluated from the fixed arena clock, so offline clips and
// normal play share the same 60 Hz timing and pause behaviour.
if(level.custom_arena && !studio && !collision && power_bubble_texture_) {
    const auto forward=normalized(focus-eye),right=normalized(cross(Vec3{0,1,0},forward)),up=cross(forward,right);
    const auto& arena=*level.custom_arena;const float time=arena.ticks+alpha;
    objects.clear();
    for(const auto& p:arena.pieces)if(p.kind==ArenaKind::UnderwaterJet && p.phase) {
        const bool active=p.phase==2;const unsigned count=active?32:5;
        for(unsigned i=0;i<count;++i) {
            const float phase=std::fmod(time*(active?.029f:.011f)+i*.618034f,1.f);
            const float a=i*2.399963f+time*.019f;
            const float spread=(.14f+phase*.42f)*(active?1:.65f);
            const auto center=p.center+Vec3{std::sin(a)*spread,.12f+phase*(active?2.95f:.55f),std::cos(a)*spread};
            const float radius=(.045f+.065f*phase)*(active?1.5f:1);
            const auto x=right*radius,y=up*radius;
            const Vec3 color=Vec3{.65f,.92f,1}*(.5f+.5f*std::sin(phase*3.14159265f));
            const std::array<Vec3,4> points{center-x-y,center+x-y,center+y-x,center+x+y};
            for(const auto k:{0,1,2,1,3,2})objects.push_back({points[k],color.x,color.y,color.z,1,float(k&1),float(k>>1),1});
        }
    }
    context_->OMSetDepthStencilState(transparent_depth_.Get(),0);
    context_->OMSetBlendState(additive_blend_.Get(),nullptr,0xffffffff);
    auto* bubble=power_bubble_texture_.Get();context_->PSSetShaderResources(0,1,&bubble);draw_vertices(objects);objects.clear();
    context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(depth_state_.Get(),0);
}
