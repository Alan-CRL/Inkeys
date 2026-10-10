# P0 验证记录

> 本阶段只提供基线、探针与IPC选型。没有生产功能变更，P1-P5未执行。

## 设备与工具

- 源码HEAD：`a50ad143cbe14c7a0e4a4d97bc14733f33eda4e1`，分支`feature/pptcom`。
- 本机Windows 11 ARM64，OS API版本`10.0.26200`；已安装.NET Framework Full `4.8.09221`，Release=`533509`。
- vswhere发现`C:/Program Files/Microsoft Visual Studio/18/Community`，本次MSBuild为`MSBuild/Current/Bin/arm64/MSBuild.exe`，18.10.1。
- PptCOM/PptCOM.Tests目标v4.0/AnyCPU；本机实际CLR较新，不能把目标框架或CLR 4.x版本字符串当作Win7/原版.NET4运行证据。
- Python使用Codex现有bundled runtime；`python`的WindowsApps别名不可直接作为本轮工具入口。未修改持久PATH、SDK或全局配置；仅按仓库要求在构建invocation内规范化PATH。

## 已执行的既有测试

在同一PowerShell invocation执行PATH规范化，使用当前VS的ARM64原生MSBuild。构建进程时限300000ms，测试时限60000ms，后台无窗口启动并重定向输出；检查进程ExitCode。

```powershell
$p0VsInstall = & 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
$p0Msbuild = Join-Path $p0VsInstall 'MSBuild/Current/Bin/arm64/MSBuild.exe'
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
# 本轮用Start-Process -WindowStyle Hidden/WaitForExit(300000)包装下述命令并记录退出码。
& $p0Msbuild PptCOM.Tests/PptCOM.Tests.csproj /t:Rebuild /p:Configuration=Release /p:Platform=AnyCPU /m:1 /nr:false /v:minimal
# 本轮同样使用隐藏进程与60000ms超时运行；测试不创建Office或窗口。
& ./PptCOM.Tests/bin/Release/PptCOM.Tests.exe
```

| 验证 | 结果与范围 |
| --- | --- |
| PptCOM.Tests Release AnyCPU Rebuild | PASS，exit=0；项目引用正确重建PptCOM DLL/TLB，未复制到Inkeys随附资源 |
| PptCOM.Tests.exe | PASS，exit=0，输出`PASS PptCOM descriptor ownership and session/owner contracts` |
| 命名管道probe | PASS，最终修复源码使用真实net40引用重新编译exit=0；独立复核1轮11/11，脚本/用例/peer退出码均0。修复前3轮10/10作为历史证据分开保留，初次受限runner拒绝也保留；详见[probe-results.md](research/probe-results.md) |
| 生产/随附产物散列 | PASS，基线26个文件逐个SHA-256匹配，无生产源码、接口、工程或随附DLL/TLB/manifest变化；基线为[baseline-hashes.json](research/baseline-hashes.json) |
| 独立Trellis check复核 | PASS，覆盖P0/父任务需求与实际探针、COM/native事实、选型、既有测试日志；修复局部异常清理并重验11/11，详见[check-report.md](check-report.md) |

日志在忽略目录`TestResults/pptcom-p0/managed-tests/{build.stdout.log,build.stderr.log,tests.stdout.log,tests.stderr.log}`。没有以已存在的旧exe代替本次托管源码构建。

既有测试覆盖生产HWND accessor借用/late-bound回退、PowerPoint/WPS fake descriptor、temporary ledger子先父后恰一次、borrowed root零释放、fallback/busy/HRESULT、结束页/冷启动/失败、状态发布与会话退出、窗口guard、native observation gap、owner mailbox、annotation takeover和ABI。它们不启动真实Office，不能证明用户报告的损坏COM识别或Office退出内存释放通过。

## 探针复现入口与限制

