# Existing Mag Host WGC sidecar PoC

2026-10-23. Preparation only. No compiler, probe executable, product harness, WGC session, GUI or computer-use action was run by the capture implementer. Main owns all launches and verification. Files are confined to `Build/ScrollFollow` and this research note. No production backend, project/dependency configuration, Mag owner, filter list, transform, window affinity, Freeze behavior or worker RPC is changed.

## Prior evidence and scope

The reusable source is `.trellis/tasks/10-21-scroll-follow-mag-capture/research/probes/quick_probe_grid.cpp`, with results in `research/quick-probe.md` of that task. Its corrected 640×480 experiment obtained all nine sampled points in 90/90 WGC Host frames, including 30 alpha-zero and 60 opaque frames, with roughly 900 ms settling. WGC child HWND returned E_INVALIDARG. This is motivation to test the existing **top-level Host**, not evidence of full-size pixel correctness, normal-speed freshness, throughput or current product exclusion. The earlier second-owner exclusion experiment visibly captured the excluded blue overlay and had Filter/error 50; do not create a second Mag owner or ignore that API result.

The product fixture already writes `[ProductLive] original_mag owner_tid=... host=... child=...` and `[ProductLive] source_paint sequence=... offset=(x,y) qpc=... hash=...`. `Draw3.ProductLiveTest.inl` supplies a deterministic TexturePixel formula, source-only paint attribution and QPC after the real BitBlt/GdiFlush. The sidecar reads those existing records. It requires the passed process PID, the dedicated source class, the exact Host/child classes and parent relationship, full primary client dimensions, source origin (0,0), Host origin (0,0), and the fixture's deliberate one-row crop. It never falls back to an arbitrary foreground window or desktop capture.

## Prepared files

- `Build/ScrollFollow/wgc-existing-mag-probe.cpp`: one source built as a minimal Win32 bootstrap EXE and a separately loaded C++/WinRT DLL. Bootstrap checks system combase and D3D11 exports before loading the adjacent DLL. Runtime separately checks WGC support, required activation factories, cursor-control interface and HWND capture. No WinRT header/library enters the bootstrap branch or production binary. The bootstrap design is intended to return unavailable on old systems; **Win7 execution and import-table inspection are still unperformed**, especially because this machine/build is ARM64.
- `Build/ScrollFollow/build-wgc-existing-mag-probe.ps1`: locates Visual Studio through vswhere and requires native Hostarm64/arm64 tools. Builds only these two independent /O2 experiment binaries. It does not build Inkeys or alter any project. It is not invoked during preparation.
- `Build/ScrollFollow/summarize-wgc-existing-mag-probe.py`: numeric CSV summary, per-phase correctness/coverage and readback/freshness timings. It always sets `production_integration_eligible=false` for this sidecar mode.

The runtime retains a two-frame WGC pool and a reusable staging texture, with bounded latest-only draining and no image queue. It checks every RGB pixel of every captured original-size content image against an independently logged source pose, including the final row and column; a 16-point signature selects a pose but cannot produce a correctness pass. Alpha is ignored because the current capture contract normalizes alpha to 255. RGB has zero tolerance. GPU texture padding is reported, while the exact requested content rectangle is fully checked. There is no resize/downscale, no buffer of saved screenshots and no disk image output. The maximum is 60 seconds, 10,000 observations, a 128 MiB single-frame size, 32 MiB of input log and 512 source-pose records.

The CSV reports actual WGC SystemRelativeTime, QPC at CPU Map readiness, full dimensions, row pitch, copy/Map readback service, separate full-pixel verification time, matching and latest source-paint sequence, duplicate source-pose ambiguity, first-seen source-paint-to-CPU latency, WGC frame age, drained frames, monotonic timestamps, Host visibility and alpha. Readback includes initial staging allocation. Source repeated poses cannot prove freshness and are excluded from fresh-pose latency statistics. QPC after source GdiFlush is an input paint observation, **not** compositor completion or visible ink latency. No 80 ms end-to-end claim can be based on this metric alone.

## Main-agent launch

