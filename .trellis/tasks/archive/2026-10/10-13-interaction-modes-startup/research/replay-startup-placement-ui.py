"""Extract the production Presets block and all sidebar geometry, with real Combo/Navigation methods.
No HWND is created. The default font and config-write recorder are test doubles;
sidebar labels are placeholders because only rectangle overlap is checked.
Compile Build/StartupPlacementUI/presets.cpp using vcvarsall arm64 and imgui sources
as documented by the earlier interaction-mode replay.
"""
from pathlib import Path
import re, json

source = Path('Inkeys/Inkeys/UI/Setting/Setting.cpp').read_text(encoding='utf-8-sig')
begin = source.index('case settingTabEnum::tabPreset:')
begin = source.index('{', begin) + 1
end = source.index('\n\t\t\t\t\tbreak;', begin)
block = source[begin:end]
block = re.sub(r'IA\(I18nKey.SettingsUI.Preset.StartupPlacement.(\w+)\)', r'Text("\1")', block)
block = block.replace('IA(I18nKey.SettingsUI.Preset.N)', 'Text("Page")')
block = block.replace('if (Widgets::combo.Begin(', 'positions[0]=ImGui::GetCursorScreenPos(); positions[0].x+=10*settingGlobalScale; positions[0].y+=10*settingGlobalScale; if (Widgets::combo.Begin(')
block = block.replace('const bool selected = placement == i;', 'const bool selected = placement == i; positions[i+1]=ImGui::GetCursorScreenPos(); positions[i+1].x+=10*settingGlobalScale; positions[i+1].y+=5*settingGlobalScale;')
block = block.replace('ImGui::PopTextWrapPos();', 'if (!ImGui::GetCurrentWindow()->ClipRect.Contains(ImGui::GetItemRectMax())) ++clips; ImGui::PopTextWrapPos();')
block = block.replace('ImGui::TextUnformatted(Text("N").c_str());', 'ImGui::TextUnformatted(Text("N").c_str()); labelRight=ImGui::GetItemRectMax().x;')
block = block.replace('ImGui::SetNextItemWidth(comboWidth);', 'if(ImGui::GetCursorScreenPos().x<labelRight+5*settingGlobalScale)++overlaps; ImGui::SetNextItemWidth(comboWidth);')
langs=[]
for lang in ('zh-CN','zh-TW','en-US'):
    text=Path('Inkeys/src/i18n/'+lang+'.jsonc').read_text(encoding='utf-8-sig')
    preset=re.search(r'"Preset":\s*\{\s*"N":\s*("(?:[^"\\]|\\.)*"),\s*"StartupPlacement":\s*\{(.*?)\}',text,re.S)
    obj=json.loads('{'+preset[2]+'}')
    langs.append('{'+','.join([preset[1]]+[json.dumps(obj[k],ensure_ascii=False) for k in ('N','E','CenteredBottomBar','BottomLeftFloating','BottomRightFloating')])+'}')
widgets=Path('Inkeys/Inkeys/UI/Setting/Setting.Widgets.cpp').read_text(encoding='utf-8-sig')
pop=re.search(r'\t\tvoid PopControlStyle\(.*?\n\t\t\}',widgets,re.S)[0]
push=re.search(r'\t\tvoid PushComboColors\(\).*?\n\t\t\}',widgets,re.S)[0]
methods=widgets[widgets.index('\tbool ComboClass::Begin'):widgets.index('\tComboClass combo;')]
combo='struct Combo {bool Begin(const char*,const char*,int) const;bool Selectable(const char*,bool) const;void End() const;};'+pop+push+methods.replace('ComboClass::','Combo::')+'Combo combo;'
navigation=re.search(r'\tbool ButtonClass::Navigation\(.*?\n\t\}',widgets,re.S)[0].replace('ButtonClass::','Button::')
combo+='struct Button {bool Navigation(const char*,const ImVec2&,bool,ImU32=FluentColor::TextStrong,const ImVec2& = {0.f,.5f}) const;};'+navigation+'Button button;'
sidebar=source[source.index('// 左侧导航栏'):source.index('// 主版块 765')]
tokens=re.finditer(r'ImGui::SetCursorPos(?:Y)?\(.*?\);|Widgets::button.Navigation\(',sidebar,re.S)
nav=[]
nav_count=0
for match in tokens:
    if match[0].startswith('ImGui'):
        nav.append(match[0]);continue
    position=match.end();depth=1
    while depth:
        char=sidebar[position];position+=1
        if char=='(':depth+=1
        elif char==')':depth-=1
    call=sidebar[match.start():position]
    call=re.sub(r'IA\(I18nKey.[\w.]+\)',f'string("Navigation{nav_count}")',call)
    call=re.sub(r'settingTab == settingTabEnum::\w+','false',call)
    nav.append(call+'; navRects.push_back(ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()));')
    nav_count+=1
