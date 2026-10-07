# 状态与 FineDial 生产补证独立复审（2026-10-03）

基线 `21a37239336864b2b334abedec4e88d484d10473`。范围仅为本轮 `IdtState.cpp/h`、`IdtMain.cpp`、`Bar.Interaction.cpp`、`Bar.Main.cppm` 的 325 行增量和所调用的直接依赖；未重审历史提交，不修改原有工作区内容。主会话进行真实 GUI 采样期间，本 reviewer 不构建、执行 CLI 或操控 GUI；采样结束后只读取主会话实际构建/执行日志并复核最终五文件 diff，不重复运行。

## Findings (fixed)

未发现需要直接修正的已确认源码问题；此次未修改源码。

## 核对结果

- `IdtMain.cpp:1048` 以 `CommandLineToArgvW` 的完整首参数识别模式，释放 argv 后检查恰有两个参数。合法参数执行后直接返回；附加参数返回 2，均早于配置、单实例互斥体、窗口服务、Host、COM/RTS 和渲染初始化。无该参数时沿原启动路径继续。测试线程是明确创建并 join 的短期受控回调，不是产品绘制线程。
- `IdtState.cpp:1188` 调用真实工具/PPT/条件 setter、锁内值快照和产品 `StateBridge`。工具映射、三个状态字段一致、revision 增量、重复意图不重新发布 bridge 均独立断言；32 次切换包含 HardPen，且没有强求其与 PPT Pen 等价。桥接参数检查使用实际 getter，并以显式宽度值、槽隔离和工具枚举防止仅比较两份同源结果。
- 延迟回调由 condition variable 在新 HardPen 提交之后放行；旧宽度、形状、Selection 和 PPT 四个请求必须全部拒绝。join 后核对模式 revision、完整状态值及 bridge revision/投影均不变，并检查旧值快照仍为 SoftPen。不是用 Sleep 或某一帧采样推测交错成功。
- `SyncDraw3State` 沿实际入口执行。直接依赖 `Window::Service::Impl::Submit` 在未 Running 时立即返回 false，`ReconcileDraw3PresentationState` 在 Host 未运行时早退；`ProductHost().ProductBridge()` 只取得已有对象。此次入口未调用 Window/Host Start、RenderPipeline Initialize、文件加载、设备创建或自动保存。状态 setter 的配置保存参数全部为 false。
- `Bar.Interaction.cpp:1290` 构造真实 `BarInteractionSession`，直接调用 Begin/End/Advance/Cancel/Commit 和原采样环。固定采样时间差只进入既有速度估计，不替代其算法。切工具后 Advance 的实际版本门清候选；跨代 Begin 必须丢弃旧惯性；Cancel 先断言清理再调用空 Commit。最后失效 Commit 的版本门阻止 `SetPenWidthIfRevision(..., true)`，因此当前代码不进入成功写盘分支。
- 失效 Advance 调用 `UpdateRendering(false)`；该路径只通知和请求尚未启动的 scheduler，不调用按钮状态更新、SVG/路径构建或设备初始化。空 memberAccess 未被这些方法解引用。测试中的全局 Bar 状态只存在于即将退出的专用进程，不续接正常 GUI 启动。
- 新声明和定义一致，Bar 导出沿既有测试入口形式，未增加工程文件/模块或第二份实现。三份 Idt 文件及 Bar.Main 保留 UTF-8 BOM，Bar.Interaction 保留无 BOM；工作树五文件均为 CRLF，无混合换行。Git 的仓库 blob 为规范化 LF，实际 diff 只有新增测试行，没有整文件格式变化。

## Findings (not fixed)

- 该入口不证明实际 HWND owner/光标/输入副作用、真实 Office 回调、FineDial 成功提交的配置保存或渲染呈现。真实可见自动检查及成功保存分支必须保留各自证据；这些是验收边界，不是本轮新增缺陷，不扩大五文件改动。
- 建议主会话在最终结果确认后，将此 CLI 的真实覆盖与上述边界补入 `native-desktop/cpp-conventions.md` 的 StateMode 验证说明；本 reviewer 不改规格或任务状态。

## Verification

证据根为 `Build/automation-perf/release-closeout-resume-20261003/`；复审实际 `.exit`、完整日志中的编译/总结和测试输出，不只确认 EXE 存在。

- Lint：本范围最终 `git diff --check` 退出 0；没有独立 C++ linter 执行。
- TypeCheck：主会话用原生 ARM64 MSBuild 对完整 `InkeysRepo.sln Debug/Release|ARM64` 增量构建，实际退出码均为 0，均有 0 Error(s)。Debug 使用 `/p:LinkIncremental=false`，Release 使用标准优化；五文件 diff 仍为 325 行新增，与静态复审版本一致。149/148 条 warning 包含未改区域数值转换及 Debug 非增量链接提示；检查到的五文件诊断均位于未修改旧行，不为零警告扩大修复。
- Tests：Debug/Release 的 `*-state.exit` 均为 0；两个 PASS 实际位于 `*-state.stderr.log`，stdout 为空。`*-state-extra.exit` 均为 2，无测试输出；两个 `state-empty-*` 工作目录仍为 0 个文件。Debug/Release `*-headless.exit` 均为 0，均输出 `layouts=216 failures=0` 和 `PASS animation correctness`。
- 最终 Release `Build/ARM64/Release/Inkeys.exe` 的实际 SHA256 为 `F017237F29F68758ADEC5A255F3B1D623DC2FF2AF6407AA25DE8A7FE576809FE`。独立 reviewer 未操控最终 GUI；其可见交互结果由主会话另行补证。之前的性能采样使用 `21a37239` 的未加 CLI 程序，不能把此处无窗口结果算作成功 Present、慢帧或光学性能证据。

## 此次 CLI 的直接安全结论

该显式参数只在自己的早期专用进程执行；没有配置路径或外部数据参数，没有文件/网络调用、窗口/Host 启动或原文件修改。所有有效宽色 setter 使用 `setMemory=false`，取消和失效 FineDial 提交在当前真实代码中不会进入保存分支；附加参数的实际退出 2 与空目录结果补证该边界。新增日志只包含固定阶段/检查名，无文稿身份或笔迹数据。正常产品策略、HTTP/HTTPS 回退、PPT/UInk 数据保护、Win7 呈现与所有历史安全结论未因本轮发生改动。此次范围未发现新增需要修复的安全问题；不据此声明全环境发布验收通过。
