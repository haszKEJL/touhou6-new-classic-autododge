#define TH_ASSIST_LIBRARY
#include "main.cpp"
uintptr_t remoteModule(DWORD pid,const wchar_t* name) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if(Module32FirstW(snap.value,&entry)) do { if(!_wcsicmp(entry.szModule,name)) return reinterpret_cast<uintptr_t>(entry.modBaseAddr); } while(Module32NextW(snap.value,&entry));
    return 0;
}
int main() {
    try {
        DWORD pid=findGame(); if(!pid) throw std::runtime_error("Uruchom th06nc.exe, a nastepnie ponownie run.bat.");
        if(remoteModule(pid,L"scarlet_assist.dll")) { std::cout<<"Panel jest juz zaladowany. W grze nacisnij Insert.\n"; return 0; }
        Game validation(pid);
        wchar_t ownPath[32768]{}; GetModuleFileNameW(nullptr,ownPath,32768);
        auto dll=std::filesystem::path(ownPath).parent_path()/L"scarlet_assist.dll";
        if(!std::filesystem::is_regular_file(dll)) throw std::runtime_error("Brak scarlet_assist.dll obok launchera.");
        { Handle stop(OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\TouhouNewClassicDodgeAssistStop")); if(stop.value) SetEvent(stop.value); }
        for(int i=0;i<100;++i) { Handle lock(OpenMutexW(SYNCHRONIZE,FALSE,L"Local\\TouhouNewClassicDodgeAssist")); if(!lock.value) break; Sleep(20); }
        Handle process(OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid));
        if(!process.value) throw std::runtime_error("Brak dostepu do gry. Uruchom gre i launcher z tym samym poziomem uprawnien.");
        auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"); HMODULE owner=nullptr;
        if(!load || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(load),&owner)) throw std::runtime_error("LoadLibraryW unavailable");
        wchar_t ownerPath[32768]{}; GetModuleFileNameW(owner,ownerPath,32768);
        auto remote=remoteModule(pid,std::filesystem::path(ownerPath).filename().c_str());
        if(!remote) throw std::runtime_error("Cannot resolve remote system module");
        auto entry=reinterpret_cast<LPTHREAD_START_ROUTINE>(remote+(reinterpret_cast<uintptr_t>(load)-reinterpret_cast<uintptr_t>(owner)));
        size_t bytes=(dll.native().size()+1)*sizeof(wchar_t);
        void* buffer=VirtualAllocEx(process.value,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!buffer) throw std::runtime_error("Cannot allocate DLL path");
        SIZE_T written=0;
        if(!WriteProcessMemory(process.value,buffer,dll.c_str(),bytes,&written) || written!=bytes) { VirtualFreeEx(process.value,buffer,0,MEM_RELEASE); throw std::runtime_error("Cannot copy DLL path"); }
        Handle thread(CreateRemoteThread(process.value,nullptr,0,entry,buffer,0,nullptr));
        if(!thread.value) { VirtualFreeEx(process.value,buffer,0,MEM_RELEASE); throw std::runtime_error("Cannot load DLL into game"); }
        if(WaitForSingleObject(thread.value,15000)!=WAIT_OBJECT_0) throw std::runtime_error("Loading is still pending. DLL path retained; do not run another injector until game restart.");
        VirtualFreeEx(process.value,buffer,0,MEM_RELEASE);
        if(!remoteModule(pid,L"scarlet_assist.dll")) throw std::runtime_error("Game could not load scarlet_assist.dll");
        std::cout<<"Scarlet Assist zaladowany. Insert: panel. F9: wylacz wszystkie funkcje.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<'\n'; return 1; }
}
