# Research: 多设备漫游惯性设置与配置传递

- Query: 为白板拖动/平移模式增加惯性总开关及鼠标、触控板、触摸、笔、笔尾五类偏好；寻找现有内联控件、配置持久化和热更新最小接线。
- Scope: internal；只读产品代码，仅写本研究，不实施、不运行 GUI、不编译、不提交。
- Date: 2026-11-08
- Baseline: `961ffdf8` 是用户已确认的鼠标节拍修复；当前 `canvas-pan.md` 为产品合同，历史 Chromium 替换与 scale-stage 方案不能作为本阶段实施授权。

## Findings

### 1. 已确认的用户要求与仍待方案评审的建议

| 项目 | 要求/建议 | 状态 |
| --- | --- | --- |
| 支持范围 | 只在白板拖动/平移模式启用多设备漫游，新增笔、笔尾、触控板 | 用户明确要求 |
| 笔靠近/按下 | 整个拖动/平移模式取消笔靠近急停（不区分自身/其他笔）；只有落笔按下急停并开始新 pan；绘制模式靠近急停留后续 | 用户本轮补充已明确 |
| 惯性粒度 | 一个总开关，下方内联细分五类输入 | 用户明确要求 |
| 子项默认 | 鼠标=false、触控板=true、触摸=true、笔=true、笔尾=false | 用户明确要求 |
| 惯性关闭 | 保留按住/手势过程中的实际平移，只禁止后续惯性 | 需求语义与主会话本阶段 M3 一致 |
| 总开关默认 | true，保证默认仍有 Touch/Pen/Touchpad 惯性 | 建议，用户未单独明确 |
| 总开关关闭后的 UI | 父行常驻，下方五项收起；不清空子项偏好，重新开启恢复原选择 | 建议，沿用现有内联设置模式 |
| 生效时机 | 设置热生效；下个绘制帧停止被禁类型 coast、清理 boost；进行中的拖动继续跟手；重新开启不恢复旧速度 | 建议，需纳入本阶段方案评审 |

本研究不裁决触控板手势种类、来源识别或系统惯性接管；这些是另一个输入研究主题。

### 2. 推荐放置：设置 → 绘制 → 通用

- `Inkeys/Inkeys/UI/Setting/Setting.cpp:3666` 是“绘制”主路由；现有子页为 Cursor/PalmEraser/SpeedEraser，主页面从 `:3791` 绘制。
- 绘图环境组位于 `:3829`，通用组位于 `:3855`；通用目前依次多指绘制、笔尾修正、笔迹预测（`:3862`、`:3865`、`:3868`），其后 `:3888` 是橡皮组。
- 推荐在笔迹预测之后、橡皮组之前新增单一“白板拖动惯性”卡片，说明“在白板拖动模式下，松开后继续滑动，可按输入类型调整。”不增加新侧栏页或二级页面。位置属于建议，用户已授权“找个地方”，无技术必要再问位置。
- 五项标签建议严格按用户顺序：鼠标、触控板、触摸、笔、笔（笔尾）。不要直接复用橡皮“鼠标左键/鼠标右键”标签：本功能是 Mouse 一项，用户没有要求右键漫游或右键独立惯性开关。

### 3. 已有可直接复用的内联组件

- `Setting.Controls.h:63` 定义 `InlineRowContent`，含按真实像素宽度测高/绘制函数、稳定 visibilityId 和 visible。
- `Setting.Controls.h:143` 的 `ToggleRow` 最后参数接受 `const InlineRowContent*`；`:151` 的 `InlineCheckboxes` 直接生成父行下方复选框，支持条件动画显隐。
- 实际同型生产例子位于 `Setting.cpp:3914`–`:3935`：断触接续总开关，下方内联“触摸/笔（笔尾）”，`InlineCheckboxes(..., visibilityId, reconnectEnabled)` → `ToggleRow(..., &reconnectDevices)`。关闭时保留选项，收起过程禁止交互，没有新页面、额外标题卡片或独立 expander。
- `Setting.Controls.cpp:2204`–`:2245` 是内联复选框实现；`:2215` 仅当宽度至少 440 DIP 且两列放得下最宽项才两列，否则一列；`:2231` 禁止收起时交互。复用它即可适应五项、窄宽窗口、多语言和 DPI，不需要修改共用控件。
- 用户提到的笔速橡皮当前实际用 `Setting.Pages.cpp:992`–`:1000` 的 `CheckboxGroup`，属于独立设备卡。其数据/事件模式（`SpeedEraserDraft.enabled`、`SpeedEraserEvents.enabledChanged`，见 `Setting.Pages.h:230`–`:246`）可参考，但本需求明确“下方内联”，应选更准确的断触接续组合。
- 为真实页面回放验证，可以在 `Setting.Pages.h/.cpp` 增加很小的 draft/events/render helper，由 `Setting.cpp` 负责业务发布，沿用已有可测试页面做法；不因此重构整个绘制页。

### 4. 配置结构建议及缺项兼容

