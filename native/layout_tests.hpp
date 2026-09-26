#pragma once
#include "game_layout.hpp"

template<class Require> void layoutPrimitiveTests(Require require) {
    ExecutableCode code;
    code.bytes={0x48,0x8d,0x05,0xf9,0x0f,0,0,0x90};
    code.rva=0x1000; code.imageSize=0x4000; code.writable={{0x2000,0x1000}};
    auto rejects=[](auto operation) { try { operation(); } catch(const std::exception&) { return true; } return false; };
    require(code.find("48 8D 05 ?? ?? ?? ??","test")==0,"Wildcard code signature finds a relocated reference");
    require(code.rip(0,3,7,4)==0x2000,"Decode signed RIP-relative operand");
    require(code.data(0x2ffc,4)==0x2ffc,"A field may end exactly at the data section boundary");
    require(rejects([&]{code.data(0x2ffd,4);}),"Reject fields spanning outside writable data");
    require(rejects([&]{code.data(0x1000,4);}),"Reject resolved fields in executable code");
    require(rejects([&]{code.find("AA BB","missing");}),"Missing signatures disable unsupported layouts");
    require(rejects([&]{code.value<uint32_t>(6);}),"Reject truncated instruction operands");
    require(rejects([&]{ExecutableCode::pattern("?? ??");}),"Reject signatures containing only wildcards");
    const auto copy=code.bytes; code.bytes.insert(code.bytes.end(),copy.begin(),copy.end());
    require(rejects([&]{code.find("48 8D 05 ?? ?? ?? ??","duplicate");}),"Ambiguous signatures cannot select the first arbitrary address");
    code.bytes=copy; const int32_t negative=-0x1007;
    std::memcpy(code.bytes.data()+3,&negative,4); code.rva=0x3000;
    require(code.rip(0,3,7,4)==0x2000,"References support negative displacements");
}

// Exercise every global address after a simulated relink. Only a private copy of
// the executable's code is changed; nothing is written to the game or its files.
inline void verifyLayoutRelocation(const ExecutableCode& original) {
    const auto before=resolveLayout(original);
    auto relocated=original;
    constexpr uintptr_t delta=0x3000;
    relocated.rva+=delta; relocated.imageSize+=delta;
    for(auto& range:relocated.writable) range.begin+=delta;
    const auto record=relocated.find("48 8D 0C 40 48 C1 E1 07 48 8D B6 ?? ?? ?? ?? 48 03 F1 48 8D 4E 18","spell record relocation");
    const auto movedTable=uint32_t(before.spellRecords+delta);
    std::memcpy(relocated.bytes.data()+record+11,&movedTable,4);
    const auto after=resolveLayout(relocated);
    const uintptr_t GameLayout::* fields[]={&GameLayout::player,&GameLayout::bullets,&GameLayout::simulationFrame,
        &GameLayout::lasers,&GameLayout::items,&GameLayout::enemies,&GameLayout::power,&GameLayout::bombs,
        &GameLayout::gui,&GameLayout::scene,&GameLayout::pause,&GameLayout::retry,&GameLayout::menu,
        &GameLayout::timeStop,&GameLayout::spellState,&GameLayout::spellId,&GameLayout::spellRecords,&GameLayout::translations};
    for(auto field:fields) if(after.*field!=before.*field+delta)
        throw std::runtime_error("Layout relocation regression: a global address stayed fixed.");
}
