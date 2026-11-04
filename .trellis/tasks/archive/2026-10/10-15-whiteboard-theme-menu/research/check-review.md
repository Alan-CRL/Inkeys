# UI / scene check review

## Fixed
- PageControl WM_NCHITTEST previously returned HTTRANSPARENT while the menu/layout was input-locked. Visible whiteboard backgrounds now consume clicks throughout transitions; action handlers stay locked and transparent rounded corners remain transparent. Added production policy regression assertions.
- Theme and page publication previously occurred separately, allowing new theme with old interactive page metadata. Added Whiteboard::PublishPageSnapshot and one-lock page/theme/generation capture; IdtState entering/active paths publish one coherent runtime snapshot.
- Theme clicks previously stamped the latest runtime page rather than the page the user pressed. SurfaceState now captures rendered whiteboard state on pointer down; callbacks carry generation/page and IdtState rejects stale identity before Bridge submission.
- Real PageControl Scene offscreen tests now inspect dot/grid pixels in addition to six color swatches, animation reversal, and resource rebuilding.

## Review boundaries / remaining evidence
- Full solution C++ compile/type checks and no-window runtime tests are owned by root to avoid concurrent MSBuild. At review time root reported CL dependency-scan exit 4 without a compiler source diagnostic. No compilation success claimed here.
- Same-HWND Scene layout/AnimatedBounds paths and whiteboard menu callback release path inspected; workspace snapshot only captures on first Desktop -> overlay and restores on final -> Desktop. Existing pure state tests cover the transition policy, not complete rendering coordinator geometry on multiple actual monitors.
- Outside click dismissal is supplied by the low-level mouse hook; pointer/touch contacts that never generate a mouse event are not independently wired to dismissal. This needs explicit production input-path validation/fix in RTS/Draw3 ownership, rather than assuming mouse promotion always occurs.
- Scene tests validate theme colors/pattern pixels and resource release/rebuild but have not yet run against the changed binary. Physical pointer capture, taskbar restore, real PPT transfer, and frame-perfect synchronization of the separately rendered ink/background/page HWNDs require the authorized later GUI acceptance pass.
- No GUI, commit, push, or parallel build performed.

## Verification
- git diff --check: PASS.
- Scripts/i18n.ps1 check: PASS, en-US and zh-TW 364/364 translated.
- TypeCheck: pending root solution build.
- Tests: new/extended tests authored; execution pending root centralized verification.

## Follow-up: headless assertion failure
Root executed the headless suite and found the preexisting DPI independence assertion still expected 230 DIP for the left whiteboard bar. The production layout correctly uses 305 DIP after adding More. Updated the assertion to require left 610px / right 460px at 2x DPI, both 160px tall, and exact 10px side/bottom offsets. This retains and strengthens the original independent-scale/position contract rather than removing the check. Corrected the accidental extra tab on BarWhiteboardBottomInsetDip. git diff --check passes; rerun belongs to root's centralized build/test pipeline.