建议新增在现有 `Drawing.Whiteboard` 组内（`Other.Config.cppm:438`），与已存在 DefaultTheme 相邻：

```text
Drawing.Whiteboard.PanInertia.Enabled = true       // 建议的总开关默认
Drawing.Whiteboard.PanInertia.Mouse = false
Drawing.Whiteboard.PanInertia.Touchpad = true
Drawing.Whiteboard.PanInertia.Touch = true
Drawing.Whiteboard.PanInertia.Pen = true
Drawing.Whiteboard.PanInertia.PenTail = false
```

- 六个叶子均采用 `IdtAtomic<bool>`、`ConfigUploadMode::NoUpload`，不加入旧 `deploy.json`。现有通用绘制偏好（`:430`–`:433`）、橡皮（`:455`–`:465`）、断触接续（`:486`–`:488`）均为此 schema/配置系统所有。
- `Other.Config.cppm:648`–`:664` 明确 schema 为唯一声明源，新增 GROUP/X 即生成成员/ReadAll/ReadMini/Write，通常无需修改 `.cpp` 编解码器。
- `Other.Config.cpp:171`–`:175` 只接受真正 JSON bool；`ReadDocumentHandler:575`–`:580` 和 `:708`–`:711` 解析失败不强转。默认遍历 `:540`–`:545` 先恢复选中路径默认，所以缺项/非法类型会保留指定默认，不把数字、字符串当 bool。
- `ReadAll:890`、`ReadMini:907`、`Write:913` 使用同一 rwMutex。`IdtAtomic.h:37`/`:46` 的 load/exchange 可跨线程安全读写单叶；六叶各自原子并不提供“六叶同一代”的成组快照，运行时不应逐事件零散读取它们。
- 这是新配置，旧版没有对应键可迁移。旧配置升级默认 Mouse 从原固定有惯性变为无惯性，是用户本轮明确指定的新行为；不要悄悄保留 Mouse=true 去“兼容”旧版本。
- 总开关关闭时只改变有效策略，不覆盖保存的五项值；持久化保留原子 bool，Host 发布一个完整编码快照。

### 5. 最小运行态传递与线程边界

推荐链路：启动/用户设置变化 → 读取完整六项形成类型化 options/小位掩码 → `Host::SetCanvasPanInertiaOptions`（拟新增）→ owner 的 `PumpBridgeState` → DrawingController 同一快照 → CanvasPanInput/CanvasPanDynamics 释放策略。

- 启动真实接点：`IdtMain.cpp:3367` 先 config.ReadAll，`:3370`–`:3384` 在 Host 启动绘制线程之前发布通用、Laser、Palm、断触接续等偏好；本设置在相同区域发布，不能只在打开设置页面后才生效。
- 已有 Host 小快照范式：`Draw3.Host.cpp:2970`–`:2975` 将三项编码 uint32，用 exchange 比较，只有真实改变才 PublishControlWake；`:460`、`:462` 保存 desired/applied；`:1772`–`:1777` 的 `PumpBridgeState` 检查不同后交给 DrawingController。`ConsumeBridge:2054`–`:2063` 保证在 owner 消费控制唤醒时应用，不从 UI 线程直接操作导航内核。
- `Host::SetLaserOptions:2978`–`:2983` 已保存子偏好，由 owner 计算有效值，可复用其总开关不覆写子项思想。
- 设置页对应生产例 `Setting.cpp:3928`–`:3935`：写 config、发布 Host、仅用户改变后 `QueueConfigWrite()`。不要在每帧页面绘制重复发布，不要把每个 Move 当设置变化。
- `Setting.cpp:1339` 的 QueueConfigWrite 走既有冻结配置写盘 FIFO；不得在 Draw3 输入/绘制线程自行写文件。
- Host 已为本次设置变化提供唤醒；如果 Controller setter 仅由 owner 调用，可沿 `SetPalmEraserOptions:12353` 只写 owner 状态，避免照抄 `SetGeneralDrawingOptions:12345` 再造不必要第二个控制 wake。若保留公开跨线程 setter，则需要明确其原子/唤醒合同及测试，不能依靠调用方猜测。
- 本轮鼠标修复合同要求普通位置流不打断帧节拍。设置快照读取/发布不得放在 `PublishMouseCursorSample`、普通 Move 或渲染每帧无条件 setter 中；只有真正配置变化可唤醒。

惯性策略建议作为独立门控进入现有唯一动力学真值：真实 Up 完成几何和终态闭合后，在创建 FlingStart/boost 前根据当前手势类型裁决。关闭不能通过停止整个 held 手势来实现；运行中禁用 coast 需清除剩余和 boost，避免后来开启或跨类型 Down 复活旧速度。更详细 source/仲裁由输入研究决定。

### 6. 国际化与可复用现有测试

- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:356`–`:365`：基准 `Inkeys/src/i18n/zh-CN.jsonc`，其他十九语言跟随，`scripts/i18n.ps1 sync` 生成 `Inkeys/IdtI18nKeys.g.h` 及 `scripts/i18n.zh-CN.snapshot.jsonc`。禁止手改生成头。
- `scripts/i18n.ps1:4` 为 sync/check 命令，`:13`–`:16` 定义产物，`:724`–`:733` 遍历 locale/更新头和快照；新增键必须完整翻译全部二十语言（规范 `:777`）。本轮研究不运行 sync（它会写产品文件）。
- 建议新标题/说明/五标签集中 `SettingsUI.Draw.General.CanvasPanInertia`（命名示例，避免 Win32 宏冲突），文字描述产品行为，不显示内核类名/队列策略。
- `InkeysHeadlessTests/config_metadata_tests.cpp:37` 为真实配置测试入口；`:798`–`:832` 的既有 CanvasPan 诊断 flag 测试可参考缺项、true/false 轮转、冻结 copy、Write/restart、ReadMini 仅影响自身、非法 bool、Reset、NoUpload。新惯性六项需同样经过真实文件/真实 Config，不能只测独立默认 struct。
- `IdtMain.cpp:679` 的 `RunConfigBoundaryTest` 在隔离路径测试真实配置，`:1985` 精确命令 `--config-boundary-test` 在产品 GUI 启动前返回；可增六项覆盖，但核心 Config 元数据回归仍放现有 headless 模块。
- `setting_design_tests.cpp:2635` 的 CheckSpeedEraserPage 使用真实 ImGui 字体/鼠标按下抬起/DrawData，适合复用模式验证新行五项 ID/一次点击一次事件、master 不清子值、隐藏收起不产生事件、多主题/多语言/DPI/窄宽换列/热切语言。
- `animation_tests.cpp:1832` 的 `--setting-general-only` 是现有无窗口设置专项；新 helper 的回放接在该集合即可，无需新构建系统或新可执行。通用 `--no-window` 屏障位于 `:1790`–`:1803`。

### 7. 最小必要文件与后续验证建议

| 文件 | 本主题必要作用 |
| --- | --- |
| Other.Config.cppm | 六个 NoUpload bool 默认/持久化声明 |
| IdtMain.cpp | 启动 config → Host，必要配置边界 probe |
| Draw3.Host.h/.cpp | desired/applied 完整快照、真实变化唤醒、owner 应用 |
| Draw3.DrawingController.cppm/.cpp | 接收策略并应用到当前导航；与输入改动同一 owner |
| Draw3.CanvasNavigation.cppm/.cpp | 释放/boost 门控，保持拖动几何；具体粒度需输入设计收敛 |
| Setting.Pages.h/.cpp | 小型真实行渲染 helper/draft/events（为了现有无窗口回放） |
| Setting.cpp | 在绘制→通用插入行；config/Host/异步写盘接线 |
| src/i18n/*.jsonc + Keys.g.h + snapshot | 新文案二十语及生成产物 |
| config_metadata_tests.cpp / setting_design_tests.cpp | 真配置/真控件回归 |
| draw3_canvas_pan_tests.cpp / 既有生产 control-fence probe | 策略门控、关闭/重开、不同来源回归 |

不必修改 `.sln/.vcxproj`（优先扩展现有文件）、配置编解码器、共享设置控件、旧配置系统、橡皮本身、屏幕尺度或动力学摩擦参数。

待实施后最小验证：i18n sync 后填全翻译再 check；真实 Config 与设置行专项；CanvasPan 纯策略和生产 transport/probe；ARM64 原生 MSBuild `InkeysRepo.sln Debug|ARM64`（同 PowerShell 清 PATH 重复键，至少五分钟预算）；完整 Headless `--no-window`。最终再按用户授权单独做实机设备体验，不把离屏控件或纯策略测试称为实机输入通过。本轮没有执行这些产品验证。

## External References

无。此主题完全由仓库现有设置、配置与线程接线证据支持；触控板 Windows API 事实由独立输入研究核查。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：当前产品手感、可靠输入及鼠标 ordinary Move 保持帧节拍合同。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：配置系统归属、内联子项保留、二十语生成合同。
- `.trellis/spec/native-desktop/settings-performance-and-drawing.md`：绘图环境→通用结构、热发布/下次落笔设置区别。
- `.trellis/spec/native-desktop/settings-attention-notes.md`：普通说明/特别注意文本边界；本设置计划热生效，不应添加“重启后生效”。

## Caveats / Not Found

- 现有仓库没有白板惯性类型开关或现成六项配置，以上路径和 API 名是最小方案建议，不是已存在符号。
- 用户未单独指定总开关默认及运行中关闭的精确收敛语义，应在最终方案一并呈现；无需把可由现有 UI 证据决定的位置、布局重复问成阻塞问题。
- 子项可全部关闭；不要自动强制保留至少一类，也不要把 master 自动改为 false，这会破坏“总开关+独立偏好”含义。
- 触控板是否采用双指滚动、按压拖动、系统 momentum 或自建 fling 是更高优先级产品边界；设置仅有 Touchpad 标签不能证明已有可靠来源识别。
- 本主题未修改任何产品文件、未运行构建/测试/GUI，未执行 Git 操作。
