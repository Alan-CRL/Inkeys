"""Replay production toolbar settings/ComboClass without HWND or GPU.

Atomic preferences, layout wake and frozen write queue are counting fixtures;
production Config IO is exercised by config_metadata_tests separately.
"""
from pathlib import Path
import json
import re

output = Path('Build/ToolbarOrderExpansionValidation')
output.mkdir(parents=True, exist_ok=True)
source = Path('Inkeys/Inkeys/UI/Setting/Setting.cpp').read_text(encoding='utf-8-sig')
start = source.index('\t\t\t\t\t{\n\t\t\t\t\t\tconst string itemNames[]')
end = source.index('\n\t\t\t\t\t{\n\t\t\t\t\t\tImGui::SetCursorPosY', start)
block = source[start:end]
assert 'barUISet.UpdateRendering(false)' in block and 'QueueConfigWrite()' in block
assert 'Inkeys::config.UI.Bar.IconOrder' in block and 'Inkeys::config.UI.Bar.ExpandDirectionAdjustment' in block
block = re.sub(r'IA\(I18nKey.SettingsUI.Regular.(Toolbar\w+).(\w+)\)', r'Text("\1.\2")', block)
block = block.replace('ImGui::TextUnformatted(', 'RecordText(')
block = block.replace('if (Widgets::combo.Begin(',
    'activeItem=item; positions[item][0]=ImGui::GetCursorScreenPos(); '
    'if(lastTextMax.x+10*settingGlobalScale>positions[item][0].x) ++overlaps; '
    'positions[item][0].x+=10*settingGlobalScale; positions[item][0].y+=10*settingGlobalScale; '
    'if (Widgets::combo.Begin(')
block = block.replace('const bool selected = value == i;',
    'const bool selected = value == i; positions[item][i+1]=ImGui::GetCursorScreenPos(); '
    'positions[item][i+1].x+=10*settingGlobalScale; positions[item][i+1].y+=5*settingGlobalScale;')
keys = ['ToolbarIconOrder.'+k for k in ('N','FollowMainButton','FollowMainButtonReverse','LeftToRight','RightToLeft')]
keys += ['ToolbarExpansionDirection.'+k for k in ('N','Centerline','WhenSpaceInsufficient')]
locales=[]
for locale in ('zh-CN','zh-TW','en-US'):
    raw = Path(f'Inkeys/src/i18n/{locale}.jsonc').read_text(encoding='utf-8-sig')
    regular=json.loads('\n'.join(l for l in raw.splitlines() if not l.lstrip().startswith('//')))['SettingsUI']['Regular']
    locales.append('{'+','.join(json.dumps(regular[group][key],ensure_ascii=False) for group,key in (k.split('.') for k in keys))+'}')
widgets=Path('Inkeys/Inkeys/UI/Setting/Setting.Widgets.cpp').read_text(encoding='utf-8-sig')
pop=re.search(r'\t\tvoid PopControlStyle\(.*?\n\t\t\}',widgets,re.S)[0]
push=re.search(r'\t\tvoid PushComboColors\(\).*?\n\t\t\}',widgets,re.S)[0]
methods=widgets[widgets.index('\tbool ComboClass::Begin'):widgets.index('\tComboClass combo;')]
methods=methods.replace('const bool open = ImGui::BeginCombo(label, preview);',
    'RecordPreview(preview); const bool open = ImGui::BeginCombo(label, preview);')
methods=methods.replace('const bool clicked = ImGui::Selectable(label, selected);',
    'RecordOption(label); const bool clicked = ImGui::Selectable(label, selected);')
combo=('struct Combo {bool Begin(const char*,const char*,int)const;bool Selectable(const char*,bool)const;void End()const;};'
       +pop+push+methods.replace('ComboClass::','Combo::')+'Combo combo;')
