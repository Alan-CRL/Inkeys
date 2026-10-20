"""Replay the actual new Setting block and ComboClass without HWND or renderer.

The runtime setter and write queue are counters; production state/FIFO correctness
is covered separately. Embedded font source files match the resource registrations.
"""
from pathlib import Path
import json
import re

output = Path("Build/SelectClickThroughValidation")
output.mkdir(parents=True, exist_ok=True)
source = Path("Inkeys/Inkeys/UI/Setting/Setting.cpp").read_text(encoding="utf-8-sig")
anchor = source.index("ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Regular.N)")
marker = "\t\t\t\t\t{\n\t\t\t\t\t\tImGui::SetCursorPosY"
start = source.index(marker, anchor)
end = source.index(marker, start + len(marker))
block = source[start:end]
assert '"选择与穿透说明"' in block and "SetSelectAndClickThroughMode(i)" in block
block = re.sub(
    r"IA\(I18nKey.SettingsUI.Regular.SelectAndClickThrough.(\w+)\)",
    r'Text("\1")', block)
block = block.replace(
    "ImGui::TextUnformatted(modeDescription.c_str());",
    "RecordDescription(modeDescription.c_str(), descriptionHeight, infoCardHeight);")
block = block.replace("ImGui::TextUnformatted(", "RecordText(")
block = block.replace(
    "if (Widgets::combo.Begin(",
    "positions[0]=ImGui::GetCursorScreenPos(); "
    "if (lastTextMax.x+10*settingGlobalScale>positions[0].x) ++overlaps; "
    "positions[0].x+=10*settingGlobalScale; positions[0].y+=10*settingGlobalScale; "
    "if (Widgets::combo.Begin(")
block = block.replace(
    "const bool selected = mode == i;",
    "const bool selected = mode == i; positions[i+1]=ImGui::GetCursorScreenPos(); "
    "positions[i+1].x+=10*settingGlobalScale; positions[i+1].y+=5*settingGlobalScale;")

keys = ("N", "Combined", "Separate", "CombinedDescription", "SeparateDescription",
        "ClickThrough", "ClickThroughDescription")
locales = []
for locale in ("zh-CN", "zh-TW", "en-US"):
    text = Path(f"Inkeys/src/i18n/{locale}.jsonc").read_text(encoding="utf-8-sig")
    document = json.loads("\n".join(line for line in text.splitlines()
                                   if not line.lstrip().startswith("//")))
    values = document["SettingsUI"]["Regular"]["SelectAndClickThrough"]
    locales.append("{" + ",".join(json.dumps(values[k], ensure_ascii=False)
                                 for k in keys) + "}")

widgets = Path("Inkeys/Inkeys/UI/Setting/Setting.Widgets.cpp").read_text(encoding="utf-8-sig")
pop = re.search(r"\t\tvoid PopControlStyle\(.*?\n\t\t\}", widgets, re.S)[0]
push = re.search(r"\t\tvoid PushComboColors\(\).*?\n\t\t\}", widgets, re.S)[0]
methods = widgets[widgets.index("\tbool ComboClass::Begin"):widgets.index("\tComboClass combo;")]
combo = ("struct Combo {bool Begin(const char*,const char*,int)const;"
         "bool Selectable(const char*,bool)const;void End()const;};"
         + pop + push + methods.replace("ComboClass::", "Combo::") + "Combo combo;")

