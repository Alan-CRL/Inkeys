# 技术设计

## 1. 边界与现状

本任务沿现有产品 Draw3 瞬态光标链实施，不修改独立 `inkStrokeModelerTest` demo，也不恢复旧 Draw2/HCURSOR 路径。

主要入口：

- 配置 schema/校验：`Inkeys/Inkeys/Other/Other.Config.cppm:445`、`Inkeys/Inkeys/Other/Other.Config.cpp:663`。
- 产品快照：`Inkeys/IdtState.cpp:601`、`Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp:161`。
- 共享模型与打包：`Inkeys/Inkeys/Drawing/Draw3/Draw3.CursorSettings.h:30`、`:138`。
- 主/触摸 resolver：`Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:32`、`:313`、`:629`。
- CPU 上传：`Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp:329`。
- VS/PS：`Inkeys/Inkeys/Drawing/Draw3/Assets/inkVertexShader.hlsl:77`、`Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:352`。
- 设置草稿/事件与页面：`Inkeys/Inkeys/UI/Setting/Setting.Pages.h:270`、`Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:595`、`:671`、`Inkeys/Inkeys/UI/Setting/Setting.cpp:3839`。

## 2. 配置合同

在 `Drawing.Cursor` 增加四个 `NoUpload` 整数字段，磁盘单位为百分比：

| JSON 字段 | 合法值 | 默认 |
| --- | --- | --- |
| `PenOpacity` | 25..100 | 50 |
| `HighlightPenFillOpacity` | 25..100 | 50 |
| `DotOpacity` | 25..100 | 25 |
| `HighlightDotFillOpacity` | 25..100 | 50 |

- `ReadMini`/`ReadAll` 只接受可表示为 `int` 的 JSON int/uint 且值在 25..100；bool、浮点、字符串、null、超宽整数和越界值恢复该字段 schema 默认值。
- 缺省旧配置直接使用新默认值，不增加迁移键，不改旧 `MouseStyle/PenStyle` 或设备样式回落。
- 四项进入普通 Config 复制、冻结快照和写盘链，保持 `NoUpload`；未知配置键处理不变。

## 3. 共享值模型与发布

`CursorSettings` 在既有六个字段之后追加四个百分比字段，保持旧聚合初始化可继续使用追加字段默认值。共享 helper 提供：

- `DefaultCursorOpacityPercent(style)`：四种目标样式的默认值。
- `NormalizeCursorOpacityPercent(style, value)`：非法值回对应默认值。
- `CursorOpacityPercent(settings, style)`：选择当前样式的独立配置。

`PackCursorSettings`/`UnpackCursorSettings` 改用 `std::uint64_t`；现有 bit0..15 的含义和 `kCursorSettingsValidBit` 位置不变，四个 7-bit 百分比依次放 bit16..22、23..29、30..36、37..43。Controller 的 `cursorSettings_` 同步改为 `std::atomic<std::uint64_t>`，继续一次性发布完整值，避免四个滑块与设备样式混配。

数据流保持：

```text
Setting draft
  -> Inkeys::config.Drawing.Cursor
  -> deferred SyncDraw3State()
  -> Bridge::ProductState 完整 cursorSettings
  -> DrawingController::SetCursorSettings()
  -> uint64 原子快照 + RequestDrawingCursorRender()
  -> 主光标 resolver / 逐 Touch 工厂
```

触摸工厂接收同一份 `CursorSettings`（或由其解析出的百分比），旧调用可通过默认参数保持原行为。`productSettings == nullptr` 的独立 API、Arrow、Highlighter、Shape、Eraser、Laser 不消费这些新值。

## 4. 外观与透明度语义

在 `DrawingCursorAppearance` 增加默认 `0` 的 `outerOutlineWidth`，它使用画布物理像素；既有 `outlineWidth` 继续表示灰色/白色环。有效性检查、外观等价比较和 CPU 上传必须包含新字段，保证仅透明度或黑边变化也会产生新视觉和重绘。

`ApplyCursorStyle` 统一生成最终外观：