prefix=r'''
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <array>
using std::max; using std::string;
const char* labels[3][8]={ LANGS };
const char* keys[]={ KEYS };
int lang=0,writes=0,wakes=0,clips=0,glyphClips=0,missing=0,overlaps=0,optionChecks=0,previewChecks=0;
int activeItem=0;
float settingGlobalScale=1;
constexpr float settingItemWidth=758;
ImFont* ImFontMain;
ImVec2 positions[2][5],lastTextMax;
std::vector<std::array<int,2>> frozen;
struct Atomic { int value; int load()const{return value;} Atomic& operator=(int v){value=v;return *this;} };
namespace Inkeys {struct Config {struct UI {struct Bar {Atomic IconOrder{0},ExpandDirectionAdjustment{1};} Bar;} UI;} config;}
struct Bar {void UpdateRendering(bool full){if(full)++clips; ++wakes;}} barUISet;
void QueueConfigWrite(){++writes;frozen.push_back({Inkeys::config.UI.Bar.IconOrder.load(),Inkeys::config.UI.Bar.ExpandDirectionAdjustment.load()});}
string Text(const char* key){for(int i=0;i<8;++i)if(!strcmp(key,keys[i]))return labels[lang][i];return "";}
void CheckGlyphs(const char* text){
    for(const char* p=text;*p;){unsigned int c=0;int n=ImTextCharFromUtf8(&c,p,nullptr);if(n<=0)break;p+=n;
        if(c>32&&!ImGui::GetFontBaked()->FindGlyphNoFallback(static_cast<ImWchar>(c)))++missing;}
}
void RecordText(const char* text){
    auto* w=ImGui::GetCurrentWindow();const int begin=w->DrawList->VtxBuffer.Size;
    ImGui::TextUnformatted(text);lastTextMax=ImGui::GetItemRectMax();const ImRect r=w->ClipRect;
    if(!r.Contains(ImGui::GetItemRectMin())||!r.Contains(lastTextMax))++clips;
    const ImRect t({r.Min.x-.5f,r.Min.y-.5f},{r.Max.x+.5f,r.Max.y+.5f});
    for(int i=begin;i<w->DrawList->VtxBuffer.Size;++i)if(!t.Contains(w->DrawList->VtxBuffer[i].pos))++glyphClips;
    CheckGlyphs(text);
}
void RecordPreview(const char* text){
    ++previewChecks;CheckGlyphs(text);
    const auto size=ImGui::CalcTextSize(text);const auto p=ImGui::GetCursorScreenPos();
    if(size.x+45*settingGlobalScale>ImGui::CalcItemWidth()+.5f
       ||p.y+size.y+15*settingGlobalScale>ImGui::GetCurrentWindow()->ClipRect.Max.y+.5f)++clips;
}
void RecordOption(const char* text){
    // ImGui 弹窗的第一帧只测量尺寸，Hidden=true 时尚未呈现，不能以临时 clip 判定截断。
    if(ImGui::GetCurrentWindow()->Hidden)return;
    ++optionChecks;CheckGlyphs(text);
    const auto size=ImGui::CalcTextSize(text);const auto p=ImGui::GetCursorScreenPos();const auto r=ImGui::GetCurrentWindow()->ClipRect;
    if(p.x+size.x>r.Max.x+.5f||p.x<r.Min.x-.5f)++clips;
    // popup 逻辑行框含字体上下留白，按字形的实际包络检查边框处是否裁切。
    auto* baked=ImGui::GetFontBaked();const float fontScale=ImGui::GetFontSize()/baked->Size;float x=std::floor(p.x),y=std::floor(p.y);
    for(const char* q=text;*q;){unsigned int c=0;int n=ImTextCharFromUtf8(&c,q,nullptr);if(n<=0)break;q+=n;
        const auto* glyph=baked->FindGlyph(static_cast<ImWchar>(c));if(!glyph)continue;
        if(glyph->Visible&&(x+glyph->X0*fontScale<r.Min.x-.5f||x+glyph->X1*fontScale>r.Max.x+.5f
            ||y+glyph->Y0*fontScale<r.Min.y-.5f||y+glyph->Y1*fontScale>r.Max.y+.5f))++glyphClips;
        x+=glyph->AdvanceX*fontScale;
    }
}
namespace Widgets {namespace FluentColor {
constexpr ImU32 CardBackground=0xff333333,TextStrong=0xffffffff,AccentText=0xffff0000,
ControlStroke=0xff444444,ControlFill=0xff333333,ControlFillHovered=0xff444444,
ControlFillPressed=0xff555555,PopupBackground=0xff333333;
} COMBO }
void frame(ImVec2 p,bool down){auto& io=ImGui::GetIO();io.AddMousePosEvent(p.x,p.y);io.AddMouseButtonEvent(0,down);
    ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({2400,2200});
    ImGui::Begin("root",nullptr,ImGuiWindowFlags_NoTitleBar);ImFontMain->Scale=.8f;ImGui::PushFont(ImFontMain);
    ImGui::SetCursorPosY(100*settingGlobalScale);
'''.replace('LANGS',','.join(locales)).replace('KEYS',','.join(json.dumps(k)for k in keys)).replace('COMBO',combo)
suffix=r'''
    ImGui::PopFont();ImGui::End();ImGui::Render();
}
void clickAt(ImVec2 p){frame(p,false);frame(p,true);frame(p,false);}
void choose(int item,int i){clickAt(positions[item][0]);frame({-1,-1},false);clickAt(positions[item][i+1]);}
int main(){int failures=0;
    for(lang=0;lang<3;++lang)for(float scale:{.6f,1.f,1.5f,2.f}){
        settingGlobalScale=scale;ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={2400,2200};io.DeltaTime=1.f/60;
        ImFontGlyphRangesBuilder builder;for(const auto& locale:labels)for(const auto* text:locale)builder.AddText(text);
        ImVector<ImWchar> ranges;builder.BuildRanges(&ranges);ImFontConfig base;base.OversampleH=base.OversampleV=1;
        ImWchar excluded[]={0xe81e,0xe81e,0};base.GlyphExcludeRanges=excluded;
        if(lang==1)io.Fonts->AddFontFromFileTTF("Inkeys/src/ttf/HarmonyOS_Sans_TC_Regular.ttf",30*scale,&base,ranges.Data);
        ImFontMain=io.Fonts->AddFontFromFileTTF("Inkeys/src/ttf/HarmonyOS_Sans_SC_Regular.ttf",30*scale,&base,ranges.Data);
        ImFontConfig icons;icons.OversampleH=icons.OversampleV=1;icons.MergeMode=true;icons.PixelSnapH=true;icons.GlyphOffset.y=10*scale;
        ImFontMain=io.Fonts->AddFontFromFileTTF("Inkeys/src/ttf/Segoe Fluent Icons.ttf",36*scale,&icons,ranges.Data);
        icons.GlyphOffset.y=4*scale;ImFontMain=io.Fonts->AddFontFromFileTTF("Inkeys/src/ttf/icomoon.ttf",32*scale,&icons,ranges.Data);
        if(!ImFontMain)return 2;unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        auto& style=ImGui::GetStyle();style.FramePadding=style.WindowPadding={0,0};style.ItemSpacing={8*scale,0};
        style.ChildBorderSize=style.FrameBorderSize=scale;style.ChildRounding=style.FrameRounding=style.PopupRounding=4*scale;
        Inkeys::config.UI.Bar.IconOrder=0;Inkeys::config.UI.Bar.ExpandDirectionAdjustment=1;
        writes=wakes=clips=glyphClips=missing=overlaps=optionChecks=previewChecks=0;frozen.clear();
        frame({-1,-1},false);frame({-1,-1},false);
        for(int v:{1,2,3,0}){const int before=writes;choose(0,v);
            if(Inkeys::config.UI.Bar.IconOrder.load()!=v||writes!=before+1||wakes!=writes)++failures;
            choose(0,v);if(writes!=before+1||wakes!=writes)++failures;}
        for(int v:{0,1}){const int before=writes;choose(1,v);
            if(Inkeys::config.UI.Bar.ExpandDirectionAdjustment.load()!=v||writes!=before+1||wakes!=writes)++failures;
            choose(1,v);if(writes!=before+1||wakes!=writes)++failures;}
        const std::vector<std::array<int,2>> expected={{{1,1}},{{2,1}},{{3,1}},{{0,1}},{{0,0}},{{0,1}}};
        if(frozen!=expected||clips||glyphClips||missing||overlaps||!optionChecks||!previewChecks)++failures;
        printf("locale=%d scale=%.1f writes=%d wakes=%d clipped=%d glyphClipped=%d missing=%d overlaps=%d optionChecks=%d previewChecks=%d failures=%d\n",
            lang,scale,writes,wakes,clips,glyphClips,missing,overlaps,optionChecks,previewChecks,failures);
        ImGui::DestroyContext();
    }return failures?1:0;
}
'''
(output/'settings-replay.cpp').write_text(prefix+block+suffix,encoding='utf-8')
print(output/'settings-replay.cpp')
