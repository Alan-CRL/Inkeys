# Validation — 11-05-shape-ux-icons

## 已完成（implement 子代理，2026-11-05）

- 已替换 28 个 `Inkeys/src/UI/barShape*.svg`，清单见 `research/icon-audit.md`。`barShapeStraightLine.svg` 按用户决定保留原样。
- 54 个 barShape 资源在工作树中仍为 CRLF；git diff 只涉及这 28 个文件。
- 草稿已用 XML 解析检查：只用 `rgba(10,0,7,0)` / `rgba(9,0,2,0)` 两个占位色，`fill-opacity` 只有 0.24，没有写死的颜色。
- 预览页 `Build/shape-ux-icons/preview.html`，截图 `preview.png` / `preview-solids.png`，已经用户确认。
- 没有修改 C++、rc 或 vcxproj：资源名不变。

## 待执行（由主会话运行）

- [ ] 完整构建 `InkeysRepo.sln` Debug|当前原生架构，期望 0 error（其他代理正在构建，本子任务未运行）。
- [ ] `Inkeys.exe --ui3-geometry-attribute-test`：真实 SVG 像素与非空检查通过（覆盖 36 类目录图标与 Polygon3–12 轮播资源）。
- [ ] 人工：深浅主题下主栏图形面板选中变色正常（IconPrimary ↔ Accent，填充层随 color2 一起变色）；1.0/1.5/2.0 倍率下立体虚线可辨。
- [ ] 注意：预览由浏览器渲染，lunasvg 的 dasharray、fill-opacity 实际效果以离屏测试和实机为准。
