# 光标类型规则修订：Trellis 独立审查

日期：2026-10-19。当前权威：research/cursor-policy-revision.md、PRD 当前 R4-R6/D1-D4 与 draw3-cursors.md。本文件仅记录本轮两类型修订；check-implementation.md、verification.md 与三场景可选高亮研究均为首版历史证据，不能代替本轮代码验证。

本代理按 trellis-check recursion guard 直接审查，不派生代理；未构建、执行 EXE、启动窗口/GUI、Computer Use 或进行 Git 写入。root 统一负责本轮完整 solution Debug|ARM64 与 headless --no-window。

## 根范围阶段：已冻结

审查 Other.Config.cpp/.cppm、IdtState.cpp、Setting.cpp、三语源/generated header/snapshot 与 config_metadata_tests.cpp，未发现必须修复的源码问题，未改源码并已通知 root 冻结。

- 唯一配置为 Drawing.Cursor.MouseStyle/PenStyle，默认均0、严格整数0/1、NoUpload。bool/float/string/null/数组/超宽整数不能强转为笔形；ReadMini 先恢复对应选项默认，另一输入与其它配置保持。
- 三个退役边缘 bool 不在 schema/发布/UI 中；Write 在 OverlayDocument 后仅清理三个已知退役键，AutoClean=false 保留其它未知配置。合法既存类型值不被重置。
- IdtState 仅发布两个枚举，设置仅上“鼠标”/下“笔”两个类型下拉，无任何边缘或触摸隐藏开关。说明测量和绘制使用相同字体与wrap宽度，卡片高度随说明增加，两个控件行及字体/style 栈由局部范围平衡。
- 设置沿用 deferred SyncDraw3State→同一业务FIFO→冻结QueueConfigWrite，不在ImGui锁内访问Controller/HWND或写盘。
- 配置测试使用真实Config读写，覆盖四组合重启、ReadMini非法回退/隔离、缺失默认、退役键忽略与清理、未知键保留、冻结副本和NoUpload。

本阶段静态检查：i18n check退出0，英文/繁体各370/370（100%）；10个根范围文件的UTF-8/BOM/CRLF身份与Git checkout filters一致（core.autocrlf=true）；git diff --ignore-submodules=all --check退出0。不得将首版372条或首版build/headless通过写为本轮结果。

## Core 首次冻结审查

已读 implementation-cursor-revision.md，并审查共享两枚举/恢复函数、Bridge生产工具helper及快照、Host、PenCursor、Controller packed状态与Bridge/contact测试。

- 两枚举与两个恢复函数、4|mouse1|pen2的显式启用状态、固定设备高亮、PenIcon仅软/硬笔、原Contact/Touch/Eraser/Laser路径符合本轮静态合同。
- Bridge/contact测试调用实际生产helper并覆盖工具/两类型/来源矩阵，未复制一份工具投影实现。公开RequestDrawingCursorRender仍仅设置请求位/wake，不引入HWND/GPU访问。

### 发现：选择工具与活动contact的有效工具可不同

root 提示检查后，沿生产调用链确认不是“切换工具必然终止旧contact”：

1. Host::PumpBridgeState 只按state.tool投影并写SetActiveTool；其工具更新没有关闭或取消已有contact。RefreshPresentationInputGateLocked的门禁是停止/场景/目标就绪条件，没有tool变化条件。
2. Controller initializeStroke明确沿物理批次锁存selectedTool，活动runtime直到真实Up/Cancel/场景边界才终止；activeCursorIterator会将仍活动runtime的tool写入Window::SetActiveDrawingCursorTool。EffectiveDrawingCursorTool优先该值。
3. 因此Mouse Highlighter按住时切到Pen/Shape且MouseStyle=System，首次core版本会把packed和Window策略设为System，但正在画的有效工具仍Highlighter；resolver会关闭矩形而Window显示箭头。反向切换也可把旧Pen/Shape活动工具误设为Drawing，导致Shape出现Circle。

