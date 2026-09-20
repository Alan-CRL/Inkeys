# Implementation Plan

## 1. Rebase On Current UI3 State

- [x] 重新检查 `git status`、Bar UI3 最新提交、现有渲染循环/动态 HWND/脏区接口和 `IdtMain.h` 用户改动。
- [x] 搜索 `ppt_window`、`WindowRole::PptControls`、`PptUiChangeSignal`、鼠标钩子、放大镜排除和 PPT 配置的全部生产引用。
- [x] 确认工程文件中的 C++20 module 清单、编译顺序和 Headless 测试接入模式。

## 2. Shared UI3 Scheduler

- [x] 抽取共享 device 所有权和单帧客户端契约，实现请求位、无丢失 wake/reset、固定顺序、60 FPS pacing、续帧与失败重试。
- [x] 将 Bar 的内部等待循环改为调度器客户端；保留现有动态 HWND 和脏区语义，使 Bar 直移只暂停自身。
- [x] 添加调度器 Headless 测试：单请求、多请求、并发合并、续帧、局部 retry、设备级重建和单次 idle 休眠。

## 3. Five PPT Windows

- [x] 扩展窗口服务角色与生命周期，创建五个由 Drawpad 拥有的 no-activate layered popup，并实现 Bar 固定置顶与 PPT 最近交互前置。
- [x] 新增 PPT UI3 状态、几何、交互、绘制及每窗目标资源；迁移浅色视觉、固定 backing、动画、命中和坐标映射。
- [x] 实现五窗独立 ULW dirty 提交，以及红色活动、绿色 idle 最终、蓝色 HWND 边界诊断。
- [x] 添加 owner/Z 序、生命周期、固定尺寸、DPI/缩放、拖动/吸附/排斥和脏区事务 Headless 测试。

## 4. Behavior Migration And Cutover

- [x] 从 `IdtPlug-in.cpp` 迁移鼠标、触摸、滚轮、400 ms 长按、页码点击、拖动、结束放映确认和绘制模式 UI 行为，保留业务命令桥。
- [x] 连接页码快照、成对位置配置和 Settings 精确请求；迁移鼠标钩子输入及放大镜五窗排除列表。
- [x] 删除生产路径中的旧全屏 `ppt_window`、旧窗口角色和旧 D2D/ULW 状态，保留 ABI、配置及页级墨迹逻辑。
- [x] 更新工程文件和必要资源声明，保持原编码、CRLF 与关键中文注释。

## 5. Verification Gates

- [x] 运行新增及现有 `InkeysHeadlessTests`，修复回归后重复执行。
- [x] 使用 ARM64 host `MSBuild.exe` 构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，超时不少于 300 秒。
- [x] 检查 `git diff --check`、变更范围和工程文件同步，确认未覆盖 `IdtMain.h` 等用户改动。
- [ ] 手工验证 PowerPoint/WPS：显隐、页码、点击/滚轮/长按、拖动/吸附/排斥、结束确认、绘制模式、配置开关、缩放、DPI/显示器、Bar 遮挡及脏区诊断。
- [x] 运行 `trellis-check`，记录无法自动完成的手工验收项和剩余风险。

自动验证已完成：`InkeysHeadlessTests.exe --no-window`、完整窗口模式以及 ARM64 host 的完整 Solution `Debug|ARM64` 构建均通过。PowerPoint/WPS、真实显示器/DPI 切换和红/绿/蓝脏区可视化仍需在可用 GUI 与 Office/WPS 环境中手工验收，因此对应手工项保持未勾选。

## Risk And Rollback Points

- 调度器接入 Bar 后先构建和测试；若失败，回退该阶段而不继续五窗迁移。
- 五窗并行运行稳定后才移除旧窗口；切换失败时可恢复旧创建路径而不回退 ABI 或配置。
- 对共享设备丢失和 wake/reset 竞态优先使用纯逻辑测试锁定，再连接 Win32/D2D 资源。
