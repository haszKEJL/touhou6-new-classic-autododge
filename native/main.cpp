#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include "offsets.hpp"
#include "planner.hpp"
#include "behavior.hpp"
#include "navigation.hpp"
#include "extra.hpp"
#include "extra_tests.hpp"
#include "maze_tests.hpp"
#include "maze_corridor_tests.hpp"
#include "scoring_tests.hpp"
#include "controls.hpp"

using Clock = std::chrono::steady_clock;
std::atomic_bool stopping{false};
BOOL WINAPI control(DWORD) { stopping = true; return TRUE; }
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

#include "game_layout.hpp"
#include "layout_tests.hpp"

std::string hashFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open game executable for version check.");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 unavailable.");
    struct Cleanup { BCRYPT_ALG_HANDLE& a; BCRYPT_HASH_HANDLE& h;
        ~Cleanup() { if(h) BCryptDestroyHash(h); if(a) BCryptCloseAlgorithmProvider(a,0); }
    } cleanup{algorithm, hash};
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("SHA256 init failed.");
    std::array<unsigned char, 65536> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        if (BCryptHashData(hash, buffer.data(), ULONG(input.gcount()), 0) < 0)
            throw std::runtime_error("SHA256 update failed.");
    }
    if (!input.eof()) throw std::runtime_error("Cannot read full executable.");
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), digest.size(), 0) < 0)
        throw std::runtime_error("SHA256 finish failed.");
    std::ostringstream out;
    for (auto b : digest) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
    return out.str();
}

DWORD findGame() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    DWORD result = 0;
    if (Process32FirstW(snapshot.value, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"th06nc.exe") == 0) {
            if (result) throw std::runtime_error("More than one th06nc process. Close the extra instance.");
            result = entry.th32ProcessID;
        }
    } while (Process32NextW(snapshot.value, &entry));
    return result;
}

struct Game {
    DWORD pid;
    Handle process;
    uintptr_t base = 0;
    size_t imageSize=0;
    GameLayout layout{};
    std::string profileName;
    explicit Game(DWORD id) : pid(id), process(OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, id)) {
        if (!process.value) throw std::runtime_error("Cannot read game process. Use the same privilege level as the game.");
        std::filesystem::path imagePath;
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        MODULEENTRY32W module{}; module.dwSize = sizeof(module);
        if (Module32FirstW(snapshot.value, &module)) do {
            if (_wcsicmp(module.szModule, L"th06nc.exe") == 0) {
                base = reinterpret_cast<uintptr_t>(module.modBaseAddr); imageSize=module.modBaseSize; imagePath=module.szExePath; break;
            }
        } while (Module32NextW(snapshot.value, &module));
        if (!base || !imageSize) throw std::runtime_error("Game module not found.");
        if (hashFile(imagePath) == offsets::sha256) {
            layout=legacyLayout(); profileName="New Classic verified 1.03";
            const std::array<unsigned char, 10> expected{0xc7,0x83,0x30,0x77,0,0,0,0,0x40,0x43};
            std::array<unsigned char, 10> actual{}; read(0x6822e,actual.data(),actual.size());
            if(actual!=expected) throw std::runtime_error("Loaded player code differs from the verified profile.");
        } else {
            const auto executable=ExecutableCode::load(imagePath);
            IMAGE_DOS_HEADER dos{}; read(0,&dos,sizeof(dos));
            if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<64) throw std::runtime_error("Invalid loaded image.");
            IMAGE_NT_HEADERS64 nt{}; read(size_t(dos.e_lfanew),&nt,sizeof(nt));
            if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.TimeDateStamp!=executable.timestamp ||
               nt.OptionalHeader.SizeOfImage!=executable.imageSize || imageSize!=executable.imageSize)
                throw std::runtime_error("Game was updated while running. Restart it before attaching.");
            layout=resolveLayout(executable);
            profileName="New Classic signature profile";
        }
        std::cout << "Matched " << profileName << ". Module: 0x" << std::hex << base << std::dec << '\n';
    }
    void read(uintptr_t rva, void* data, size_t size) const {
        if(rva>imageSize || size>imageSize-rva) throw std::runtime_error("Game read outside module bounds.");
        SIZE_T received = 0;
        if (!ReadProcessMemory(process.value, reinterpret_cast<const void*>(base+rva), data, size, &received) || received != size)
            throw std::runtime_error("Incomplete memory read; input stopped.");
    }
    template<class T> T read(uintptr_t rva) const { T v{}; read(rva,&v,sizeof(v)); return v; }
    void absolute(uintptr_t address,void* data,size_t size) const {
        SIZE_T got=0;
        if(!ReadProcessMemory(process.value,reinterpret_cast<void*>(address),data,size,&got) || got!=size)
            throw std::runtime_error("Incomplete absolute memory read.");
    }
    template<class T> T absolute(uintptr_t address) const { T v{}; absolute(address,&v,sizeof(v)); return v; }
    std::string nativeString(uintptr_t address) const {
        auto length=absolute<uint64_t>(address+0x10), capacity=absolute<uint64_t>(address+0x18);
        if(length>512 || capacity<length) return {};
        std::string value(size_t(length),'\0');
        if(length) absolute(capacity<=15 ? address:absolute<uintptr_t>(address),value.data(),value.size());
        return value;
    }
    std::string translate(const std::string& key) const {
        auto map=read<uintptr_t>(layout.translations);
        if(!map || key.empty()) return key;
        uint64_t hash=0xcbf29ce484222325ull;
        for(unsigned char c:key) { hash^=c; hash*=0x100000001b3ull; }
        auto mask=absolute<uint64_t>(map+0x30);
        if(mask>0x100000) return key;
        auto bucket=absolute<uintptr_t>(map+0x18)+(hash&mask)*16;
        auto head=absolute<uintptr_t>(map+8), first=absolute<uintptr_t>(bucket), node=absolute<uintptr_t>(bucket+8);
        for(int i=0;i<128 && node && node!=head;++i) {
            if(nativeString(node+0x10)==key) return nativeString(node+0x30);
            if(node==first) break;
            node=absolute<uintptr_t>(node+8);
        }
        return key;
    }
    bool foreground() const { DWORD active{}; GetWindowThreadProcessId(GetForegroundWindow(), &active); return active == pid; }
    bool live() const { return WaitForSingleObject(process.value,0) == WAIT_TIMEOUT; }
};

