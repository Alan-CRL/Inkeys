# 白板菜单层级与主题预览独立检查

## 范围

仅检查相对 `92c3c8b1` 本轮 `Bar.Scene.cpp`、`Whiteboard.Theme.h`、`PageControl.cpp` 与新增回归；未重复评审此前整轮 UI3 重构。当前白板父任务继续 `in_progress`，本轮未提交或归档。

## Findings (fixed)

- `PageControl.cpp`：新色块像素断言使用 `size_t` 参与负 Y 坐标计算，引发无符号下溢；显式转换行/列索引为 `double`，保留原精确 RGB 阈值。同时为两次刻意忽略的 `Advance` 返回值添加 `(void)`，消除本轮新增警告。
- `PageControl.cpp`：新重叠测试把每一组 menu/theme 重叠都当成 menu 最前，遗漏两者同时被分页栏遮挡的三层情况。加入固定 305×80、8 DIP 圆角面板的独立区域判据：三层情况下三个输入入口必须一致指向分页按钮或分页空白，且 sample 只能报告 primaryBackground；其他重叠仍严格要求具体前层按钮身份，并要求三种重叠均实际发生。
- 测试判据首次尝试引用 Bar 内部未导出命中类型导致编译失败；改为局部独立圆角区域算式，没有为测试扩大模块导出接口。

## Findings (not fixed)

未发现本轮产品代码缺陷。既有 hashlib 三项编译 warning 不属本次范围，保留。

## 代码路径确认

- `BarPageSurface::Render` 先绘主题、再更多、最后分页栏；不改变面板目标位置、共享 motion 或资源路径。
- `FrontPanel` 按分页、更多、主题选择首个包含指针的可见圆角背景；其空白区也会阻挡后层按钮。`HitTest`、`SamplePointer`、`Pointer` 共用该层级；关闭动画保留可见面板遮挡。
- 六色由同一 `WhiteboardVisual::ThemeColors` 提供显示、选中与点击映射，顺序为墨绿、深蓝、纯黑、白、灰白、米白。
- 样式纹理范围改为完整按钮，标签保持标准 UI3 槽；70 DIP 预览配现有 24 DIP 居中相位产生 3×3 点／三横三竖九交点。格线延伸到按钮上下左右边缘，点与线均不因标签槽缩短。沿用原画布纹理函数，未引入新缓存、动画或后端。

## Verification

- Lint：`git diff --check` 通过；原 prd 历史混合换行提示保留。
- TypeCheck / Build：主会话完成 ARM64 原生 MSBuild 完整 `InkeysRepo.sln Debug|ARM64`，最终退出 0，仅既有 hashlib warning。日志：`TestResults/ui3-shared-runtime/preview-revision-final-build-2.log`。
- Tests：主会话实际 D2D 无窗口 `--bar-eraser-offscreen-test` 退出 0；独立核对最终日志 `PageControlUI3 failures=0`、`EraserVisual failures=0`。包括六色色块／点击映射、全按钮九点／九交点、动画三种重叠及命中一致性；原九组 dirty-vs-full 均 `exactPixels=1`。
- 源文件 UTF-8 无 BOM、CRLF 保持；三源均无 lone LF。
- 未启动 GUI；未新增 27 组性能基准，未把本轮小改回归报告为原生视觉或性能预算通过。重叠层级直接像素断言未额外扩展，层级由生产绘制顺序审查及实际动画命中回归确认。