已将缺陷和证据发给root协调core owner修复，未直接改owner文件。最小修复应保留原始用户MouseStyle，并按实际有效/runtime工具将同一生产投影用于系统光标与resolver；不能为了修光标而结束原contact或改变墨迹批次合同。此项在owner再次冻结后复查并记录关闭证据。

## 验证边界

本轮未改变Win7 Window/正式HLSL/guarded字节，仍需独立核对最终身份；本机无窗口逻辑不能证明Win7实际双光标、首触闪现、静止穿透交接或真实DPI/热点画面。root的本轮构建/无窗口测试结果须另行记录，不能沿用首版verification。

## Core 再次冻结：缺陷关闭复查

Owner 已按上述生产链证据完成窄修，本代理仅只读复查、未改源码。

- Host 在SetActiveTool后传原始cursorSettings，不在Host丢失MouseStyle用户选择。
- Bridge::ResolveCursorSettingsForTool只是值拷贝包装，继续调用唯一ResolveMouseCursorStyleForTool；Controller的DrawingTool分类只用于选择Pen/HardPen/Highlighter/System策略类别，没有第二份策略。
- packed状态保留raw MouseStyle/PenStyle，bit2为产品启用位，bit0/1为两类型。SetCursorSettings仍在首次默认值时同步Window，静止仅笔类型变化也能通过公开的窄请求触发重绘。
- buildDrawingCursorVisuals在primaryRuntime最终决定cursorTool后重新计算effective设置，并把同一mouseStyle同时用于Window系统光标布尔与resolver，抬起后从仍保存的raw选择重投影。原始配置/Bridge快照不被临时策略覆盖，也没有结束或改写旧contact。
- 未启用bit2时不进入新增工具投影/Window同步块，resolver继续得到nullptr，legacy独立API路径保持。PenStyle、固定来源边缘、Contact/Touch/Eraser/Laser未被本次缺陷修正扩展。
- Bridge/contact新增真实生产值包装的Highlighter→Shape→Pen投影与原始System选择保留断言。既有RunPenWidthProductionProbe还使用真实未附着WindowController，覆盖活动Highlighter+新Pen/Shape、活动Shape+新Highlighter、释放活动工具后Highlighter及Pen/HardPen恢复；断言同时检查Window布尔和独立PenStyle。该入口原本不附着HWND或初始化Host/RTS/GPU，未新增CLI/module导出/friend或GUI测试。

据最终调用链与上述回归源码，先前记录的“state.tool与实际活动工具不一致导致错误隐藏/新增主光标”缺陷已关闭。实际测试执行结果仍待root本轮构建和现有无窗口probe输出。

最终静态核对：core 8个既有文件的UTF-8/BOM/CRLF身份与checkout一致，CursorSettings.h维持UTF-8无BOM/CRLF；公开重绘请求声明唯一且仍在public；check manifest含当前revision且已移除旧高亮澄清项。git diff --ignore-submodules=all --check退出0。前述正式PS源码与guarded 31160 bytes/SHA256 43C7E21FE9AA531271E2036D44998A5DCE6EAC9C032B63965A5417AE293C8D57身份保持，未重新生成或改动shader。

另外单列既有相邻边界：活动contact的墨迹样式锁存Down值，但普通光标appearance沿用最新ProductVisualStyle重算；切工具可改变预览宽色。该路径在本轮两类型修订前已存在，不是raw/effective策略修正引入。已告知root，本代理遵守minimal diff没有自行扩大源码修改范围；不将当前类型策略回归称为活动笔迹/预览全部宽色场景的实机验收。

## 本轮独立审查结论

当前两类型配置/UI与最终core接线静态检查完成。发现的1项实际工具投影缺陷已通过owner窄修并复查关闭，没有其它阻止串行构建的新源码问题。源文件保持冻结。本代理未构建/执行产品或测试EXE；root须单独记录本轮solution/headless/既有--draw3-pen-width-test结果，不能复用旧verification.md。Win7真机和GUI视觉验收仍未执行。
