# UI-only replay: mock Sync/Write validates combo/layout, not HWND deadlock.
from pathlib import Path
import re,json
Path('Build/ModeReplay').mkdir(parents=True, exist_ok=True)
t=Path('Inkeys/Inkeys/UI/Setting/Setting.cpp').read_text(encoding='utf-8-sig')
start=t.index('\t\t\t\t\t{\n\t\t\t\t\t\tImGui::SetCursorPosY',t.index('ImGui::TextUnformatted(IA(I18nKey.SettingsUI.Draw.N)'))
end=t.index('\n\t\t\t\t\t{\n\t\t\t\t\t\tImGui::SetCursorPosY',start+10)
block=t[start:end]
block=re.sub(r'IA\(I18nKey.SettingsUI.Draw.Effect.Device.(\w+)\)',r'Text("\1")',block)
block=block.replace('if (Widgets::combo.Begin(', 'positions[0]=ImGui::GetCursorScreenPos(); positions[0].x+=10*settingGlobalScale; positions[0].y+=10*settingGlobalScale; if (Widgets::combo.Begin(')
block=block.replace('const bool selected = paintDevice == i;', 'const bool selected = paintDevice == i; positions[i+1]=ImGui::GetCursorScreenPos(); positions[i+1].x+=10*settingGlobalScale; positions[i+1].y+=5*settingGlobalScale;')
block=block.replace('ImGui::PopTextWrapPos();', 'if (!ImGui::GetCurrentWindow()->ClipRect.Contains(ImGui::GetItemRectMax())) ++clips; ImGui::PopTextWrapPos();')

langs=[]
for l in ('zh-CN','zh-TW','en-US'):
    s=Path('Inkeys/src/i18n/'+l+'.jsonc').read_text(encoding='utf-8-sig')
    device=re.search(r'"Device":\s*\{(.*?)\}',s,re.S)[1]
    obj=json.loads('{'+device+'}')
    langs.append('{'+','.join(json.dumps(obj[k],ensure_ascii=False) for k in ('N','Touch','MousePen','TouchE','MousePenE'))+'}')
widgets=Path('Inkeys/Inkeys/UI/Setting/Setting.Widgets.cpp').read_text(encoding='utf-8-sig')
pop=re.search(r'\t\tvoid PopControlStyle\(.*?\n\t\t\}', widgets,re.S)[0]
push=re.search(r'\t\tvoid PushComboColors\(\).*?\n\t\t\}', widgets,re.S)[0]
methods=widgets[widgets.index('\tbool ComboClass::Begin'):widgets.index('\tComboClass combo;')]
combo_code='struct Combo { bool Begin(const char*,const char*,int) const; bool Selectable(const char*,bool) const; void End() const;};'+pop+push+methods.replace('ComboClass::','Combo::')+'Combo combo;'
prefix=r'''
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <string>
#include <cstring>
#include <algorithm>
using std::max;
using std::string;
const char* labels[3][5] = { LANGS };
const char* keys[]={"N","Touch","MousePen","TouchE","MousePenE"};
int lang=0,writes=0,syncs=0,clips=0; float settingGlobalScale=1,drawingScale=1,stopTimingError=1;
constexpr float settingItemWidth=758,settingDescriptionWidth=718;
struct { int paintDevice=0; }setlist;
ImFont* ImFontMain;ImVec2 positions[3];
string Text(const char*k){for(int i=0;i<5;i++)if(!strcmp(k,keys[i]))return labels[lang][i];return "";}
void SyncDraw3State(){++syncs;} void WriteSetting(){++writes;}
float GetDrawingScale(){return 1;} float GetStopTimingError(){return 1;}
namespace Widgets{namespace FluentColor{constexpr ImU32 Transparent=0,TextStrong=0xffffffff,TextSecondary=0xffbbbbbb,CardBackground=0xff333333,AccentText=0xffff0000,ControlStroke=0xff444444,ControlFill=0xff333333,ControlFillHovered=0xff444444,ControlFillPressed=0xff555555,PopupBackground=0xff333333;} COMBO_CODE }
void frame(ImVec2 pos,bool down){auto&io=ImGui::GetIO();io.AddMousePosEvent(pos.x,pos.y);io.AddMouseButtonEvent(0,down);ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1920,1600});ImGui::Begin("root",nullptr,ImGuiWindowFlags_NoTitleBar);int PushStyleColorNum=0,PushStyleVarNum=0,PushFontNum=0;
'''.replace('LANGS',','.join(langs)).replace('COMBO_CODE',combo_code)
suffix=r'''
ImGui::End();ImGui::Render();}
void clickAt(ImVec2 p){frame(p,false);frame(p,true);frame(p,false);}
void click(int i){clickAt(positions[0]);frame({-1,-1},false);clickAt(positions[i+1]);}
int main(){int failures=0;for(lang=0;lang<3;lang++)for(float scale:{0.6f,1.f,1.5f,2.f}){settingGlobalScale=scale;ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1920,1600};io.DeltaTime=1.f/60;ImFontConfig cfg;cfg.SizePixels=32*scale;ImFontMain=io.Fonts->AddFontDefault(&cfg);unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);ImGui::GetStyle().FramePadding={0,0};ImGui::GetStyle().WindowPadding={0,0};ImGui::GetStyle().ItemSpacing={8*scale,0};setlist.paintDevice=0;writes=syncs=clips=0;frame({-1,-1},false);frame({-1,-1},false);click(1);if(setlist.paintDevice!=1||writes!=1||syncs!=1)++failures;click(1);if(writes!=1)++failures;click(0);if(setlist.paintDevice!=0||writes!=2||syncs!=2)++failures;if(clips)++failures;printf("lang=%d scale=%.1f mode=%d writes=%d syncs=%d clipped=%d failures=%d\n",lang,scale,setlist.paintDevice,writes,syncs,clips,failures);ImGui::DestroyContext();}return failures?1:0;}
'''
Path('Build/ModeReplay/combo.cpp').write_text(prefix+block+suffix,encoding='utf-8')
