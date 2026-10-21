# 2026-10-21 quick Mag / PrintWindow / WGC probe

User authorized this turn's 30-minute dedicated self-closing GUI experiments. No production changes, capture-exclusion affinity, or user-window manipulation. Source: probes/quick_probe.cpp. Main session owns Computer Use observation.

## Procedure
Single STA owner calls MagInitialize once. Known-color source HWND (80,100), Mag Host (800,100), child640x480, identity transform, excludesHost. Host is layered, shown, nonactivating/toolwindow, alpha255/0, not minimized/offscreen. Source repaints uniqueRGB each phase, records painted phase. After source paint pump100ms, SetWindowSource+Redraw then pump800ms. PrintWindow child flags0/2 and WGC CreateForWindow source/MagHost. Drain latest available WGC frame. Pixel(100,100) avoids text. Pixel match is freshness evidence, not whole-image validation.

## Completed results
Exploratory run1: Build/ScrollFollow/quick-probe-run1.log, PID56504/session53488, exit0, UninitializeTRUE. Opaque/alpha0/opaque80 phases each. PrintWindow2/WGC changed red-green, but two-state source and FIFO permitted stale ambiguity. Not per-phase proof. PrintWindow0 FALSE left sentinel0xcc.

Corrected run3: Build/ScrollFollow/quick-probe-82708.log, PID82708/session33359, exit0, UninitializeTRUE. Cold alpha0 then opaque255 then alpha0,30phases each. Thread/InputDesktop Default, session1. Mag init/filter/transform/source/redraw successful. Painted-phase90/90. PrintWindow2 current uniqueRGB90/90, WGC source90/90, WGC MagHost90/90. Transparent60/60 and opaque30/30. PrintWindow0 FALSE90/90 untouched sentinel. WGC Mag child HWND CreateForWindow E_INVALIDARG(0x80070057), top-levelHost succeeds. No opaque prewarm required.

## Interpretation
Alpha0Mag and PrintWindow2 CAN return correct changing pixels: both worked under singleSTA at640x480/~900ms settling. This does not resolve cross-owner Filter/error50, full2880x1920/20FPS, production stability or oldWindows. WGC ordinaryHWND is an experimental option, no production replacement. ChildHWND and top-levelHWND differ. WGC is not independent physical-display observation; main session must record any ComputerUse evidence separately.

## Reproduce
Existing ARM64 vcvars environment:
```
cl /nologo /utf-8 /std:c++20 /DUNICODE /D_UNICODE /EHsc /W3 .trellis\tasks\10-21-scroll-follow-mag-capture\research\probes\quick_probe.cpp /FoBuild\ScrollFollow\quick_probe.obj /FeBuild\ScrollFollow\quick_probe4.exe /link user32.lib gdi32.lib d3d11.lib dxgi.lib windowsapp.lib
```
Noarg opaque/alpha0/opaque; anyarg alpha0/opaque/alpha0. Latest source60phases/stage (~54s), run3 used30. Logs auto Build/ScrollFollow/quick-probe-<PID>.log; latest source permits realtime reading. Normal exit closes windows/WGC/device/runtime. No permanently blocked systemcall test. Fatal process exit OS cleanup is not proof of graceful release.
## Follow-up no-WGC isolation (extended authorization, 10:13-10:16 UTC)
The earlier probe opened WGC sessions before PrintWindow. Those sessions may influence rendering lifecycle, so the earlier90/90 is not proof of PrintWindow-alone behavior. Separate pw_only_probe.cpp includes/calls no WGC/D3D APIs; links only user32/gdi32/ole32 plus CRT. Global FILE logging works without a console. Root abstained from sky calls during these three native runs.

All sessions: first global Initialize in STA, alpha0 set before ShowWindow, identity transform, source repaint with uniqueRGB+painted verification, then source/update, message pumping for listed settling time and PrintWindow2. 20phases each50/200/800ms, contiguous sequence (only first50ms phase is a fresh cold startup). DIB prefilled0xcc. Timing GetTickCount64 is coarse (~15.6ms quantization).

| PID / mode | 50ms | 200ms | 800ms | Exit / details |
| --- | --- | --- | --- | --- |
|60216 singleowner640x480|19/20|20/20|20/20|exit1,59/60. First phase got32,32,32 instead of67,101,127; buffer was written (not0xcc). FilterTRUE/error0.|
|48272 singleowner2880x1920|19/20|19/20|19/20|exit1,57/60. First phase32gray; phase37 returnedphase36, phase50 returnedphase49. FilterTRUE/error0.|
|56596 twoowner640x480|20/20|20/20|20/20|exit0 on pixel-only assertion. FilterFALSE/error50 remains! Minimal probe deliberately continues Source/PrintWindow despite filter failure; production still correctly rejects this.|

PrintWindow mean/max milliseconds by50/200/800ms stages:
- small14.10/16,15.65/32,13.95/16.
- full43.90/63,56.25/94,84.95/109.
- two93.80/94,109.40/125,124.95/140.

