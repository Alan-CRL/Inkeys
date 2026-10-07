# Draw3 首发透明 presenter 选路收口

## 边界与行为

- 对应 F-006：生产 `kTransparentPresentModes` 由 DComp→DWM2→DWM→ULW 缩为 DComp→ULW。`allowDirectComposition=false` 时自动选路只尝试 ULW。
- `Impl::TryInitialize` 是启动强制模式、自动候选和 `RecoverFromRuntimeFailure` 的共同入口。它在配置 HWND、创建 swap chain 之前拒绝 DWM2/DWM 等非正式模式并输出禁用日志；拒绝前仍调用既有 `ReleaseAttempt` 清理上一尝试。DComp API 缺失时也在修改 HWND 样式之前跳到下一候选。
- 历史 DWM enum、presenter 实现和 Host 映射保留，不构成正式可选入口。`DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` 原语句未变，不引入 bitblt 或其他 swap effect 回退；用户报告 Win7 SP1+仅 KB2670838 上 FLIP 可用，本轮未独立复现。
- 对应测试在 `Draw3.HiddenWindowTest.cpp`：自动模式断言结果只可为 DComp/ULW，legacy HWND 且禁 DComp 时只能为 ULW；两种强制 DWM 请求均应失败、Host 停止、Window Service 样式回调次数不增加，随后继续验证强制 ULW 与原有 dirty/alpha/命令合同。
- 独立复审 R-DWM-02 后，`Host::Impl::Start` 在 `bridge.Reset`、`AttachExternal` 和 AutoSave 启动前拒绝强制 DWM；`TryInitialize` 门禁仍保留为共同防线。隐藏测试还比较拒绝前后的真实 HWND exStyle 与 `MICROSOFT_TABLETPENSERVICE_PROPERTY`，避免只靠样式回调次数判断无副作用。
- 独立复审 R-DWM-03 后，隐藏测试用生产 `ShouldPreconfigureNoRedirectionBitmap()` 探测 DComp API：缺失时首个 HWND 使用 legacy 样式，自动模式验证 ULW，不执行强制 DComp 和 DComp 专属样式切换试验；后续独立强制 ULW、dirty/alpha/命令测试仍执行。API 存在时保留原 DComp 场景。

## 本轮检查与限制

- `git diff --check` 无输出；生产两文件保留 UTF-8 BOM+CRLF，隐藏窗口测试保留 UTF-8 无 BOM+CRLF；差异限于上述选路、注释和测试预期。
- 只读 PowerShell 断言退出码 0：候选数组精确为 DComp、ULW，`TryInitialize` 在 `activeMode`/窗口配置前设门禁，恢复的强制/自动分支均回到该入口，FLIP_SEQUENTIAL 原赋值存在，两种禁用模式的 hidden-window 拒绝断言存在。这是静态检查，不替代编译或运行。
- 静态追踪：`Host::Start` 将 `HostPresentationMode` 映射为 `TransparentPresentationOptions`；`Initialize` 的强制/自动分支与 `RecoverFromRuntimeFailure` 的强制/自动分支都调用 `Impl::TryInitialize`。自动及恢复列表均使用同一个二元素数组。
- 按 AGENTS.md 和本子任务运行边界，本轮未运行会创建 HWND 的 hidden-window 集成测试，也未并发启动 MSBuild。代码编译、Win7 SP1+仅 KB2670838 上硬件 FL11.0 有/无与 WARP、真实 DComp/ULW 透明度、FLIP 呈现和恢复仍待主任务记录与目标机人工验证。
- `.trellis/spec/guides/cross-layer-thinking-guide.md` 的 Presenter fallback 段仍含旧 DWM 顺序；已通知主 agent，该文件不在本子任务写入范围。
- R-DWM-01 指出的 DComp 运行期持久故障与 HWND 创建期样式冲突，涉及 Host/Window Service 生命周期，本子任务未修改；仍须故障注入并保留发布门禁，不能由本次启动选路静态检查升级为已修复。
