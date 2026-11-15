# 编辑框与浮条样式：视觉规格

> 主 Agent（UI/UX 负责人）在 2026-11-08 定稿。需求见 `prd.md`，现状见 `../11-05-shape-ux-rework/research/edit-visual-and-floating-bar.md`。本文所有尺寸单位为 DIP，实现时乘 `scale`（显示缩放）。本文没有写到的视觉行为保持原样，不要自行“改进”。

## 1. 绘制层次（同一 SelectionOverlay 窗口，自下而上）

1. 对象外框
2. 旋转连接线
3. 控制点（外框角点 → 专属点 → 中心点）与旋转钮
4. 浮条
5. 第 1 层弹层（色板层 / 边数层）
6. 第 2 层弹层（取色器层）

输入按相反顺序命中：最上层的弹层先命中；点击弹层外部只关闭最上一层，不穿透给下层（这一次点击被消费）。Esc 同理逐层关闭，全部关闭后再按 Esc 才结束编辑。

## 2. 对象外框

| 项 | 值 |
|---|---|
| 几何 | 共享核心给出的 `frameRect`（本地坐标；含线宽的包围盒外扩 8 DIP）。UI 不再自己算框 |
| 形状 | 圆角矩形，圆角半径 6 DIP，线宽 2 DIP |
| 旋转 | 随对象 transform 旋转：用 D2D 变换矩阵（平移 + 旋转，不含对象缩放）绘制同一个圆角矩形，保证线宽与圆角在任何角度下都不变形 |
| 颜色 | `Accent`（深色 `RGB(88,255,236)`，浅色 `#006A63`），不透明度 0.90 × 内容透明度 × 入场进度 |
| 退化 | `frameRect` 宽或高小于 1 DIP 时（例如水平直线），按 1 DIP 处理，仍画成细长圆角框 |
| 拖动中 | 外框跟随本地预测的候选对象（本地跟手），不等绘制线程快照 |

## 3. 控制点（Thumb）

抽出共享绘制 helper（设计 §4 已定接口）：

```
struct BarSliderThumbStyle { centerPx; outerDiameterPx; centerDiameterPx; accentOpacity;
    lightWeight; opacity; frameThicknessPx; surfaceColor; /* 主栏所需的其余输入按旧公式补齐 */ };
DrawBarSliderThumb(renderer, dc, const BarSliderThumbStyle&, iconSvg = nullptr, iconScale = 1)
```

（2026-11-09 修订：接口改为参数结构体，补上主栏旧公式依赖的描边缩放和表面色，保证主栏迁移像素一致。）

- 主栏粗细滑块改用该 helper，像素必须与现在一致（回归测试保护）。
- 外圈填充、外圈 1 DIP 描边、内芯颜色公式与主栏 Thumb 完全一致（research §3.3），按 `lightWeight` 在深浅端点之间连续混合。

| 状态 | 外径 | 内芯直径 | 强调色强度 |
|---|---|---|---|
| 正常 | 20 | 12 | 1.00 |
| 悬停 | 20 | 14 | 0.90 |
| 按下 / 拖动中 | 20 | 10 | 0.80 |

- 状态过渡 0.12 s，EaseOutCubic；外圈不缩放。
- **中心类控制点**（圆心、椭圆中心、正多边形中心、圆弧/扇形圆心，即 G2 的“整体移动”点）使用同一外圈，内芯缩小为 正常 8 / 悬停 10 / 按下 6，以区分“改形状”和“整体移动”。其他所有控制点（端点、顶点、半径点、焦点、外框角点、立体关键点）都用上表。
- 入场：opacity 0→1、scale 0.75→1，0.28 s（与主栏 Thumb 相同曲线）；离场 0.16 s 反向。
- 命中半径不小于 22 DIP（44 DIP 直径），视觉外径与命中区无关。
- 外框角缩放点（只有立体图形有）**不用 Thumb**，画成沿外框圆角的 L 形角标，视觉上属于外框：线宽 4 DIP、圆头，沿外框圆角弧，并从弧两端各沿边再延伸 12 DIP，颜色 `Accent`，不透明度与外框相同。悬停时线宽 5 DIP，按下/拖动时强度 0.80；过渡 0.12 s EaseOutCubic。原因：立体图形的正面角点离外框角只有约 11 DIP，两个 Thumb 会叠在一起（2026-11-09 截图验收）。
- 命中：旋转钮优先；专属点与外框角标之间**取距离最近者**（相等时取专属点）；最后才是本体。这样两者在重叠区域都能点到。
- 44 DIP 输入热区的填充必须在任何主题、透明度下都肉眼不可见（最终 alpha ≤ 1/255，不受投影或内容透明度放大）。

