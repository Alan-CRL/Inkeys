"""Read-only source-excerpt probe. It is NOT the full PptInfo loop or Host."""
from pathlib import Path
import hashlib, json, subprocess, os
# 归档层级会变化，以 solution 标记定位仓库。
root = next((p for p in Path(__file__).resolve().parents if (p / "InkeysRepo.sln").is_file()), None)
if root is None:
    raise RuntimeError("Repository root (InkeysRepo.sln) was not found.")
out = root / 'Build/CanvasFreezeInvestigation/ppt-turn-20261017/events'
out.mkdir(parents=True, exist_ok=True)
plugin = root / 'Inkeys/IdtPlug-in.cpp'
host = root / 'Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp'
ps = plugin.read_text(encoding='utf-8-sig')
hs = host.read_text(encoding='utf-8-sig')
parts = {}
def excerpt(name, source, first, last):
    start = source.index(first)
    end = source.index(last, start)
    value = source[start:end]
    parts[name] = dict(firstLine=source[:start].count('\n')+1,
        lastLine=source[:end].count('\n')+1, sha256=hashlib.sha256(value.encode()).hexdigest())
    return value
trusted = excerpt('trusted', ps, '\t\tbool trustedPage =', '\t\tauto session = CapturePptSession();')
resolve = excerpt('resolve', ps, '\t\t\tif ((trustedPage || trustedEndScreen)', '\n\t\t}\n\t\t// COM getter')
trust_target = excerpt('trust_target', ps, '\t\tconst bool trustedTarget =', '\t\tconst bool whiteboard =')
ack = excerpt('ack', ps, '\tvoid AcknowledgePptPageUi(', '\n\t[[nodiscard]] bool IsInRect')
gate = excerpt('gate', hs, '\t\t\tconst bool blocked = commandScenePending', '\t\t\tconst bool changed = input.AdmissionBlocked()')
source = r'''
#define NOMINMAX
#include <windows.h>
#include "Inkeys/Inkeys/Business/PptSession.h"
#include <atomic>
#include <iostream>
#include <cassert>
namespace D3 = Inkeys::Drawing::Draw3;
namespace B = D3::Bridge;
namespace Bridge = D3::Bridge;
using namespace Inkeys::Business;
using std::lock_guard;
std::mutex pptSessionMutex;
PptSessionToken currentPptSession;
std::atomic<std::uint64_t> pptComGeneration{1};
std::optional<B::PresentationTarget> expectedPptUiTarget;
std::optional<B::PresentationReadyIdentity> uiReadyPresentationTarget;
namespace Inkeys::Drawing::Draw3 {
bool PublishProductPresentationUiReady(const Bridge::PresentationReadyIdentity& id) {
    uiReadyPresentationTarget=id; return true; // stub endpoint; real ack predicate above it is retained.
}
}
''' + ack + r'''
struct Probe {
    B::StateBridge bridge;
    std::optional<B::PresentationTarget> cachedTarget;
    D3::PresentationDescriptor saved;
    bool stateReliable=true, sessionApi=true;
    PptLifecycle lifecycle=PptLifecycle::Active;
    PptPageStatus pageStatus=PptPageStatus::Valid;
    PptSessionToken session;
    long long descriptorAcceptanceQpc=0;
    std::optional<B::PresentationReadyIdentity> readyPresentationTarget, suspendedPresentationTarget;
    Probe() {
        session.active=true; session.localSession=1; session.serviceGeneration=1; session.bindingRevision=1;
        currentPptSession=session;
        saved.status=D3::PresentationDescriptorStatus::StableSlideIds;
        saved.provider="PowerPoint"; saved.fullName="C:\\probe\\two-slides.pptx";
        saved.presentationName="probe"; saved.bindingRevision=1;
        saved.currentPage=1; saved.totalPage=2; saved.slideIds={101,102}; saved.currentSlideId=101;
        cachedTarget=D3::ResolvePresentationTarget(saved); assert(cachedTarget);
        cachedTarget->sessionRevision=1;
        cachedTarget->targetRevision=*bridge.PublishPresentationTarget(*cachedTarget);
        readyPresentationTarget=B::ReadyIdentityFor(*cachedTarget);
        uiReadyPresentationTarget=readyPresentationTarget;
    }
    bool tick(int rawPage, bool descriptorChanged, bool show=false) {
        int rawTotal=2;
        const auto* descriptor=&saved;
''' + trusted + resolve + trust_target + r'''
        if (!trustedTarget) {
            suspendedPresentationTarget=readyPresentationTarget;
            uiReadyPresentationTarget.reset(); // exactly the suspension endpoint effect, no full Host.
        } else {
            const auto revision=bridge.PublishPresentationTarget(*cachedTarget);
            assert(revision); cachedTarget->targetRevision=*revision;
            expectedPptUiTarget=cachedTarget;
            suspendedPresentationTarget.reset();
            // Simulate successful renderer Present for accepted target; no GPU is exercised.
            readyPresentationTarget=B::ReadyIdentityFor(*cachedTarget);
            AcknowledgePptPageUi(1,*revision,rawPage,rawTotal);
        }
        const auto desired=bridge.Snapshot();
        const auto expected=std::optional(B::ReadyIdentityFor(*desired.presentationTarget));
        bool commandScenePending=false;
        struct { bool requirePresentationUiReady=true; } startOptions;
        std::atomic<B::Workspace> workspace{B::Workspace::Presentation};
''' + gate + r'''
        if(show) std::cout << "raw=" << rawPage << " descriptor=" << saved.currentPage
          << " cachedTarget=" << cachedTarget->pageIndex+1 << " revision=" << cachedTarget->targetRevision
          << " trusted=" << trustedTarget << " uiReady=" << uiReadyPresentationTarget.has_value()
          << " blocked=" << blocked << "\n";
        return blocked;
    }
    void nextDescriptor() {saved.currentPage=2; saved.currentSlideId=102;}
};
int main() {
    Probe race; race.nextDescriptor();
    assert(race.tick(1,true,true));
    for(int n=0;n<10000;++n) assert(race.tick(2,false,n==0||n==9999));
    assert(race.cachedTarget->pageIndex==0);
    // Control 1: raw and descriptor are already aligned on first envelope consumption.
    Probe aligned; aligned.nextDescriptor(); assert(!aligned.tick(2,true,true));
    assert(aligned.cachedTarget->pageIndex==1);
    // Control 2: raw advances first, descriptor only becomes B the next tick.
    Probe rawFirst; assert(rawFirst.tick(2,false,true)); rawFirst.nextDescriptor();
    assert(!rawFirst.tick(2,true,true));
    // Recovery control: a subsequent new envelope forces resolution of the current B target.
    assert(!race.tick(2,true,true));
    std::cout << "PASS original-excerpt race blocks for 10000 unchanged ticks; 3 controls pass\n";
}
'''
(out/'probe.cpp').write_text(source,encoding='utf-8')
draw_sources=root/'Inkeys/Inkeys/Drawing/Draw3'
audited=[plugin,host,Path(__file__),root/'PptCOM/SlideShowSession.cs',root/'PptCOM/PptCOM.cs',
         root/'Inkeys/Inkeys/Business/PptSession.h']
