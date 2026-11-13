#include "spyro/video_capture.hpp"
#include <windows.h>
#include <wrl/client.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>

namespace spyro {
namespace {
using Microsoft::WRL::ComPtr;
void require(bool ok,const char* message){if(!ok)throw FormatError(message);}
std::wstring quote(const std::wstring& text) {
    std::wstring result=L"\"";unsigned slashes=0;
    for(auto c:text){if(c==L'\\'){++slashes;continue;}result.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;}
    result.append(slashes*2,L'\\');return result+L'"';
}
std::filesystem::path encoder() {
    wchar_t path[32768]{};const auto n=SearchPathW(nullptr,L"ffmpeg.exe",nullptr,32768,path,nullptr);
    require(n && n<32768,"Video recording needs FFmpeg. Install FFmpeg and make ffmpeg.exe available on PATH.");return path;
}
HANDLE process(const std::filesystem::path& executable,const std::vector<std::wstring>& args,HANDLE input,HANDLE log) {
    auto command=quote(executable.wstring());for(const auto& arg:args)command+=L" "+quote(arg);
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=input;startup.hStdOutput=log;startup.hStdError=log;
    PROCESS_INFORMATION info{};
    require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Could not start the video encoder");
    CloseHandle(info.hThread);return info.hProcess;
}
Bytes read_frame(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,CaptureArea area,unsigned width,unsigned height) {
    require(source && width && height,"There is no game image to capture");
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    require(area.width && area.height && area.x<desc.Width && area.y<desc.Height,"Capture area is outside the game image");
    area.width=std::min(area.width,desc.Width-area.x);area.height=std::min(area.height,desc.Height-area.y);
    desc.Width=area.width;desc.Height=area.height;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"Cannot allocate capture readback");
    D3D11_BOX box{area.x,area.y,0,area.x+area.width,area.y+area.height,1};context->CopySubresourceRegion(staging.Get(),0,0,0,0,source,0,&box);
    D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Cannot read the rendered frame");
    Bytes pixels(std::size_t(width)*height*4);
    // Preserve the recording dimensions if the workbench viewport is resized.
    for(unsigned y=0;y<height;++y) {
        const auto* row=static_cast<const std::uint8_t*>(mapped.pData)+(std::uint64_t(y)*area.height/height)*mapped.RowPitch;
        auto* out=pixels.data()+std::size_t(y)*width*4;
        if(width==area.width)std::copy_n(row,width*4,out);
        else for(unsigned x=0;x<width;++x)std::copy_n(row+(std::uint64_t(x)*area.width/width)*4,4,out+x*4);
    }
    context->Unmap(staging.Get(),0);return pixels;
}
void wave_header(std::ofstream& out,unsigned bytes) {
    const auto u16=[&](unsigned n){out.put(static_cast<char>(n));out.put(static_cast<char>(n>>8));};
    const auto u32=[&](unsigned n){for(unsigned i=0;i<4;++i)out.put(static_cast<char>(n>>(i*8)));};
    out.seekp(0);out.write("RIFF",4);u32(36+bytes);out.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(48000);u32(48000*4);u16(4);u16(16);out.write("data",4);u32(bytes);
}
}
struct VideoCapture::Impl {
    std::filesystem::path output,temporary,video,wave,partial,log_path,exe;
    HANDLE pipe{},child{},log{};
    std::ofstream audio;unsigned width{},height{},count{};
    std::jthread writer;std::mutex mutex;std::condition_variable ready;
    std::deque<Bytes> queue;bool stopping=false,failed=false,running=false;
    void close() {
        {std::lock_guard lock(mutex);stopping=true;}ready.notify_all();
        if(writer.joinable())writer.join();
        if(pipe){CloseHandle(pipe);pipe=nullptr;}
        if(child){if(WaitForSingleObject(child,30000)!=WAIT_OBJECT_0)TerminateProcess(child,1);CloseHandle(child);child=nullptr;}
        if(log){CloseHandle(log);log=nullptr;}if(audio.is_open())audio.close();running=false;
    }
    ~Impl(){close();}
};
VideoCapture::VideoCapture():impl_(std::make_unique<Impl>()){}
VideoCapture::~VideoCapture()=default;
bool VideoCapture::active()const{return impl_->running;}
unsigned VideoCapture::frames()const{return impl_->count;}
void VideoCapture::start(const std::filesystem::path& path,unsigned width,unsigned height) {
    require(!active(),"A recording is already running");require(width>=16 && height>=16 && width<=8192 && height<=8192,"Invalid recording size");
    require(path.extension()==L".mp4","Choose an .mp4 file for the recording");
    impl_=std::make_unique<Impl>();auto& p=*impl_;p.exe=encoder();p.output=std::filesystem::absolute(path);
    std::filesystem::create_directories(p.output.parent_path());
    const auto token=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
    p.temporary=p.output.parent_path()/(L".spyro-recording-"+token);std::filesystem::create_directory(p.temporary);
    p.video=p.temporary/L"picture.mp4";p.wave=p.temporary/L"sound.wav";p.partial=p.temporary/L"complete.mp4";p.log_path=p.temporary/L"encoder.log";
    p.width=width;p.height=height;
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE read{};
    p.log=CreateFileW(p.log_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(p.log!=INVALID_HANDLE_VALUE,"Cannot create video encoder log");
    require(CreatePipe(&read,&p.pipe,&security,0)!=0,"Cannot open video encoder input");SetHandleInformation(p.pipe,HANDLE_FLAG_INHERIT,0);
    const auto dimensions=std::to_wstring(width)+L"x"+std::to_wstring(height);
    try{p.child=process(p.exe,{L"-hide_banner",L"-loglevel",L"error",L"-nostdin",L"-y",L"-f",L"rawvideo",L"-pixel_format",L"rgba",
        L"-video_size",dimensions,L"-framerate",L"60",L"-i",L"pipe:0",L"-an",L"-vf",L"pad=ceil(iw/2)*2:ceil(ih/2)*2",
        L"-c:v",L"libx264",L"-preset",L"veryfast",L"-crf",L"18",L"-threads",L"4",L"-pix_fmt",L"yuv420p",p.video.wstring()},read,p.log);}
    catch(...){CloseHandle(read);throw;}CloseHandle(read);
    p.audio.open(p.wave,std::ios::binary);require(bool(p.audio),"Cannot create recording audio");wave_header(p.audio,0);p.running=true;
    p.writer=std::jthread([&p]{
        for(;;) {
            Bytes frame;
            {std::unique_lock lock(p.mutex);p.ready.wait(lock,[&]{return p.stopping || !p.queue.empty();});
                if(p.queue.empty())break;frame=std::move(p.queue.front());p.queue.pop_front();}p.ready.notify_all();
            std::size_t offset=0;
            while(offset<frame.size()) {DWORD written{};if(!WriteFile(p.pipe,frame.data()+offset,static_cast<DWORD>(frame.size()-offset),&written,nullptr) || !written) {
                    {std::lock_guard lock(p.mutex);p.failed=true;p.queue.clear();}p.ready.notify_all();return;}
                offset+=written;
            }
        }
    });
}
void VideoCapture::frame(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,CaptureArea area,std::span<const std::int16_t,1600> audio) {
    auto& p=*impl_;require(p.running,"No recording is running");require(p.count<1200000,"Recording reached its WAV size limit");
    auto pixels=read_frame(device,context,source,area,p.width,p.height);
    {std::unique_lock lock(p.mutex);p.ready.wait(lock,[&]{return p.failed || p.queue.size()<24;});
        require(!p.failed,"Video encoder stopped; see the recording's encoder.log");p.queue.push_back(std::move(pixels));}p.ready.notify_all();
    p.audio.write(reinterpret_cast<const char*>(audio.data()),audio.size_bytes());require(bool(p.audio),"Could not write recording audio");++p.count;
}
void VideoCapture::finish() {
    auto& p=*impl_;if(!p.running)return;
    struct Stopped {Impl& p;~Stopped(){p.running=false;}} stopped{p};
    {std::lock_guard lock(p.mutex);p.stopping=true;}p.ready.notify_all();if(p.writer.joinable())p.writer.join();
    if(p.pipe){CloseHandle(p.pipe);p.pipe=nullptr;}
    require(WaitForSingleObject(p.child,60000)==WAIT_OBJECT_0,"Video encoder did not finish");DWORD code{};GetExitCodeProcess(p.child,&code);CloseHandle(p.child);p.child=nullptr;
    require(!p.failed && code==0 && p.count>0,"Video encoding failed; intermediate recording and encoder.log were preserved");
    wave_header(p.audio,p.count*3200);p.audio.close();
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    try{p.child=process(p.exe,{L"-hide_banner",L"-loglevel",L"error",L"-nostdin",L"-y",L"-i",p.video.wstring(),L"-i",p.wave.wstring(),
        L"-map",L"0:v:0",L"-map",L"1:a:0",L"-c:v",L"copy",L"-c:a",L"aac",L"-b:a",L"192k",L"-shortest",L"-movflags",L"+faststart",p.partial.wstring()},input,p.log);}
    catch(...){CloseHandle(input);throw;}CloseHandle(input);
    require(WaitForSingleObject(p.child,60000)==WAIT_OBJECT_0,"Recording audio mux did not finish");GetExitCodeProcess(p.child,&code);CloseHandle(p.child);p.child=nullptr;
    require(code==0,"Could not combine recording picture and sound; intermediate files were preserved");
    require(MoveFileExW(p.partial.c_str(),p.output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Could not install the finished MP4");
    CloseHandle(p.log);p.log=nullptr;p.running=false;
    std::error_code ignored;std::filesystem::remove(p.video,ignored);std::filesystem::remove(p.wave,ignored);std::filesystem::remove(p.log_path,ignored);std::filesystem::remove(p.temporary,ignored);
}
void VideoCapture::photo(const std::filesystem::path& path,ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,CaptureArea area) {
    const auto pixels=read_frame(device,context,source,area,area.width,area.height);
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);
    std::array<std::uint8_t,54> header{};header[0]='B';header[1]='M';
    const auto put=[&](unsigned at,unsigned n){for(unsigned i=0;i<4;++i)header[at+i]=static_cast<std::uint8_t>(n>>(i*8));};
    put(2,54+static_cast<unsigned>(pixels.size()));put(10,54);put(14,40);put(18,area.width);put(22,area.height);header[26]=1;header[28]=32;
    out.write(reinterpret_cast<const char*>(header.data()),header.size());
    for(unsigned y=0;y<area.height;++y)for(unsigned x=0;x<area.width;++x) {
        const auto p=(std::size_t(area.height-1-y)*area.width+x)*4;
        const char bgra[]{static_cast<char>(pixels[p+2]),static_cast<char>(pixels[p+1]),static_cast<char>(pixels[p]),char(-1)};out.write(bgra,4);
    }
    require(bool(out),"Could not save the photo");
}
} // namespace spyro