## 4. 旋转钮

| 项 | 值 |
|---|---|
| 位置 | 圆心在 `frameRect` 顶边中点沿对象本地 −y 方向外移 24 DIP（共享核心计算，UI 只消费） |
| 外圈 | 与 Thumb 相同的外圈填充和描边，外径 28 DIP |
| 内芯 | 不画实心内芯；画 `barShapeRotate` 图标，16 DIP，颜色为 `MixBarUiColor(外圈填充, Accent, accentOpacity)` |
| 状态 | 正常：图标缩放 1.00、强度 1.00；悬停：1.08、0.90；按下/拖动：0.92、0.80；过渡 0.12 s EaseOutCubic |
| 连接线 | 从外框顶边中点到旋转钮外圈下沿，线宽 1.5 DIP，`Accent`，不透明度与外框相同，圆头 |
| 入场 | 与控制点相同 |
| 命中 | 半径不小于 22 DIP |

图标随对象一起旋转（拖动时可看出当前角度）。

## 5. 浮条

### 5.1 结构

单行 `BarButtonListClass`（SingleRow），全部走主栏 Configure/Retarget/Advance/Draw。成员顺序：

```
[颜色入口] │ [描边 | 填充 | 描边+填充] │ [实线 | 虚线 | 点线 | 点划线] │ [粗细 ×3] │ [边数入口] │ [辅助开关 …]
```

- 分隔线用主栏 SingleRow Divider 模板：宽 1、高 `OneSide − 4`、`Divider` 色、pct 0.30、`frameLightColor=Frame`、`framePrimaryLightEnabled=false`、`frameCursorLightIntensityScale=BarUiDividerCursorLightIntensity`，与 GeometryAttribute 面板分隔项的参数一致。
- 某组整组隐藏时，它前面的分隔线也隐藏（不出现相邻两条分隔线或首尾分隔线）。
- 显隐规则沿用现有逻辑（`showStyle`、`showStroke`、`regularSides`）；辅助开关按图形的辅助项清单出现（part B 提供数据）。
- 按钮尺寸 OneOne（32.5），背景、选中态、悬停、按压与主栏 GeometryAttribute 面板按钮完全相同。
- 宽度超过工作区时不再手动换行：改为整体按 `FitEraserSurfacePose` 等比缩小（最小 0.75），仍放不下才换成两行（第二行从“粗细”组开始）。两行在 0.75 下仍放不下时，继续等比缩小直到放进工作区（浮条永不越出工作区）。

### 5.2 颜色入口

- OneOne 按钮内画 20×20、圆角 4 的当前颜色色块，1 DIP `SwatchFrame` 描边（与主栏色块描边相同的透明度）。
- 色板层打开时，入口按钮处于 Selected 态。

### 5.3 边数入口（只有正多边形、棱柱、棱锥）

- OneOne 按钮内用 `BarUiWordClass` 显示当前边数（13 Bold，TextPrimary）。
- 点击后打开第 1 层“边数层”（替代原来浮条下方的展开区）：5 列 × 2 行的 OneOne 数字按钮（3–12），面板内边距 5、间距 5；当前值 Selected。

### 5.4 材质与透明度

- 浮条表面：`residentSurface=true`、`themeSurface=true`，fill/frame 写 Dark 端点，`lightMaterial = GetThemeLightWeight()` 每帧写入；pct 为 `BarMainBarFillOpacity`（与主栏相同；常驻背景透明度由渲染器按 `residentSurface` 统一解析，不能再在 pct 里乘一次）；圆角 8；投影与边缘光按主栏常驻表面规则。
- 浮条内容（图标、文字、色块、分隔线）：绘制前 `SetResidentContentOpacity(GetThemeResidentContentOpacity())`，绘制后恢复 1。
- 外框、连接线、控制点、旋转钮：不透明度乘 `GetThemeResidentContentOpacity()`。
- 记住已应用的背景/内容透明度和 lightWeight，变化时整窗重画；`SetResidentOpacity` 与主题切换时 `RenderPipeline::Request(Client::SelectionOverlay)`。

### 5.5 位置

- 默认放在对象屏幕包围盒（含旋转钮与控制点的外沿）下方 16 DIP，水平居中于对象。
- 下方放不下时放到上方；左右按工作区留 5 DIP 边距钳制；与主栏矩形相交时移到主栏外侧（沿用现有规则）。
- 拖动对象期间浮条位置不跟随（保持手势开始时的位置），手势结束后用 0.4 s EaseOutCubic 移到新位置，避免拖动时浮条抖动、遮挡手指。