template<class T> T field(const unsigned char* data, size_t at) { T v; std::memcpy(&v,data+at,sizeof(v)); return v; }
bool finite(Vec v) { return std::isfinite(v.x) && std::isfinite(v.y); }
struct State {
    Vec player;
    float speed{}, radius{}, fast{}, slow{};
    int power{}, bombs{}, grace{};
    bool bombActive{}, dialogue=true;
    unsigned playerState{};
    int scene{};
    int spell=-1, spellState=0;
    std::string spellKey;
    bool active{};
    std::vector<Bullet> bullets;
    std::vector<Laser> lasers;
    uint32_t simulationFrame{};
    std::vector<Item> items;
    std::vector<Enemy> enemies;
};
State sample(const Game& game, bool focus) {
    State state;
    std::array<unsigned char, 0x170> p{};
    const uintptr_t player=game.base+game.layout.player;
    game.absolute(player+game.layout.playerXY,p.data(),p.size());
    state.player = field<Vec>(p.data(),0);
    state.radius = field<float>(p.data(), game.layout.playerRadius-game.layout.playerXY);
    state.speed = field<float>(p.data(), (focus ? game.layout.focusSpeed : game.layout.speed)-game.layout.playerXY);
    state.playerState = p[game.layout.playerState-game.layout.playerXY];
    state.fast=field<float>(p.data(),game.layout.speed-game.layout.playerXY);
    state.slow=field<float>(p.data(),game.layout.focusSpeed-game.layout.playerXY);
    state.grace=game.layout.bombGrace ? game.absolute<int>(player+game.layout.bombGrace):0;
    state.power=game.layout.power ? game.read<uint16_t>(game.layout.power):0;
    state.bombs=game.read<uint8_t>(game.layout.bombs);
    state.bombActive=game.layout.bombActive ? game.absolute<uint8_t>(player+game.layout.bombActive)!=0:false;
    auto gui=game.layout.gui ? game.read<uintptr_t>(game.layout.gui):0;
    if(gui && game.layout.dialogue) {
        int dialogue=0; SIZE_T received=0;
        if(!ReadProcessMemory(game.process.value,reinterpret_cast<void*>(gui+game.layout.dialogue),&dialogue,sizeof(dialogue),&received) || received!=sizeof(dialogue))
            throw std::runtime_error("Invalid dialogue snapshot.");
        state.dialogue=dialogue>=0;
    }
    state.scene = game.layout.scene ? game.read<int>(game.layout.scene):2;
    state.spellState=game.layout.spellState ? game.read<int>(game.layout.spellState):0;
    int spell=game.layout.spellId ? game.read<int>(game.layout.spellId):-1;
    if(state.spellState>0 && spell>=0 && spell<int(game.layout.spellRecordCount)) state.spell=spell;
    if(state.spell>=0) {
        std::array<char,129> key{};
        game.read(game.layout.spellRecords+state.spell*game.layout.spellRecordStride+game.layout.spellRecordName,key.data(),128);
        std::string value=key.data();
        if(value.starts_with("ST_ECLDATA")) state.spellKey=value;
    }
    std::array<unsigned char,10> flags{};
    if(game.layout.pause) game.read(game.layout.pause,flags.data(),flags.size());
    const bool validPlayerState=state.playerState==0 || state.playerState==3;
    state.active = state.scene == 2 && !flags[0] && !flags[1] && !flags[6] && !flags[9] &&
        validPlayerState && finite(state.player) &&
        state.player.x >= 0 && state.player.x <= 384 && state.player.y >= 0 && state.player.y <= 448 &&
        std::isfinite(state.speed) && state.speed > 0 && state.speed <= 10 &&
        std::isfinite(state.radius) && state.radius > 0 && state.radius <= 10 &&
        std::isfinite(state.fast) && state.fast>0 && state.fast<=10 &&
        std::isfinite(state.slow) && state.slow>0 && state.slow<=state.fast && state.power<=128 && state.bombs<=8;
    std::vector<unsigned char> data(game.layout.bulletStride*game.layout.bulletCount);
    game.read(game.layout.bullets,data.data(),data.size());
    for (size_t i=0; i<game.layout.bulletCount; ++i) {
        const auto* b = data.data()+i*game.layout.bulletStride;
        auto status = field<uint16_t>(b,game.layout.bulletState);
        if (status == 0 || status == 5) continue;
        if (status > 5) throw std::runtime_error("Invalid bullet state; profile or snapshot invalid.");
        Vec pos = field<Vec>(b,game.layout.bulletXY), v = field<Vec>(b,game.layout.bulletVelocity);
        Vec size = field<Vec>(b,game.layout.bulletSize);
        if (!finite(pos) || !finite(v) || !finite(size) || size.x <= 0 || size.y <= 0 ||
            size.x > 128 || size.y > 128 || std::abs(v.x)>100 || std::abs(v.y)>100)
            throw std::runtime_error("Invalid bullet values; input stopped.");
        auto age=field<uint32_t>(b,game.layout.bulletAge);
        state.bullets.push_back({pos,v,std::min(size.x,size.y)*.5f,age<=1000000 ? int(age):-1});
    }
    data.resize(game.layout.laserStride*game.layout.laserCount);
    game.read(game.layout.lasers,data.data(),data.size());
    state.simulationFrame=game.read<uint32_t>(game.layout.simulationFrame);
    for(size_t i=0;i<game.layout.laserCount;++i) {
        const auto* raw=data.data()+i*game.layout.laserStride;
        if(!raw[game.layout.laserInUse]) continue;
        Laser laser;
        laser.slot=unsigned(i);
        laser.origin=field<Vec>(raw,game.layout.laserOrigin);
        laser.angle=field<float>(raw,game.layout.laserAngle);
        laser.start=field<float>(raw,game.layout.laserStart);
        laser.end=field<float>(raw,game.layout.laserEnd);
        laser.maxLength=field<float>(raw,game.layout.laserLength);
        laser.speed=field<float>(raw,game.layout.laserSpeed);
        laser.width=field<float>(raw,game.layout.laserWidth);
        laser.phase=raw[game.layout.laserPhase];
        laser.timer=field<int>(raw,game.layout.laserTimer);
        laser.startTime=field<int>(raw,game.layout.laserStartTime);
        laser.duration=field<int>(raw,game.layout.laserDuration);
        if(!finite(laser.origin) || !std::isfinite(laser.angle) || !std::isfinite(laser.start) ||
           !std::isfinite(laser.end) || !std::isfinite(laser.maxLength) || !std::isfinite(laser.speed) ||
           !std::isfinite(laser.width) || laser.width<0 || laser.width>1024 || std::abs(laser.speed)>100 ||
           std::abs(laser.start)>100000 || std::abs(laser.end)>100000 || laser.maxLength<0 || laser.maxLength>100000 ||
           laser.phase>2 || laser.timer<0 || laser.timer>10000000 || laser.startTime<0 || laser.startTime>10000000 ||
           laser.duration<0 || laser.duration>10000000) throw std::runtime_error("Invalid laser snapshot; input stopped.");
        state.lasers.push_back(laser);
    }
    data.resize(game.layout.itemStride*game.layout.itemCount);
    game.read(game.layout.items,data.data(),data.size());
    for(size_t i=0;i<game.layout.itemCount;++i) {
        const auto* item=data.data()+i*game.layout.itemStride;
        if(!item[game.layout.itemActive]) continue;
        Vec pos=field<Vec>(item,game.layout.itemXY), v=field<Vec>(item,game.layout.itemVelocity);
        int type=item[game.layout.itemType];
        if(!finite(pos) || !finite(v) || type>6) throw std::runtime_error("Invalid item snapshot.");
        state.items.push_back({pos,v,type,item[0x0c]==1});
    }
    data.resize(game.layout.enemyStride*game.layout.enemyCount);
    game.read(game.layout.enemies,data.data(),data.size());
    for(size_t i=0;i<game.layout.enemyCount;++i) {
        const auto* enemy=data.data()+i*game.layout.enemyStride;
        if(!(enemy[game.layout.enemyFlags]&0x80)) continue;
        unsigned flags=enemy[game.layout.enemyFlags+1];
        Vec pos=field<Vec>(enemy,game.layout.enemyXY), v=field<Vec>(enemy,game.layout.enemyVelocity);
        Vec size=field<Vec>(enemy,game.layout.enemySize);
        int life=field<int>(enemy,game.layout.enemyLife);
        if(!finite(pos) || !finite(v) || !finite(size)) throw std::runtime_error("Invalid enemy snapshot.");
        if(!(flags&9) || life<=0) continue;
        state.enemies.push_back({pos,v,size,life,bool(flags&8),bool((flags&1)&&(flags&16)),bool((flags&1)&&(flags&2))});
    }
    // Refuse a sample if a pause/menu transition occurred during the block read.
    std::array<unsigned char,10> after{};
    if(game.layout.pause) game.read(game.layout.pause,after.data(),after.size());
    state.active = state.active && after == flags && game.read<int>(game.layout.scene) == state.scene;
    return state;
}