assert nav_count==16
prefix=r'''
#include "imgui.h"
#include "imgui_internal.h"
#include "../../Inkeys/Inkeys/UI/StartupPlacement.h"
#include <cstdio>
#include <string>
#include <cstring>
#include <algorithm>
#include <vector>
using std::max;using std::string;
const char* labels[3][6]={ LANGS };
const char* keys[]={"Page","N","E","CenteredBottomBar","BottomLeftFloating","BottomRightFloating"};
int lang=0,writes=0,clips=0,overlaps=0;float settingGlobalScale=1,labelRight=0;
constexpr float settingItemWidth=758,settingContentPanelWidth=780;
struct Atom {int value=0;int load(){return value;}void operator=(int v){value=v;}};
namespace Inkeys {struct{struct{struct{Atom StartupPlacement;}Bar;}UI;}config;}
ImFont* ImFontMain;ImVec2 positions[4];std::vector<int> payloads;
string Text(const char*k){for(int i=0;i<6;i++)if(!strcmp(k,keys[i]))return labels[lang][i];return "";}
void QueueConfigWrite(){++writes;payloads.push_back(Inkeys::config.UI.Bar.StartupPlacement.load());}
namespace Widgets{namespace FluentColor{constexpr ImU32 Transparent=0,TextStrong=0xffffffff,TextPrimary=0xffffffff,TextSecondary=0xffbbbbbb,CardBackground=0xff333333,WindowBackground=0xff222222,AccentText=0xffff0000,Danger=0xff0000ff,SubtleFill=0xff444444,SubtleFillPressed=0xff555555,ControlStroke=0xff444444,ControlFill=0xff333333,ControlFillHovered=0xff444444,ControlFillPressed=0xff555555,PopupBackground=0xff333333;} COMBO }
int navFailures=0;
void drawSidebar(){ImFontMain->Scale=.5f;ImGui::PushFont(ImFontMain);std::vector<ImRect>navRects; NAV
 for(size_t i=0;i<navRects.size();++i){if(navRects[i].Min.y<40*settingGlobalScale||navRects[i].Max.y>695*settingGlobalScale)++navFailures; for(size_t j=0;j<i;++j)if(navRects[i].Overlaps(navRects[j]))++navFailures;}
 if(navRects[10].Max.y>485*settingGlobalScale)++navFailures;ImGui::PopFont();}
void frame(ImVec2 pos,bool down){auto&io=ImGui::GetIO();io.AddMousePosEvent(pos.x,pos.y);io.AddMouseButtonEvent(0,down);ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({4096,3000});ImGui::Begin("root",nullptr,ImGuiWindowFlags_NoTitleBar);
drawSidebar();
'''.replace('LANGS',','.join(langs)).replace('COMBO',combo).replace('NAV','\n'.join(nav))
suffix=r'''
ImGui::End();ImGui::Render();}
void clickAt(ImVec2 p){frame(p,false);frame(p,true);frame(p,false);}
void choose(int i){clickAt(positions[0]);frame({-1,-1},false);clickAt(positions[i+1]);}
int main(){int failures=0;for(lang=0;lang<3;lang++)for(float scale:{0.6f,1.f,1.5f,2.f}){settingGlobalScale=scale;ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={4096,3000};io.DeltaTime=1.f/60;ImFontConfig cfg;cfg.SizePixels=32*scale;ImFontMain=io.Fonts->AddFontDefault(&cfg);unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);ImGui::GetStyle().FramePadding={0,0};ImGui::GetStyle().WindowPadding={0,0};ImGui::GetStyle().ItemSpacing={8*scale,0};Inkeys::config.UI.Bar.StartupPlacement=0;writes=clips=overlaps=0;payloads.clear();frame({-1,-1},false);frame({-1,-1},false);choose(1);if(Inkeys::config.UI.Bar.StartupPlacement.load()!=1||writes!=1)++failures;choose(1);if(writes!=1)++failures;choose(2);if(Inkeys::config.UI.Bar.StartupPlacement.load()!=2||writes!=2)++failures;choose(0);if(Inkeys::config.UI.Bar.StartupPlacement.load()!=0||writes!=3)++failures;if(clips||overlaps||payloads!=std::vector<int>({1,2,0}))++failures;printf("lang=%d scale=%.1f writes=%d clips=%d overlaps=%d failures=%d\n",lang,scale,writes,clips,overlaps,failures);ImGui::DestroyContext();}return failures?1:0;}
'''
suffix=suffix.replace('writes=clips=overlaps=0;', 'writes=clips=overlaps=navFailures=0;')
suffix=suffix.replace('if(clips||overlaps||payloads', 'if(clips||overlaps||navFailures||payloads')
suffix=suffix.replace('clips=%d overlaps=%d failures=%d', 'clips=%d overlaps=%d navigation_failures=%d failures=%d')
suffix=suffix.replace('writes,clips,overlaps,failures', 'writes,clips,overlaps,navFailures,failures')
Path('Build/StartupPlacementUI/presets.cpp').write_text(prefix+block+suffix,encoding='utf-8')