### 5.6 出现与消失

- 进入新的编辑会话：重置整个布局，所有成员、分隔线位置 SetDirect 到目标值；然后整体用 `BarAttributePanelMotion` 从锚点（对象 `frameRect` 底边中点的屏幕位置）紧凑态（宽 60）以 0.4 s EaseOutBack 展开，opacity EaseOutSine。
- 结束会话：0.24 s EaseInBack 收回锚点，opacity EaseInSine；结束后清空快照和布局。
- 会话中成员显隐（例如切换填充后线型组出现/消失）：0.4 s 批次 + CanJoin；隐藏成员的位置锚定到前一个可见成员（没有则后一个），pct 淡出；工具条宽度用同一批次时长，不按距离计速。
- 工具条不再用随宽度动画的 clip 裁剪内容（消除“从左往右加载”）；只在退场成员需要时裁剪到最终宽度与当前宽度中的较大者。

## 6. 颜色层级

### 6.1 第 1 层：色板层

- 面板：圆角 8，`Surface`/`SurfaceFrame`，`themeSurface=true` + `lightMaterial`，pct 固定 `BarDrawAttributeSurfaceOpacity`（0.95），不受常驻透明度影响。
- 内容：与主栏绘制属性色块完全相同的视觉——30×30、圆角 4 的 11 个预设色 + 第 12 个自定义入口（30 直径色轮 PNG + 20 直径当前色内芯）；6 列 × 2 行，列距 35，内边距 5（面板 215 × 75）。选中色叠加 `colorSelect` 15×15 徽标；悬停/按压与主栏色块相同（点光、按压缩放）。
- 选中预设色：写入当前对象（`ShapeUiAction::SetStyle`，一条编辑历史），色板层保持打开，便于连续比较。
- 位置：在浮条远离对象的一侧（浮条在对象下方时，色板层在浮条下方），间距 8 DIP；水平方向以颜色入口中心对齐后在工作区内钳制。放不下时换到另一侧；仍放不下按 `FitEraserSurfacePose` 缩放/平移进工作区。
- 动画：`BarAttributePanelMotion` 从颜色入口中心紧凑态展开 0.4 s EaseOutBack；关闭 0.24 s EaseInBack。

### 6.2 第 2 层：取色器层

- 几何、内容、交互逐项复用主栏取色器：300×209、圆角 8；顶部 Tone（`colorSun`/`colorMoon`）与关闭按钮 30×30；调色板 290×132 位于 (5,40)；标记点、颜色预览、Hold、RGB 数值等**以主栏取色器的现有绘制代码为准**（`Bar.RenderLoop.cpp` 约 11515 起），参数相同，不另行设计。
- 颜色计算用 `GetBarColorPickerColor` / `ProjectBarColorPickerColor`；Tone 状态为编辑器本地状态，不改主栏的 `colorPickerDarkTone`。
- 拖动调色板时只更新本地反馈（标记点、预览色块、浮条颜色入口），松手时提交一条编辑历史（2026-11-09 修订：Draw3 暂无样式预览事务，对象实时预览留作后续）。
- 位置（2026-11-09 修订）：按顺序尝试，取第一个能完整放进工作区的位置，任何位置都**不得与浮条或色板层重叠**：
  1. 色板层外侧（同 6.1 方向），间距 8 DIP，水平以自定义入口对齐后钳制；
  2. 色板层右侧，顶边与色板层顶边对齐，间距 8 DIP；
  3. 色板层左侧，同上；
  4. 浮条的另一侧（对象一侧），间距 8 DIP；
  5. 以上都放不下：取第 1 个位置，按 `FitEraserSurfacePose` 缩放/平移进工作区。
- 材质与透明度同色板层（0.95）。

## 7. 深浅主题

- 所有表面（浮条、两个弹层）走 `themeSurface` + Dark 端点 + `lightMaterial = GetThemeLightWeight()`，投影和边缘光由材质系统解析。
- Thumb 与外框颜色按 `lightWeight` 连续混合（见 §3）。
- 离屏截图矩阵：深/浅 × 1.0/1.5/2.0 × {矩形（含旋转钮）、圆（含中心点）、长方体（含外框角点）、色板层打开、取色器层打开}。

## 8. 按键（R8）

- Esc：只在 Inkeys 前台、或浮条可见且光标在编辑器窗口内时处理；逐层关闭（§1）。
- 方向键、Space、Enter、Tab：只在浮条内容拥有键盘焦点时截获；否则放行（PPT 放映翻页不受影响）。
