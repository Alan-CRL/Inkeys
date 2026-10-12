# Research: L-shaped upper-left stroke start dead zone

- **Query**: Why can the brush not start a stroke inside an L-shaped region in the upper-left of the canvas, while a stroke that already started elsewhere can continue and leave ink there?
- **Scope**: internal
- **Date**: 2026-10-12

## Findings

### Root cause (static)

The L is the union of the four page-control HWNDs at their **creation** rectangles, all anchored at screen `(0,0)`:

| Window | Creation rect | Role |
|---|---|---|
| `PptBottomLeft`, `PptBottomRight` | `(0,0,230,80)` | horizontal arm |
| `PptMiddleLeft`, `PptMiddleRight` | `(0,0,43,165)` | vertical arm |

Right and left siblings share the same origin and size, so they overlap. The union is one L: a 230×80 top band plus a 43×165 left band. It is not a single corner rectangle.

Those windows are owned by Drawpad, so they sit above both canvas surfaces. `TopWindow` shows all four after startup without moving them. Outside a slideshow, PageControl does not commit a positioning frame, so the creation rects can stay visible. A new pen/pointer down is resolved against the topmost HWND. Page control does not publish a Draw3 contact. An already-open RealTimeStylus contact keeps receiving `Packets` on the Drawpad plugin, and the canvas surface still covers those pixels. The page-control windows have no layered bitmap until their first present, so the continuing ink shows through.

This is independent of DirectComposition versus UpdateLayeredWindow. Both canvas present modes sit under the same owned popups.

### Files Found

| File Path | Description |
|---|---|
| `Inkeys/IdtMain.cpp` | Creates the four page-control HWNDs at `(0,0)` with sizes 230×80 and 43×165, and registers them for touch |
| `Inkeys/Inkeys/Window/Window.cppm` | Default `WindowSpec` origin is `(0,0)` |
| `Inkeys/Inkeys/Window/Window.cpp` | Drawpad is owned by DrawpadPresentation; Bar and PPT popups are owned by Drawpad |
| `Inkeys/Inkeys/Window/Window.Legacy.cpp` | `TopWindow` shows all four page-control windows, plus Freeze and Bar |
| `Inkeys/IdtPlug-in.cpp` | Marks PPT UI ready at client registration; comment says the four windows do not commit a first frame when not presenting |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` | `WM_NCHITTEST`, show/hide after layout, initial `targetVisible=false` / `inputLocked=true` |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cppm` | Layout positions, hidden-mode policy, hit-accept policy |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | `StylusDown` publishes a new contact; `Packets` updates an existing binding |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` | `PublishDown` has no region test |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | New-contact rejection is admission, whiteboard selection, or presentation suppress |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cppm` | Preferred present mode is DirectComposition |

### Screenshot geometry

The attached crop is 436×454. Sampling non-red pixels against the creation constants:

- At y=70, red begins at x=231. The strip x&lt;230 is empty. That is the bottom-window width `230`.
- The horizontal empty band ends near y=78–80. That is the bottom-window height `80`.
- x in `[0,42]` stays empty much farther down than x≥43. That is the middle-window width `43`.
- Red reaches x=0 at y=165. That is the middle-window height `165`.

Stroke thickness makes the painted edge a few pixels inside those constants. The measured arms are the two creation rectangles, not a laid-out PPT plate (`165×42.5` DIP) and not a whiteboard control parked at the bottom corner.

These sizes are the raw `CreateWindow` pixels in `IdtMain.cpp`. A successful `ResolveSurfaceLayout` would multiply DIP by DPI and move the windows to the bottom or the side. A 150% layout would not stay 230×80 and 43×165. The crop matching the unscaled creation sizes means those HWNDs had not been through layout.

### Code Patterns

#### 1. The L is created at `(0,0)` and then shown there

`WindowSpec` defaults to the origin:

```88:89:Inkeys/Inkeys/Window/Window.cppm
		int x = 0;
		int y = 0;
```

Page-control specs override only the size. Bottom windows reserve the whiteboard 2×2 box. Middle windows use the compact side box. The comment says transparent areas are supposed to pass hits through PageControl:

```2901:2908:Inkeys/IdtMain.cpp
				if (role >= Inkeys::Window::WindowRole::PptBottomLeft &&
					role <= Inkeys::Window::WindowRole::PptMiddleRight)
				{
					// 底部共享窗预留白板 2x2 形态容量，透明区由 PageControl 穿透命中。
					spec.width = role <= Inkeys::Window::WindowRole::PptBottomRight
						? 230 : 43;
					spec.height = role <= Inkeys::Window::WindowRole::PptBottomRight
						? 80 : 165;
```

They are created hidden (`WindowSpec.visible` defaults to false) and registered for touch:

```2823:2827:Inkeys/IdtMain.cpp
		auto touchRegisterFuc = [&](HWND hWnd) -> void
			{
				RegisterTouchWindow(hWnd, 0);
				disableGestureFuc(hWnd);
			};
```

```2935:2946:Inkeys/IdtMain.cpp
		AddOverlayWindow(Inkeys::Window::WindowRole::PptBottomLeft,
			L"Inkeys4.BottomLeft;", L"Inkeys Ppt Bottom Left",
			Inkeys::UI::Ppt::WindowProc(), 0, touchRegisterFuc);
		AddOverlayWindow(Inkeys::Window::WindowRole::PptBottomRight,
			L"Inkeys4.BottomRight;", L"Inkeys Ppt Bottom Right",
			Inkeys::UI::Ppt::WindowProc(), 0, touchRegisterFuc);
		AddOverlayWindow(Inkeys::Window::WindowRole::PptMiddleLeft,
			L"Inkeys4.MiddleLeft;", L"Inkeys Ppt Middle Left",
			Inkeys::UI::Ppt::WindowProc(), 0, touchRegisterFuc);
		AddOverlayWindow(Inkeys::Window::WindowRole::PptMiddleRight,
			L"Inkeys4.MiddleRight;", L"Inkeys Ppt Middle Right",
			Inkeys::UI::Ppt::WindowProc(), 0, touchRegisterFuc);
```

`TopWindow` then shows every page-control window. The comment says this only wakes the first commit. The call is `Show`, and it does not set bounds:

```149:156:Inkeys/Inkeys/Window/Window.Legacy.cpp
	(void)service.Show(Inkeys::Window::WindowRole::Freeze);
	// Drawpad 已由 Draw3 首帧握手确认，显隐只能交给选择/内容状态机。
	// PPT 五窗按各自配置决定显隐，TopWindow 只负责唤醒其首次提交。
	(void)service.Show(Inkeys::Window::WindowRole::PptBottomLeft);
	(void)service.Show(Inkeys::Window::WindowRole::PptBottomRight);
	(void)service.Show(Inkeys::Window::WindowRole::PptMiddleLeft);
	(void)service.Show(Inkeys::Window::WindowRole::PptMiddleRight);
	(void)service.Show(Inkeys::Window::WindowRole::Bar);
```

`IdtWindow.cpp` has an older `TopWindow`, but `Inkeys.vcxproj` compiles `Window.Legacy.cpp` and lists `IdtWindow.cpp` as `None`. The legacy function is the one started from `IdtMain.cpp` (`topWindowThread = jthread(TopWindow)`).

Startup treats client registration as the PPT gate, and explicitly says the four windows do not commit a first frame when nothing is presenting:

```971:974:Inkeys/IdtPlug-in.cpp
	if (pptUiReady)
	{
		// 未放映时四个 PPT 分页窗口不会提交首帧；客户端注册完成即代表启动门禁已就绪。
		IdtWindowsIsVisible.pptWindow = true;
```

`Ppt::Initialize` calls `PageControl::Acquire`, which `RequestAll()`s once, then `PublishSnapshot`. That can run a Hidden-mode frame. Hidden mode calls `service.Hide` (`PageControl.cpp` around 2006). `TopWindow` does not `Show` until the bar first frame, freeze first frame, and `pptWindow` are all set (`Window.Legacy.cpp` 109–112, `IdtFreezeFrame.cpp` 98–102). The bar/freeze wait is longer than that one queued hide. The likely command order on the window thread is Hide (already hidden, no-op), then `TopWindow`'s Show. Nothing in desktop drawing requests another page-control frame, so the four HWNDs stay visible at the creation L.

Normal layout would not leave this L on screen. `ResolveSurfaceLayout` puts whiteboard boxes on the bottom corners and PPT boxes on the bottom and the vertical middle (`PageControl.cppm` 1016–1046). Hidden middle windows are moved off the left/right monitor edge (`ResolveHiddenSurfaceBounds`, `PageControl.cppm` 1226–1240). Hidden bottom windows are hidden in place (`ShouldPreserveSurfaceBoundsWhileHiding`). None of those paths produce a 230×80 ∪ 43×165 L at the origin. That shape exists only while the creation rects are still the real window rects.

#### 2. Why a new down does not start a stroke

Z-order puts these HWNDs above the canvas. Drawpad is owned by DrawpadPresentation so Bar and PPT stay above both canvases:

```935:938:Inkeys/Inkeys/Window/Window.cpp
				else if (role == WindowRole::Drawpad)
				{
					// 主 Drawpad 挂到 presentation surface 下，Bar/PPT 才会稳定高于两块画布。
					roleOwner = Handle(WindowRole::DrawpadPresentation);
```