audited += [draw_sources/name for name in ['Draw3.Bridge.cpp','Draw3.Bridge.h','Draw3.Presentation.cpp','Draw3.Presentation.h']]
manifest={'kind':'exact native excerpt + real Bridge/Presentation; stub COM envelope delivery, renderer and Host endpoints',
          'parts':parts,'files':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in audited}}
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=Path(subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.Component.MSBuild','-property','installationPath'],text=True).strip())
env_dump=subprocess.check_output(f'cmd.exe /d /s /c ""{vs / "Common7/Tools/VsDevCmd.bat"}" -arch=arm64 -host_arch=arm64 >nul && set"',text=True)
env={}
for line in env_dump.splitlines():
    if '=' in line:
        k,v=line.split('=',1)
        if k: env[k.upper()]=v
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostarm64/arm64/cl.exe'
provider=Path('D:/Project/Inkeys/Repo/Inkeys/VcpkgInstalled/Arm64/arm64-windows-static-v143')
draw=root/'Inkeys/Inkeys/Drawing/Draw3'
args=[str(cl),'/nologo','/std:c++20','/EHsc','/MT','/Od','/utf-8','/DNOMINMAX','/DUNICODE','/D_UNICODE',
      '/I'+str(root),'/I'+str(provider/'include'),str(out/'probe.cpp'),str(draw/'Draw3.Bridge.cpp'),str(draw/'Draw3.Presentation.cpp'),
      '/Fo'+str(out)+'\\','/Fe'+str(out/'probe.exe'),'/link','/LIBPATH:'+str(provider/'lib'),'jsoncpp.lib','normaliz.lib','user32.lib']
build=subprocess.run(args,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(out/'build.log').write_text(build.stdout,encoding='utf-8')
print('build exit',build.returncode)
if build.returncode:
    print(build.stdout); raise SystemExit(build.returncode)
run=subprocess.run([str(out/'probe.exe')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
(out/'run.log').write_text(run.stdout,encoding='utf-8')
(out/'results.json').write_text(json.dumps({'buildExit':build.returncode,'runExit':run.returncode,'compiler':str(cl),'exeSha256':hashlib.sha256((out/'probe.exe').read_bytes()).hexdigest()},indent=2),encoding='utf-8')
print(run.stdout); raise SystemExit(run.returncode)
