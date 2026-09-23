# 第一批功能修正与帧采样实施记录

日期：2026-09-23。用户已批准第一批 S1–S4；本文只记录本实现分工，不替代主代理的完整构建与质量门记录。

## 变更

- `Bar.RenderLoop.cpp`：删除 ColorSelect1..11 选中/未选中分支 22 个中间 `ft.SetTar(1.0)`；统一几何循环的最终目标、SyncValueDuration、一次性显隐/换边重定向保留。添加简短中文注释说明最终目标所有权。
- `Bar.Scene.cpp`：三个私有 helper 返回本次实际非空 damage 贡献。Publish 始终保存最新快照，只在本次贡献为真时收集 hooks；首次订阅仍全脏/唤醒，Render 调用仍应用快照，回调仍在 registry/scene 锁外执行。没有修改公共 Scene API、其它 producer 或 PageControl 通用空 damage fallback。
- `Bar.RenderLoop.cpp`、`PageControl.cpp`：DC 仍经 COPY 获取且仅供 ULW 读取，ReleaseDC 使用空 RECT；API 配对与四阶段成功提交事务保持。
- `Bar.FramePacing.cppm`：增加最近 Tick 原始 elapsed 的只读 getter；Tick 的有限值/负数处理、50ms clamp、Rebase 及调用时机保持。
- Bar S4：使用 RenderPipeline 的可空诊断指针与 FrameStageTimer，采集 raw/used dt、是否实际进入动画推进、GetDC/ULW 尝试、完整提交、资源/呈现失败、合法延期、退避跳过/重置和结束时计数；绘制、两处既有 direct-drag 锁等待、GetDC、ULW、ReleaseDC、EndDraw 分段耗时。记录 target/capacity、显示峰值 zoom、当前 zoom、viewport/source、光源状态。源自回调开始的旧已提交几何会在新 candidate 确定后覆盖；资源失败时没有新的 candidate，错误码区分此状态。
- PageControl S4：同一数组采样绘制/四阶段呈现和两把既有回调锁等待；记录真实资源结果、HR 与 ULW 失败后的即时 GetLastError、target/backing/presentation/source/zoom。Scene.Render 在原有锁内只读现成光源快照补光源 flags。PageControl 的 barSampled 保持 false，不伪造 Bar 动画数据。

所有新增采样只观察数据，没有新增 GUI、线程、锁、每帧日志或资源探测；日志汇总/限频由 RenderPipeline 诊断分工处理。FrameStageTimer 只在诊断指针非空时读取单调时钟。

## S2 静态边界审查

- 无影响 cursor 变化：resolved.damage 经 presentation 裁剪为空，返回 false；旧 pending/invalidated 不参与贡献判断。
- 光离开、隐藏、强度归零：原 helper 保留 previous 与 current 的 union，因此旧光擦除贡献不丢。
- 同区域强度/颜色变化：cursorChanged 仍来自完整字段比较，返回本次非空 union，不依赖累计矩形是否扩大。
- primary/mapping 变化：仍保守全脏；先更新 cursor 历史，再返回 primary 或 cursor 贡献，未被短路跳过。
- 首次订阅/退订、布局显隐、业务与失败请求：入口及 pending/Retry/Continue 原样保留，未混入光照过滤门。
- 当前 headless 没有链接 Scene.cpp，未执行真实 Scene hooks 集成计数；不得以此记录宣称该集成覆盖已完成。

## 验证

2026-09-23 从当前源码重新编译 Metrics、Animation 接口/实现、FramePacing，采用当前 Visual Studio 的 v143 ARM64 原生 cl；路径从 vswhere/v143.default.txt 动态查询，SDK 版本读取现有 headless 项目。探针直接提取本次新增的两个测试函数原文，链接当前完整 dirty_region_tests.cpp；未创建新项目/构建系统。

- `TestColorSwatchThicknessFollowsPanelScale`：8 组起始展开/收起、是否中途反向、均匀/非均匀正 dt；每组 90 帧，断言每帧描边/面板比例为 1，最终均收敛不续动画。
- `TestAnimationFrameClockRebase`：包含既有 Rebase 用例；新断言 200ms 原始间隔仍返回 50ms，负间隔仍返回零，重置后原始/实际间隔一致。
- 当前 `RunDirtyRegionTests` 全部算法用例（包括边缘裁剪、旧/新矩形和失败保留）直接编译链接并执行。
- 编译/执行 exit 0，无 warning；stdout：`functional_regression_failures=0`。临时产物目录 `%TEMP%/ui3-functional-regression`。
- 生产接线静态审查确认 22 处中间目标均不存在、最终目标只在统一循环出现一次、SyncValueDuration 保留；两个只读 ReleaseDC 均使用空 RECT。
- `git diff --check` 通过；六个所有权文件 UTF-8/BOM 策略与原始 HEAD 一致，全部保持 CRLF。

探针验证真实 Animation/FramePacing 模块和 dirty 算法，不等价于运行整个 RenderLoop 或 Scene hooks。主 Solution 与完整 headless 程序由主代理统一构建；本分工没有启动 GUI、commit 或 push。用户原始偶发严重卡顿是否全部消失仍需实际故障反馈/日志。

## 执行命令入口

独立针对性验证使用本任务 probe 流程，不调用产品窗口：

```powershell
# cl.exe 从 vswhere + Microsoft.VCToolsVersion.v143.default.txt 定位 Hostarm64/arm64。
# INCLUDE/LIB 指向该工具链与现有测试项目声明的 Windows SDK。
$ui3Flags = @('/nologo','/std:c++20','/EHsc','/utf-8','/W4','/O2','/MT','/permissive-')
# 用 /c /TP /interface 分别编译 Bar.Metrics.cppm、Bar.Animation.cppm、Bar.FramePacing.cppm；
# 使用 /reference 编译 Bar.Animation.cpp，及原样提取两个测试函数的 functional_probe.cpp。
# 另直接编译 InkeysHeadlessTests/dirty_region_tests.cpp，再链接上述 obj 与 user32.lib。
& (Join-Path $env:TEMP 'ui3-functional-regression/functional_probe.exe')
# stdout: functional_regression_failures=0 ; exit 0
```

这份记录中的 cl 步骤是实际命令概要，不是完整可复制的环境初始化脚本。主代理的正式验证入口为完整 Solution/既有 headless 项目与 `InkeysHeadlessTests.exe --no-window`；最终结果以主代理的构建日志为准。
