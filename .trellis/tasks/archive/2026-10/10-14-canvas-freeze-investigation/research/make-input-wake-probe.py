"""Generate a headless probe from production source, without compiling or running it.

Only C++ module/export wrappers are removed from ContactInput.  The contact
coordinator, wake functions, SpeedEraser implementation, ingress-drain template,
and the stalled controller wait loop are reused verbatim.  The WindowController
stub supplies only exit/pending-command flags; this is not a full controller or
RTS integration test.  Generated output stays under Build/CanvasFreezeInvestigation.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import re


RESEARCH = Path(__file__).resolve().parent
# 归档层级会变化，以 solution 标记定位仓库。
ROOT = next((p for p in RESEARCH.parents if (p / "InkeysRepo.sln").is_file()), None)
if ROOT is None:
    raise RuntimeError("Repository root (InkeysRepo.sln) was not found.")
DRAW = ROOT / "Inkeys/Inkeys/Drawing/Draw3"


def read_source(name: str) -> str:
    return (DRAW / name).read_text(encoding="utf-8-sig")


def strip_module_wrapper(text: str, expected: int) -> str:
    result, count = re.subn(
        r"(?m)^(?:export )?module(?: Inkeys\.Drawing\.Draw3\.contact_input)?;[ \t]*$",
        "",
        text,
    )
    if count != expected:
        raise RuntimeError(f"Unexpected module wrapper count: {count}")
    return result


header = strip_module_wrapper(read_source("Draw3.ContactInput.cppm"), 2)
if header.count("export namespace Inkeys::Drawing::Draw3") != 1:
    raise RuntimeError("Unexpected ContactInput export namespace")
header = header.replace("export namespace Inkeys::Drawing::Draw3", "namespace Inkeys::Drawing::Draw3")
implementation = strip_module_wrapper(read_source("Draw3.ContactInput.cpp"), 2)
controller = read_source("Draw3.DrawingController.cpp")
drain = re.search(
    r"(?m)^\t\ttemplate<class Process>\n\t\tvoid DrainIngressBatch[\s\S]*?^\t\t}\s*$",
    controller,
)
if drain is None:
    raise RuntimeError("Cannot locate the original DrainIngressBatch template")
wait_loop = re.search(
    r"while\(!window_\.ExitRequested\(\) && !input_\.WaitForWake\(frameWakeGeneration,1000\.0\)\) \{\}",
    controller,
)
if wait_loop is None:
    raise RuntimeError("Cannot locate the original eraser idle wait loop")

prefix = r'''
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <windows.h>
'''
stub = r'''
namespace Inkeys::Drawing::Draw3
{
    // 测试替身只提供被摘录函数读取的两个标记，不创建 HWND。
    class WindowController
    {
    public:
        std::atomic<bool> exit = false;
        std::atomic<bool> pending = false;
        bool ExitRequested() const { return exit.load(std::memory_order_acquire); }
        bool HasPendingCanvasCommand() const { return pending.load(std::memory_order_acquire); }
    };
'''
wait_wrapper = r'''
    void OriginalControllerIdleWait(ContactInputCoordinator& input_,
        WindowController& window_, uint64_t frameWakeGeneration)
    {
'''
test = r'''
    }
}

using namespace Inkeys::Drawing::Draw3;
using namespace std::chrono_literals;

namespace
{
    int failures = 0;
    void Check(bool okay, const char* description)
    {
        std::printf("%s %s\n", okay ? "PASS" : "FAIL", description);
        if (!okay) ++failures;
    }
    ContactSnapshot Sample(float x, ContactPhase phase)
    {
        ContactSnapshot result;
        result.position = {x, 40.0f};
        result.phase = phase;
        result.source.kind = SpeedEraser::SourceKind::Touch;
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        result.qpc = qpc.QuadPart;
        return result;
    }
    ContactHandle BeginTouch(ContactInputCoordinator& input)
    {
        Check(input.PublishDown(11, 17, InputDeviceType::Touch,
            Sample(10.0f, ContactPhase::Down)), "Touch Down published");
        ContactRecord* record = nullptr;
        Check(input.TryDequeue(record) && record, "Touch Down consumed");
        input.WaitForFrameDeadline(3.0); // 使用原码等待，消费已有 event 信号。
        return {record, record ? record->Generation() : 0};
    }
    void CheckActualEraserGate()
    {
        SpeedEraser::DisplayScale display;
        display.pixelWidth = 1920;
        display.pixelHeight = 1080;
        display.logicalOutputKnown = true;
        SpeedEraser::InputSource source;
        source.kind = SpeedEraser::SourceKind::Touch;
        SpeedEraser::InputSettings settings;
        const auto resolved = SpeedEraser::ResolveInput(display,
            SpeedEraser::DeviceMode::LargeScreen, source,
            SpeedEraser::InputEntry::Touch, settings,
            SpeedEraser::EraserToolPolicy::Speed);
        SpeedEraser::Controller eraser;
        eraser.Reset(10.0f, 40.0f, 100.0, SpeedEraser::StartKind::Touch,
            resolved.config);
        for (int step = 1; step <= 2400; ++step)
            eraser.Advance(100.0 + step / 120.0);
        Check(resolved.kind == SpeedEraser::EraserKind::Speed,
            "Product ResolveInput admits Touch Speed eraser");
        Check(!eraser.NeedsAnimation(120.0) &&
            eraser.SecondsSinceMovement(120.0) >= eraser.Configuration().idleStartSeconds &&
            eraser.NextAreaWakeSeconds() == 0.0,
            "Real SpeedEraser reaches idle gate with no area deadline");
    }
    void CheckMoveAndTerminal(bool boundary)
    {
        ContactInputCoordinator input;
        WindowController window;
        const auto handle = BeginTouch(input);
        if (boundary)
        {
            Check(input.TryReserveCommandWake(), "Reserve actual command wake");
            input.PublishReservedCommandWake();
            window.pending = true;
            bool commandBoundaryPending = true;
            int consumed = 0;
            DrainIngressBatch(input, window, commandBoundaryPending,
                [&](ContactRecord*) { ++consumed; });
            Check(consumed == 0 && input.HasPendingWork(),
                "Original DrainIngressBatch leaves pending ingress at command boundary");
            input.WaitForFrameDeadline(3.0); // 信号消费后，队列依旧保留。
        }
        const auto generation = input.CaptureWakeGeneration();
        std::atomic<bool> entered = false;
        std::atomic<bool> completed = false;
        std::thread waiter([&]
        {
            entered.store(true, std::memory_order_release);
            OriginalControllerIdleWait(input, window, generation);
            completed.store(true, std::memory_order_release);
        });
        while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
        std::this_thread::sleep_for(20ms);
        Check(input.PublishMove(11, 17, Sample(220.0f, ContactPhase::Move)),
            "Real PublishMove succeeds without cursor/hidden-test helper wake");
        Check(input.CaptureWakeGeneration() == generation,
            "Move does not advance wake generation");
        ContactSnapshot latest;
        Check(input.TryReadSnapshot(handle, latest) && latest.position.x == 220.0f &&
            latest.phase == ContactPhase::Move, "Updated Move is readable while consumer waits");
        std::this_thread::sleep_for(1200ms); // 超过原码 while 的单次 1000ms 等待。
        Check(!completed.load(std::memory_order_acquire),
            boundary ? "Boundary: original while still waits after timeout and Move" :
                "Original while still waits after timeout and Move");
        Check(input.PublishUp(11, 17, Sample(240.0f, ContactPhase::Up)),
            "Real Up reliably closes route and signals wake");
        for (int step = 0; step < 200 && !completed.load(std::memory_order_acquire); ++step)
            std::this_thread::sleep_for(5ms);
        Check(completed.load(std::memory_order_acquire),
            boundary ? "Boundary: Up exits original idle wait despite pending ingress" :
                "Up exits original idle wait");
        // 即使断言失败也释放实验线程；不以进程永久挂起作为测试机制。
        window.exit = true;
        input.PublishControlWake();
        waiter.join();
        Check(input.TryReadSnapshot(handle, latest) && latest.phase == ContactPhase::Up,
            "Terminal remains readable independently of command dequeuing");
        input.DiscardUntilTerminal(handle);
    }
    void CheckConsumedEventGenerations()
    {
        ContactInputCoordinator input;
        const auto oldGeneration = input.CaptureWakeGeneration();
        input.PublishControlWake();
        input.WaitForFrameDeadline(4.0); // event 可被这里消费；单调代次仍保留。
        Check(input.WaitForWake(oldGeneration, 5.0),
            "Old generation detects wake even after frame-deadline wait consumed event");
        const auto currentGeneration = input.CaptureWakeGeneration();
        Check(!input.WaitForWake(currentGeneration, 10.0) && input.HasPendingWork(),
            "Current-generation wait times out even with pre-existing queued work");
    }
}

int main()
{
    CheckActualEraserGate();
    CheckMoveAndTerminal(false);
    CheckMoveAndTerminal(true);
    CheckConsumedEventGenerations();
    std::printf("probe_failures=%d\n", failures);
    return failures ? 1 : 0;
}
'''


def line_directive(name: str, line: int = 1) -> str:
    return f'\n#line {line} "{(DRAW / name).as_posix()}"\n'


output = ROOT / "Build/CanvasFreezeInvestigation/input-wake-probe.generated.cpp"
output.parent.mkdir(parents=True, exist_ok=True)
content = (
    prefix
    + line_directive("Draw3.ContactInput.cppm")
    + header
    + line_directive("Draw3.ContactInput.cpp")
    + implementation
    + '\n#include "Draw3.SpeedEraser.cpp"\n'
    + stub
    + line_directive("Draw3.DrawingController.cpp", controller.count("\n", 0, drain.start()) + 1)
    + drain.group(0)
    + wait_wrapper
    + line_directive("Draw3.DrawingController.cpp", controller.count("\n", 0, wait_loop.start()) + 1)
    + wait_loop.group(0)
    + '\n#line 1 "input-wake-probe-harness"\n'
    + test
)
output.write_text(content, encoding="utf-8", newline="\n")
print(f"generated={output}")
for name in (
    "Draw3.ContactInput.cppm",
    "Draw3.ContactInput.cpp",
    "Draw3.SpeedEraser.h",
    "Draw3.SpeedEraser.cpp",
    "Draw3.DrawingController.cpp",
):
    print(f"source_sha256 {name} {hashlib.sha256((DRAW / name).read_bytes()).hexdigest()}")
print("Compile this single translation unit with the existing Windows ARM64 C++20 toolchain.")
print(f"Include paths: {DRAW}; {ROOT / 'VcpkgInstalled/Arm64/arm64-windows-static-v143/include'}")
print("Libraries: user32.lib kernel32.lib. No product build, IFCs, modeler, renderer, or GUI are needed.")