First build with the prepared PowerShell script after the coordinated product build is idle. A product rebuild is not needed just to attach. Start the existing controlled `--scroll-follow-debug-product-test` or an explicitly bounded dedicated ProductLive phase, with stdout redirected to a unique log. The debug test has long enough phases for attachment; the short ordinary product test may end before the sidecar starts.

Invoke only after the stdout file exists; the sidecar waits up to 10 seconds for the verified PID, original Mag handles and a source-paint record. Use the product process's actual PID, not the shell PID, and a new CSV name. For example, from the repository directory:

```powershell
& .\Build\ScrollFollow\build-wgc-existing-mag-probe.ps1
# Main starts and owns the controlled product harness, obtaining $productProcess.Id.
$probeExe = (Resolve-Path '.\Build\ScrollFollow\wgc-existing-mag-probe.exe').Path
$liveLog = (Resolve-Path '.\Build\ScrollFollow\controlled-live.stdout.log').Path
$numericCsv = Join-Path (Resolve-Path '.\Build\ScrollFollow').Path 'wgc-existing-host-round1.csv'
$probeArguments = @([string]$productProcess.Id, ('"{0}"' -f $liveLog), ('"{0}"' -f $numericCsv), '60')
$probeProcess = Start-Process -FilePath $probeExe -ArgumentList $probeArguments -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput '.\Build\ScrollFollow\wgc-existing-host-round1.stdout.log' `
    -RedirectStandardError '.\Build\ScrollFollow\wgc-existing-host-round1.stderr.log'
# Main waits/checks the concrete process exit and retains its PID for bounded cleanup.
& 'C:\Users\alan-\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' `
    '.\Build\ScrollFollow\summarize-wgc-existing-mag-probe.py' $numericCsv
```

Neither executable creates an application window or captures anything except the verified Host. WGC may add its OS capture border. The observer stops on product process exit, incompatible window geometry/identity or input-desktop change. D3D Map and OS capture calls are synchronous and cannot be guaranteed to honor the outer wall-clock deadline if the graphics driver hangs; main must retain the exact sidecar process handle and a separate bounded launch watchdog. Do not kill a process by name or alter product cleanup. A zero probe exit means only that numeric observations completed; the CSV and summary decide what was observed. No-frame/error exits are nonzero, and missing bootstrap capability returns 4.

## Minimal optional fixture markers

No markers are needed to obtain unmarked full-pixel samples and timing. They are required to claim lifecycle/exclusion coverage. If main adds bounded **test-only** ProductLive checks, write this line through the fixture's existing serialized Log only after independently confirming the condition:

```text
[WgcProbePhase] name=before qpc=<QPC> source_sequence=<paintSequence> verified=1 excluded_mask=<0..7>
[WgcProbePhase] name=freeze qpc=<QPC> source_sequence=<actual-frozen-source-sequence> verified=1 excluded_mask=0
[WgcProbePhase] name=after qpc=<QPC> source_sequence=<paintSequence> verified=1 excluded_mask=<0..7>
```

For `before`/`after`, confirm actual Capture readiness/publishing, original owner/handles, visible alpha-zero Host, correct filters, and actual observed foreground source/geometry. `excluded_mask` is 1 for verified visible ink, 2 for verified open debug window, 4 for verified visible selection overlay, with a real screen/fixture proof of those pixels covering source content. Set a bit only while that overlay is actually present and proved; emit another marker when it changes. A configured filter list or an empty hidden overlay is not proof.

For `freeze`, use the normal business Freeze toggle and await the real coordinator revision, visible alpha-255 Host, capture-drained/occupied-release state, and expected freeze content proven against a known source paint. That actual frozen pose sequence is held as the full-frame oracle until the next marker. Continue source movement so a repeated still image cannot hide accidental live capture. Do not invent the frozen sequence from whatever WGC returns. For `after`, perform the normal release and await Capture reentry on the **same** owner/handles; drive at least ten distinct source poses. Marker QPC selects frames at or after the corresponding actual transition. The summary requires all three verified phases, a full-size frozen transition frame with the held pose (an unchanged WGC image need not emit repeated frames), at least ten fresh unique poses before and after, and all three overlay bits. Missing fixtures/markers remain incomplete rather than passing.

