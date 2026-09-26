#pragma once
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "offsets.hpp"
// A layout is accepted only when its executable signatures resolve uniquely.
// The original Steam build still uses the verified fixed profile; newer builds
// use the code references below and fail closed if their structure changes.
struct GameLayout {
    uintptr_t spellState{},spellId{},spellRecords{},power{},bombs{},gui{},translations{};
    size_t spellRecordStride{},spellRecordName{},spellRecordCount{};
    size_t bombActive{},bombGrace{},dialogue{};
    uintptr_t player{}; size_t playerXY{},playerRadius{},speed{},focusSpeed{},playerState{};
    uintptr_t bullets{}; size_t bulletStride{},bulletCount{},bulletXY{},bulletVelocity{},bulletState{},bulletSize{},bulletAge{};
    uintptr_t lasers{},simulationFrame{}; size_t laserStride{},laserCount{},laserWidth{},laserTimer{},laserStart{},laserEnd{},laserAngle{},laserDuration{},laserSpeed{},laserStartTime{},laserInUse{},laserOrigin{},laserLength{},laserPhase{};
    uintptr_t scene{},pause{},retry{},menu{},timeStop{};
    uintptr_t items{}; size_t itemStride{},itemCount{},itemActive{},itemXY{},itemVelocity{},itemType{};
    uintptr_t enemies{}; size_t enemyStride{},enemyCount{},enemyXY{},enemyFlags{},enemyLife{},enemySize{},enemyVelocity{};
    bool signatureResolved=false;
};

GameLayout legacyLayout() {
    GameLayout l;
    l.spellState=offsets::spellState; l.spellId=offsets::spellId; l.spellRecords=offsets::spellRecords;
    l.spellRecordStride=offsets::spellRecordStride; l.spellRecordName=offsets::spellRecordName; l.spellRecordCount=offsets::spellRecordCount;
    l.power=offsets::power; l.bombs=offsets::bombs; l.gui=offsets::gui; l.bombActive=offsets::bombActive; l.bombGrace=offsets::bombGrace; l.dialogue=offsets::dialogue;
    l.translations=0xa6ec28;
    l.player=offsets::player; l.playerXY=offsets::playerXY; l.playerRadius=offsets::playerRadius; l.speed=offsets::speed; l.focusSpeed=offsets::focusSpeed; l.playerState=offsets::playerState;
    l.bullets=offsets::bullets; l.bulletStride=offsets::bulletStride; l.bulletCount=offsets::bulletCount; l.bulletXY=offsets::bulletXY; l.bulletVelocity=offsets::bulletVelocity; l.bulletState=offsets::bulletState; l.bulletSize=offsets::bulletSize; l.bulletAge=offsets::bulletAge;
    l.lasers=offsets::lasers; l.simulationFrame=offsets::simulationFrame; l.laserStride=offsets::laserStride; l.laserCount=offsets::laserCount; l.laserWidth=offsets::laserWidth; l.laserTimer=offsets::laserTimer; l.laserStart=offsets::laserStart; l.laserEnd=offsets::laserEnd; l.laserAngle=offsets::laserAngle; l.laserDuration=offsets::laserDuration; l.laserSpeed=offsets::laserSpeed; l.laserStartTime=offsets::laserStartTime; l.laserInUse=offsets::laserInUse; l.laserOrigin=offsets::laserOrigin; l.laserLength=offsets::laserLength; l.laserPhase=offsets::laserPhase;
    l.scene=offsets::scene; l.pause=offsets::pause; l.retry=offsets::retry; l.menu=offsets::menu; l.timeStop=offsets::timeStop;
    l.items=offsets::items; l.itemStride=offsets::itemStride; l.itemCount=offsets::itemCount; l.itemActive=offsets::itemActive; l.itemXY=offsets::itemXY; l.itemVelocity=offsets::itemVelocity; l.itemType=offsets::itemType;
    l.enemies=offsets::enemies; l.enemyStride=offsets::enemyStride; l.enemyCount=offsets::enemyCount; l.enemyXY=offsets::enemyXY; l.enemyFlags=offsets::enemyFlags; l.enemyLife=offsets::enemyLife; l.enemySize=offsets::enemySize; l.enemyVelocity=offsets::enemyVelocity;
    return l;
}

