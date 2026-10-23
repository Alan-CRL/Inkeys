# TOPMOST follow-up review

2026-10-23，trellis-check `/root/p1_review`。用户明确要求诊断测试窗口置顶，授权本任务整体 commit、不 push；checker按main安排只改源码/测试并验证，commit由main处理。本轮未启动可见GUI/Computer Use，未覆盖或改写此前非置顶实屏记录。

## Findings (fixed)

- `Inkeys/Inkeys/Window/Window.cpp`：只对ScrollFollowDebug角色强制WS_EX_APPWINDOW | WS_EX_TOPMOST，仍清除Layered/NoActivate/ToolWindow并保持owner=null；新增简短中文注释说明独立置顶不加入画布owner链。Setting、overlay排序/线程队列、Show/Close和Mag语义未改。
- `Inkeys/Inkeys/UI/ScrollFollowDebug/ScrollFollowDebug.cpp`：MakeSpec同步声明WS_EX_TOPMOST，与Service创建合同一致。
- `InkeysHeadlessTests/window_tests.cpp`：原屏外合同改为要求TOPMOST；原owner线程/空owner/样式、可选ready、Hide/Show/Close/recreate/退出清理断言保持。补充overlay降级/恢复都保留debug独立TOPMOST，Hide后Show仍保持TOPMOST/owner=null。
- `Inkeys/IdtMagnification.cpp`、`Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`：仅新增debug专用fixture的非置顶断言翻转为要求TOPMOST，错误名/事实日志与中文注释同步。专用Source显式HWND_NOTOPMOST，不再插到置顶debug之后而可能进入置顶带；全部GT/像素/捕获/Tracker/生命周期门保持，未改生产Open或Mag过滤/读回。SetWindowPos关于topmost band的主源依据：[Microsoft SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)。

## Findings (not fixed)

本轮范围未发现剩余已确认源码问题。用户选择的人工控件项目仍待人工；本次没有可见GUI、真实Mag像素或性能重跑。此前非置顶窗口的实屏通过/失败保留为历史证据，不能追溯称TOPMOST实屏已验收。

## Verification

- Lint：pass。全任务git diff --ignore-submodules=all --check exit0；22个feature source/project文件可解码UTF-8、原BOM状态、纯CRLF，新文件whitespace与工程XML/唯一登记通过。main维护的既有LF Markdown未规范化。
- TypeCheck：pass。vswhere动态定位ARM64原生MSBuild；同一PowerShell invocation先Remove-Item Env:PATH -ErrorAction SilentlyContinue、MSBUILDDISABLENODEREUSE=1，再执行InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m /nr:false /v:minimal /fl /flp:logfile=Build/ScrollFollow/topmost-build.log;verbosity=normal。沿已审查host方式执行，预留完整构建等待，真实exit0、16.392s，3条既有hashlib++ C4267、0errors。原vcpkg pwsh缺失后既有WindowsPowerShell fallback成功，未改工具链/配置绕过。
- Tests：pass。只运行以下受影响短测试，均Start-Process -PassThru -WindowStyle Hidden、stdout/stderr文件重定向、WaitForExit/Refresh读取真实ExitCode。

| argv（InkeysHeadlessTests.exe） | Exit | 秒数 | 结果 |
| --- | --- | --- | --- |
| --scroll-follow-debug-only | 0 | 1.881 | CV failures0、snapshot71008bytes、model failures0、shared export/odd mapping PASS |
| --exit-visibility-gate-test | 0 | 0.263 | 屏外独立TOPMOST/owner-null/overlay策略/显示关闭合同与exit visibility gate PASS |

日志：Build/ScrollFollow/topmost-short-tests.json、topmost-diagnostic.stdout.log/.stderr.log、topmost-window-contract.stdout.log/.stderr.log；stderr未发现FAILED/FAIL或非零error事实。共享离线导出目录为debug-export-test-41504-42336531，不等于native picker控件测试。未运行其它矩阵、可见GUI或提交操作。源码已释放给main。