参见[探针运行脚本](research/probes/run-probe.ps1)和[结果](research/probe-results.md)。最终复核执行`-Repeat 1 -RunLabel check-final`，输出为`TestResults/pptcom-p0/check-final/`；当前探针源码SHA-256为`4426576940c348f46238203e9abd6297e95b5b15b34a69aa474edd51576b24be`，历史结果对应旧源码，未覆盖。编译时明确引用真正v4.0的mscorlib/System/System.Core、禁用自动标准库引用；只调用该框架提供的API。程序是隔离验证工具，未登记到产品工程，不是已经实现的helper/生产IPC协议。

P0探针测试的是字节通信与停止机制。Envelope/epoch/候选/ACK/路由与目标修订属于P1及P3真实adapter验收；探针中的重连全量样本不能代替生产来源恢复。探针中的单次耗时和时限只是断言，不能外推P95或防抖毫秒值。

## 未执行及后续门禁

| 组合/能力 | 状态 | 对应阶段与理由 |
| --- | --- | --- |
| Win7 SP1 x86/x64 + 原版.NET4运行 | NOT VERIFIED | P3前真实设备/VM验证；本机Windows11 ARM64与v4.0引用编译均不能替代 |
| elevated host→普通helper / 普通host→elevated helper | NOT VERIFIED | P3；token、runas/UAC、ACL、实际对象完整性及peer身份需真实权限组合，本轮无GUI授权 |
| UAC取消/另一管理员凭据/不同logon与session | NOT VERIFIED | P3；不能由同SID同权限probe外推 |
| 生产native server/managed client、写端拥塞取消、父进程失效/heartbeat/epoch | NOT VERIFIED | P3；当前probe没有实现生产host/route |
| 真PowerPoint/WPS正常与损坏COM、无事件轮询、多show、反复启停/退出 | NOT VERIFIED | P4/P5设备验收；本轮不启动Office/GUI，也不把fake ledger称为真实RCW释放测试 |
| 页事件→缓存→source→仲裁→target→Present→UI ack和输入ready实测延迟/资源趋势 | NOT VERIFIED | P3-P5；P0只有源码采样周期和后续测量点，不固定防抖参数 |
| ARM64原生helper与x86/x64helper发布包运行 | NOT VERIFIED | P3/P5；AnyCPU probe在当前CLR上的运行不是各架构发布证明 |
| 完整InkeysRepo.sln Debug ARM64和native headless | 未运行 | 本轮没有修改生产接口、native源码或工程；最小验证范围是当前managed合同和隔离pipe probe，不需重新编译主程序。既有headless.exe来源时序未核定，未把它算作本次证据 |

## P0 交付与后续入口

- 保留dynamic/ROT/事件与轮询/释放/原线程路径；详见[com-baseline.md](research/com-baseline.md)。
- 首发helper通道选择本机双向命名管道；依据与运行准入详见[transport-decision.md](research/transport-decision.md)。
- 活动放映优先仍由统一抽象层管理；P0识别当前单赢家与Focus反馈路径，防抖只作用于绑定切换，正常PageChanged保持快路径。参数由后续可控时间合同测试和实机时序确定。
- P0完成后下一阶段为P1通用合同与仲裁纯逻辑；本轮不激活或实现P1。

## Code-spec 审阅

已按trellis-update-spec审阅ppt-interop与native-desktop规范：dynamic/typed HWND例外、temporary与root释放、descriptor owner、事件保留页码读取、native业务直接命令、DLL/TLB交接和Draw3门禁已有合同。本轮没有改变生产API、数据布局或实际IPC接入，未新增已生效的产品合同。

P0首发管道选择、普通执行与受限runner的对照、当前owner/Focus路径及未验证平台结果保留在本子任务研究，并更新父任务设计/路线。暂不把probe的帧格式、FullControl实验ACL、停止时限或未验Win7组合上升为全局规范。P1/P3实现并冻结真实消息/API和运行结果时，再更新可执行合同及测试矩阵。
