# Draw3 性能诊断启动范围第一轮检查

- Reviewer: `diagnostics_check`（已派发的 `trellis-check`，未派生子代理）
- Date: 2026-10-15
- Scope: `IdtMain.cpp`、`Launch/IdtPerformanceLaunch.h/.cpp`、`Other.Config.cppm`、`Setting.cpp`、三语 JSONC/生成头/快照、`Inkeys.vcxproj/.filters`。
- Concurrency: telemetry 与 package 仍在实现；Draw3 与 `Scripts/Draw3Performance` 仅作启动合同所需的只读检查，未修改、未提前给未完成代码下完整验收结论。
- Context: 已读取保存的 Trellis hook、check.jsonl 对应规范与研究、PRD/design/implement、startup-implementation.md；用户已授权至最终，不重复申请授权。

## Findings (fixed)

无。第一轮未发现本范围需要机械修复的局部源码问题；未修改产品源码。

## Findings (not fixed)

- 全工作树检查过程中发现并发实现的 `Draw3.RendererSnapshot.cpp:253` 有一处新增行尾空白。该文件不属于第一轮独占范围，已向主会话报告，交 telemetry 完工清除。本范围的 `git diff --check` 独立通过。
- 第一轮没有发现需要主会话裁决的启动接口/配置归属/生命周期问题。完整 Draw3 采样真值、输出 schema 和用户端脚本仍须在实现完成后续审查；这是本轮明确边界，并非通过声明。

## 核查证据

1. **关闭状态**：`PerformanceLaunch` 默认只有空字符串、句柄/标志与常量指针；`ParseArguments` 无性能参数时不读取进程时钟/令牌、不创建目录/文件/线程。`BeginFinalProcess` 和配置目录入口仅在性能启用分支调用。Scope 析构普通路径不调用 `StopProduct` 或 `Complete`。启动 Mini 读取新增 bool，但不创建采集会话。
2. **精确 argv 与路径**：正式名称大小写必须精确；探针要求恰好一个参数；重复/拼接/其他混合参数拒绝，正式采集仅允许既有四个内部标志。会话目录要求绝对、已存在、非 drive/UNC share 根，无尾分隔符/非法字符/点空格组件尾；owner/launch/completion/host-* / launch-failure 已存在则拒绝复用。错误在配置、单实例及 HWND 前返回。
3. **真实 SuperTop 链**：保留原 `Split(..., '*')` → `LaunchState::commandLine` → `LaunchSurperTop` → `-CmdLine=?...?` 透传，最终进程保留原性能参数。initial/helper 不建立 launch owner；只有越过 SuperTop 后执行 `BeginFinalProcess`。纯 CPU launcher probe 复用产品 Split 测中文/空格路径，但尚未运行，不等同实际 UAC/UIAccess 证据。
4. **最终身份与原子握手**：owner 为 `CREATE_NEW` 的 `.draw3-performance-owner.lock`；`launch.json` 保存 final PID、完整 FILETIME 十进制字符串、UTC 毫秒时间、EXE/版本/架构/source/UIAccess。JSON 先写同目录 `CREATE_NEW` 临时文件并 Flush，再不替换目标的 Move 发布。锁竞争或握手失败不冒称成功。
5. **启动优先级与配置失败**：CLI 目录先固定，不写回持久化 Performance bool；仅配置途径创建 `log/Draw3Performance/<UTC-PID-GUID>`。配置输出创建/握手失败写可得 failure、保留 cold stderr/后续 logger warn，然后 `Disable` 继续普通业务；不改用户偏好。仅开 Performance 不打开 Console/Draw3/Cursor/TouchArea/StartupEnvironmentDiagnosticsEnabled。
6. **结束真值**：所有 final 之后的显式早返回保留原 return code；fatal 通道记录 failure reason/code，runtime Host 意外停止记录独立 reason。`Complete` 分别计算业务 failed、导出 succeeded 和 capacity incomplete：DComp 失败后 ULW 成功可完整结束；帧/输入/receipt 掉样或 proof overflow 为 incomplete；业务失败仍可有 `export_succeeded=true`。不会把 exit code 0 或导出成功直接解释为业务成功。
7. **关闭与导出顺序**：既有正式退出/已知 fatal 的监督建立点和返回码保留。主路径先 `StopProduct()` 再 Window/共享 pipeline 释放；诊断 scope 再调用幂等 StopProduct，之后读取 export status 才写 completion。只读核对 Host 的 Stop 为关闭命令生产 → RTS Shutdown → 保存屏障/worker drain → 绘制 owner join → RuntimeMetrics/Performance 封口。未处理 C++ 异常不写虚假的完成 JSON，强退/崩溃缺 completion 留给 collector 判 incomplete。
8. **重启隔离**：旧监督器 production restart 参数只构造 `-Restart`/`-CrashTry`，不消费 `LaunchState::commandLine`；旧 CLI 会话目录不会继承到新业务进程。配置开关开启时新进程创建新 GUID 目录，旧结果不会覆盖。
9. **配置/UI/工程**：新增字段位于同组现有 owner 的 `Experimental.Inkeys3.ConsoleOutput.Performance`，默认 false、NoUpload；设置仅写下一次启动偏好并沿 QueueConfigWrite 保存。新中文/繁体/英文说明写明重启与退出后目录。工程两份 XML 对 launcher 和性能 h/cpp 均登记一次，不调整依赖/SDK/toolchain。

## Verification

- Lint/static: 本范围 `git diff --check` 通过；工程/filters 真实 .NET XML 解析通过，新四个文件各一项且磁盘文件存在；三语 `pwsh -NoProfile -File Scripts/i18n.ps1 check`，en-US/zh-TW 各 362/362、100% PASS。
- TypeCheck/build: 本轮未执行。主会话明确统一完整 `InkeysRepo.sln Debug|ARM64` 构建，避免与并发实现冲突；不将静态/工程 XML 解析写成 C++ 编译通过。
- Tests: 本轮未执行软件 CLI。主会话待运行 `--draw3-performance-launch-test`、`--config-boundary-test`、`--draw3-performance-test`、Headless `--no-window`；本轮已只读审查 launcher probe 的九个状态场景及配置默认/true/false Mini 往返接线。
- Format: 十个既有文件 UTF-8 严格解码通过；BOM 与 EOF newline 同 HEAD：IdtMain 保留 BOM、无 EOF newline，其余无 BOM、有 EOF newline。工作树全为 CRLF、无裸 LF；两个新 launcher 文件无 BOM+CRLF。注意 `core.autocrlf=true`，HEAD blob 为 LF，应区分 Git 存储与工作树原格式，不能由此误判漂移。
- Diff: 启动/i18n/工程 diff 仅本任务必要增量，未进行无关格式化或重构。
- Runtime boundary: 未运行可见/隐藏 GUI、Computer Use、实际 SuperTop/UAC、Win7、真实 RTS/低端或兆芯设备；未 commit/push、未修改环境依赖配置。

## 后续

主会话统一构建/CLI 与规范同步；实现完成后沿同一 reviewer 继续全范围 review，重点核采样关闭、输入精确 publication/proof、容量分母、GPU 非阻塞、export 聚合及脚本最终进程/报告完整性。