Logs Build/ScrollFollow/pw-only-60216.log, pw-only-48272.log, pw-only-56596.log; all resource cleanup UninitializeTRUE. Twoowner retains first-main-thread hidden Mag control until worker stops; globalInitialize once, no perworkerInitialize/earlyUninitialize. MainCOM0, workerCOM0, distinct threadIDs62244/40952. Sameowner nestedCOM returnsS_FALSE1 with balanced cleanup.

Crucial interpretation: Filter50 does not prove Mag cannot produce pixels. Readback can succeed while exclusion capability is unverified. Alpha0 Host itself does not occlude source, so successful pixels cannot demonstrate exclusion. Never remove production API check to force a pass. Fullscreen also occasionally returns previous phase despite800ms wait, so no universal current-image synchronization guarantee was established.

## Additional executables
- quick_probe5.exe: no arguments small; full=primary dimensions; two=hiddenfirstowner thenworker. Source pw_only_probe.cpp.
- quick_probe6.exe: explicit visual variant source quick_probe_visual.cpp; MagHost APPWINDOW for sky enumeration, FILE logging,80phases/stage~72sec. WGC enabled; classify separately from noWGC baseline.
- quick_probe7.exe: compiled dedicated blue covering-window exclusion experiment, source pw_only_overlay.cpp; not yet run at this writing. Single vs two owner,200ms20phases. Requires currentvisual run to end first.
## Correctness confounds and corrected exclusion experiment
Initial quick_probe/visual/noWGCbaseline used same source-color Proc for SOURCE and Host. This is a confound for WGC Host (and painted attribution); single-point matches do not prove full ROI. Root also visually observed differing lower region in an opaque Mag screenshot. Do not infer all pixels correct from earlier sampled(100,100). Corrected pw_only_overlay.cpp tagsHost fixeddark12,12,12, overlay fixedblue20,70,220; painted onlyupdatedbySOURCE. Nine samplepositions x40/320/600 by y40/240/440; excludeHost+overlay coveringentire640x480.

- Singleowner PID37292: FilterTRUE/error0; source/host/child WindowRects0,0,640,480; sourceclient640x480 origin0,0; DPI192; MagGetWindowSource0,0,640,480. First phase200ms startupgray/black; phases2..20 exact currentunique source atall9points,19/20 currentphase. Blue overlay effectively removed atthose points. exit1 preserves firstfailure.
- Twoowner PID36992: firsthiddenMag retained, globalonceInitialize, distinctSTAowners; FilterFALSE/error50. Same geometry/DPI. ALL20phases/all9points read overlayblue20,70,220;0/20 current source matches. Thus exclusion actually fails; successfully continuingPrintWindow alone is insufficient. exit1.
- Both processes normalUninitializeTRUE andclosedwindows. Logs Build/ScrollFollow/pw-only-37292.log andpw-only-36992.log.

This is stronger evidence than merely trusting FilterBOOL: same-location positivecontrol removesblue, crossowner negativecontrol retainsblue. No production checkrelaxed. Attribution remains Magexclusion/owner setup beforePrintWindow ratherthan PrintWindow failingto readpixels.

quick_probe8.exe / quick_probe_grid.cpp compiled: correctedHostfixedgray12, source-onlypainted,9pointPrintWindow/WGC, geometry/DPI log. No runyet atthiswriting.
## Corrected WGC / child PW grid result (final, 10:23 UTC)
quick_probe8.exe PID50504/session84657 completed exit0, MagUninitializeTRUE. Log Build/ScrollFollow/quick-probe-50504.log. Root made no sky calls during this native experiment. SOURCE physical WindowRect80,100,720,580/client640x480/clientOrigin80,100; Host800,100,1440,580; GetDpiForWindow192. RequestedSource80,100,720,580. Host fixedgray12 vs uniqueSOURCE, painted attributable onlytoSOURCE. 30opaque/30alpha0/30opaque phases with~900mssettle.

PrintWindow2 child, WGC source, WGC MagHost each had90/90 frames with ALL9 sampledpoints exactly matchingcurrentphaseRGB (810/810 samplepoints perpath). Transparent30/30frames, opaque60/60. PrintWindow0 remainedFALSE/sentinel. WGC childHWND stillE_INVALIDARG. This corrected independent native result did not reproduce the earlier visually observed lowerregion mismatch atthoseninepositions. It does not prove everypixel or identify the causeof the earlier visualdifference. Noorigin0 rerun required because this sampledROI had noanomaly. No newproductionfix.

All native probe runs now stopped; no windows/threads from these test executables remain according to returnedprocess exits andnormalcleanup. Main session owns any remaining desktop-observation verification. Sourcevariants retain reproducibility; don't combine countsfromdifferentstyles/settling/WGCpresence intoonepassrate. No screenshot data fromuserwindows savedbythisagent.