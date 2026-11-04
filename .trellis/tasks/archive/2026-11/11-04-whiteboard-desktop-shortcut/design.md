# 白板桌面快捷方式与启动转发 — 技术设计

## Scope and Boundaries

在 11-03 已落地的 `DesktopShortcuts` 页、`Application.Shortcut` 配置、`ShortcutAssistantClass` 与 `Business::DesktopShortcut` 名称解析基础上扩展。不重做单实例/交接/更新框架，只在其准入点追加“转发”分支并保证参数在更新/SuperTop 链中不丢失。证据锚点：`IdtMain.cpp` 参数循环与准入（约 1959–2023）、更新（2093+）、`SuperTop`（2647）、`IdtPlug-in.cpp` 保障（1886–2060）、`Setting.Pages.cpp:1313` 页面渲染、`Other.Config.cppm:404` 配置、`IdtState.cpp:1517 RequestWhiteboardActive`。

## 1. 配置合同

`Application` 下新增 `WhiteboardShortcut` 组（NoUpload），字段同 `Shortcut`：`NamePreset:int=0`、`CustomName:wstring=L""`、`CorrectLnk:bool=true`、`CreateLnk:bool=false`。ID：0 缺省、1 品牌（仅简中）、2 自定义。非法值回退 0；品牌项在非简中归一为 0 并经配置队列保存。名称对读写沿用 Config 内受 rwMutex 保护的专用方法（新增 `Get/SetWhiteboardShortcutNamePreference`），不公开私有锁。旧 `Application.Shortcut` 字段与语义不变。

## 2. 名称解析与冲突

`Business::DesktopShortcut` 增加“种类”维度（App / Whiteboard），每种类有独立预设表与顺序映射：

- App：沿用现有 [Brand, Default, Annotation, AnnotationTool, Custom]。
- Whiteboard：简中 [Brand, Default, Custom]，其他 [Default, Custom]；ID 0/1/2 如 PRD。

`IsValidName` 复用；新增 `NamesCollide(a,b)`（`CompareStringOrdinal` 忽略大小写）。校验由页面草稿同时持有两类当前解析名：任一类名称变化时对另一类重新校验冲突，冲突视为该次输入无效（仅就地提示、创建禁用、不覆盖最后有效配置）。启动保障同样在入口检查冲突；已持久化的冲突（手改配置）时白板类回退缺省名并仍冲突则跳过白板保障，不删除不创建，记录日志。

## 3. 快捷方式内容与保障分类

`CreateShortcut(path, target)` 扩展为带 `arguments` 与 `iconLocation(path,index)` 的结构化请求（默认空，应用类调用点语义不变）。白板类：`SetArguments(L"-Whiteboard")`、`SetIconLocation(exe, -<资源ID>)`（用负资源 ID，不依赖图标序号）。

`IsShortcutPointingToDirectory` 目前只比对目标路径子串。新增 `ClassifyShortcut(path, exe) -> {NotOurs, App, Whiteboard}`：目标指向本 exe 且参数为空 → App；参数 trim 后与 `-Whiteboard` 不区分大小写相等 → Whiteboard；目标指向本 exe 但参数为其他 → 当作 NotOurs（不是我们创建的类型，不删除）。`SetShortcut(kind, name, correct, create, dir)`：

- correct：所选名称存在但类别/目标/参数/图标不符 → 重建；遍历桌面，只对**同类**且名称不同者统一到所选名称（先创建后删除，原语义）。
- create：所选名称缺失或类别不符 → 创建。
- 两类相互独立执行，同类遍历不触碰异类，因此应用类保障不会删除白板快捷方式，反之亦然。

启动入口 `SetShortcut()` 先后执行 App 与 Whiteboard 两类，共用一次桌面枚举与一次 `SHChangeNotify`；设置页即时开关和“创建”按钮走既有业务队列，命令增加 `kind` 字段，worker 仍只消费冻结快照（名称、路径、exe、开关、kind）。

## 4. 图标资源

- 源：`Inkeys/src/UI/whiteboardLnk.svg`（彩色，256×256；按用户 2026-11-04 预览反馈，采用用户已确认、其他 agent 完成的蓝色点阵卡片图标，去掉原笔和圆盘，中间改为横贯画面的白色书写线）。
- 产物：`Inkeys/src/whiteboard.ico`（16/20/24/32/40/48/64/128/256 PNG 压缩帧），`Inkeys.rc` 新增 `IDI_ICON2 ICON "src\\whiteboard.ico"`，`resource.h` 新增 ID，vcxproj `Image Include` 同步。`IDI_ICON1` 仍为最小 ID，主图标/任务栏不变。
- 生成：用已有 Python 3.13 + 可用的 SVG 渲染手段；渲染工具若不可用则在 implement 阶段先确认再选方案，不引入新依赖到产品构建。预览 PNG 放任务 research 目录供用户确认，不提交为产品资源。

## 5. 转发通道（单实例延伸）

