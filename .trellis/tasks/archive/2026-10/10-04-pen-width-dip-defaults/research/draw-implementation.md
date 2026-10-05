# Draw3 implementation evidence

## Scope and production changes

Owned files: `Draw3.Bridge.h`, `Draw3.Host.h/.cpp`, `Draw3.WindowControl.cppm/.cpp`, `Draw3.DrawingController.cppm/.cpp`. No existing dirty HiddenWindowTest, TransparentPresentation, shader diagnostic, project, State, Bar, Main or spec files edited by this worker.

- Bridge ProductState carries `double widthDip` plus shared `PenWidth::Context`. Main-owned Product.cpp merge forwards the same context; core-owned state publication must preserve double resolved metrics instead of the float UI cache.
- ProductVisualStyle groups double DIP, color and Context under a single WindowController mutex. Snapshot readers do not assemble width/DPI from independent atomics.
- `CanvasDiameterForTool` calls shared `PenWidth::CanvasWidthPx` for Pen, HardPen, Highlighter, all four shape tools and Laser. Its last step narrows safe canvas pixels to float; no integer width/radius quantization. Eraser still uses its existing override paths.
- Ordinary pen cursor uses the same conversion plus intentional minimum visibility diameter of 5 DIP at the captured target scale. Highlighter rectangle uses actual nominal canvas diameter and existing aspect/alpha; Laser cursor retains existing visual rules and exactly one DIP multiplier.
- Laser nominal width, renderer glow configuration and per-layer/per-tip bounds use the captured target Context scale. Laser preset identity remains 3/5/7 DIP and is never fed into the large-display content policy. Layer replay/resize/bake retain their own captured context.
- Global GPU particle simulation `ConfigureLaserParticles` remains startup-compatible by explicit main-session scope decision. It is a transient visual effect, not the selected nominal width. This task does not add per-stroke particle context storage or change particle/batch behavior. Visual behavior while DPI changes is a manual limitation.
- Host PumpDisplayScale consumes existing dirty display publications/window messages, reads actual Drawpad monitor and full output pixels/DPI from the same immutable snapshot, excluding EDID/workArea/virtual desktop. When a target is absent from the snapshot, existing compatible DPI lookup and GetMonitorInfoW full monitor bounds provide a deterministic fallback. No hotpath enumeration/hardware probes added.
- `HostRuntimeCallbacks::penWidthContextChanged(void*, const Context&) noexcept` is invoked outside display locks only for a semantic target change, plus guaranteed initial preparation. Main synchronously applies user paintDevice and state context and republishes bridge when an equivalent initial context otherwise would not publish.
- Initial PumpBridgeState runs on the drawing owner before graphicsReady signal, so the owner cannot call stylus.Initialize before context/preferences/style are prepared. Later display changes are consumed before the same bridge snapshot. First-context/real-device input ordering remains source-confirmed until hardware acceptance.
- Down uses `CaptureProductStrokeStyle` once and shared `ResetNominalStroke`; ongoing stroke model/pressure/prediction paths retain physical float geometry. Existing autosave/uink dpi metadata and document/history coordinates are untouched.

## New no-window production probe

Export: `int Inkeys::Drawing::Draw3::RunPenWidthProductionProbe() noexcept` in DrawingController.cppm.
Main dispatcher: `Inkeys.exe --draw3-pen-width-test` (exact single flag, no GUI initialization).

Probe calls real policy, CanvasDiameterForTool, WindowController style snapshot/cursor setup, Down capture/reset seams, ActiveStroke width estimator and CanvasRuntimeHistory:

- All ordinary pen and shape tools across 100/125/150/200/275% literal continuous and separate integer-adapter expectations.
- Ordinary highlighter 16/24/36 DIP converted across the same DPI scales.
- Classroom 1080/1440/2160p brush/highlighter presets across multiple DPI values; highlighter cursor agreement.
- 1440p/275% resolved DIP approximately 1.9393939394, ideal/applied float approximately 5.3333333333, integer compatibility 5. Probe prints these separately.
- Active stroke captures revision/color/width at Down; a later target/new user width does not change it, while next Down sees the new target.
- Valid 0.02 DIP preserved, odd 5px nominal diameter retains radius 2.5; Laser converts once; Eraser fallback unchanged; invalid and maximum widths have safe deterministic behavior.
- Existing stored physical geometry survives real CPU history Undo/Redo with no extra DPI conversion.

## Verification performed by worker

`git diff --check -- Inkeys/Inkeys/Drawing/Draw3` passed (exit 0).
Read-only searches reviewed all widthDip, CanvasDiameterForTool, ProductVisualStyle, dpiScale and laser bounds/configure sites.
Byte-preserving edits retained original working-tree CRLF and BOM (Bridge/WindowControl/DrawingController BOM; Host files no BOM). Git stores LF because core.autocrlf=true; a naive raw HEAD vs working-tree EOL comparison is not a valid checkout preservation check.

No build, product executable, HWND, GUI, full solution build, commit or task archive executed by this worker. Main session owns build and runtime probe execution. Await exact build/test results before marking runtime PASS.

## Remaining acceptance

- Independent reviewer checks grouped generations, callback startup ordering, current target source, float precision boundary and absence of old ordinary-px assumptions.
- Mixed-DPI target changes with actual RTS/Touch/PPT, initial physical first stroke, soft pressure feel and Win7 render behavior require manual devices.
- Laser global particle visualization cache is deliberately distinct from nominal ink; test real DPI-changing particle appearance manually.
- Target full monitor is the deterministic reference; PPT black bars/content are not detected. No new cross-display drawing architecture.
