# Windows Ink 绘制光标技术设计

## Architecture

新增 `draw3.pen_cursor` 模块，负责纯 CPU 光标栅格化、`HCURSOR` 创建/销毁、设备状态类型和窄事件接收接口。`WindowController` 实现 `PenCursorEventSink` 并独占窗口级光标状态；RTS 只发布状态，不直接调用 GDI 或 D3D。

数据流：

```text
RTS in-air/contact/range callbacks
  -> PenCursorEventSink (non-owning)
  -> WindowController atomic cursor state
  -> coalesced private WM_APP message / WM_SETCURSOR
  -> SetCursor(custom or IDC_ARROW)
```

Windows 8+ 的 `WM_POINTER*` 通过动态解析 `GetPointerType`/`GetPointerPenInfo` 校验系统当前活动类型；Windows 7 缺失这些 API 时保持 RTS 回退。所有调用仅作用于当前 HWND 客户区。

## Contracts

- `RealTimeStylusInput::Initialize(HWND, ContactInputCoordinator&, PenCursorEventSink*)` 保存非拥有 sink；`Shutdown` 先禁用并移除插件，再清空 sink。
- RTS 对 Pen 的 hover/contact 分别发布 Hover/Contact 状态并保留 inverted 信息；Down 和接触 Packets 使窗口按工具选择隐藏 Pen 光标或显示不透明 Eraser，Up/InAir 恢复 Hover。out-of-range、disabled、error、tablet removal 和 shutdown 发布 Default。Touch/Mouse 不发布 Pen 状态。
- WindowController 用一个原子设备状态和一个原子 pending 标志合并刷新；状态变化可频繁发生，但消息队列最多只有一个光标刷新消息。
- `WM_SETCURSOR` 仅在 `HTCLIENT` 处理；非客户区继续交给默认过程。Pointer API 已确认 `PT_PEN` 时才使用 Pen 状态，系统切换到 Mouse 时立即设置 `IDC_ARROW`。
- 绘制线程发布活动 Pen 的有效工具覆盖；无活动 Pen 时使用 `ActiveTool()`。Eraser 工具或 inverted 状态解析为 Eraser 光标，Normal Pen/Highlighter Contact 才解析为隐藏。
- 自定义光标位图带透明 padding，热点位于几何中心。Pen 外轮廓直径取基准直径与 `6px × DPI scale` 的较大值，Highlighter 外轮廓覆盖 6.25x50 footprint；内框距离使用现有配置，外框为浅灰色 `#B8B8B8` 且透明度语义不变，填充颜色为当前 RGB、Alpha 0.5，全部像素为 Windows 彩色光标使用的 straight BGRA。EraserGripCircle 使用当前橡皮直径、4% 比例中灰圆环、三条 2.5% 比例竖线和 Hover/Contact 两档整体 Alpha。
- 自建 Pen/Highlighter `HCURSOR` 及两枚 Eraser `HCURSOR` 在重配或 WindowController 销毁时调用 `DestroyCursor`；共享 `IDC_ARROW` 不销毁。

## Compatibility And Failure

- 不引入静态 Pointer API 导入，保持 Windows 7 装载兼容；Pointer API 不存在或查询失败时不报致命错误。
- 光标创建失败时记录一次诊断并使用默认箭头，不影响 RTS 或绘制初始化。
- 不修改 contact payload、模型、renderer、shader、presenter 或三层画布。
- `HCURSOR` 的 Hover 显示、Down/Move 隐藏和 Up 恢复必须使用实体设备人工验证；当前实现不引入 GPU 覆盖层。
