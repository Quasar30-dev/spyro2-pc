#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include "spyro/multiplayer.hpp"
#include <mutex>
#include <thread>
#include <chrono>
#include <set>

namespace spyro {
namespace {
struct Handle {HINTERNET h{};~Handle(){if(h)WinHttpCloseHandle(h);}operator HINTERNET()const{return h;}};
void require(bool yes,const char* message){if(!yes)throw FormatError(message);}
bool token(const std::string& text){return text.size()==32 && text.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos;}
}
struct OnlineClient::Impl {
    mutable std::mutex mutex;
    OnlineState local;
    bool ready=false;
    OnlineStatus status;
    std::jthread worker;
};
OnlineClient::OnlineClient():impl_(std::make_unique<Impl>()){}
OnlineClient::~OnlineClient(){disconnect();}
std::string OnlineClient::room_code() {
    std::array<unsigned char,16> bytes{};
    require(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"Could not generate a room code");
    constexpr char hex[]="0123456789abcdef";std::string result;
    for(const auto b:bytes){result+=hex[b>>4];result+=hex[b&15];}return result;
}
void OnlineClient::disconnect() {
    if(impl_->worker.joinable()){impl_->worker.request_stop();impl_->worker.join();}
    std::lock_guard lock(impl_->mutex);impl_->status.connected=false;impl_->status.players.clear();impl_->status.message="Offline";
}
void OnlineClient::publish(const OnlineState& state){if(!state.valid())return;std::lock_guard lock(impl_->mutex);impl_->local=state;impl_->ready=true;}
OnlineStatus OnlineClient::status()const{std::lock_guard lock(impl_->mutex);return impl_->status;}
void OnlineClient::connect(const std::string& url,const std::string& room,bool host) {
    require(token(room),"Room code must contain 32 hexadecimal characters");
    require(url.size()<2048 && url.find_first_of("\r\n") == std::string::npos,"Invalid relay URL");
    disconnect();const auto client=room_code();
    {std::lock_guard lock(impl_->mutex);impl_->status={};impl_->status.hosting=host;impl_->status.room=room;impl_->status.message="Connecting to relay...";}
    impl_->worker=std::jthread([this,url,room,host,client](std::stop_token stop) {
        try {
            const std::wstring wide(url.begin(),url.end());URL_COMPONENTS parsed{sizeof(parsed)};
            parsed.dwHostNameLength=parsed.dwUrlPathLength=parsed.dwExtraInfoLength=parsed.dwUserNameLength=parsed.dwPasswordLength=DWORD(-1);
            require(WinHttpCrackUrl(wide.c_str(),static_cast<DWORD>(wide.size()),0,&parsed)!=0,"Use an HTTPS relay URL");
            const std::wstring hostname(parsed.lpszHostName,parsed.dwHostNameLength);
            require(parsed.dwUserNameLength==0 && parsed.dwPasswordLength==0 && parsed.dwExtraInfoLength==0,"Relay URL must not contain credentials, query or fragment");
            const bool secure=parsed.nScheme==INTERNET_SCHEME_HTTPS;
            require(secure || (parsed.nScheme==INTERNET_SCHEME_HTTP && (hostname==L"127.0.0.1" || hostname==L"localhost")),"Internet relay connections require HTTPS; HTTP is only allowed on localhost");
            auto path=std::wstring(parsed.lpszUrlPath?parsed.lpszUrlPath:L"",parsed.dwUrlPathLength);if(path.empty() || path==L"/")path=L"/v1/poll";
            Handle session{WinHttpOpen(L"Spyro2PC-Online/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
            require(session.h!=nullptr,"Could not start internet client");WinHttpSetTimeouts(session,2000,2000,2000,2000);
            Handle connection{WinHttpConnect(session,hostname.c_str(),parsed.nPort,0)};require(connection.h!=nullptr,"Could not contact relay");
            std::uint64_t sequence=0;
            while(!stop.stop_requested()) {
                const auto start=std::chrono::steady_clock::now();OnlineState local;bool ready;
                {std::lock_guard lock(impl_->mutex);local=impl_->local;ready=impl_->ready;}
                if(!ready){std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}
                local.sequence=++sequence;
                const auto body="S2NET 2 "+std::to_string(host?1:0)+" "+room+" "+client+"\n"+encode_online_state(local);
                Handle request{WinHttpOpenRequest(connection,L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,secure?WINHTTP_FLAG_SECURE:0)};
                require(request.h!=nullptr,"Could not open relay request");
                DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
                require(WinHttpSendRequest(request,L"Content-Type: text/plain; charset=us-ascii\r\n",DWORD(-1),const_cast<char*>(body.data()),static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0)!=0 && WinHttpReceiveResponse(request,nullptr)!=0,"Relay connection lost or timed out");
                DWORD status=0,size=sizeof(status);require(WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr)!=0,"Invalid relay response");
                if(status!=200)throw FormatError(status==404?"Room not found or host disconnected":status==409?"Room full or code already in use":status==429?"Relay is busy; reconnect shortly":"Relay rejected the session");
                std::string response;std::array<char,2048> buffer{};
                for(;;){DWORD n=0;require(WinHttpReadData(request,buffer.data(),static_cast<DWORD>(buffer.size()),&n)!=0,"Could not read relay response");if(!n)break;response.append(buffer.data(),n);require(response.size()<=8192,"Relay response is too large");}
                std::istringstream in(response);in.imbue(std::locale::classic());std::string magic;unsigned version{},self{},owner{},count{};
                in>>magic>>version>>self>>owner>>count;require(bool(in) && magic=="S2NET" && version==2 && self<4 && owner<4 && count>0 && count<=4,"Incompatible relay protocol");
                std::vector<OnlineState> players;std::set<unsigned> peers;
                for(unsigned i=0;i<count;++i){auto player=decode_online_state(in);require(peers.insert(player.peer).second,"Duplicate relay player");players.push_back(player);}
                in>>std::ws;require(in.eof() && peers.contains(self) && peers.contains(owner),"Incomplete relay roster");
                {std::lock_guard lock(impl_->mutex);auto& view=impl_->status;view.connected=true;view.self=self;view.host=owner;view.players=std::move(players);++view.exchanges;view.message=host?"Hosting online exploration":"Connected to online exploration";}
                while(!stop.stop_requested() && std::chrono::steady_clock::now()-start<std::chrono::milliseconds(50))std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }catch(const std::exception& error){std::lock_guard lock(impl_->mutex);impl_->status.connected=false;impl_->status.players.clear();impl_->status.message=error.what();}
    });
}
} // namespace spyro
