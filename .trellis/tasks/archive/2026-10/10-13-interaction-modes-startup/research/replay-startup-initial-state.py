"""Extract current production startup assignments and replay them without HWND.

Uses real StartupPlacement geometry/enums and std::atomic dock fields. Property
SetDirect/Initialization and the button list are minimal test doubles; this does
not execute the renderer, ULW commit, device resources, or window handoff.
"""
from pathlib import Path

source = Path('Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp').read_text(encoding='utf-8-sig')
start = source.index('bool BarUISetClass::InitializeStartupPlacement(')
end = source.index('void BarUISetClass::PublishDisplaySnapshot(', start)
member = source[start:end].replace('BarUISetClass::', 'Owner::')
selection = '\t\tconst bool floatingStartup = barUISet.InitializeStartupPlacement(startupPlacement);\n'
assert selection in source
assert 'bool InitializeStartupPlacement(Inkeys::UI::StartupPlacement placement) noexcept;' in Path(
    'Inkeys/Inkeys/UI/Bar/Bar.Main.cppm').read_text(encoding='utf-8-sig')
start = source.index('\t\tif (floatingStartup)\n')
end = source.index('\t\t(void)Inkeys::Startup::Report(Inkeys::Startup::Milestone::BarComponentsReady);', start)
children = source[start:end]
start = source.index('\t\t\t\tconst bool foldedStartup =')
end = source.index('\t\t\t\tshape->frameRendering', start)
shape_initialization = source[start:end]
assert source.index(selection) < source.index('if (!barUISet.Rendering())')
assert source.index(children) < source.index('if (!barUISet.Rendering())')

prefix = r'''
// Intentionally load Win32 macros first: verify the new header preserves them.
#include <windows.h>
#include "../../Inkeys/Inkeys/UI/StartupPlacement.h"
#ifndef min
#error Header unexpectedly removed caller min macro
#endif
#ifndef max
#error Header unexpectedly removed caller max macro
#endif
#include "../../Inkeys/IdtAtomic.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace Inkeys::UI;
using namespace Inkeys::UI::Bar;
using std::memory_order_relaxed;
struct Property { double value=1; void SetDirect(double x){value=x;} void Initialization(double x){value=x;} };
struct BarUiPctClass : Property { explicit BarUiPctClass(double x){value=x;} };
struct Shape {
 Property x,y,pct;
 std::optional<BarUiPctClass> framePct=BarUiPctClass(1),frameLightPct=BarUiPctClass(1);
};
struct Button { Shape button; struct{Property pct;}icon,pngIcon,name; };
struct ButtonList {
 std::array<Button,2> buttons;
 Button* Get(int i){return i==1?nullptr:&buttons[i==0?0:1];}
};
struct Owner {
 StartupPlacement startupPlacement=StartupPlacement::CenteredBottomBar;
 struct{IdtAtomic<bool>fold=false;}barState;
 bool InitializeStartupPlacement(StartupPlacement placement) noexcept;
 BarBottomDockMode DockMode()const{return bottomDockMode.load();}
 BarBottomDockCenterMode CenterMode()const{return bottomDockCenterMode.load();}
 struct{int tot=3; ButtonList buttonList;}barButtonSet;
protected:
 std::atomic<BarBottomDockMode>bottomDockMode=BarBottomDockMode::Floating;
 std::atomic<BarBottomDockCenterMode>bottomDockCenterMode=BarBottomDockCenterMode::Free;
};
PRODUCTION_MEMBER
constexpr double BarMainBarFillOpacity=.8,BarMainBarFrameOpacity=.18;
void replay(Owner& barUISet,StartupPlacement startupPlacement,Shape* shape) {
'''
suffix = r'''
}
int main(){
 for(int value:{0,1,2,-1,3}){
  Owner owner;Shape mainBar;
  replay(owner,static_cast<StartupPlacement>(value),&mainBar);
  const bool corner=value==1||value==2;
  assert(owner.startupPlacement==NormalizeStartupPlacement(value));
  assert(owner.barState.fold.load()==corner);
  assert(owner.DockMode()==(corner?BarBottomDockMode::Floating:BarBottomDockMode::BottomDocked));
  assert(owner.CenterMode()==(corner?BarBottomDockCenterMode::Free:BarBottomDockCenterMode::Centered));
  assert(mainBar.pct.value==(corner?0:.8));
  assert(mainBar.framePct->value==(corner?0:.18));
  for(auto& b:owner.barButtonSet.buttonList.buttons){
   if(corner){
    assert(b.button.x.value==40&&b.button.y.value==40);
    assert(b.button.pct.value==0&&b.button.framePct->value==0&&b.button.frameLightPct->value==0);
    assert(b.icon.pct.value==0&&b.pngIcon.pct.value==0&&b.name.pct.value==0);
   }else assert(b.icon.pct.value==1&&b.name.pct.value==1);
  }
 }
 puts("source-extracted startup assignment replay passed: 3 placements, invalid fallback, Floating/Free/fold, child terminal alpha, MainBar alpha, Win32 macro preservation; no HWND commit tested");
}
'''
output=Path('Build/StartupPlacementReplay')
output.mkdir(parents=True,exist_ok=True)
(output/'initial-state.cpp').write_text(prefix.replace("PRODUCTION_MEMBER",member)+selection+children+shape_initialization+suffix,encoding='utf-8')
