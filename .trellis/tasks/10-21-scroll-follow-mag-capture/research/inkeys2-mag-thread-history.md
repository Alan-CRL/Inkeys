# Research: Inkeys2 的 Magnification 同线程历史及 Inkeys3 对照

- Query: 定位用户记忆中 2025 年 2 月针对窗口置顶问题整理 Mag 初始化/窗口操作线程的提交，核对 exact commit/date/diff，并判断当前 Inkeys3 是否保留这项约束。
- Scope: mixed（本地源码与 refs 只读检查；GitHub 第一方仓库 API 读取历史）。
- Date: 2026-10-21
- Compared versions: Inkeys2 修订 `0c0aaf0e99a4d5ced0902c1d692855ef61449f96` 及其父提交 `0d743b5b64582788dbbd43213a4198e67fd05a82`；`inkeys2-final` 指向 `3bfc8cfcfdb8594e86861ba0de3b3463b5b0d7c8`；Inkeys3 定格基线 `1b8cb4b6bd76c7fb9e75c221f687d63be00e5a57` 与工作区 Window/IdtMain 调用链。

## Findings

### 可直接采用的结论

1. 确切提交为 **`0c0aaf0e99a4d5ced0902c1d692855ef61449f96`，2025-02-15 12:52:28 +0800（04:52:28 UTC），`[auto]20250215 1252 update`**。它在 `智绘教/IdtMagnification.cpp:65` 的 `MagInitialize()` 同行新增中文注释“**一系列操作必须在同一线程中完成**”。[完整提交及 diff](https://github.com/Alan-CRL/Inkeys/commit/0c0aaf0e99a4d5ced0902c1d692855ef61449f96)。
2. 该次变更确实让新 `MagnifierWindow` 线程承担初始化、Host/Child 创建、变换与色效设置、首次显示和消息循环，并让主线程等待创建完成后再建立后续窗口 owner 链及置顶顺序。
3. **不能把它表述为“从此所有 Mag API 都在同一线程调用”。** 同次提交后的 `MagnifierThread` 是另起的业务线程，仍直接设置排除、Source、刷新和透明度。`inkeys2-final` 也保留这一分工。
4. **当前 Inkeys3 保留了初始化/创建/初始设置位于同一窗口 owner 线程的安排。** `PrepareMagnifierWindow`、Host 创建、Child 创建和 `MagnifierChildCreated` 都由 Overlay `RunGroup` 的同步调用链执行；后续定格请求由另一个 `jthread(MagnifierThread)` 处理。这一分工不能作为“本次迁移新引入了所有 Mag 更新跨线程”的证据。
5. 用户说明的置顶问题修复意图与该提交中窗口 owner 链、样式及统一置顶的改动一致；提交消息自身只有自动更新时间，没有写明具体根因或运行验证，因此代码能证明结构变化，不能独立证明历史故障的唯一根因。

### Files found

| 文件/版本 | 内容 |
| --- | --- |
| `.git/config`、`.git/HEAD`、`.git/packed-refs` | 只读确认 remote 为 `Alan-CRL/Inkeys.git`，工作分支为 `feature/move`，本地存在 `origin/inkeys2-final` 与 2025 年标签。未运行 Git 命令。 |
| `智绘教/IdtMagnification.cpp` @ `0c0aaf0e` | 新增同线程注释、`MagnifierWindow` 与 `CreateMagnifierWindow`；原更新循环改名/保留为另一 `MagnifierThread`。 |
| `智绘教/IdtMain.cpp` @ `0c0aaf0e` | 删除显示枚举阶段提前启动的 `MagnifierThread`；窗口阶段同步调用 `CreateMagnifierWindow`，随后创建 owned 窗口链，业务启动阶段再独立启动 `MagnifierThread`。 |
| `智绘教/IdtWindow.cpp` @ `0c0aaf0e` | 显隐/样式检查加入 magnifier；按浮栏→PPT→Setting→Drawpad→Freeze→Mag 排序，并统一置顶 Mag 作为链底。 |
| `智绘教/IdtMagnification.cpp` @ `inkeys2-final` | 保留创建 owner 与更新业务线程分工；`MagUninitialize` 已移入创建 owner 消息循环之后。 |
| `Inkeys/IdtMagnification.cpp` @ `1b8cb4b6` | 直接 `MagInitialize/MagUninitialize` 的原定格路径；创建回调与更新协调器分开。该文件通过精确 SHA 的 GitHub API 读取，未把独立捕获原型或当前未提交编辑当作历史基线。 |
| `Inkeys/IdtMain.cpp` | 声明 Mag Host/Child 的 WindowSpec 回调，并另起定格协调器线程。 |
| `Inkeys/Inkeys/Window/Window.cpp` | Overlay owner 创建、STA COM、`beforeCreate/created/destroyed` 的同步执行路径和消息循环。 |

### Feb 15 的关键 diff 和前后路径

以下是原 diff 的关键片段，非完整补丁：

```diff
-BOOL SetupMagnifier(HINSTANCE hinst)
+void MagnifierWindow(HINSTANCE hinst, promise<void>& promise)
 {
+    if (!MagInitialize()) // 一系列操作必须在同一线程中完成
```

同一函数内的实际顺序（精确 SHA 文件行号）：

- `智绘教/IdtMagnification.cpp:63`：`MagnifierWindow`。
- `:65`：`MagInitialize()` 与同线程注释。
- `:84`：创建 `magnifierWindow`，样式包含 `WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE`。
- `:91`：创建以 Host 为 parent 的 `WC_MAGNIFIER` Child。
- `:112`、`:124`：`MagSetWindowTransform`、`MagSetColorEffect`。
- `:131`、`:132`：`ShowWindow(...SW_SHOWNOACTIVATE)`、`UpdateWindow`。
- `:135`：通知主线程创建完成。
- `:138`：同线程 `GetMessage/DispatchMessage`。
- `:149`、`:150`：`CreateMagnifierWindow` 调用 `thread(MagnifierWindow, ...).detach()`，随后 `future.get()` 等待。

`IdtMain.cpp` 同次 diff 把窗口创建改为：先 `CreateMagnifierWindow()`，再建立 `Freeze(owner=Mag)` → `Drawpad(owner=Freeze)` → `Setting` → `PPT(owner=Setting)` → `Floating(owner=PPT)` 的 owner 关系，并在 RTS 前对 `magnifierWindow` 调用 `SetWindowPos(...HWND_TOPMOST...)`。后面的业务线程启动区域另有 `thread(MagnifierThread).detach()`。

与此同时，`智绘教/IdtMagnification.cpp:161` 的业务 `MagnifierThread` 仍执行 `:177` 的 `MagSetWindowFilterList`；`:212` 调用 `UpdateMagWindow()`，而 `:23` 的该 helper 直接 `MagSetWindowSource` 与 `InvalidateRect`。它没有把这些调用投递回 `MagnifierWindow`。

**细节修正：** 在 Feb 15 这个精确修订中，`:238` 的 `MagUninitialize()` 仍位于业务 `MagnifierThread` 尾部；不能说这个提交已经让初始化、所有更新以及清理全部在 owner 线程。到 `inkeys2-final` 时，清理才已位于创建 owner `MagnifierWindow` 消息循环后的 `:159`，而更新仍在另一线程。

**前版本也要准确描述：** 父提交的 `MagnifierThread` 本来就顺序调用 `MagInitialize` 和 `SetupMagnifier`，再启动独立 `MagnifierUpdate` 并进入消息循环。Feb 15 的实质变化包括把窗口创建从业务启动时机提前为显式同步阶段、重新命名创建/业务入口、建立 Mag 在底部的 owner/置顶链和调整样式；不能仅从新增注释推断旧版初始化与创建此前必然跨线程。

### Inkeys3 实际线程路径

本节 Window/IdtMain 行号为研究时工作区；原定格 `IdtMagnification.cpp` 已用 `1b8cb4b6` 精确 SHA 对照，后续实现可能移动行号。

1. `Inkeys/IdtMain.cpp:3339` 为 `MagnifierHost.beforeCreate` 绑定 `PrepareMagnifierWindow`，`:3340` 绑定 `MagnifierHostCreated`，`:3341` 绑定 `ShutdownMagnifierWindow`；Child 的 created 绑定 `MagnifierChildCreated`。
2. `Window.cpp:243` 创建 `overlayThread_`，`:246` 在该线程调用 `RunGroup(false, ...)`。
3. `Window.cpp:918` 的 `RunGroup` 记录实际 owner thread ID；`:924` 调用 `CoInitializeEx(...COINIT_APARTMENTTHREADED)`，`:931` 同步调用 `CreateGroup`。
4. `Window.cpp:1017` 的创建顺序先 `MagnifierHost`、`MagnifierChild`，再其余 overlay。`:1057` 将 Host 设为 Child parent。
5. `Window.cpp:1145` 同步 `spec.beforeCreate()` → `:1214` `CreateWindowExW` → `:1304` 同步 `spec.created(hwnd)`，没有在这些步骤另起线程。
6. 因此基线 `IdtMagnification.cpp:272` `MagInitialize`、`:294` `MagSetWindowTransform`、`:307` `MagSetColorEffect` 和两个 HWND 的创建都在同一 Overlay owner；`ShutdownMagnifierWindow:327` 的 `MagUninitialize` 也随 owner 的 destroyed 生命周期回调执行。
7. `IdtMain.cpp:3714` 另外启动 `jthread(MagnifierThread)`；基线 `IdtMagnification.cpp:349` 的 `UpdateMagWindow` 用 `Win32MagnifierOperations` 直接调用 `:151` FilterList、`:162` Source、`:172` Redraw。这些包装没有把 Mag 调用派发回 owner，虽然 `PrepareHidden` 内的 Window Service Show 命令会由窗口 owner 处理。

可核对的线程表：

| 操作 | Feb 15 Inkeys2 | `inkeys2-final` | 当前 Inkeys3 定格基线 |
| --- | --- | --- | --- |
| `MagInitialize`、Host/Child 创建、初始 transform/color、窗口消息泵 | `MagnifierWindow` | `MagnifierWindow` | Window Service Overlay owner |
| FilterList、Source、业务更新/刷新 | 另一 `MagnifierThread` | 另一 `MagnifierThread` | 另一 `jthread(MagnifierThread)` |
| `MagUninitialize` | 业务 `MagnifierThread` 尾部 | 创建 owner 消息泵之后 | Overlay owner destroyed 回调 |

### External references / versions

- [Feb 15 原提交与完整 diff](https://github.com/Alan-CRL/Inkeys/commit/0c0aaf0e99a4d5ced0902c1d692855ef61449f96)。GitHub API author 与 committer 时间均为 `2025-02-15T04:52:28Z`。
- [Feb 15 Mag 源码](https://github.com/Alan-CRL/Inkeys/blob/0c0aaf0e99a4d5ced0902c1d692855ef61449f96/%E6%99%BA%E7%BB%98%E6%95%99/IdtMagnification.cpp#L63)。
- [Inkeys2 final Mag 源码](https://github.com/Alan-CRL/Inkeys/blob/3bfc8cfcfdb8594e86861ba0de3b3463b5b0d7c8/%E6%99%BA%E7%BB%98%E6%95%99/IdtMagnification.cpp#L62)。
- [Inkeys3 原定格基线 Mag 源码](https://github.com/Alan-CRL/Inkeys/blob/1b8cb4b6bd76c7fb9e75c221f687d63be00e5a57/Inkeys/IdtMagnification.cpp#L252)。
- 检索范围为 `inkeys2-final` 分支 `2025-02-01T00:00:00Z` 至 `2025-03-01T00:00:00Z` 的 16 个提交；路径筛选后 5 个提交修改 `智绘教/IdtMagnification.cpp`。Feb 10 仅 include/avoidFullScreen；Feb 16 删除临时 `Testi(123)`；Feb 28 两次分别恢复 Setting 排除、移除 Host `WS_EX_TRANSPARENT`。同线程注释和重排来自 Feb 15，不能误归到这些相邻提交。

### Related specs

- `.trellis/workflow.md`：研究持久化及使用现有 task；本研究未加载 implement/check JSONL。
- `.trellis/spec/native-desktop/index.md`：修改渲染/输入前先辨明窗口线程和业务线程所有权；证据等级不能混用。
- `.trellis/spec/native-desktop/cpp-conventions.md`：按全部读写/执行路径判断实际线程；不能由名称推断所有权或新增跨线程安全保证。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：Freeze 状态机与渲染 owner 的现状边界。
- `.trellis/spec/native-desktop/scroll-follow-capture.md`：真实共享控件像素和排除测试是门槛；API 成功或线程模型相似不等于捕获已可用。
- `research/freeze-owner-reuse.md`：原定格生命周期与主退出顺序已有研究，本次只补历史证据，不能借此扩大为完整异步捕获服务。

## Caveats / Not Found

- 源码/历史研究，没有运行编译、应用窗口、Computer Use 或真实像素实验；不声称新增 PrintWindow 读回已正确或最新。
- 只在当前任务 `research/` 写入本文件；未编辑代码/spec、未提交、未执行 Git 命令、未创建/修改远程内容。
- “一系列操作必须同线程”是仓库中的历史注释和约束证据；本研究没有把它升级为微软对所有 Magnification API 的公开线程保证。
- 用户已给出的置顶缺陷记忆是任务上下文；自动提交消息没有具体故障说明。明确可证的是同次提交的初始化/创建路径、owner 顺序和置顶代码变化。
- 仍需区分“当前实现保留了同线程初始化/创建”与“跨线程 Filter/Source/PrintWindow 在当前机器确实可靠”。前者已由调用链证明，后者必须以本次应用内实际测试判断，历史结构无法替代。
