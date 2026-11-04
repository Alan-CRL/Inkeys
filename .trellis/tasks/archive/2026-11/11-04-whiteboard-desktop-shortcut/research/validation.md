# 实现与验证记录

日期：2026-11-04。任务由用户授权开始实现，后接力采用已确认的外部 agent 图标。实现及自动验证阶段没有 GUI/Computer Use 或 push；用户本轮批准提交与归档，见末尾收尾记录。

## 实现范围

- 应用程序与白板两类同布局设置、独立四字段配置、名称预设/自定义与 Windows ordinal 双向冲突检查；未保存草稿仍保护最后有效名称。
- FIFO 冻结种类、名称、目标与开关；真实 ShellLink 按完整目标与参数分类，两类保障互不删除，未知/超长参数不误分类。
- 精确 -Whiteboard 参数、受限固定包管道、启动完成后一次进入、白板幂等/最小化恢复，以及更新/SuperTop 合法交接中的意图保留与失败丢弃。
- 图标采用最终蓝色点阵与白色书写线 SVG/ICO，IDI_ICON2=332；原主图标191与原icon.ico保持。
- 二十语言通过既有 sync 生成头与快照，三份规范同步。独立检查见 final-review.md。

## 环境与修正记录

1. Python 系统别名不可执行，使用已有 KiCad Python 执行 Trellis/检查；图标来自交接产物，不新增产品生成依赖。
2. 沙箱完整 Solution 首轮在多个未改动编译单元报 C1902 PDB manager mismatch。仓库既往验证有相同沙箱限制；相同 ARM64 原生 MSBuild、Solution、配置在沙箱外越过该错误。未改 SDK、依赖、DebugInformationFormat 或项目配置。
3. 首次实际源码构建暴露 C2512：显式 Kind 构造带缺省参数不能用于 aggregate 成员的隐式缺省初始化。已改为普通默认构造与显式 Kind 构造分开，完整构建通过。
4. 原同名多监听设计在真实测试失败：首监听者退出后，系统仍返回首创建者 PID。保持身份校验，改为 PID 后缀独立端点和共用总截止的只读候选发现；小型探针通过后再完整构建验证。
5. 关停复核补齐崩溃重启的拒收门、更新失败意图丢弃及原 GetLastError 保留；普通关闭提前释放原presence的保护使用已准入寿命事件，避免通用活进程探测导致两个冷启动互相拒绝。新增真实并发与释放后仍存活回归。
6. 沙箱中 Medium 标签管道创建报1314；同一 ARM64 探针在常规 Windows 成功。管道/配置进程专项在已批准的沙箱外运行，未降低产品权限检查。

## 最终检查结果

