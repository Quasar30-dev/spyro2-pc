#include "spyro/audio.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <fstream>
using namespace spyro;
namespace {
unsigned checks=0;
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
template<class F> void rejects(F f,const char* message){bool failed=false;try{f();}catch(const FormatError&){failed=true;}check(failed,message);}
void extracted_readers(const Bytes& xa,const PcmAudio& expected) {
    const auto folder=std::filesystem::temp_directory_path()/
        ("spyro-audio-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(folder))throw std::runtime_error("Cannot create audio fixture");
    struct Cleanup {std::filesystem::path folder;~Cleanup(){std::error_code e;
        std::filesystem::remove(folder/"SPEECH.STR",e);std::filesystem::remove(folder/"SPEECH.STR.raw",e);std::filesystem::remove(folder,e);}} cleanup{folder};
    const auto write=[&](const char* name,const Bytes& bytes){std::ofstream out(folder/name,std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!out)throw std::runtime_error("Cannot write audio fixture");};
    write("SPEECH.STR",xa);
    check(read_xa(folder,{0,1,2}).samples==expected.samples,"2336-byte extracted XA differs");
    rejects([&]{read_xa(folder,{0,2,2});},"Truncated XA without an end marker was accepted");
    auto ended=xa;ended[2]|=0x80;ended[6]|=0x80;write("SPEECH.STR",ended);
    check(read_xa(folder,{0,2,2}).samples==expected.samples,"Stale XA length ignored the selected stream's end marker");
    auto following=xa;following[3]=following[7]=4;auto combined=ended;combined.insert(combined.end(),following.begin(),following.end());
    write("SPEECH.STR",combined);
    check(read_xa(folder,{0,2,2}).samples==expected.samples,"XA decode continued into another stream after EOF");
    ended[1]=ended[5]=3;combined=ended;combined.insert(combined.end(),xa.begin(),xa.end());write("SPEECH.STR",combined);
    check(read_xa(folder,{0,2,2}).samples==expected.samples,"Another channel's end marker stopped the selected stream");
    rejects([&]{read_xa(folder,{0,3,2});},"Another channel's end marker accepted a truncated stream");
    Bytes raw(2352);std::fill(raw.begin()+1,raw.begin()+11,255);raw[15]=2;std::copy(xa.begin(),xa.end(),raw.begin()+16);
    write("SPEECH.STR",raw);
    check(read_xa(folder,{0,1,2}).samples==expected.samples,"2352-byte extracted XA differs");
    Bytes riff{'R','I','F','F',0,0,0,0,'C','D','X','A','J','U','N','K',3,0,0,0,1,2,3,0,'d','a','t','a'};
    for(unsigned i=0;i<4;++i)riff.push_back(static_cast<std::uint8_t>(raw.size()>>(i*8)));
    riff.insert(riff.end(),raw.begin(),raw.end());
    for(unsigned i=0;i<4;++i)riff[4+i]=static_cast<std::uint8_t>((riff.size()-8)>>(i*8));
    write("SPEECH.STR",riff);
    check(read_xa(folder,{0,1,2}).samples==expected.samples,"CDXA chunk traversal or odd-byte padding wrong");
    rejects([&]{read_xa(folder,{1,1,2});},"XA read escaped declared stream extent");
    check(read_xa(folder,{0,1,2},[]{return true;}).samples.empty(),"Cancelled XA decode still returned audio");
    write("SPEECH.STR",Bytes(2048));
    rejects([&]{read_xa(folder,{0,1,2});},"Data-only extraction accepted as XA");
    write("SPEECH.STR.raw",raw);
    check(read_xa(folder,{0,1,2}).samples==expected.samples,"Preserved raw extraction was not preferred");
}
}
int main() {
    try {
        Bytes spu(32);spu[0]=12;spu[1]=4;std::fill(spu.begin()+2,spu.begin()+16,0x87);spu[16]=12;spu[17]=3;
        const auto loop=decode_spu(spu);check(loop.frames()==56 && loop.loop_start==0,"SPU loop markers or frame count lost");
        check(loop.samples[0]==7 && loop.samples[1]==-8,"SPU signed nibbles or nibble order wrong");
        spu[16]=0x2c;const auto predicted=decode_spu(spu);
        check(predicted.samples[28]==-21 && predicted.samples[29]==-32,"SPU must truncate each signed predictor product separately");
        spu[16]=12;
        spu[17]=1;check(!decode_spu(spu).loop_start,"SPU end without repeat still loops");
        spu[17]=0;rejects([&]{decode_spu(spu);},"Unterminated SPU accepted");spu[0]=0x50;
        rejects([&]{decode_spu(spu);},"Invalid SPU predictor accepted");
        Bytes xa(2336);xa[0]=xa[4]=1;xa[1]=xa[5]=2;xa[2]=xa[6]=0x64;xa[3]=xa[7]=1;
        for(unsigned g=0;g<18;++g) {
            const auto p=8+g*128;std::fill(xa.begin()+p,xa.begin()+p+16,12);
            std::fill(xa.begin()+p+16,xa.begin()+p+128,0x87);
        }
        XaDecoder decoder;PcmAudio pcm;
        check(!decoder.append(xa,{0,1,3},pcm) && pcm.samples.empty(),"XA channel filter leaked another track");
        check(decoder.append(xa,{0,1,2},pcm) && pcm.frames()==2016 && pcm.channels==2 && pcm.rate==37800,"XA packet geometry wrong");
        bool stereo=true;for(unsigned i=0;i<pcm.frames();++i)stereo&=pcm.samples[i*2]==7 && pcm.samples[i*2+1]==-8;
        check(stereo,"XA channels or group order scrambled");
        extracted_readers(xa,pcm);
        // First-order predictor carries history across sectors independently.
        for(unsigned g=0;g<18;++g) {const auto p=8+g*128;std::fill(xa.begin()+p,xa.begin()+p+16,0x1c);std::fill(xa.begin()+p+16,xa.begin()+p+128,0);}
        decoder.append(xa,{0,1,2},pcm);check(pcm.samples[4032]==7 && pcm.samples[4033]==-7,"XA predictor reset at sector boundary");
        xa[3]=xa[7]=0;rejects([&]{decoder.append(xa,{0,1,2},pcm);},"Changing XA format was accepted");
        XaDecoder mono;PcmAudio mono_pcm;mono.append(xa,{0,1,2},mono_pcm);
        check(mono_pcm.channels==1 && mono_pcm.frames()==4032,"Mono XA packet length wrong");
        auto wide=xa;wide[3]=wide[7]=0x14;
        for(unsigned g=0;g<18;++g) {const auto p=8+g*128;std::fill(wide.begin()+p,wide.begin()+p+16,12);std::fill(wide.begin()+p+16,wide.begin()+p+128,0x80);}
        XaDecoder wide_decoder;PcmAudio wide_pcm;wide_decoder.append(wide,{0,1,2},wide_pcm);
        check(wide_pcm.rate==18900 && wide_pcm.frames()==2016 && wide_pcm.samples.front()==-8,"8-bit XA rate, sign or unit order wrong");
        xa[4]=0;rejects([&]{XaDecoder d;PcmAudio p;d.append(xa,{0,1,2},p);},"Corrupt XA subheader copies accepted");
        rejects([&]{XaDecoder d;PcmAudio p;d.append(Bytes(2048),{0,1,2},p);},"Truncated Form 2 payload accepted");
        std::cout<<checks<<" audio codec checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
