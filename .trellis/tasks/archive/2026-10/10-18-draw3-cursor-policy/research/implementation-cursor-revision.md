# 光标类型规则修订实施（2026-10-19）

本次以 cursor-policy-revision.md 为权威；原 implementation-cursor.md 的三场景可选高亮只作历史。最小差距是鼠标默认系统、笔默认圆形且两份类型独立，荧光笔始终矩形，设备边缘固定，软/硬笔才允许笔形。

修改范围仅共享 CursorSettings、Bridge 工具投影 helper、Host 消费、PenCursor resolver、Controller packed 状态及 Bridge/contact 生产测试。配置/设置/三语及 spec 由主会话负责。本次复用既有 PenIcon/热点/独立边缘透明度协议，不改 WindowControl、Renderer、HLSL、Win7 guarded 字节、墨迹/历史、输入身份或来源过滤；不构建/运行 EXE/GUI，不操作 Git 写入。

实现顺序：共享两类型默认和严格恢复 → 单一生产工具投影 → Host 与 Controller 成组传递 → resolver 笔形作用域与固定边缘 → 生产测试矩阵 → 编码/引用/差异静态检查后冻结，统一构建由主会话执行。

## 已完成实现

- 共享 CursorSettings 仅保留 MouseStyle（默认 System=0）/PenStyle（默认 Circle=0）；两个恢复函数均只有整数 1 进入可选笔形，其它整数恢复默认。删除三项运行时高亮字段。
- Bridge::ResolveMouseCursorStyleForTool 是唯一生产工具策略：软/硬笔返回用户选择，荧光笔固定 Drawing，其余 System。ResolveCursorSettingsForTool 只生成临时有效快照。Host 在 SetActiveTool 后传原始快照，Controller 按当前实际工具解析，不把临时投影存回原始设置。
- Controller 用同一个原子 packed 值成组保存原始两类型与显式产品启用位（4 | mouse1 | pen2）；首次和静止 SetCursorSettings 按当前 EffectiveDrawingCursorTool 同步 Window，每帧 primaryRuntime 确定实际 cursorTool 后再次投影，同一有效快照同步系统箭头与 resolver。静止类型变化继续使用公开 RequestDrawingCursorRender。
- resolver 的 penIconAllowed 只由软/硬笔入口启用，Mouse 按有效 mouseStyle、Pen 按独立 penStyle 选择笔形。荧光笔保留实际宽度 Rectangle，Pen Shape 保留原 Circle，Mouse Shape 不产生新主光标。
- 固定边缘只读来源：Mouse 固定高亮，非 Conflict 的 IntegratedPen 固定半透明，其余 Pen（包括 Unknown/Conflict）固定高亮，不改写 source。工具填充、24 DIP 图标、左上热点、DPI、CPU bounds/dirty、独立 Pen Contact、倒转橡皮、Laser/Touch/穿透合同及 legacy API 显式 opt-in 均沿用。

## 测试与静态检查

- Bridge 测试覆盖默认 System/Circle、仅 Pen/仅 Mouse 更新的独立快照与 revision、生产工具 helper 的两种选择 × 全部 11 个工具（包括四种 Shape）、两恢复函数的合法/非法整数。测试直接调用 Controller 所用生产 helper，未复制一份投影函数。
- Contact 生产 resolver 测试覆盖两 MouseStyle × 两 PenStyle × ScreenPen/Digitizer/Unknown/Integrated Conflict/External Conflict，软/硬笔类型与固定边缘、荧光笔始终 Rectangle/0.35/真实宽度、全部四种 Shape 排除、两类型独立、PenIcon 热点/DPI/负坐标、旧透明度 API 隔离、Contact、Eraser/倒转、Laser Hover/Contact、Touch primary 禁止以及新旧脏区等价字段。
- 已按当前权威 spec draw3-cursors.md 与 revision.md 静态核对数据流：Config/IdtState → ProductState（root）→ Host 原始快照 → Controller 按实际工具临时投影 → Window/resolver 共用同一有效快照；本代理没有改配置/设置/UI 线程边界。
- 全仓 C++ 搜索不再存在 mouseHighlightEdge/screenPenHighlightEdge/digitizerHighlightEdge 或旧 mousePenIcon 引用。固定高亮不再读取废弃运行时字段。
- 本次未改 WindowControl、Renderer、HLSL、guarded header。正式 PS 源 SHA256 仍为 7EA2564A3A5FCD244DA2124095FA0B262DBA590629C0A5ECC228F758936FF977；guarded 数组重新提取哈希仍为 43C7E21FE9AA531271E2036D44998A5DCE6EAC9C032B63965A5417AE293C8D57（31160 bytes），不重新生成。
- 九份源文件逐字节检查：UTF-8 可严格解码、原 BOM 状态和 CRLF 保留，无混合换行；git diff --ignore-submodules=all --check 通过。
- 本代理没有构建或执行产品/测试/EXE，没有 GUI/窗口 harness，没有 Git 写操作。当前测试结果应等待 root 的本轮串行完整 solution/headless；前次通过不算本次验证。Win7 真实视觉仍单列待验收。

源码在独立审查指出活动 contact 工具边界后做下述窄修，当前再次冻结。

## 独立审查修正：实际工具与原始偏好分离

审查确认 Host 的 state.tool 是新选择，而活动 contact 的 selectedTool/tool 继续锁存旧工具。若在 Host 提前把 MouseStyle=System 投影为 Highlighter 的 Drawing，Controller 就丢失原始选择；切工具还可能使正在绘制的矩形被新 Pen/Shape 设置隐藏，或在旧 Shape 上错误新增 Circle。这是可达源码边界，未通过取消接触或改写笔画规避。

最小修正：Host 在设置当前选择后仅传 raw cursorSettings；Controller packed 保留 raw MouseStyle/PenStyle。Controller 内部的 DrawingTool→策略类别映射只区分 Pen/HardPen/Highlighter，其余归 System 类别，策略继续调用同一个 Bridge helper。SetCursorSettings 用当前 EffectiveDrawingCursorTool 同步；每帧 buildDrawingCursorVisuals 在 primaryRuntime 最终决定 cursorTool 后再投影，将同一 effective.mouseStyle 同步到 Window.SetMouseUsesSystemCursor 和 resolver。未启用产品策略时整段跳过，legacy 行为保持。没有新增 Window 状态、取消 contact、模块导出、friend 或私有访问。

追加回归源码：

- Bridge 原始 System/PenIcon 经过实际 Highlighter→Shape→Pen 投影，得到 Drawing/System/System，原始 System 和独立 PenStyle 不丢失。
- Contact 测试使用同一生产纯值包装，验证旧实际 Highlighter/Shape 的矩形/系统行为与最后 Pen 恢复，避免沿用上一工具的投影快照。
- 既有 RunPenWidthProductionProbe 新增真实未附着 WindowController 的有效工具锁存/切换/释放检查：活动 Highlighter + 新选 Pen/Shape 保留 Drawing；活动 Shape + 新选 Highlighter 保留 System；释放活动工具后按 Highlighter 再恢复 Pen/HardPen 的原始 System。该用例调用 Controller 共用的真实映射和 Window 策略，不创建 contact、HWND、Host、RTS 或 GPU，也不新增测试入口。
- 主会话可执行既有 `Inkeys.exe --draw3-pen-width-test`；本代理未执行 EXE 或构建。静态 diff --check 通过，原 BOM/CRLF 保持，正式/guarded shader 未改。

本次窄修已完成并再次冻结，等待主会话统一审查/solution/headless/既有无窗口 production probe 的新结果；真实设备和 Win7 视觉边界不变。
