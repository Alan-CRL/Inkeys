# ScrollFollow 产品接入独立检查（2026-10-21）

本检查在 Core 22:20 冻结源码后进行，范围包含 Business、Draw3 owner/Bridge/Host、IdtState、Bar/shortcut、布局配置及资源。已按当前 prd/design/implement/check 上下文、相关 native/native-desktop index 与 Quality Check、跨层/退出规范核对实际执行路径。没有派发子代理、Computer Use、普通产品启动、可见/前台/置顶测试、commit 或 push。

**结论：已发现的产品软件问题已修复，完整 ARM64 构建、实际 Business 控制专测、全隐藏真实 Host、原退出失败接管和一次完整 `--no-window` 均通过，可以继续私有持久化回归阶段。正式 Mag worker 的实际屏幕像素、系统窗口交接及用户可见端到端仍未验收；本报告不放行这些门槛，也不将父任务标为完成。**

## Findings (fixed)

- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`、`.h`，`Inkeys/Inkeys/Business/ScrollFollow.cpp`
  - Issue: Host Start 重建 `follow` shared_ptr、清零 `followActionHolds` 时没有与外部 FollowChannel/Begin/End/hold 使用共同保护门。Business 的旧 Context/Action 之后通过无代 `HoldFollowAction(false)` 归还输入原因，可能减掉新 Host 的 hold；旧 Context 的 End/Begin 也没有精确绑定取得的通道。
  - Fix: 经主会话确认，Start 按原 command→presentation 锁序换代；FollowChannel 按同 presentation 门复制。Begin/End 增加可选 expected Channel，hold 则必须显式传凭据并返回是否命中当前 Host。Business serviceHold 和每个 Action 保存原 Channel，所有相关 Begin、End 和 hold 归还均携原凭据；只有明确的进程整体 Shutdown 保留“当前 Host”语义。旧结果的纯 wake 不修改新通道。Stop/Retire 的 Context 读取与封口也留同 Business 短锁内，取消尚未执行的 toggle，不能在锁外用晚来的当前通道代替旧凭据。
  - Regression: 扩展原严格不可见 Host 用例，真实 Stop/Start 同一 Host 后，新通道持有自己的 hold。旧 release/acquire/Begin 都被拒绝，新 Begin 仍被自身 hold 阻挡；当前 release 正常生效，旧 End 无法封口新的 Active 会话。另在实际 Business 控制专测中覆盖旧 Action Cancel、旧 Service Stop/cleanup 不能归还新代 hold。

- File: `Inkeys/IdtMain.cpp::SetOffSignal`
  - Issue: 新增的有锁 `ScrollFollow::RequestShutdown()` 位于独立退出监督 Arm 之前；Host command/Capture 短锁若被故障线程持有，原先“任何业务等待前先建立退出监督”的保障会丢失。
  - Fix: CAS 后只调用已有的 `RequestShutdownNoWait()` 发布单调原子撤销并唤醒已存在 worker，然后保持原 Window BeginShutdown→Arm→offSignal→Failed 接管顺序。监督建立且原隐藏入队/Wake 后才调用有锁 RequestShutdown。Failed 分支仍只 Wake 并进入原截止接管，不构造 Business worker、不等待 Host/Capture 锁。
  - Regression: 最终产物执行既有纯无 GUI `--shutdown-supervisor-tests --failed-arm-only`。六个真实 SetOffSignal 故障子进程及两个身份负例均通过；没有启动普通窗口/崩溃报告 UI。原 Close/Restart 专用退出码、剩余截止、不进入后续 cleanup 与无重复 restart 都按既有断言验证。

- File: `Inkeys/Inkeys/Business/ScrollFollow.cpp`、`Draw3.ScrollFollowTest.h`、`IdtMain.cpp` 的原 protocol CLI
  - Issue: Core 原测试只覆盖 Channel、State 和目标注入的真实 Host，尚未执行 Business 的实际 Tick/Frame；无法据此确认 F0→ACK、Pause、初始化回滚与票据 hold。
  - Fix: 经主会话批准，在同一文件内增加受统一 harness 宏保护的外部边界接缝，仍执行生产 `Service::Tick`、public action 入口、`Frame` 和真实 `VisualMotionTracker`。仅替换 Capture Start/Stop/Snapshot、Host/Scope、投影/ACK、时钟与 UI 唤醒；线程本地绑定只对本 CLI 线程生效，普通进程继续使用原绑定。测试 Service 不创建控制 worker，不创建 HWND，不新增产品状态/API 或另一个追踪算法；接入原严格 protocol CLI，harness-off 继续早退。
  - Regression: 实际控制矩阵覆盖旧 Capture reason、新 Start/frame session 相关性、首帧纯白拒绝后有效 F0 重试、错误/旧 ACK 不得进入 Tracking、真实 ROI 图像及原像素平移进入 Channel、晚帧不能穿过 End、Presented 后仍等待原输出 ACK、真实 action publication receipt、占用未排空不能重启、普通视觉 1/2 拒绝仍 Tracking/第 3 次 Paused、严重 capture/API 单次立即 Pause、Paused 不自动恢复、F0 timeout 回滚、多个动作独立归还/幂等取消、Off action 防 Begin、退休/Shutdown 升级、跨 Host 旧凭据拒绝。

## 核对的实际合同

- Host Begin 先保留独立 End wake，原 256 普通命令+End+最终退出仍在 258 预算内；Channel Publish/Seal 同短锁冻结绝对 final，Bridge 固定槽保持前后 FIFO。Follow 命令跳过普通场景重放，旧 End/Presented/Handoff 均核 session。
- 唯一 owner 读取真实 Canvas、workspace/page GUID、Host/scene/device/client 身份；目标从 `base - total` 求绝对 viewport，沿原 ApplyCanvasContentTranslationChecked/SetViewport、热前像失效、可信 L2 和预算 Tile 恢复。没有重复 96/DPI 换算、第二 GPU 画布、Shader/导航门禁或 Stroke/UndoRedo/UInk 改动。
- 正常 End 跨帧挡住后继命令执行，状态、contact 终端与 Present 继续泵送；回执要求指定 final 的 full present 且 recovery/refresh 收敛，再等原工具输出与真实窗口 pair ACK。Retire/Shutdown 单调升级，CPU final 先于原 ExitAutoSave；不把 commandProcessed、accepted 或 outputReady 单独当作完成。
- 临时 selection 使用独立 epoch，不改真实工具、modeRevision 或 shortcutIntentEpoch；Publish、Reconcile 和 Window guard 都核同 epoch。Action 完成携带原 setter 在线程内取得的真实 Bridge publication receipt。Bar 完整回调留原 coroutine，haptic scope 在真正执行时建立；shortcut 保持原 expected/lease/具名 canvas 回执与按压归属，Follow contact gate 不关闭整个 shortcut route。
- UI 资源/布局已核对：enum 尾部追加、41 容量、官方 ID 白名单、显式 More/Setting 前按钮、最终 Selected/Disable、三语 SVG 与生成键齐全。窄 JSON 补缺使用既有 codec 验证后插入缺项，已有新项/其它插件原字段与 A/B 顺序保留，不恢复整套 B 自定义投影。
- 可选能力动态探测 Win8.1+、WOW64/Mag ready，未新增 Win7 缺失的硬调用。Console/verbose 复用原唯一 DiagnosticOutput Session；没有普通启动新 console。Release/harness-off 的 parser 在 GUI 初始化前拒绝新增内部 flag；本检查未另建 harness-off 运行产物。

最终 Host 窄签名：

```cpp
bool BeginFollow(uint64_t session, const shared_ptr<Follow::Channel>& expected = {}) noexcept;
void EndFollow(Follow::Disposition disposition = Restore,
               const shared_ptr<Follow::Channel>& expected = {}) noexcept;
