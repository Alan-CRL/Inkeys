# 颜色按钮描边样式实施清单

## Implementation

- [x] 恢复 `Bar.Main.cpp` 的原始色块几何、间距和属性栏布局。
- [x] 将 11 个选中分支的描边目标由 `2px` 改为 `1px`。
- [x] 将深浅主题的 `SwatchFrame` 调整为更柔和的中性灰。
- [x] 保持 `Bar.Main.cppm` 和 `colorSelect.svg` 与基线一致。

## Validation

- [x] 搜索确认 11 个选中分支均为 `1px`，不存在颜色选中 `2px` 描边。
- [x] 对比确认 `Bar.Main.cpp` 没有几何、布局和输入逻辑差异。
- [x] 运行 `git diff --check` 并检查涉及文件的编码与换行。
- [x] 使用 ARM64 MSBuild 构建完整 `InkeysRepo.sln`。
- [x] 运行 Trellis 质量检查。
