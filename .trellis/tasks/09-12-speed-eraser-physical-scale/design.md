# Design: speed eraser physical scale and medium dynamics

## Implementation boundary
Controller is hardware-independent ordinary C++ in Draw3.SpeedEraser.h/.cpp and is compiled by both product and headless tests. Keep the ink_prediction module's existing public names as thin aliases/wrappers; remove the old OC implementation from StrokeGeometry, not other pen algorithms.

## Shared API contract (all workers follow these names)
Namespace: Inkeys::Drawing::Draw3::SpeedEraser.
- enum class DeviceMode { LargeScreen, Laptop };
- enum class ScaleSource { Dip, Physical };
- enum class StartKind { Hover, Touch };
- DisplayScale: uint64_t generation=0, revision=0; uintptr_t monitor=0; float dipPerPixelX=1, dipPerPixelY=1, cmPerPixelX=0, cmPerPixelY=0; bool physicalAvailable=false, directTouchMapped=false. Equality compares the whole value. revision also changes on window identity/bounds changes even when Display generation does not.
- Config: DisplayScale display; DeviceMode mode; ScaleSource motionSource, coverageSource; float motionPerPixelX, motionPerPixelY, minimumDiameterPx, maximumDiameterPx; centralized medium tuning, defaults from PRD. Config equality available.
- Config ResolveConfig(const DisplayScale&, DeviceMode, bool touch) noexcept.
- Controller::Reset(float x,float y,double seconds,StartKind kind=StartKind::Hover,const Config& config=Config{}) noexcept.
- Controller::UpdatePosition(float x,float y,double seconds) noexcept -> float (no per-call DPI).
- Controller::Advance(double seconds) noexcept -> float.
- Controller::PauseForReconnect(double seconds) noexcept.
- Controller::ResumeFromReconnect(float x,float y,double seconds) noexcept -> float: preserve size/history deadlines but re-anchor; never add bridge displacement.
- Controller::Diameter() const noexcept -> float; Controller::NeedsAnimation(double seconds) const noexcept -> bool; Controller::Configuration() const noexcept -> const Config&. Preserve any existing required inspection getters via the adapter.
- WidthInterval: double startTimeSeconds=0,endTimeSeconds=0; float startDiameter=16,endDiameter=16,minimumDiameter=16,maximumDiameter=160.
- float InterpolateDiameter(const WidthInterval&,double seconds) noexcept; invalid values are sanitized within supplied bounds, no hard pixel bounds.
- float ContactDiameter(float acceptedRadius,float fallbackDiameter) noexcept: finite positive acceptedRadius -> 2*radius; otherwise fallback.
WindowController additions (Host worker): SetSpeedEraserDisplayScale(const SpeedEraser::DisplayScale&), SpeedEraserDisplayScaleSnapshot() const; SetSpeedEraserDeviceMode(SpeedEraser::DeviceMode), SpeedEraserDeviceModeSnapshot() const. Use an existing-style small mutex protected snapshot and control wake, not many independent atomics read as a torn config.
Bridge::ProductState addition: int paintDevice=1. Host maps 0 to LargeScreen, other to Laptop. Setting PaintDevice change calls SyncDraw3State.

## Mapping and lifecycle
No new input capability queries. Pen cannot be distinguished from indirect tablet and uses DIP movement. Host may mark directTouchMapped only where existing mapping is provable (single physical active display, matching full Drawpad bounds/client mapping); ambiguous extended/multi-target mapping must remain DIP movement. Coverage independently uses the current monitor's available physical size. Do not infer direct pen from paintDevice.
Display callback only stores the snapshot/publishes control wake; window monitor lookup/rebuild on startup/window messages/control consumption outside the per-point path. Subscribe lifetime ends before Host/window fields are torn down. No cross-thread HWND mutation in callback. Display generation changes must refresh even when Bridge revision is unchanged.
One scale and mode per contact batch, including simultaneous contacts and reconnect pending runtimes. Hover follows latest config; stale/incompatible lane invalidates/re-anchors before Down. Clear/reset releases old batches.

## Dynamics/time
Real segments use raw monotonic QPC seconds and per-axis motion scale. Keep separate sample time and animation time so late but newer raw samples are not rejected solely because a frame advanced. Duplicate/backwards sample timestamps never create velocity. Long time intervals progress timers/decay without huge loops or dropping elapsed time.
80ms path speed; use40/160ms scalar speeds to accelerate growth only. smoothstep(log-speed-normalized) targets a log-diameter. Hold and shrink evidence overlap; movement supporting a significantly smaller target never keeps refreshing the hold forever. Snap sufficiently close to the limit and stop animation when settled.
Touch unlock uses maximum displacement from Down in movement units, not summed jitter. Reconnect excludes gap displacement/time from velocity, keeping established recovery semantics.

## Recovery execution ownership
The user explicitly required main-session-only continuation after the transport failure. All remaining edits, checks, builds and validation belong to the current main agent; do not create or call subagents. Existing integration and project registrations are retained.
