# C2 可实例化的主栏内容层

## Goal

从真实主栏中抽出可创建多份的主栏本体实例 `BarContentInstance`，真实主栏委托它绘制，设置页预览创建另一份。总体设计见父任务 `design.md` §4。

## Requirements

- 实例持有自己的按钮列表、布局结果、动画状态、尺度、主题快照与局部光照，不读写 `barUISet`、`BarAtomic`、`floating_window` 等全局对象；需要的全局输入以快照传入。
- 可绘制到任意 `UI3::FrameTarget`（含 `ExternalContext`）。
- 支持：无主按钮模式；禁用按钮动作；指定条目“抓起”（从布局移除并收缩）与“放入”（插入并展开）的动画；More 面板展开/收起与面板内布局；外部提供的指针坐标驱动悬停、点光与小尺寸悬停提示。
- 真实主栏行为不变。

## Acceptance Criteria

- [ ] 开工前在本任务 `design.md` 列出 `DrawMainBarContent` 读取的全部状态，并逐项决定“实例化 / 快照传入 / 预览不需要”。
- [ ] 真实主栏委托后：Bar golden、第二宿主、属性脏区测试全部与基线一致。
- [ ] 新增离屏测试：两个实例同时存在、各自布局与动画互不影响；预览实例在大/小、深/浅下可绘制。
- [ ] `InkeysRepo.sln` `Debug | ARM64` 构建通过。

## Dependencies

可与 C1 并行开发，但为避免两个写入方同时改 `Bar.Button.cpp` / `Bar.RenderLoop.cpp`，按 C1 → C2 顺序进行。
