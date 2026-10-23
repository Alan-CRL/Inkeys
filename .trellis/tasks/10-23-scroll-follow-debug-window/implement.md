# 实施顺序

用户已批准完整计划，main 负责协调、研究/规范同步和最终验证，按顺序 dispatch trellis-implement/check；按用户最新授权提交本任务，不推送。

## P0
- [x] 创建任务、完整需求 request.md、收敛 PRD、design、implement、research 及两份上下文清单。
- [x] task validate/start。当前分支 feature/move，初始工作区干净，HEAD=980dc39b。

## P1（只读 CV，先不接窗口）
- [x] 增加可选有界快照和纯数值类型；所有早退及异常路径有合理有效性。
- [x] 短诊断差分用例纳入严格单参 --scroll-follow-debug-only（无窗口）。复用原夹具，不调用长 TestValidSequences。
- [x] 测试主解决方案的 InkeysHeadlessTests 定向 target（此阶段不声称主程序编译通过），运行短入口，记录结果。
- [x] 独立检查生产逻辑/阈值/short-circuit/异常不被诊断改变后进入 P2。

## P2（窗口骨架与排除）
- [x] Window Service 追加 role/设置 owner 路由/生命周期，参数只启动打开，关闭不重开。
- [ ] BuildCurrentFilterList 角色排除；最小窗口实际像素验证。若环境无法真实执行，记录未验证和人工步骤后保留待验收，不能声称通过。
- [x] 扩展现有窗口合同的创建销毁/过滤列表测试。

## P3（图像与界面）
- [x] 有界三槽 snapshot transport，真实 ROI 复制及统计、会话状态、capture/业务失败来源；关闭无大分配/复制。
- [x] 完整 GDI 图像/标记/光流/网格/范围/面板/历史/Freeze/zoom/pan。
- [x] 显式异步 PNG/JSON 导出，资源和取消清理。
- [x] 对应短 buffer/映射/关闭/Paused/导出测试，项目中登记必要新源码，不修改 unrelated 配置。

## P4
- [x] trellis-check 全范围检查及修复，完整 solution Debug ARM64 编译（至少 5 分钟预算），定向短测试，不跑全量长序列或 UInk。
- [x] 完整UI真实Mag、空页正反滚动/ACK及有限三段性能通过；按用户选择交付控件人工步骤与未验证表。最小P2原型单独像素未验证。
- [x] BOM/EOL、完整diff/新增文件whitespace、工程XML与任务上下文检查通过；规范同步及delivery.md完成。

## 命令与环境

Python 使用 C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe；系统 python.exe 是不可用 WindowsApps alias。

vswhere.exe 位于 C:/Program Files (x86)/Microsoft Visual Studio/Installer，-latest -products * -requires Microsoft.Component.MSBuild -find MSBuild/Current/Bin/arm64/MSBuild.exe。不要硬编码 VS 版本。

每次 MSBuild 同一 PowerShell invocation 先 Remove-Item Env:PATH -ErrorAction SilentlyContinue，再 $env:MSBUILDDISABLENODEREUSE='1'，然后构建 InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m /nr:false。异步观察进程 exit code，不能因几十秒无输出重启。先记录首个有意义错误，环境问题不得改源码绕过。编译产物为 Build/ARM64/Debug/Inkeys.exe 与 InkeysHeadlessTests.exe。

最终完整UI的真实Mag排除已通过两图案/28帧及覆盖ROI、关闭重建；早先最小原型未取得像素通过证据，保留未验证。产品三段性能与实际控件仍在独立验收，具体结果见 validation.md。用户明确不commit/push/PR，故不执行Trellis提交归档流程，任务保留待审。

用户最终选择控件交付人工；新严格产品短入口已取消75秒人工等待，保留全部像素/性能/生命周期门，最终真实exit0。最终结果以delivery.md/validation.md顶部为准。

用户最新确认：诊断窗口采用TOPMOST；授权提交全部本任务改动，不push。此前不提交/非置顶记录保留为历史，当前决策以此为准。
