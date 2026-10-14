"""Extract the production FIFO and render deferral, with a bounded HWND wait model.

No HWND is created. This tests the real dispatch implementation, not Win32 owner
execution. Unrelated business cases and completion notification are test doubles.
"""
from pathlib import Path
import re

source = Path('Inkeys/Inkeys/UI/Setting/Setting.cpp').read_text(encoding='utf-8-sig')
state = Path('Inkeys/IdtState.cpp').read_text(encoding='utf-8-sig')
assert 'if (Inkeys::UI::Setting::DeferDraw3SyncIfRendering()) return;' in state
assert 'IdtAtomic<int> paintDevice;' in Path('Inkeys/IdtConfiguration.h').read_text(encoding='utf-8-sig')
assert source.count('SettingRenderScope renderScope;') == 2
assert source.count('SyncDraw3State();') == 3  # Execute + mode + save toggle
queue = source[source.index('\tclass SettingBusinessQueue'):source.index('\n\tvoid QueueBusiness')]
execute_start = queue.index('\t\tstatic bool Execute(')
execute_end = queue.index('\n\t\tmutex mutex_', execute_start)
cases = re.search(r'case SettingBusinessKind::SyncDraw3State:(.*?)case SettingBusinessKind::WritePptSetting:', queue, re.S)[0]
cases = cases[:cases.rindex('case SettingBusinessKind::WritePptSetting:')]
queue = queue[:execute_start] + '''static bool Execute(const SettingBusinessCommand& command) {
switch(command.kind) {''' + cases + '''} return false; }
''' + queue[execute_end:]
helper = re.search(r'\tbool DeferDraw3SyncIfRendering\(\)\n\t\{.*?\n\t\}', source, re.S)[0]
capture_case = re.search(r'case SettingBusinessKind::WriteSetting:\n\t\t\tcommand.jsonPayload = CaptureSettingJson\(\);\n\t\t\tbreak;', source)[0]
device_start = state.index('    {', state.index('void SyncDraw3State()'))
device_end = state.index('\t// 所有工具入口', device_start)
device_apply = state[device_start:device_end]
assert device_apply.count('setlist.paintDevice.load()') == 1
prefix = r'''
#include "../../Inkeys/IdtAtomic.h"
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
using namespace std;
using namespace chrono_literals;
mutex settingStateMutex;
struct Session { unsigned serial=0; Session BusinessCompletion(){return *this;} void PublishBusinessCompletion(unsigned s,bool){serial=s;} }settingSessionState;
namespace Inkeys::UI::RenderPipeline { enum class Client{Settings}; void Request(Client){} }
namespace Inkeys::UI::Setting { bool DeferDraw3SyncIfRendering(); }
enum class SettingBusinessKind {WriteSetting,SyncDraw3State};
struct SettingBusinessCommand { SettingBusinessKind kind; string jsonPayload; };
timed_mutex imguiMutex;
atomic<bool> waiting=false, failed=false;
mutex eventsMutex;
vector<string> events;
struct { IdtAtomic<int> paintDevice=0; }setlist;
mutex stateModeTransitionMutex;
struct { struct { int paintDevice=0; }penWidthContext; }stateMode;
atomic_uint64_t stateModeTransitionRevision=0;
void ResolveWidthSlotsLocked(){}
void ApplyDevice(){ DEVICE_APPLY }
string CaptureSettingJson(){return to_string(setlist.paintDevice.load());}
bool WriteSettingJson(const string& s){lock_guard l(eventsMutex);events.push_back("write"+s);return true;}
void SyncDraw3State(){
 if (Inkeys::UI::Setting::DeferDraw3SyncIfRendering()) return;
 ApplyDevice();
 waiting=true;
 // Model owner Submit::future.get waiting for an HWND callback that needs ImGui.
 thread hwnd([]{if(!imguiMutex.try_lock_for(1500ms)){failed=true;return;}imguiMutex.unlock();});
 hwnd.join();
 lock_guard l(eventsMutex);events.push_back("sync");
}
'''.replace('DEVICE_APPLY', device_apply)
dispatch = '''
void QueueBusiness(SettingBusinessCommand command) {
 switch(command.kind) {''' + capture_case + '''default: break; }
 settingBusinessQueue.Enqueue(std::move(command));
}
namespace Inkeys::UI::Setting {
''' + helper + '''
}
'''
suffix = r'''
int main(){
 if(!settingBusinessQueue.Start())return 1;
 if(Inkeys::UI::Setting::DeferDraw3SyncIfRendering())return 2;
 {
  lock_guard render(imguiMutex);
  SettingRenderScope scope;
  {SettingRenderScope nested;}
  if(!settingRenderingOnCurrentThread)return 3;
  setlist.paintDevice=1;SyncDraw3State();QueueBusiness({SettingBusinessKind::WriteSetting});
  auto deadline=chrono::steady_clock::now()+500ms;
  while(!waiting && chrono::steady_clock::now()<deadline)this_thread::yield();
  if(!waiting)return 4;
  setlist.paintDevice=0;SyncDraw3State();QueueBusiness({SettingBusinessKind::WriteSetting});
  {lock_guard l(eventsMutex);if(!events.empty())return 5;} // render has not released ImGui yet
 }
 if(settingRenderingOnCurrentThread)return 6;
 settingBusinessQueue.Stop(); // real Stop drains FIFO, including both frozen writes
 if(failed || events!=vector<string>{"sync","write1","sync","write0"})return 7;
 if(stateMode.penWidthContext.paintDevice!=0)return 11;
 // Use the real copyable atomic and device-apply block during rapid concurrent changes.
 thread writer([]{for(int i=0;i<10000;++i)setlist.paintDevice=i%2;setlist.paintDevice=1;});
 for(int i=0;i<10000;++i)ApplyDevice();
 writer.join();ApplyDevice();
 auto copied=setlist;setlist.paintDevice=0;
 if(stateMode.penWidthContext.paintDevice!=1 || copied.paintDevice.load()!=1)return 12;
 try{SettingRenderScope scope;throw runtime_error("test");}catch(...){}
 if(settingRenderingOnCurrentThread)return 8;
 // Stopped queue must not fall back to synchronous wait under the render lock.
 {lock_guard render(imguiMutex);SettingRenderScope scope;SyncDraw3State();}
 if(events.size()!=4)return 9;
 SyncDraw3State(); // non-render caller retains synchronous semantics
 if(events.size()!=5)return 10;
 puts("production FIFO: render deferral, HWND lock wait, frozen writes, nested/exception reset, stop drain, atomic mode/latest apply passed");
}
'''
Path('Build/ModeReplay').mkdir(parents=True, exist_ok=True)
Path('Build/ModeReplay/threading.cpp').write_text(prefix+queue+dispatch+suffix, encoding='utf-8')
