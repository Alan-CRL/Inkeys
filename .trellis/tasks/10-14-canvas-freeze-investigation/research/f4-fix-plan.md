# Research: F4 最小修复方案

- Query: 确定 Settings 隐藏捕获残留的 owner-thread 清理入口、ImGui 输入生命周期、最小修改集合和真实生产路径回归。
- Scope: mixed；只读生产源码、现有测试、既有完整程序证据与 Microsoft 一手文档；本轮仅规划，不修改产品、不运行 GUI、不构建。
- Date: 2026-10-15（Asia/Shanghai）

## Findings

### 1. 单一优选方案

**在 Window Service 的 Setting 隐藏事务中释放仍属于该 HWND 的原生 capture；在 Setting 会话状态中锁存隐藏失效，使旧 ImGui backend/context 必须在渲染线程销毁，快速 Hide→Show 也不能复用旧输入。** 两部分各自留在现有线程所有权内，不在窗口线程直接清理 ImGui，不在渲染线程 ReleaseCapture，不修改第三方 ImGui backend。

此方案直接针对已证 F4：完整产品 Settings 内容区 Down → 按住时 Alt+F4 → Up 后，隐藏 Settings 保留捕获，Bar 橡皮点击持续失效；同进程仅取消捕获后恢复。该场景下真实新笔迹仍可绘制，故修复验收是捕获、工具点击和 Settings 生命周期，不宣称已经修复所有原始画布冻结。

### 2. 生产调用链与缺口

| 代码位置 | 已确认事实及对修复的约束 |
| --- | --- |
| `Setting.cpp:610–617` | WndProc 只在逻辑可见时、持 `settingImguiMutex` 向 backend 转发；隐藏后 Up 被跳过。 |
| `Setting.cpp:647–658,8860–8869` | SC_CLOSE/WM_CLOSE → `Setting::Hide()`；先 `SetVisible(false)`，再将 HideWindow 排入现有业务 FIFO，并请求 Settings 渲染。不能在此跨线程直接 ReleaseCapture。 |
| `Setting.cpp:262–267` | FIFO 的 HideWindow 调用真实 `WindowService().Hide(WindowRole::Setting)`。 |
| `Window.cpp:1733–1741` | Hide command 在目标 HWND 所属线程执行，当前直接 `ShowWindow(SW_HIDE)`。这是原生捕获清理的准确事务入口。 |
| `Window.cpp:1928–1937` | Setting 退出隐藏组也直接 ShowWindow；需使用同一私有清理逻辑，防止最终退出漏掉。 |
| `Window.cpp:1723–1727` | Show 执行期间收到 shutdown 的回退也会隐藏，Setting 分支同样覆盖。 |
| `Setting.SessionState.h:101–126` | Resolve 只根据最终 visible/epoch 判断 release；Hide→Show 在下一次 Resolve 前合并会继续旧 session。 |
| `Setting.cpp:8657–8664,8676–8691,8712–8717` | 真实会话销毁由渲染线程执行，依次关闭 DX11/Win32 backend、DestroyContext；这是清理 backend 按钮位和 IO 队列/ActiveId 的既有完整边界。源码没有名为 ReleaseSession 的独立函数；实际入口是 DrainSettingSessionOnRenderThread + SessionState::Release。 |
| `Setting.cpp:8818–8845` | Shutdown 已设置隐藏、排 HideWindow、PostControl 排空会话，最后停止并排空业务 FIFO。不能新增窗口线程等待渲染排空的环。 |
| `imgui_impl_win32.cpp:687–724` | Down 写 MouseButtonsDown 并 SetCapture；Up 清按钮位且只在 GetCapture()==hwnd 时 ReleaseCapture。 |
| `imgui_impl_win32.cpp:226–244` | Shutdown 释放 backend data，但不释放 Win32 capture；只 DestroyContext 不能修原生捕获残留。 |
| `imgui_impl_win32.cpp` | 当前 backend 不处理 WM_CAPTURECHANGED/WM_CANCELMODE；把隐藏后这些消息继续转发并不等于清理按钮位。 |
| `imgui.cpp:1747–1792` | ClearEventsQueue、ClearInputKeys、ClearInputMouse 分别处理不同内容；ClearInputMouse 不重置 backend 私有 MouseButtonsDown，也不单独保证清空旧事件/ActiveId。 |

