# Execution
User approved implementation; do not ask again.
- [x] Freeze baseline source and existing binaries/hashes.
- [x] P0 short independent GT benchmark with current baseline.
- [x] P1 tracker coarse matching/identity/relay/recovery and review; v18 repairs cumulative micro-motion membership, final v21 full regression confirms it.
- [x] P2 capture low information/schedule/timing; diagnostics and bounded recording.
- [x] P3 targeted iterations and retained long micro-motion safety gates; final 28/28 directed cases and full Tracker regressions pass.
- [x] P4 bounded live product/performance measurements and WGC decision completed. Locked-static Debug sampling is 2.57Hz; WGC pixel correctness fails, so no integration. This is not a realtime acceptance pass.
- [x] P5 final full solution and complete no-window exit0, protocol/owner/persistence regressions, encoding/diff checks, spec, journal and delivery evidence.
- [ ] Acceptance: effective moving-target 20Hz, compositor-visible P95 <=80ms and continuous moving-ink screen error.
- [ ] Acceptance: normal Edge/Word/PDF UI and interactive controls; Computer Use normal-start attempts exposed no targetable window.
- [ ] Device follow-up: hardware pen/touch, multiple monitors and old Windows.
See research/delivery.md; keep task in_progress until the open acceptance items are resolved.
Ownership: algorithm agent VisualMotionTracker.cpp/.h only; capture agent FreezeCapture.cpp/.h, IdtMagnification.cpp, capture tests; diagnostic agent Business/ScrollFollowDebug, UI/ScrollFollowDebug, debug tests. Main owns benchmark/replay, ScrollFollow business and Draw3 timing, all builds and GUI. Coordinate shared headers. No agents run builds/long tests/GUI independently.
Iterate short benchmark/debug-only. Final --no-window already includes full CV: do not also rerun full visual-motion matrix. Strict product protocol/hidden/persistence/live-ink entries and real Edge/Word/PDF as applicable.
Full InkeysRepo.sln Debug|ARM64 via vswhere native ARM64 MSBuild, same PowerShell PATH normalization and no node reuse; 10-minute budget. No standalone Inkeys.vcxproj, dependency upgrades or environment bypass. No automatic commit/push/PR/archive.
