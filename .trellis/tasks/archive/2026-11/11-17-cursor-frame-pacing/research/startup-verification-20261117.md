# A2 开工核对（Codex gpt-6.1-sol / high / fast，只读，2026-11-17）

> Claude 已逐项抽查属实：CursorSettings bit13 为有效位；PublishMove 只写 Producing、TryReadSnapshot 不读 Quarantined；兼容鼠标准入拒收时 Active()=false；手掌拒收不清光标信箱；waitable 开关只写配置不触发 SyncDraw3State。

Baseline: `refactoring/draw@0c8a73375e3134dfb0cfa9ed275760eebf1933a6`, verified from Git metadata. Working-tree W/CI already contained partial A2 changes, so this report uses **committed source**, read directly from Git objects. No files were modified; no builds or tests were run.

Abbreviations follow your request. P=`Draw3.PenCursor.cpp`; H=`Draw3.Host.cpp`; CS=`Draw3.CursorSettings.h`; tests reside under `InkeysHeadlessTests/`.

## 1. Control-marker cost

**Facts**

- `DrainIngressBatch` processes each dequeued item; a null item establishes `commandBoundaryPending` **only if** `HasPendingCanvasCommand()` is true (C:175–190).
- Null processing acknowledges the marker, invokes `observer_.controlWake`, checks pending canvas commands, calls `sealPresentationContacts()` and `synchronizeLaserResources()`, then returns (C:18303–18314).
- The wrapper creates an ingress metrics scope and calls `reconcileDrawingActivity()` (C:18346–18361). Reconciliation scans physical/quarantined/pan contacts and only notifies when the aggregate activity changes (C:16223–16231).
- General acknowledgment only clears `controlWakePending` (CI:1225–1228).
- The production callback is H’s `ConsumeBridge`: drain desktop/presentation completions, consume a bridge command **only for Command markers**, pump bridge state, then auxiliary release (H:2076–2085).
- This callback is not lock-free: bridge pumping takes preference/presentation/bridge locks and copies `ProductState`; unchanged revision returns before applying tool/cursor state (H:1741–1807). Display updates return immediately unless dirty (H:1624–1626).
- With unchanged admission revision, sealing returns immediately (C:18209–18213). Stable non-laser state returns without renderer work; stable prepared laser state checks availability and returns (C:18266–18278; C:16303–16315).
- Pending completions can enqueue canvas commands; desktop completion processing allocates a `shared_ptr` (H:2024–2033). Admission changes can terminate contacts, clear renderer resources and request a full present (C:18214–18263).
- Existing mouse position changes already request this same General control wake outside pan mode (W:1836–1855; W:1901–1905; CI:381–407).

**Inference**

An unchanged General marker performs bookkeeping, locks, state snapshots and contact scans. It does not itself flush ink, split a stroke, commit ink, log, allocate renderer resources or issue GPU drawing. Those effects require actual pending work/state changes.

Mouse pacing does not introduce a heavier marker path; pen pacing adds executions of an existing path. Coalescing guarantees one **outstanding** marker, not literally one processed marker per frame: acknowledgment permits another concurrent enqueue during draining.

**Verdict — CONSISTENT with design**, with qualified wording: “cheap stable-state reconciliation,” not “side-effect-free/lock-free” or a strict “one marker per frame.” No dedicated marker is required by the observed stable-state behavior. Runtime cost was not measured.

## 2. Rejected contact main cursor

**Facts**

- Selection mode exits before generating any drawing cursor (C:18596–18606).
- Cursor construction reads both mailboxes; pen suppression invalidates a **local copy**, leaving the underlying mailbox intact (C:18608–18614).
- A runtime is optional: `primaryRuntime` defaults null; the primary visual still resolves directly from samples and cursor authority (C:18641–18658; C:18702–18712).
- Authority selects only its corresponding valid sample; Touch authority selects neither; Unknown prefers pen (P:98–114).
- Mouse system-arrow style suppresses the application visual. Pen/tablet contact-dot hiding also applies without an accepted runtime (P:369–388). Thus a rejected contact does not override existing visibility preferences.
- Mouse eraser contact is locally forced false only when the lifecycle lacks ownership and sample time is no newer than `LastEventSeconds()`; invalid-mailbox retraction can synthesize a hover sample (C:18623–18635).

| Rejection kind | Mailbox result |
|---|---|
| Admission blocked | Controller discard does not clear either mailbox (C:18318–18322). **Exception:** compatibility mouse refuses Down before becoming active, then publishes `inContact=mouseInput_.Active()`, therefore false (MouseInput.h:54–74; W:1014–1036). RTS mouse publishes button state (W:2995–3005). |
| Extra finger | Touch-only test/discard; no mouse/pen mailbox invalidation (C:5383–5406). |
| Palm | `hasBlockingPalm()` can reject a noneligible mouse/pen contact too; this discard does not clear its cursor mailbox (C:17458–17468). |
| Touch-pan pen suppression | Pen samples can remain valid/contact in the mailbox, accompanied by suppression state (W:1725–1788; W:1428–1435; C:17005–17013). |
| Selection | Mailbox validity/contact is not a sufficient exclusion; visual construction explicitly exits (C:18596–18606). Compatibility mouse isolation produces inactive contact (W:1008–1034). |
| Pen compatibility mouse suppression | Messages return before publishing a new mouse sample; suppression is latched and pen feedback suppressed (W:2928–2984). Existing mailbox contents are not a rejection-reason indicator. |

