# Implementation Plan

## Ordered Checklist

- [ ] 追加工具枚举、`Q/W/E/R` 键位、工具分类/名称/宽度/光标/触觉规则和 README 文档。
- [ ] 在 renderer 契约中加入 `ShapePrimitiveKind`、32 字节 `ShapePrimitive`、批量 Draw、DPI 圆角配置、bounds helper 与零像素预热。
- [ ] 扩展 VS/PS shape 16..19，实现实线、4:2 圆头虚线、8 DIP rounded rectangle fill/outline。
- [ ] 增加固定大小 Shape runtime；接入模型 Update/Predict、原始 Down/Up 端点、L0-only 批量重建和稳定帧复用。
- [ ] 扩展 Stored 类型、双端点完成态、统一重放、精确 bounds/tile footprint，并接入现有 L2/history/page restore。
- [ ] 补充文档、geometry、history、建模回退和性能测试；同步 Native/Shader specs。
- [ ] 运行 diff/编码检查、ARM64 Debug/Release 全解决方案构建、自动测试、`--drawing-perf` 和人工 D3D 验证。

## Validation Commands

```powershell
git diff --check
& '<ARM64 MSBuild.exe>' inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64
.\ARM64\Debug\inkStrokeModelerTestTests.exe
& '<ARM64 MSBuild.exe>' inkStrokeModelerTest.sln /m /p:Configuration=Release /p:Platform=ARM64
.\ARM64\Release\inkStrokeModelerTestTests.exe --drawing-perf
```

构建超时至少 5 分钟；必须构建完整 solution，不能只构建单个项目。

## Risk And Rollback Points

- CPU/HLSL shape 编号或 32 字节布局不一致会产生错误几何；用 explicit enum、`static_assert` 和 shader spec 锁定。
- 填充矩形 footprint 若仍按对角线计算会破坏撤回；必须有内部 tile 测试。
- Shape 错入 L1 会在 prediction 回缩后残留；controller 分支必须在稳定提交前提前处理。
- 多 contact L0 清理后必须重放所有仍活动 Shape；完成/Cancel/resize 测试覆盖该边界。
- 若新 renderer pass 失败，可独立回退 Shape tool 分支，不修改既有 Pen/Highlighter/Eraser/Laser 协议。
