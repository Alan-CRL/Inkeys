# 实施计划：主栏 Redo 弹窗

## 有序清单

- [x] 1. 状态与通道：`Bar.State.cppm` 加 `recallAttribute`；`Bar.ToggleClickCoalescer.cppm` 加 `RecallAttribute`。
- [x] 2. Layout：新建 `Bar.RecallAttributeLayout.cppm`，单按钮布局+上下边/clamp，对齐橡皮。
- [x] 3. Motion+Panel：新建/复用 Motion，新建 `Bar.RecallAttribute.cpp(.cppm)`，`Bar.Main.cppm` 注册成员；工程文件登记新源码（如需）。
- [x] 4. Redo 按钮：弹窗内 `twoTwo`/`Redo` 按钮，`barRedo` 图标+i18n，接线 `PublishProductCommand(Redo)` 常开。
- [x] 5. Recall 改写：`Bar.Button.cpp` 先 Undo 后展开常 true；更新 TODO。
- [x] 6. 互斥管线：`CollapseAuxiliaryPanels`、`Pointer` 前置、`PresetHoming`/fold、Escape，以及 RenderLoop 的 Advance/Draw/脏区/窗口边界。
- [x] 7. 测试：headless `recall_attribute_tests.cpp` 覆盖布局与 0.4s Back/Sine；`InkeysHeadlessTests --no-window` 已通过。离屏视觉用例未单独复制橡皮的 D2D 截图测试。

## 验证命令

```bash
# 构建（按 spec：完整 sln，Debug|ARM64；本机按实际架构调整前先确认）
# msbuild InkeysRepo.sln /m:1 /t:Build /p:Configuration=Debug /p:Platform=ARM64
.\build\InkeysHeadlessTests.exe --no-window
```

- 动画断言：展开/收起 0.4s、Back 超调 >1.01、Sine alpha 有界、60 DIP 压缩、换边同批次。
- 行为断言：AC1-AC6 逐项手动+单测覆盖。

## 评审门

- `implement.jsonl` / `check.jsonl` 已维护且 `task.py validate` 通过后才 `start`。
- 实现后走 `trellis-check`（spec 合规、lint/typecheck、跨层一致、复用检查）。

## 回滚点

- 步骤 1-2 无 UI 可见变化，可独立 revert。
- 步骤 3-6 任一步回归绘制/橡皮动画即停，优先 revert 本任务新增文件。
