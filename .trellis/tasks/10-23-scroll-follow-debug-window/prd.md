# Scroll Follow 实时可视化诊断窗口

## 目标

为 Inkeys 主进程提供独立 Win32 实时诊断窗口，准确显示正式 Mag 输入、OpenCV 匹配与拒绝条件及实际运动输出。完成标准是可观察性，不是追踪性能提升。

## 已批准需求与边界

- 基于 feature/move 980dc39b；仅 --scroll-follow-debug-window 启用，console/verbose 保持独立。
- 窗口只在启动时打开；关闭后本进程不自动重开，不添加快捷键，不影响 Scroll Follow 或退出 Inkeys。
- Window Service 统一所有权及生命周期，真实 Mag 排除当前调试 HWND，不使用捕获亲和属性或另一套截图。
- 真实 WorkArea 输入图像、有效/Forward/FB/内外点/补点、光流、8x6 网格、主运动与已评估竞争运动及证据范围。
- 三个区域：图像、可滚动详细统计、最近 30 条数值事件；支持等比缩放/平移、标记开关、箭头倍率。
- 来源明确的 Session/Frame/Reference、Capture/Tracker 分离状态、真实拒绝值与门限、全部现有 MotionStatistics、分段耗时及显示年龄。
- 不伪造未计算项；早退显示阶段完整性；Capture 失败/业务暂停与 Tracker 拒绝分开。
- Freeze 固定同帧图像/标记/统计但不中断追踪；Paused 保留最后失败真实输入及最后可信运动。
- 显式选择目录，导出原始输入 PNG、标记预览 PNG、JSON；默认不保存，不实现录制。
- 不修改算法参数、判断顺序、捕获调度/方式、Draw3/Viewport/UInk/穿透/主栏按钮语义，不提升 Windows 最低版本。
- 诊断关闭无新窗口/定时任务/图像复制/大型特征数组；开启时有界 latest-only 非阻塞发布，不在 Mag 回调绘图、格式化或写盘。
- 保持文件编码/BOM/EOL；关键逻辑中文注释；用户已授权本次 commit，不 push/PR。

## 验收标准

- [x] P1：诊断开启/关闭全部非计时 MotionResult 精确一致；初始化、正常/反向、LK/FB、Coverage、AmbiguousMotion、Paused、接力被短用例覆盖。
- [x] P2：窗口属于 Window Service 设置 owner 线程；可调整的 TOPMOST 窗口；创建/关闭/销毁/重建和退出隐藏正确；列表包含有效当前 HWND，不保留失效句柄。
- [x] P3：图像与诊断身份一致，借用寿命/stride/裁剪/奇数尺寸/缩放正确；消费落后不影响生产；最终失败帧保留。
- [ ] P3：真实检查来源、未执行项、捕获失败、Frozen/Stale/Paused 清楚可见；最近事件、导出与图例可用。（实现及离线合同通过，真实控件按用户选择人工待验。）
- [x] P4：定向无窗口/窗口合同测试和完整 Debug ARM64 solution 成功；不运行长算法或 UInk 全量回归。
- [ ] P4：最小/完整窗口 Mag 实际像素排除、短 GUI 冒烟和关闭/开启/关闭后有限性能对照；环境不支持时明确未验证并给人工步骤。

## 授权与来源

用户已在规划阶段同意新建 Trellis 任务，随后明确请求实现完整方案；本次请求包含短真实 GUI 验证授权。原始完整需求见 request.md。计划已由用户逐字批准，无未决产品问题。

用户最新确认：诊断窗口采用TOPMOST；授权提交全部本任务改动，不push。此前不提交/非置顶记录保留为历史，当前决策以此为准。
