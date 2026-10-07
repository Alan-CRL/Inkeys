# E02 B：真实 wWinMain 启动故障夹具边界与红测方案

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本文件先于源码修改；等待主会话确认隔离合同。E01 当前源码与 E03 身份修补保持冻结。构建、进程运行和日志归主会话。

## 目标与实际入口

夹具只在现有 `--shutdown-supervisor-tests --startup-failure-only` 父入口下启动。父进程沿 `MakeUefTestDirectory` 创建唯一 `TestResults/release-hardening/Inkeys Shutdown Test UEF .../bin/Inkeys.exe`，复制当前可信 EXE，显式 HANDLE_LIST 继承 parent 进程、ack 事件和**新版本**固定 POD 映射。新内部 child selector `--inkeys-internal-startup-failure-child-v1` 在 `TryRunShutdownSupervisorEarly` 中精确识别；鉴权失败须在配置、单实例、窗口和系统状态访问之前以专用非零码返回。鉴权通过则只设置进程内私有 fixture 状态，返回 `false`，继续真实 `wWinMain`，不得改走重写的假启动函数。

此 selector 不接受环境变量、普通配置或从用户 UI 触发；无继承句柄的直接 CLI 调用拒绝。parent HANDLE 必须为本次父 PID，`QueryFullProcessImageNameW` 取得的父镜像与参数给出的绝对镜像通过 volume/file-index 同一性检查；root、bin、copied EXE 都拒绝 reparse，当前镜像必须与 `root/bin/Inkeys.exe` 同一。复用 E01 的身份辅助函数与目录创建，仅给新 selector 扩展独立验证；旧 UEF argc6/7 和 E01 argc9 信封原样保留。

## 协议与观察

新 observation 是 pagefile-backed mapping 的固定 POD，不复用 E01 的 48B 结构。包含 magic、version、bytes、枚举 site、已授权位、目标 fatal 发布位、Arm 开始/结果与原 deadline、gate 到达位、真实初始化结果、目标 failure code、请求/失败/Arm/gate 的 `GetTickCount64`、早/晚边界枚举及窗口数。`static_assert` 检查三架构 `sizeof`/关键 offset；HANDLE 的十进制参数做 `uintptr_t`/DWORD 边界验证，检查继承属性、映射大小和包头，未全通过不得设置任何 fault。各写入者独占字段，写 POD 后 `InterlockedExchange` 发布；parent 只在对应 published 位后读其字段，精确 child HANDLE 死亡后仍持有 mapping。记录目标是否真正到达，普通提前错误不能算目标场景 PASS。

child 首先在私有目录写并 Flush 哨兵；它只能证明夹具没有覆盖已提交文件，不能冒充 UInk/index 恢复。ack 表示鉴权和隔离状态安装完毕。parent 对目标 child 最长等待“初始化预算 + 原 15 秒 + 调度容差”；旧源码红灯只允许在上限后终止本轮持有的精确 child HANDLE，强杀永远不计绿色。绿灯需产品监督自行使同一 HANDLE signaled，且 mapping 表明目标 fatal/真实 gate、先 Arm、原 deadline 不重置。Close 不生成重启实例。无 hold 反例由 parent 只处理自己的提示 HWND（若需要），正常返回原分支码、没有固定 15 秒等待。

## 真正进入分支的 probe

只在鉴权后的 child 使用 `IsAuthorizedStartupFailure(site)`，并在原结果判定点生成合成失败；保留真实调用、真实资源所有权及错误记录。D004 在 `CoInitializeEx` 返回后故障，避免凭 `RPC_E_CHANGED_MODE` 假装 fatal；D005 在实际 DLL/activation/load 判定处故障，记录原 GetLastError 与 synthetic site，不删除真实资源；D003 在真实字体初始化返回后故障；B002 在 Main 本来启动 Bar 线程的位置发布实际 `SetBarStartupState(ClientRegistrationFailed)`，再由 `ActiveSnapshot` 生产路径决定退出。B002 只证明 Main 消费状态，不写成真实 Register 自然失败。目标发布才允许 `ShowStartupMessage` 前或对应 fatal logger 前的 auth-only gate 挂起；旧源码必须观察到目标发布、gate 到达、意图尚未 Arm、child 仍存活。新源码先 Arm 后到 gate，约原 15 秒由产品退出。最初只做 D004/D005/D003/B002；早晚 D002、D001 和已前移的 D101 等可分批扩展，不解锁 Whiteboard/NotReady。

## 必须拦截的外部副作用

复制目录本身不隔离系统状态。鉴权 child 的 `globalPath/pluginPath` 必须指向复制根；其配置、opt、log、嵌入 PptCOM DLL、临时文件只在此根。`QueryStartupState`/`SetStartupState` 注册表自启、`ShortcutAssistant.SetShortcut`、`StartDesktopDrawpadBlocker`、SuperTop 提权/辅助进程、PPTLinkage/Office 连接、`AutomaticUpdate` 网络、预启动 `update.json` 执行/旧 EXE 启动及其它可达外部进程创建，都在**已鉴权 child**显式跳过；正常产品路径不改。单实例名称由 copied image 目录派生，不占用户实例 mutex。若上述任一步无法先于副作用安全隔离，停止该 site 实施并交主会话准确缺口，不能启动试验。

目标分支 D004/D005/D003 在正常后期线程/PPT 联动之前；B002 到主循环需显式跳过真实 PPT 线程，并确保 fixture 的失败状态不会被正常 Bar 初始化覆盖。所有窗口仅属于 copied child，必要时指定私有 offscreen 矩形；parent 不按进程名称结束任何实例，不接触 Office 或用户文档，不使用 computer-use。正式产品无故障开关、无默认行为改变。专用测试夹具跳过的系统集成不由本测试证明。

## 分批顺序和冻结点

1. 主会话核此安全边界后发 `GREEN_HARNESS`；然后只实现夹具和原 fatal 顺序下的红测，不提前修 A。
2. 夹具静态检查、编码/CRLF、`git diff --check` 后发 `HARNESS_READY`；主会话独占完整 Debug ARM64 构建和 copied-child 红测。必须有目标 site/真实 `wWinMain`、gate、无 Arm 与旧 PID 仍活的原始证据。
3. 仅主会话发 `GREEN_IMPLEMENT` 后，按已审 A 设计实施 Arm-first 最小修复；根会话构建/跑 green、独立 reviewer 看 diff 和真实日志。
4. `Host::Start(false)` 内部失败 join/drain、Window owner join 与 DComp→ULW 的可取消清理合同属于未批准的 C，不在本 agent 写入范围；已 Arm 的正常保存排空仍由进程 15 秒保护。
