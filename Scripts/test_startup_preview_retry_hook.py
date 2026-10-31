"""Replay the real IdtMain manual retry hook without opening windows.

Run in a native Visual Studio developer shell (cl must be on PATH). The fixture
uses real Win32 environment reads in its own process. Presentation, process
launch, logging, waits and cleanup are doubles; this is not GUI/ULW validation.
Generated sources, executables and preprocessor output stay in ignored Build/.
"""

import argparse
from pathlib import Path
import shutil
import subprocess


GUARD = "#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS"
RETRY_ENV = "INKEYS_STARTUP_PREVIEW_RETRY_FAILURE"

PREFIX = r'''
#include <windows.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

std::vector<std::string> events;
bool active = true;
bool fadeCommitted = true;
INT_PTR shellResult = 33;
int environmentReads = 0;
DWORD ReadEnvironment(const wchar_t* name, wchar_t* output, DWORD size) {
 ++environmentReads;
 return ::GetEnvironmentVariableW(name, output, size);
}
struct Logger {
 template<class... T> void warn(T&&...) { events.push_back("warn"); }
 template<class... T> void critical(T&&...) { events.push_back("critical"); }
} logger;
Logger* IDTLogger = &logger;
struct LaunchState { inline static bool warnTry = false; };
std::wstring GetCurrentExePath() { return L"fixture.exe"; }
void SetOffSignal(int value) {
 assert(value == 1); events.push_back("close");
}
void PublishFatalStartupFailure(unsigned code, const wchar_t*) {
 assert(code == 0xD0FEu);
 assert(!events.empty() && events.front() == "sleep800");
 assert(events[1] == "close");
 events.push_back("fatal");
}
HINSTANCE TestShellExecute(HWND, LPCWSTR, LPCWSTR file, LPCWSTR args, LPCWSTR, int) {
 assert(std::wstring(file) == L"fixture.exe");
 assert(std::wstring(args) == L"-WarnTry");
 events.push_back("spawn");
 return reinterpret_cast<HINSTANCE>(shellResult);
}
namespace std::this_thread {
 template<class Rep, class Period> void TestSleepFor(std::chrono::duration<Rep,Period> value) {
  assert(std::chrono::duration_cast<std::chrono::milliseconds>(value).count() == 800);
  events.push_back("sleep800");
 }
}
namespace Inkeys::UI::StartupPreview {
 bool IsActive() { return active; }
 void RequestFadeOutForExit() { events.push_back("fade"); }
 bool WaitForFadeOut(std::chrono::milliseconds value) {
  assert(value.count() == 500); events.push_back("wait500"); return fadeCommitted;
 }
 void Stop() { events.push_back("stop"); }
}
namespace Inkeys::UI::RenderPipeline {
 void Shutdown() { events.push_back("pipeline"); }
}
void TestFreeLibrary(HMODULE) { events.push_back("free"); }
void TestDeactivateActCtx(DWORD, ULONG_PTR) { events.push_back("deactivate"); }
void TestReleaseActCtx(HANDLE) { events.push_back("release"); }
void TestCoUninitialize() { events.push_back("com"); }
#define GetEnvironmentVariableW ReadEnvironment
#define ShellExecuteW TestShellExecute
#define sleep_for TestSleepFor
#define FreeLibrary TestFreeLibrary
#define DeactivateActCtx TestDeactivateActCtx
#define ReleaseActCtx TestReleaseActCtx
#define CoUninitialize TestCoUninitialize
'''

