# Research: Draw3 speed eraser dynamics and input identity

- Query: Current product speed eraser controller, timestamps, hover/down handoff, reconnect, downstream clamps, direct/indirect input metadata, and existing tests.
- Scope: internal; Inkeys/Inkeys/Drawing/Draw3 product code, excluding Display/Host/Bridge/configuration implementation.
- Date: 2026-09-12

## Findings

### Files found and related specs

- Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cppm: speed eraser declarations, constants and width interval.
- Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp: SpeedEraserOcController implementation and real/predicted point conversion.
- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp: runtime controller ownership, hover lanes, raw sample consumption, reconnect, cursor output.
- Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cppm and .cpp: contact snapshot, stable record identity and sequence mailbox.
- Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp: cached tablet decoder, device classification, callback-time QPC and last-packet publication.
- Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cppm: hover sample and atomic mailbox contract.
- InkeysHeadlessTests/draw3_bridge_tests.cpp: eraser mode mapping tests found by targeted search; no bridge implementation inspected.
- Read .trellis/workflow.md, .trellis/spec/native-desktop/index.md, input-and-ink.md and draw3-integration.md. Product uses unique RTS producer -> ContactInputCoordinator -> DrawingController. Draw2 and old demo are not product evidence.
- Read task prd.md, design.md and implement.md. Existing artifacts explicitly exclude Draw3 eraser algorithm changes; current user steering requests research for a prospective extension. No implementation/check JSONL loaded.

### Controller, units and constants

- Draw3.InkPrediction.cppm:64 defines minimum/normal/maximum diameter as 20/50/200 physical canvas pixels. SpeedEraserStartKind is Hover or Touch (:68), and SpeedEraserOcController is declared at :71.
- Draw3.StrokeGeometry.cpp:231 Reset clears segment history, accepted position/time, direction/reversal state, decrease confirmation, middle hold and reconnect pause; current/target/guard diameter start at 20. Touch startup is enabled only for Touch start kind.
- UpdatePosition (:587) measures delta in DIP using each canvas delta divided by the same scalar dpiScale, not display centimeters and not separate X/Y scales. Rejects nonfinite coordinates/time or invalid dpiScale by advancing time only. Time is clamped to acceptedTime, rather than rejecting duplicate or backwards timestamps. Motions below 0.25 DIP do not change accepted position/time, so displacement accumulates. Accepted motions add a segment, total travel and direction evidence.
- AddSegment (:270) and MotionDistance (:289) use a 64 ms history window. Distance is weighted by overlap duration; segment duration <= 1 microsecond contributes its full distance. This is recent travel, not an explicit normalized speed estimator.
- ResolveTargetDiameter (:315): travel <=0.75 DIP -> 20 px; (0.75,3] -> linear 20..50 px; (3,24] -> linear 50..200 px; >=24 -> 200 px. Touch startup additionally caps diameter by total travel: <=8 DIP ->20 px, 8..20 ->20..50, 20..32 ->50..200. There is no minimum sample-count or minimum elapsed-history growth gate in this function.
- StepDiameter (:475) uses elapsed seconds, monotonically clamped to lastAdvanceTime. Attack <=900 px/s. Decrease starts after >=4 px discrepancy; confirmation is (coherent motion ?60:110) ms plus up to30 ms proportional to clamp((diameter-50)/150,0,1). Release <=500 px/s. Crossing down through 50 px waits another60 ms. Sub-4 px residue decays once the motion window empties. Advance (:621) changes dynamics without adding motion evidence.
- UpdateDirection (:349) uses reverse dot <=-0.70710678 (135 degrees), previous window travel >=3 DIP and >=8 DIP rearm. Reverse evidence must reach1.5 DIP within110 ms. Confirmed reversal protects the captured diameter for120 ms, bounded by candidate onset+160 ms (:403). Guard expiry resets rearm travel; short oscillation cannot perpetually rearm. Direction coherence uses dot >=0.5. These state variables must remain separate from coverage sizing.

### Time, real samples, predicted and synthetic identity

