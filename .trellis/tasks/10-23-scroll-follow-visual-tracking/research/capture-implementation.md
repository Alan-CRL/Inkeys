# P2 capture implementation

2026-10-23. Active task: `.trellis/tasks/10-23-scroll-follow-visual-tracking`. Capture implementer owns only FreezeCapture.cpp/.h, IdtMagnification.cpp and freeze_capture_tests.cpp. No builds, executable tests, GUI, WGC sessions, commits or remote writes were run by this agent; main owns coordinated validation.

## Behavior

- `LowInformation` is delivered with unchanged quality and a strictly increasing frame sequence. `accepted` counts transport delivery/liveness; `lowInformation` counts that delivered subset. It is not a trusted-motion counter. NearBlack/InvalidBuffer still increment rejected without invoking the consumer; Read API failures retain rejected+apiFailures, and Begin/Update API/geometry failures keep existing decisions. A later low-information frame does not reset previous error counters, failure stage or failure time.
- Capture's one-second deadline is now explicitly delivery/liveness. Low-information deliveries refresh it; black/invalid/API failures do not. No deliveries for one second still revoke, including a blocked admitted consumer. Business's genuine Tracker initialization gate and five-second startup gate, and Tracker's independent bounded trusted-evidence recovery, remain necessary. Main/algorithm agents own those changes.
- Steady cadence starts at actual Update invocation. Update + 10ms settling + Read + Analyze + synchronous callback consume the same 50ms budget. A late completed cycle permits an immediate new Update rather than rounding up to a future slot. Each new actual Update establishes the next cycle; no FIFO/catch-up reads. The first 800ms warmup is retained and excluded from missed-period statistics. Skipped now counts nominal periods missed by completed service; callback service is included. Failed backend operations retain the existing 100ms retry path.

## Timing interface

`TimedStage { std::chrono::microseconds duration; bool observed; }` distinguishes an executed zero-duration stage from an unexecuted stage. `BackendTimings { redraw, printWindow, gdiFlush, pixelWork }` is appended to `BackendResult::timings`. Existing aggregate initialization remains source-compatible.

`Statistics` appends `lowInformation`, `update/analyze/callback` TimedStage fields, `backend` BackendTimings, and `callbackSequence`.

- Update wraps the complete Backend::Update. Redraw wraps the actual RedrawWindow call before its existing trace observer.
- PrintWindow and GdiFlush are individually measured, retaining the original sequence, including GdiFlush after failed PrintWindow.
- pixelWork sums clear-DIB and alpha repair only; partial execution is measured on failure. Geometry validation remains inside original lastReadDuration, outside these sub-stages.
- lastReadDuration retains the complete Backend::Read boundary, before Analyze. Analyze measures the actual quality analysis and is unobserved on API failure.
- callback is measured outside the state lock and recorded after callback return/throw, excluding exception cleanup. callbackSequence identifies that actual completed callback. Frame.statistics contains the preceding completed callback; it cannot claim the unfinished current callback's duration. Snapshot after return contains the current completed callback. For a same-frame sum use Frame statistics and an explicitly matching callback sequence; live Snapshot latest Update can already belong to the next attempt.
- All timing and errors remain by-value numeric observations. No new worker, image copy, HWND, Mag runtime, owning buffer or owner RPC is introduced. The original inFlight/occupied/shared lifetime and drain gates remain intact.

## Directed matrix changes

The existing capture short matrix is expanded, not a second runner:

- A deterministic 7ms Update + 10ms settle + 20ms Read + 5ms consumer leaves only 8ms of a 50ms period. A 95ms consumer overruns and permits immediate Update; the following short cycle resumes without backlog. A 170ms Read also permits immediate next Update but still requires its fresh 10ms settling before Read.
- First-frame warmup remains 800ms after Update completion; warmup is not counted as missed work.
- Timing propagation, validity clearing on failed/unreached stages, and previous/current callback identity are checked.
- Thirty-one low-information white frames deliver with quality and increasing identities, zero rejection/API failures, and live capture beyond one second. Stopping delivery still times out after one second.
- NearBlack and InvalidBuffer remain rejected with no consumer invocation; intervening white frames cannot hide prior severe failure counters/timestamps. Severe failures do not extend delivery timeout; the exact 999/1000ms boundary is checked.
- Existing startup-black timeout, API failures, geometry change, Freeze priority, blocked Read cancellation, real supervisor/borrowed-callback timeout, stop wake and terminal cleanup cases remain.

## Validation status and limits

`git diff --check` passed for the four owned source/test files. Physical-byte inspection confirms all four remain UTF-8 BOM with CRLF and no bare LF. Diff is confined to capture quality admission, cycle scheduling, stage observations and their focused tests. No project/dependency files changed.

Compilation and runtime assertions are **pending main-agent execution**, preferably the existing `InkeysHeadlessTests.exe --freeze-capture-only` after the coordinated build. The new timing fixture validates Session observation semantics using its fake backend; it does not measure real Win32 costs. No FPS improvement, current-pixel freshness, real desktop exclusions, Freeze visible pixels, WGC viability or old-Windows behavior is claimed by this implementation record. Real phase timings and same-input comparison decide whether the bounded current-Mag-Host WGC PoC is necessary.
