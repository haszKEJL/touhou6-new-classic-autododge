#pragma once
#include <cstddef>
#include <cstdint>
// Legacy profile derived from New Classic 1.03 x64, not the 2002 game.
// Newer builds resolve executable code references in game_layout.hpp.
// Every address below is an RVA, relative to the loaded th06nc.exe module.
namespace offsets {
inline constexpr uintptr_t spellState=0xbadf60, spellId=0xbadf68, spellRecords=0x4f27c8;
inline constexpr size_t spellRecordStride=0x180, spellRecordName=0x18, spellRecordCount=0x86;
inline constexpr uintptr_t power = 0x4f1e88, bombs = 0x4ff0f1, gui = 0xa6ec08;
inline constexpr size_t bombActive = 0x9ec8, bombGrace = 0x773c, dialogue = 0x36b0;
inline constexpr char sha256[] = "07850c8c6e469c0e82c13423e6d0d096a88d693455bdacacbb44c0aa3bcce473";
inline constexpr uintptr_t player = 0x4ff3a0;
inline constexpr size_t playerXY = 0x7730, playerRadius = 0x774c;
inline constexpr size_t speed = 0x7860, focusSpeed = 0x7864, playerState = 0x7898;
inline constexpr uintptr_t bullets = 0x3ec2a8;
inline constexpr size_t bulletStride = 0x620, bulletCount = 0x280;
inline constexpr size_t bulletXY = 0x30, bulletVelocity = 0x08, bulletState = 0x44;
inline constexpr size_t bulletSize = 0x5f4, bulletAge = 0x2c;
inline constexpr uintptr_t lasers=0x4e12b8, simulationFrame=0x3ec2a4;
inline constexpr size_t laserStride=0x298, laserCount=64;
inline constexpr size_t laserWidth=0x04, laserTimer=0x0c, laserStart=0x10, laserEnd=0x14;
inline constexpr size_t laserAngle=0x260, laserDuration=0x268, laserSpeed=0x26c, laserStartTime=0x270;
inline constexpr size_t laserInUse=0x274, laserOrigin=0x278, laserLength=0x288, laserPhase=0x290;
inline constexpr uintptr_t scene = 0xc21d9c;
inline constexpr uintptr_t pause = 0x4f27b0, retry = 0x4f27b1, menu = 0x4f27b6, timeStop = 0x4f27b9;
inline constexpr uintptr_t items = 0xbaf0d8;
inline constexpr size_t itemStride = 0x160, itemCount = 0x400;
inline constexpr size_t itemActive = 0, itemXY = 0x10, itemVelocity = 0x1c, itemType = 0x34;
inline constexpr uintptr_t enemies = 0xaa1e98;
inline constexpr size_t enemyStride = 0x10b0, enemyCount = 0x100;
inline constexpr size_t enemyXY = 0xb0, enemyFlags = 0xbc, enemyLife = 0x234;
inline constexpr size_t enemySize = 0x2c0, enemyVelocity = 0x1084;
}
