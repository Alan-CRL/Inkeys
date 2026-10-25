# Setting 顶部边框独立审查

2026-09-08；基线 9ef709df。审查范围以 top-border-followup.md 为准：只修“智绘教 Inkeys 选项”窗口 caption 上方粗条，禁止 Computer Use；本审查未操作窗口、未运行完整构建、未暂存或提交。

## Findings (fixed)

- File: Inkeys/Inkeys/Window/Window.cpp、Inkeys/Inkeys/UI/Setting/Setting.Layout.h
- Issue: 创建前无 HWND 时 SM_CYBORDER 回退为 1px，192 DPI 下真实 DWM 描边为 2px，直接使用创建前估计会使请求 1920×1400 的客户区成为 1920×1399。主会话实测了产品差值；审查者独立在 Per Monitor aware 脚本中查询 96/120/144/192/240 DPI，确认 SM_CYBORDER 均为 1，不能冒充 DWM 随 DPI 的实际描边。
- Fix: 实施代理在既有 IsSetting 创建分支的 FRAMECHANGED 后测量真实 outer/client，用实际差值校正一次；保留请求中心、当前工作区夹紧及尺寸正确时的 no-op。无需猜测分数 DPI 的 DWM 取整；没有修改其他 WindowRole。

- File: InkeysHeadlessTests/window_tests.cpp
- Issue: 旧窗口 fixture 没有产品的 DPI awareness；对齐为 Per Monitor aware 后，物理 160×90 请求又低于 USER32 的最小 outer 宽度。实际诊断为 client 234×90、outer 258×104、DPI 192、native minTrack 258×71，234 正好为 258 减左右 frame 24；不能放宽断言或把这个失败误判成生产校正仍错误。
- Fix: 只在 Setting 专用创建线程设置并恢复 awareness，在其 created 回调采集实际客户区；Service.Start 的 promise/future 完成后主测试线程读取。专用测试 WndProc 显式将 minTrack 设为 1×1 隔离默认 caption 的最小尺寸，继续精确断言 160×90，保留 USER32 max 字段及失败几何诊断。生产最小客户区 720×520 DIP 不变。

上述修复由实施代理落盘，审查者独立复核，没有改写源代码。

## Findings (not fixed)

当前未发现其他需要修复的代码问题。真实跨显示器 DPI、旧系统 composition 切换不由本次静态审查替代；实际窗口证据由主会话的最终验收记录负责。

## 逐路径复核

- 恢复态且 DWM 可用时只减小 top inset；左右、右侧 scrollbar 外的 sizing frame、底部、最大化和 composition 关闭路径继续使用系统 frame。DWM 属性不可用回退系统细边框，创建后校正处理估计与实际差值。
- NCCALCSIZE、创建前度量、最小 track 共用 Setting 的 frame 查询。WM_GETMINMAXINFO 仍仅修改 ptMinTrackSize，不改 USER32 预填最大化字段。
- 顶部补足的 resize 高度只适用于空白 HTCAPTION 及客户区外顶角；close/max/min、主题、版本、系统菜单先保护。正文和完整右侧 scrollbar client 区不增加 resize 命中，最大化不返回 resize。
- 创建校正仅在 IsSetting 分支、首次 Show/Hide 前执行；查询失败不新增资源或改变清理顺序。Bar/PPT 的创建、owner、位置、大小、显隐、输入和渲染路径没有差异；未修改主栏/主按钮。
- 新纯几何回归覆盖 1px 差、no-op、负原点、工作区不足和贴底修正；原顶部/顶角、按钮保护、右 scrollbar、多尺度及最大化字段回归保留。
- rendering-and-ui.md 已由主会话同步顶部例外与创建实测校正合同，避免旧的“四边都相同”规则造成反向回归。

## Verification

- Lint: pass；git diff --check 通过。仓库本范围没有独立 C++ lint 命令。
- Encoding: pass；6 个源码/测试文件仍为 UTF-8 无 BOM、CRLF，与工作树编码基线一致。
- TypeCheck / full Solution: pass；主会话使用 ARM64 host 对独立 OutDir 完整构建 InkeysRepo.sln Debug|ARM64，exit 0。审查者复读 Build/setting-top-border-final-build.log 确认 0 errors；最终仍有 3 条既有 hashlib++ 转换告警。最新产物位于 Build/SettingTopBorderFinal。早先默认输出 LNK1168 已通过独立输出目录规避，没有终止用户现有程序。
- Tests: pass；最终 --no-window 与包含真实 WindowTests 的完整集成运行均由主会话执行，exit 0；审查者复读 Build/setting-top-border-final-no-window.log 和 Build/setting-top-border-final-windows.log，均为 PASS animation correctness。该固定输出代表集成总结果，不仅是动画测试。
- GUI: 审查者未执行。主会话对用户已打开的 Options HWND 读取到恢复态可见顶部从原 12px 减为 2px，四边 inset 为 12/2/12/12；顶部/顶角、scrollbar、close/max/theme 命中符合预期。该默认 Debug 进程是创建 1px 校正前的版本，client 1399 不能当作最终产物测量。后续 WTS 锁屏阻止截图及实际拖动验证；没有使用 Computer Use，也不声称最新 EXE 已热更新到该进程。
