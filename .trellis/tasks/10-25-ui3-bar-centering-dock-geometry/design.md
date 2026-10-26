# UI3 底栏居中与吸附帧几何修复 — 集成技术设计

> 2026-10-26 接力：本计划已获本会话授权并完成代码与自动化，最新证据见父任务 `research/final-integration-validation.md`；原文未来式保留作历史设计。GUI/真实系统呈现验收待授权，任务未归档。

## 目标与边界

父任务统一两个子任务的逐帧几何合同和集成验收，不另建一套布局、动画或窗口呈现架构。具体根因与修复边界分别见：

- `../10-25-ui3-centered-bar-dynamic-centering/research/centering-investigation.md`
- `../10-25-ui3-bottom-dock-transition-clipping/research/dock-transition-clipping-investigation.md`
- `research/shared-frame-contract-and-validation.md`

本轮仅形成规划，不修改产品代码。

## 证据分级

- 用户报告描述实际可见症状，但具体像素幅度和触发频率尚需真实 GUI 逐帧验证。
- 仓库规范给出应有合同，不等于当前实现满足合同。
- 子任务研究中的静态根因必须引用当前生产代码；历史提交只解释演变，不能替代现状。
- 当前 HEAD 的完整 ARM64 Debug Solution 已构建成功（0 警告/错误，目标均为 up-to-date），同一无窗口测试产物回归通过；这些结果仍未覆盖真实 PPT/白板/任务栏视觉过程。

## 共享所有权与数据流

1. **输入/交互侧**持有指针样本、两轴吸附候选和直接窗口位移；必须把同一采样产生的横纵模式、弹性状态、deferred barrier、显示环境与 translation 作为一个稳定 serial 事务发布。
2. **渲染侧**持有动画对象、主按钮/主栏布局、弹性映射、提示几何、capacity/viewport 和 dirty。每帧只能消费一个稳定 tuple，不能在同一帧拼接实时状态与上一成功状态。
3. **派生顺序**必须是：同帧动画推进 → 主按钮/主栏及继承几何 → 居中/吸附映射 → 提示等下游几何 → damage 与完整预测包络 → candidate viewport → 最终 capacity/source → D2D transform/clip → ULW。重排仅限无副作用几何，最终容量必须覆盖完整 candidate viewport。
4. **呈现侧**在同次提交中配对 `pptDst/psize/pptSrc/prcDirty` 和实际 translation。只有 GetDC、ULW、ReleaseDC、EndDraw 及必要窗口交接全部成功，才推进成功快照。
5. **下一帧连续性**只能以上一次完整成功呈现的屏幕像素和几何为旧状态；候选、失败或半途状态不能冒充已呈现基线。

## 核心设计不变量

### A. 动态居中

对居中底栏，水平根位置应从同一帧主按钮、主栏及可见描边的实际联合外框反推，使联合中心等于所在 monitor 水平中心。宽度、主栏 X、描边和竖直位置可在其他动画中变化，但水平居中不能只在终态重新成立。

Enter/Switch 场景 Y 过渡与水平居中应被视为可组合的二维几何，而不是互斥的根位置所有权。实际根反推和 viewport 根范围预测必须共用窄门禁；最终 Leave 独立验证，不强行重写。不得新增 correction、pending rebase 或第二套补偿动画，Free、拖动、捕获/脱离弹簧和普通显示环境切换不能被无意接管。

### B. 吸附首帧连续性与快照一致性

吸附捕获/脱离的首帧必须从上一成功呈现像素反推新映射或弹簧初值，不能先把旧位图直移到吸附目标，再在下一帧恢复。普通捕获已有该保护，不得重写为旧方案；脱离方向需补齐对称 seed，且首个成功候选不提前积分。

释放吸收必须区分 layout delta 与真实 screen delta，同步重基准两轴绝对 base/visual/grip 端点、translation、中心和 indicator/hit 语义，保证 `origin + layout*zoom + translation` 在吸收前后不变。

真实局部抓点代理差异只有被逐帧证据确认为直接原因时才进入独立修复分支；不恢复历史大补丁，也不通过延迟帧或禁止直拖掩盖。

### C. 提示完整包络与最终容量

提示的实际视觉包络应包含文字/矩形、Back 超调、描边、光影、Gaussian 和抗锯齿余量。该包络必须同时进入：

- visible content 与旧/新 damage；
- 完整 predicted viewport 与最终 target capacity；
- D2D clip/Clear 和 ULW source/dirty；
- 最终真实 HWND 外框。

优先复用现有保守包络、批次开始扩窗和 idle 收缩机制。GetDC/ULW 前必须验证 source+size 位于实际 target bitmap，活跃提示包络位于 candidate viewport；越界不得钳 source 后提交。禁止每帧随内容缩窗、无限扩大窗口或只修 dirty 而不修窗口区域。

## 坐标与提交合同

| 数据 | 必须保持的语义 |
| --- | --- |
| monitor/layout 几何 | 与动画、DIP/zoom 使用同一原点和单位 |
| 屏幕像素几何 | 用于上一成功像素连续性、直接 HWND 位移和最终居中验收 |
| capacity/source 几何 | D2D transform、`pptSrc + psize` 必须落在实际 target 内 |
| client dirty | 从同一 candidate frame 的 layout damage 与映射派生 |
| 成功快照 | 只表示完整成功提交的旧帧，不包含候选或失败帧 |

所有修复必须检查负 monitor 原点、既有 DPI/zoom 和整数取整，不引入新的坐标换算分支。

## 集成顺序与重叠控制

两个子任务都会触及 `Bar.RenderLoop.cpp`、`Bar.BottomDock.h` 及相近测试。实施时不得并行改同一文件：

1. 先落地能复现原症状的回归证据和共享诊断采样。
2. 按居中子任务完成窄范围根位置/场景门禁修复并验证。
3. 按吸附子任务分别完成脱离首帧 seed、释放吸收快照重基准、完整预测与最终 capacity/source 包含，并验证提示 dirty/真实窗口。
4. 最后进行父任务集成审查，重跑居中与吸附交叉场景，特别检查 viewport、root ownership、direct translation 和成功快照是否互相覆盖。

若最终研究发现更安全的依赖顺序，应在两个子任务 `implement.md` 中明确记录，而不是依赖任务树隐式排序。

## 兼容与回退

- 保留现有 UI3 唯一生产路线、动画曲线、果冻手感、Free/Centered 正交状态和桌面基线恢复。
- 不改变公开配置、Win32/ULW API 路线、共享 RenderPipeline 线程模型或旧 UI2 编译边界。
- 新诊断默认关闭并有界，复用现有 raw capture/调试框；不得引入逐帧阻塞磁盘日志。
- 每个子任务应形成独立可回退变更单元；集成问题只回退对应子任务，不通过大规模重写清理。

## 验证策略与未验证事项

自动化必须先能复现原缺陷，再验证修复；测试应调用生产 helper/真实协调路径，不能复制公式后自证。完整构建与无窗口测试是最低门槛，真实 GUI 逐帧验收仍是视觉完成的必要条件。

后续人工矩阵至少覆盖：Desktop↔PPT、Desktop↔Whiteboard、PPT↔Whiteboard、Centered/Free、左右展开、进入/退出吸附、横纵同时捕获、普通/居中提示双向切换、动画开关、既有 DPI/显示器配置和失败重试。GUI 未获授权或未执行时必须明确标记未验证，不得宣称视觉修复完成。
