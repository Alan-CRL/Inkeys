# Research: dev-side eraser pointer fixes for PR #219 merge

- Query: Identify the dev fixes that prevent Touch eraser pointer residue and the invariants a merge into `bugfix/eraser` must preserve.
- Scope: internal; read-only Git comparison of `bugfix/eraser@aea346c8` and `dev@5780f616`, merge base `94e07b25`.
- Date: 2026-09-27

## Findings

### Commits and ownership

| dev commit | Relevant change |
| --- | --- |
| `5fdd4a67aee3bff3b642323a290489101984c639` | Hides the old primary Pen/Mouse cursor and system arrow for Touch erasing; keeps each active Touch eraser circle separate; adds `touchCursorSuppressed_` and visual vs persistent authority. |
| `428860b56d7b5ae807e9e571f850571d7236f496` | Rejects unattributed `WM_MOUSEMOVE` at the last Touch position so it cannot reclaim the cursor after Touch Up; still permits actual mouse movement or identified Mouse input. |
| `31c48f4e47e88bda21adf122703583db45c50299` | Consolidates source/barrier/system-origin/position filtering in production `FilterMouseCursorMessage`. An `IMDT_UNAVAILABLE` + `IMO_SYSTEM` Move is rejected throughout Touch suppression even with button state or a different finger position. Adds sequence tests. |
| `f0560ecd099ed7c24e79e9f7b727a167575a55f0` | On `WM_MOUSELEAVE`, clears persistent Mouse owner via `cursorOwner_` rather than the temporary visual `CursorOwner()`, which can report Touch. |

All four commits are reachable from dev but not `bugfix/eraser`: its `WindowControl`, `PenCursor`, and `draw3_contact_tests` files equal their `94e07b25` versions. Only dev changes those paths after the merge base. A normal three-way merge should therefore carry them automatically; taking `bugfix/eraser` versions wholesale, or dropping dev test cases, would regress this repair. Both branches changed `.trellis/spec/native-desktop/input-and-ink.md`, so merge its independent contracts rather than choosing one complete file.

### Production invariants and code patterns

- `dev:Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:378-410`: Eraser/Laser and visual Touch authority hide the system cursor. `ResolveDrawingCursorVisualAuthority` may temporarily return Touch while the persistent owner remains Pen/Mouse/Unknown. Do not write Touch into the persistent owner just to hide the primary cursor.
- `dev:Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:634-640, 721-743`: `CursorOwner()` resolves visual authority. Touch begin latches suppression and clears the Mouse sample, except when a real mouse has already taken over a Touch Pan. Touch end only reduces contact count and does not restore old Hover (`:753-769`). New valid Pen sample clears suppression (`:802`); accepted real Mouse input does so at `:1717-1731`.
- `dev:Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:448-508`: promoted Pointer Mouse, stale Touch barrier and `IMDT_TOUCH/PEN` are rejected; identified `IMDT_MOUSE/TOUCHPAD` may take over. With Touch suppression, successful source query reporting `IMDT_UNAVAILABLE + IMO_SYSTEM` on `WM_MOUSEMOVE` is rejected regardless of button or last Touch coordinates. If the API is absent/failed, exact last-touch-position fallback remains; it must not classify all unknown or app-injected input as system input.
- `dev:Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1571-1655`: `WindowController` invokes the production filter before publishing any Mouse sample. The `buttonDown` exception applies only to the position fallback, never to the system-origin rejection. `WM_MOUSELEAVE` uses the persistent `cursorOwner_` at `:1740-1760`; using `CursorOwner()` there leaves stale persistent Mouse ownership when visual Touch suppression is active.
- `dev:Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:4066-4075, 4106-4160, 4174-4199`: primary Pen/Mouse rendering uses visual `CursorOwner()` while active Touch eraser circles come from each Touch runtime and its actual diameter. Speed-eraser merge changes to this file must keep those paths distinct; a Touch circle is not proof that an old primary hover circle or arrow is hidden.
- `dev:InkeysHeadlessTests/draw3_contact_tests.cpp:246-372, 373-429`: tests cover button-state system Move, multiple positions/missing position, source API fallback, injected input, Touch Up, late Move and real Mouse takeover. Preserve these alongside current speed eraser tests.

### 合并后的诊断归属补充

`DrawingController` 先生成主 Mouse/Pen 光标，再追加每个 Touch 橡皮圆环。当前分支的限频诊断与 dev 光标列表自动合并后，非 Touch 主输入分支原本只检查列表非空；主光标被抑制而 Touch 圆环存在时会误报首个 Touch 圆环为主光标。合并结果改为检查追加 Touch 前记录的 `primaryCursorCount != 0`，仅修正诊断归属，不更改 dev 的视觉所有权、渲染或输入过滤。

### Related spec and validation

`dev:.trellis/spec/native-desktop/input-and-ink.md:21-59` states the Touch eraser/primary cursor authority contract. It expressly separates Touch contact circles from the primary cursor, requires Touch Up/Cancel not to resurrect old Hover, and requires real Mouse/Pen recovery. The current branch's same file contains the speed/area eraser contracts; neither replaces the other. `.trellis/spec/native-desktop/draw3-integration.md:255-299` also requires Touch eraser cursor diameter to match the same contact's actual geometry and diagnostic snapshot, not another contact's cursor.

## Caveats / Not Found

- This is static research only. No merge, build, hidden-window or hardware test was performed here.
- `f0560ecd` changes one conditional and appears not to add a dedicated test; merge verification should inspect that condition directly.
- No external reference was needed. The existing dev spec and code provide the contract; hardware Touch/Pen/Mouse behavior still requires separate device validation.