// Resolve code references, never search mutable game data for plausible floats.
// A changed object layout must match the structural checks below before use.
struct ExecutableCode {
    struct Range { uintptr_t begin; size_t size; };
    std::vector<unsigned char> bytes;
    std::vector<Range> writable;
    uintptr_t rva=0;
    size_t imageSize=0;
    uint32_t timestamp=0;

    static ExecutableCode load(const std::filesystem::path& path) {
        std::ifstream stream(path,std::ios::binary|std::ios::ate);
        if(!stream) throw std::runtime_error("Cannot read game executable.");
        const auto length=stream.tellg();
        if(length<64 || length>256*1024*1024) throw std::runtime_error("Invalid executable size.");
        std::vector<unsigned char> file(static_cast<size_t>(length));
        stream.seekg(0); stream.read(reinterpret_cast<char*>(file.data()),length);
        if(!stream) throw std::runtime_error("Incomplete executable read.");
        auto get=[&]<class T>(size_t offset) {
            if(offset>file.size() || sizeof(T)>file.size()-offset) throw std::runtime_error("Truncated PE header.");
            T value{}; std::memcpy(&value,file.data()+offset,sizeof(value)); return value;
        };
        const auto dos=get.template operator()<IMAGE_DOS_HEADER>(0);
        if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<64) throw std::runtime_error("Invalid DOS header.");
        const auto nt=get.template operator()<IMAGE_NT_HEADERS64>(size_t(dos.e_lfanew));
        if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
           nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.NumberOfSections==0 ||
           nt.FileHeader.NumberOfSections>96 || nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64) ||
           nt.OptionalHeader.SizeOfImage<0x1000 || nt.OptionalHeader.SizeOfImage>512*1024*1024)
            throw std::runtime_error("Unsupported PE image; expected Windows x64.");
        ExecutableCode result; result.imageSize=nt.OptionalHeader.SizeOfImage; result.timestamp=nt.FileHeader.TimeDateStamp;
        const size_t sectionTable=size_t(dos.e_lfanew)+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
            const auto section=get.template operator()<IMAGE_SECTION_HEADER>(sectionTable+i*sizeof(IMAGE_SECTION_HEADER));
            const size_t span=std::max(section.Misc.VirtualSize,section.SizeOfRawData);
            if(section.VirtualAddress>result.imageSize || span>result.imageSize-section.VirtualAddress ||
               section.PointerToRawData>file.size() || section.SizeOfRawData>file.size()-section.PointerToRawData)
                throw std::runtime_error("Invalid PE section range.");
            if(section.Characteristics&IMAGE_SCN_MEM_WRITE) result.writable.push_back({section.VirtualAddress,span});
            if(std::memcmp(section.Name,".text\0",6)==0 && (section.Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
                if(!result.bytes.empty()) throw std::runtime_error("Duplicate executable section.");
                result.rva=section.VirtualAddress;
                const size_t count=std::min(section.Misc.VirtualSize,section.SizeOfRawData);
                result.bytes.assign(file.begin()+section.PointerToRawData,file.begin()+section.PointerToRawData+count);
            }
        }
        if(result.bytes.size()<0x10000 || result.writable.empty()) throw std::runtime_error("Required PE sections missing.");
        return result;
    }
    template<class T> T value(size_t offset) const {
        if(offset>bytes.size() || sizeof(T)>bytes.size()-offset) throw std::runtime_error("Code operand outside executable section.");
        T result{}; std::memcpy(&result,bytes.data()+offset,sizeof(result)); return result;
    }
    static std::vector<int> pattern(const char* text) {
        std::istringstream input(text); std::string token; std::vector<int> result;
        while(input>>token) {
            if(token=="??") result.push_back(-1);
            else {
                size_t used=0; const int v=std::stoi(token,&used,16);
                if(token.size()!=2 || used!=2 || v<0 || v>255) throw std::runtime_error("Invalid signature token.");
                result.push_back(v);
            }
        }
        if(result.empty() || std::all_of(result.begin(),result.end(),[](int v){return v<0;}))
            throw std::runtime_error("Empty signature.");
        return result;
    }
    size_t find(const char* text,const char* name,bool unique=true) const {
        const auto p=pattern(text); size_t count=0,found=0;
        if(p.size()<=bytes.size()) for(size_t at=0;at<=bytes.size()-p.size();++at) {
            bool same=true;
            for(size_t j=0;j<p.size();++j) if(p[j]>=0 && bytes[at+j]!=p[j]) {same=false;break;}
            if(same) {found=at;++count;if(!unique) return found;}
        }
        if(count!=1) throw std::runtime_error(std::string("Unsupported game layout: ")+name+" (matches="+std::to_string(count)+"). Input disabled.");
        return found;
    }
    uintptr_t data(uintptr_t address,size_t length=1) const {
        for(const auto& section:writable) if(address>=section.begin && address-section.begin<=section.size &&
            length<=section.size-(address-section.begin)) return address;
        throw std::runtime_error("Resolved field is outside writable game data.");
    }
    uintptr_t rip(size_t at,size_t displacement,size_t instructionSize,size_t length=1) const {
        const int64_t result=int64_t(rva)+int64_t(at)+int64_t(instructionSize)+value<int32_t>(at+displacement);
        if(result<0) throw std::runtime_error("Invalid relative data address.");
        return data(uintptr_t(result),length);
    }
};