bool HoldFollowAction(bool hold, const shared_ptr<Follow::Channel>& expected) noexcept;
```

主会话维护 `scroll-follow-product.md`、任务状态和矩阵，本检查将精确变更交其同步，不覆盖其它人的 spec/任务编辑。没有修改已通过 check 的 Capture/Tracker 生产源码及接口。

## Verification

- **Lint: pass。** 本层没有独立可复用的 C++ lint 目标；`git diff --check --ignore-submodules=all -- Inkeys InkeysHeadlessTests Scripts` 通过。46 个修改/新增源、工程和资源文本 strict UTF-8、原 BOM、物理 CRLF 及原 EOF 风格通过；IdtMain 原本无末尾换行，保持原状。三份工程和 SVG 共四份 XML 解析通过，新增项登记无遗漏。`Scripts/i18n.ps1 check` exit 0，en-US/zh-TW 各 426/426。静态及源码哈希保存在 `Build/ScrollFollow/product-check-static.json`。未递归检查/修改第三方子模块。
- **TypeCheck: pass。** 复用既有完整 `InkeysRepo.sln Debug|ARM64` wrapper；vswhere 定位当前 ARM64 原生 MSBuild，同一 PowerShell 规范单一 Path、禁 node reuse，normal host，`/m:1 /nr:false /p:BuildInParallel=false /p:PreferredToolArchitecture=arm64`，每进程期限 10 分钟。最终构建 exit 0，MSBuild 15.10s/外部 15,332ms，4 个既有 hashlib++/IdtMain 转换 warning、0 error；本次新逻辑无新增 warning。
- **环境与中间构建：** 首轮源码全部编译后，link.exe 在 IncrBuildImage 出现原有 LNK1000 内部故障，退出 1/44,487ms。只核实并删除生成 `Build/ARM64/Debug/Inkeys.ilk` 后，原配置完整重链 exit 0/10,740ms；没有改 SDK、项目参数、PDB、依赖或工具链来规避。最后 Shutdown 修复后原配置再次完整构建成功。日志 `motion-tracker-build-product-check-control-first*`、`-control-relink*`、`-final*`。

| 最终检查 | Exit | 外部耗时 | 证据（Build/ScrollFollow） |
| --- | ---: | ---: | --- |
| `Inkeys.exe --scroll-follow-protocol-test`，含实际控制矩阵 | 0 | 1,232ms | `product-check-final-targets-scroll-follow-protocol-test*` |
| `--state-mode-production-test` | 0 | 115ms | `product-check-final-targets-state-mode-production-test*` |
| `--bar-view-adjust-config-test` | 0 | 182ms | `product-check-final-targets-bar-view-adjust-config-test*` |
| `--scroll-follow-hidden-test`，包括同 Host 重启 | 0 | 1,398ms | `product-check-final-targets-scroll-follow-hidden-test*` |
| 最终新增内部 flag 的错位/重复/额外/后缀 argv，12 产品+3 headless | 全为 2 | 每条 <10s 限制 | `product-check-final-argv.json`，每条 stdout/stderr=0B |
| `InkeysHeadlessTests --no-window`，唯一完整运行 | 0 | 157,972ms | `product-check-final-full-no-window*` |
| `--shutdown-supervisor-tests --failed-arm-only`，8 项原无 GUI 回归 | 0 | 95,773ms | `product-check-failed-arm.json`、`.log`、`-stderr.log` |

完整 `--no-window` 的 Tracker failures=0、结尾 PASS animation correctness；stdout 42,332B，stderr 35B 只有 `[ScrollFollowProtocol] failures=0`。最后 Main 退出顺序调整没有改 Headless 及其依赖，最终 Headless 哈希保持一致，未重复三分钟矩阵。最终四项定向在最后主程序构建后重跑。

最终真实不可见 Host 的指定 final 跨 12 帧，其中 10 帧仍有 recovery，后继 Undo 到 handoff 后才执行。此前本次检查轮为 20/18，同样通过；帧数是调度结果，不是 FPS。Down/accepted contact seal/真实 Up、极大有限目标 clamp、最后 CPU viewport、旧页/历史和新 Host 旧凭据断言均通过。日志中的自建 HWND 全程 `visible=0`；ACK 明确使用 seam，没有调用真实显示、前台、置顶或电脑操控。

8 项失败接管回归中六个授权 child 自身按原专用退出码结束；五个正常预算样本约 15.03–15.06s，已过期样本约 16.53s 且失败返回后约 15ms 结束。六者均 cleanup_entered=0、durable marker=1、restart_count=0，两个身份负例按原拒绝码通过。marker 仅证明该夹具文件，不能当 UInk 保存证明。

最终产物 SHA-256：

- `Build/ARM64/Debug/Inkeys.exe`：`D4D2A2BACCA6490A39AF21AA47ABA71072E2EC0C38171F355021D874339682DE`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe`：`F332B191E60FEFBF175707C9A7CE050AAAC845E8288E841B728FAD906CEF06EE`。

