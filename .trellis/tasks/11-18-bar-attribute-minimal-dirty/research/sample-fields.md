# 采样字段表（V2a，2026-11-18）

实现：`Inkeys/Inkeys/UI/UI3/UI3.VisualDirtySampler.cppm`。原则：只采样「影响像素的有效绘制输入」；动态光源（主光/鼠标光的位置、颜色、强度）不采样，走原光影通道；对象自身的光响应参数（`frameLightPct`、材质、边缘光配置）要采样。所有对象额外采样当帧 `zoom`，并由宿主经 `VisualSamplerContext::extra` 追加绘制点选项与所在作用域的冻结环境。

## 可见性门槛（与 `UIRendering` 跳过绘制条件一致）

| 类型 | 可见条件 | Renderer 出处 |
|---|---|---|
| Shape | `enable` 且有 fill 或 frame，`w/h>0`，`pct>0` 或 `frameLightPct>0` | `UI3.Renderer.cpp` `UIRendering::Shape` 开头 |
| Superellipse | `enable` 且有 fill 或 frame，`w/h>0`，`pct>0` | `UIRendering::Superellipse` |
| SVG | `enable`，`w/h>0`，`pct>0`，`contentScale` 有限且 >0，`contentPct>0` | `UIRendering::Svg` |
| PNG | `enable`，`w/h>0`，`pct>0` | `UIRendering::Png` |
| Word | `enable`，`size>0`，`w/h>0`，`pct>0`，`contentPct>0` | `UIRendering::Word` |

修改 Renderer 门槛时必须同步 `IsVisuallyDrawn`。

## 字段

| 类型 | 采样字段 |
|---|---|
| 公共 | `inhX/inhY`（须已由准备阶段写成绘制位置）、`w.val/h.val`、`pct.val` |
| Shape | 公共 + `rw/rh/ft`（存在性与值）、`fill/frame`（存在性与值）、`framePct`、`frameLightPct`、`lightMaterial`、`themeSurface`、`lightFillOpacityScale`、`residentSurface`、`frameRendering`、`frameLightColor`、`frameLightOpacitySource`、`framePrimaryLightEnabled`、两种鼠标光强度倍率 |
| Superellipse | 公共 + `n/ft`、`fill/frame`、`framePct`、材质与边缘光配置（同 Shape，无 `frameLightPct` 字段） |
| SVG | 公共 + 内容版本 `svg.ValRevision()`、`angle`、`contentScale/contentPct`、`color1/color2`、`appearanceKey`（代表 documentPreprocess 输出） |
| PNG | 公共 + `contentRevision`（位图载荷替换递增）、`angle`、原始尺寸 `rW/rH` |
| Word | 公共 + `w.tar/h.tar`（字号拟合读取）、`size.val/size.tar`、`fontTargetSize`、字号修正当前值与目标值、内容版本 `content.ValRevision()`、`contentScale/contentPct`、`color` |
| Button | `zoom`、`pressScale`、`iconKind`、背景 Shape、icon SVG（PNG 时加 pngIcon）、主文字、可选副文字、角标输入（`progress/below/angle/scale/shown` 与角标 SVG 内容版本；角标几何在绘制时派生，采样输入而非派生字段） |
| 固定按钮（`ButtonVisualParts`） | `zoom`、`pressScale`、背景、可选 icon、可选主文字 |

## 包络

- 标准图元：`RenderingAttribute::GetWeigetRect`（描边 + 点光 6 DIP + 主题阴影 + 3 像素抗锯齿；SVG/PNG 计入旋转与 contentScale），再经宿主映射。
- 按钮：各可见部分包络并集 + 角标 `ResolveButtonBadgeBounds`；`pressScale>1` 时围绕包络中心按比例外扩（回弹峰值约 1.005，已被外扩覆盖，这里再保守一层）。

## 版本号（V2a 新增，`UI3.Primitive.cppm`）

- `UiStringClass::ValRevision()`：显示值每真正改变一次递增（构造、`Initialization`、`SetVal` 值不同、`ApplyTar` 值不同）。显示值只能经这些方法写入（无子类），版本可靠。A→B→A 也会递增，只会多刷，不会漏刷。
- `UiPNGClass::contentRevision`：`InitializationFromMemory` 成功替换载荷时递增（`InitializationFromResource` 经它实现）。

## 已知待 V2b 处理

- 字号修正在绘制中推进：当前以「当前值 + 目标值」采样，动画中持续判定变化、静止后收敛；V2b 前移解析后改为采样解析结果。
- 按钮继承位置、角标、PNG 字段复制在绘制时写入：V2b 前移后采样读到的才是本帧绘制值。
- 环境输入（常驻背景透明度覆盖、常驻内容透明度、idle fade、字体环境、DPI/unit mode）：由宿主在组采样里冻结（V3 起），采样器不直接读全局。