inline GameLayout resolveLayout(const ExecutableCode& code) {
    auto l=legacyLayout();
    const auto player=code.find("48 8D 1D ?? ?? ?? ?? 33 D2 48 8B CB 41 B8 54 78 00 00","player object");
    l.player=code.rip(player,3,7,0xa2b8);
    code.find("C7 83 30 77 00 00 00 00 40 43 C7 83 34 77 00 00 00 00 C0 43","player position layout");
    code.find("48 C7 83 4C 77 00 00 00 00 A0 3F","player hitbox layout");
    code.find("F3 0F 10 8B 60 78 00 00 F3 0F 10 93 64 78 00 00","player speed layout");
    const auto bullet=code.find("48 8D 1D ?? ?? ?? ?? 33 D2 48 8B CB 41 B8 10 50 0F 00","bullet manager");
    const auto manager=code.rip(bullet,3,7,0xf5010);
    l.bullets=manager+8; l.simulationFrame=manager+4;
    code.find("48 81 C3 20 06 00 00 48 81 C5 20 06 00 00 48 81 C6 20 06 00 00 41 B8 EF FF 00 00 41 B9 DF FF 00 00 41 81 FE 80 02 00 00","bullet stride and count");
    const auto laser=code.find("48 8D 1D ?? ?? ?? ?? 80 BB 74 02 00 00 00 74 1C FF C0 48 81 C3 98 02 00 00 83 F8 40 7C","laser pool");
    l.lasers=code.rip(laser,3,7,l.laserStride*l.laserCount);
    const auto items=code.find("48 8D 0D ?? ?? ?? ?? 0F 29 70 B8 44 0F B6 E2 0F 29 78 A8 48 8D 15 ?? ?? ?? ??","item pool");
    l.items=code.rip(items,3,7,l.itemStride*l.itemCount);
    if(code.rip(items+19,3,7)!=l.items) throw std::runtime_error("Item pool references disagree.");
    const auto enemies=code.find("48 8D 3D ?? ?? ?? ?? BB 01 01 00 00 48 8B CF E8 ?? ?? ?? ?? 48 81 C7 B0 10 00 00 48 83 EB 01 75","enemy pool");
    l.enemies=code.rip(enemies,3,7,l.enemyStride*l.enemyCount);
    code.find("48 81 C3 B0 10 00 00 3D 00 01 00 00 7C","enemy pool count",false);

    const auto power=code.find("0F B7 05 ?? ?? ?? ?? 66 83 F8 10 77 04 8B C6 EB 08 B9 F0 FF 00 00 66 03 C1","power field");
    l.power=code.rip(power,3,7,2);
    const auto bomb=code.find("48 8B 05 ?? ?? ?? ?? 39 B0 B0 36 00 00 0F 8D ?? ?? ?? ?? 39 B1 3C 77 00 00 0F 84 ?? ?? ?? ?? 0F B6 0D ?? ?? ?? ?? 84 C9","bomb and dialogue fields");
    l.gui=code.rip(bomb,3,7,8); l.bombs=code.rip(bomb+31,3,7);
    code.find("40 38 B1 C8 9E 00 00 74 ?? 48 8B 81 D0 9E 00 00 FF D0","active bomb field");
    code.find("80 BF 98 78 00 00 02 75","player state field",false);

    const auto flags=code.find("83 3D ?? ?? ?? ?? 02 0F 28 D8 75 ?? 80 3D ?? ?? ?? ?? 00 75 ?? 80 3D ?? ?? ?? ?? 00 75 ?? 80 3D ?? ?? ?? ?? 00 75","scene and pause flags");
    l.scene=code.rip(flags,2,7,4); l.pause=code.rip(flags+12,2,7,10);
    l.retry=code.rip(flags+21,2,7); l.menu=code.rip(flags+30,2,7);
    const auto stopped=code.find("80 3D ?? ?? ?? ?? 00 48 8D 59 08 4C 8B F9 0F 85","simulation stop flag");
    l.timeStop=code.rip(stopped,2,7);
    if(l.retry!=l.pause+1 || l.menu!=l.pause+6 || l.timeStop!=l.pause+9)
        throw std::runtime_error("Game pause structure changed; input disabled.");

    const auto spell=code.find("44 89 3D ?? ?? ?? ?? 41 0F BF 46 0E 89 05 ?? ?? ?? ?? 3D 86 00 00 00","spell state and index");
    l.spellState=code.rip(spell,3,7,4); l.spellId=code.rip(spell+12,2,6,4);
    const auto records=code.find("48 8D 0C 40 48 C1 E1 07 48 8D B6 ?? ?? ?? ?? 48 03 F1 48 8D 4E 18","spell record table");
    l.spellRecords=code.data(code.value<uint32_t>(records+11),l.spellRecordCount*l.spellRecordStride);
    const auto translation=code.find("4C 8B 3D ?? ?? ?? ?? 49 8D 5E 10 48 85 DB 75 ?? 4C 8D 3D","spell translations");
    l.translations=code.rip(translation,3,7,8);
    l.signatureResolved=true;
    return l;
}

inline void printLayout(const GameLayout& l,std::ostream& out) {
    out<<std::hex<<"player=0x"<<l.player<<" bullets=0x"<<l.bullets<<" frame=0x"<<l.simulationFrame
       <<" lasers=0x"<<l.lasers<<" items=0x"<<l.items<<" enemies=0x"<<l.enemies
       <<"\npower=0x"<<l.power<<" bombs=0x"<<l.bombs<<" gui=0x"<<l.gui
       <<" scene=0x"<<l.scene<<" pause=0x"<<l.pause<<" timeStop=0x"<<l.timeStop
       <<"\nspellState=0x"<<l.spellState<<" spellId=0x"<<l.spellId<<" records=0x"<<l.spellRecords
       <<" translations=0x"<<l.translations<<std::dec<<'\n';
}
