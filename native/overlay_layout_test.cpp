// Headless panel test with the production font and glyph ranges.
#include "overlay.cpp"
#include "imgui_internal.h"
#include <iostream>
int assistMain(int,char**) { return 0; }
int main() {
    ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.DisplaySize={1920,1080}; io.DeltaTime=1.f/60;
    io.IniFilename=nullptr;
    static const ImWchar ranges[]={0x20,0x024f,0x0400,0x052f,0};
    wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows,MAX_PATH);
    auto fontPath=(std::filesystem::path(windows)/"Fonts"/"segoeui.ttf").string();
    auto* font=io.Fonts->AddFontFromFileTTF(fontPath.c_str(),16,nullptr,ranges);
    if(!font) return 1;
    unsigned char* pixels=nullptr; int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height); theme();
    bool good=true;
    for(ImWchar glyph:{ImWchar(0x105),ImWchar(0x142),ImWchar(0x17c),ImWchar(0x416),ImWchar(0x44f),ImWchar(0x451)}) good=good && font->FindGlyphNoFallback(glyph);
    for(int language=0;language<3;++language) for(auto size:{ImVec2(1920,1080),ImVec2(1280,720),ImVec2(640,480)}) {
        controls::state.language=i18n::valid(language); io.DisplaySize=size;
        for(int i=0;i<6;++i) { ImGui::NewFrame(); draw(); ImGui::Render(); }
        auto* panel=ImGui::FindWindowByName("Scarlet Assist");
        std::cout<<"Language "<<language<<" viewport "<<size.x<<'x'<<size.y<<" overflow "<<panel->ScrollMax.x<<','<<panel->ScrollMax.y<<'\n';
        good=good && panel->ScrollMax.y<=1 && panel->ScrollMax.x<=1 && panel->Size.x<=size.x && panel->Size.y<=size.y;
        const auto& text=i18n::get(controls::state.language);
        // Text must fit the left descriptions and right binding controls.
        const float scale=io.FontGlobalScale;
        std::cout<<"Widths: hold "<<ImGui::CalcTextSize(text.holdHelp).x/scale<<" footer "<<(ImGui::CalcTextSize(text.stopAll).x+ImGui::CalcTextSize(text.pausedHelp).x)/scale+36<<" capture "<<ImGui::CalcTextSize(text.capture).x/scale<<'\n';
        for(auto description:text.descriptions) good=good && ImGui::CalcTextSize(description).x<340*scale;
        good=good && ImGui::CalcTextSize(text.capture).x<=134*scale;
        good=good && ImGui::CalcTextSize(text.predictionHelp).x<=710*scale;
        good=good && ImGui::CalcTextSize(text.scoreModeHelp).x<=680*scale;
    }
    io.FontGlobalScale=1; io.DisplaySize={1920,1080};
    auto& state=controls::state; state.menu=false; state.ready=true; state.playing=true; state.showPrediction=true;
    state.prediction=prediction::project({192,384},{1,-1},4); state.prediction.target=Vec{260,320};
    state.predictionTime=GetTickCount64();
    auto previewVertices=[&]() { ImGui::NewFrame(); drawPrediction(); ImGui::Render(); return ImGui::GetDrawData()->TotalVtxCount; };
    good=good && previewVertices()>0;
    state.showPrediction=false; good=good && previewVertices()==0;
    state.showPrediction=true; state.playing=false; good=good && previewVertices()==0;
    state.playing=true; state.predictionTime=GetTickCount64()-1000; good=good && previewVertices()==0;
    state.predictionTime=GetTickCount64(); state.stop(); good=good && previewVertices()==0;
    good=good && i18n::valid(-1)==i18n::Language::English && i18n::valid(99)==i18n::Language::English;
    ImGui::DestroyContext(); std::cout<<(good ? "PASS":"FAIL")<<" localization, glyphs and panel layout\n"; return good ? 0:1;
}