- 普通 Pen/Dot：`opacity = 配置百分比 / 100`、`fillAlpha = 1`、`outlineOpacity = opacity`，使灰边和填充最终 alpha 完全一致。
- HighlightPen/HighlightDot：`opacity = 1`、`fillAlpha = 配置百分比 / 100`、`outlineOpacity = 1`、`outerOutlineWidth = 1 px`，因此只有彩色填充受滑块影响。
- HighlightPen 白色环保持 `1.25 DIP × scale`；HighlightDot 白色环为 `clamp(实际圆点直径 / 8, 1.0 px, 1.25 DIP × scale)`。
- 黑边从原外轮廓向内占用 1 px，白环再向内排列；不扩大 width/height、center offset、quad 或 dirty 外框。

## 5. CPU/GPU 合同

不新增 shape、寄存器、buffer 或 constant-buffer 字段，复用 Cursor 第二个 `InkPoint` 目前未使用的 `time`：

```text
InkData[1].pos.x = outlineWidth
InkData[1].pos.y = fillAlpha
InkData[1].r     = outlineOpacity + 1（现有合同）
InkData[1].time  = outerOutlineWidth（新增）
VS uv.x          = outlineOpacity + 1（现有）
VS uv.y          = outerOutlineWidth（新增）
```

PS 对同一 signed distance 分解三层 coverage：

1. 原外轮廓到内缩 `outerOutlineWidth`：纯黑，alpha 1。
2. 继续内缩 `outlineWidth`：现有灰色/白色环，使用既有 edge opacity；高亮时该值为 1。
3. 其余内部：当前 RGB，使用 `fillAlpha × opacity`。

各层 coverage 非负且不超过外层，最终仍只输出一次 premultiplied `Add/Retain`。`outerOutlineWidth=0` 时必须还原当前公式，普通光标、Arrow、橡皮、荧光笔矩形和其他 shape 不受影响。极细圆的内部 coverage 允许收缩为空，不产生反向或 NaN 几何。

正式 `inkPixelShader.hlsl` 修改后，使用归档任务现有生成器重新生成 `Draw3.Win7GuardedShader.h`；保留 type21/22 的正向 guard、DefaultInk 数学、未知 type 输出和签名检查。VS/PS 的 CPU 字段上传、shader 源、生成头和像素验证必须作为同一变更审查。

## 6. 设置页与主题

`CursorPageDraft` 增加四个 `[0.25,1.0]` 浮点值；`CursorPageEvents` 增加对应的 changed/active/reset 状态。页面使用现有 `SettingRow + SliderResetAction`，显示整数百分比并复用既有“恢复默认”图标及 tooltip，不新增另一套滑块布局。每次实际编辑后将值量化为 `round(value × 100) / 100`，确保配置和预览都严格按 1% 步进。

- 新 SectionHeader 和四行放在数位板 Notice 之后、FineControl 之前。
- 每次实际编辑立即写对应 Config 字段并 `SyncDraw3State()`；以 save-pending 状态在所有滑块均未 active 时 `QueueConfigWrite()`，重置立即保存，离开光标页时 flush 未保存值。
- `DrawCursorPreview` 接收当前样式和对应透明度，复用共享默认值/环宽 helper；普通样式给灰边和内容同一 alpha，高亮只给填充 alpha。
- 浅色背景使用 `Design::Card`（当前纯白）并配合 `Stroke`；深色继续用现有固定背景和边界，不改变其他预览。

## 7. i18n

在 `SettingsUI.Draw.Cursor` 下新增分组和四个标题键，先修改 `Inkeys/src/i18n/zh-CN.jsonc`，再运行 `Scripts/i18n.ps1 sync`，补齐全部二十语言的真实翻译并运行 `check`。`IdtI18nKeys.g.h` 和 snapshot 只由脚本更新。重置按钮 tooltip 复用现有“恢复默认”键。

## 8. 兼容、风险与回滚

- 新字段缺省会有意提高三种样式的视觉不透明度；没有其他配置迁移或双写。
- 旧调用和专用工具保持原行为；普通样式的默认 Pen 变为 50%，Dot 保持 25%。
- 无窗口测试和 WARP 离屏读回不能替代真实桌面、多设备及 Win7 视觉验收；未获 GUI/实机授权时明确保留这些验证项。
- 回滚以配置字段、共享打包、外观/CPU-GPU 协议、设置 UI/i18n 为一组；新 JSON 字段在旧版本中无消费者，不需要降级迁移。