prefix = r'''
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
using std::max;
using std::string;
const char* labels[3][7] = { LANGS };
const char* keys[] = { KEYS };
int lang=0, mode=0, writes=0, setterCalls=0, clips=0, glyphClips=0, missingGlyphs=0, overlaps=0;
int descriptionChecks=0, spacingFailures=0;
float compactHeight=0, looseHeight=0, cardHeight=0;
float settingGlobalScale=1;
constexpr float settingItemWidth=758;
ImFont* ImFontMain;
ImVec2 positions[3], lastTextMax;
std::vector<int> frozenWrites;
string Text(const char* key) {
    for(int i=0;i<7;++i) if(!strcmp(key,keys[i])) return labels[lang][i];
    return "";
}
int GetSelectAndClickThroughMode() {return mode;}
bool SetSelectAndClickThroughMode(int value) {
    ++setterCalls;
    if(value==mode) return false;
    mode=value;
    return true;
}
void QueueConfigWrite() {++writes; frozenWrites.push_back(mode);}
void RecordText(const char* text) {
    auto* window=ImGui::GetCurrentWindow();
    const int begin=window->DrawList->VtxBuffer.Size;
    ImGui::TextUnformatted(text);
    lastTextMax=ImGui::GetItemRectMax();
    const ImRect rect=window->ClipRect;
    if(!rect.Contains(ImGui::GetItemRectMin()) || !rect.Contains(lastTextMax)) ++clips;
    const ImRect tolerant({rect.Min.x-.5f,rect.Min.y-.5f},{rect.Max.x+.5f,rect.Max.y+.5f});
    for(int i=begin;i<window->DrawList->VtxBuffer.Size;++i)
        if(!tolerant.Contains(window->DrawList->VtxBuffer[i].pos)) ++glyphClips;
    for(const char* p=text;*p;) {
        unsigned int c=0;
        const int consumed=ImTextCharFromUtf8(&c,p,nullptr);
        if(consumed<=0) break;
        p+=consumed;
        if(c>32 && !ImGui::GetFontBaked()->FindGlyphNoFallback(static_cast<ImWchar>(c)))
            ++missingGlyphs;
    }
}
void RecordDescription(const char* text,float measuredHeight,float infoHeight) {
    ++descriptionChecks;
    RecordText(text);
    compactHeight=measuredHeight;
    cardHeight=infoHeight;
    // 实际文字项应匹配测量高度；旧双换行应恰好多占两行，而非改变换行宽度。
    if(std::count(text,text+strlen(text),'\n')!=2 || strstr(text,"\n\n")
        || std::abs(ImGui::GetItemRectSize().y-measuredHeight)>.5f) ++spacingFailures;
    string loose;
    for(const char* p=text;*p;++p) {loose+=*p;if(*p=='\n') loose+='\n';}
    looseHeight=ImGui::CalcTextSize(loose.c_str(),nullptr,false,(settingItemWidth-80)*settingGlobalScale).y;
    if(std::abs((looseHeight-measuredHeight)-2*ImGui::GetTextLineHeight())>.5f
        || std::abs(infoHeight-(max(measuredHeight,18*settingGlobalScale)+40*settingGlobalScale))>.5f)
        ++spacingFailures;
}
namespace Widgets {
namespace FluentColor {
constexpr ImU32 CardBackground=0xff333333,TextStrong=0xffffffff,AccentText=0xffff0000,
ControlStroke=0xff444444,ControlFill=0xff333333,ControlFillHovered=0xff444444,
ControlFillPressed=0xff555555,PopupBackground=0xff333333;
}
COMBO
}
void frame(ImVec2 pos,bool down) {
    auto& io=ImGui::GetIO();
    io.AddMousePosEvent(pos.x,pos.y);
    io.AddMouseButtonEvent(0,down);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize({2400,2200});
    ImGui::Begin("root",nullptr,ImGuiWindowFlags_NoTitleBar);
    ImFontMain->Scale=.8f;
    ImGui::PushFont(ImFontMain);
    ImGui::SetCursorPosY(10*settingGlobalScale);
    ImGui::TextUnformatted(lang==0?"常规":lang==1?"常規":"General");
'''.replace("LANGS", ",".join(locales)).replace(
    "KEYS", ",".join(json.dumps(k) for k in keys)).replace("COMBO", combo)