// Track physical arrows and Z separately from injected input. Own only the
// controls being automated, and restore physical input when automation stops.
class Keyboard {
    static inline Keyboard* instance = nullptr;
    HHOOK hook{};
    std::array<bool,8> physical{}, output{};
    bool correcting = false;
    bool firing = false;
    bool focusing=false, bombing=false;
    static constexpr std::array<int,8> keys{VK_LEFT,VK_UP,VK_RIGHT,VK_DOWN,'Z',VK_LSHIFT,'X',VK_RSHIFT};
    static LRESULT CALLBACK callback(int code, WPARAM msg, LPARAM arg) {
        if (code == HC_ACTION && instance) {
            const auto& event = *reinterpret_cast<KBDLLHOOKSTRUCT*>(arg);
            if (!(event.flags & LLKHF_INJECTED)) {
#ifdef TH_ASSIST_DLL
                controls::event(int(event.vkCode),msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
#endif
                for (size_t i=0;i<keys.size();++i) if (event.vkCode == unsigned(keys[i])) {
                    instance->physical[i] = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
                    if ((i<4 && instance->correcting) || (i==4 && instance->firing) || (i==5 && instance->focusing) || (i==6 && instance->bombing)) return 1;
                    instance->output[i] = instance->physical[i];
                }
            }
        }
        return CallNextHookEx(nullptr,code,msg,arg);
    }
    void apply(const std::array<bool,8>& desired,size_t begin=0,size_t end=4) {
        for (size_t i=begin;i<end;++i) if (desired[i] != output[i]) {
            INPUT input{}; input.type = INPUT_KEYBOARD;
            input.ki.wScan = WORD(MapVirtualKeyW(keys[i], MAPVK_VK_TO_VSC));
            input.ki.dwFlags = KEYEVENTF_SCANCODE | (i<4 ? KEYEVENTF_EXTENDEDKEY : 0) | (desired[i] ? 0 : KEYEVENTF_KEYUP);
            if (SendInput(1,&input,sizeof(input)) != 1) throw std::runtime_error("SendInput failed.");
            output[i] = desired[i];
        }
    }
public:
    Keyboard() {
        for(size_t i=0;i<keys.size();++i) physical[i] = output[i] = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        #ifdef TH_ASSIST_DLL
        { std::lock_guard lock(controls::state.mutex);
          for(int key=1;key<256;++key) controls::state.physical[key]=(GetAsyncKeyState(key)&0x8000)!=0;
          for(auto& b:controls::state.bindings) b.blocked=controls::state.held(b.key);
        }
#endif
        instance = this;
        HMODULE owner=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&callback),&owner);
        hook = SetWindowsHookExW(WH_KEYBOARD_LL, callback, owner,0);
        if (!hook) { instance=nullptr; throw std::runtime_error("Keyboard hook failed."); }
    }
    ~Keyboard() { try { release(false); } catch (...) {} try { shoot(false,false); } catch (...) {} try { focus(false,false); bomb(false,false); } catch (...) {} if(hook) UnhookWindowsHookEx(hook); instance=nullptr; }
    void pump() { MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    Direction user() const { return {int(physical[2])-int(physical[0]),int(physical[3])-int(physical[1])}; }
    void correct(Direction d) {
        correcting = true;
        apply({d.x<0,d.y<0,d.x>0,d.y>0});
    }
    void release(bool restore) {
        if (correcting) { apply(restore ? physical : std::array<bool,8>{}); correcting=false; }
    }
    bool physicalFocus() const { return physical[5] || physical[7]; }
    void auxiliary(size_t index,bool enabled,bool restore,bool& owned) {
        if(enabled || owned) {
            auto desired=output; desired[index]=enabled || (restore && physical[index]);
            owned=true; apply(desired,index,index+1); owned=enabled;
        }
    }
    void focus(bool enabled,bool restore) { auxiliary(5,enabled,restore,focusing); }
    void bomb(bool enabled,bool restore) { auxiliary(6,enabled,restore,bombing); }
    void shoot(bool enabled,bool restore) {
        if(enabled || firing) {
            auto desired=output;
            desired[4]=enabled || (restore && physical[4]);
            firing=true; // Own Z until any pending synthetic transition is applied.
            apply(desired,4,5);
            firing=enabled;
        }
    }
};
bool down(int key) { return (GetAsyncKeyState(key)&0x8000) != 0; }

