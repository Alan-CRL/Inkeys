# Trellis implementation check

Date: 2026-10-19. Reviewer: trellis-check subagent; direct review, no recursive delegation.

## Stage 1: configuration, product wiring and settings

Reviewed the active PRD/design/implementation artifacts and every check.jsonl reference, including the complete Touch ownership, schema=2 and Win7 guarded-shader sections that exceed normal context injection. Current scope follows the 2026-10-19 implementation authorization; earlier planning sections are historical context.

Files reviewed: Draw3.CursorSettings.h, Draw3.Bridge.h, Draw3.Product.cpp, Draw3.Host.cpp, IdtState.cpp, Other.Config.cpp/.cppm, Setting.cpp, all three locale files, generated key header/snapshot, both project header entries, config_metadata_tests.cpp and draw3_bridge_tests.cpp.

- Confirmed Drawing.Cursor has one persistent value source, integer System=0/Drawing=1 with Drawing default, three independent boolean profiles defaulting Mouse=true/ScreenPen=false/Digitizer=true, and no system-touch-hiding toggle.
- MouseStyle validates the JSON integer type before range conversion, so bool/floating/string/overwide values cannot coerce to System. Generic boolean decoding is strict and ReadAll/ReadMini first restores each selected field default.
- IdtState publishes settings with tool/mode state, Product merges cursorSettings, Bridge revisions include cursor-only publications, and Host limits the new mouse style to Pen/HardPen/Highlighter while eraser/laser retain their dedicated policy.
- Settings uses the existing deferred SyncDraw3State command before QueueConfigWrite freezes its payload on the same FIFO. No new controller/HWND access occurs while holding the ImGui lock.
- The new card measures the description with the same font and actual wrap width used to render it. Its height includes the description plus four spaced rows; local font/style stacks balance. The available row width comfortably fits the current English labels and control widths by source geometry; this is static review, not GUI visual verification.
- Persistence tests use real Config I/O in the existing private temporary directory, cover independent restart behavior, strict invalid types, frozen copies and NoUpload. Bridge tests cover cursor-only revision/immutable prior snapshot without altering tool/width.

No source fixes were needed in this stage. These root-owned files were frozen and reported to the main session before its build.

## Verification performed by this reviewer

- Scripts/i18n.ps1 check: exit 0; en-US and zh-TW each 372/372 translated, 100% PASS.
- git diff --ignore-submodules=all --check: exit 0. The ignore-submodules scope avoids the known HiMsg worktree metadata restriction and does not claim submodule validation.
- UTF-8/BOM/EOL review: 16 existing root-owned files match their Git checkout-filter form, all retaining homogeneous CRLF and prior BOM identity. Git core.autocrlf=true and index LF are expected; comparing raw HEAD bytes alone would incorrectly label the checkout CRLF as a conversion. New CursorSettings.h is UTF-8 without BOM and CRLF.
- Builds and EXE execution are reserved for the main session; no product/test EXE, GUI, Computer Use, git commit/push or remote write was performed by this reviewer.

## Next stage

Read-only review of the frozen cursor/controller/renderer/HLSL/guarded PS/contact tests, then the final window lifecycle changes. Owner files will be fixed through their assigned owners if a defect is found. Win7 double-cursor, first-contact flash and physical pen/touch/mouse behavior remain hardware acceptance items; no local headless/static result proves them.

## Stage 2: final cursor, shader and window lifecycle review

The main session reported all implementation sources frozen before its serial build. This stage was read-only; no owner source or shader generator was modified by this reviewer.

Reviewed PenCursor.cpp/.cppm, DrawingController.cpp/.cppm, RendererPrimitives.cpp, formal inkVertexShader.hlsl/inkPixelShader.hlsl, Win7GuardedShader.h, WindowControl.cpp/.cppm, draw3_contact_tests.cpp, both implementation reports and the actual current generator/generated shader evidence. Also followed production LoadShaders, the transient draw order, dirty unions and the bounded diagnostic sink.