### 3. 精确修改设计

#### 3.1 原生捕获：Window Service 的 Setting 专用隐藏事务

在 `Window.cpp` 内增加小型私有 helper，仅供 Setting 隐藏使用，并在上述三个 Setting Hide 落点复用。执行前提是已进入该 HWND 的 owner command 线程：

1. 在即将隐藏的实际 HWND 上检查 `GetCapture() == hwnd`。
2. 仅相等时调用 `ReleaseCapture()`，随后复查；若仍是该 HWND，沿现有命令错误/返回值报告失败，不把清理失败当成功。
3. 正常继续 `ShowWindow(hwnd, SW_HIDE)`。重复隐藏仍执行检查，不能依赖 `IsWindowVisible()` 来跳过清理。
4. capture 为 null 或为其他 HWND 时不调用 ReleaseCapture，不执行跨窗口取消广播。

使用直接 ReleaseCapture 而不是向 Settings 发同步 WM_CANCELMODE：此处本就位于正确 owner 线程，直接调用无需再经过 Settings 的可见性/ImGui 转发入口，也不依赖 DefWindowProc 的缺省取消策略。不要对通用 Hide 的所有角色统一加 ReleaseCapture；overlay 已有自己的捕获和 Draw3 终态合同，本次只扩展 Setting 分支。

`ReleaseCapture()` 会同步产生 `WM_CAPTURECHANGED`。因此在 `Setting.cpp::ImGuiWndProc` 的可见性检查和 `settingImguiMutex` 之前，对该消息作轻量直接返回 0（当前 backend 本来也不处理它）；不读取 context，不再次 SetCapture，不发 RTS Cancel。这样快速 Hide→Show 已将逻辑 visible 恢复为 true 时，此次释放产生的重入也不会新增 owner → ImGui mutex 的等待。不要在此把所有正常 Up 产生的 capture changed 都当成会话失效，否则每次正常点击都会重建设置。

此检查遵循现有 Window Service 的 owner-thread 捕获保护模式；GetCapture/ReleaseCapture 不是系统提供的原子 compare-and-release 操作，不应声称对任意其他线程恶意抢占存在额外原子保证。回归必须覆盖在执行隐藏前 capture 已转移到其他 HWND 的真实情况。

#### 3.2 ImGui：锁存隐藏失效，复用真实销毁路径

在 `SessionState` 增加一个很小的 pending invalidation 状态：

- `SetVisible(false)` 将会话标记为必须释放；随后的 `SetVisible(true)` 不清除此标记。
- `Resolve(epoch, hasSession)` 对已有 session 把该标记纳入 release 条件；最终可见时返回 release + rebuild，隐藏时返回 release + idle。
- 标记只在旧 session 确认 Drain 完成后的 `Release()` 清除，或在确认不存在旧 session、即将创建全新 session 的边界清除。
- `CommitEpoch()` 不得无条件清标记。Hide 可能发生在 Resolve 与当帧 Resume/CommitEpoch 之间；旧帧提交不能抹去新到的隐藏请求。
- 初次显示、创建失败后重试、没有 session 时的 Hide→Show，均在新建前消费旧失效，避免新建后的下一帧再无意义重建。新建边界之后到达的 Hide 则保留到下一次 Resolve。

状态读写继续只用现有短持有的 `settingStateMutex`。`RenderSettingFrame` 在已有 release 分支调用真实 `DrainSettingSessionOnRenderThread`；新建分支在没有旧 session 时完成上述状态确认。不要持 state mutex 进入 Drain，不新增 mutex、future 或 owner-thread 往返等待。

