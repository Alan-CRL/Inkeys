# Implementation: batched screen oracle for controlled live tests

- Date:2026-10-23. Test-only change; task remains in progress. No GUI, commit/push or task finish performed by this implementation step.
- Source: `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`, only `ScreenSourceGt`.

## Motivation and evidence boundary

The previous real long test completed16 downward steps in93.672seconds, then the root session cancelled its owned source window normally (PID30628, exit1/143.324seconds, private root `product-live-30628-7972109`). It did not complete the long roundtrip and is not a PASS. The last frame facts remained trusted rather than Paused; accepted frame609 first reported zero initialization survivors and later610..661 continued accepted tracking. Root/checker retain those raw facts separately.

The old oracle performs99 screen GetPixel calls and one screen ROI BitBlt per invocation. `InkWheel` invokes it before movement and after convergence, resulting in198 screen GetPixel calls per step. Repeated readback synchronization was a plausible test bottleneck, but no old per-phase timing receipt establishes it as the unique cause. This change does not alter product capture FPS or infer that Mag/PrintWindow was slow.

## Minimal change and safety

The oracle now makes one SRCCOPY screen BitBlt into a local top-down32-bit BI_RGB DibSurface, followed by GdiFlush. GetDC, DIB resize, BitBlt or flush failure rejects immediately; missing pixels are never replaced by zeros. GetDC is released on either path and the surface uses its existing RAII lifetime.

The exact same99 coordinates and RGB equality checks are evaluated from that buffer. The same original(64,64,256,192) ROI is still checked at every pixel against the unchanged immutable world texture. This uses one coherent read rather than99 separate sample times; it does not skip points or loosen tolerances. DibSurface layout and channel conversion were confirmed in existing Surface.cpp: negative biHeight is top-down, pixels are32-bit RGB, and RGB conversion retains the original COLORREF order.

Original CheckDesktop and paintSequence/painted-offset prerequisites remain. A second CheckDesktop runs after the copy, before inspecting/saving pixels, so newly observed safety layers abort. Original bounded mismatch metadata (first3 sample failures, first ROI pixel, current source/window identity) remains. **The full snapshot has no save/export path on success or failure.** Only after all99 samples and all49152 ROI pixels pass, an optional original crop is copied from memory, alpha is set255 and that crop is saved; calls without imageName do not create/save a crop. Ink/GT motion thresholds,128px steps,19200px physical travel,480s watchdog, tracker, input, Mag, rendering, storage and all other inl bytes remain unchanged.

The existing screen_gt log retains samples/ROI fields and adds batch_copy, read_us, verify_us and byte count. read_us includes GetDC/DIB creation or resize/BitBlt/GdiFlush/release; it is not pure BitBlt, PrintWindow or Mag time. verify_us includes the post-read security check and CPU validation, before PNG copy/write. This instrumentation permits later actual timing without claiming a speedup before execution.

## Checks and artifacts

- Exact before file/gt-batch.patch/manifest live under ignored `Build/ScrollFollow/gt-batch-capture/`.
- Removing only ScreenSourceGt from before/after reconstructs identical remaining bytes. UTF8 without BOM, pure CRLF and original EOF preserved; diff check passes.
- Independent checker pre-review passed scope, layout/channel conversion, failure handling, fullbuffer privacy and exact99/ROI checks.
- Original full `InkeysRepo.sln Debug|ARM64` via ARM64-native MSBuild/vswhere and same-PowerShell PATH normalization/node reuse disable: `build-motion-tracker.ps1 -Round gt-batch-capture-1023`, exit0/15.571seconds. Original three hashlib++ C4267 warnings only.
- `product-live-stage/offline-8464812/result.json`:25 malformed internal arguments all exit2/stdout0/stderr0; protocol exit0/233ms and state exit0/92ms.
- The334-second tracker matrix was not repeated because its production/test sources were not changed by this test-oracle optimization. Earlier L5 regression remains separately evidenced.

## Frozen handoff

- Inl SHA256: `05D40CCEA79548A11E087697695F41F4BDA3616BD7AE3722478F01493E33DEF6`.
- Final product EXE SHA256: `D58E04E15D3195388E47B0F5D8D01E7653503D5BE6E423C92DE31019D284BC76`.

No new GUI has run in this step. All build/test/GUI execution ownership has been returned to root for actual short then unchanged long validation. Actual batch read timing, real-screen sample equivalence, final long completion/return pixels/UInk remain pending that execution; no runtime PASS or speedup is implied by compilation/CLI checks.
