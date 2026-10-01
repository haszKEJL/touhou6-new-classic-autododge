#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <mutex>
#include <fstream>
#include "controls.hpp"
#include "version.hpp"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "MinHook.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
int assistMain(int,char**);
namespace {
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Resize=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
Present originalPresent=nullptr; Resize originalResize=nullptr;
std::recursive_mutex renderMutex;
ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
ID3D11RenderTargetView* target=nullptr; IDXGISwapChain* activeChain=nullptr;
HWND window=nullptr; WNDPROC originalWndProc=nullptr;
void log(const char* message) { std::ofstream out(controls::state.config.parent_path()/L"overlay.log",std::ios::app); out<<message<<'\n'; }
LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    std::lock_guard lock(renderMutex);
    if(ImGui::GetCurrentContext() && controls::state.menu) {
        ImGui_ImplWin32_WndProcHandler(hwnd,msg,w,l);
        if((msg>=WM_MOUSEFIRST && msg<=WM_MOUSELAST) || (msg>=WM_KEYFIRST && msg<=WM_KEYLAST) || msg==WM_CHAR) return 0;
        if(msg==WM_SETCURSOR && LOWORD(l)==HTCLIENT) { SetCursor(LoadCursor(nullptr,IDC_ARROW)); return TRUE; }
    }
    return CallWindowProcW(originalWndProc,hwnd,msg,w,l);
}
bool makeTarget(IDXGISwapChain* chain) {
    ID3D11Texture2D* buffer=nullptr;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer)))) return false;
    HRESULT result=device->CreateRenderTargetView(buffer,nullptr,&target); buffer->Release(); return SUCCEEDED(result);
}
void theme() {
    auto& s=ImGui::GetStyle(); ImGui::StyleColorsDark();
    s.WindowRounding=16; s.ChildRounding=12; s.FrameRounding=7; s.PopupRounding=8;
    s.WindowPadding={24,22}; s.FramePadding={12,8}; s.ItemSpacing={12,10};
    s.WindowBorderSize=1; s.ChildBorderSize=0; s.ScrollbarSize=10;
    auto* c=s.Colors;
    c[ImGuiCol_WindowBg]={.045f,.047f,.075f,1}; c[ImGuiCol_ChildBg]={.078f,.081f,.12f,1};
    c[ImGuiCol_Border]={.18f,.17f,.25f,1}; c[ImGuiCol_Text]={.93f,.94f,.98f,1}; c[ImGuiCol_TextDisabled]={.52f,.55f,.65f,1};
    c[ImGuiCol_FrameBg]={.13f,.13f,.20f,1}; c[ImGuiCol_FrameBgHovered]={.21f,.18f,.31f,1}; c[ImGuiCol_FrameBgActive]={.28f,.22f,.40f,1};
    c[ImGuiCol_Button]={.19f,.16f,.29f,1}; c[ImGuiCol_ButtonHovered]={.34f,.25f,.48f,1}; c[ImGuiCol_ButtonActive]={.44f,.30f,.62f,1};
    c[ImGuiCol_CheckMark]={.51f,.87f,.77f,1}; c[ImGuiCol_Header]={.29f,.21f,.43f,1}; c[ImGuiCol_HeaderHovered]={.36f,.26f,.53f,1};
}
std::string keyName(int key) {
    if(!key) return i18n::get(controls::state.language).unbound;
    wchar_t label[80]{}; LONG scan=LONG(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16);
    if(key==VK_LEFT || key==VK_RIGHT || key==VK_UP || key==VK_DOWN || key==VK_RCONTROL || key==VK_RMENU) scan|=1<<24;
    if(!GetKeyNameTextW(scan,label,80)) return "VK "+std::to_string(key);
    char utf8[240]{}; WideCharToMultiByte(CP_UTF8,0,label,-1,utf8,240,nullptr,nullptr); return utf8;
}
void drawPrediction() {
    prediction::Path path;
    i18n::Language language;
    {
        std::lock_guard lock(controls::state.mutex); const auto& state=controls::state;
        if(!state.showPrediction || !state.ready || !state.playing || state.menu ||
           state.prediction.count<2 || GetTickCount64()-state.predictionTime>150) return;
        path=state.prediction; language=state.language;
    }
    auto* viewport=ImGui::GetMainViewport();
    auto map=prediction::mapping(viewport->Size.x,viewport->Size.y);
    auto screen=[&](Vec p) { auto q=map.point(p); return ImVec2{viewport->Pos.x+q.x,viewport->Pos.y+q.y}; };
    auto* draw=ImGui::GetBackgroundDrawList();
    draw->PushClipRect(screen({0,0}),screen({384,448}),true);
    const ImU32 cyan=IM_COL32(92,235,213,220), red=IM_COL32(255,113,118,235);
    const float thickness=std::clamp(map.scale,1.f,2.5f);
    bool blocked=path.searched && path.safeFrames==0;
    for(int i=1;i<path.count;++i) {
        auto a=screen(path.points[i-1]), b=screen(path.points[i]);
        draw->AddLine(a,b,IM_COL32(10,15,28,180),thickness+3);
        draw->AddLine(a,b,blocked ? red:cyan,thickness);
        if(i%4==0) draw->AddCircleFilled(b,2*thickness,blocked ? red:cyan,12);
    }
    auto end=screen(path.points[path.count-1]);
    draw->AddCircle(end,5*thickness,blocked ? red:cyan,24,thickness);
    if(path.target) {
        auto goal=screen(*path.target);
        draw->AddCircle(goal,7*thickness,IM_COL32(203,167,255,165),24,thickness);
        draw->AddLine({goal.x-3*thickness,goal.y},{goal.x+3*thickness,goal.y},IM_COL32(203,167,255,165),thickness);
        draw->AddLine({goal.x,goal.y-3*thickness},{goal.x,goal.y+3*thickness},IM_COL32(203,167,255,165),thickness);
    }
    const auto& text=i18n::get(language);
    std::string label=path.searched ? text.plannedRoute:text.projectedMove;
    if(path.searched) label+="  "+std::to_string(path.safeFrames)+"/36";
    auto pos=screen({10,432});
    draw->AddText({pos.x+1,pos.y+1},IM_COL32(0,0,0,220),label.c_str());
    draw->AddText(pos,blocked ? red:cyan,label.c_str());
    draw->PopClipRect();
}
void draw() {
    if(!controls::state.menu) return;
    std::lock_guard lock(controls::state.mutex); auto& state=controls::state;
    const auto& text=i18n::get(state.language);
    auto* viewport=ImGui::GetMainViewport();
    const float scale=std::clamp(std::min((viewport->Size.x-24.f)/760.f,(viewport->Size.y-24.f)/716.f),.4f,1.f);
    static const ImGuiStyle baseStyle=ImGui::GetStyle(); ImGui::GetStyle()=baseStyle; ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale=scale;
    ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_FirstUseEver,{.5f,.5f});
    ImGui::SetNextWindowSize({760*scale,716*scale},ImGuiCond_Always);
    if(ImGui::Begin("Scarlet Assist",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored({.76f,.59f,1,1},"S C A R L E T   /   A S S I S T");
        ImGui::SameLine(590*scale); if(ImGui::SmallButton(text.hide)) state.menu=false;
        ImGui::TextDisabled("%s: %s   |   %s: %s",text.author,app::author,text.version,app::version);
        ImGui::TextDisabled("TOUHOU 6 NEW CLASSIC");
        ImGui::SameLine(480*scale); ImGui::TextUnformatted(text.language); ImGui::SameLine();
        ImGui::SetNextItemWidth(120*scale); int language=int(state.language);
        if(ImGui::Combo("##language",&language,"ENG\0PL\0RU\0")) { state.language=i18n::valid(language); controls::save(); }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        for(int i=0;i<4;++i) {
            auto& b=state.bindings[i]; ImGui::PushID(i);
            ImGui::BeginChild("card",{0,85*scale},ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPos({16*scale,12*scale}); ImGui::TextUnformatted(text.names[i]);
            ImGui::SetCursorPos({16*scale,43*scale}); ImGui::TextDisabled("%s",text.descriptions[i]);
            ImGui::SetCursorPos({370*scale,10*scale});
            bool enabled=b.enabled;
            if(ImGui::Checkbox(b.hold ? text.armed:text.enabled,&enabled)) b.enabled=enabled;
            ImGui::SameLine(); ImGui::TextColored(b.enabled ? ImVec4(.51f,.87f,.77f,1):ImVec4(.5f,.52f,.6f,1),b.enabled ? text.on:text.off);
            ImGui::SetCursorPos({370*scale,43*scale}); ImGui::SetNextItemWidth(145*scale);
            int mode=b.hold ? 1:0;
            const char* modes[]={text.toggle,text.hold};
            if(ImGui::Combo("##mode",&mode,modes,2)) { b.hold=mode==1; b.enabled=false; b.blocked=state.held(b.key); controls::save(); }
            ImGui::SameLine(); auto label=state.capture==i ? text.capture:keyName(b.key);
            if(ImGui::Button(label.c_str(),{158*scale,0})) state.capture=state.capture==i ? -1:i;
            ImGui::EndChild(); ImGui::PopID();
        }
        if(state.capture>=0) ImGui::TextColored({.76f,.59f,1,1},"%s",text.captureHelp);
        else ImGui::TextDisabled("%s",text.holdHelp);
        if(ImGui::Checkbox(text.prediction,&state.showPrediction)) { state.prediction.count=0; controls::save(); }
        ImGui::TextDisabled("%s",text.predictionHelp);
        if(ImGui::Button(text.stopAll)) state.stop();
        ImGui::SameLine(); ImGui::TextDisabled("%s",text.pausedHelp);
        ImGui::Separator();
        ImGui::TextColored(state.ready ? ImVec4(.51f,.87f,.77f,1):ImVec4(1,.6f,.5f,1),"%s",state.stopped ? text.stopped:(state.ready ? text.ready:(state.status.empty() ? text.starting:text.error)));
        ImGui::SameLine(); ImGui::TextDisabled(text.stats,state.power,state.bombs,state.bullets,state.lasers);
        if(state.saveFailed) ImGui::TextColored({1,.6f,.5f,1},"%s",text.saveError);
        if(!state.ready && !state.status.empty()) { ImGui::TextWrapped("%s",state.status.c_str()); }
    }
    ImGui::End();
}
bool initializationFailed=false;
bool initialize(IDXGISwapChain* chain) {
    DXGI_SWAP_CHAIN_DESC desc{}; if(FAILED(chain->GetDesc(&desc))) return false;
    DWORD pid=0; GetWindowThreadProcessId(desc.OutputWindow,&pid); if(pid!=GetCurrentProcessId()) return false;
    if(FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))) return false;
    device->GetImmediateContext(&context); window=desc.OutputWindow;
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    wchar_t fonts[MAX_PATH]{}; GetWindowsDirectoryW(fonts,MAX_PATH);
    std::string font=(std::filesystem::path(fonts)/"Fonts"/"segoeui.ttf").string();
    static const ImWchar ranges[]={0x20,0x024f,0x0400,0x052f,0};
    io.Fonts->AddFontFromFileTTF(font.c_str(),16,nullptr,ranges);
    theme();
    if(!ImGui_ImplWin32_Init(window) || !ImGui_ImplDX11_Init(device,context)) { log("ImGui backend initialization failed"); return false; }
    SetLastError(0); originalWndProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(windowProc)));
    if(!originalWndProc) { log("WndProc initialization failed"); return false; }
    activeChain=chain; makeTarget(chain); log("ImGui initialized on game D3D11 swapchain"); return true;
}
HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* chain,UINT interval,UINT flags) {
    {
        std::lock_guard lock(renderMutex);
        if(!(flags&DXGI_PRESENT_TEST)) {
            if(!activeChain && !initializationFailed && !initialize(chain)) { initializationFailed=true; controls::status("Panel initialization failed; restart game."); }
            if(activeChain==chain && (target || makeTarget(chain))) {
                ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); drawPrediction(); draw(); ImGui::Render();
                ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{}; ID3D11DepthStencilView* depth=nullptr;
                context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,oldTargets,&depth);
                context->OMSetRenderTargets(1,&target,nullptr); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,oldTargets,depth);
                for(auto* old:oldTargets) if(old) old->Release();
                if(depth) depth->Release();
            }
        }
    }
    return originalPresent(chain,interval,flags);
}
HRESULT STDMETHODCALLTYPE resize(IDXGISwapChain* chain,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) {
    std::lock_guard lock(renderMutex);
    if(chain==activeChain && target) { target->Release(); target=nullptr; }
    return originalResize(chain,count,width,height,format,flags);
}
DWORD WINAPI engine(void*) { char name[]="scarlet_assist"; char* args[]={name}; int code=assistMain(1,args); if(!code) { std::lock_guard lock(controls::state.mutex); controls::state.stopped=true; controls::state.ready=false; } return 0; }
DWORD WINAPI bootstrap(void*) {
    try {
        controls::load(); log((std::string("Starting Scarlet Assist ")+app::version).c_str());
        WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"ScarletAssistD3DProbe";
        RegisterClassW(&wc); HWND dummy=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
        DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferCount=1; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow=dummy; desc.SampleDesc.Count=1; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        IDXGISwapChain* chain=nullptr; ID3D11Device* probe=nullptr; ID3D11DeviceContext* probeContext=nullptr;
        HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&probe,nullptr,&probeContext);
        if(FAILED(result)) throw std::runtime_error("Cannot create D3D11 hook probe");
        auto** table=*reinterpret_cast<void***>(chain); void* presentAddress=table[8]; void* resizeAddress=table[13];
        if(MH_Initialize()!=MH_OK || MH_CreateHook(presentAddress,reinterpret_cast<void*>(&present),reinterpret_cast<void**>(&originalPresent))!=MH_OK ||
           MH_CreateHook(resizeAddress,reinterpret_cast<void*>(&resize),reinterpret_cast<void**>(&originalResize))!=MH_OK) throw std::runtime_error("Cannot prepare rendering hooks");
        probeContext->Release(); probe->Release(); chain->Release(); DestroyWindow(dummy); UnregisterClassW(wc.lpszClassName,wc.hInstance);
        if(MH_EnableHook(resizeAddress)!=MH_OK || MH_EnableHook(presentAddress)!=MH_OK) throw std::runtime_error("Cannot enable rendering hooks");
        log("D3D11 hooks enabled");
        HANDLE worker=CreateThread(nullptr,0,engine,nullptr,0,nullptr); if(worker) CloseHandle(worker); else controls::status("Cannot start assist thread");
    } catch(const std::exception& e) { controls::status(e.what()); log(e.what()); }
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(module); HANDLE thread=CreateThread(nullptr,0,bootstrap,nullptr,0,nullptr); if(thread) CloseHandle(thread); else return FALSE; }
    return TRUE;
}




