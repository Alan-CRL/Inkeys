# 技术设计：擦除形状、微光光标与样式设置

位置以基线 babdecf6 为准，实施前复核。路径简写：Draw3 = `Inkeys/Inkeys/Drawing/Draw3/`。

## 1. 形状与尺寸

```text
enum class EraserShape : uint8_t { Circle = 0, Rectangle = 1, Square = 2 };
r = D/2（逐点标量，保持现有 realPoints.r / 持久 width = 2r 语义不变）
Circle    : 半径 r
Rectangle : 半宽 = r·√(π/6) ≈ 0.72360r，半高 = 1.5×半宽 ≈ 1.08540r   （面积 = πr²，宽:高 = 2:3）
Square    : 半边 = r·√π/2 ≈ 0.88623r
```

- 形状是**笔划级**属性（Down 时锁存），逐点仍只存标量 r，笔速/手掌变尺寸照常工作。
- 圆角：光标/预览固定 `kEraserCursorCornerPx = 3`（最终像素，实施时可微调但须为常量 px），`min(3, 0.5×短边)`；擦除几何无圆角。

## 2. 擦除几何与着色器（R1、R3、R4）

- 现状：橡皮重放固定 `RoundCapsule + Erase`（Draw3.StrokeGeometry.cpp:1125），PS 变半径胶囊距离 + `fwidth×1.5 + smoothstep` AA，擦除输出 `Add=0, Retain=1−coverage`（inkPixelShader.hlsl:589）。荧光笔已有轴对齐矩形扫掠（StrokeGeometry:585、inkVertexShader:98、inkPixelShader:72），但每段只有一组宽高（两端平均）。
- 新增擦除段类型 `EraseRectSweep`：两端为轴对齐矩形，半尺寸分别 (ax0, ay0)、(ax1, ay1)。**线性插值的轴对齐矩形沿线段的扫掠恰为两端矩形的凸包**（B(t) = (1−t)B0 + tB1，凸组合点必落在某个 B(t) 内），因此 PS 只需计算“点到两矩形凸包（≤6 边凸多边形）”的有符号距离；VS 包围盒取两端矩形并集外扩 2px（沿用荧光笔做法）。单点/零长段退化为单个矩形。
- 不复用荧光笔的“两端平均尺寸”，以保证变尺寸时擦除边界精确。
- AA 与圆形相同的覆盖函数；Erase 输出不变。
- 实时 dirty、历史瓦片 bounds：用两端矩形并集（含 AA 外扩）替代 `±r` 圆界。
- Win7：正式 VS/PS 经 vcxproj 编译 SM5.0 CSO；Win7 WARP 使用 `Draw3.Win7GuardedShader.h`（生成器见 `.trellis/tasks/archive/2026-10/10-18-draw3-cursor-policy/research/Generate-Win7-GuardedShader.ps1`），新增类型须同步门限、CSO 与兼容字节码。

## 3. 存储（R5）

- 持久点保持 `x/y/width`；样式新增 `eraserShape`，仅橡皮笔划写出，缺省 Circle。
- 写入位置：UInk **私有/扩展字段**，要求“当前已发布版本读取时忽略、仍识别为橡皮笔划并按圆形重放”。调查已知：UInk 只接受版本 10；未知字段可忽略；新增 inkType 会被降级为普通笔（禁用）；`ink.extra` 被当前 Draw3 导入拒绝（`inkStrokeModelerTest/draw3/uink_codec.cpp:837/1405`、`uink_draw3_import.cpp:35`）。**实施第一步须确认一个满足上述要求的私有字段位置**（用当前发布版的解析代码写用例验证）；找不到时回到规划，不得选用会被旧版拒读或误判为墨迹的位置。
- 白板历史 JSON（DrawingController.cpp:582，点严格三项）：在笔划/样式层加可选字段，点结构不变。
- 撤销/重做：历史瓦片 bounds 使用形状 bounds。
- 记录：UInk 1.1 将 `eraserShape` 升为官方字段（写入 spec 的 UInk 章节与代码注释）。

## 4. 微光光标（R6，设计已确认）

S = 形状短边（圆为直径，矩形为宽，正方形为边长），中心 c。

- 主星：中心 c，外半径 `Ra = 0.14·S`；形状为指数 0.5 的超椭圆（四角凹星“✦”）：`f(p) = sqrt(|x|/Ra) + sqrt(|y|/Ra) − 1`，距离近似 `f/|∇f|`（轴向处梯度加 ε 防奇异）。
- 小星：中心 `c + (0.22·S, −0.22·S)`（右上），`Rb = 0.06·S`，同形状。
- 显隐：S < 20px 都不画；20 ≤ S < 40 仅主星；S ≥ 40 主星 + 小星。
- 颜色/透明度：沿用旧抓手的着色方式（向 outline 颜色混合，`ERASER_GRIP_OPACITY` 与 Hover/接触透明度逻辑不变），覆盖率与内填充相交，不越过内描边。
- 共享参数：`Assets/EraserGripVisual.h` 中以 `ERASER_SPARKLE_*` 替换 `ERASER_GRIP_STRIPE_*`（C++ 预览与 HLSL 共用）；Bar 预览用同参数生成 D2D 路径（超椭圆采样多边形）。
- 光标外形：圆形保持现 type 6 圆路径；矩形/正方形使用圆角矩形距离（圆角 px 常量），外黑边/内灰边宽度沿用 `ERASER_GRIP_OUTLINE_RATIO`（以短边为基准）。
- 光标入口：鼠标/笔主光标与逐 Touch 光标（DrawingController.cpp:14080、14170）都需带形状与半宽高。

## 5. 设置与配置（R7、R9）

- 配置：`Drawing.Eraser.Shape`（枚举，NoUpload，默认 Circle），按现有枚举字段方式严格校验原始整数与宽度（Other.Config.cppm:469、Other.Config.cpp:209/642），非法回落默认。
- 传递：沿普通橡皮现有路径 偏好 mutex → `ProductState.eraserInputs` → Host → Window 配置 → 接触快照（IdtState.cpp:603、Draw3.WindowControl.cpp:728），Down 锁存。
- UI：新复合控件 `EraserStylePicker`：一行三张样式预览卡（选中高亮、可点击）+ 下方内联下拉（与卡片双向同步）；预览用同一形状/微光绘制函数。放在设置-绘制-橡皮分组最上方（Setting.cpp:3938 附近，笔速入口之前）。控件接口支持附加“跟随默认”选项与按样式渲染，供 custom 子任务复用。
- i18n：zh-CN 为基准，`Scripts/i18n.ps1 sync` 生成其余语言并补译、check；不手改 `IdtI18nKeys.g.h`。

## 6. 主栏预览（R8）

- `Bar.EraserAttribute.cpp:379` D2D 画圆+抓手 → 按形状画圆/圆角矩形 + 微光；`Bar.EraserAttributeLayout.cppm:100` 的布局、命中、选中环、焦点改用形状半宽高；圆角固定设备 px（不除 UI 倍率）。

## 7. 风险与回退

- 着色器/字节码是最高风险项：先做数值与 Headless 回归，再 Win7 字节码同步。
- 旧版近似重放：矩形在旧版按等面积圆擦除，结果近似（已接受）。
- 回退：形状枚举默认 Circle；关闭入口即恢复原行为；存储字段可选，回退后新文件中的矩形按圆形读取。