int selfTest() {
    int tests=0;
    auto require=[&](bool condition,const char* name) { ++tests; if(!condition) throw std::runtime_error(name); };
    layoutPrimitiveTests(require);
    Vec p{192,350};
    require(plan(p,2,1.25,{}, {1,0}) == Direction{1,0},"Preserve player input on empty field");
    std::vector<Bullet> incoming{{{192,320},{0,3},4}};
    auto dodge = plan(p,2,1.25,incoming,{});
    require(dodge != Direction{},"Dodge incoming bullet");
    require(clearance(p,velocity(dodge,2),1.25,incoming)>clearance(p,{},1.25,incoming),"Dodge improves predicted clearance");
    require(plan({9,350},2,1.25,{{{9,320},{0,3},4}},{}).x>=0,"Do not cross left wall");
    require(plan(p,2,1.25,{{{192,320},{0,-3},4}}, {})==Direction{},"Ignore departing threat");
    require(plan(p,2,1.25,{{{220,350},{0,0},4}}, {1,0})!=Direction{1,0},"Correct unsafe user movement");
    require(redPower(0) && redPower(2) && !redPower(1) && !redPower(6),"Select red power items only");
    std::vector<Item> red{{{260,350},{0,1},0}};
    auto collecting=intention(p,4,red,{}, {},true,false);
    require(collecting.objective==Objective::Collect && collecting.direction.x==1,"Collect red item on a clear field");
    require(intention(p,4,red,{}, {-1,0},true,false).direction==Direction{-1,0},"Manual arrows override F7 collection");
    require(intention(p,4,{{{260,350},{0,1},1}}, {},{},true,false).objective==Objective::Manual,"Do not chase blue items");
    require(intention(p,4,{}, {},{},true,false).direction==Direction{},"F7 without items stays still");
    std::vector<Enemy> enemies{{{192,100},{0,0},{24,24},20,false,true,true},
                               {{300,120},{0,0},{48,48},2000,true,true,true}};
    require(attackTarget(p,enemies)==&enemies[0],"Finish nearby enemy instead of crossing entire field for boss");
    auto fighting=intention(p,4,{{{60,320},{0,1},2}},enemies,{},false,true);
    require(fighting.objective==Objective::Attack && fighting.direction.x==0,"Stay in firing lane instead of chasing distant P");
    auto opportunistic=intention({300,350},4,{{{310,335},{0,1},0}},enemies,{},false,true);
    require(opportunistic.objective==Objective::Collect,"Collect nearby red item in attack lane");
    enemies[1].damageable=false;
    require(attackTarget(p,enemies)==&enemies[0],"Ignore invulnerable target for attack selection");
    require(intention(p,4,red,{}, {},false,true).objective==Objective::Collect,"Autoplay collects between enemy waves");
    require(intention(p,4,{}, {},{},false,true).objective==Objective::Wait,"Autoplay holds a useful position between waves");
    auto bodies=obstacles({},{{{216,350},{0,0},{28,28},20,false,true,true}});
    require(plan(p,4,1.25,bodies,{1,0})!=Direction{1,0},"Avoid enemy body while collecting");
    require(plan({375,350},4,1.25,{}, {1,0}).x<=0,"Do not drive out of field without bullets");
    Toggle toggle;
    require(toggle.update(true) && toggle.enabled,"Toggle turns on at press");
    require(!toggle.update(true) && toggle.enabled,"Held toggle does not repeat");
    require(!toggle.update(false) && toggle.enabled,"Release retains toggle state");
    require(toggle.update(true) && !toggle.enabled,"Second press turns toggle off");
    require(intention(p,4,{{{60,120},{0,1},2}},enemies,{},false,true,0).objective==Objective::Attack,"Low power does not cause upward chase during combat");
    require(intention(p,4,{},enemies,{},false,true,0).objective==Objective::Attack,"Attack to generate power drops");
    require(redPower(4),"Full power pickup contributes power");
    require(intention(p,4,{{{260,350},{0,1},0,true}},{},{},true,false).objective==Objective::Manual,"Do not chase items already homing");
    require(escapeAvailable(p,1.25,{},4,2),"Empty field needs no bomb");
    require(escapeAvailable(p,1.25,incoming,4,2),"Escapable bullet needs no bomb");
    require(!escapeAvailable(p,1.25,{{p,{},12}},4,2),"Overlapping threat has no predicted escape");
    std::vector<Bullet> ring;
    for(int i=0;i<32;++i) { float a=float(i)*6.2831853f/32; ring.push_back({{p.x+20*std::cos(a),p.y+20*std::sin(a)},{-4*std::cos(a),-4*std::sin(a)},4}); }
    require(!escapeAvailable(p,1.25,ring,4,2),"Closing ring triggers last resort");
    BombControl bomb;
    require(bombEligible(true,true,0,false,false,8),"Autobomb eligibility does not depend on inventory");
    require(!bombEligible(true,true,1,false,false,8),"Autobomb waits for respawn to finish");
    require(!bombEligible(true,true,2,false,false,8),"Autobomb never triggers while dead");
    require(!bombEligible(true,true,3,false,false,8),"Invulnerable player does not need an emergency bomb");
    require(!bombEligible(true,true,0,true,false,8),"An active bomb suppresses further bomb input");
    require(!bombEligible(true,true,0,false,true,8),"Autobomb is disabled during dialogue");
    require(!bombEligible(true,true,0,false,false,0),"Respect the game's bomb input gate");
    require(!bomb.update(0,false,true),"Inactive bomb gate blocks input");
    require(!bomb.update(0,true,false),"Safe route preserves bombs");
    require(bomb.update(0,true,true),"Trapped eligible player starts bomb pulse");
    require(bomb.update(.03,true,false),"Pulse spans multiple game frames");
    require(!bomb.update(.07,true,true),"Bomb pulse releases X");
    require(!bomb.update(.5,true,true),"Cooldown avoids repeated bombs");
    require(bomb.update(.9,true,true),"Cooldown eventually rearms");
    require(!bomb.update(.91,false,true),"Pause interrupts bomb pulse");
    SweepControl sweep;
    std::vector<Item> drops{{{100,150},{},0},{{200,150},{},2},{{300,150},{},0}};
    require(!sweep.update(0,p,0,drops,enemies,{}),"Keep attacking instead of sweeping a small batch");
    require(!sweep.update(.1,p,0,drops,enemies,{{{192,240},{},8}}),"Threat aborts upward collection");
    drops.push_back({{160,150},{},2});
    require(sweep.update(.2,p,0,drops,{},{}),"Collect valuable power batch in clear post-boss window");
    require(!sweep.update(.3,{192,120},128,drops,{},{}),"Stop ascending after crossing collection line");
    require(!sweep.update(.4,p,128,drops,{},{}),"Collection cooldown prevents oscillation");
    require(preferFocus(p,incoming,{{},Objective::Collect}),"Focus for nearby threats");
    require(!preferFocus(p,{},{{0,-1},Objective::Sweep}),"Fast travel on clear collection route");
    require(preferFocus(p,{},{{},Objective::Attack}),"Focused fire when aligned with enemy");
    Laser vertical;
    vertical.origin={192,0}; vertical.angle=1.570796327f; vertical.end=448;
    vertical.maxLength=448; vertical.width=16; vertical.phase=1; vertical.duration=300;
    require(laserClearance(p,p,1.25,{vertical},0)<0,"Detect middle of long beam far from its origin");
    require(laserClearance({205,350},{205,350},1.25,{vertical},0)>0,"Respect beam width rather than full sprite rectangle");
    require(laserClearance({180,350},{204,350},1.25,{vertical},0)<0,"Swept path cannot tunnel across thin laser");
    require(plan({180,350},4,1.25,{}, {1,0},{vertical})!=Direction{1,0},"Avoid walking into laser with empty bullet table");
    Laser diagonal=vertical; diagonal.origin={20,20}; diagonal.angle=.785398163f;
    require(laserClearance({200,200},{200,200},1.25,{diagonal},0)<0,"Rotated diagonal beam collides along its length");
    require(laserClearance({200,230},{200,230},1.25,{diagonal},0)>0,"Diagonal beam leaves safe adjacent lane");
    Laser shortBeam=vertical; shortBeam.start=100; shortBeam.end=200; shortBeam.maxLength=100;
    require(laserClearance(p,p,1.25,{shortBeam},0)>0,"Do not extend beam beyond actual endpoint");
    require(laserClearance({192,50},{192,50},1.25,{shortBeam},0)>0,"Respect clipped beam origin");
    Laser warning=vertical; warning.phase=0; warning.startTime=30; warning.timer=0;
    require(clearance(p,{},1.25,{}, {warning})>0,"Distant warning does not count as active hitbox");
    require(escapeAvailable(p,1.25,{},4,2,{warning}),"Do not bomb merely for warning line");
    warning.timer=24;
    auto laserDodge=plan(p,2,1.25,{}, {},{warning});
    require(laserDodge.x!=0,"Leave beam before activation");
    require(clearance(p,velocity(laserDodge,2),1.25,{}, {warning})>0,"Predicted warning dodge clears the beam");
    require(escapeAvailable(p,1.25,{},4,2,{warning}),"Preserve bomb when warning beam is escapable");
    Laser trapped=warning; trapped.timer=29; trapped.width=200;
    require(!escapeAvailable(p,1.25,{},4,2,{trapped}),"Autobomb detects unavoidable activating wide laser");
    Laser fading=vertical; fading.phase=2;
    require(laserClearance(p,p,1.25,{fading},0)>0,"Fading NC laser has no damaging hitbox");
    require(escapeAvailable(p,1.25,{},4,2,{fading}),"Fading visual must not waste bombs");
    require(!collectionCorridor({192,400},{},{warning}),"Warning beam blocks long collection ascent");
    require(collectionCorridor({192,400},{},{fading}),"Despawned beam permits collection");
    require(preferFocus({180,350},{},{{},Objective::Collect},{vertical}),"Nearby beam requests precise movement");
    Laser extending=shortBeam; extending.end=300; extending.maxLength=300; extending.speed=10;
    require(laserClearance(p,p,1.25,{extending},0)>0 && laserClearance(p,p,1.25,{extending},5)<0,"Predict growing beam tip");
    Laser rotating=vertical; rotating.angle=1.370796327f; rotating.turn=.02f;
    require(laserClearance(p,p,1.25,{rotating},0)>0 && laserClearance(p,p,1.25,{rotating},10)<0,"Predict rotating beam sweep");
    require(segmentDistance({0,0},{10,0},{5,0},{20,0})==0,"Collinear overlapping segments");
    require(std::abs(segmentDistance({0,0},{0,0},{3,4},{3,4})-5)<.001f,"Degenerate segment distance");
    LaserTracker tracker;
    std::vector<Laser> tracked{vertical}; tracker.update(tracked,100);
    tracked[0].angle+=.04f; tracked[0].origin.x+=2; tracked[0].timer+=2; tracker.update(tracked,102);
    require(std::abs(tracked[0].turn-.02f)<.001f && tracked[0].drift.x==1,"Track rotation and translation in simulation frames");
    tracker.update(tracked,102);
    require(std::abs(tracked[0].turn-.02f)<.001f,"Duplicate polling retains measured motion");
    tracked[0]=vertical; tracker.update(tracked,103);
    require(tracked[0].turn==0 && tracked[0].drift.x==0,"Timer rollback resets reused laser slot");
    auto resting=intention({170,370},4,{}, {},{},false,true,0);
    require(resting.direction==Direction{},"Idle bottom position does not drift back to center");
    require(intention({170,180},4,{}, {},{},false,true,0).direction.y==1,"Return to lower field after collection");
    require(intention(p,4,{{{192,100},{0,2},0}},enemies,{},false,true,0).objective==Objective::Attack,"Ignore isolated high P while fighting");
    auto rare=pickupTarget(p,4,{{{192,350},{},2},{{260,180},{0,2},3}},&enemies[0],true,128);
    require(rare && rare->x==260 && rare->y>=330,"Bomb outranks nearby P and is intercepted from below");
    auto life=pickupTarget(p,4,{{{220,350},{},3},{{130,300},{0,2},5}},nullptr,true,128);
    require(life && life->x==130,"Life outranks bomb and remains eligible at full power");
    require(intention(p,4,{{{220,350},{},5}},enemies,{},false,true,128).objective==Objective::Collect,"Autoplay collects extra lives during combat");
    require(!pickupTarget(p,4,{{{220,350},{},5,true}},nullptr,true,128),"Already homing rare drop needs no pursuit");
    require(intention(p,4,{{{220,350},{},3}}, {},{-1,0},true,false).direction==Direction{-1,0},"F7 still respects manual control around rare drops");
    SweepControl smallBatch;
    require(!smallBatch.update(0,p,0,{{{192,100},{},0}}, {},{}),"Single P does not cause full-height ascent");
    SweepControl rareSweep;
    require(rareSweep.update(0,p,128,{{{80,100},{},5}},enemies,{}),"High extra life justifies safe collection ascent");
    require(!rareSweep.update(.1,p,128,{{{80,100},{},5},{{200,400},{},3}},enemies,{}),"Low bomb interrupts ascent before it falls offscreen");
    require(intention({192,350},4,{},{{{192,110},{},{24,24},10,false,true,true}}, {},false,true).direction.y==1,"Attack position stays near lower part of field");
    navigation::Forecast emptyForecast({},{});
    auto travel=navigation::choose(p,4,2,1.25,emptyForecast,{300,350},{1,0},false,false);
    require(travel.direction.x==1 && travel.safeFrames==36,"Route planner progresses toward clear target");
    auto forced=navigation::choose(p,4,2,1.25,emptyForecast,{300,350},{1,0},false,true);
    require(forced.focus,"Physical Shift constrains route speed");
    auto blocked=navigation::choose(p,4,2,1.25,navigation::Forecast({{p,{},20}},{}),{300,350},{1,0},false,false);
    require(blocked.safeFrames==0,"Route planner reports immediate overlap");
    auto laserRoute=navigation::choose({180,350},4,2,1.25,navigation::Forecast({},{vertical}),{210,350},{1,0},false,false);
    require(laserRoute.safeFrames==36 && laserRoute.direction.x!=1,"Route respects continuous beam barrier");
    auto preview=navigation::choose({180,350},4,2,1.25,navigation::Forecast({},{vertical}),{210,350},{1,0},false,false,{},.22f,std::nullopt,true);
    require(preview.direction==laserRoute.direction && preview.focus==laserRoute.focus && preview.margin==laserRoute.margin,
            "Preview preserves the planner decision");
    require(preview.path.count==37 && preview.path.safeFrames==36 && preview.path.searched && preview.path.target->x==210,
            "Preview records full selected route and goal");
    Vec firstStep=velocity(preview.direction,preview.focus ? 2:4);
    require(std::abs(preview.path.points[1].x-180-firstStep.x)<.001f && std::abs(preview.path.points[1].y-350-firstStep.y)<.001f,
            "Preview begins with the action sent to the game");
    navigation::Forecast laserForecast({},{vertical}); bool previewSafe=true;
    for(int frame=0;frame<36;++frame) previewSafe=previewSafe && laserForecast.clearance(preview.path.points[frame],preview.path.points[frame+1],frame,2.05f)>0;
    require(previewSafe,"Every displayed safe segment avoids the laser");
    auto trappedPreview=navigation::choose(p,4,2,1.25,navigation::Forecast({{p,{},20}},{}),p,{},false,false,{},.22f,std::nullopt,true);
    require(trappedPreview.path.count==2 && trappedPreview.path.safeFrames==0,"No-route preview marks only the immediate fallback");
    auto projected=prediction::project({374,430},{1,1},4);
    require(projected.count==13 && projected.points[12].x==376 && projected.points[12].y==432 && !projected.searched,
            "Direction forecast stops at playfield boundaries");
    auto map=prediction::mapping(1920,1080); auto origin=map.point({0,0}); auto bottom=map.point({384,448});
    require(origin.x==528 && origin.y==36 && bottom.x==1392 && bottom.y==1044,"Preview aligns with New Classic widescreen playfield");
    Laser upcoming=vertical; upcoming.phase=0; upcoming.startTime=18; upcoming.width=80;
    auto early=navigation::choose(p,4,2,1.25,navigation::Forecast({},{upcoming}),p,{},true,false);
    require(early.safeFrames==36 && early.direction.x!=0,"Leave wide warning before old 12-frame horizon");
    auto edgeRoute=navigation::choose({374,420},4,2,1.25,emptyForecast,{500,500},{1,1},false,false);
    require(edgeRoute.direction!=Direction{1,1},"Planner avoids wedging into field corner");
    SweepSurvival survival;
    require(!survival.update(p,{},1),"Normal field retains lower combat behavior");
    Laser sword=vertical; sword.origin={192,90}; sword.width=80;
    auto flank=survival.update({150,365},{sword},2);
    require(flank && flank->x<100 && flank->y<300,"Wide sword triggers flank escape instead of bottom fighting");
    auto top=survival.update({47,180},{sword},3);
    require(top && top->y==40,"After reaching flank continue above boss");
    require(survival.update({47,50},{},20).has_value(),"Retain escape through brief gaps between laser samples");
    require(!survival.update({47,50},{},60),"Resume normal strategy after laser phase ends");
    LaserTracker staggered;
    std::vector<Laser> staggeredLasers{vertical}; staggered.update(staggeredLasers,10);
    staggeredLasers[0].angle+=.02f; staggeredLasers[0].timer++;
    staggered.update(staggeredLasers,10); staggered.update(staggeredLasers,11);
    require(std::abs(staggeredLasers[0].turn-.02f)<.001f,"Same-frame polling does not erase rotating laser motion");
        controls::Binding binding{VK_F8};
    binding.press(true); binding.tick(true,true);
    require(binding.active,"Custom toggle activates on press");
    binding.press(false); binding.tick(true,true);
    require(binding.active,"Key repeat cannot toggle off");
    binding.tick(false,true); require(binding.active,"Toggle survives release");
    binding.tick(false,false); require(!binding.active && binding.enabled,"Focus loss suspends toggle without forgetting preference");
    binding.press(true); binding.tick(true,true); require(!binding.active,"Second physical press disables toggle");
    binding.hold=true; binding.enabled=true; binding.tick(true,true);
    require(binding.active,"Armed hold runs while physically down");
    binding.tick(false,true); require(!binding.active,"Hold releases immediately");
    binding.bind(VK_LSHIFT,true); binding.tick(true,true); require(!binding.active,"Binding capture does not activate until release");
    binding.tick(false,true); binding.tick(true,true); require(binding.active,"Rebound Shift activates on next hold");
    binding.tick(true,false); require(!binding.active,"Open menu suspends hold");
    controls::Shared input; input.physical[VK_LSHIFT]=true;
    require(input.held(VK_SHIFT) && !input.held(VK_RSHIFT),"Physical shift respects left and right bindings");
    input.bindings[0].enabled=true; input.stop(); require(!input.bindings[0].enabled,"Panic disables armed hold and toggle");
        auto& preferences=controls::state;
    preferences.config=std::filesystem::temp_directory_path()/("scarlet-settings-test-"+std::to_string(GetCurrentProcessId())+".ini");
    struct RemoveTestIni { std::filesystem::path path; ~RemoveTestIni() { std::error_code error; std::filesystem::remove(path,error); } } cleanupIni{preferences.config};
    for(int language=0;language<3;++language) {
        preferences.language=i18n::valid(language);
        preferences.showPrediction=language!=1;
        preferences.scoreMode=language!=1;
        preferences.bindings[0].key=VK_LSHIFT; preferences.bindings[0].hold=true; preferences.bindings[0].enabled=true;
        controls::save(); require(!preferences.saveFailed,"Save preferences to INI");
        preferences.language=i18n::Language::English; preferences.bindings[0].key=VK_F8; preferences.bindings[0].hold=false;
        preferences.showPrediction=!preferences.showPrediction;
        preferences.scoreMode=!preferences.scoreMode;
        controls::readPreferences();
        require(int(preferences.language)==language,"Language persists across settings reload");
        require(preferences.bindings[0].key==VK_LSHIFT && preferences.bindings[0].hold,"Custom Shift hold binding persists");
        require(!preferences.bindings[0].enabled,"Reload never arms automation");
        require(preferences.showPrediction==(language!=1),"Prediction visibility persists across settings reload");
        require(preferences.scoreMode==(language!=1) && !preferences.bindings[3].enabled,
                "Score priority persists without enabling autoplay");
    }
    WritePrivateProfileStringW(L"UI",L"Language",L"99",preferences.config.c_str()); controls::readPreferences();
    require(preferences.language==i18n::Language::English,"Invalid language falls back to English");
    extraTests(require);
    mazeTests(require);
    mazeCorridorTests(require);
    scoringTests(require);
    std::cout << tests << " planner / behavior / toggle tests passed.\n"; return 0;
}

