# 白板启动转发实现记录

## 变更与边界

- `Launch/IdtCommandPipe.h` 仅允许固定 16 字节 OpenWhiteboard 请求；版本、魔数、命令、保留字段、消息长度都检查，1 字节 Accepted 只代表接纳。
- 各监听端点为 session + 安装目录 fileID + listener PID；客户端只读枚举进程寻找端点，同时验证系统回报 PID=候选 PID、用户 SID、会话、目录物理身份。服务端也验证客户端 PID/SID/目录；远程管道客户端被拒。
- 每端点 DACL 只授予当前用户/SYSTEM，Medium 强制完整性标签允许普通入口写入 UIAccess/较高完整性实例；客户端使用 SECURITY_IDENTIFICATION 限制服务器冒用。
- 准入/转发早于目录写探针、更新文件处理和 SuperTop。只在冷启动准入后执行原目录权限探针；转发成功直接退出。
- 目录级 CommandLifetime event 仅在准入后建立，显式同用户/SYSTEM DACL、只读 SYNCHRONIZE 探测；其引用独立于 Stop 和原 presence 的 ReleaseForClose，至进程退场才释放。逻辑根重绑定同时保留旧/新已验真根引用。建立失败由主程序作为启动保护错误处理；只有管道失败仍允许本次本地白板意图在就绪后消费。
- 启动中 pending 为有界合并位。真实启动里程碑和工作区服务完成后，主循环取一次 pending；白板显示中不重复进入，最小化恢复同一窗口组，其余场景调用既有进入合同。
- 普通退出、fatal cleanup 和确认的崩溃重启退役同一监听状态。主循环使用 TakeReadyPending CAS，不消费已退出状态的 pending；监听 join 纳入 ShutdownTrace。
- 更新/SuperTop 先停接纳并冻结 pending，参数在现有 Ready/精确父退出链中保留；失败意图作废。重启监督器继续只生成原 -Restart/-CrashTry，不携带白板意图。
- `StartReplacement` 和兼容旧目标入口只允许空参数或精确 `-Whiteboard`；不能将额外参数变成任意命令行通道。

## 实现期发现与设计修正

1. 原设计使用字节流。固定请求需要严格拒绝过长消息，最终改为消息管道；读取缓冲多一个字节，短包/超长/ERROR_MORE_DATA 都不 Accepted。
2. 原设计的多进程同名管道在本机暴露系统身份差异：首创建者退出后，第二监听端点的 GetNamedPipeServerProcessId 仍回报首创建者 PID。小型 ARM64 真实两进程探针在正常 Windows 环境稳定复现：第二实例 Ready=1；首实例退出0；此后客户端在 peer 验证阶段拒绝，原始回报 PID 与首实例相同。修复使用 PID 后缀端点和只读 PID 枚举，保留强身份校验。
3. 目录权限探针原先在准入前写 IdtRootCheck 临时文件；已后移，保证白板转发路径不写安装目录，并允许现有只读安装实例被激活。
4. CrashHandler 自动/确认重启不经过 SetOffSignal；新增主程序侧 RetireLaunchCommands 入口，不在 module 中重新实例化全局 Server。退出消费也检查同一状态。
5. 替换失败后重启监听会改变 GetLastError；已由主会话保存并恢复原替换错误，保留既有 ERROR_CANCELLED 分支。
6. 原单实例纯关闭会提前 ReleaseForClose，若监听也已停止但进程仍在清理，白板请求可能错误获得新准入。修复仅对白板且禁止多开探测 admitted CommandLifetime，存在即有限转发重试/失败，不修改原 presence 释放策略。
7. 不能用“同目录存在其他活进程”代替上述 admitted marker：两个同时白板冷启动可能互相等待并双双失败，也会误认同目录其他 EXE。最终移除活进程/文件名推断，使用准入后才创建的独立寿命 event；真实并发 barrier 覆盖只有一个 Accepted、另一个 Forwarded。
8. 如果寿命标记成功但可选管道失败，仍需允许冷启动本地意图消费。已将 accepting 状态移到 mandatory lifetime 成功之后、pipe 创建之前；用预占自己端点强制 pipe 失败验证，不修改权限或产品测试旋钮。

## 验证证据

- 使用当前 Visual Studio ARM64 原生 cl、现有 Windows SDK 编译独立头文件探针；`/std:c++20 /EHsc /utf-8 /W4 /DUNICODE /D_UNICODE`，无产品项目配置修改。临时版本资源只为真实 StartReplacement 协议验证提供 StartupHandoffProtocol=1。
- Codex 受限 runner 中服务端创建 Medium 标签返回 ERROR_PRIVILEGE_NOT_HELD (1314)。同一未改动探针在正常 Windows 子进程返回 serverStart=1；这属于 runner 权限差异，未降低产品完整性标签。
- 初始同名多监听设计红测证据：`Build/whiteboard-shortcut-validation/pipe-probe/normal4-stderr.log`。Stage2 为身份校验，First PID=45756 在首实例退出后仍被回报；其余协议用例通过。
- 首轮 PID 修复后 `Build/whiteboard-shortcut-validation/pipe-probe/normal5-stderr.log`：真实进程等待退出码0，专项 PASS。覆盖默认拒重复、带参数转发、允许多开仍转发、两监听者并存、首监听者退出仍可转发、全部退出后新端点、启动中重复请求恰好合并一次、退出拒绝、坏魔数/版本/命令/保留字段/短包/长包、错误安装目录客户端和伪服务端、无端点、无响应超时、有界重试、待更新哨兵字节不变、真实 Ready/父退出替换保留一次白板意图、额外参数白名单拒绝。
- 最终 `Build/whiteboard-shortcut-validation/pipe-probe/normal6-stderr.log`：真实等待退出码0，约9秒，完整专项 PASS。新增：预占自己的 endpoint 强制 pipe 创建失败，mandatory lifetime 成立且本地冷启动意图仍消费一次、退出后拒绝、外部转发失败closed；两个白板冷启动在同一真实 barrier 进入生产 Admit，结果0/1（一个 Accepted、一个 Forwarded）；监听停止并真实 ReleaseForClose 后仍保持老进程存活，白板禁止多开返回 ForwardFailed，普通重开与允许多开白板仍正常 Accepted。
- 参数比较、不同 SID 比较/同 SID 接受、失败意图丢弃、退出后不消费/不接纳均使用生产 helper。
- 产品无窗口状态断言位于 `--state-mode-production-test`，覆盖已显示幂等、最小化恢复和沿既有进入入口。主会话负责完整 Solution 与最终产品产物复跑，不能以独立探针替代最终集成。

## 覆盖限制

- 没有 GUI/Computer Use、UAC、真实 UIAccess 跨完整性连接、真实更新文件替换或 PPT/桌面视觉验收。
- 错误用户 SID 为实际鉴权 helper 的确定性比较测试，没有启动另一 Windows 用户进程；DACL/SACL 为产品真实创建路径，跨用户令牌/UAC仍需人工实机验证。
- 不宣称 Win7/跨架构动态运行已通过；协议使用固定宽度字段和 Win7 可用 API，仍需目标系统实测。
