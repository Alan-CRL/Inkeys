# Research: 首批导航、主页、常规绑定审计

- Query: 新设计迁移时必须保留哪些原控件、配置字段、FIFO 行为、实时同步与目标链接？
- Scope: internal；仅导航 / Home / General。
- Date: 2026-09-06

## Findings

### 证据边界与文件

主会话从 HEAD 导出了 `research/baseline/Setting.cpp` 等快照，本研究读取该快照；以下 `Setting.cpp:N` 均指 **实现前 HEAD 的 `Inkeys/Inkeys/UI/Setting/Setting.cpp` 行号**，不指并发改动后的工作区。研究代理没有执行 git 操作。快照可由主会话在审计结束后清理。

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 原导航、Home、General、临时 UI 状态和业务 FIFO |
| `Inkeys/Inkeys/UI/Setting/Setting.Layout.h` | 原导航断点与尺度纯函数 |
| `Inkeys/IdtConfiguration.h` | 传统 setlist 类型 |
| `Inkeys/IdtConfiguration.cpp` | CaptureSettingJson / WriteSettingJson 和 deploy.json 键 |
| `Inkeys/Inkeys/Other/Other.Config.cppm` | 新配置 UI.Bar.Zoom / EdgeLighting schema |
| `Inkeys/Inkeys/UI/Bar/Bar.Zoom.cppm` | Bar 缩放范围、两位小数与即时应用 |
| `Inkeys/src/i18n/zh-CN.jsonc` | 原说明、选项和重启提示文案 |

### 必须先保住的执行边界

`Setting.cpp:1420` 起，仅 `RunSettingSession` 后半文件内的宏把 `WriteSetting`、`ShellExecuteW`、`SetStartupState`、`RestartProgram`、`CloseProgram` 分别替换为 Queue helper。**提取页面到另一编译单元时，不得直接照搬这些函数名**，应由明确的动作回调/Queue API 承接；否则看似相同的调用会重新在渲染线程上写盘、Shell 或重启。

- 旧配置：`WriteSetting -> QueueWriteSetting -> QueueBusiness(WriteSetting)`；在生产者线程 `CaptureSettingJson()`，worker 调 `WriteSettingJson(payload)`，目标 `globalPath + opt/deploy.json`。证据：`Setting.cpp:647,767,819`；`IdtConfiguration.cpp:406,568`。
- 新配置：`QueueConfigWrite()` 在生产者线程创建 `Inkeys::Config` 副本并赋值自 `Inkeys::config`；worker 仅对副本 `Write()`。目标为 `Inkeys/Config/main.json`。证据：`Setting.cpp:651,809,829`；configuration spec。
- FIFO 先进先出，停止时拒绝新命令且 drain 已接收命令；每个完成后发布不可变 completion snapshot 并 `Request(Settings)`。不能借用帧内 string 或让 worker 读取实时配置。证据：`Setting.cpp:586,601,613,631`。
- 该范围原页面没有保存/取消按钮，没有常规控件更改后立即弹出的确认框；不要把说明文案误转成新模态流程。

### 导航完整清单

原条目顺序位于 `Setting.cpp:2189` 起。13 个页面加 3 个 footer 动作，全都需要保留可达性。

| 可见项 | 原路由 | 图标码点 | 附带行为 |
| --- | --- | --- | --- |
| 主页 | `tab1` | E80F | 无 |
| 语言 | `Language` | E774 | 无 |
| 配置 | `tabConfiguration` | E81E | 无 |
| 软件版本 | `tab6` | E946 | 无 |
| 常规 | `tab2` | E7B8 | 无 |
| 绘制 | `tab3` | EE56 | 无 |
| 预设 | `tabPreset` | F259 | 无 |
| 插件 | `tab4` | E74C | 每次普通导航点击先令 `settingPlugInTab=tabPlug1` |
| 组件 | `tabComponent` | E70B | 无 |
| 快捷键 | `tab5` | E765 | 无 |
| 实验选项 | `tabExperimental` | EC4A | 原来是硬编码中文 label |
| 赞助 | `tab8` | E789 | 无 |
| 软件调试 | `tab9` | E90F | 无 |
| 社区 | 无页面路由 | E716 | 简中 `https://www.inkeys.top/community.html`；其他语言 `https://en.inkeys.top/community.html`，通过 FIFO Shell |
| 重启软件 | 动作 | E72C | 先 `Setting::Hide()`，再 FIFO Restart，无新增确认 |
| 退出软件 | 动作 | E711 | 先 `Setting::Hide()`，再 FIFO Close，无新增确认 |

