# UI3 绘制属性增量交互调整

## Goal

在不改变绘制业务模式、标注线可用性、颜色选择语义或第三鼠标光其他使用者的前提下，完善 UI3 绘制属性面板的笔型扩展分割线、粗细 Slider 状态与位置、荧光笔快捷粗细、颜色选择器 Popup 及两枚三角指示器的视觉和动画一致性。

## Confirmed Facts

- 当前实现位于 `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`，绘制属性的瞬态状态位于 `Bar.State.cppm`，形状、SVG、文字与光源字段由 `Bar.Main.cppm`、`Bar.UI.cppm` 定义。
- 笔型扩展入口只对 `PenModeSupportsAnnotationLine()` 返回 true 的当前笔型出现；当前为硬笔与荧光笔。自由线仍为当前选中项，标注线仍必须保持 Disabled。
- 颜色选择器和笔型菜单均在现有全屏 `floating_window` 内布局和命中，不创建独立 HWND。
- 当前高亮笔快捷值是 `30 / 50 / 80 DIP`；通过 `GetBarThicknessPresetPx()` 乘 `barStyle.dpiZoom` 后写入既有 `SetPenWidth()`。
- Inkeys2 历史分支 `origin/inkeys2-final` 的 `智绘教/IdtFloating.cpp` 只提供了两个荧光笔快捷按钮：`35 * drawingScale` 与 `50 * drawingScale`；`35 * drawingScale` 同时是默认宽度。`drawingScale = min(screenWidth / 1920, screenHeight / 1080)`，因此它是画布像素宽度随屏幕分辨率缩放的体系，不是 DIP。

## Requirements

### R1 Pen Type Divider

- 当前选中且支持标注线的笔型入口保持 `[主区域 | 分割线 | 小三角]`。
- 分割线是稳定的独立视觉元素：主入口按压时不得改变分割线的位置、缩放、透明度、颜色或其他视觉值；不得用反向坐标补偿父级 transform。
- 选中入口的分割线使用既有 `BarThemeColorEnum::Accent`，不硬编码 RGB。
- hover、pressed、selected、disabled 的过渡不得让分割线闪回 `SurfaceFrame` 的白色；资格失效时仍立即撤销分割线和入口命中。

### R2 Thickness Slider

- 对 Slider Thumb 的有效 pointer/mouse down 即视为已开始 Slider 交互，并立即结束粗细小三角的展开/固定状态；不能等待 drag threshold、pointer move 或数值变化。
- 单击 Thumb 不移动、单击后拖动、点击 track、capture/release/cancel 均不得破坏 Slider 拖动和一次性提交规则。
- Slider 不论 Overflow Hint 在进入会话前是否存在，都使用旧 Position B 的较低 Y；不再保留 A/B 的位置分支。
- 必须保留上一轮的 Overflow Hint 生命周期：Slider 中不新建 Hint，已有 Hint 可继续保留；不可能溢出时可消失；Slider 会话中消失后即使再次 overflow 也不重建；仅 Preview 或 Slider -> Preview 可新出现。

### R3 Highlighter Presets

- 以 Inkeys2 实际的 35、50 画布像素基准和默认值 35 为依据，重设 UI3 的三档荧光笔快捷值。
- 不直接复制旧值：UI3 采用 DIP 常量乘 `dpiZoom` 后保存画笔宽度，旧版采用 1920x1080 基准的画布像素缩放。
- 建议下一轮采用 `35 / 50 / 70 DIP`：前两档保持旧版基准，第三档按约 1.4 倍的连续笔触梯度补齐，而非沿用当前跨距过大的 80。该建议必须在实现前以运行态书写手感验证，不改变 Slider 的 `30..100 DIP` 范围。

### R4 Color Picker

- 大尺寸颜色选择器延续现有 Popup motion language：锚点相对位移、opacity、克制的 scale 差和 `EaseOutBack` 展开/`EaseInCubic` 收起；不得从极小尺寸夸张膨胀。
- 底部 R、G、B、透明度使用固定列/锚点和保留宽度。任一数值长度变化不得推动相邻字段或自身的数字对齐位置；保持现有本地化路径可替换文案。
- R 左边缘和透明度右边缘的外侧 horizontal padding 应从 footer 的实际 vertical padding 推导，而非沿用色板固定 `5 DIP` inset。
- Color Picker 只使用第三鼠标光。不得通过全局关闭第一光源实现，也不得影响其他 Popup。

### R5 Triangle Animation

- 粗细调节小三角和笔型扩展小三角在 Expanded/Collapsed 间均平滑旋转 180 度；收起沿展开路径反向返回，稳定值是 `0 <-> 180`，不能继续转到 360。
- Collapsed 朝向应指向真实展开方向，Expanded 朝向应指向收回方向。笔型菜单必须基于锁存的 `penTypeMenuOpenBelow`，粗细入口必须基于实际 Preview/Slider 展开侧，禁止固定写为 up/down。
- 快速连续点击、动画中反向、Popup 被外部状态关闭时，必须从 `BarUiSVGClass::angle` 的当前值继续向新 target 过渡，且与实际 Popup/Slider 状态同步。

### R6 Regression Constraints

- 不启用标注线功能，不修改绘制算法、颜色选取规则、资源、shader、持久化字段或传统 UI。
- 保持 Overflow Hint 生命周期、Hint Tooltip 命中、Slider/Hint 共存条件、快速粗细按钮与分割线对齐、Slider 两端对齐、hold-lock 提示位置。
- 保持笔型 Popup 远离 Main Toolbar 展开、自由线选中、标注线 Disabled、Fluent Checkmark、Popup 与 `[?]` Tooltip overlay 顺序及已有入退场动画。
- 保持第三光源的其余使用者和主光行为。

## Acceptance Criteria

- [ ] AC1 选中笔型入口的分割线为主题 Accent，按压主入口时其几何、透明度和颜色均稳定，hover/pressed 不闪白。
- [ ] AC2 Thumb 的单击无拖动也会结束粗细三角固定展开态；拖动、track 点击、capture 取消和抬起后 Slider 行为正确。
- [ ] AC3 Slider 所有会话均位于原 Position B；删除仅服务于 Position A/B 选择的状态分支，Overflow Hint 状态机逐项无回归。
- [ ] AC4 高亮快捷档有清晰的 Inkeys2 依据和换算说明，建议的 `35 / 50 / 70 DIP` 在 100% DPI 时对应旧版前两档的 35/50 画布宽度基准。
- [ ] AC5 Color Picker 打开/关闭呈现与其他 Popup 一致的克制回弹；固定字段锚点、数值右对齐和外侧 padding 在中英文文案下均稳定。
- [ ] AC6 Color Picker 只保留第三鼠标光，第一光仍不参与，其他 Popup 光源配置无改变。
- [ ] AC7 两个三角展开和收起均做反向 180 度动画；真实展开方向、快速反向和强制关闭时无跳角、360 度旋转或状态不同步。
- [ ] AC8 `git diff --check` 通过；使用 ARM64 host MSBuild 对 `InkeysRepo.sln` 执行 `Debug | ARM64` 构建；完成任务中的手工回归清单。

## Out Of Scope

- 启用或新增标注线/自由线业务模式、修改笔迹算法、改变 Slider 数值范围或重做颜色选择逻辑。
- 修改 SVG、shader、resource、传统 `IdtFloating` 源码、配置或第三光源架构。
- 与本任务无关的格式化、重构或性能改造。
