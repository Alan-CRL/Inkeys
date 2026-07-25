# Windows Ink 绘制光标实施计划

## Implementation

1. 新增 `draw3.pen_cursor` 模块及项目清单，完成 Pen/Highlighter/EraserGripCircle 的纯 CPU SDF 栅格化、straight BGRA、`CreateIconIndirect` 和句柄销毁。
2. 扩展 `WindowController`：配置 Pen/Highlighter/Eraser 两档外观，实现事件 sink、活动工具覆盖、Pointer 类型动态探测、合并刷新消息和 `WM_SETCURSOR`。
3. 扩展 RTS：订阅 range/in-air 事件，在 Pen hover/contact/reset 路径发布 Hover/Contact/Default 并保留 inverted 信息，保证 shutdown 后不再访问 sink。
4. 由 `DrawingController` 使用现有尺寸/颜色配置三类光标，并在活动 Pen 工具变化时发布覆盖；更新 `main.cpp` 初始化接线。
5. 在测试工程加入新模块和 `pen_cursor_tests.cpp`，覆盖位图与状态决策；补充 RTS DataInterest/生命周期测试入口。

## Validation

```powershell
MSBuild.exe .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64
.\ARM64\Debug\inkStrokeModelerTestTests.exe
git diff --check
```

人工使用实体 Windows Ink Pen 验证 Pen/Highlighter hover 显示、Down/Move 隐藏、Eraser/倒转 Pen Hover/Contact 两档 Alpha、Up 恢复、系统 Mouse 接管、Touch、窗口边界和其他应用光标。记录当前 OS、设备与实际结果；Windows 7 无法实测时明确标记未验证。

## Validation Result

- 2026-07-25：ARM64 MSBuild 18.7.8 完整 `Debug|ARM64` 构建成功；两个 shader、主程序和测试工程均成功，只有既有 third-party 浮点转换警告。
- 2026-07-25：`ARM64\Debug\inkStrokeModelerTestTests.exe` 通过，包含 highlighter、pen cursor、contact input 和 RTS DataInterest 回归测试。
- 2026-07-25：根据真机反馈修正 Pen 光标 DPI 下限和 straight BGRA；本轮按用户要求仅完成差异、引用、编码与换行静态检查，未重新构建或运行测试。
- 2026-07-25：根据后续反馈将外框改为 0.75px `#B8B8B8`，并在 Pen Contact 期间使用当前窗口 `SetCursor(nullptr)` 隐藏；完整 `Debug|ARM64` 构建和全部控制台测试再次通过，仅有既有 third-party 警告。
- 2026-07-25：实现 EraserGripCircle、普通/倒转 Eraser 状态选择、Hover 0.75/Contact 1.0 两枚句柄、#808080 抓手圆环与三条竖线；ARM64 Debug 全量构建和全部控制台测试通过。
- 未验证：实体 Pen/Mouse/Touch、接触隐藏与 Eraser 不透明显示、Up 恢复、Windows 7 Pointer API 回退和 D3D Debug Layer。

## Risk And Rollback

- 风险集中在 Pointer/RTS 的 Hover/Contact 事件排序和 GDI Alpha 光标兼容。
- 光标状态与绘制数据完全隔离；发生问题时可移除 sink 接线和 `WM_SETCURSOR` 分支，不需要迁移或清理墨迹数据。
