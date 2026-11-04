# Design — 统一主栏与组件 SVG 图标设计语言

## 1. 现有机制（不改动）

- SVG 以 `UI` 类型资源登记在 `Inkeys/Inkeys.rc`（名称大写，代码中以 `L"barXxx"` 大小写不敏感加载），工程 `Inkeys.vcxproj` 以 `<None Include>` 跟踪源文件。
- `BarUiSVGClass` 加载时字符串替换：`rgba(10,0,7,0)` → color1，`rgba(9,0,2,0)` → color2（`Bar.UI.cpp:500-545`）；未提供颜色时占位保持全透明。
- 按钮 color1 由 `RetargetBarFixedButtonVisual` 在 IconPrimary / Accent 间切换；状态内容切换走 `BarButtonClass::TransitionContent` → `TransitionToResource`（仅 `iconKind == Svg`）。
- 渲染使用 D2D SVG 文档：支持 path/rect/circle/fill-rule/stroke，不支持 `<text>`。

## 2. 设计语言规范

| 项 | 规则 |
|---|---|
| 画布 | `width="24" height="24" viewBox="0 0 24 24" fill="none"` |
| 活动区 | v2：满幅 0.5–23.5（与画笔/橡皮/选择同高；Bar 按高度缩放，v1 的 2–22 显小显轻）。组件图标由 v1 图形等比放大到最长边 23 |
| 着色 | 仅 `fill="rgba(10,0,7,0)"`；不使用 `stroke`，线性元素在设计源中按统一线宽缓冲为填充 |
| 主体 | 实心剪影，内部细节用 `fill-rule="evenodd"` 镂空表达 |
| 镂空间隙 | 最小 1.5，常用 1.5–2；保证 14 DIP 下仍可辨 |
| 实心最小宽 | 2（细部件如屏幕支架、笔尖） |
| 统一线宽 | 线性元素（关闭 X、箭头、加号、菜单线、雪花臂等）宽 2.5、圆端点圆连接；镂空细线 1.75–2.25 |
| 圆角 | 外轮廓矩形圆角 3（≥16 宽）或 2（较小部件）；镂空内角 1 |
| 角标/徽记 | 位于右下 13–23 区域，主体在交叠处镂出 1.5 间隙环 |
| 禁止 | `#`/`rgb()` 颜色、color2、`opacity`/`fill-opacity`、渐变、`clip-path`、`transform`、`<text>`、`id`、注释残留、编辑器元数据 |
| 文件 | UTF-8 无 BOM，单行或少量缩进均可；坐标保留 ≤2 位小数 |

> 选择/画笔/橡皮等基准图标的 viewBox 不统一（16×23、19×20…），本任务不改它们；新图标统一 24 画布，并以基准图标在 28 DIP 下的视觉重量校准。

## 3. 图标清单与资源命名

命名沿用 `bar*` 前缀；组件用 `comp*` 前缀。新资源登记在 rc 的 `UI` 段并加入 vcxproj/filters。

### 3.1 主栏重绘（原资源名，原地替换文件内容）

| 资源 | 语义 | 构思 |
|---|---|---|
| `barWhiteboard` | 打开白板 | 实心白板面板 + 支架，面板内镂空一支斜笔 |
| `barEndShow` | 结束放映 | 实心投影屏幕 + 支架，屏幕内镂空停止方块 |
| `barFreeze` | 冻结画面 | 六臂实心雪花（粗臂 + 短分叉），去掉细碎描边 |
| `barPierce` | 鼠标穿透 | 实心指针穿过半层叠板（板在指针处断开） |
| `barGeometry` | 几何 | v2 恢复原版文件（26×24），不由设计源生成 |

### 3.2 主栏状态新图标

| 新资源 | 语义 | 替换的调用点 | 构思 |
|---|---|---|---|
| `barReturnShow` | 返回放映 | `Bar.Button.cpp:1215` 附近原 `barDismiss` | 实心投影屏幕，屏幕内镂空播放三角 |
| `barResumeWhiteboard` | 回到白板 | 同处「To board」分支原 `barWhiteboard` | 白板面板 + 右下向上展开角标 |
| `barMove` | 移动（白板） | `Bar.Button.cpp:1058` 附近 | v2：实心手形，外轮廓参照开发者给定线稿（中指最高、小指最短、拇指下沿弧线扫到圆掌底），指缝镂空 |

### 3.3 组件（16 个，新资源）

| 组件 id | 新资源 | 构思 |
|---|---|---|
| Appliance.Explorer | `compExplorer` | 实心文件夹 |
| Appliance.Taskmgr | `compTaskManager` | 窗口 + 镂空心电/性能折线 |
| Appliance.Control | `compControlPanel` | 三条滑杆（实心轨道 + 圆钮镂空环） |
| System.Desktop | `compShowDesktop` | 显示器 + 向下最小化箭头镂空 |
| System.LockWorkStation | `compLockScreen` | 实心挂锁，锁体镂空锁孔 |
| Keyboard.Keyboardesc | `compEscape` | 键帽 + 镂空左上外出箭头 |
| Keyboard.KeyboardAltF4 | `compAltF4` | 窗口 + 标题栏镂空关闭 X |
| RollCall.IslandCaller1 | `compRollCallRandom` | 单人剪影 + 骰子角标 |
| RollCall.IslandCaller2 | `compRollCallSingle` | 单人剪影 + 靶心/选中角标 |
| RollCall.SecRandom1 | `compRollCallQuick` | 单人剪影 + 闪电角标 |
| RollCall.SecRandom2 | `compRollCallQuick2` | 双人剪影 + 闪电角标 |
| RollCall.SecRandom2Compat | `compRollCallQuickCompat` | 双人剪影 + 闪电角标外加兼容（链接）小标，与上一个区分 |
| RollCall.NamePicker | `compNamePicker` | v2：单人剪影 + 姓名牌角标（镂空两行名字线） |
| Linkage.ClassislandSettings | `compClassSettings` | 课表网格 + 齿轮角标 |
| Linkage.ClassislandProfile | `compClassProfile` | 课表网格 + 铅笔角标 |
| Linkage.ClassislandClassswap | `compClassSwap` | 课表网格 + 循环双箭头角标 |