### 5.1 选型
命名管道，不用 `WM_COPYDATA`：SuperTop 实例带 UIAccess、可能与普通启动处于不同完整性/桌面语境，窗口消息受 UIPI 约束且需放行过滤；管道可用显式安全描述符精确授权。

### 5.2 对象与权限
- 名称：`\\.\pipe\Inkeys.Command.<sessionId>.<安装目录 fileID>.<listenerPID>`，与 presence 键同一 fileID 来源，不同安装目录互相独立。每个进程独占一个端点；客户端只读枚举进程 PID 并寻找同前缀端点，最终须同时通过系统回报 PID=端点 PID、同用户 SID、同 session 和同逻辑安装根校验。
- 创建：首个创建者 `FILE_FLAG_FIRST_PIPE_INSTANCE`（多实例的后续实例见 5.5），`PIPE_REJECT_REMOTE_CLIENTS`，消息模式（实现时用于严格拒绝超长整包，字节流无法确认消息边界）；DACL 仅授予当前用户 SID（读写）与 LocalSystem，SACL 完整性标签设为 Medium，使普通启动的客户端可写入 UIAccess/较高完整性服务器。需验证 SuperTop 的 UIAccess token 保持用户 SID（`IdtSuperTop.cpp:305` 为复制 UIAccess 位，非换用户）。
- 客户端：`GetNamedPipeServerProcessId` 须等于被枚举的端点 PID，再校验服务端镜像位于同一逻辑安装根；服务端 `GetNamedPipeClientProcessId` 校验客户端镜像目录同样为本安装根（拒绝其他目录/路径别名）。

### 5.3 协议
定长小包：`{magic, version, command=OpenWhiteboard, 保留零}` ≤32 字节；服务端回 1 字节 ack：`Accepted`（已排队/已处理）/`Busy-Exiting`/`Unsupported`。不携带路径、命令行或可变数据；任何长度/版本/魔数/命令不符直接断开不回 Accepted。“Accepted”只表示请求已接纳，不承诺白板已显示。

### 5.4 服务端生命周期
- 目录级 `CommandLifetime` event 只在准入成功后的 Server.Start 建立，显式 DACL 仅同用户/SYSTEM；客户端只读 SYNCHRONIZE 探测其存在，不写该对象。它与 pipe Stop、原 presence 的纯关闭提前释放独立，保存已验证旧/新逻辑根的引用直至进程退场。建立失败属于启动保护失败；只读探测无需额外降低完整性标签。
- 命令寿命标记成立即进入可消费本地意图的状态；可选管道或监听线程创建失败不阻塞本次冷启动参数。真正的 BeginExit 才禁止消费本地意图；标记建立失败不得当作可继续的普通 pipe 失败。
- 单一监听线程（命名、可 join），在准入成功且 Config/工作区服务初始化到可接受请求之前就创建管道，使“启动中”实例能接纳请求并置位一次性 `pendingOpenWhiteboard`（合并重复请求为一次，latest-wins 无队列增长）。
- 主流程在工作区服务就绪且全部真实启动里程碑完成后，沿既有 100ms 主循环消费该标志，调用 `RequestWhiteboardActive(true)`；活动白板已存在则改为“恢复最小化 + 请求前台”，通过现有 `RequestWhiteboardWindowView(false)`。
- 退出：普通关闭/重启、fatal cleanup 与已确认的崩溃重启都通过主程序侧 `RetireLaunchCommands` 置 `exiting`，并取消管道 I/O；主线程消费使用同一 CAS 状态门，不得在退出后消费旧 pending；此后新连接得到 `Busy-Exiting`/连接失败。线程在 `Shutdown` 屏障内 join，加入既有 ShutdownTrace 阶段，不新增无限等待。
- 启动失败路径（isolated startup failure、单实例拒绝）不创建管道。

### 5.5 客户端流程（准入处）

```text
解析参数(-Whiteboard 精确匹配，其他行为不变)
Adopt() > 0 → 交接接任路径，不进入转发
-Whiteboard 时先探测管道：
  可连接 → 发包 → 收 ack
     Accepted      → 退出 0（不更新、不提权、不弹提示）
     其他/超时/失败 → ShowStartupMessage(无法转发…已有实例保持不变) → 退出 0
  不存在(ERROR_FILE_NOT_FOUND) → 允许多开则继续；禁止多开则只读探测 CommandLifetime：
      已有已准入 owner → 有限重试转发；仍失败提示并退出（不抢已提前释放的 presence）
      无 owner        → 继续
      探测错误        → 提示并退出
Acquire(...)
  Accepted        → 正常启动；LaunchState::openWhiteboard = true（一次性）
  Duplicate + -Whiteboard → 实例存在但管道尚未建立（启动中）：有限重试连接（总 ~3s）
                           → 成功转发退出；仍失败 → 提示并退出
  Duplicate + 无参数 → 现有提示并退出（不变）
```

