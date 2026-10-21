# Quick probe final read-only review — 2026-10-21

This supersedes the preliminary review of run3. Reviewed research/probes/quick_probe.cpp, quick_probe_visual.cpp, quick_probe_grid.cpp, pw_only_probe.cpp, pw_only_overlay.cpp, quick-probe.md, and the logs below. No GUI launch, compile, product edit, or commit performed by reviewer. Root owns Computer Use observations and other agents own their edits.

## Verified final evidence

- Corrected grid log `Build/ScrollFollow/quick-probe-50504.log`: 90 phase rows with 90 distinct expected RGB values; painted phase matches requested phase throughout. PW2, WGC source and WGC Mag Host each match current expected RGB at all nine grid positions in all 90 rows (810 comparisons per method; zero mismatches). Stages are opaque/alpha0/opaque: 30 alpha0 samples. Host paints fixed (12,12,12), SOURCE alone updates painted; source/Host same-color confound is removed here. Mag child WGC attempt returns E_INVALIDARG; top-level Host succeeds. All measured frames are 640x480.
- Earlier run3 `quick-probe-82708.log`: 90 unique phases, 90/90 sampled-point matches for each method, 60 alpha0 samples beginning cold alpha0. That evidence is single-point and retains same-color Host Proc confound; corrected grid result is stronger, but does not replace cold-alpha0 with a corrected cold-alpha0 grid experiment.
- No-WGC `pw-only-60216.log`: 59/60 sampled-point matches; first phase is written gray (32,32,32), distinct from untouched 0xcc sentinel. Fullscreen `pw-only-48272.log`: 57/60, including first gray and two prior-phase results. `pw-only-56596.log`: 60/60 sampled matches despite Filter FALSE/error50. These sources contain no WGC/D3D path, addressing the earlier concern that every successful PrintWindow run had concurrent WGC. They still use same-color SOURCE/Host Proc and do not prove exclusion.
- Corrected no-WGC overlay `pw-only-37292.log`: Filter TRUE; 19/20 phases current at all nine positions, first phase not ready. Fixed blue covering overlay excluded at those positions. Corrected two-owner `pw-only-36992.log`: Filter FALSE/error50; 0/20 current; all nine positions blue (20,70,220) in every one of 20 phases. SOURCE alone updates painted. This provides an exclusion positive/negative control, not merely API return-value evidence.
- All these logs report final Uninitialize TRUE. Runner-reported exits must be read separately; the probes preserve first mismatch as exit1. No production API checks were relaxed.

## Interpretation and remaining limitations

- PrintWindow(flags=2) can read changing Mag pixels without WGC. Alpha0 alone does not prevent readback. Neither success nor gray/old pixels identifies the precise internal Mag refresh stage; no universal frame-ready synchronization was established.
- Cross-owner failure concerns exclusion capability before PrintWindow: continuing readback can produce pixels, but corrected overlay experiment proves unwanted overlay remains. Do not treat two-owner 60/60 in uncovered test as a product-gate pass.
- Corrected nine-point tests are stronger than one pixel, but still not whole-frame identity/corruption validation. WGC is an API readback, not independent physical display evidence. Root's Computer Use observations are recorded separately; reviewer did not operate UI or inspect its screenshots here. Lower-region visual discrepancy must not be claimed solved from point counts.
- Corrected-grid WGC/PW measurements run together; no-WGC corrected-overlay experiment separately establishes a working PW2 route. Relative WGC performance, lock-screen behavior, Win10 version floor, source-window occlusion, full-screen exclusion and steady 20FPS remain unproven. No affinity exclusion was introduced.
- Fullscreen observed occasional old phase even at800ms settling; small corrected overlay first200ms gray. Fifty-millisecond settling plus readback is not20FPS, and GetTickCount64 timing is coarse. No new production reliability guarantee, blocked-system-call test, or long-run memory/resource bound.
- Reproduction: quick_probe.cpp uses60phases/stage (~54s per stage, ~162s total minimum); run3 used30/stage (~81s total). Corrected quick_probe_grid.cpp uses30/stage. quick_probe_visual.cpp differs in duration/APPWINDOW/logging. Retained sources and report need their own executable/mode mapping; do not describe all runs as the same binary.
- Experimental sources omit robust null/API/dimension checks and graceful exceptional cleanup; keep as standalone investigation artifacts, not production implementations.

## Verification

- Read-only source/log audit: pass with above limits.
- Parsed current phase/grid comparisons: pass; recorded transient/exclusion failures preserved.
- New GUI/process tests, compile/type-check and lint: not run by reviewer, per parent scope/time instruction. Static production rebuild unnecessary for research-only additions.