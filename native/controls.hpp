#pragma once
#include <windows.h>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include "i18n.hpp"
#include "prediction.hpp"
namespace controls {
struct Binding {
    int key=0;
    bool hold=false, enabled=false, active=false, blocked=false;
    void press(bool rising) { if(rising && !hold && !blocked) enabled=!enabled; }
    void tick(bool down,bool usable) { if(!down) blocked=false; active=usable && enabled && (!hold || (down && !blocked)); }
    void bind(int value,bool isDown) { key=value; blocked=isDown; active=false; }
};
struct Shared {
    std::mutex mutex;
    std::array<Binding,4> bindings{{{VK_F8},{VK_F7},{VK_F6},{VK_F5}}};
    std::array<bool,256> physical{};
    std::atomic_bool menu{true};
    int capture=-1;
    i18n::Language language=i18n::Language::English;
    bool saveFailed=false, stopped=false;
    std::string status, pattern;
    int bullets=0, lasers=0, power=0, bombs=0;
    bool playing=false, ready=false;
    bool showPrediction=false;
    prediction::Path prediction;
    ULONGLONG predictionTime=0;
    std::filesystem::path config;
    bool held(int key) const {
        if(key==VK_SHIFT) return physical[VK_LSHIFT] || physical[VK_RSHIFT];
        if(key==VK_CONTROL) return physical[VK_LCONTROL] || physical[VK_RCONTROL];
        if(key==VK_MENU) return physical[VK_LMENU] || physical[VK_RMENU];
        return key>0 && key<256 && physical[key];
    }
    void stop() { for(auto& b:bindings) { b.enabled=false; b.active=false; b.blocked=true; } prediction.count=0; }
};
inline Shared state;
inline void save() {
    state.saveFailed=!WritePrivateProfileStringW(L"UI",L"Language",std::to_wstring(int(state.language)).c_str(),state.config.c_str());
    if(!WritePrivateProfileStringW(L"UI",L"Prediction",state.showPrediction ? L"1":L"0",state.config.c_str())) state.saveFailed=true;
    for(int i=0;i<4;++i) {
        auto section=L"Feature"+std::to_wstring(i);
        if(!WritePrivateProfileStringW(section.c_str(),L"Key",std::to_wstring(state.bindings[i].key).c_str(),state.config.c_str())) state.saveFailed=true;
        if(!WritePrivateProfileStringW(section.c_str(),L"Hold",state.bindings[i].hold ? L"1":L"0",state.config.c_str())) state.saveFailed=true;
    }
}
inline void readPreferences() {
    state.language=i18n::valid(int(GetPrivateProfileIntW(L"UI",L"Language",0,state.config.c_str())));
    state.showPrediction=GetPrivateProfileIntW(L"UI",L"Prediction",0,state.config.c_str())==1;
    for(int i=0;i<4;++i) {
        auto section=L"Feature"+std::to_wstring(i); auto& b=state.bindings[i];
        int key=int(GetPrivateProfileIntW(section.c_str(),L"Key",b.key,state.config.c_str()));
        if(key>=0 && key<=255 && key!=VK_INSERT && key!=VK_F9) b.key=key;
        b.hold=GetPrivateProfileIntW(section.c_str(),L"Hold",0,state.config.c_str())==1;
        b.enabled=false; b.active=false; b.blocked=false;
    }
}
inline void load() {
    wchar_t path[32768]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768)) throw std::runtime_error("LOCALAPPDATA unavailable");
    state.config=std::filesystem::path(path)/L"TouhouAssist"/L"settings.ini";
    std::filesystem::create_directories(state.config.parent_path());
    readPreferences();
}
inline bool foreground() { DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid); return pid==GetCurrentProcessId(); }
inline void event(int key,bool pressed) {
    if(key<=0 || key>=256) return;
    std::lock_guard lock(state.mutex);
    bool rising=pressed && !state.physical[key]; state.physical[key]=pressed;
    if(!foreground()) return;
    if(rising && key==VK_F9) { state.stop(); state.capture=-1; return; }
    if(rising && key==VK_INSERT) { state.menu=!state.menu.load(); state.capture=-1; return; }
    if(state.menu) {
        if(state.capture>=0 && rising) {
            if(key!=VK_ESCAPE) {
                auto& b=state.bindings[state.capture];
                b.bind(key==VK_BACK || key==VK_DELETE ? 0:key,true); save();
            }
            state.capture=-1;
        }
        return;
    }
    if(rising) for(auto& b:state.bindings) {
        bool match=b.key==key || (b.key==VK_SHIFT && (key==VK_LSHIFT || key==VK_RSHIFT));
        if(match) b.press(true);
    }
}
inline std::array<bool,4> tick(bool foreground) {
    std::lock_guard lock(state.mutex); std::array<bool,4> result{};
    for(int i=0;i<4;++i) { auto& b=state.bindings[i]; b.tick(state.held(b.key),true); result[i]=b.active; b.active=b.active && foreground && !state.menu; }
    return result;
}
inline void status(const std::string& value,bool ready=false) { std::lock_guard lock(state.mutex); state.status=value; state.ready=ready; }
}