int assistMain(int argc,char** argv) {
    try {
        std::string mode = argc>1 ? argv[1] : "--assist";
        if (mode == "--self-test") return selfTest();
        if (mode == "--verify-layout") {
            if(argc!=3) throw std::runtime_error("Usage: --verify-layout path-to-th06nc.exe");
            const auto code=ExecutableCode::load(std::filesystem::path(argv[2]));
            const auto resolved=resolveLayout(code); printLayout(resolved,std::cout);
            verifyLayoutRelocation(code);
            std::cout<<"All required fields resolved; simulated relink passed for all 18 global addresses.\n";
            return 0;
        }
        if (mode == "--stop") {
            Handle event(OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\TouhouNewClassicDodgeAssistStop"));
            if(event.value) SetEvent(event.value);
            return 0;
        }
        if (mode != "--assist" && mode != "--probe" && mode != "--trace" && mode != "--spell-list") {
            std::cout << "dodge_assist.exe [--assist | --probe [seconds] | --trace [seconds] | --self-test | --spell-list | --verify-layout EXE | --stop]\n"; return 0;
        }
        #ifndef TH_ASSIST_DLL
        SetConsoleCtrlHandler(control,TRUE);
#endif
        DWORD pid = findGame();
        if(!pid && mode=="--trace") {
            std::cout << "Waiting up to five minutes for th06nc.exe; read-only recording.\n" << std::flush;
            for(int attempt=0;attempt<300 && !pid && !stopping;++attempt) {
                std::this_thread::sleep_for(std::chrono::seconds(1)); pid=findGame();
            }
        }
        if (!pid) throw std::runtime_error("Start th06nc.exe and enter a stage first.");
        Game game(pid);
        if(mode=="--spell-list") {
            std::cout << "spellState=" << game.read<int>(game.layout.spellState) << " spellId=" << game.read<int>(game.layout.spellId) << '\n';
            for(size_t i=0;i<game.layout.spellRecordCount;++i) {
                std::array<char,129> name{};
                game.read(game.layout.spellRecords+i*game.layout.spellRecordStride+game.layout.spellRecordName,name.data(),128);
                std::string key=name.data();
                if(key.starts_with("ST_ECLDATA7_")) std::cout << i << " " << key << " = " << game.translate(key) << '\n';
            }
            for(int sub=58;sub<85;++sub) {
                auto key="ST_ECLDATA7_SUB"+std::to_string(sub)+"_0";
                auto translated=game.translate(key);
                if(translated!=key) std::cout << "key " << key << " = " << translated << '\n';
            }
            return 0;
        }
        const bool trace = mode == "--trace";
        const bool probe = mode == "--probe" || trace;
        Handle singleton(probe ? nullptr : CreateMutexW(nullptr,FALSE,L"Local\\TouhouNewClassicDodgeAssist"));
        if(!probe && (!singleton.value || GetLastError()==ERROR_ALREADY_EXISTS))
            throw std::runtime_error("An assist instance is already running, or its lock is unavailable. F9 stops the running instance.");
        Handle stopEvent(probe ? nullptr:CreateEventW(nullptr,TRUE,FALSE,L"Local\\TouhouNewClassicDodgeAssistStop"));
        if(!probe && !stopEvent.value) throw std::runtime_error("Cannot create stop event.");
        const int seconds = probe ? (argc>2 ? std::stoi(argv[2]) : 15) : 0;
        if (probe && (seconds<1 || seconds>3600)) throw std::runtime_error("Probe duration must be 1..3600 seconds.");
        std::cout << (probe ? "READ-ONLY probe; no keyboard injection.\n" :
            "F8: dodge. F7: red P collection. F6: autobomb. F5: autoplay (power + fire + focus + bomb).\nAll start OFF. F9: quit.\n");
        // No keyboard hook or synthetic input is installed in probe mode.
        std::unique_ptr<Keyboard> keyboard;
        if(!probe) keyboard = std::make_unique<Keyboard>();
        const auto start = Clock::now();
        auto report = start;
        Toggle dodge{false,down(VK_F8)}, collect{false,down(VK_F7)}, autoplay{false,down(VK_F5)}, autobomb{false,down(VK_F6)};
        bool scoreMode=false;
        SweepControl sweep;
        SweepSurvival survival;
        ExtraStrategy extra;
        navigation::Decision route;
        uint32_t plannedFrame=std::numeric_limits<uint32_t>::max();
        Direction previousMove{};
        BombControl bomb;
        LaserTracker laserTracker;
        uint32_t tracedFrame=std::numeric_limits<uint32_t>::max();
        #ifdef TH_ASSIST_DLL
        controls::status("Gotowy",true);
#endif
        while(!stopping && game.live()) {
#ifndef TH_ASSIST_DLL
            if(down(VK_F9)) break;
#endif
            if(stopEvent.value && WaitForSingleObject(stopEvent.value,0)==WAIT_OBJECT_0) break;
            auto tick = Clock::now();
            if(probe && std::chrono::duration<double>(tick-start).count()>=seconds) break;
            if(keyboard) keyboard->pump();
            if(keyboard) {
                // Update each key, even if another toggle changed during this tick.
                #ifdef TH_ASSIST_DLL
                auto enabled=controls::tick(game.foreground());
                bool requestedScore=false;
                {std::lock_guard lock(controls::state.mutex);requestedScore=controls::state.scoreMode;}
                bool changed=dodge.enabled!=enabled[0] || collect.enabled!=enabled[1] || autobomb.enabled!=enabled[2] || autoplay.enabled!=enabled[3] || scoreMode!=requestedScore;
                scoreMode=requestedScore;
                dodge.enabled=enabled[0]; collect.enabled=enabled[1]; autobomb.enabled=enabled[2]; autoplay.enabled=enabled[3];
#else
                bool changed=dodge.update(down(VK_F8));
                changed=collect.update(down(VK_F7)) || changed;
                changed=autoplay.update(down(VK_F5)) || changed;
                changed=autobomb.update(down(VK_F6)) || changed;
#endif
                if(changed) {
                    sweep.cancel();
                    plannedFrame=std::numeric_limits<uint32_t>::max();
                    keyboard->release(game.foreground());
                    if(!autoplay.enabled) keyboard->shoot(false,game.foreground());
                    std::cout << "Dodge " << (dodge.enabled ? "ON" : "OFF")
                        << " | Collect " << (collect.enabled ? "ON" : "OFF")
                        << " | Autobomb " << (autobomb.enabled ? "ON" : "OFF")
                        << " | Autoplay " << (autoplay.enabled ? "ON" : "OFF") << '\n' << std::flush;
                }
            }
            State s;
            try { s=sample(game,down(VK_SHIFT)); }
            catch(const std::exception& error) {
                if(!trace) throw;
                std::cerr << "Skipped inconsistent read: " << error.what() << '\n';
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }
            laserTracker.update(s.lasers,s.simulationFrame);
            if(trace && tracedFrame!=s.simulationFrame) {
                tracedFrame=s.simulationFrame;
                auto target=extra.update(s.spellKey,s.simulationFrame,s.player,s.bullets,s.lasers,s.enemies);
                std::cout << "T " << s.simulationFrame << ' ' << s.scene << ' ' << s.spell << ' ' << s.playerState
                    << ' ' << s.player.x << ' ' << s.player.y << ' ' << s.bombs << ' ' << extra.phase
                    << ' ' << (target ? target->x:0) << ' ' << (target ? target->y:0) << '\n';
                for(const auto& l:s.lasers) std::cout << "L " << l.slot << ' ' << l.phase << ' ' << l.timer << ' '
                    << l.origin.x << ' ' << l.origin.y << ' ' << l.angle << ' ' << l.turn << ' ' << l.width
                    << ' ' << l.start << ' ' << l.end << ' ' << l.startTime << ' ' << l.duration
                    << ' ' << l.maxLength << ' ' << l.speed << ' ' << l.drift.x << ' ' << l.drift.y << '\n';
                for(const auto& e:s.enemies) std::cout << "E " << e.p.x << ' ' << e.p.y << ' ' << e.life
                    << ' ' << e.boss << ' ' << e.collidable << ' ' << e.damageable << ' ' << e.size.x << ' ' << e.size.y
                    << ' ' << e.v.x << ' ' << e.v.y << '\n';
                for(const auto& b:s.bullets)
                    std::cout << "B " << b.p.x << ' ' << b.p.y << ' ' << b.v.x << ' ' << b.v.y << ' ' << b.radius << ' ' << b.age << '\n';
                for(const auto& item:s.items)
                    std::cout << "I " << item.type << ' ' << item.p.x << ' ' << item.p.y << ' ' << item.v.x << ' ' << item.v.y << ' ' << item.homing << '\n';
                std::cout << "S " << s.fast << ' ' << s.slow << ' ' << s.radius << ' ' << s.power << ' ' << s.bombActive
                    << ' ' << s.active << ' ' << s.dialogue << ' ' << s.spellKey << '\n';
                std::cout << std::flush;
            }
            // A clear field still needs collection / attack. Pause is read directly from the game.
            bool active = s.active;
            Direction d{};
            bool correction = false;
            bool focused=false;
            Intent intent{};
            if(keyboard) {
                bool foreground=game.foreground();
                bool playing=active && foreground;
#ifdef TH_ASSIST_DLL
                playing=playing && !controls::state.menu;
                { std::lock_guard lock(controls::state.mutex); auto& ui=controls::state; ui.playing=playing; ui.bullets=int(s.bullets.size()); ui.lasers=int(s.lasers.size()); ui.power=s.power; ui.bombs=s.bombs; ui.pattern=extra.profile ? std::string(extra.profile->name):"--"; }
#endif
                bool showPrediction=false;
#ifdef TH_ASSIST_DLL
                { std::lock_guard lock(controls::state.mutex);
                  showPrediction=controls::state.showPrediction;
                  if(!showPrediction || !playing || !(dodge.enabled || collect.enabled || (autoplay.enabled && !s.dialogue))) controls::state.prediction.count=0;
                }
#endif
                auto threats=obstacles(s.bullets,s.enemies);
                double now=std::chrono::duration<double>(tick-start).count();
                bool autoMove=autoplay.enabled && !s.dialogue;
                if(!autoMove || !playing) {
                    sweep.cancel(); survival={}; plannedFrame=std::numeric_limits<uint32_t>::max(); previousMove={};
                    // Pausing must not reset a card's route to its opening.
                    if(!autoMove || s.scene!=2) extra.reset();
                }
                if(playing && (dodge.enabled || collect.enabled || autoMove)) {
                    auto user = keyboard->user();
                    intent=intention(s.player,s.fast,s.items,s.enemies,user,collect.enabled,autoMove,s.power,scoreMode);
                    auto escapeGoal=autoMove ? survival.update(s.player,s.lasers,s.simulationFrame):std::nullopt;
                    auto cardGoal=autoMove ? extra.update(s.spellKey,s.simulationFrame,s.player,s.bullets,s.lasers,s.enemies):std::nullopt;
                    if(cardGoal) {
                        sweep.cancel();
                        intent={toward(s.player,*cardGoal,s.fast),Objective::Extra,*cardGoal};
                    } else if(escapeGoal) {
                        sweep.cancel();
                        intent={toward(s.player,*escapeGoal,s.fast),Objective::Survive,*escapeGoal};
                    } else if(autoMove && sweep.update(now,s.player,s.power,s.items,s.enemies,threats,s.lasers,scoreMode,
                        keyboard->physicalFocus() ? s.slow:s.fast,s.radius))
                        intent={toward(s.player,sweep.goal,s.fast),Objective::Sweep,sweep.goal};
                    focused=keyboard->physicalFocus() || (autoMove && preferFocus(s.player,threats,intent,s.lasers));
                    s.speed=focused ? s.slow:s.fast;
                    if(autoMove) {
                        if(plannedFrame!=s.simulationFrame || (keyboard->physicalFocus() && !route.focus) || (showPrediction && !route.path.count)) {
                            Vec goal=intent.target.value_or(Vec{s.player.x+intent.direction.x*80.f,s.player.y+intent.direction.y*80.f});
                            navigation::Forecast forecast(threats,s.lasers,extra.card==ExtraCard::Laevateinn);
                            route=navigation::choose(s.player,s.fast,s.slow,s.radius,forecast,goal,intent.direction,focused,keyboard->physicalFocus(),previousMove,
                                extra.card==ExtraCard::Maze ? .8f:.22f,extra.orbitLane(),showPrediction);
                            plannedFrame=s.simulationFrame;
                        }
                        d=route.direction; focused=route.focus; s.speed=focused ? s.slow:s.fast;
                    } else d=plan(s.player,s.speed,s.radius,threats,intent.direction,s.lasers);
                    if(!autoMove && focused && !keyboard->physicalFocus() && clearance(s.player,velocity(d,s.slow),s.radius,threats,s.lasers)<0) {
                        auto fast=plan(s.player,s.fast,s.radius,threats,intent.direction,s.lasers);
                        if(clearance(s.player,velocity(fast,s.fast),s.radius,threats,s.lasers)>clearance(s.player,velocity(d,s.slow),s.radius,threats,s.lasers)) {
                            focused=false; s.speed=s.fast; d=fast;
                        }
                    }
                    correction = autoMove || d != user;
                    previousMove=d;
#ifdef TH_ASSIST_DLL
                    if(showPrediction) {
                        auto path=autoMove ? route.path:prediction::project(s.player,d,s.speed);
                        if(!autoMove) { path.target=intent.target; path.focus=focused; }
                        std::lock_guard lock(controls::state.mutex);
                        controls::state.prediction=path; controls::state.predictionTime=GetTickCount64();
                    }
#endif
                    if(correction && game.foreground()) keyboard->correct(d);
                    else keyboard->release(game.foreground());
                } else keyboard->release(game.foreground());
                keyboard->focus(autoMove && playing && focused,game.foreground());
                keyboard->shoot(autoplay.enabled && playing,game.foreground());
                bool eligible=bombEligible(playing,autobomb.enabled || autoplay.enabled,s.playerState,
                    s.bombActive,s.dialogue,s.grace);
                bool imminentTrail=autoMove && extra.card==ExtraCard::Laevateinn && plannedFrame==s.simulationFrame && route.safeFrames<6;
                bool trapped=eligible && (imminentTrail || !escapeAvailable(s.player,s.radius,threats,
                    keyboard->physicalFocus() ? s.slow:s.fast,s.slow,s.lasers));
                keyboard->bomb(bomb.update(now,eligible,trapped),game.foreground());
            }
            if(tick>=report) {
                int redCount=0, bossLife=0;
                for(const auto& item:s.items) if(redPower(item.type)) ++redCount;
                for(const auto& enemy:s.enemies) if(enemy.boss) bossLife+=enemy.life;
                std::cout << "F8=" << dodge.enabled << " F7=" << collect.enabled << " F6=" << autobomb.enabled << " F5=" << autoplay.enabled
                    << " scorePriority=" << scoreMode
                    << " objective=" << objectiveName(intent.objective) << " scene=" << s.scene << " active=" << s.active << " player=(" << s.player.x << ',' << s.player.y
                    << ") state=" << s.playerState << " speed=" << s.speed << " radius=" << s.radius
                    << " bullets=" << s.bullets.size() << " lasers=" << s.lasers.size() << " items=" << s.items.size() << " enemies=" << s.enemies.size()
                    << " redP=" << redCount << " bossHP=" << bossLife
                    << " power=" << s.power << "/128 bombs=" << s.bombs << " bombActive=" << s.bombActive
                    << " grace=" << s.grace << " dialogue=" << s.dialogue << " focus=" << focused
                    << " routeFrames=" << route.safeFrames
                    << " spell=" << s.spell << " spellState=" << s.spellState
                    << " pattern=" << (extra.profile ? extra.profile->name:std::string_view("none"))
                    << " patternPhase=" << extra.phase
                    << " pivot=" << extra.boss.x << ',' << extra.boss.y << " pivotKnown=" << extra.bossKnown << " orbit=" << extra.orbit
                    << " correction=" << correction << " dir=" << d.x << ',' << d.y << '\n' << std::flush;
                if(probe) {
                    if(!s.spellKey.empty()) std::cout << "  spellKey=" << s.spellKey << '\n';
                    for(const auto& item:s.items) if(redPower(item.type)) {
                        std::cout << "  red P type=" << item.type << " pos=" << item.p.x << ',' << item.p.y << " velocity=" << item.v.x << ',' << item.v.y << '\n';
                        break;
                    }
                    if(!s.enemies.empty()) {
                        const auto& e=s.enemies.front();
                        std::cout << "  enemy pos=" << e.p.x << ',' << e.p.y << " hp=" << e.life << " boss=" << e.boss
                            << " damageable=" << e.damageable << " size=" << e.size.x << ',' << e.size.y << '\n';
                    }
                }
                if(probe) for(const auto& l:s.lasers) {
                    auto [a,b]=laserSegment(l,0);
                    std::cout << "  laser slot=" << l.slot << " phase=" << l.phase << " timer=" << l.timer
                        << " startTime=" << l.startTime << " duration=" << l.duration << " width=" << l.width
                        << " from=" << a.x << ',' << a.y << " to=" << b.x << ',' << b.y
                        << " speed=" << l.speed << " turn=" << l.turn << '\n';
                }
                if(probe && !s.bullets.empty()) {
                    const auto& b=s.bullets.front();
                    std::cout << "  first bullet pos=" << b.p.x << ',' << b.p.y << " velocity=" << b.v.x << ',' << b.v.y << " radius=" << b.radius << '\n';
                }
                report=tick+std::chrono::seconds(1);
            }
            std::this_thread::sleep_until(tick+std::chrono::milliseconds(8));
        }
        if(keyboard) { keyboard->release(game.foreground()); keyboard->shoot(false,game.foreground()); keyboard->focus(false,game.foreground()); keyboard->bomb(false,game.foreground()); }
        return 0;
    } catch(const std::exception& error) {
#ifdef TH_ASSIST_DLL
        controls::status(error.what());
#endif
        std::cerr << "ERROR: " << error.what() << '\n'; return 1; }
}




#if !defined(TH_ASSIST_DLL) && !defined(TH_ASSIST_LIBRARY)
int main(int argc,char** argv) { return assistMain(argc,argv); }
#endif





