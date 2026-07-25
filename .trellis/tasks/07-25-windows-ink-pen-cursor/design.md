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
- RTS 对 Pen 的 hover/down/move/up 发布 Normal/Inverted，对 out-of-range、disabled、error、tablet removal 和 shutdown 发布 Default。Touch/Mouse 不发布 Pen 状态。
- WindowController 用一个原子设备状态和一个原子 pending 标志合并刷新；状态变化可频繁发生，但消息队列最多只有一个光标刷新消息。
- `WM_SETCURSOR` 仅在 `HTCLIENT` 处理；非客户区继续交给默认过程。Pointer API 已确认 `PT_PEN` 时才使用 Pen 状态，系统切换到 Mouse 时立即设置 `IDC_ARROW`。
- 绘制线程发布活动 Pen 的有效工具覆盖；无活动 Pen 时使用 `ActiveTool()`。Eraser 或 inverted 状态解析为默认箭头。
- 自定义光标位图带透明 padding，热点位于几何中心。外轮廓覆盖实际 5x5 或 6.25x50 footprint；内框距离使用 1px SDF，填充颜色为当前 RGB、Alpha 0.5，全部像素为预乘 BGRA。
- 自建 `HCURSOR` 在重配或 WindowController 销毁时调用 `DestroyCursor`；共享 `IDC_ARROW` 不销毁。

## Compatibility And Failure

- 不引入静态 Pointer API 导入，保持 Windows 7 装载兼容；Pointer API 不存在或查询失败时不报致命错误。
- 光标创建失败时记录一次诊断并使用默认箭头，不影响 RTS 或绘制初始化。
- 不修改 contact payload、模型、renderer、shader、presenter 或三层画布。
- `HCURSOR` 在实体设备接触时是否持续可见必须人工验证；失败时回到设计阶段选择 GPU 覆盖层，当前实现不自动回退。
