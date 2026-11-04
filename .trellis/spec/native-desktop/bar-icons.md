# Bar 图标设计语言与资源合同

## 1. Scope / Trigger

新增、重绘或替换 UI3 Bar（主栏、属性面板、PageControl、白板、组件按钮）SVG 图标，或改变图标资源登记/加载方式时适用。Logo（`logo1`/`Frame 94`）外观系统见 [bar-theme-material.md](bar-theme-material.md)，不在本合同内；`color/colorSelect.svg` 是有意保留的绿底白勾选中标记，也不在本合同内。

## 2. Signatures

- 资源登记：`Inkeys/Inkeys.rc` 中 `NAME  UI  "src\UI\name.svg"`（名称大写），并在 `Inkeys.vcxproj`/`.filters` 以 `<None Include="src\UI\name.svg" />` 跟踪。
- 加载：`BarUiSVGClass::InitializationFromResource(L"UI", L"name")`（返回 void，失败时 `svg.GetVal()` 为空）；状态切换 `BarButtonClass::TransitionContent(L"name", label, immediate)`。
- 着色：`Bar.UI.cpp` 加载时把 `rgba(10,0,7,0)` 替换为 color1、`rgba(9,0,2,0)` 替换为 color2；color1 由 `RetargetBarFixedButtonVisual` 在 IconPrimary/Accent 间切换。
- 设计源：`.trellis/tasks/archive/**/11-03-bar-icon-redesign/research/icon_build.py` + `icon_lib.py`（shapely 几何，常量 `LINE=2.5`、`GAP=1.5`）；校验 `lint_icons.py <文件或目录>`；预览 `preview_build.py <Inkeys/src> <research>` 生成 `icon-preview.html`。

## 3. Contracts

| 项 | 规则 |
|---|---|
| 画布 | `width="24" height="24" viewBox="0 0 24 24" fill="none"`，满幅 0.5–23.5（Bar 按高度缩放，需与画笔/橡皮等满幅基准图标同高同重） |
| 结构 | 单个 `<path fill-rule="evenodd" fill="rgba(10,0,7,0)" d="…"/>`；只允许 `svg`/`path` 元素与 `xmlns/width/height/viewBox/fill/fill-rule/d` 属性 |
| 风格 | 单色实心剪影，内部细节用镂空；线性元素（X、箭头、加号、菜单线、雪花臂）宽 2.5、圆端点/圆连接，在设计源中缓冲成填充 |
| 间隙 | 主体与角标/相邻部件统一 1.5（`knock`）；镂空细线 1.75–2.25；实心最小宽 2 |
| 角标 | 位于右下（中心约 `18,18`，半径 ≤5），主体让出 1.5 间隙 |
| 禁止 | 写死颜色（`#…`/`rgb()`）、color2、`opacity`、渐变、`clip-path`、`transform`、`stroke`、`<text>`、`id` |
| 朝向 | `barMore`、`barThicknessAdjust` 的 0° 基准朝上（翻页角度合同依赖，见 [rendering-and-ui.md](rendering-and-ui.md)） |

资源映射（每个行为一个专属图标，禁止跨行为复用）：

| 使用点 | 资源 |
|---|---|
| 白板按钮：打开 / 回到白板 / 返回放映 / 关闭 | `barWhiteboard` / `barResumeWhiteboard` / `barReturnShow` / `barCloseSmall`（`Bar.Button.cpp` `UpdateWhiteboardButtonStyle`） |
| 选择按钮：白板中「移动」 / 其他 | `barMove` / `barSelect` |
| 橡皮面板项 0 / 4 / 9 | `barClean`（沿用主栏垃圾桶） / `barAutoEraser` / `barEraserSpeedSetting`（`Bar.EraserAttribute.cpp` 显式映射，无默认分支） |
| 组件 Explorer / Taskmgr / Control / Desktop / Lock / Esc / Alt+F4 | `compExplorer` / `compTaskManager` / `compControlPanel` / `compShowDesktop` / `compLockScreen` / `compEscape` / `compAltF4` |
| 组件 IslandCaller1 / IslandCaller2 / SecRandom1 / SecRandom2 / SecRandom2Compat / NamePicker | `compRollCallRandom` / `compRollCallSingle` / `compRollCallQuick` / `compRollCallQuick2` / `compRollCallQuickCompat` / `compNamePicker` |
| 组件 ClassIsland 设置 / 档案 / 换课 | `compClassSettings` / `compClassProfile` / `compClassSwap` |

## 4. Validation & Error Matrix

| 条件 | 结果 / 处理 |
|---|---|
| 资源名未登记或拼错 | `svg.GetVal()` 为空；组件注册被放弃（按钮不出现），普通按钮无图标 —— 新增资源必须同时改 rc、vcxproj、filters |
| SVG 含写死颜色 | 不随主题着色（曾致浅色主题白色图标不可见）—— lint 拒绝 |
| 未提供 color1 却使用占位色 | 占位色保持 `rgba(…,0)` 全透明 —— 调用方必须传入 color1 |
| 新图标改变 `barMore` 朝向 | 翻页 Previous/Next 方向错误 |

## 5. Good / Base / Bad Cases

- Good：新状态图标在设计源中用共享部件（`person`、`eraser_with_badge`、`projection_screen`）+ 角标生成，lint 通过，预览页经开发者确认后再复制到 `Inkeys/src/UI/`。
- Base：仅调整现有图标几何，资源名不变，无需改代码与 rc。
- Bad：手写带 `stroke-width="1.85"` 的描边图标，或为新行为复用别的按钮图标（如曾经的极速橡皮设置复用主栏齿轮）。

## 6. Tests Required

- `lint_icons.py Inkeys/src/UI/<本次涉及文件>` 全部通过。
- `InkeysRepo.sln` Debug|当前原生架构完整构建 0 错误；`InkeysHeadlessTests.exe --no-window` 通过。
- `Inkeys.exe --bar-eraser-offscreen-test` 中白板图标用例断言 `barResumeWhiteboard`/`barCloseSmall` 资源非空（`Bar.EraserAttribute.Test.cpp`）。
- 人工：深浅主题下主栏、More 面板组件、白板四态、橡皮面板、提示气泡、PageControl 箭头/加页/结束放映显示与选中变色。

## 7. Wrong vs Correct

#### Wrong

```xml
<svg viewBox="0 0 48 48"><path d="M10 34h28" stroke="#fff" stroke-width="4"/></svg>
```

#### Correct

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none"><path fill-rule="evenodd" fill="rgba(10,0,7,0)" d="…"/></svg>
```