- Explicit product opt-in preserves legacy resolver callers. SetCursorSettings always synchronizes the Window mouse policy, even on its first default-valued call; a packed atomic publishes profile changes together and requests a stationary redraw when required.
- The pen icon uses an explicit shape 21 in both VS and PS and a convex polygon whose upper-left tip equals the unchanged input sample. CPU bounds and renderer quad use the same half-icon center offsets. Offset/edge alpha participate in validity and equivalence; old/new dirty bounds are rebuilt before rendering the final transient overlay. No icon is added to L0/L1/L2, ink documents, exports or Undo.
- The ordinary/highlighter preview geometry still comes from the existing product appearance. Highlighted edges have their own encoded opacity while fill uses opacity*fillAlpha, keeping highlighter fill 0.35. Legacy style.r=0 continues to inherit overall opacity. Coverage keeps output premultiplied Add/Retain; eraser stripe and laser paths retain their established policies.
- Source preference routing uses read-only IntegratedPen/ExternalPen/Unknown/Conflict data and does not alter classification or eraser response. Touch authority cannot create a primary icon; existing filtering and real-device takeover rules are unchanged.
- Independently parsed header bytecode: 31160 bytes, SHA256 43C7E21FE9AA531271E2036D44998A5DCE6EAC9C032B63965A5417AE293C8D57, exactly matching the generated CSO and header declaration. The current generator differs from the archived method only at the >20 to >21 gate boundary. The generated prefix matches current formal source; input/output signature hashes match the formal reference. The actual assembly includes the dedicated type-21 branch and excludes type 21 from the positive DefaultInk guard (ilt with threshold 21 before if_nz), while retaining type 0/3 and unknown <0/>21 behavior. This removes the initial >20 overlap that the main session had already identified and the cursor owner had corrected before this stage.
- FORCEOFF joins the existing Vista+ tablet mask before first contact and the query message returns that same mask, retaining multitouch. Property installation checks errors and restoration only reverts this object’s still-owned value. No new Win8 static import, global setting/hook or ShowCursor counter is introduced.
- Touch End and repeated Pen Clear requeue UI-owner policy without reviving old hover or changing persistent ownership. Selection changes and show/hide boundaries request refresh. Selection projection returns ordinary Mouse/Unknown to the system policy while retaining Pen/Touch and touch-pan suppression, and the Controller clears application visuals in selection mode.
- ApplyWindowCursor preserves WindowFromPoint/IsChild ownership checks, including destroy cleanup. Cursor observations are gated off when diagnostics are disabled and bounded by lifecycle/decision/hit changes or a one-second cadence when enabled. GetCursorInfo/diagnostic thread reads preserve LastError and retain before/after success, flags and handle. Posted-refresh observation is lifecycle-driven; no per-packet global cursor-query loop was added.
- All 11 existing cursor/window/shader/contact-test files retain checkout BOM/EOL identity; no wholesale source formatting or mixed line endings were found. git diff --ignore-submodules=all --check remains exit 0.

No additional source defect requiring a fix was found during the independent review. The root-reviewed files remained frozen, and no source modification was made by this reviewer.

## Verification and remaining acceptance

This reviewer’s executed checks are the read-only i18n check, diff whitespace, checkout-format comparison, generator/source comparison, bytecode identity and assembly inspection listed above. Main-session solution/headless results must be recorded separately; they were still running when this review completed. No product/test EXE or GUI was executed by this reviewer.

Win7 real-hardware double-cursor identity, first-Down zero flash, pen-up system-cursor lifetime, stationary click-through handoff, actual pen-icon raster/hotspot and cross-DPI/bright-dark appearance remain runtime/visual acceptance items. The new guarded PS has a new identity and cannot inherit the old Win7 shader bytecode’s hardware acceptance. FXC success, equal signatures, static source math and pure resolver tests cannot prove Win7 WARP’s final pixels or driver feedback behavior.

Main-session build status received during review: the first restricted-runner attempt exited 1 with the repository’s previously recorded C1902/PDB-manager mismatch. The unchanged-source verification in the permitted PDB-service environment passed that point and was still compiling product modules, with existing hashlib C4267 warnings at the time of the update. This is a main-session environment retry, not a source repair or a claimed successful build; final exit code and headless results remain to be added by the main session.

## Compile follow-up: C2248 access visibility corrected

After the earlier static review, the permitted-PDB-environment solution build found one source compile error: DrawingController::SetCursorSettings called WindowController::RequestDrawingCursorRender while its declaration was private (C2248). The earlier statement that this static review found no blocking defect did not establish compilation; this access issue was missed by that static pass and was then caught by the compiler.

The main session made a minimal source correction, moving the one existing RequestDrawingCursorRender() noexcept declaration to the public request/consume API next to ConsumeDrawingCursorRenderRequest, with a short Chinese ownership comment. The implementation, atomic request bit, coalescing and existing coordinator wake path are unchanged. The new draw3-cursors.md signature entry describes that same narrow public request contract. This source fix is separate from the prior C1902/PDB runner environment retry.

Read-only follow-up verified the declaration occurs exactly once and is in public scope. Its production body only stores drawingCursorRenderRequested_ with release ordering and invokes RequestControlWake, which publishes the existing coordinator wake. It does not issue HWND or GPU calls, expand rendering ownership, request a full-canvas present, or change the dirty-region path. This makes the existing Controller call legal without changing its runtime behavior. UTF-8 BOM and homogeneous CRLF were retained; git diff --ignore-submodules=all --check passed.

The reviewer did not modify product source, build or execute an EXE during this follow-up. Sources are frozen again for the main session’s same-configuration complete-solution rebuild. No successful rebuild/headless result or Win7 hardware acceptance is inferred from the access fix.