具体图形在设计预览评审时可调整，但必须两两可区分。

### 3.4 面板与辅助

| 资源 | 处理 |
|---|---|
| ~~`barEraserClearCanvas`~~ | v2 取消：橡皮面板项 0 沿用主栏垃圾桶 `barClean` |
| `barEraserSpeedSetting`（新） | 橡皮面板项 9：橡皮 + 小齿轮角标 |
| `barAutoEraser` | 字母 A 向橡皮收紧（保持 28 画布的现有布局习惯或迁至 24 画布，以评审为准） |
| `barCloseSmall`、`barInfo`、`barQuestion`、`barMore`、`barThicknessAdjust`、`barAdd`、`barMenu` | 原名原地重绘；线性图形统一 2.5 线宽；信息/问号改为实心圆 + 镂空符号 |
| `barMinimize`、`barPalette` | 原名原地重绘为 24 画布 + 占位色（修复浅色主题不可见） |

`barMore` 必须保持「0° 朝上」基准（`rendering-and-ui.md` 翻页角度合同依赖）；`barThicknessAdjust` 同样保持原朝向，不改调用侧旋转。

### 3.5 删除

| 资源 | 原因 |
|---|---|
| `barDismiss` | 唯一产品用法被 `barReturnShow` 取代；测试引用同步迁移 |
| `barDivider`、`logo2` | 已确认从未加载 |
| `CUSTOMIZEICO1`–`11` 及 `src/ClassIsland.png`、`src/IslandCaller.png`、`src/SecRandom.png`、`src/NamePicker.png`、`src/quick/*.png` | 组件改 SVG；仅剩未编译的 `IdtFloating.cpp` 引用 |

删除 PNG 文件前复核除 `IdtFloating.cpp` 外无引用（含 Setting 页面、插件代码）；若 `IdtFloating.cpp` 被其他配置编译则改为保留文件仅移除产品用法。`Frame 94.svg` 有测试引用，保留。

## 4. 代码改动

1. **组件改 SVG**（`Bar.Button.cpp` `registerComponent`，约 760–930 行）：参数 `iconResource` 改为 UI 资源名；去掉 `pngIcon` 初始化与 `rW/rH` 复制，改为 `obj->icon.InitializationFromResource(L"UI", iconResource)`，`iconKind` 保持默认 Svg；加载失败按原逻辑放弃注册。核对 More 面板、布局、Presentation 测试中对 `iconKind == Png` 的特殊分支，组件不再命中这些分支，分支本身若仍有其他使用者则保留。
2. **状态图标**：`Bar.Button.cpp` 白板按钮动态样式（返回放映 → `barReturnShow`，回到白板 → `barResumeWhiteboard`）与选择按钮白板「移动」→ `barMove`，均通过既有 `TransitionContent` 资源名切换，不新增动画路径。
3. **橡皮面板**：`Bar.EraserAttribute.cpp:122` 三元表达式改为 `i==0 → barClean, i==4 → barAutoEraser, i==9 → barEraserSpeedSetting`，写成显式映射避免默认分支落到无关资源。
4. **资源登记**：`Inkeys.rc` 增删条目；`Inkeys.vcxproj` 与 `.filters` 同步 `<None Include>`；保持文件原编码（rc 为 UTF-8 + CRLF）。
5. **测试**：`Bar.EraserAttribute.Test.cpp:1244-1245` 迁移到新资源名；`Bar.Presentation.Test.cpp` SVG 绑定探针覆盖组件按钮（如原测试按 Png 区分则调整断言）。

## 5. 交付流程与评审门

1. 先在任务 `research/` 下产出设计预览页（全部新/重绘图标，浅/深两种主题，14/18/20/28 DIP，与基准图标并排），由开发者评审确认。
2. 通过后再写入 `Inkeys/src/UI/` 与改代码。
3. 设计源为 `research/icon_build.py` + `icon_lib.py`（shapely 几何，线宽/间隙为常量）；静态校验脚本 `research/lint_icons.py` 断言 24 画布、单个 evenodd 路径、仅占位色、无 stroke/transform 等禁止内容、坐标不越界。

## 6. 兼容性与回滚

- 资源名不参与持久化：按钮布局持久化使用按钮 id（如 `Component.ShortcutButton.*`），资源名只在代码内引用，增删改名不影响用户配置。
- 回滚：图标文件与 rc/代码改动在同一提交范围内，可整体 revert；组件改 SVG 是独立代码点，可单独回退恢复 PNG。
