# Setting 顶边修正后的创建尺寸校正

2026-09-08。仅记录本次已获批准的“选项窗口最上沿粗条”修复后续；不涉及悬浮主栏、主题/页面重构或其他 WindowRole。

## 已确认问题

主会话在真实 192 DPI 选项窗口测得：WindowSpec 请求客户区 1920×1400，顶部改用真实 DWM 描边后实际客户区为 1920×1399。创建前没有 HWND，细边框回退估算为 1px；创建后 DWM 返回 2px。不能通过猜测分数 DPI 的取整常量修复这一差值。

## 最小实现

- 仅在 Window::Service 的 IsSetting 创建分支、既有 SWP_FRAMECHANGED 之后，测量真实 outer/client 尺寸，以两者差值补回 WindowSpec 请求的客户区。
- 目标外框按原请求中心定位，并重新夹紧到当前显示器 work area。仅执行一次校正；已有尺寸正确或被较小工作区夹紧后无需变化时，不追加 SetWindowPos。
- ResolveWindowFrameCreationCorrection 是产品与无窗口回归共用的纯几何 helper；没有修改 USER32 最大化字段、其他角色创建、渲染/设备/配置或显示隐藏合同。
- 有窗口测试只对 Setting 专用创建线程通过已有 beforeCreate/destroyed 钩子切换并恢复 PER_MONITOR_AWARE，与产品模式一致；不改变其他窗口或全测试进程 DPI awareness。

## 定向检查

新增回归覆盖：创建前后 1px 差值、客户区已匹配时 no-op、负原点与空间不足时保持原 clamp，以及贴近工作区底部时向上修正而不溢出。原 160×90 实际窗口客户区断言保留；尺寸由 Setting.created 在创建线程捕获，主测试线程只读取结果，避免跨 DPI awareness 度量。

本子任务已完成静态差异和 UTF-8 无 BOM / CRLF 检查，git diff --check 通过。完整 ARM64 Solution、集成无窗口/窗口测试与真实 192 DPI 产品测量由主会话统一执行；本记录不将这些待回报结果冒充已通过。未运行 GUI、Computer Use、git add 或 commit。

## 192 DPI 窗口 fixture 下限修正

独立完整构建和无窗口测试通过后，实际窗口测试暴露 fixture 下限冲突。失败诊断在创建线程捕获：client=234×90，outer=258×104，DPI=192，USER32 预填 min-track=258×71。原测试请求160px客户区加24px左右框仅184px，低于该原生caption下限；234=258−24，证实失败来自测试默认最小跟踪尺寸，而不是顶部1px校正。

测试专用 SettingFrameTestProc 现只覆盖 ptMinTrackSize 为1×1，以隔离客户区创建合同，保持原160×90精确断言。最大化字段不变，产品720×520 DIP最小尺寸与所有其他窗口角色不变。失败诊断保留。主会话最终重跑：Build/setting-top-border-final-build.log 为0 errors（3条既有hashlib++警告），Build/setting-top-border-final-windows.log 输出 PASS animation correctness；实际产品192 DPI尺寸与视觉仍由主会话窗口测量验收。