旧 Win32 backend data 的销毁清除了 MouseButtonsDown；旧 ImGui context 的销毁清除了 IO 事件队列、鼠标/键盘状态和 ActiveId。新会话通过现有 Init/CreateContext 取得干净状态。因此不需要拷贝 backend 私有结构、不需要伪造五个按钮 Up、不需要直接访问已销毁 context，也不需要仅清一半状态的 ClearInputMouse 补丁。隐藏时已经存在“销毁 Settings session”合同，此修改只是保证该合同不被快速重开合并掉。

#### 3.3 会话结束与锁序边界

- Hide/X/Alt+F4：逻辑隐藏失效与原生 Hide command 分别投递；二者不互相等待，最终旧会话消失且原生捕获释放。即使渲染先销毁 context，随后 owner 清理也不引用它。
- Shutdown：沿既有 FIFO/渲染 drain 顺序，native Hide helper 不等待 render，render drain 不等待 native Hide。隐藏组和 Show 的 shutdown 回退都覆盖同一 capture 保护。
- 纯 device epoch/resize failure 的 session 重建没有隐藏 HWND；不要从其 render-thread 清理分支直接调用 ReleaseCapture，也不要把全局 device 重建无条件转为 RTS 输入取消。这些情况应做生命周期回归，不能为 F4 引入跨线程同步释放链。
- 保留 `DeferDraw3SyncIfRendering` 与 Setting owner 不同步等待 overlay root refresh 的既有保护。新增路径不调用 SyncDraw3State，不持 ImGui 锁等待 WindowService，不持 Window queue lock 调用 ImGui。

### 4. 最小文件集合及未选择方案

产品预计只需要：

1. `Inkeys/Inkeys/Window/Window.cpp`：Setting 专用原生捕获清理及三个隐藏落点复用。
2. `Inkeys/Inkeys/UI/Setting/Setting.cpp`：WM_CAPTURECHANGED 早处理；新 session 创建边界确认；保留现有 render-thread Drain。
3. `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h`：隐藏失效锁存，避免 Hide→Show 复用旧 context。

回归预计扩充既有 `InkeysHeadlessTests/setting_session_state_tests.cpp` 与 `InkeysHeadlessTests/window_tests.cpp`。不新增项目或构建系统，不修改 ImGui vendor、公开 Setting module API 或主程序 CLI，仅为了内部状态断言而扩大公开 API 没有必要。

未选“只改 Setting::Hide”：该函数可能运行在 render/业务线程，不拥有 HWND capture；直接 ReleaseCapture 错线程，额外同步回 owner 又可能恢复既有锁环。未选“owner handler 持 settingImguiMutex 清 IO/backend”：隐藏/退出时 context 可已销毁，且窗口线程新增等待与 Present 的同步消息存在组合风险。未选“通用 WindowService Hide 释放所有角色”：扩大 Draw3/RTS/Bar 合同。未选“单独释放 capture 后不处理旧会话”：快速重开可能保留旧按钮位/排队输入，无法完成输入生命周期要求。

### 5. 回归位置与直接生产路径

分层测试必须明确各层证明范围：