**Inference**

Admission-rejected RTS mouse/pen can generate a main cursor without a runtime, subject to authority and existing visibility settings.

Raw `valid && inContact` plus nonselection/nonpan is insufficient. It includes palm-rejected mouse/pen and potentially suppressed pen samples after pan ends. Reading suppression alongside the pen sample is essential.

**Verdict — CONTRADICTS design** in two places:

1. The blanket “rejected mouse remains in contact” assumption fails for compatibility mouse.
2. The proposed predicate cannot distinguish allowed admission rejection from palm rejection.

Minimal correction: preserve a physical-held cursor state for admission-rejected compatibility mouse, independently of stroke acceptance; reuse the effective pen suppression projection (`TouchPanActive`, `suppressPenUntilRelease`, and `ReadPenCursorSample(...,&suppressed)`), and track rejection eligibility/reason through release. Apply that eligibility to both paced requests and the tail predicate. Preserve ownership and cursor-style hiding.

## 3. Eraser F3 idle

**Facts**

- `WaitForEraserIdle` arms one Move notification, rechecks actual snapshot sequence/terminal/readability, waits using `WaitForWake`, and disarms through RAII (C:5859–5889).
- Despite its misleading variable name, the argument originates from `CaptureWakeGeneration()` (C:22168), which reads **wakeGeneration**, not frameWakeGeneration (CI:1286–1289).
- `WaitForWake` compares wakeGeneration (CI:1291–1322). `SignalWake(false)` still increments that generation and signals the event (CI:468–472).
- Successfully published subscribed Move already calls `SignalWake(false)` (CI:879–882).
- F3 is referenced by `input-and-ink.md:83`; its detailed contract requires arm/recheck/frame-start generation/RAII, including deadline waits (`canvas-freeze-boundaries.md:88–105`).
- Eraser idle requires physical contact, no navigation, and settled eligible eraser runtimes (C:5836–5848). Mouse retraction/hover animation precedes idle waiting and uses frame-deadline pacing (C:23561–23603).
- Production `WaitForFrameDeadline` callers are the physical-contact and animation/navigation branches (C:23543, C:23569). Its generation checks are confined to CI’s implementation/reset (CI:1248; CI:1325–1365).

**Inference**

Paced cursor requests wake F3 idle correctly. Stationary mailboxes produce no repeated requests; valid stroke Move still has the independent subscription handshake. Retraction remains deadline-driven. No other production consumer was found that requires mouse position moves to interrupt frame deadlines.

**Verdict — CONSISTENT with design**, provided the new tail branch explicitly requires **no physical-contact stroke**. Testing merely “physical branch was not taken” would also capture idle erasers and bypass F3.

## 4. Rejected-touch snapshot readability

**Facts**

- Discard changes Producing → Quarantined; Closing → ClosingDiscarded; already ConsumerOwned is recycled (CI:969–992).
- Producer lookup finds Quarantined routes (CI:487–505), but `PublishMove` subsequently requires **Producing**, so quarantined Move returns false without updating the snapshot (CI:834–865).
- `TryReadSnapshot` accepts only matching-generation Producing, Closing or ConsumerOwned routes. Quarantined/ClosingDiscarded are unreadable (CI:902–920).
- Handles combine record pointer and generation. Down increments generation before initialization; reads validate route generation before and after snapshot copying (CI:800–810; CI:905–919).
- Up/cancel can close Quarantined routes; producer publishes terminal, changes to Free, releases the slot and sends an urgent wake (CI:595–674).
- Additional-touch rejection explicitly warns that record reuse can happen immediately after discard (C:5402–5405).
- Existing regression asserts quarantined Move rejection (`draw3_contact_tests.cpp:954–957`).

**Inference**

There is **no existing API** providing the proposed discarded-contact position stream. Simply allowing `TryReadSnapshot` to read quarantine would expose a frozen position. Treating every failed read as termination would also confuse transient snapshot contention with actual end.

Rejected-touch Move additionally needs to wake/request rendering: ordinary Move has no ongoing control marker, and the design intentionally returns to idle when nothing changes.

**Verdict — CONTRADICTS design §4.2.**

Smallest scoped addition: opt selected rejected contacts into cursor observation before surrendering their route; allow producer updates to their visual snapshot while preserving rejection from ink. Provide a generation-checked read API distinguishing **live / retry / ended-or-reused**, and a coalesced paced cursor notification on changed position. Terminal can remain producer-recycled; route invalidation is sufficient to retire the visual. Preserve ordinary discarded-contact behavior and its existing tests.

## 5. Live setting propagation

**Facts**

