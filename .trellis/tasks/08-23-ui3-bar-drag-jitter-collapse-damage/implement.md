# UI3 主栏拖动闪动与缩窄残影修复 - Implementation Plan

## Implementation

1. [x] 在 `Bar.BottomDock.h` 增加可测试的释放交接判定与过期帧屏幕位移解析，保持当前帧路径不变。
2. [x] 在 `Bar.RenderLoop.cpp` 的帧入口阻止位移吸收前的释放态呈现，并在 ULW 锁内使用实际已呈现 translation 解析目的地。
3. [x] 合并 viewport mapping 与 present mapping 的整窗替换判定，同一布尔值控制 damage 和 `prcDirty`。
4. [x] 在 `Bar.Main.cppm` / `Bar.Interaction.cpp` 增加单快照二维命中入口，替换主体与抓手的分轴组合调用。
5. [x] 为 release handoff、stale-frame translation、full replacement 和单快照双轴逆映射补充 Headless 回归测试。

## Validation

1. [x] 运行受影响的 Headless 测试目标。
2. [x] 运行全部 `InkeysHeadlessTests.exe --no-window`。
3. [x] 运行 `git diff --check` 并检查修改范围、编码与 CRLF。
4. [x] 用 ARM64 host `MSBuild.exe` 构建完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，超时至少 5 分钟。
5. [x] 静态复核：没有可见 GUI、没有额外窗口/提交链、没有改变阈值和动画语义。

## Risk And Rollback Points

- 释放门禁位于共享渲染帧入口；若错误返回 Idle 会丢请求，因此只允许 Retry/Continue 并依赖已发布 generation。
- 过期帧目的地必须读取真实已呈现 translation，不能读取未上屏目标 translation。
- 整窗替换可能增加少量瞬态提交面积，但只发生在映射 tuple 改变时，稳定动画帧仍使用局部 dirty。
- 组合命中只改变快照读取次数，不改变映射公式和控件命中范围。

## 2026-09-09 续修计划

1. [x] 对照初始设计、当前 spec 和现有测试，定位三项复现的实际坐标/动画/damage 根因。
2. [x] 在 UI3 Bar 所属层做最小修复，关键步骤补充简短中文注释。
3. [x] 在现有 Headless 测试中增加能复现旧行为失败的组合回归，不只测试公式镜像。
4. [x] 由独立 trellis-check 审查实现与边界，修复确证问题。
5. [x] ARM64 host 完整构建 InkeysRepo.sln Debug | ARM64，构建至少允许 5 分钟；运行全部 --no-window 测试。
6. [x] 复核 git diff --check、文件编码和原换行；更新任务根因与 spec，仅记录无 GUI 验证结论，不 commit。

## 2026-09-09 验证结果

- 独立 trellis-check 已完成全部 Bar/Headless 改动审查，并修正抓取 X/Y 配对发布、首次转换过期帧目的地及 barrier 发布顺序。
- 最终使用 ARM64 host `C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe` 构建完整 `InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /p:PreferredToolArchitecture=arm64`，退出码 0，耗时 23.78 秒；构建超时配置为 900 秒。主程序与 PptCOM/测试项目均包含在完整 Solution 中。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 退出码 0，耗时 2.13 秒，输出 `PASS animation correctness`。新增用例覆盖持久像素清除、并发 resize 发布/部分失败、同目标隐藏按钮轨迹、1/4 帧快速重捕获，以及普通后续采样赶超首次吸附帧的 X/Y 连续性。
- 日志：`Build/ui3-bottom-dock-validation/msbuild.log` 与 `headless.log`；验证脚本仅对其子进程消除重复 Path/PATH 环境键，不修改系统或项目配置。
- `git diff --check` 通过，源文件 BOM/UTF-8/CRLF 保留；构建产生的无关 `Inkeys/PptCOM.dll` 差异已恢复。
- 未启动可见 GUI，未创建 commit。真实鼠标/触摸果冻、PPT 按钮收起及截图脏区效果待维护者复测，任务保持 in_progress。

## 当前提交与后续工作

用户已确认问题 2、3 修复并明确授权提交当前改动。问题 1 尚未关闭，下一轮继续追踪反复进入/退出底栏时的 Y 跳帧，不因当前构建和 Headless 通过而认定其完成。