1. **生产 SessionState 的无窗口测试**：扩充 `setting_session_state_tests.cpp`，直接包含真实头文件。覆盖有 session 的 Show→Hide→Show 必须 release+rebuild；重复 Hide 幂等；无 session 时快速切换不会无限重建；Resolve 后新 Hide 不被旧 CommitEpoch 清掉；Release 后恢复正常稳定可见态；普通 epoch/resize 行为不退化。
2. **真实 WindowService 测试**：`window_tests.cpp:347–350,496–498,558–586` 已创建独立 Setting owner 线程并实际执行 Hide/Show。通过测试窗口消息在目标所属线程建立 capture，再走真实 `service.Hide(Setting)`；覆盖 owned/unowned、重复隐藏、Setting capture、已转移到其他 HWND 的 capture，以及 shutdown/HideAll。使用真实 GetGUIThreadInfo/窗口可见性断言。此测试的 SettingOwnerProbeProc 不是完整 Settings WndProc，所以不得称为 ImGui/Setting 生命周期验收。
3. **完整重新构建的产品 GUI 回归是必需项**：复用主会话已有原生脚本和隔离程序，不使用独立 probe 替代。实际入口为 Settings 内容区 mouse Down → Alt+F4 → Up → Bar 橡皮点击；记录 Settings/overlay TID、隐藏前后 capture、Bar 工具状态和二进制 hash。验收 hide 后不再捕获、Up 后不残留、原坐标工具点击立即成功；同时保持新笔成功。
4. 完整产品补充正常 X 关闭、重复打开关闭、按住期间通过实际关闭入口隐藏、快速 Hide→Show/重开后第一次正常点击、退出时仍按住。重开后不得出现上次按下对应的延迟点击/拖动或按钮保持按住。纯状态测试为“渲染观察之前合并”的竞争提供确定性覆盖，实际 GUI 验证真实生产接线与行为，不用复制 WndProc 的模型声称已验证接线。
5. 依父任务运行完整 `InkeysRepo.sln Debug|ARM64` 与 `InkeysHeadlessTests --no-window`。窗口测试及上述 GUI 回归另行标记需要 HWND/桌面；`--no-window` 通过不代表它们已运行。完成后检查最小 diff、中文关键注释、编码/BOM/CRLF。

## Files Found

- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：实际 WndProc、业务 FIFO、Hide/Shutdown、ImGui coroutine 与 render-thread Drain。
- `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h`：生产会话决策状态，现有无窗口测试直接复用。
- `Inkeys/Inkeys/Window/Window.cpp`：HWND 所属线程事务、Setting Hide/HideAll/shutdown 回退。
- `Inkeys/additional/imgui/imgui_impl_win32.cpp`、`imgui.cpp`：真实 backend 按钮位、捕获、IO 清理与生命周期；本方案只读 vendor。
- `InkeysHeadlessTests/setting_session_state_tests.cpp`、`window_tests.cpp`：可复用测试入口及各自证明范围。
- 本任务 `research/window-owner-deep-audit.md`：完整产品故障/恢复与独立 owner 对照，后者没有完整 Setting/Draw3/RTS。
- `Build/CanvasFreezeInvestigation/window-interaction/capture-hide.json`、`capture-recovery.json` 及关联截图：已完成 F4 产品触发/恢复基线。

## External References

2026-10-15 只读核验 Microsoft Learn：

- [ReleaseCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-releasecapture)：作用于当前线程窗口的捕获；失败返回 0。支持 Windows 2000 起，不新增 Win7 之外的 API 要求。
- [GetCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcapture)：返回与当前线程关联的捕获窗口；查询其他线程应使用 GetGUIThreadInfo。
- [WM_CAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged)：主动 ReleaseCapture 也会收到消息；不能在其响应中重新 SetCapture。
- backend 事实以仓库当前生产源码为准；不借用较新 upstream 的取消行为覆盖本仓库实现。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`：共享 RenderPipeline/Settings session 生命周期、HWND owner-thread、业务 FIFO 与现有锁环隔离。
- `.trellis/spec/native-desktop/index.md`：现有产品/测试边界；无窗口测试不替代 HWND 验收。
- `.trellis/spec/native/quality-and-validation.md`：最小 diff、vendor 边界、真实生产入口验证；实际主程序构建服从用户 AGENTS.md 和主产品构建合同。
- 父任务 `prd.md` 的 R4/AC4：捕获释放、工具点击、重开/X关闭/退出、保护其他窗口捕获。

## Caveats / Not Found

- 本文件是待实施方案，本轮没有修复、编译或测试结果，未执行 task.py start。
- F4 故障及取消 capture 的恢复已由完整产品证明，对此具体触发器修复把握较高；快速重开/退出的生命周期安全仍需按本方案验证。
- 已证 F4 不能解释“真实新笔也完全失效”的所有现场；本机 mouse/Windows 11 ARM64 结果不能外推为真实物理触摸、Win7、多屏均已验收。
- 这里只修隐藏引起的 capture/会话残留，不添加全局 capture changed → RTS Cancel，不解决未闭合的 owner/DXGI 永久死锁候选。
