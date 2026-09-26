# PPT recognition regression: managed cache and native admission

2026-09-25; source checkpoint 10ea1e0d. Read-only product-source investigation; no product code changed. Root agent owns actual PowerPoint COM observation, so the hypotheses here are not Office test results.

## Confirmed source facts

1. An already-running show does not need a new Begin event to create a session. PptCOM.cs PptComService first binds pptApplication / pptActivePresentation / pptSlideShowWindow; FullCleanup advances binding via InvalidatePresentationDescriptor. The subsequent owner lifecycle probe captures the current stamp, reads SlideShowWindows.Count and SlideShowWindow, then ObserveIfCurrent(..., true, hwnd) increments session and sets Active. RefreshPresentationDescriptorIfNeeded obtains a NEW stamp before ReadShow. The previous lifecycle stamp becoming stale is intentional and requests a refresh, not a permanent rejection.
2. SlideShowSessionCache.Publish replaces descriptor with Unavailable only when captured lifecycle is not Active. It rejects genuinely stale generation/binding, but normal initial activation reaches a matching fresh stamp. No deterministic initial-activation cache deadlock was found by source inspection.
3. The cache serializes exactly seven envelope fields, matching ParsePptSessionSnapshot. Initial Unknown/session0/binding0/Unavailable is legal. Active requires nonzero session. The native parser allows a structurally valid descriptor whose HWND is zero; native PptInfo later rejects it silently at descriptorWindowMatches.
4. Important new admission dependency: IdtPlug-in.cpp lines 679-694 requires descriptor.slideShowHwnd to be a live window when sessionApi exists. GetPptShow fallback only executes when !sessionApi. The baseline got ppt_show through GetPptShow, independent of descriptor HWND.
5. Two HWND reads are NOT the same adapter. PptCOM.cs GetPptHwndFromSlideShowWindow around 1717 casts the RCW to the PowerPoint PIA SlideShowWindow interface and reads typed .HWND. PresentationDescriptor.cs TryGetHwnd uses MarshalLateBoundComAccessor.GetProperty, hence Type.InvokeMember/GetProperty on IDispatch. Nonbusy exceptions are swallowed, returning zero, without changing an otherwise valid page's status.
6. Consequently successful `Got Hwnd GetPptHwndFromSSW` output and binding are compatible with a zero HWND in the descriptor. If the provider does not expose HWND through this automation dispatch path, the new code blocks all initial PPT admission even though typed HWND works. This is a deterministic conditional regression, not yet a claimed real-provider reproduction in this note.
7. Root's report of no native [PPT] warning is consistent: JSON parse success clears ReportIssue; later window mismatch changes local trustedPage/pageStatus with no diagnostic. Unknown descriptor status likewise does not necessarily create a parse warning.
8. The newly inspected View.State is optional: missing-member exceptions do not set stateUnknown and valid Slide/Slides data still yields Valid. Only an actual numeric State outside 1..5 forces Unknown. Thus a missing State by itself does NOT prove the recognition cause.

## Why current mocks miss the boundary

- PptCOM.Tests FakeAccessor implements property reads by dictionary lookup, not MarshalLateBoundComAccessor/COM InvokeMember.
- FakeGraph supplies Window.Properties["HWND"] = 100L unconditionally, and reader tests inject PID resolver returning 42.
- Cache tests directly Begin/Observe and Publish fake-reader results. They prove generation and schema behavior but not the production service's owner-to-reader COM adapter behavior.
- Native session parser tests can legitimately accept HWND=0; they do not execute the real IsWindow/GetWindowThreadProcessId admission with a fake valid HWND while contrasting typed and IDispatch getters.
- Existing native/managed tests therefore cannot establish real Office recognition.

## Precise next tests and narrow fix boundary