## Performance gate and next decision

**This attached sidecar cannot establish a causal replacement FPS benefit.** Production still executes PrintWindow and synchronous tracker work, and these continue to limit the Host update cadence. Running the sidecar adds readback/verification load. It can cheaply reject WGC on full-size correctness, exclusion, freshness or Freeze behavior and estimate the component readback cost, but it cannot turn a simultaneous PW/WGC comparison into an isolated backend result.

If correctness and lifecycle observations pass and timings justify further work, main may prepare a second **test-only existing-owner** producer experiment: identical controlled source trajectory, same primary geometry, 50 ms Update requests, 10 ms settling, identical exclusions/Freeze transitions; alternate complete PW baseline and WGC candidate runs without the other backend/callback simultaneously consuming the owner. All Mag calls remain on the existing owner, no second owner and no production backend switch. Measure first-visible unique valid content frames and first-seen paint-to-CPU P95 over the same steady windows, and separately retain owner service/CV/copy/verification costs. Do not bypass readiness/drain or weaken any product safety error to obtain a run.

Require `candidate_fps / baseline_fps >= 1.5` **and** `candidate_paint_to_cpu_P95 / baseline_paint_to_cpu_P95 <= 0.7`, alongside all full-pixel/current-content/exclusion/Freeze/repeated-start-stop/handle-drain checks and dynamic-capability/import/fallback verification. The baseline latency must have the same input/clock/boundary, not current `lastReadDuration` or source-paint-to-Present. Inapplicable or missing evidence fails the integration decision. Even a ratio pass below 20 Hz or with visible follow P95 over 80 ms cannot be called smooth. Any future backend choice must happen at session start with the original PW fallback; mid-session backend swapping is outside this probe.

## Preparation validation

Python AST parsing and PowerShell parser checks pass. The new C++ and PowerShell files are UTF-8 BOM/CRLF with no bare LF; the Python and Markdown files use UTF-8/LF. Static review checked the SDK-provided WGC interface names, TexturePixel constants and complete-pixel validation loop. Existing modified capture files still pass git diff --check. These are preparation checks only: C++ compilation, PE import audit, actual capture results and old-Windows runtime behavior are unverified. Build/ is ignored by Git, so main must intentionally retain the probe source/scripts as task evidence if they are needed beyond this workspace; no force-add or commit was performed. No build or runtime claim belongs to this record until main appends concrete exits, binary hashes and measured files.


## Main-agent preparation evidence (2026-10-24)

The independent bootstrap EXE and runtime DLL were successfully compiled by the main agent using the prepared script. Native ARM64 dumpbin /imports now completed with exit0 for both; bootstrap imports only KERNEL32.dll, while WGC/WinRT/D3D imports reside in the dynamically loaded runtime DLL. Build/ScrollFollow/wgc-bootstrap-imports.log, wgc-runtime-imports.log and wgc-poc-binaries-sha256.json preserve the actual imports and binary hashes. This improves the static separation evidence only: old-Windows execution, full-pixel capture, exclusion, lifecycle and causal performance gates remain unverified. No production backend was integrated.


## Bounded device result (2026-10-24): rejected

The first invocation reached CreateForWindow before the existing Host was visible and returned E_INVALIDARG. A bounded retry waited for the same original Host and Child to be visible, then successfully opened the item, frame pool and session. It observed26 full2880x1919 frames; only1 matched every expected source pixel. Remaining frames differed, including57444-pixel border-sized early mismatches (cause unproven). It stopped when the product destroyed the original HWND. Numeric CSV, API-stage logs and wgc-existing-host-v18-ready-summary.json retain the facts.

Full-pixel correctness therefore fails. Freeze/exclusion/freshness and causal replacement performance are not established, and no production WGC backend is integrated. Component Map/readback timing (P50 about1.69ms/P95 about8.4ms) and simultaneous-sidecar5.96Hz cannot pass the required performance gate. No crop, border removal or security/permission setting change was used to make the observation pass. No further WGC experiment is needed for this implementation decision.
