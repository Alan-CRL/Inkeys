# F-018 Product 命令 Running 门独立复审

日期：2026-09-28。只读审当前 `Draw3.Product.cpp`、`Bar.EraserAttribute.Test.cpp` diff，以及 Host/Bridge、Bar Clear、Facade、Whiteboard 调用链和 `TestResults/release-hardening/` 已保存日志。未运行 GUI、构建或采样。

## 结论

**源码修正可接受；状态为已修复待真实启动/关闭交错验证。** `PublishProductCommand()` 在 `Draw3.Product.cpp:180–189` 保持原先 `productStopping` 双重检查与 `productCallMutex` 范围，只在锁内额外读取 `Host::Running()`；`Host::Running()` (`Draw3.Host.cpp:1373`) 是 acquire 原子读取，不增加另一把锁，未形成新的锁逆序。Host 未启动/已退出时返回 `NotRunning`，不把命令放进默认 `running_=true` 的独立 `StateBridge` 队列。

## 调用链和竞态

- H0 的 `StateBridge` 构造默认 `running_=true` (`Draw3.Bridge.h:255`)；`Host::PublishCommand()` (`Draw3.Host.cpp:1655–1660`) 原样调用 `bridge.Publish()` 并在 Accepted 后唤醒输入。离屏测试没有启动 Host，旧 `PublishProductCommand(Clear)` 仍返回 Accepted。`BarEraserAttributePanel::Execute:470–489` 将 Accepted 视为真正已入 Draw3 FIFO，接着恢复模式、关闭面板并唤醒渲染；新门使这个无消费者状态返回 NotRunning，保留面板和模式。主栏 Clear 的双击资格只在 Accepted 时建立 (`Bar.Button.cpp:77–84`)；拒绝后第二击继续尝试 Clear。Undo/Redo Facade 与按钮忽略返回值，未启动时现在不排入虚假队列，相关 UI 状态不因它们改变。Whiteboard Previous/NextPage 将结果与 Accepted 比较，拒绝时 `pageSwitching=false` (`IdtState.cpp:1253–1265`)。
- 产品正常启动时 `Host::Start` 先 `bridge.Reset()` (`Draw3.Host.cpp:1038`)，再在绘制线程创建前置 `running=true` (`:1081`)；可用绘图 Controller 的启动握手之后才返回成功。`IdtMain.cpp:2029–2057` 先等 `StartProduct()` 成功，`:2144` 才启动 Bar interaction/render 和 `StateMonitoring`，故正式 UI Clear/Whiteboard 命令不会因为新 Running 门在启动前被意外拒绝。并行外部调用在 Host 的 `running=true` 与首帧 ready 之间仍可能入桥接队列，这是既有 Host 语义；若随后初始化失败，已接受命令没有消费保证，当前离屏回归不覆盖该极窄启动失败交错，不能宣称每个 Accepted 最终完成。
- `StopProduct()` (`Draw3.Product.cpp:32–44`) 先置 `productStopping`，以 `productCallMutex` 排空已经进入的调用，再在**锁外**执行 Host::Stop。命令若先拿到调用锁，可在 Stop 标志之前 Accepted，Host::Stop 以 `StopWithFinalCommand(PrepareExitAutoSave)` 保留已接受 FIFO 并排在退出屏障前；若 Stop 标志先发布，锁内复查拒绝。最终保存屏障不经过本次 Product 命令门。`Host::Running()` 没有反向锁，未发现本次新增的 productCall/Bridge/Stop 锁循环。
- 其它 Product 入口的语义不同：`PublishProductPresentationTarget` 与 `PublishProductPage` 已有 Host Running 检查；`PublishProductState/Workspace` 是无返回值的目标快照发布，启动后主线程会重新发布；它们没有把命令 Accepted 告诉 UI 的相同风险。独立 `StateBridge` 的默认运行语义保持不变，桥单元测试仍可单独构造并发布命令。

## 测试与像素边界

- `Bar.EraserAttribute.Test.cpp:39–52` 在 Map 后按像素偏移访问前，新增 context/source 非空和 `x/y < source->GetPixelSize()` 检查；无效离屏采样返回零像素，避免负几何转无符号或超出目标 bitmap 后读取映射外内存。这是测试工具的安全边界，不改变产品绘图/命令语义。
- 新测试在橡皮属性 Clear 的 `click(0)` 前直接断言真实 `PublishProductCommand(Clear)==NotRunning` (`:359–364`)，并保留“拒绝后面板仍开”的实际 Pointer/Execute 检查。红灯阶段现有无 HWND WARP/D2D CLI 在第 0 个 Clear item 的 Up 以 `0xC0000005` 退出；当前 `ui3-product-command-guard-release-arm64.log` 记录完整 Release|ARM64 Solution Build succeeded、0 error，同一离屏 CLI 后由主 agent 记录 exit0/failures=0。保存的 stdout 空、stderr 仅 `[PageControlScene] failures=0`；原始命令/退出码应以父任务 validation 记账。`ReadEraserTestPixel()` 的调用仅在视觉采样位置 (`:326,337,476,557`)，`click(0)` 内没有读像素；因此 Clear Up 的消失更直接对应 Running 门，而新增像素边界是后续采样防护。两改动未单独 A/B 跑，因果结论限于该调用链与前后同一 CLI。
- 该 CLI 不创建产品 HWND/RTS，也没有真正运行 Draw3 Host；证明的是**未启动 Host 时必须拒绝 Product Clear**及离屏组件回归，不证明正常 Draw3 Clear/Undo/PPT 命令消费、透明 Present 或启动/停止真实交错。隐藏 HWND/真 GUI 仍需按仓库授权与目标环境单独验证；性能收益没有从此修正推断。

`git diff --check` 本次退出码 0。审查未发现本改动新增的严重锁顺序或正式 UI 命令丢失路径；上述启动握手失败和真实关闭交错保留未验证。
