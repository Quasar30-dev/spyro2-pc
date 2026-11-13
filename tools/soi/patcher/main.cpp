// Portable ImGui front end for the independently verified PowerShell/xdelta backend.
// Season of Ice copy (Spyro 2_PC tools/soi/patcher): the house front end (Theia tools/uya_patcher/main.cpp) with the
// disc file type read from patch.json ("gui": output_ext, disc_word, file_filter, source_pattern), so a PS1 BIN/CUE
// package can use it; every default is the house value (PS2 .iso).
#include <cwctype>
// The backend owns input hashes, source locking, append-only ISO reconstruction,
// temporary-output validation and atomic promotion. No shell command interpolation.
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include "media/capture.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs=std::filesystem;
using Microsoft::WRL::ComPtr;
static HWND window_handle;
static bool close_requested=false;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<IDXGISwapChain> swapchain;
static ComPtr<ID3D11RenderTargetView> target;
static std::atomic<bool> busy=false;
static std::atomic<int> stage=0,result=-1;
static std::mutex log_mutex;
static std::string log_text;
static std::thread worker;
static char source[32768]{},output[32768]{};
static fs::path package_dir;
static fs::path requested_output;
static ImFont *heading=nullptr,*title=nullptr;
static auto started=std::chrono::steady_clock::now();
static std::string utf8(const std::wstring& w){
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);
    std::string s(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;
}
static std::wstring wide(const std::string& s){
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring w(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;
}
static std::wstring quote(const std::wstring& s){
    // Windows CommandLineToArgvW / CRT quoting, including trailing backslashes.
    std::wstring q=L"\"";size_t slash=0;
    for(wchar_t c:s){if(c==L'\\'){++slash;continue;}q.append(slash*(c==L'"'?2:1),L'\\');slash=0;if(c==L'"')q+=L'\\';q+=c;}
    q.append(slash*2,L'\\');q+=L'"';return q;
}
static void append(std::string s){std::lock_guard lock(log_mutex);log_text+=s;if(log_text.size()>262144)log_text.erase(0,log_text.size()-262144);}
// Display strings: patch.json's optional "gui" object (a package other than the hoverboots one, e.g. the debug menu,
// names itself there); without it the hoverboots wording below is kept unchanged. The game-specific keys (game,
// source_*, step2 / step3, output_name, building, done, script) let another game's package (R&C1 multiplayer) use the
// same front end; the patch file is patch.json's "patch"."file". Every default is the UYA hoverboots package's value.
struct Texts{std::string window="UYA Hoverboots Patcher",heading="HOVERBOOTS PATCHER",tag="USA / SCUS-97353    |    Hoverboots only",
    summary="Add hoverboots to your retail copy: fitted models, blue thrusters, animations, sound and the expanded Gadgets menu.",
    scope="Creates a separate expanded ISO. Outer Space is excluded.",button="Create hoverboots ISO",
    step5="5. Boot the new ISO in your normal PS2 setup. This package adds hoverboots and the expanded Gadgets menu; it excludes Outer Space. Windows PowerShell 5.1 and Direct3D 11 are required.",
    game="UP YOUR ARSENAL",source_prompt="Select an original UYA ISO to begin.",
    source_hint="USA English/French/Spanish; exact source hash checked before patching.",
    source_dialog="Select your original USA UYA ISO",output_name="UYA-Hoverboots-Only.iso",
    step2="2. Select your original USA SCUS-97353 UYA ISO (English/French/Spanish). The patcher checks the complete image, not just its filename.",
    step3="3. Choose a new .iso filename on a local drive with room for the expanded image. The patcher checks the exact space required. Existing files are never replaced. Spaces and punctuation are supported; accented/non-ASCII paths are not supported by this decoder.",
    building="Building expanded ISO",done="SUCCESS  /  Expanded ISO verified",
    script="Patch-UYA.ps1",patch_file="uya-hoverboots-only.xdelta",
    output_ext="iso",disc_word="ISO",file_filter="PlayStation 2 disc image (*.iso)",source_pattern="*.iso";};
static Texts texts;
static std::wstring source_dialog_title;
static std::string json_string(const std::string& j,const std::string& key){
    const auto k=j.find("\""+key+"\"");if(k==std::string::npos)return {};
    auto q=j.find('"',j.find(':',k)+1);if(q==std::string::npos)return {};
    std::string v;for(size_t i=q+1;i<j.size()&&j[i]!='"';++i){if(j[i]=='\\'&&i+1<j.size())++i;v+=j[i];}return v;
}
// A package file named by patch.json must be a plain file name inside the package folder.
static bool plain_name(const std::string& s){
    return !s.empty()&&s!="."&&s!=".."&&s.find_first_of("/\\:")==std::string::npos;
}
static void load_texts(){
    std::ifstream in(package_dir/L"patch.json",std::ios::binary);if(!in)return;
    const std::string j((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(const auto p=j.find("\"patch\"");p!=std::string::npos)
        if(auto v=json_string(j.substr(p),"file");!v.empty())texts.patch_file=plain_name(v)?v:std::string();
    const auto g=j.find("\"gui\"");if(g==std::string::npos)return;const auto body=j.substr(g);
    for(auto [key,dst]:{std::pair{"window",&texts.window},{"heading",&texts.heading},{"tag",&texts.tag},{"summary",&texts.summary},
                        {"scope",&texts.scope},{"button",&texts.button},{"step5",&texts.step5},{"game",&texts.game},
                        {"source_prompt",&texts.source_prompt},{"source_hint",&texts.source_hint},
                        {"source_dialog",&texts.source_dialog},{"output_name",&texts.output_name},{"step2",&texts.step2},
                        {"step3",&texts.step3},{"building",&texts.building},{"done",&texts.done},
                        {"output_ext",&texts.output_ext},{"disc_word",&texts.disc_word},{"file_filter",&texts.file_filter},
                        {"source_pattern",&texts.source_pattern}})
        if(auto v=json_string(body,key);!v.empty())*dst=v;
    if(auto v=json_string(body,"script");!v.empty())texts.script=plain_name(v)?v:std::string();
    if(!plain_name(texts.output_name))texts.output_name="patched."+texts.output_ext;
}
static void copy_path(char* dest,const fs::path& p){const auto s=utf8(p.wstring());strncpy_s(dest,32768,s.c_str(),_TRUNCATE);}
static void choose(bool save){
    wchar_t path[32768]{};const auto initial=wide(save?output:source);wcsncpy_s(path,initial.c_str(),_TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=window_handle;ofn.lpstrFile=path;ofn.nMaxFile=32768;
    static std::wstring filter,ext,save_title;
    const auto pattern=save?L"*."+wide(texts.output_ext):wide(texts.source_pattern);
    filter=wide(texts.file_filter);filter.push_back(L'\0');filter+=pattern;filter.push_back(L'\0');
    filter+=L"All files";filter.push_back(L'\0');filter+=L"*.*";filter.push_back(L'\0');filter.push_back(L'\0');
    ext=wide(texts.output_ext);save_title=L"Choose a new patched "+wide(texts.disc_word)+L" filename";
    ofn.lpstrFilter=filter.c_str();ofn.lpstrDefExt=ext.c_str();
    source_dialog_title=wide(texts.source_dialog);ofn.lpstrTitle=save?save_title.c_str():source_dialog_title.c_str();
    ofn.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn)){copy_path(save?output:source,path);if(!save&&!output[0])copy_path(output,fs::path(path).parent_path()/wide(texts.output_name));}
}
static std::string preflight(){
    std::error_code ec;
    if(!source[0])return texts.source_prompt;
    if(!fs::is_regular_file(fs::path(wide(source)),ec))return "The selected source file could not be found.";
    if(!output[0])return "Choose a new output "+texts.disc_word+" filename.";
    for(const char* path:{source,output})for(const unsigned char* c=reinterpret_cast<const unsigned char*>(path);*c;++c)
        if(*c>=128)return "The bundled decoder needs plain ASCII paths. Move the "+texts.disc_word+" or choose a folder without accented characters.";
    if(fs::exists(fs::path(wide(output)),ec))return "That output already exists. Choose a new filename.";
    {auto e=fs::path(wide(output)).extension().wstring();for(auto& c:e)c=static_cast<wchar_t>(towlower(c));
     if(e!=L"."+wide(texts.output_ext))return "The output filename must end in ."+texts.output_ext+".";}
    if(!fs::is_directory(fs::path(wide(output)).parent_path(),ec))return "Choose an existing output folder.";
    if(texts.script.empty()||texts.patch_file.empty())return "patch.json names an invalid package file. Extract a fresh copy of the complete ZIP.";
    for(const auto& p:{package_dir/wide(texts.script),package_dir/L"patch.json",package_dir/wide(texts.patch_file),package_dir/L"tools/xdelta3.exe"})
        if(!fs::is_regular_file(p,ec))return "Package files are missing. Extract the complete ZIP before opening the patcher.";
    return {};
}
static void begin_patch(){
    if(busy)return;
    if(worker.joinable())worker.join();
    const auto why=preflight();
    {std::lock_guard lock(log_mutex);log_text.clear();}
    result=-1;stage=0;
    if(!why.empty()){append("ERROR: "+why+"\n");result=2;return;}
    const std::wstring src=wide(source),dst=wide(output);requested_output=dst;busy=true;started=std::chrono::steady_clock::now();
    worker=std::thread([src,dst]{
        HANDLE read=nullptr,write=nullptr;SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        if(!CreatePipe(&read,&write,&sa,0)){append("ERROR: Could not open the patching log pipe.\n");result=3;busy=false;return;}
        SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
        wchar_t sys[MAX_PATH]{};GetSystemDirectoryW(sys,MAX_PATH);
        fs::path powershell=fs::path(sys)/L"WindowsPowerShell/v1.0/powershell.exe";
        std::wstring command=quote(powershell.wstring())+L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+quote((package_dir/wide(texts.script)).wstring())+L" -Source "+quote(src)+L" -Output "+quote(dst)+L" -NonInteractive";
        STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdOutput=write;si.hStdError=write;
        HANDLE null_input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);si.hStdInput=null_input;
        PROCESS_INFORMATION pi{};
        BOOL ok=CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,package_dir.c_str(),&si,&pi);
        CloseHandle(write);if(null_input!=INVALID_HANDLE_VALUE)CloseHandle(null_input);
        if(!ok){append("ERROR: Could not start Windows PowerShell. Windows error "+std::to_string(GetLastError())+".\n");CloseHandle(read);result=3;busy=false;return;}
        CloseHandle(pi.hThread);
        char buffer[4096];DWORD n=0;std::string scan;
        while(ReadFile(read,buffer,sizeof(buffer),&n,nullptr)&&n){
            std::string chunk(buffer,n);append(chunk);scan+=chunk;
            for(int i=1;i<=4;++i)if(scan.find("["+std::to_string(i)+"/4]")!=std::string::npos)stage=i;
            if(scan.size()>8192)scan.erase(0,scan.size()-2048);
        }
        CloseHandle(read);WaitForSingleObject(pi.hProcess,INFINITE);DWORD exit=1;GetExitCodeProcess(pi.hProcess,&exit);CloseHandle(pi.hProcess);
        result=int(exit);if(exit==0)stage=5;busy=false;
    });
}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static void resize(){
    target.Reset();ComPtr<ID3D11Texture2D> back;
    if(SUCCEEDED(swapchain->GetBuffer(0,IID_PPV_ARGS(&back))))device->CreateRenderTargetView(back.Get(),nullptr,&target);
}
static LRESULT WINAPI proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(ImGui_ImplWin32_WndProcHandler(h,m,w,l))return 1;
    if(m==WM_CLOSE){close_requested=true;return 0;}
    if(m==WM_GETMINMAXINFO){auto* mm=reinterpret_cast<MINMAXINFO*>(l);mm->ptMinTrackSize={860,700};return 0;}
    if(m==WM_DROPFILES){if(!busy){wchar_t p[32768]{};DragQueryFileW(HDROP(w),0,p,32768);copy_path(source,p);if(!output[0])copy_path(output,fs::path(p).parent_path()/wide(texts.output_name));}DragFinish(HDROP(w));return 0;}
    if(m==WM_SIZE&&swapchain&&w!=SIZE_MINIMIZED){
        // The context retains bound views even after our ComPtr is reset.
        // Release that binding before DXGI replaces the back buffer.
        context->OMSetRenderTargets(0,nullptr,nullptr);target.Reset();
        swapchain->ResizeBuffers(0,LOWORD(l),HIWORD(l),DXGI_FORMAT_UNKNOWN,0);resize();return 0;
    }
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
static void ui(){
    const auto size=ImGui::GetIO().DisplaySize;ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(size);
    ImGui::Begin("UYA Hoverboots",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(title);ImGui::TextColored({.20f,.85f,1.f,1},"%s",texts.game.c_str());ImGui::PopFont();
    ImGui::PushFont(heading);ImGui::TextUnformatted(texts.heading.c_str());ImGui::PopFont();
    ImGui::TextDisabled("%s",texts.tag.c_str());
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    ImGui::TextWrapped("%s",texts.summary.c_str());
    ImGui::TextDisabled("%s",texts.scope.c_str());
    ImGui::Spacing();ImGui::BeginDisabled(busy);
    ImGui::TextUnformatted(("1   Original retail "+texts.disc_word).c_str());ImGui::SetNextItemWidth(-130);ImGui::InputText("##source",source,sizeof(source));ImGui::SameLine();if(ImGui::Button("Browse...##in",{114,0}))choose(false);
    ImGui::TextDisabled("%s",texts.source_hint.c_str());
    ImGui::Spacing();ImGui::TextUnformatted(("2   New patched "+texts.disc_word).c_str());ImGui::SetNextItemWidth(-130);ImGui::InputText("##output",output,sizeof(output));ImGui::SameLine();if(ImGui::Button("Save as...",{114,0}))choose(true);
    ImGui::TextDisabled("Use a local NTFS/exFAT/ReFS drive. Space is checked before patching. ASCII paths only.");
    ImGui::Spacing();const auto why=preflight();ImGui::BeginDisabled(!why.empty());
    if(ImGui::Button(result==0?("Create another patched "+texts.disc_word).c_str():texts.button.c_str(),{270,42}))begin_patch();
    ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Instructions",{125,42}))ImGui::OpenPopup("How to patch");
    ImGui::EndDisabled();
    ImGui::SetNextWindowSize({670,0},ImGuiCond_Appearing);
    if(ImGui::BeginPopupModal("How to patch",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("1. Extract the entire download into one folder. Keep the patch, script and tools next to this program.");
        ImGui::Spacing();ImGui::TextWrapped("%s",texts.step2.c_str());
        ImGui::Spacing();ImGui::TextWrapped("%s",texts.step3.c_str());
        ImGui::Spacing();ImGui::TextWrapped("4. Click %s and let all four steps finish. Only a verified output is moved to your selected filename. Your retail %s stays unchanged.",texts.button.c_str(),texts.disc_word.c_str());
        ImGui::Spacing();ImGui::TextWrapped("%s",texts.step5.c_str());
        ImGui::Spacing();if(ImGui::Button("Close",{120,0}))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    if(!busy&&!why.empty()&&result!=0)ImGui::TextWrapped("%s",why.c_str());
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    const std::string checking="Checking retail "+texts.disc_word;
    const char* phases[]={"Ready to patch",checking.c_str(),"Checking patch files",texts.building.c_str(),"Verifying complete output","Patch complete"};
    const int st=stage.load(),rc=result.load();
    if(busy){ImGui::TextColored({.20f,.85f,1,1},"%s",phases[st]);ImGui::SameLine();ImGui::TextDisabled("  %.0f seconds elapsed",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());}
    else if(rc==0){ImGui::TextColored({.40f,1,.62f,1},"%s",texts.done.c_str());ImGui::TextWrapped("%s",utf8(requested_output.wstring()).c_str());if(ImGui::Button("Open output folder"))ShellExecuteW(window_handle,L"open",requested_output.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
    else if(rc>0)ImGui::TextColored({1,.46f,.34f,1},"PATCH FAILED  /  Read the explanation below");
    else ImGui::TextDisabled("Source validation  >  Package checks  >  Apply patch  >  Verify output");
    if(busy){char label[64];snprintf(label,sizeof(label),"Step %d of 4",std::clamp(st,1,4));ImGui::ProgressBar(st>0?(st-1)/4.f:0.f,{-1,8},"");ImGui::TextDisabled("%s. Each step takes a different amount of time.",label);}
    static int last_result=-1;static bool was_busy=false;
    if((busy&&!was_busy)||(rc>0&&rc!=last_result))ImGui::SetNextItemOpen(true);
    last_result=rc;was_busy=busy;
    if(ImGui::CollapsingHeader("Patching log")){
        std::string text;{std::lock_guard lock(log_mutex);text=log_text;}
        if(ImGui::SmallButton("Copy log"))ImGui::SetClipboardText(text.c_str());
        ImGui::BeginChild("log",{0,std::max(40.f,size.y-ImGui::GetCursorPosY()-24)},ImGuiChildFlags_Borders);
        const bool follow=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-2;
        ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(text.empty()?"No patch has been started.":text.c_str());ImGui::PopTextWrapPos();
        if(follow)ImGui::SetScrollHereY(1);ImGui::EndChild();
    }
    if(close_requested&&busy){ImGui::OpenPopup("Patching is still running");close_requested=false;}
    if(ImGui::BeginPopupModal("Patching is still running",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("Please let validation and patching finish before closing.\nThe source is held read-only while the output is being built.");
        if(ImGui::Button("Keep patching",{170,0}))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    ImGui::End();
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);package_dir=fs::path(exe).parent_path();
    bool hidden=false,automatic=false,test_resize=false;fs::path screenshot;int frames=0;
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    for(int i=1;i<argc;++i){std::wstring a=argv[i];if(a==L"--hidden")hidden=true;else if(a==L"--auto")automatic=true;
        else if(a==L"--source"&&i+1<argc)copy_path(source,argv[++i]);else if(a==L"--output"&&i+1<argc)copy_path(output,argv[++i]);
        else if(a==L"--package"&&i+1<argc)package_dir=fs::absolute(argv[++i]);else if(a==L"--screenshot"&&i+1<argc)screenshot=argv[++i];else if(a==L"--test-resize")test_resize=true;}
    LocalFree(argv);
    load_texts();
    WNDCLASSEXW wc{sizeof(wc),CS_CLASSDC,proc,0,0,instance,nullptr,LoadCursorW(nullptr,IDC_ARROW),nullptr,nullptr,L"TheiaUYAPatcher",nullptr};RegisterClassExW(&wc);
    RECT rect{0,0,980,720};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    window_handle=CreateWindowW(wc.lpszClassName,wide(texts.window).c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=2;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=window_handle;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL level;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swapchain,&device,&level,&context);
    if(FAILED(hr))hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swapchain,&device,&level,&context);
    if(FAILED(hr)){if(!hidden)MessageBoxW(nullptr,L"Direct3D 11 could not be initialized.",wide(texts.window).c_str(),MB_ICONERROR);return 3;}resize();
    IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    wchar_t windows[32768]{};GetWindowsDirectoryW(windows,32768);
    auto font=[&](const wchar_t* name,float points)->ImFont*{const auto path=fs::path(windows)/L"Fonts"/name;std::error_code ec;return fs::is_regular_file(path,ec)?io.Fonts->AddFontFromFileTTF(utf8(path.wstring()).c_str(),points):nullptr;};
    if(!font(L"segoeui.ttf",18))io.Fonts->AddFontDefault();heading=font(L"seguisb.ttf",26);title=font(L"bahnschrift.ttf",36);
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowPadding={26,22};style.FramePadding={12,8};style.ItemSpacing={10,8};style.FrameRounding=5;style.ChildRounding=5;
    style.Colors[ImGuiCol_WindowBg]={.035f,.055f,.085f,1};style.Colors[ImGuiCol_FrameBg]={.085f,.125f,.18f,1};style.Colors[ImGuiCol_Button]={.08f,.32f,.43f,1};style.Colors[ImGuiCol_ButtonHovered]={.1f,.48f,.63f,1};style.Colors[ImGuiCol_Header]={.07f,.14f,.22f,1};
    ImGui_ImplWin32_Init(window_handle);ImGui_ImplDX11_Init(device.Get(),context.Get());DragAcceptFiles(window_handle,TRUE);
    if(!hidden){ShowWindow(window_handle,SW_SHOWDEFAULT);UpdateWindow(window_handle);}if(automatic)begin_patch();
    bool done=false;int exit_code=0;
    while(!done){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);if(msg.message==WM_QUIT)done=true;}if(done)break;
        if(hidden&&test_resize&&frames==2){RECT resize_bounds{0,0,860,700};AdjustWindowRect(&resize_bounds,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(window_handle,nullptr,0,0,resize_bounds.right-resize_bounds.left,resize_bounds.bottom-resize_bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);}
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();ui();ImGui::Render();
        const float clear[]={.03f,.05f,.08f,1};context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);context->ClearRenderTargetView(target.Get(),clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());++frames;
        bool finish=hidden&&((!automatic&&frames>=4)||(automatic&&!busy&&result>=0&&frames>=4));
        if(finish&&!screenshot.empty()){
            ComPtr<ID3D11Texture2D> back;swapchain->GetBuffer(0,IID_PPV_ARGS(&back));std::vector<uint8_t> pixels;unsigned w=0,h=0;std::string error;
            if(!media::read_texture_rgba(device.Get(),context.Get(),back.Get(),pixels,w,h,error)||!media::save_image(screenshot,pixels.data(),w,h,w*4,"png",92,error))exit_code=4;
        }
        swapchain->Present(hidden?0:1,0);if(hidden)Sleep(16);
        if(finish){if(automatic&&result!=0)exit_code=result;done=true;}if(close_requested&&!busy)done=true;
    }
    if(worker.joinable())worker.join();ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();DestroyWindow(window_handle);UnregisterClassW(wc.lpszClassName,instance);CoUninitialize();return exit_code;
}
