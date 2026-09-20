# 实施计划

## Checklist

- [x] 1. 在现有 Draw3/产品状态边界集中定义 `0.35f` 荧光笔最终透明度，并给 UI3 提供当前工具有效透明度的只读查询。
- [x] 2. 在 `DrawingController` 中收敛普通笔/荧光笔 appearance 构造；构造时和帧边界样式变化时更新光标颜色、尺寸与 alpha。
- [x] 3. 调整 `ResolvePrimaryDrawingCursorVisual` 的 alpha 归一化条件：普通笔保持既有不透明语义，荧光笔保持 `0.35`，Eraser Hover 保持 `0.5`，Eraser Contact 保持 `1.0`。
- [x] 4. 将 UI3 Bar 透明度数字改为当前工具的有效最终透明度，移除旧 Draw2 `130/255` 注释与公式。
- [x] 5. 在产品集成的 `InkeysHeadlessTests` 中增加荧光笔和橡皮 Hover/Contact 回归；对 laser cursor/stroke 共享直径做静态或纯逻辑校验。
- [x] 6. 检查所有修改文件编码与 CRLF，运行 `git diff --check`。
- [x] 7. 使用 ARM64 MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug|ARM64`，超时至少 5 分钟。
- [x] 8. 运行 `Build\\ARM64\\Debug\\InkeysHeadlessTests.exe --no-window` 与 `Build\\ARM64\\Debug\\Inkeys.exe --draw3-hidden-test`，不得显示窗口。
- [x] 9. 对照 PRD 逐项复核 state -> bridge -> cursor/Bar 数据流，确认 Laser 无需新增 state 字段。

## Expected Files

- `Inkeys/IdtState.h`
- `Inkeys/IdtState.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`
- `InkeysHeadlessTests/draw3_contact_tests.cpp`

仅在验证表明主测试工程需要额外登记时修改 `.vcxproj`；优先扩展已登记的测试文件，避免新增工程项。

## Validation Commands

```powershell
git diff --check
& '<ARM64 MSBuild.exe>' InkeysRepo.sln /m:1 /t:Build /p:Configuration=Debug /p:Platform=ARM64
.\\Build\\ARM64\\Debug\\InkeysHeadlessTests.exe --no-window
.\\Build\\ARM64\\Debug\\Inkeys.exe --draw3-hidden-test
```

## Review Gates

- 普通笔实际 width 不得因光标最小值而被修改。
- 活动笔画继续使用 Down 时锁存样式，只有 Hover 光标跟随最新 state。
- 荧光笔 shader 最终 alpha 必须是 `opacity * fillAlpha = 0.35`。
- Eraser Hover/Contact 必须覆盖 Pen、Mouse、inverted Pen 与 Touch 的既有 resolver 分支。
- Laser 的笔迹和光标都必须从 `kLaserSolidDiameterAt96Dpi * dpiScale` 推导。
