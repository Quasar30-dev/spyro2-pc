namespace {
// 80038f90 creates one class-104 mesh per decimal digit. These are world
// objects with their own trajectories, not HUD text or additional pickups.
void queue_gem_numbers(std::vector<RetailObject>& pending,const RetailObject& gem,const ObjectModels& models,Vec3 camera_forward) {
    const auto bank=models.models.find(0x104);
    if(!gem.gem_value || gem.position.y*retail::units_per_world<768 || bank==models.models.end() || bank->second.animated || bank->second.meshes.size()<10)return;
    const unsigned heading=models.math.angle(int(camera_forward.x*4096),int(camera_forward.z*4096));
    const unsigned lateral=(heading-64)&255;
    auto position=retail::from_world(gem.position);int up=128;
    for(unsigned remaining=gem.gem_value;remaining;remaining/=10) {
        RetailObject digit;digit.type=0x104;digit.runtime_spawned=true;digit.held_gem=255;digit.draw_flags=8;
        // Class-104 allocator: Summer Forest 8007c138 / Winter 8007a550.
        // Palette zero is empty; material 2 supplies the gold face pair.
        digit.material_color=2;
        digit.model=std::uint8_t(remaining%10);digit.source_id=gem.id;digit.lifetime=96;
        digit.position=digit.previous_position=retail::to_world(position);
        digit.rotation[2]=float(heading);digit.previous_rotation=digit.rotation;
        digit.script_velocity={models.cosine[lateral]>>8,models.sine[lateral]>>8,up};
        digit.interpolate_position=true;digit.transform_span=2;pending.push_back(digit);
        position.x-=models.sine[heading]>>4;position.y+=models.cosine[heading]>>4;up-=16;
    }
}
void tick_gem_numbers(std::span<RetailObject> objects,const Level& level,bool script_tick) {
    for(auto& digit:objects)if(digit.runtime_spawned && digit.type==0x104 && digit.state>=0) {
        digit.transform_phase=std::min(digit.transform_phase+1,digit.transform_span);
        if(!script_tick)continue;
        if(!digit.lifetime){digit.state=-1;continue;}
        begin_object_transform(digit,2);--digit.lifetime;
        digit.rotation[2]=float((unsigned(digit.rotation[2])+1)&255);
        digit.script_velocity.z=std::max(-128,digit.script_velocity.z-6);
        const auto desired=retail::from_world(digit.position)+digit.script_velocity;
        if(desired.z<1024){digit.state=-1;continue;}
        Vec3 unused{};bool grounded=false;
        const auto next=level.resolve_sphere(retail::to_world(desired),256/retail::units_per_world,unused,grounded);
        const auto correction=next-retail::to_world(desired);
        if(length(correction)>0.00001f) {
            const auto normal=normalized(correction),velocity=retail::to_world(digit.script_velocity);
            const float toward=dot(velocity,normal);
            if(toward<0)digit.script_velocity=retail::from_world(velocity-normal*(2*toward));
        }
        digit.position=next;
    }
}
void install_gem_numbers(std::vector<RetailObject>& objects,std::span<const RetailObject> pending) {
    for(auto number:pending) {
        auto vacant=std::find_if(objects.begin(),objects.end(),[](const auto& o){return o.runtime_spawned && o.type==0x104 && o.state<0;});
        if(vacant!=objects.end()){number.id=vacant->id;*vacant=number;}
        else {
            for(const auto& o:objects){if(o.id==UINT32_MAX)throw FormatError("No free gem-feedback object ID");number.id=std::max(number.id,o.id+1);}
            objects.push_back(number);
        }
    }
}
}