最终源码的完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false`：ARM64原生MSBuild实际退出0，0 error，4个既有转换warning（hashlib++与原IdtMain代码）。末次增量完整Solution耗时20.06秒，构建进程允许10分钟。原生MSBuild路径由vswhere动态发现，同一PowerShell invocation先保存Path、移除PATH后只恢复一个Path，并设置MSBUILDDISABLENODEREUSE=1。

| 检查 | 最终结果 |
| --- | --- |
| `InkeysHeadlessTests.exe --desktop-shortcut-only --no-window` | exit0；Config0failures，实际设置控件/布局/预设与双向冲突通过 |
| `Inkeys.exe --desktop-shortcut-test` | exit0；冻结FIFO与真实临时目录ShellLink两类保障、参数/图标、长参数及大小写保护 |
| `--desktop-shortcut-test extra` | 预期exit2，初始化产品GUI前拒绝 |
| `--single-instance-tests` | exit0；原交接协议及追加白板管道专项均PASS |
| `--shutdown-supervisor-tests --instance-lease-only` | exit0；旧进程77，restart_count=1 |
| `--config-boundary-test` | exit0 |
| `--state-mode-production-test` | exit0，白板活动幂等/最小化恢复/原工作区入口及既有状态回归 |
| `--supertop-token-failure-test` | exit0，无UAC/GUI |
| `--i18n-locales-test` | exit0 |
| `Scripts/i18n.ps1 sync/check` | 生成链sync通过；最终check实际exit0，十九个非基准语言均720/720 |
| `research/icon-resource-check.py` 对最终EXE | exit0；9帧PNG逐字节匹配ICO，16~256全部负资源ID提取成功，主图标组191保留 |
| `git diff --check --ignore-submodules` / 原工作区BOM+换行对照 | exit0；47个已跟踪文本文件无编码/换行漂移，无构建生成物误入diff |
| `task.py validate` | exit0，两份清单均8项有效；保留已有大规范文件注入截断提醒，本轮直接读相关实际规范 |

最终管道专项实际产品EXE覆盖：多监听者与首份退出/全退后新监听者；启动中多请求合并一次；真实Ready/精确父退出替换携带一次意图；无端点/超时/错误目录/伪服务端/坏包/版本/超长；更新哨兵独占下仍转发且字节不变；监听失败仍消费一次本地冷启动意图；两个并发冷启动恰好一个准入、一个转发；纯关闭已释放原presence但仍存活时白板禁止多开入口拒绝，普通重开及允许多开策略保持。

管道关闭专项使用独立的已准入寿命事件，不用任意活进程/文件名判断，避免把两个尚未准入的冷启动互相拒绝。必需寿命标记创建失败在初次启动和安装根重新绑定处提示退出；管道监听本身失败与本地冷启动意图分开处理。

全部GUI subsystem专项通过Start-Process -WindowStyle Hidden启动，WaitForExit后读取真实Process.ExitCode；每项允许5分钟。最终测试总驱动实际exit0，原始结果见test-results.json。末次复跑未改产品源码，之后仅更新任务记录。

## 日志位置

忽略目录 `Build/whiteboard-shortcut-validation/`：build-sandbox.log、build-source-1.log、build-integration-2.log、build-pid-endpoints.log、最终 build.log；test-results-first.json、最终 test-results.json 与各专项 stdout/stderr；icon-resources.json、format-check.json。管道独立探针见 pipe-probe/。

## 覆盖边界

- 无窗口专项使用私有复制 EXE、唯一临时目录与受控 IPC，不触碰真实用户桌面，不执行真实更新文件替换。
- 不同 SID 为生产鉴权 helper 比较测试，没有启动另一 Windows 用户的客户端。真实 UIAccess/跨完整性、UAC/SuperTop、更新/回滚全链、Win7、跨架构及桌面/PPT视觉行为未做实机验收。
- 只运行相关专项，不重复整套无关 Headless；快捷方式入口同时覆盖 Config 与真实设置控件，已有独立综合套件会生成其他任务目录下的无关预览图。
- git 子模块元数据受 runner 读取限制，diff/status 使用 --ignore-submodules；未修改子模块。编码/换行以 git cat-file --filters 的原工作区形态核对，不能以 Git 内部 LF blob 误判 CRLF。

## 后续文案调整（2026-11-04）

按用户截图反馈，将应用程序/白板两处共享的 `SettingsUI.PlugIn.LnkHelper.Expansion.CreateLnk` 标题统一为“启动时创建桌面快捷方式”；同步20个语言文件及生成的简中快照。对本轮修改前的字节基线核对：每个语言文件和快照仅改变这一条标题，编码/换行及生成键身份保持，手动创建按钮文案不变。

`Scripts/i18n.ps1 sync/check`最终均通过（720/720）；完整Debug ARM64 Solution实际exit0、0error，既有warning保留；更新后`--desktop-shortcut-only --no-window`实际exit0，`git diff --check`通过。仅文案/内嵌资源更新，本轮不重复无关管道/监督器专项，不进行GUI测试。日志为startup-label-sync.log、startup-label-check.log、startup-label-headless.*.log、startup-label-build.log；上轮构建日志另保留build-before-startup-label.log。

## 验收与提交归档（2026-11-04）

用户确认改动有效，批准结束任务、归档并提交。功能与规范已按现有SSH签名配置提交：`40ba6efec7767d64b2206079bc157640c062b3c9`。收尾仅更新任务/日志元数据，不再改动实现；沿用此前通过的完整构建、9项专项及标题调整后构建/无窗口复查。真实目标环境验证边界继续保留，未执行push。
