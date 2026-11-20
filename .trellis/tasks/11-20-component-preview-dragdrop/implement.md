# C3 实施计划

实现：Astra（`gpt-6.2-astra`，workspace-write）。审查：Sol。主 Agent 复验门禁。Astra 运行期间主 Agent 不并行构建或运行产品测试。

## 白名单
见 `design.md` §1 表格。另允许：`Inkeys/src/i18n/zh-CN.jsonc`（只新增 §5.2 的拖动提示键，放在 `SettingsUI.Component` 末尾）及其生成产物 `Inkeys/IdtI18nKeys.g.h`、`Scripts/i18n.zh-CN.snapshot.jsonc`（运行 `pwsh -NoProfile -File Scripts/i18n.ps1 sync` 生成；其他语言的 `[[Need translation]]` 标记由主 Agent 补译）。

## 步骤
1. `UI3::DragSession` + 单测。
2. `BarContentInstance` 幽灵按钮/跟随提示 + 离屏测试。
3. 预乘混合回调（ImGui DX11 backend）+ 宿主纹理生命周期与合成（预览块居中、尺度、主题/尺寸/方向跟随）。
4. 输入映射、悬停/点光/提示、点击 More 展开。
5. 拖放：落点候选与过滤（纯函数 + 单测）、插入光标、More 停留展开、垃圾桶、放下应用（C1 入口 + 写盘回调）、Stale 与外部变更处理、`BeginLibraryDrag`。
6. 边缘自动滚动。
7. 门禁：golden 与 C2 基线逐字节一致；`--ui3-host-offscreen-test`、`--ui3-attribute-dirty-test`、`--bar-eraser-offscreen-test`、`--ui3-geometry-attribute-test` 通过；headless 通过。

## 回滚点
每步独立；步骤 3 起的设置页接入可整体回滚，C1/C2 不受影响。