- Existing config uses `IdtAtomic<bool>` beside `EnableWaitableWait` (Other.Config.cppm:571–579).
- `PublishDraw3StateLocked` reads cursor config into `ProductState`, then publishes it (IdtState.cpp:654–692).
- Cursor-page changes explicitly call `SyncDraw3State()` (Setting.cpp:3754).
- During settings rendering, that call queues `SettingBusinessKind::SyncDraw3State`; the worker invokes it later (Setting.cpp:5545–5550; Setting.cpp:1119–1120).
- Actual sync calls `PublishDraw3StateLocked()` (IdtState.cpp:1416–1442). Product publishing copies cursor settings and calls Host publishing (Draw3.Product.cpp:185–205).
- Host publishes bridge state and sends a control wake (H:3151–3159); bridge publishing increments revision (Draw3.Bridge.cpp:31–50). `PumpBridgeState` observes that revision and calls `SetCursorSettings` (H:1806–1840).
- Controller compares packed settings and requests cursor rendering on change (C:15142–15153).
- Waitable toggle only assigns config and calls `QueueConfigWrite()` (Setting.cpp:4664–4668). That queue entry writes configuration; it does not invoke state sync (Setting.cpp:1367–1370; Setting.cpp:1122–1124).
- **Bit 13 is occupied:** `kCursorSettingsValidBit = 1 << 13` (CS:175). Styles occupy 0–11, injected mouse 12, contact hiding 14–15, opacities 16–43 (CS:177–204).
- Equality is defaulted (CS:43); controller change detection uses `PackCursorSettings` (C:15151–15153).

**Inference**

Live propagation is available, but not automatic for Performance-page config writes. A new bool propagates live when the new toggle explicitly invokes `SyncDraw3State()` and serialization/controller forwarding includes it. No separate cursor-settings hash was found in the traced chain.

**Verdict — CONTRADICTS design’s bit-13 allocation; live behavior is otherwise feasible.** Use free bit **44**, update pack/unpack, and explicitly sync after toggling. Default equality includes the added field automatically.

## 6. Existing tests that change expectations

**Facts**

| Tests / location | Current assertion → required treatment |
|---|---|
| `RunCanvasPanProductionProbe`, C:9223–9299 | Pan mouse position bursts create no generation/control/render request and do not interrupt deadlines; state edges remain urgent. **Unchanged.** |
| Same, C:9303–9310 | “Ordinary drawing Mouse position” and nonwhiteboard Selection move call `checkUrgent`. The sample is actually **not in contact** after C:9289. **Unchanged hover expectations**, not contact tests. |
| Same, C:9317–9356 | Pan pen hover bursts produce no requests; Down/Up/inversion urgent; nonpan hover urgent. **Unchanged.** Add separate contact-position cases. |
| `RunEraserIdleWakeProductionProbe`, C:6677–6865 | Move at arm/recheck/wait boundaries advances wake generation once when subscribed, creates zero control markers; stationary deadline, exit, exception and stale handle behavior. **Unchanged:** fixture directly calls `PublishMove`, not cursor publication. |
| `draw3_contact_tests.cpp:105–176,178–208` | Subscription burst wakes once, no queue/control marker; invalid moves do not consume subscription. **Unchanged.** |
| `TestNavigationUrgentFrameWake`, `draw3_contact_tests.cpp:2121–2144` | Existing control wakes interrupt; subscribed Move does not interrupt deadline. **Unchanged**; extend with paced/mixed-wake cases. |
| Control/fallback tests, `draw3_contact_tests.cpp:223–813` | Queue/fallback/controlWakes contracts. **Unchanged** for `PublishControlWake`; add equivalent paced coverage. |
| Admission/quarantine tests, `draw3_contact_tests.cpp:935–1116` | Discard rejects Move; terminal/reuse remain safe. Preserve ordinary path; add opt-in visual-observer cases. |
| Compatibility input, `draw3_mouse_input_tests.cpp:94–126,228–262` | Admission/selection refuse stroke Down; boundary cancels without resuming held contact. **Preserve stroke expectations**; add distinct cursor-held behavior tests. |
| H:3247–3280 | Pending admission produces control callbacks; assertion is nonzero, not a mouse-position count. **Unchanged.** |

W contains the request consumer at W:1109, but no matching embedded wake-count tests. Searching Draw3 sources and headless `.cpp` tests found no direct public assertion of private frameWakeGeneration; tests infer interruption through elapsed deadline waits.

**Inference**

The cited baseline tests mostly protect behavior explicitly retained by A2. The design overstates how many existing expectations need changing. New nonpan in-contact mouse/pen coverage is necessary.

**Verdict — CONTRADICTS design’s blanket “update existing expectations” direction.** Keep the listed valid expectations; add paced-contact tests. Change quarantine expectations only for an explicitly opted-in observation path.

## Contradictions / stop items

- Bit 13 is occupied; use bit 44.
- Discarded touch neither updates nor supports `TryReadSnapshot`; add scoped observation plus paced notification.
- Compatibility admission-rejected mouse publishes `inContact=false`; preserve physical-held cursor state separately.
- Raw mailbox predicates include palm/suppressed contacts; track allowed rejection eligibility and reuse effective suppression.
- New Performance toggle must explicitly call `SyncDraw3State()`.
- Preserve existing pan, hover, F3 and stroke-isolation assertions; most require added coverage, not relaxed expectations.