页面与动作都会令 `navigationActivated=true`；overlay 的任意导航激活都关闭 pane (`Setting.cpp:4051`)。一般插件入口的默认子页与“更多选项”定向子页必须区分，不能用统一路由 reset 覆盖定向子页。

原标题栏版本标签还有 `tab6` 入口 (`Setting.cpp:2118`)；本轮标题栏不在改动范围，应继续保留。

### Home 原内容与链接

原主页 `Setting.cpp:2266` 无配置控件，也没有写入配置；已有六个外部动作：

| 入口 | 目标 / 条件 | 证据 |
| --- | --- | --- |
| Website | 简中 `https://www.inkeys.top`；其他 `https://en.inkeys.top` | 2276 |
| GitHub | `https://github.com/Alan-CRL/Inkeys` | 2282 |
| Community | 简中 `https://www.inkeys.top/community.html`；其他 `https://github.com/Alan-CRL/Inkeys/discussions` | 2285 |
| Bilibili | `https://space.bilibili.com/1330313497` | 2291 |
| Feedback | `https://www.wjx.cn/vm/mqNTTRL.aspx#` | 2294 |
| 联系作者 | `mailto:alan-crl@foxmail.com` | 2302 |

**Home 与导航 footer 的非简中 Community 目标不同。** 不得为复用一个 helper 意外归并。所有 Shell 都进入 FIFO，使用 `SW_SHOW`。

原内容：Inkeys 标题及 `Home.Prompt`；快速访问区；作者 AlanCRL 及 `Home.Developer`；教程图片 `TextureSettingSign[1]` 按 `settingSign[1]` 原纵横比展示；旧教程尾部有占位说明 (`2310` 起)。新设计可重新组织这些内容，不应交付旧占位说明冒充真实入口。新增“绘制 / 预设 / 插件 / 常用设置”属于本次获准的真实页面入口，须明确对应已有路由；主操作也须连接真实业务动作。

### General 原控件与绑定

原 General 共 **9 张设置卡，10 个输入/按钮**：4 toggle、2 slider、2 combo、快捷方式区 2 button。控件编号/值不能因标签改变而改变。

| 卡 / 原 ID | 状态与配置字段 | 保存 / 运行时行为（顺序） | HEAD 行号 |
| --- | --- | --- | --- |
| 自动启动 `regular-startup` | UI `StartUp`；`setlist.startUp`；JSON `StartUp` | 值不同：QueueSetStartup(flag, GetCurrentExePath(), `$Inkeys`) → 改 setlist → QueueWriteSetting；worker 执行真实 SetStartupState。UI 不等待返回值或回滚 | 2719–2728 |
| 桌面快捷方式 `regular-shortcut` / 创建 | 无配置字段 | UI 用 SHGetSpecialFolderPathW 查 Desktop；成功才 QueueBusiness(CreateShortcut)，text=`desktop\\` + 本地化 Widget.LnkName + `.lnk`，directory=当前 EXE。worker 仅当目标不存在或指向不符才 CreateShortcut | 2732–2747；692–697 |
| 桌面快捷方式 / 更多选项 | `settingPlugInTab`, `settingTab` | 先 `tabPlug3`，再 `tab4`，不保存；不能被普通插件导航改回 tabPlug1 | 2750–2753 |
| 主栏缩放 `regular-bar-scale` | UI `BarZoom`；新配置 `UI.Bar.Zoom` | 范围 **0.50–2.00**，每帧 round(value×100)/100。差值>0.0001 时改新配置 → `Bar::SetConfigZoom(double)` 实时应用 → savePending=true。非 active 且 pending 才 QueueConfigWrite，然后清 pending | 2760–2777 |
| 边缘光影 `regular-edge-light` | UI `Experimental.Inkeys3.EdgeLightingEnable`；新配置 `Experimental.Inkeys3.UI3.EdgeLighting.Enable` | 值不同：改 Enable → `Bar::SetEdgeLightingOptions(Enable, DynamicEdgeLighting)` → QueueConfigWrite。**必须携带现有 Dynamic 值，不能关闭总开关时重置它** | 2781–2794 |
| 设置页缩放 `regular-setting-scale` | UI `SettingGlobalScale`；`setlist.settingGlobalScale`；JSON `SettingGlobalScale` | 范围 **1.00–2.00**，两位小数；只在 `!IsItemActive` 且与配置不同后：NormalizeUserScale → 改 setlist → UpdateSettingScale(QuerySettingDpi(hwnd)) → mutex下 QueueFontRebuild → Request(Settings) → QueueWriteSetting。不要拖动每帧重建图集 | 2798–2817 |
| 置顶间隔 `regular-top-window` | UI `TopSleepTime`；`setlist.topSleepTime`；JSON `TopSleepTime` | Combo 真更改且两份 old/current 不同才：改 setlist → QueueWriteSetting → `topWindowNow=true`。保存的是 **0..6 索引**，对应 100ms/500ms/1s/3s/5s/10s/30s，不是毫秒值 | 2823–2844 |
| 右键关闭 `regular-right-click` | UI `RightClickClose`；`setlist.RightClickClose`；同名 JSON | 值不同：改 setlist → QueueWriteSetting；该操作本身不弹确认框 | 2848–2857 |
| 避免全屏 `regular-avoid-fullscreen` | UI `RegularSetting.AvoidFullScreen`；`setlist.regularSetting.avoidFullScreen`；JSON `Regular.AvoidFullScreen` | 值不同：改 setlist → QueueWriteSetting；保留需要重启的说明，本控件不弹重启确认，也没有新窗口重建动作 | 2863–2872 |
| 教学安全 `regular-safety` | UI `RegularSetting.TeachingSafetyMode`；`setlist.regularSetting.teachingSafetyMode`；JSON `Regular.TeachingSafetyMode` | Combo 真更改且两个 old/current 不同才：改 setlist → QueueWriteSetting → `CrashHandler::SetFlag(...)`。保存 **0..3 索引**：弹窗并重启/静默重启/系统崩溃过滤器/直接关闭 | 2876–2896 |