本检查主要源码 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| `Business/ScrollFollow.cpp` | `94F766162F38BA145F7DCA7C88AD0A5480CAA6D5684451CEAFD1130A87C555AF` |
| `Draw3.Host.cpp` | `3B8C939316FDBAFA5BBAF2DECB6507B9B79BA03968DCA9DC900421B8CC28DFCD` |
| `Draw3.Host.h` | `8DB46EFFAF0B861CB5A8A7A8D4C2AC8DEDE7BBA9A03CBCEF457DA69A0875A745` |
| `IdtMain.cpp` | `7CC4C1CB10693CADF8C2FBA360F54170EEFBE95FD5B64C0E3B0CF8E30ED8CF7E` |
| `Draw3.HiddenWindowTest.cpp` | `98346D15ACE7F572157B8B7E90C72E0DD3E9A9723FA21DD7B5DAF480813DF44E` |

## Findings (not fixed) / 验收边界

没有留存本阶段已经确认但未处理的源码错误。以下是不能由本检查放行的设计或证据边界：

1. Business 控制专测的 Tick/Frame/Tracker 是生产代码；外部 Capture、Host 与 Window ACK 是接缝。真实全隐藏 Host 是另一层证据，输入来自绝对目标，窗口 ACK 仍是 seam。两者相加不等于实际 `Mag F0 -> Tracker -> Business -> 系统成对显隐 -> 底层滚动` 整链验收，也没有执行真实 Bar coroutine/按键/窗口交接的可见现场矩阵。
2. 正式 MagnifierThread 读回的变化像素、完整排除、持续帧率/资源、普通 Freeze 互斥仍须实屏验收；旧 owner 基础 PrintWindow 正确图像不能替代当前正式线程证据。同步 PW 无取消/强杀保证、异常 HWND 销毁与永久阻塞边界沿原 Capture 合同保留，不声称所有 join 有界。
3. UInk A→follow→B→编辑/退出、已保存 A→仅 follow→退出的实际文件读回及磁盘安装尚未做，留后续持久化阶段。共享 storage 四组既有 CLI 基线已由主会话完成，本检查未重复，也未将其算作新产品保存证明；没有新增任意 Desktop 冷载 API。
4. 未运行 x64/Release/harness-off、真实 Win7/8.1/WOW64、设备故障、DPI/显示硬件事件、笔/触控、浏览器/Word/PDF、长期性能及视觉交接。本轮严格权限禁止可见/前台/电脑操控，必要可见验收依然是最终停止门槛。最终跨架构与 PE 导入由主会话统筹，当前 ARM64 编译不扩大平台承诺。

后续可以在私有目录及真正不可见 HWND 中继续真实 Host→保存 worker→UInk 读回/恢复，复用本文新 expected Channel 合同。待全部允许的软件阶段完成后，若只剩实屏必要门槛，保持其未通过并向用户列明具体步骤。