- ContactSnapshot at Draw3.ContactInput.cppm:54 contains position, stylus values, inverted flag, contact size, qpc, phase and sequence. It does not carry tablet context, a directness flag or real/predicted/synthetic provenance.
- ContactRecord (:102) separately exposes TabletContextId, ContactId, DeviceType and Generation. ContactInput.cpp:193 initializes Down sequence to2; updates publish odd/even seqlock transitions ending at sequence+2 (:239). Snapshot sequence is a mailbox publication identity, not a hardware packet ID. Record generation distinguishes reused slots.
- RTS InAirPackets at Draw3.RealtimeStylus.cpp:1147 and Packets at :1173 only decode the last packet in each callback batch and stamp QueryQpc at callback handling (:1161/:1194). No per-packet hardware time or complete batch history reaches the controller. Packets use binding lookup by tcid/cid. Position uses lifecycle-shared first-context scales; contact size retains per-context scales (:875).
- DrawingController.cpp:3002 consumeLatestSnapshot suppresses already-consumed sequences and updates speed eraser from raw snapshot absolute QPC seconds before modeler movement filtering (:3039). Modeler time is relative to stroke origin and forced to increase by at least1 microsecond (:3070). Generic filteredInputSpeed is separate from the speed eraser dynamics.
- Frame-only Advance at :6267 changes current diameter; AppendRuntimeModeledPoints (:598) deliberately preserves the previous model-input width/time between raw events. It interpolates diameter over successive model-input times for real modeled points.
- RebuildPredictedPoints at StrokeGeometry.cpp:1355 inherits the last real radius, and never drives the controller. DrawingController.cpp:3142 does not create reconnect prediction for Eraser. Prediction/modeler results are separate vectors, not tagged ContactSnapshots.
- Reconnect contains a synthetic segment described below; treating every controller UpdatePosition as authentic timed hardware motion would therefore be incorrect.

### Hover/down handoff and reconnect

- DrawingController.cpp:1979 selects three shared hover lanes: mouse (left/right share), normal pen, inverted pen. There is no per-tablet hover lane. A changed eraser width mode revision invalidates all lanes (:1990).
- initializeSpeedEraserController (:2003): fresh Touch Down always Reset(Touch). Pen/mouse Down copies the entire initialized, unowned lane controller then UpdatePosition with Down; otherwise Reset(Hover). A pending post-Up lane beyond its deadline is invalidated. Lane becomes contact-owned. No tablet identity comparison is possible with current hover sample fields.
- handBackSpeedEraserController (:2043) returns eligible completed speed eraser state to its lane, with a250 ms post-Up preservation deadline and a minimum hover sample QPC from last input; Cancel or incompatible mode invalidates. updateSpeedEraserHoverLanes (:3586) additionally requires sample and current time within deadline for preserved return, rejects stale mailbox samples after expiry, and updates movement only when sequence changes. Frame Advance is separate.
- DrawingCursorSample at PenCursor.cppm:49 carries x/y/qpc/valid/inverted/inContact/sequence only, no tcid/cid, generation, source discriminator or directness. Thus contact records retain tablet context, but standalone snapshots and hover do not.
- DrawingController.cpp:3134 creates a reconnect candidate only after model success, filtered input speed and real-tail direction; SpeedEraser PauseForReconnect is called at :3154. Successful reconnect calls ResumeFromReconnect (:2577), keeps runtime controller/model state and replaces the contact handle (:2612). Failed model update restores saved controller (:2686). Deferred completion also resumes a paused controller (:2943).
- StrokeGeometry.cpp:644 Pause advances once then freezes. ResumeFromReconnect (:652) shifts segment and hysteresis/guard time bases by the pause duration, then sets acceptedTime=resumeTime-1 microsecond and feeds the bridge displacement to UpdatePosition. This preserves diameter but injects the gap displacement as an almost-zero-duration segment into the64 ms window. A future history-gated velocity estimator must not treat that synthetic interval as observed hardware velocity/history; preserve state and re-anchor instead. Invalid reconnect inputs preserve paused state.

