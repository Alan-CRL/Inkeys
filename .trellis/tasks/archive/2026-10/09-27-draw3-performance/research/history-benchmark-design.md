# Integrated Draw3 CPU history benchmark

## Entry and measured production APIs

`Inkeys.exe --draw3-history-benchmark` calls `RunDraw3HistoryBenchmark()` before normal window/configuration startup (the parent task registers the CLI and project files). The probe imports the product `ink_document` and `ink_history` modules and calls `InkCanvas::AppendStroke`, `BuildStrokeTileFootprint`, `CanvasRuntimeHistory::AppendStroke`, `UndoLastVisible`, `RedoLastUndone`, `DiscardRedoBranch`, `VisibleCompositionTiles`, and `CompositionRangeTree::DecomposeRange`. It does not instantiate a Draw3 Host, HWND, D3D device, UInk worker or presenter.

Six deterministic cases cover 10/100/1000 stored strokes with 64 points each, once all pen and once with every fourth stroke an eraser crossing the preceding three pen traces. Three warm-up blocks precede 11 measured blocks per case. Stroke inputs are copied before each block's timers. Each block emits raw nanoseconds for document append, footprint, history append, undo, redo, undo/new-branch operations and composition query, along with footprint tile totals, visible/retained/redo counts and a deterministic content/history digest. Windows process private bytes, working set and handle count are emitted before append, after append and after branching when the OS counters are available. Parent task will run three serialized process rounds and summarize median/P95 without treating the 11-block sample as a reliable P99.

The probe asserts actual sequential stroke and render IDs, redo order, redo invalidation after new branch, visible and retained counts, composition tree count, two distinct page GUIDs and isolation of the untouched page. The explicit finite extreme-point boundary check is outside timed blocks and emits `extreme_valid=1 footprint_rejected=1`: it confirms the currently separate document-validity/footprint representation domains, but does not exercise the DrawingController append transaction or prove real stylus reachability.

## Interpretation and remaining coverage

- Phase timers include clock call overhead and allocator effects inside each production API. Process memory counters are process-level snapshots, not ownership-attributed heap allocations; warm-up and CRT heap retention can obscure freed bytes. Raw output and toolchain/build identity are needed for every before/after comparison.
- This probe measures CPU document/history/tile work only. It cannot measure RTS sampling, queue latency, modeler, prediction, GPU upload, cache residency, successful Present, optical latency, UInk durability or visible pen feel. The mixed eraser case validates history bookkeeping, not erased pixels.
- It uses 64-point traces within a 2048px query view. Tap, 1024-point slow stroke, page switch/replay, long-running save queue, GPU cache hit/miss and multiple physical contacts require separate production tests. No Win7, FL11.0 Hardware/WARP or DComp/ULW result follows from this no-HWND CPU probe; current FLIP behavior is untouched.

## Verification state

At handoff, the implementer has performed static API/diff/encoding checks only. The parent task owns solution registration, serialized build, execution, raw-data capture and independent final review. Until those succeed, this benchmark and its correctness assertions are **未验证**.