SUFFIX = r'''
#undef GetEnvironmentVariableW
#undef ShellExecuteW
#undef sleep_for
#undef FreeLibrary
#undef DeactivateActCtx
#undef ReleaseActCtx
#undef CoUninitialize

// 环境修改仅发生在该独立测试进程中，并恢复其原值。
struct EnvironmentScope {
 const wchar_t* name;
 std::wstring previous;
 bool existed;
 explicit EnvironmentScope(const wchar_t* value): name(value) {
  DWORD required = ::GetEnvironmentVariableW(name, nullptr, 0);
  existed = required != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
  if (required) {
   previous.resize(required);
   DWORD length = ::GetEnvironmentVariableW(name, previous.data(), required);
   assert(length < required); previous.resize(length);
  }
 }
 ~EnvironmentScope() { assert(SetEnvironmentVariableW(name, existed ? previous.c_str() : nullptr)); }
};
constexpr bool enabled =
#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
 true;
#else
 false;
#endif
const wchar_t* retryName = L"INKEYS_STARTUP_PREVIEW_RETRY_FAILURE";
const wchar_t* delayName = L"INKEYS_STARTUP_PREVIEW_MANUAL_DELAY";
void Set(const wchar_t* retry, const wchar_t* delay) {
 assert(SetEnvironmentVariableW(retryName,retry));
 assert(SetEnvironmentVariableW(delayName,delay));
 events.clear(); environmentReads=0; active=true; fadeCommitted=true; LaunchState::warnTry=false; shellResult=33;
}
void ExpectNormal() {
 assert(InvokeProductionCall() == 0);
 assert(events.empty());
 if (!enabled || !active) assert(environmentReads == 0);
}
int main() {
 EnvironmentScope retryScope(retryName),delayScope(delayName);
 const std::wstring overlong(16384,L'1');
 const wchar_t* values[] = {nullptr,L"",L"0",L"10",L"1x",L"true",L" 1",L"1 ",overlong.c_str()};
 for (const auto value : values) {
  Set(value,L"1"); ExpectNormal();
  Set(L"1",value); ExpectNormal();
 }
 Set(L"1",L"1"); active=false; ExpectNormal();
 Set(L"1",L"1");
 if (!enabled) {
  ExpectNormal();
  puts("manual retry hook: disabled/undefined compile gate has no fault side effects or env reads");
  return 0;
 }
 // 第一次正常色淡出后仅启动一次；此处为进程启动桩，不会产生子进程。
 assert(InvokeProductionCall() == 1);
 assert((events == std::vector<std::string>{"sleep800","close","warn","fade","wait500","stop",
  "spawn","free","deactivate","release","com","pipeline"}));
 assert(environmentReads == 2);
 const auto normalRetryEvents = events;
 Set(L"1",L"1"); fadeCommitted=false;
 assert(InvokeProductionCall() == 1 && events == normalRetryEvents);
 Set(L"1",L"1"); LaunchState::warnTry=true;
 assert(InvokeProductionCall() == 1);
 assert((events == std::vector<std::string>{"sleep800","close","critical","fatal",
  "free","deactivate","release","com","pipeline"}));
 for (INT_PTR error : {0,2,31,32}) {
  Set(L"1",L"1"); shellResult=error;
  assert(InvokeProductionCall() == 1);
  assert((events == std::vector<std::string>{"sleep800","close","warn","fade","wait500","stop",
   "spawn","critical","fatal","free","deactivate","release","com","pipeline"}));
 }
 Set(L"1",L"1");
 assert(InvokeProductionCall(RPC_E_CHANGED_MODE) == 1);
 assert(events[events.size()-2] == "release" && events.back() == "pipeline");
 puts("manual retry hook: exact dual Win32 env gate, inactive/normal zero effects, one retry, final failure, launch errors and cleanup passed");
}
'''


def extract_fixture(root: Path) -> str:
    source = (root / "Inkeys/IdtMain.cpp").read_text(encoding="utf-8-sig")
    start = source.index(GUARD + "\n\tbool RunStartupPreviewRetryFailureForManualTest()")
    end = source.index("\n\tbool WriteStartupPreviewSmokeReport(", start)
    runner = source[start:end]
    call_start = source.index(GUARD + "\n\tif (!RunStartupPreviewRetryFailureForManualTest())")
    call_end = source.index("\t// 自动更新初始化", call_start)
    call = source[call_start:call_end]
    assert source.rindex("Inkeys::Startup::Milestone::PptComReady", 0, call_start) < call_start < call_end
    assert source.count("ShellExecuteW(") >= 1
    assert "startupPreviewManualDelayRequested" not in runner
    assert runner.count("ShellExecuteW(") == 1
    # 复用真实调用块，包括 HRESULT 条件释放；只替换外部动作，不重写生产分支。
    wrapper = """
int InvokeProductionCall([[maybe_unused]] HRESULT comInitializeResult = S_OK) {
 [[maybe_unused]] HMODULE pptComModule = reinterpret_cast<HMODULE>(1);
 [[maybe_unused]] bool actCtxActivated = true;
 [[maybe_unused]] ULONG_PTR ulCookie = 1;
 [[maybe_unused]] HANDLE hActCtx = reinterpret_cast<HANDLE>(2);
""" + call + "\n return 0;\n}\n"
    return PREFIX + runner + wrapper + SUFFIX


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=shutil.which("cl"), help="Native MSVC cl.exe (developer environment required)")
    parser.add_argument("--generate-only", action="store_true", help="Generate fixture without compiling")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = root / "Build/StartupRetryHookValidation"
    output.mkdir(parents=True, exist_ok=True)
    source = output / "manual-retry-hook.cpp"
    source.write_text(extract_fixture(root), encoding="utf-8", newline="\r\n")
    if args.generate_only:
        print(f"Generated production replay: {source}")
        return 0
    if not args.compiler:
        parser.error("cl.exe not found; run from the native Visual Studio developer shell")
    for variant, definitions in (("enabled", ["/DINKEYS_ENABLE_UI3_PRESENTATION_HARNESS=1"]),
                                 ("disabled", ["/DINKEYS_ENABLE_UI3_PRESENTATION_HARNESS=0"]),
                                 ("undefined", [])):
        executable = output / f"manual-retry-{variant}.exe"
        subprocess.run([args.compiler, "/nologo", "/std:c++20", "/utf-8", "/EHsc", "/W4",
                        *definitions, str(source), f"/Fe:{executable}", f"/Fo:{output / (variant + '.obj')}"],
                       cwd=output, check=True, timeout=300)
        subprocess.run([str(executable)], cwd=output, check=True, timeout=30)
        preprocessed = output / f"manual-retry-{variant}.i"
        subprocess.run([args.compiler, "/nologo", "/std:c++20", "/utf-8", *definitions,
                        "/P", f"/Fi:{preprocessed}", str(source)], cwd=output, check=True, timeout=300)
        # fixture 主程序中有环境名字，但宏关闭产物不得保留生产 runner 定义/调用。
        if variant != "enabled":
            assert "RunStartupPreviewRetryFailureForManualTest" not in preprocessed.read_text(encoding="utf-8-sig")
    print("All three native production replay variants passed (no HWND or real process launch).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
