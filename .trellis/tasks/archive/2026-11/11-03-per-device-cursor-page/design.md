# Design: 按设备光标模型与专页

## 现状(源码已确认)

- `Draw3.CursorSettings.h`:`MouseCursorStyle{System,Drawing}`、`PenCursorStyle{Circle,PenIcon}`、`CursorSettings{mouseStyle,penStyle}`。
- 配置 `Drawing.Cursor.{MouseStyle,PenStyle}`(`Other.Config.cpp` 约 644 行校验、880 行清理旧键);`IdtState.cpp:517` 读入 `state.cursorSettings`;`Setting.cpp:3683` 两个 `ComboRow`。
- `DrawingController` 把设置打包为 `std::atomic<uint32_t> cursorSettings_`(bit0 mouse、bit1 pen、bit2 valid);`Draw3.Bridge.h::ResolveCursorSettingsForTool` 做工具投影。
- `Draw3.PenCursor.cpp::ResolvePrimaryDrawingCursorVisual`:`highlight=!screenPen`;`penIconAllowed` 门控画笔图标;Touch authority 返回无样本(即无自绘触摸光标)。
- 设置页子页导航:`Setting.Pages.h` `DrawingSubpage{Overview,SpeedEraser,PalmEraser}` + `NavigateDrawingSubpage`,入口 `RenderDrawingEraserEntries`。

## 数据模型

```cpp
enum class DeviceCursorStyle : uint8_t { Default = 0, PenIcon = 1, Dot = 2, HighlightDot = 3, None = 4 };
// Default: Mouse→System, ScreenPen/Tablet/Touch→Circle。None 仅 Touch 合法。
enum class PenHighlightMode : uint8_t { Off = 0, DigitizerOnly = 1, Always = 2 };
struct CursorSettings {
    DeviceCursorStyle mouse = Default, touch = None, screenPen = Default, tablet = Default;
    PenHighlightMode penHighlight = DigitizerOnly;
};
```

- 取值 0/1 与旧 `MouseStyle/PenStyle` 同义,旧配置无需转换即可被新枚举读取。
- `Restore*(int)`:按设备校验合法范围(非 Touch 拒绝 4),非法→该设备默认。

## 配置

`Drawing.Cursor` 下新增 `TouchStyle/ScreenPenStyle/TabletStyle/PenHighlightMode`(均 `NoUpload` 整数)。读取规则:

- `MouseStyle`:原键沿用。
- `ScreenPenStyle/TabletStyle` 缺失时回落到旧 `PenStyle`(一次性迁移语义:首次写盘后写出新键;旧 `PenStyle` 键保留不再写,或继续写为屏幕笔值——实现时选更少歧义者并在 spec 写明)。
- `TouchStyle` 缺失=None(4);`PenHighlightMode` 缺失=1。
- 校验沿用 `Other.Config.cpp` 的“JSON 整数且范围内”模式,拒绝 bool/float/超宽。

## 渲染解析(Controller / PenCursor)

1. 打包:32 位 `cursorSettings_` 重排为 `valid(1) | mouse(3) | touch(3) | screenPen(3) | tablet(3) | highlight(2)`,足够容纳,仍单次原子交换;Window 侧 `mouseUsesSystemCursor` 投影不变(`mouse==Default`)。
2. 样本→设备行:Mouse 样本→mouse;Pen 样本按 `SourceKind`:IntegratedPen→screenPen;ExternalPen→tablet;Unknown/Conflict→tablet(沿用“固定高亮”现状);Touch→touch。
3. 外观:
   - Default 对笔设备=现有圆形(随笔宽);Default 对鼠标=系统光标(不自绘,保持现状)。
   - PenIcon:沿用现有几何/热点。
   - Dot/HighlightDot:固定小尺寸(初值约 8 DIP×DPI,待预览定),复用 Circle 着色路径,仅替换直径与描边;不进入 `CanvasDiameterForTool`;高亮变体直接复用现有高亮描边常量(1.25 DIP×DPI、0.96 灰白,与荧光笔既有高亮一致),不新增颜色/线宽。
   - 高亮描边判定(圆形/PenIcon,Pen 样本):`Off→false`;`Always→true`;`DigitizerOnly→!(screenPen)`(现状)。Mouse 自绘光标保持始终高亮。
4. 工具投影:`ResolveCursorSettingsForTool` 保持“Pen/HardPen 应用设备样式;Highlighter 矩形、形状、橡皮、激光走既有策略”。设备样式不得覆盖荧光笔矩形与形状圆形。
5. 触摸:新增逐接触的 Touch 光标可视。无 hover,仅 contact 期间在每根接触手指的位置各画一个所选样式(类比现有逐 Touch runtime 的橡皮圆环生命周期:Down 生成、Up/Cancel 清除)。仅 Pen/HardPen 软硬笔应用;荧光笔、激光、形状、橡皮的触摸行为保持原样(荧光笔触摸无光标)。需要先读 `input-and-ink.md` 中 Touch owner/suppress 与逐 runtime 圆环合同,新增视觉不得破坏“系统触摸箭头始终隐藏”“Touch 终态不恢复旧 Hover”。
6. 荧光笔/激光笔:`ResolveMouseCursorStyleForTool` 与 Laser 解析路径不接入设备样式;回归测试断言四设备×全部样式下荧光笔/激光的可视结果与改动前逐项一致。

## 设置页

- `DrawingSubpage` 增 `Cursor`;`Overview` 原“光标”区替换为一个入口条目(`RenderDrawingCursorEntries`,仿 `RenderDrawingEraserEntries`),返回处理与橡皮子页一致。
- 专页布局:
  - 区段“按设备”:4 个 `ComboRow`,条目右侧(下拉左侧)内联预览块(约 32×32 DIP),用 ImGui draw list 复刻 shader 外观(圆形环、PenIcon 位图/路径、点)。预览受高亮模式影响;预览是近似,不替代实际绘制验收。
  - 区段“笔设备”:高亮模式 `ComboRow`(关闭/仅数位板/始终)。
- 选项列表按设备生成(触摸多“无光标”),下拉索引与枚举值用显式映射表,不直接把索引当枚举(参照预测模式处理)。
- 数据流:`SetXxx` 写 `Inkeys::config.Drawing.Cursor`→`SyncDraw3State()`→`QueueConfigWrite()`;预览读取同一份配置,不触碰 Controller。
- i18n:`SettingsUI.Draw.Cursor` 增 Page/Device 名/样式名/高亮模式名/说明;运行 i18n sync/check 并更新 `IdtI18nKeys.g.h`。

## 兼容、回滚

- 回滚点:枚举与配置迁移独立于 UI;若专页有问题可保留模型而临时恢复两下拉(`PenStyle` 同时映射 screenPen/tablet)。
- 旧版本读取新配置:未知键被保留;`MouseStyle`/(旧)`PenStyle` 取值范围与旧版一致,不会致旧版崩溃。
- 与子任务 A:仅通过 `SourceKind`→设备行映射函数耦合;A 若改分类,B 不需改模型。

## 风险

- 触摸自绘光标引入新的 Touch 视觉归属,与 `input-and-ink.md` 的 Touch suppress/owner 合同相交,需先读 spec 并补测试。
- Win7 guarded shader:仅复用既有 Circle/PenIcon 着色类型,不新增 GPU type;若小圆点需要新 type,须同步 Win7 guarded 数组(`draw3-cursors.md` 要求)。
- 预览与实际外观漂移:以同一组参数常量驱动二者。