探测与转发都在 `update.json` 处理之前，天然满足“已有实例时不碰更新”。CommandLifetime 只认已经准入的 owner，不按“有其他进程正在冷启动”推断退出态；两个无参数 owner 的白板冷启动仍进入原 Acquire 原子竞争，只有一个获准，另一个有限重试转发。因此既覆盖纯关闭提前释放占位后的退场窗口，也不让两个冷启动互相误拒。普通应用重开、允许多开且无可用监听者、已验真 Adopt 的行为保持原合同。允许多开时，探测先于 `Acquire`，因此有可连接实例即转发；探测不到才启动新实例。竞态：实例刚启动、管道未建立时，允许多开模式会多启动一份（可接受，用户本就允许多开）。

多实例监听：每个实例独占 PID 后缀端点，客户端通过 Toolhelp 只读进程快照尝试任一可连接端点，不保证选中特定实例。首个实例退出不改变其余实例的系统回报 PID；全部实例退出后新实例仍可重新监听。枚举、候选连接、写入和回执共用一个绝对截止，不因实例数量累计等待。已经连接却身份/协议失败不能当成“无实例”重新启动。

实现期验证发现：当前 Windows 对同名管道的多服务端实例调用 `GetNamedPipeServerProcessId` 时，首个创建者退出后仍回报首个 PID，导致严格的身份校验拒绝仍存活的第二监听者。已用真实两进程无窗口测试复现。因此有限调整原设计为 PID 后缀端点，保持身份校验，不放宽鉴权。证据和修复后结果见 `research/startup-implementation.md`。

## 6. 参数在更新/SuperTop 链中的保留

- 一次性意图以 `-Whiteboard` 保存在 `LaunchState`（`openWhiteboard`），不依赖 `commandLine` 字符串。
- SuperTop：`LaunchSurperTop(LaunchState::commandLine)` 已转发未识别参数，`-Whiteboard` 保持 addCommandLine 以经过三段式；final 实例作为首个业务实例消费意图。实现时核对 `-SuperTopComplete` 之后 final 的命令行是否仍含该参数，缺失则在三段式的 Arguments 中补充。
- 更新/回滚：`StartReplacement(image)` 原先只拼接 handoff 参数；为携带意图，新增可选 `extraArguments`（仅允许白名单 `-Whiteboard`），三处调用（IdtMain 约 2275/2331/2495）传入 `LaunchState::openWhiteboard`。该参数在 handoff 消费后由新进程解析；不削弱“参数不构成准入豁免”的合同（`Adopt` 仍依赖验真包）。
- 替换/SuperTop 前先停止接纳、合并 pending 并冻结一次性参数，避免接纳后丢在旧进程。交接失败或更新验证/发布失败即清除这次意图；原进程继续时重新监听，仅接收后续新请求。`StartReplacement` 拒绝非空且不是精确 `-Whiteboard` 的额外参数。
- `-Restart`/`-CrashTry`/`-WarnTry` 重启链不携带意图，避免崩溃循环中反复打开白板。

## 7. UI 设计（设置页）

`Setting.Pages.cpp` 的 DesktopShortcuts 渲染拆为可复用的“快捷方式类别块”函数，参数：标题 key、种类、名称草稿、两个开关草稿、事件输出；调用两次生成应用类与白板类。去掉“快捷方式保障”标题。`DesktopShortcutDraft` 增加 whiteboard 同构草稿与事件（`whiteboardNameCommitted/PresetChanged/Create/Correct/Create`）；ID 与 ImGui PushID 区分两类，不依赖翻译文案。窗口高度、滚动/转场身份测试镜像随结构同步。

## 8. i18n

新增：`DesktopShortcut.WhiteboardSection`（白板）、白板预设文本（缺省 Whiteboard 本地化；品牌项用固定资源仅简中）、名称冲突提示、转发失败提示（启动消息，沿用已有 `ShowStartupMessage` 的英文基线或接入 i18n，视该函数能力在实现时确定）。基准 zh-CN 后 `Scripts/i18n.ps1 sync/check`。

## Risks and Rollback

| 风险 | 缓解 |
| --- | --- |
| UIAccess/较高完整性服务器拒绝普通客户端 | 显式 DACL + Medium 标签；私有副本测试覆盖，未覆盖的真实 SuperTop 单列为人工验收 |
| 现有“修正”误删白板快捷方式 | 分类保障 + 回归测试（两类互不删除） |
| 白板冲突名称 | 双向冲突校验 + 启动入口兜底 |
| 参数在更新链丢失 | `extraArguments` 白名单 + 更新后启动测试 |
| 监听线程影响关停 | 纳入关停屏障与 ShutdownTrace，join 有界 |
| 旧版本回退 | 新配置字段旧版忽略；旧版遇 `-Whiteboard` 会当未知参数照常启动，不报错 |

回滚范围仅限本任务代码、字段与资源；不删除用户桌面内容。真实桌面、SuperTop/UAC、真实更新替换和视觉效果需用户人工验收，自动化限临时目录与无窗口。
