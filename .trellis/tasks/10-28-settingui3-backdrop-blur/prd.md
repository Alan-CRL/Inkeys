# SettingUI3 背景动态模糊：仅 Win10/11 启用并统一 Win10 效果

## Goal

设置窗口的透明背景只在 Windows 10 与 Windows 11 上启用，并且两个系统使用同一种效果：实时反映下层窗口的动态背景模糊。Win10 以下的系统直接使用不透明背景，不提供任何 Aero 回退。

## Requirements

- Windows 11 不再使用为它单独设计的 DWM 透明效果（`DWMWA_SYSTEMBACKDROP_TYPE` / 旧 Mica 属性）。该效果与桌面壁纸静态合成，且当前在本窗口上看不到。
- Windows 11 改用与 Windows 10 完全相同的效果：通过 `SetWindowCompositionAttribute` 的 accent policy 做动态背景模糊，实时反映下层窗口的颜色变化。
- 该效果只在 Windows 10（现有下限 build 17134 / RS4 及以上）和 Windows 11 上尝试。
- Windows 7、Windows 8、Windows 8.1 以及低于下限的 Windows 10 一律使用不透明 Solid 背景，不引入 `DwmEnableBlurBehindWindow` 等 Aero 回退。
- 高对比度开启时保持 Solid，不改变用户设置的配色。
- 动态模糊的性能约束保持现状：拖动/缩放窗口期间使用更轻的普通模糊，结束交互后恢复完整效果；不逐条位置消息重设材质；不为此引入 DirectComposition，也不改动 ImFluent vendor。
- 效果应用失败、API 缺失或 DWM 关闭时，无错误回退到 Solid，不能阻断设置窗口的初始化与显示。
- 设置窗口关闭时必须撤销已应用的效果，不把透明状态遗留到窗口销毁。
- 模糊层必须与可见窗口边界对齐。当前 Win10 上模糊层在左、右、下三侧比窗口大一圈，需要修复；Win11 改用同一效果后也必须对齐。
- Win10 上窗口为直角；Win11 保持系统圆角，模糊不能露在圆角外侧。
- 缩放热区放在可见窗口边缘外侧，宽度与现在左/右/下的热区相同（约 8 px），四边与四角一致，与 Win10/11 原生窗口一致。不得把热区移到窗口内侧，避免挡住滚动条和内容；当前顶部热区落在窗口内侧的问题一并消除。
- 最大化与 Aero Snap 行为不变，窗口阴影保留。

## Acceptance Criteria

- [ ] Windows 11 上设置窗口呈现与 Windows 10 相同的动态背景模糊：移动下层窗口或改变其内容时，设置窗口背景随之变化，而不是显示与壁纸相关的静态效果。
- [ ] Windows 10（build 17134 及以上）上的现有效果与行为保持不变，包括拖动/缩放时的轻量模糊切换。
- [ ] Windows 7、8、8.1 及低于下限的 Windows 10 上设置窗口始终为不透明背景，且代码中不存在 Aero blur-behind 回退路径。
- [ ] 高对比度、DWM 关闭、API 缺失或调用失败时均为不透明背景，设置窗口仍可正常打开与关闭。
- [ ] 关闭设置窗口后效果被撤销，再次打开时按当前系统重新判定。
- [ ] Win10 与 Win11 上模糊层四边都与可见窗口边界对齐，不在任何一侧溢出；Win10 为直角。
- [ ] 恢复态窗口的四边与四角缩放热区都在可见边缘外侧约 8 px，顶部与其他三边一致，热区不覆盖窗口内侧的滚动条与内容；最大化、还原和 Aero Snap 正常，窗口阴影保留。
- [ ] Headless 测试与规范同步更新：背景能力不再包含 Mica 优先级，测试反映“Win10/11 动态模糊，其余 Solid”。

## Out of Scope

- 不修复 Windows 11 上 DWM Mica/系统 Acrylic 看不到的问题；该路径整体移除。
- 不改变设置窗口的渲染频率、swap chain 模型或其他窗口（悬浮栏、白板、消息框）的透明行为。
- 不读取系统“透明效果”开关；accent 效果不受该开关控制，保持与现状一致。
- Win7/8/8.1 的窗口框架几何不改动（这些系统不启用模糊）。

## Notes

- 现状调查见 `research/current-backdrop-analysis.md`。
- Windows 10 下限沿用现有判定（build >= 17134）。若需要覆盖更早的 Windows 10，需要另行确认那些版本上 accent 模糊的可用性。
