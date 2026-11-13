#include "spyro/game_build.hpp"
#include "spyro/audio.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <set>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void require(View data,std::size_t at,std::size_t count) {
    if(at>data.size() || count>data.size()-at)throw FormatError("Truncated retail audio data");
}
int sample(int value,unsigned header,int& last,int& older,bool spu) {
    constexpr int positive[]={0,60,115,98,122},negative[]={0,0,-52,-55,-60};
    const auto filter=header>>4;if(filter>=(spu?5u:4u))throw FormatError("Invalid ADPCM predictor");
    const unsigned shift=(header&15)>12?9:header&15;
    // SPU truncates each predictor product separately. The CD-XA decoder
    // rounds their sum; sharing that rounding subtly changes SPU waveforms.
    const int prediction=spu?((last*positive[filter])>>6)+((older*negative[filter])>>6):
        (last*positive[filter]+older*negative[filter]+32)>>6;
    const int decoded=std::clamp((value>>shift)+prediction,-32768,32767);
    older=last;last=decoded;return decoded;
}
std::size_t next_chunk(View data,std::size_t at) {
    const auto size=read_u32(data,at);if(size<4 || size%4)throw FormatError("Invalid audio section chain");
    require(data,at,size);return at+size;
}
SoundBank decode_yotd_sound_bank(View archive,View exe,unsigned slot,const GameProfile& profile,unsigned area) {
    const auto children=parse_level_archive(archive,profile);
    constexpr unsigned sample_base=0x80000;
    if(children.size()!=4 || children[0].size<sample_base)throw FormatError("Missing YOTD SPU bank");
    const auto scene=archive.subspan(children[1].offset,children[1].size);
    // Four initial chunks, grid dimensions, collision, sky, four post-sky
    // chunks, then 32 raw surface IDs and the unchanged descriptor chunk.
    std::size_t at=0;for(unsigned i=0;i<4;++i)at=next_chunk(scene,at);
    at=next_chunk(scene,at+12);at=next_chunk(scene,at);
    for(unsigned i=0;i<4;++i)at=next_chunk(scene,at);
    require(scene,at,0x128);const auto size=read_u32(scene,at+0x20),count=read_u32(scene,at+0x124);
    if(count>255 || size!=0x108+count*20)throw FormatError("Invalid YOTD sound descriptor table");
    require(scene,at+0x20,size);SoundBank result;result.source_game=profile.build;result.native_samples=count;
    std::copy_n(scene.begin()+at,32,result.surfaces.begin());result.native_surfaces=result.surfaces;
    std::copy_n(scene.begin()+at+0x24,256,result.native_globals.begin());
    result.globals.fill(255);
    // Resident 8004cfb8 indexes this 37x4 matrix by main-level index and
    // surface category. Wrapper 8003bb10 then indexes the raw surface map.
    // Side areas share the main level's selector and SPU bank.
    if(slot<97 || slot>169 || !(slot&1))throw FormatError("Unavailable YOTD footstep scene");
    constexpr std::size_t footstep_table=0x80067968u-0x80010000u+0x800u;
    const auto footstep_at=footstep_table+(slot-97)/2*4;require(exe,footstep_at,4);
    std::copy_n(exe.begin()+footstep_at,4,result.footsteps.begin());
    // Exact full compressed-waveform matches with the local US S2 banks.
    // Unknown event IDs remain unavailable until explicit donor fallback.
    constexpr std::array<std::array<unsigned,2>,19> globals{{
        {0,0},{1,1},{2,2},{3,3},{6,6},{7,7},{8,8},{10,10},{16,16},{18,18},{20,20},
        {21,22},{22,23},{23,24},{25,26},{28,29},{30,31},{34,34},{37,37}}};
    for(const auto& pair:globals)result.globals[pair[0]]=result.native_globals[pair[1]];
    const auto samples=archive.subspan(children[0].offset+sample_base,children[0].size-sample_base);
    for(unsigned i=0;i<count;++i) {
        const auto p=at+0x128+i*20;SoundSample sound;
        sound.offset=read_u32(scene,p);sound.volume_min=read_u16(scene,p+4);sound.volume_max=read_u16(scene,p+6);
        sound.near_distance=read_u16(scene,p+8);sound.far_distance=read_u16(scene,p+10);
        sound.pitch_min=read_u16(scene,p+12);sound.pitch_max=read_u16(scene,p+14);
        sound.looping=scene[p+16]!=0;sound.priority=scene[p+17];
        require(samples,sound.offset,16);if(sound.offset%16)throw FormatError("Unaligned YOTD SPU sound address");
        sound.pcm=decode_spu(samples.subspan(sound.offset));result.sounds.push_back(std::move(sound));
    }
    std::size_t directory=level_model_directory(archive,profile),base=0;
    if(area) {
        if(area>4)throw FormatError("Invalid YOTD object sound area");
        const auto header=0x20+(area-1)*16;require(archive,header,8);
        const auto blob=read_u32(archive,header),bytes=read_u32(archive,header+4);
        if(!blob || !bytes)throw FormatError("Missing YOTD object sound area");
        require(archive,blob,bytes);const auto banks=read_u32(archive,blob);
        if(banks<4 || banks>bytes || bytes-banks<0x180)throw FormatError("Invalid YOTD object sound directory");
        base=std::size_t(blob)+4;directory=std::size_t(blob)+banks;
    }
    for(unsigned i=1;i<63;++i) {
        const auto begin=read_u32(archive,directory+i*4),end=read_u32(archive,directory+(i+1)*4);if(!end)break;
        if(!begin || end<begin)throw FormatError("Invalid YOTD object sound bank");
        require(archive,base+begin,end-begin);const auto type=read_u16(archive,directory+0x100+i*2);
        const bool animated=std::bit_cast<std::int32_t>(read_u32(archive,base+begin))>0;
        const unsigned bytes=animated?16:8;if(end-begin<bytes)throw FormatError("Truncated YOTD object sound header");
        result.object_sounds[type]=std::vector<std::uint8_t>(archive.begin()+base+begin+4,archive.begin()+base+begin+bytes);
    }
    return result;
}
}
LevelMusic decode_level_music(View exe,unsigned slot) {
    const ExeTables tables(exe);
    if(slot<15 || slot>95 || !(slot&1) || !tables.profile.physical_slot(slot))throw FormatError("Unavailable level music scene");
    LevelMusic result;result.track=exe[tables(0x80060140)+(slot-15)/2];
    const auto p=tables(0x80064de4)+result.track*8;
    result.clip={read_u32(exe,p),read_u16(exe,p+4),read_u16(exe,p+6),1};
    if(tables.profile.build==GameBuild::July1999) {
        // 800171e0 calls 80012914 with absolute disc addresses. These three
        // catalog extents identify separate files, unlike August/retail's
        // relative offsets into the combined SPEECH.STR.
        const unsigned address=result.clip.sector;
        if(address>=153584){result.clip.asset="SPYRO_2.STR";result.clip.sector-=153584;}
        else if(address>=129032){result.clip.asset="SPYRO_1.STR";result.clip.sector-=129032;}
        else if(address>=104000){result.clip.asset="MUSIC1.STR";result.clip.sector-=104000;}
        else throw FormatError("Unmapped July music file extent");
    } else if(tables.profile.demo()) {
        // 8001a584 patches all track addresses/lengths after finding
        // SPYRO2/DEMO2.STR; the executable supplies the channel selection.
        result.clip.asset="DEMO2.STR";result.clip.sector=0;result.clip.sectors=0x3bc4;
    }
    if(!result.clip.sectors || result.clip.channel>31)throw FormatError("Invalid level music extent");
    return result;
}
bool XaDecoder::append(View sector,const XaClip& filter,PcmAudio& out) {
    if(sector.size()==2352) {
        if(sector[0]!=0 || sector[1]!=255 || sector[11]!=0 || sector[15]!=2)throw FormatError("Invalid XA sector header");
        sector=sector.subspan(16);
    }
    if(sector.size()!=2336)throw FormatError("XA audio requires complete Form 2 sectors; this copy contains only 2048-byte data");
    if(!std::equal(sector.begin(),sector.begin()+4,sector.begin()+4))throw FormatError("XA subheader copies disagree");
    if(sector[0]!=filter.file || sector[1]!=filter.channel || (sector[2]&0x24)!=0x24)return false;
    const unsigned coding=sector[3];
    if((coding&0xaa) || (coding&0x40))throw FormatError("Unsupported XA coding or emphasis");
    if(coding_>=0 && coding_!=static_cast<int>(coding))throw FormatError("XA coding changed inside a stream");
    coding_=static_cast<int>(coding);out.channels=(coding&1)?2:1;out.rate=(coding&4)?18900:37800;
    const bool wide=(coding&16)!=0;const unsigned units=wide?4:8;
    for(unsigned group=0;group<18;++group) {
        const auto data=sector.subspan(8+group*128,128);
        const auto at=out.samples.size();out.samples.resize(at+units*28);
        for(unsigned unit=0;unit<units;++unit) {
            const auto channel=unit%out.channels,frame=(unit/out.channels)*28;
            // 8-bit units use the first four parameter bytes; 4-bit units
            // use the eight middle bytes (the remaining bytes are copies).
            const auto header=data[wide?unit:4+unit];
            for(unsigned i=0;i<28;++i) {
                const unsigned packed=data[16+i*4+(wide?unit:unit/2)];
                const int value=wide?static_cast<int>(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(packed)))*256:
                    (static_cast<int>((packed>>((unit&1)*4))&15)^8)-8;
                out.samples[at+(frame+i)*out.channels+channel]=static_cast<std::int16_t>(
                    sample(wide?value:value*4096,header,last_[channel],older_[channel],false));
            }
        }
    }
    return true;
}
PcmAudio decode_spu(View data) {
    PcmAudio result;int last=0,older=0;
    for(std::size_t p=0;p+16<=data.size();p+=16) {
        const unsigned flags=data[p+1];
        if(flags&4)result.loop_start=result.frames();
        for(unsigned i=0;i<28;++i) {
            const auto packed=(data[p+2+i/2]>>((i&1)*4))&15;
            const int value=(static_cast<int>(packed)^8)-8;
            result.samples.push_back(static_cast<std::int16_t>(sample(value*4096,data[p],last,older,true)));
        }
        if(flags&1) {if(!(flags&2))result.loop_start.reset();return result;}
    }
    throw FormatError("SPU sample has no end block inside its bank");
}
PcmAudio decode_cinematic_sound(View movie,std::span<const unsigned> chunks,unsigned ticks) {
    // 80073c64 streams 0x6270 bytes every 120 VBlanks. End/loop flags
    // describe SPU double buffering, not the end of the movie's soundtrack.
    PcmAudio result;result.rate=22050;int last=0,older=0;
    for(const auto chunk:chunks) {
        const auto at=std::uint64_t(chunk)+read_u32(movie,chunk);
        require(movie,at,0x6270);
        for(unsigned p=0;p<0x6270;p+=16)for(unsigned i=0;i<28;++i) {
            const auto packed=(movie[at+p+2+i/2]>>((i&1)*4))&15;
            result.samples.push_back(static_cast<std::int16_t>(sample(((int(packed)^8)-8)*4096,movie[at+p],last,older,true)));
        }
    }
    // Keep the stream locked to the native 60 Hz cinematic timeline. Retail
    // uses a slightly reduced SPU pitch (0x7f8) to protect its DMA buffers.
    result.samples.resize(std::size_t(ticks)*result.rate/60);
    return result;
}
PcmAudio read_xa(const std::filesystem::path& source,const XaClip& clip,const std::function<bool()>& cancelled) {
    if(!clip.sectors || clip.sectors>150*60*20 || clip.channel>255 || clip.file>255)throw FormatError("Invalid XA stream extent");
    AssetStore assets(source);std::string name=clip.asset;
    if(!assets.files().contains(name) && !assets.files().contains(name+".RAW") &&
       (assets.files().contains("SPYRO2/"+name) || assets.files().contains("SPYRO2/"+name+".RAW")))name="SPYRO2/"+name;
    if(assets.files().contains(name+".RAW"))name+=".RAW";
    const auto& file=assets.file(name);
    std::ifstream extracted;std::uint64_t start=0,bytes=file.size,available=0;unsigned stride=2352;bool packed_disc=false;
    if(!file.disk_path.empty()) {
        extracted.open(file.disk_path,std::ios::binary);if(!extracted)throw FormatError("Cannot open extracted SPEECH.STR");
        std::array<std::uint8_t,16> head{};extracted.read(reinterpret_cast<char*>(head.data()),head.size());
        if(!extracted)throw FormatError("Truncated SPEECH.STR");
        if(std::memcmp(head.data(),"RIFF",4)==0 && std::memcmp(head.data()+8,"CDXA",4)==0) {
            // Walk RIFF chunks; the data chunk is not necessarily at byte 44.
            std::uint64_t at=12;bool found=false;
            while(at+8<=file.size) {
                extracted.seekg(static_cast<std::streamoff>(at));extracted.read(reinterpret_cast<char*>(head.data()),8);
                if(!extracted)throw FormatError("Truncated CDXA chunk");const auto n=read_u32(head,4);
                if(n>file.size-at-8)throw FormatError("CDXA chunk exceeds its file");
                if(std::memcmp(head.data(),"data",4)==0){start=at+8;bytes=n;found=true;break;}
                at+=8+std::uint64_t(n)+(n&1);
            }
            if(!found)throw FormatError("CDXA has no sector data");
        } else if(head[0]==0 && head[1]==255 && head[11]==0 && head[15]==2)stride=2352;
        else if(std::equal(head.begin(),head.begin()+4,head.begin()+4) && (head[2]&0x24)==0x24)stride=2336;
        else throw FormatError("SPEECH.STR is missing its XA headers; extract with full sectors or mount your CUE/BIN");
        if(bytes%stride)throw FormatError("Incomplete extracted XA sector");
        available=bytes/stride;
    } else {
        // Jampack stores complete 2336-byte XA records inside ordinary Form 1
        // ISO file data. They can cross physical-sector boundaries. Normal
        // retail Form 2 streams instead retain their physical subheaders.
        try {
            const auto head=assets.read(name,0,8);
            packed_disc=std::equal(head.begin(),head.begin()+4,head.begin()+4) && (head[2]&0x24)==0x24;
        }catch(const FormatError&){/* A Form 2 sector cannot be read as ISO file data. */}
        if(packed_disc) {
            stride=2336;
            if(bytes%stride)throw FormatError("Incomplete packed XA sector");
            available=bytes/stride;
        } else available=(std::uint64_t(file.size)+2047)/2048;
    }
    if(clip.sector>=available)throw FormatError("XA stream starts outside its file");
    const auto sectors=static_cast<unsigned>(std::min<std::uint64_t>(clip.sectors,available-clip.sector));
    bool ended=false;
    XaDecoder decoder;PcmAudio result;result.samples.reserve(static_cast<std::size_t>(clip.sectors)*504);
    for(unsigned i=0;i<sectors;++i) {
        if(cancelled && cancelled())return {};
        Bytes sector;
        if(extracted.is_open()) {
            sector.resize(stride);extracted.seekg(static_cast<std::streamoff>(start+std::uint64_t(clip.sector+i)*stride));
            extracted.read(reinterpret_cast<char*>(sector.data()),stride);if(!extracted)throw FormatError("Short XA sector read");
        } else if(packed_disc)sector=assets.read(name,std::uint64_t(clip.sector+i)*stride,stride);
        else sector=assets.read_raw_sector(file.lba+clip.sector+i);
        if(decoder.append(sector,clip,result)) {
            // End-of-file belongs to the selected XA file/channel. July's
            // Hurricos catalog overshoots SPYRO_2.STR, but channel 1 ends at
            // record 28657. Never read the following speech file as music.
            const unsigned header=sector.size()==2352?16:0;
            if(sector[header+2]&0x80){ended=true;break;}
        }
    }
    if(sectors<clip.sectors && !ended)throw FormatError("XA stream exceeds its file without a matching end marker");
    if(result.samples.empty())throw FormatError("No audio sectors matched the retail stream filter");
    return result;
}
SoundBank decode_sound_bank(View archive,View exe,unsigned slot,unsigned area) {
    const ExeTables tables(exe);
    if(tables.profile.yotd())return decode_yotd_sound_bank(archive,exe,slot,tables.profile,area);
    if(area)throw FormatError("Additional sound areas are only mapped for YOTD");
    const auto children=parse_level_archive(archive);
    if(children.size()!=4 || children[0].size<0x86000)throw FormatError("Missing level SPU bank");
    const auto scene=archive.subspan(children[1].offset,children[1].size);
    std::size_t at=0;for(unsigned i=0;i<6;++i)at=next_chunk(scene,at);
    at=next_chunk(scene,at+12);for(unsigned i=0;i<(compact_demo_layout(archive)?3u:4u);++i)at=next_chunk(scene,at);
    require(scene,at,0x128);const auto size=read_u32(scene,at+0x20),count=read_u32(scene,at+0x124);
    if(count>255 || size!=0x108+count*20)throw FormatError("Invalid sound descriptor table");
    require(scene,at+0x20,size);SoundBank result;
    std::copy_n(scene.begin()+at,32,result.surfaces.begin());std::copy_n(scene.begin()+at+0x24,256,result.globals.begin());
    const auto roll_sample=tables.profile.build==GameBuild::July1999?result.globals[0x14]:255;
    result.globals[july_roll_sound_id]=static_cast<std::uint8_t>(roll_sample);
    if(tables.profile.prerelease()) {
        const auto native=result.globals;
        // Prerelease global 0x14 was removed before retail, shifting the
        // following IDs. August charge/jump are 0x22/0x23; July and Jampack
        // use 0/1. Resolve once for live playback, captures and menu sounds.
        // Evidence: July 8002f19c/80029c30 and cross-build SPU waveform hashes
        // in prerelease-20260930/spyro-sound-id-matches.json.
        for(unsigned id=0x14;id<=0x22;++id)result.globals[id]=native[id+1];
        result.globals[0x23]=255; // No corresponding prerelease event.
        if(tables.profile.build==GameBuild::July1999 || tables.profile.demo()) {
            result.globals[0]=result.globals[1]=255; // Retail menu cues are absent.
            result.globals[0x21]=native[0];result.globals[0x22]=native[1];
        }
    }
    if(slot>=15 && slot<=95 && (slot&1))std::copy_n(exe.begin()+tables(0x800602b4)+(slot-15)/2*4,4,result.footsteps.begin());
    // The title scene uses 80077374: no preceding 0x6000-byte SPU work area.
    const unsigned sample_base=slot==95?0x80000:(tables.profile.build==GameBuild::July1999 || tables.profile.demo()?0x82800:0x86000);
    const auto samples=archive.subspan(children[0].offset+sample_base,children[0].size-sample_base);
    for(unsigned i=0;i<count;++i) {
        const auto p=at+0x128+i*20;SoundSample sound;
        sound.offset=read_u32(scene,p);sound.volume_min=read_u16(scene,p+4);sound.volume_max=read_u16(scene,p+6);
        sound.near_distance=read_u16(scene,p+8);sound.far_distance=read_u16(scene,p+10);
        sound.pitch_min=read_u16(scene,p+12);sound.pitch_max=read_u16(scene,p+14);sound.looping=scene[p+16]!=0;sound.priority=scene[p+17];
        require(samples,sound.offset,16);if(sound.offset%16)throw FormatError("Unaligned SPU sound address");
        sound.pcm=decode_spu(samples.subspan(sound.offset));result.sounds.push_back(std::move(sound));
    }
    for(unsigned i=1;i<63;++i) {
        const auto begin=read_u32(archive,level_model_directory(archive)+i*4),end=read_u32(archive,level_model_directory(archive)+4+i*4);if(!end)break;
        if(!begin || end<begin)throw FormatError("Invalid object sound bank directory");require(archive,begin,end-begin);
        const auto type=read_u16(archive,level_model_directory(archive)+0x100+i*2);const bool animated=std::bit_cast<std::int32_t>(read_u32(archive,begin))>0;
        require(archive,begin,animated?16:8);
        result.object_sounds[type]=std::vector<std::uint8_t>(archive.begin()+begin+4,archive.begin()+begin+(animated?16:8));
    }
    return result;
}
void import_yotd_sound_fallbacks(SoundBank& target,const SoundBank& donor,std::span<const unsigned> power_types) {
    if(target.source_game!=GameBuild::YearOfTheDragonUS)throw FormatError("YOTD sound fallback applied to another profile");
    if(target.pc_fallbacks_imported)return;
    std::map<unsigned,unsigned> imported;
    const auto sample=[&](unsigned id) -> std::uint8_t {
        if(id>=donor.sounds.size())return 255;
        if(const auto found=imported.find(id);found!=imported.end())return static_cast<std::uint8_t>(found->second);
        const auto& source=donor.sounds[id];
        const auto equal=[&](const SoundSample& other) {
            return source.volume_min==other.volume_min && source.volume_max==other.volume_max &&
                source.near_distance==other.near_distance && source.far_distance==other.far_distance &&
                source.pitch_min==other.pitch_min && source.pitch_max==other.pitch_max && source.priority==other.priority &&
                source.looping==other.looping && source.pcm==other.pcm;
        };
        if(const auto existing=std::find_if(target.sounds.begin(),target.sounds.end(),equal);existing!=target.sounds.end()) {
            const auto index=static_cast<unsigned>(existing-target.sounds.begin());imported[id]=index;return static_cast<std::uint8_t>(index);
        }
        if(target.sounds.size()>=255)throw FormatError("YOTD PC sound bank is full");
        const auto index=static_cast<unsigned>(target.sounds.size());target.sounds.push_back(source);imported[id]=index;
        return static_cast<std::uint8_t>(index);
    };
    for(unsigned id=0;id<target.globals.size();++id)if(target.globals[id]>=target.native_samples)target.globals[id]=sample(donor.globals[id]);
    // Native footsteps and surface IDs have an exact resident selector and
    // stay attached to the mounted level, including intentional 255 entries.
    for(const auto type:power_types) {
        const auto found=donor.object_sounds.find(type);if(found==donor.object_sounds.end())continue;
        auto& list=target.object_sounds[type|0x8000u];list.clear();
        for(const auto id:found->second)list.push_back(sample(id));
    }
    target.pc_fallbacks_imported=true;
}
bool import_july_roll_sound(SoundBank& target,const SoundBank& donor) {
    const auto id=donor.globals[july_roll_sound_id];
    if(id>=donor.sounds.size() || donor.sounds[id].looping || donor.sounds[id].pcm.samples.empty())return false;
    const auto& sample=donor.sounds[id];
    const auto existing=std::find(target.sounds.begin(),target.sounds.end(),sample);
    if(existing!=target.sounds.end())target.globals[july_roll_sound_id]=static_cast<std::uint8_t>(existing-target.sounds.begin());
    else {
        if(target.sounds.size()>=255)throw FormatError("July roll sound bank is full");
        target.globals[july_roll_sound_id]=static_cast<std::uint8_t>(target.sounds.size());target.sounds.push_back(sample);
    }
    return true;
}
void import_object_sounds(SoundBank& target,const SoundBank& donor,std::span<const unsigned> types,bool replace) {
    std::map<unsigned,unsigned> imported;
    for(unsigned type:types) {
        if(!replace && target.object_sounds.contains(type))continue;
        const auto source=donor.object_sounds.find(type);if(source==donor.object_sounds.end())continue;
        auto& list=target.object_sounds[type];list.clear();
        for(unsigned id:source->second) {
            if(id>=donor.sounds.size()){list.push_back(255);continue;}
            if(!imported.contains(id)) {
                if(target.sounds.size()>=255)throw FormatError("Imported sound bank is full");
                imported[id]=static_cast<unsigned>(target.sounds.size());target.sounds.push_back(donor.sounds[id]);
            }
            list.push_back(static_cast<std::uint8_t>(imported.at(id)));
        }
    }
}
void write_wave(const std::filesystem::path& path,const PcmAudio& audio) {
    if(audio.channels<1 || audio.channels>2 || !audio.rate || audio.samples.size()>UINT32_MAX/2-44)throw FormatError("Invalid PCM output");
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary);if(!out)throw FormatError("Cannot write audio preview");
    const auto word=[&](unsigned n,unsigned bytes){for(unsigned i=0;i<bytes;++i)out.put(static_cast<char>(n>>(i*8)));};
    const auto size=static_cast<unsigned>(audio.samples.size()*2);
    out.write("RIFF",4);word(size+36,4);out.write("WAVEfmt ",8);word(16,4);word(1,2);word(audio.channels,2);
    word(audio.rate,4);word(audio.rate*audio.channels*2,4);word(audio.channels*2,2);word(16,2);out.write("data",4);word(size,4);
    for(const auto s:audio.samples)word(static_cast<std::uint16_t>(s),2);
    if(!out)throw FormatError("Failed writing audio preview");
}
} // namespace spyro
