@echo off
setlocal
cd /d "%~dp0"
set "CXX=g++"
set "CC=gcc"
where g++ >nul 2>nul
if errorlevel 1 (
 set "CXX=C:\msys64\ucrt64\bin\g++.exe"
 set "CC=C:\msys64\ucrt64\bin\gcc.exe"
)
if not defined ASSIST_OUT set "ASSIST_OUT=bin"
if not exist "%ASSIST_OUT%" mkdir "%ASSIST_OUT%"
if not exist build mkdir build
"%CXX%" -std=c++20 -O2 -Wall -Wextra -Wpedantic -static -static-libgcc -static-libstdc++ native\main.cpp -o "%ASSIST_OUT%\dodge_assist.exe" -lbcrypt -luser32
if errorlevel 1 exit /b 1
"%ASSIST_OUT%\dodge_assist.exe" --self-test
if errorlevel 1 exit /b 1
for %%F in (buffer hook trampoline) do (
 "%CC%" -O2 -c vendor\minhook\src\%%F.c -o build\%%F.o
 if errorlevel 1 exit /b 1
)
"%CC%" -O2 -c vendor\minhook\src\hde\hde64.c -o build\hde64.o
if errorlevel 1 exit /b 1
"%CXX%" -std=c++20 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ -shared -DTH_ASSIST_DLL -Ivendor\imgui -Ivendor\imgui\backends -Ivendor\minhook\include native\main.cpp native\overlay.cpp vendor\imgui\imgui.cpp vendor\imgui\imgui_draw.cpp vendor\imgui\imgui_tables.cpp vendor\imgui\imgui_widgets.cpp vendor\imgui\backends\imgui_impl_win32.cpp vendor\imgui\backends\imgui_impl_dx11.cpp build\buffer.o build\hook.o build\trampoline.o build\hde64.o -o "%ASSIST_OUT%\scarlet_assist.dll" -lbcrypt -luser32 -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lgdi32
if errorlevel 1 exit /b 1
"%CXX%" -std=c++20 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ native\injector.cpp -o "%ASSIST_OUT%\scarlet_launcher.exe" -lbcrypt -luser32
if errorlevel 1 exit /b 1