Draw3 starts a stroke only when RealTimeStylus delivers `StylusDown` to the Drawpad plugin, which then calls `PublishDown`:

```1137:1141:Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp
			HRESULT STDMETHODCALLTYPE StylusDown(IRealTimeStylus* source, const StylusInfo* stylusInfo,
				ULONG propertyCount, LONG* packet, LONG**) override
			{
				// 产品构建不启用旧 RTS 宏；通过有界 Cursor 队列观察真实输入到达与发布。
				RecordInputDiagnostic("Down", "arrival", stylusInfo, nullptr, nullptr, false, 1, propertyCount);
```

```1220:1226:Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp
				bool published = false;
				if constexpr (kInterruptedStrokeReconnectSimulationEnabled)
					published = interruptionSimulation_.PublishDown(
						stylusInfo->tcid, stylusInfo->cid, deviceType, snapshot);
				else
					published = coordinator_.PublishDown(
						stylusInfo->tcid, stylusInfo->cid, deviceType, snapshot);
```

`PublishDown` rejects only a non-finite position or a full contact table. It does not test an L:

```737:744:Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp
	bool ContactInputCoordinator::PublishDown(uint32_t tabletContextId, uint32_t contactId,
		InputDeviceType deviceType, const ContactSnapshot& snapshot)
	{
		if (!HasFinitePosition(snapshot))
		{
			impl_->Count(impl_->downRejected);
			return false;
		}
```

`HasFinitePosition` is finite and inside `LONG` range (`Draw3.ContactInput.cpp` 77–85).

If a down does reach the drawing thread, `DrawingController` drops it only for an admission block, whiteboard selection drag, or presentation suppress. There is no geometric L test:

```9407:9428:Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp
				if (!input_.ContactAdmitted(handle))
				{
					// 被闸门拒绝的 Down 不能在重开后补画；保留其路由直至真实终态。
					input_.DiscardUntilTerminal(handle);
					return;
				}
				if (activeWorkspace == Bridge::Workspace::Whiteboard &&
					window_.SelectionMode())
				{
					// 白板“拖动”暂不启用平移，也不能让主 Drawpad 继续落笔。
					input_.DiscardUntilTerminal(handle);
					return;
				}
				if (Bridge::PresentationInputSuppressed(
					activeWorkspace, activePresentationLoadPending ||
					presentationLoadUnresolved()))
				{
					// 磁盘恢复完成前拒绝新输入，避免旧文件覆盖刚落下的墨迹。
					input_.DiscardUntilTerminal(handle);
					return;
				}
```

Page control's own hit policy does not start ink. While the surface has never been configured, the window proc asks to pass the hit through:

```174:175:Inkeys/Inkeys/UI/PageControl/PageControl.cpp
			bool targetVisible = false;
			bool inputLocked = true;
```

```2161:2176:Inkeys/Inkeys/UI/PageControl/PageControl.cpp
			if (message == WM_NCHITTEST)
			{
				std::unique_lock renderLock(renderTransactionMutex);
				if (!state.targetVisible || state.inputLocked) return HTTRANSPARENT;
				RECT window{};
				GetWindowRect(hwnd, &window);
				const POINT local{ GET_X_LPARAM(lParam) - window.left,
					GET_Y_LPARAM(lParam) - window.top };
				const auto logical = state.scene.PresentationToLogical(local);
				if (!logical.has_value()) return HTTRANSPARENT;
				const bool backgroundHit = state.scene.HitTestBackground(*logical);
				const bool widgetHit = state.scene.HitTest(*logical)
					!= Inkeys::UI::Bar::BarSurfaceNoWidget;
				// PPT 外框空白也属于拖动面；Whiteboard 仍只接收真实按钮。
				return ShouldAcceptPageControlClientHit(state.configuredMode,
					backgroundHit, widgetHit) ? HTCLIENT : HTTRANSPARENT;
			}
```

`ShouldAcceptPageControlClientHit` returns HTCLIENT for a PPT background hit (`policy.drag`) and for a whiteboard widget hit (`PageControl.cppm` 45–53 and 196–201). That path matters after a surface is actually shown by PageControl. It is not what paints the creation-sized L, because a successful frame also moves and resizes the HWND (`SetBounds` / ULW destination = `PresentationBounds()`).

The creation-sized L matches windows that were `Show`n and never presented. In that state the proc returns `HTTRANSPARENT`. Two static facts still let them eat a new pen down:

- They are visible, topmost in that rect, `WS_EX_LAYERED`, and have never received `UpdateLayeredWindow`. There is no per-pixel alpha mask yet. Touch was registered with `RegisterTouchWindow` at creation.
- Page control does not forward an unhandled pointer/touch down to Drawpad. `WM_TOUCH` is translated only into the page-control mouse path. Other pointer messages fall through to `DefWindowProcW` (`PageControl.cpp` 2471).

