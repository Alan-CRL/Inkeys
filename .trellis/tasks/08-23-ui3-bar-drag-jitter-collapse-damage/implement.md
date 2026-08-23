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