### Clamp and consumer sites

- Controller target clamp: StrokeGeometry.cpp:504, [20,200] px.
- Real modeled width interpolation clamp: InterpolateSpeedEraserDiameter, StrokeGeometry.cpp:1282, clamps both interval endpoint diameters to[20,200]. Scaling only controller output would be truncated here.
- Real point generation: AppendNewModeledPoints, StrokeGeometry.cpp:1323, radius=interpolatedDiameter/2. SpeedEraser bypasses generic widthEstimator and its temporal/spatial ClampRadiusTransition; do not change unrelated pen-pressure smoothing for this task.
- Cursor: ApplySpeedEraserCursorDiameter, DrawingController.cpp:643, only validates finite positive diameter, then sets width=height=diameter and outlineWidth=0.04*diameter. Active contact consumers use runtime diameter (:3756/:3805); hover uses its lane. No hard200 px cap at this specific cursor entrypoint.
- Terminal model failure: appendTerminalFallback, DrawingController.cpp:2920, uses current speed eraser diameter/2 directly.
- Predicted radius inherits last real point (:1355). Additional downstream renderer, dried/document serialization or storage radius caps were not conclusively established; absence must not be claimed.

### Tests and minimal prospective change boundaries

- InkeysHeadlessTests/draw3_bridge_tests.cpp:256/:261 covers legacy pressure->speed eraser normalization and SpeedEraser mode encoding. Targeted searches did not find SpeedEraserOcController dynamics tests in product headless tests or Draw3.HiddenWindowTest.cpp. Tests elsewhere, especially the old demo, were not established as product coverage.
- Existing contact/RTS tests exist, but their full coverage was not audited. No build, test, GUI, git operation or validation was performed.
- Core minimal boundary: InkPrediction.cppm for central fixed medium parameters and controller/interval contract; StrokeGeometry.cpp for one time-based, authentic-history-gated dynamics and consistent scaled clamps; DrawingController.cpp for independent motion/coverage scale injection, fresh Touch Down, matching hover ownership and reconnect re-anchor integration. No sensitivity UI or persistence per user decision.
- If directness metadata is required, bound additions to existing RTS lifecycle decoder metadata and existing contact/cursor mailboxes; retain the existing producer and acquisition model. Additional enum/validity/source identity would have to be transported atomically with samples. Do not classify directness from pressure, tilt, contact area, current tool or the Pen fallback enum.
- Parent owns Display/Host/Bridge/mode/scale pipeline. paintDevice 0=touch-screen/large-screen and1=mouse-pen/laptop, physical size availability/rotated cm and X/Y effective DPI are parent-provided facts, not inspected here.

## Caveats / Not Found

- BuildContextDecoder at RealtimeStylus.cpp:786 queries only IInkTablet2::get_DeviceKind (:814). Touch/Mouse are identified when reported; Pen is the default even when tablet lookup/interface/device-kind query fails. No integrated/direct pen display vs indirect tablet capability is collected in the inspected code. Existing metadata cannot safely distinguish them.
- Conservative inference with current fields: treat Mouse as indirect; treat Pen directness as Unknown; do not infer an on-screen physical motion ruler from mode or usable EDID alone. Touch is classified as TDK_Touch, but target-specific physical association is not proven by that enum. Require independently reliable direct-to-target mapping before using physical motion units; otherwise use parent-selected conservative DPI-based motion fallback. Coverage scale can still be resolved independently. No new input acquisition system is necessary for this conservative policy.
- Specific Windows capability APIs or flags that could improve directness detection were not researched, and no external documentation was consulted; their names/support/semantics remain unknown here.
- Source reads with broad output suffered truncation; only the cited subsequently visible evidence supports this note. No claim of comprehensive runtime or dried-layer validation.
- This research note is the sole write, required by the research-agent developer instruction despite the dispatch requesting read-only/no notes. No source/spec/task-planning artifacts were changed.