Whether the system actually honors `HTTRANSPARENT` for pen on a never-presented layered window was not executed. See Caveats.

#### 3. Why an in-progress stroke still paints there

`StylusDown` inserts an active binding. Later `Packets` look up that binding and publish a move. They do not hit-test the screen again:

```1370:1393:Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp
				const RtsActiveContactBinding* binding = activeBindings_.Find(
					stylusInfo->tcid, stylusInfo->cid);
				const RtsContextDecoder* decoder = binding ? decoderCache_.Resolve(*binding) : nullptr;
				ContactSnapshot snapshot;
				if (!decoder || !DecodeSnapshot(*decoder, propertyCount, lastPacket,
					ContactPhase::Move, QueryQpc(), snapshot))
				{
					...
				}
				...
					published = coordinator_.PublishMove(
						stylusInfo->tcid, stylusInfo->cid, snapshot);
```

The Drawpad window is created at the same origin with the primary monitor's pixel size (`IdtMain.cpp` 2835–2836, 2928–2934), so its client covers the L. Ink is drawn into that surface. The page-control windows above it have no ULW frame, so they contribute no opaque pixels. Continuing ink stays visible. Gold at the bottom of the crop is whatever is already behind the transparent canvas; it is not a second occluder.

No production draw path calls `SetCapture`. The continuing contact is the RTS binding, not a mouse capture.

#### 4. DComp and ULW

Preferred present mode is DirectComposition:

```30:31:Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cppm
	inline constexpr TransparentPresentMode kPreferredTransparentPresentMode =
		TransparentPresentMode::DirectCompositionVisualTree;
```

Drawpad drops `WS_EX_LAYERED | WS_EX_TRANSPARENT` and, when DComp is preconfigured, adds `WS_EX_NOREDIRECTIONBITMAP` (`IdtMain.cpp` 2892–2897). ULW is the fallback and applies `ComposePrimaryHitUnderlayAlpha` so empty primary pixels stay hit-testable (`Draw3.TransparentPresentation.cpp` 185–217). Selection ULW does not.

Neither of those makes an upper-left L. DComp hit-tests the whole Drawpad client. Primary ULW hit-tests every pixel whose alpha was lifted to at least 1. The L is a pair of popups above both surfaces. It is present on the DComp path and on the ULW path.

#### 5. Windows that do not form this L

- **Bar.** `InitializeWindow` places a 1×1 window at the monitor origin, then ULW resizes it to the content viewport (`Bar.Initialization.cpp` 223–228, `Bar.RenderLoop.cpp` 12902–12926). It does not handle `WM_NCHITTEST`. Empty pixels are cleared to alpha 0. That is a content rectangle, not this L.
- **Freeze.** Created with `WS_EX_TRANSPARENT` (`IdtMain.cpp` 2923–2924) and owned below the canvases. It cannot cover the drawpad.
- **Magnifier.** Full primary size minus one pixel, optional, and below the canvas owner chain.
- **StartupPreview.** `WM_NCHITTEST` returns `HTCLIENT` for its whole window (`StartupPreview.cpp` 115–116), but the window is the startup bar preview at the dock, not a persistent 230×80 ∪ 43×165 L.
- **Drawpad hit underlay / avoid-full-screen.** A 1px shorter window or an alpha-1 underlay does not produce this L.

### Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md` — page-control HWND ownership, PPT `165×42.5` / `42.5×165` DIP and whiteboard `230×80` DIP. Those DIP sizes are the post-layout geometry. The dead zone matches the pre-layout pixel sizes in `IdtMain.cpp`.

## Caveats / Not Found

- No process was launched and no live `GetWindowRect` / `IsWindowVisible` was read. The Show-after-hide order is inferred from `TopWindow` waiting on the bar and freeze first frames while `Acquire` has already queued its one `RequestAll`. A trace that shows the four HWNDs visible at `(0,0)-(230,80)` and `(0,0)-(43,165)` during the failure is still required.
- The page-control proc returns `HTTRANSPARENT` until `targetVisible` is set and the input lock clears. If pen hit-testing honors that return on a layered window that has never been given a bitmap, these HWNDs would not be the occluder. The symptom and the crop's exact creation sizes say the pen down is still taken by those rectangles. That honor question was not runtime-tested.
- If the user was in a slideshow or whiteboard session and a later frame succeeded, the HWNDs would move and the bottom window would change size. The crop matches the unreized creation L, which fits desktop drawing with no later page-control commit. That session mode was not confirmed from a log.
- `DrawingController` can still refuse a new contact for admission block, whiteboard selection, or presentation load suppress. Those are whole-canvas gates, not this L. They were not the geometry.
