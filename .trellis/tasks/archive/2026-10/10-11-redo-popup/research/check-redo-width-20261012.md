# 2026-10-12 独立检查：Redo 2×2 点击宽度暂态

## 范围与依据

- 依据本任务 prd/design/implement 顶部本轮宽度修复要求。
- 本轮生产 diff 仅 Bar.Button.cpp：删除 PresetHoming 的 Recall.hide=true/false 两次中间写入，加中文原因注释。没有修改 RenderLoop、通用 hidden 网格、窗口固定宽度或动画参数。
- 检查代理只读审查源码，主代理统一构建和运行 headless；未开 GUI、未 commit/push。

## Findings (fixed)

- File: Inkeys/Inkeys/UI/Bar/Bar.Button.cpp
- Issue: Redo 2×2 稳定布局中 Recall 是隐藏的 twoOne。完整状态刷新曾先在 PresetHoming else 分支执行 Recall.hide=false，再调用 ApplyRecallChromeVisuals 把它隐藏。宽度扫描能读到 Redo twoTwo + Recall twoOne 同时可见，新增 75 DIP 列；Undo 原本可见，没有同一中间显隐过程。
- Fix: 原实现代理删除两个中间 Recall 显隐赋值，仅保留最终 RecallChrome 投影，以及投影之后已有的非白板“选择+空页”精简门禁。独立复核确认该调用窗口已消除。

## 真实调用链证据

- Bar.Interaction.cpp 主栏业务点击调用 clickFunc 后执行 UpdateRendering()（默认 updateState=true）。
- Bar.Main.cpp UpdateRendering 在自身 static mutex 内调用 StateUpdate；Bar.Button.cpp StateUpdate 调用 PresetHoming。
- Bar.Initialization.cpp 另起交互线程；渲染回调由独立 RenderPipeline 线程持有。
- BarRenderLoopState.barButtonSet 是 owner.barButtonSet 的引用，不是整组按钮显隐/尺寸快照；CalculateButtonLayoutWidth 逐个读取 IsVisible 和 size。
- IsVisible 只组合原子 userVisible/hide，不存在保护 PresetHoming 与宽度扫描的同一整组事务。
- 2×2 后可见 2×1 的网格分支增加 BarButtonTwoSideDip+BarButtonGapDip=75 DIP。删除中间 unhide 在保持期内避免错误的额外列输入，不掩盖正常布局动画。

## 正确性与回归

- 最终 ApplyRecallChromeVisuals 仍设置当前大小/显隐，不固定布局；3 秒后的 IdleElapsed 正常解析最新 resting layout。
- 非白板、选择模式、空页的最后门禁仍把 Recall 和 Redo 一同隐藏；其他按钮精简处理不改。
- RecallChrome 3.0 常量、accepted 命令快照、禁用颜色、popup 互斥身份/内容提交均没有本轮改动。
- 既有 Recall 回归新增生产策略生命周期可见集合/尺寸检查，覆盖 Redo-only、2×2 Redo 点击、恢复 Undo 可用性、popup Undo、关闭、末步耗尽、灰色连点、工作区及 2.99/3 秒稳定调整。
- 新生产源码断言要求 PresetHoming 的最终 RecallChrome 投影之前不存在 Recall/Redo 显隐写入。独立 Python 检查确认旧 HEAD 含临时 unhide、当前源码不含，且选择空页最后门禁仍存在。
- 两个改动源码/测试文件 BOM 与 HEAD 一致，工作区 CRLF、零 bare LF；git diff --ignore-submodules=all --check 通过。

## Findings (not fixed) / 验证边界

- 未发现本轮窄范围补丁引入的新行为缺陷。
- 未执行真实 GUI、线程交错复现或 HWND 宽度呈现测试；源级断言和纯策略可见集合不是并发网格运行验收，并要求仓库源码可访问。
- 未改动的 ApplyRecallChromeVisualsLocked 仍逐字段写 size/hide；合法 3 秒 settle 从 Redo twoTwo 转 Compact 时，Undo 可先显示、Redo 尺寸随后改成 twoOne，渲染若跨此写入序列读取仍可能拿到中间组合。这只是本轮未覆盖的发布边界，未做线程交错运行复现，不能等同已经复现的额外用户缺陷。已向主代理记录，不扩大本轮明确仅删除 PresetHoming 临时显隐的修改范围；需要处理时应确定整组状态发布/读取边界，不能用通用 hidden 网格重构掩盖。

## Verification

- Lint/静态：通过 diff --check、旧/新 PresetHoming 源码断言与 BOM/CRLF 检查；项目未配置独立 lint 命令。
- TypeCheck/编译：通过。主代理本轮完整 InkeysRepo.sln Debug|ARM64 构建退出 0、0 errors、3 个既有 warnings，耗时 29 秒；日志 Build/recall-width-debug-arm64-20261012.log。
- Tests：通过。主代理运行本轮新生成 InkeysHeadlessTests.exe --no-window，退出 0、stderr 空、结尾 PASS animation correctness；RecallAttribute 与 EraserAttribute 均 failures=0。日志 Build/recall-width-headless-20261012.log。GUI/并发呈现未运行，源级断言要求仓库源码可访问。