1. Root's read-only real COM probe should compare typed SlideShowWindow.HWND with the exact production MarshalLateBoundComAccessor.GetProperty("HWND"), and run the actual reader. Record HRESULT, descriptor JSON and HWND validity/PID. Also check View.State/Slide/Slides.Count to avoid guessing from generic first-chance exception addresses.
2. Add an adapter regression fixture or existing-running-Office integration probe that actually exercises MarshalLateBoundComAccessor; a dictionary fake alone cannot catch hidden/non-dispatch members.
3. Add reader-to-envelope regression for property HWND failing DISP_E_MEMBERNOTFOUND while a same-window verified handle is available from the established typed getter. Preserve binding/session identity and original property failure diagnostic; do not weaken native IsWindow/PID checks or use arbitrary foreground HWND.
4. Add initial already-active lifecycle test: InvalidateBinding -> ObserveIfCurrent(currentStamp,true,hwnd) -> fresh Capture -> ReadShow -> Publish -> Active/nonzero session, plus rejected stale stamp after a real End. This locks the no-Begin-event path without mistaking expected stamp invalidation for a bug.
5. Add native admission diagnostics for missing/invalid HWND and raw-page/descriptor mismatch, rate-limited by state revision/reason, to distinguish successful binding from accepted page/session.

## Not established by this investigation

- The supplied first-chance exceptions identify neither the exact member nor final failure: 80020003 can arise from optional fields. Actual member attribution is still required.
- Console mojibake is not evidence of a corrupt COM BSTR identity.
- No change to ROT enumeration, provider selection, event registration, page reads, or lifecycle contract is justified by these logs alone.
## Confirmed live-provider observation and narrow fix

Root agent's read-only probe against the existing running PowerPoint confirmed show count=1, View.State=1, SlideIndex=1, SlideID=256, and correct Unicode name/path. The exact InvokeMember("HWND") path throws COMException 0x80020003, while existing typed SlideShowWindow.HWND succeeds. This confirms the adapter mismatch identified above, not a ROT/binding failure or Unicode-path failure.

The implemented fix is confined to MarshalLateBoundComAccessor.GetProperty("HWND"): read the scalar through the already-supported PowerPoint SlideShowWindow interface when available; otherwise preserve the original reflection path. No COM root is released or retained by this scalar read, and no page reads, ROT logic, Application binding, interface schema, or native safety check changes. Busy/errors from the typed property are propagated unchanged. Both descriptor creation and guarded Exit validation consume this shared accessor.

Tests invoke the production accessor, using only a narrow injected typed-HWND scalar function where a portable test cannot supply a real Office RCW. A typed-only object deliberately has no reflection HWND member; other tests execute actual reflection fallback for a non-PIA object, the default production constructor, unrelated properties, typed zero, and exact COMException propagation without accidental fallback. Real-provider before/after testing and managed/full builds are coordinated by root; not claimed passed here before their results arrive.
## Verification and follow-up (after implementation)

- Checkpoint `10ea1e0d` was committed before this investigation. The fix remains a separate change.
- Existing PowerPoint read-only production probe before the fix: typed HWND=10227496, PID=37764; `ReadShow` returned StableSlideIds / Valid, page 1 of 4, SlideIDs [256,258,257,259], but `slideShowHwnd=0` and `applicationProcessId=0`. The new native gate therefore rejected the show. The Unicode path was correct.
- Full `InkeysRepo.sln Debug|ARM64` after the typed-HWND fix: exit 0, 0 errors, 3 preexisting third-party C4267 warnings. `PptCOM.Tests.exe`: exit 0, including production adapter route/error tests. `git diff --check`: exit 0. Generated `Inkeys/PptCOM.dll` is part of this change; TLB wire shape stayed the same. Build/test logs: `Build/Validation/ppt-recognition-regression/`.
- A second real-provider probe could not be completed because the same PowerPoint process then reported one presentation but `SlideShowWindows.Count=0`, and `ActivePresentation` threw; no Office GUI was started or controlled by the agent. The user subsequently reported that the change had essentially no further issue; this feedback is not a full Office/WPS/keyboard/device acceptance matrix.
- Root cause category: cross-layer contract plus test coverage gap. A nullable descriptor HWND became a native hard gate while tests only fed a synthetic HWND into a dictionary accessor. Prevention: use the production adapter in automated tests and compare typed/IDispatch reads on a real provider when a new field becomes mandatory.