suffix = r'''
    ImGui::PopFont();
    ImGui::End();
    ImGui::Render();
}
void clickAt(ImVec2 p) {frame(p,false);frame(p,true);frame(p,false);}
void choose(int i) {
    clickAt(positions[0]);
    frame({-1,-1},false);
    clickAt(positions[i+1]);
}
int main() {
    int failures=0;
    for(lang=0;lang<3;++lang) for(float scale:{.6f,1.f,1.5f,2.f}) {
        settingGlobalScale=scale;
        ImGui::CreateContext();
        auto& io=ImGui::GetIO();
        io.IniFilename=nullptr;
        io.DisplaySize={2400,2200};
        io.DeltaTime=1.f/60;
        ImFontGlyphRangesBuilder builder;
        for(const auto& locale:labels) for(const auto* text:locale) builder.AddText(text);
        builder.AddText("常规常規General:\uf167");
        ImVector<ImWchar> ranges;
        builder.BuildRanges(&ranges);
        ImFontConfig base;
        base.OversampleH=base.OversampleV=1;
        ImWchar excluded[]={0xe81e,0xe81e,0};
        base.GlyphExcludeRanges=excluded;
        // 对应 Setting.cpp 的 258/198 基础字体与 257/262 图标合并顺序。
        if(lang==1) io.Fonts->AddFontFromFileTTF("Inkeys/src/ttf/HarmonyOS_Sans_TC_Regular.ttf",30*scale,&base,ranges.Data);
        ImFontMain=io.Fonts->AddFontFromFileTTF("Inkeys/src/ttf/HarmonyOS_Sans_SC_Regular.ttf",30*scale,&base,ranges.Data);
        ImFontConfig icons;
        icons.OversampleH=icons.OversampleV=1;
        icons.MergeMode=true;
        icons.PixelSnapH=true;
        icons.GlyphOffset.y=10*scale;
        ImFontMain=io.Fonts->AddFontFromFileTTF("Inkeys/src/ttf/Segoe Fluent Icons.ttf",36*scale,&icons,ranges.Data);
        icons.GlyphOffset.y=4*scale;
        ImFontMain=io.Fonts->AddFontFromFileTTF("Inkeys/src/ttf/icomoon.ttf",32*scale,&icons,ranges.Data);
        if(!ImFontMain) return 2;
        unsigned char* pixels;
        int width,height;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        auto& style=ImGui::GetStyle();
        style.FramePadding=style.WindowPadding={0,0};
        style.ItemSpacing={8*scale,0};
        style.ChildBorderSize=style.FrameBorderSize=scale;
        style.ChildRounding=style.FrameRounding=style.PopupRounding=4*scale;
        mode=writes=setterCalls=clips=glyphClips=missingGlyphs=overlaps=0;
        descriptionChecks=spacingFailures=0;
        compactHeight=looseHeight=cardHeight=0;
        frozenWrites.clear();
        frame({-1,-1},false);
        frame({-1,-1},false);
        choose(1);
        if(mode!=1||writes!=1||setterCalls!=1) ++failures;
        choose(1);
        if(mode!=1||writes!=1||setterCalls!=1) ++failures;
        choose(0);
        if(mode!=0||writes!=2||setterCalls!=2||frozenWrites!=std::vector<int>{1,0}) ++failures;
        if(clips||glyphClips||missingGlyphs||overlaps||!descriptionChecks||spacingFailures) ++failures;
        printf("locale=%d scale=%.1f mode=%d writes=%d setters=%d clipped=%d glyphClipped=%d missing=%d overlaps=%d compactHeight=%.1f oldHeight=%.1f cardHeight=%.1f spacingFailures=%d failures=%d\n",
            lang,scale,mode,writes,setterCalls,clips,glyphClips,missingGlyphs,overlaps,
            compactHeight,looseHeight,cardHeight,spacingFailures,failures);
        ImGui::DestroyContext();
    }
    return failures?1:0;
}
'''
(output / "settings-replay.cpp").write_text(prefix + block + suffix, encoding="utf-8")
print(output / "settings-replay.cpp")
