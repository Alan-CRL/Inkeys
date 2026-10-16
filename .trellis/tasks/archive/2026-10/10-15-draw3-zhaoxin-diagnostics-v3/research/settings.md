# Settings/startup implementation

## Change boundary
- Behavior gap: product had only hardware-first/fallback WARP and no explicit upload compatibility preference. Add independent default-on restart-only drawing preferences, persist via existing new Config schema, and carry one startup snapshot through renderer creation and device recovery.
- Actual production startup is `IdtMain.cpp`, not `IdtDrawpadFacade.cpp` (the latter contains legacy stubs). Parent explicitly approved extending ownership to startup/config-test blocks of IdtMain; facade needs no change.
- Files: Other.Config.cppm schema; Setting.cpp experimental cards; three i18n JSONC + sync-generated header/snapshot; Host.h/cpp snapshot fields/forwarding; GraphicsInitialization.cppm/cpp selection/event; TransparentPresentation.cppm/cpp renderer and recovery bridge; IdtMain.cpp startup snapshot and early tests.
- Excluded: renderer resources/upload implementation, Performance core, ResourceProbe, UI RenderPipeline device/backend, DComp selection, shader behavior, runtime device switching, solution/project/toolchain changes.

## Implemented contracts
- `Experimental.Inkeys3.Draw3.EnableHardwareAcceleration=true` and `EnableZhaoxinCompatibility=true`, `NoUpload`, missing keys preserve enabled defaults. Strict bool decoding comes from existing Config codec, no bespoke parser/migration.
- Setting experimental cards use local preference values and QueueConfigWrite only, no Host mutation or device recreation. Three language strings explain restart, software drawing fallback, and Zhaoxin GPU-only behavior. 110 DIP cards wrap descriptions; Inkeys3 containing height increases by 230 DIP.
- `configOnce` -> HostStartOptions default-on bools appended at end -> Host::startOptions copy -> InitializeGraphicsDevice + TransparentPresentationOptions. Presenter stores options by value; device-loss recovery reads that copy; renderer.Init receives compatibility flag on each attempt.
- InitializeGraphicsDevice final `bool enableHardwareAcceleration=true`. Production CreateSelectedDevice helper directly requests WARP when false; true requests hardware then WARP on failed hardware. Final failed selected backend is Unknown; no fabricated skipped hardware HRESULT in logs. New initialization event `hardware_acceleration_preference`, detail0 requested bool; existing CaptureGraphics records actual backend/adapter.
- Renderer policy remains owned by renderer worker: only actual Hardware + Vendor 0x1D17 + requested compatibility uses DEFAULT. WARP/non-Zhaoxin remain dynamic.

## Validation performed
- `pwsh -NoProfile -File ./Scripts/i18n.ps1 sync`: PASS, en-US and zh-TW 366/366 translated. Header was generated, never edited manually.
- `pwsh -NoProfile -File ./Scripts/i18n.ps1 check`: PASS, both 100%.
- Scoped git diff reviewed: localized edits only, no whole-file EOL/BOM churn. All original CRLF and per-file UTF-8 BOM preserved.
- `git diff --check` scoped to owned product/i18n files: PASS.

## Pending integrated verification (parent owns builds)
- Build full InkeysRepo.sln Debug|ARM64 using native MSBuild and normalized PATH.
- `Inkeys.exe --config-boundary-test`: existing early CPU test extended with default-on/missing keys, all four bool combinations through real main.json Write/ReadAll/ReadMini, Config assignment and NoUpload, non-bool defaults (number/string/null/object/array). Isolation remains TestResults/release-hardening/config-boundary-PID.
- `Inkeys.exe --draw3-device-policy-test`: new early pure CPU test calls the same production CreateSelectedDevice helper with recorded fake device create results; covers direct WARP success/failure, hardware success, failed hardware -> WARP, both failed, call order/count and actual result. Does not create actual D3D device or HWND.
- Restart/recovery snapshot propagation verified by source review; no runtime presenter/device-loss/GUI proof claimed. Real Zhaoxin/Qualcomm and settings GUI verification is user field acceptance.