对应 JSON 捕获：`IdtConfiguration.cpp:421–436`。本轮没有理由修改 schema、旧键或目录。

临时 UI 值原本在 resident 协程创建时从配置初始化 (`Setting.cpp:1848–1866,1983–1984`)，不在每帧重置。迁移后的状态不可在每帧重新初始化而打断拖动；主页若重复放入同一开关，需共用同一字段/动作入口，避免两份驻留镜像互相覆盖。

### 说明 / 提示的保留

原语言源 `Inkeys/src/i18n/zh-CN.jsonc:180–255`：

- 快捷方式 E 指向“更多选项”，不是点击创建后自动修正所有快捷方式的承诺。
- 主栏缩放明确“实时调节”；设置页缩放旧 E 仍写“需要重启”，但 HEAD 已是松手即时字体重建。这是历史文案与代码不一致，允许按真实行为更正文案，不应重新加重启弹窗。
- 置顶间隔说明包含非绘制模式、落笔暂停、CPU 权衡及“插件-超级置顶”建议；可折叠，但应保留完整可访问内容。
- RightClickCloseE 的“将会弹出一个确认弹框”说的是以后右键关闭动作，不是现在切 toggle。
- AvoidFulScreenE 含“需要重启软件”，原因覆盖任务栏自动隐藏/置顶/触摸，保留可访问长说明。
- 安全模式说明为崩溃时的动作，启动错误依旧提示，不是应用此设置后立即重启。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`：Setting FIFO、shared render thread、resident 字体生命周期、light-only。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：两套配置共存、源 i18n / generated header 边界。
- `.trellis/spec/native-desktop/cpp-conventions.md`：最小改动、中文注释、保留编码/换行。
- `research/approved-first-batch.md`：已批准 nav + Home + General，其他页内部布局和窗口/DWM 不在本批。

## External References

本主题为仓库绑定审计，不使用外部规范替代源码行为。外链本身列于上表，未打开目标页面。

## Caveats / Not Found

- 此清单是 HEAD 原行为，不表示工作区迁移已核对通过；主会话/check agent 应逐项对照。
- 原 General 无 theme、绘制收栏或 dynamic edge-light 输入；虽然 resident 状态/i18n 中存在相关字段，它们并不是这 9 张卡的额外可见控件。
- 本研究不更改源代码，不运行构建、应用、真实 Shell、启动项或写配置命令。