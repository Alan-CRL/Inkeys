# Design: UI3 主栏状态指示器

Research: `research/bar-rendering.md`、`research/settings.md`、`research/status-sources.md`（行号基于 `feature/state` @ 6ee09764，实施时以实际代码为准）。

## 1. 总体结构

```
状态源（任意线程）                 Bar 渲染线程（唯一 owner）                         设置渲染线程
PptInfo ─┐                      ┌──────────────────────────────────────────┐
SuperTop ┼─ StatusIndicator ──▶ │ drain commands → 过滤(配置) → 状态机推进   │◀── SetStatusIndicatorOptions
Update ──┤   Publish API        │ → 纯布局 → 绘制 → 脏区/视口 → 呈现后命中快照 │◀── Debug Add/Remove
Debug ───┘  (mutex 队列+唤醒)    └──────────────────────────────────────────┘
                                    │ 点击新版本圆形
                                    └──▶ Setting::ShowSoftwareVersion()
```

- **新模块** `Inkeys/Inkeys/UI/Bar/Bar.StatusIndicator.h`（纯模型 + 布局，无 D2D/HWND，可无窗口测试）与绘制/接线放在既有 RenderLoop 阶段中；对外发布 API 加在 `Bar.Main.cppm`/`Bar.Main.cpp` 导出。
- 状态源只调用发布 API，不碰 Bar 状态；Bar 渲染线程是全部指示器状态的唯一写者（与现有 `PublishPptSession`、`SetThemeMode` 等跨线程发布 + `UpdateRendering` 唤醒的约定一致）。

## 2. 发布契约（跨线程）

```cpp
enum class StatusIndicatorKind : uint8_t { PptSlideshow, WpsSlideshow, SuperTop, NewVersion, Debug };
enum class StatusIndicatorLifetime : uint8_t { Persistent, Notification };
enum class StatusIndicatorAction : uint8_t { None, OpenSoftwareVersion };

struct StatusIndicatorSpec {
    uint64_t key;                 // kind + 实例（如 PPT localSession、调试序号）；同 key 重复注册 = 更新而非新出现
    StatusIndicatorKind kind;
    StatusIndicatorLifetime lifetime;
    StatusIndicatorAction action;
    uint32_t accentArgb;
    StatusIndicatorIcon icon;     // SVG 资源枚举
    std::wstring text;            // 已本地化（发布方用 IW()）；调试状态为合成文本
};

void PublishStatusIndicator(const StatusIndicatorSpec&);        // 注册/更新
void RetireStatusIndicator(uint64_t key);                        // 注销（持久状态渐隐缩小退场）
void PublishStatusIndicatorProgress(uint64_t key, float progress /*<0 无进度, 0..1*/, bool complete);
void SetStatusIndicatorOptions(bool enable, bool ppt, bool superTop, bool newVersion);
void DebugAddRandomStatusIndicator(); void DebugRemoveStatusIndicator();
size_t StatusIndicatorCount();                                   // 供调试按钮禁用判定（原子快照）
```

- 实现：`std::mutex` 保护的命令队列（Publish/Retire/Progress/Options/Debug），入队后 `barUISet.UpdateRendering(false)`。渲染线程在 `SubmitTargetsAndLayout` 起始 drain。
- **去重**：同 key 已存在时 Publish 只更新文本/颜色/动作，不重放出现动画（例如 PPT 同会话重新锚定窗口时不得算作新出现）。
- **过滤**：总开关关闭或类型勾选关闭时，条目保留在注册表但不可见；重新开启时以收起态（圆形）直接出现，不重放胶囊（避免切换设置时连环弹出）。Debug 类只受总开关控制。
- **容量 25**：注册表（可见条目）上限 25。满时：新的通知挤掉最旧的通知/调试条目；仍满则拒绝并记录日志。真实状态源最多 4 个，不会触及上限。

## 3. 条目状态机（渲染线程，绝对时间截止）

```
           Publish
              │
   ┌──────────▼──────────┐ 3s 到期 / 点击        ┌───────────┐
   │ Expanded（胶囊 + 环）│ ───────────────────▶ │ Collapsing │──▶ Docked（右上角圆形, 50% 透明）
   └──────────┬──────────┘                       └───────────┘           │
      Notification│ 3s / 点击                                     Retire │   轨道展开中点选
              ▼                                                          ▼        │
         Dismissing（缩小 + 反向回弹 + 渐隐）──▶ 移除          Exiting（渐隐缩小）   ▼
                                                                         Reexpanded（选中，胶囊覆盖在弧上）
```

- 3s 停留时间用真实时间（不受动画速率影响）；动画速率/关闭只影响过渡时长（关闭 = 立即完成，沿用 `BarUiAnimationSpeedRate` 规则）。
- 参考 `PageLoadingIndicator` 的"绝对截止 + 独立视觉需求 + `WakeDeadline`"模式：空闲时 Bar 不刷新，只按最近截止唤醒。
- 展开态队列按出现顺序：只有最前（最旧、最下）的条目文字可见。**每个条目的 3s 从它移到最前位置时开始计时**（在后面排队时不计时），保证每个状态都有完整 3s 可读时间；最前者到期收起后，其后的下移补位并开始计时。指示环随最前者的 3s 显示（颜色取最前者）。（用户 2026-11-02 确认采用队首计时。）
- 新版本静默下载：`Expanded` 正常 3s 收起；进度环是独立的"环所有者"（见 §5），不绑定胶囊停留。

## 4. 布局（纯函数，可测试）

输入：主按钮当前几何（中心、`w.val/h.val/n.val`，含点击脉冲与折叠形变）、**正常态边界** `baseBounds`、显示器工作区 `work`、主题/缩放无关的 DIP 参数、条目列表与各自动画进度。

- **正常态边界 `baseBounds`**：本帧不含任何指示器贡献时的可见内容边界（`visibleContentBounds` 在加入指示器前的值）。正常态（胶囊堆叠、圆形轨道）一律夹在 `baseBounds` 内，因此不扩大窗口；只有轨道展开（Fan）与展开选中的胶囊可以越界并加入 `visibleContentBounds` + 动画包络。
- **胶囊**：高 24 DIP；右端为 22 DIP 填充圆（图标 13 DIP）；左段文字 11 DIP 粗体（`Word()` 默认 BOLD，本地化字体族），水平内边距 4 DIP。整体宽度上限 `mainButtonWidth - 4`（≈76 DIP，文字区 46 DIP，中文四字不滚动）；窄时整体在主按钮上水平居中；超限时文字段裁剪并横向滚动，速度 = 溢出宽度 / (3s − 进出场时长 − 两端停顿 0.3s)，保证 3s 内滚完。
- **胶囊锚点**：底边对齐主按钮上沿内侧（与主按钮上部重叠），出场从主按钮上端中心由小变大（`EaseOutBack` 缩放 + Sine 透明度，复用 `BarAttributePanelMotion` 曲线组合）。
- **展开态堆叠**：最旧的在锚点（最前）；之后每个向上偏移 `step = 4 DIP` 并位于其后（Z 更低），只露出颜色；累计偏移顶到 `baseBounds.top` 后不再增加（其余完全重叠）。条目移除时其后的 `offset` 以弹簧（复用 `AdvanceBarBottomDockSpring` 参数）下移。
- **圆形轨道（Docked）**：直径 16 DIP，50% 透明度；最新的锚定在主按钮右上角（圆心约在超椭圆右上角外侧 4 DIP，可越过主按钮边框，但在 `baseBounds` 内）；更早的依次向左偏移 `step = 5 DIP`（Z 更低，只露颜色）；向左顶到 `baseBounds.left` 后不再偏移。某个移除后其左侧的右移补位（弹簧）。
- **轨道展开（Fan）**：以主按钮中心为圆心、向上半平面的多层同心弧：第 k 层半径 `R0 + k·ΔR`（R0 = 主按钮半高 + 20 DIP，ΔR = 28 DIP），每层按弧长均匀放置，容量 `floor(πR / 26DIP)`（约 7–9 个），新的在最右（弧起点在右侧）。若向上空间（`work`）不足则整体镜像向下。圆形在 Fan 中为 24 DIP、不透明。
- **Fan 中选中**：在选中圆形上方展开其胶囊（覆盖其他圆形，Z 最高），超出 `baseBounds` 允许。

## 5. 指示环与进度环

- **几何**：沿主按钮当前超椭圆路径描边（独立的几何缓存，避免与主按钮共用的单条 `superellipseGeometryCache` 互相抖动）；线宽在 1 DIP 基础上加粗到 3 DIP（动画）。
- **颜色与光影**：描边色 = 状态强调色；光影开启（`BarUiEdgeLightingEnabled`）时走 `DrawPointLightFrame`，`frameLightColor` 设为固定色（非 `PenWhenDrawing`），即可得到彩色漫反射；光影关闭时只画实线。
- **出现规则**：渐显 → 呼吸（透明度在 0.55–1.0 间正弦，周期 1.6s）→ 与胶囊 3s 收起同时渐隐。多个状态同时触发时由当前最前的可读条目占有颜色（颜色过渡，与队首计时一致）。
- **非绘制态门禁**：`SnapshotFrameLighting().drawingPenColorBlend > 0`（或业务态 `IdtPen/IdtShape`）时环立即渐隐且不再出现；退出绘制后若环的所有者仍在显示期内则恢复。
- **进度环（新版本 + 静默更新）**：
  - 轨道 = 主按钮原边框；进度段 = 从超椭圆顶部中点顺时针、按路径长度比例截取（`ID2D1PathGeometry` + `StrokeStyle` dash 或 `ComputeLength`/分段几何），颜色绿色，宽 3 DIP。
  - 进度来源：渲染线程在有新版本条目时，按低频（≤10 Hz，用截止唤醒而非持续动画）读取 `downloadNewProgramState` 原子量并经 `PublishStatusIndicatorProgress` 契约归一；**单调化**：同一下载周期内显示值只增不减（线路切换/HTTPS→HTTP 回退导致的回零被吸收）；字节完成后的解压校验阶段保持 100% 呼吸。
  - `UpdateRestart` 到达 = complete → 3s 后渐隐。下载失败 → 环渐隐，状态圆形保留（新版本仍存在）。
  - 绘制态门禁同样适用（只隐藏，不丢进度）。

## 6. 绘制与 Z 序

- 位置：`CalculateDirtyAndDrawPresent` 中主按钮 + logo 绘制之后、粗细浮窗之前（`Bar.RenderLoop.cpp` ~11554 之后），同一 `SetGripTransform()` 下，跟随主按钮的抓取/底栏形变；Fan 与选中胶囊之后再画一次"前景层"（在底栏指示器之前），保证覆盖主栏其他内容。
- 主题：胶囊文字段填充与图标色按 `state.mainButtonLightMaterial.val` 在 Dark/Light 端点间插值（Dark: 深色填充 + 白图标；Light: 白填充 + 黑图标）；填充不透明度乘 `GetThemeResidentSurfaceOpacity`，图标/文字乘 `GetThemeResidentContentOpacity()`（`SetResidentContentOpacity` 只影响 Svg/Word，形状须手动乘）；强调色边框按 `GetThemeSurfaceFrameOpacity` 规则。
- 图标：新增 SVG 资源 `BarStatusSlideshow`、`BarStatusPin`、`BarStatusNewVersion` 及 3–4 个通用图标（调试用），`Inkeys.rc` 注册，`BarUISetSvgEnum` 扩展，单色可着色。

## 7. 窗口、脏区、命中

- 新的 dirty fixed key：`BarDirtyFixedVisual::StatusIndicator`；每帧 `Observe(key, union(所有可见条目 + 环 + 光影外扩))`，与 primary/cursor 光影影响范围求交（同底栏指示器模式 `:8963-8993`）。
- 视口：正常态几何已夹在 `baseBounds` 内 → 不扩张。Fan/选中胶囊：加入 `visibleContentBounds`，并登记到 `reserveAnimationEnvelope`（Back 峰值包络），首帧即保留完整空间；收起动画结束后从内容边界移除，视口按现有 `viewportController` 收缩（settle 规则）。过渡帧全量脏区覆盖旧/新视口并集，避免残影。
- 命中：呈现成功后发布指示器自有的命中快照（类似 `eraserAttribute.PresentedRegions()`）；在 `Bar.Interaction.cpp` 主按钮命中块（~6099）**之前**测试，命中则消费事件。
- 交互规则：
  - Expanded 胶囊点击 → Collapsing（通知类 → Dismissing）。
  - 单个 Docked 圆形：悬停/按压视觉沿用 `StartBarButtonHoverVisual` / `RetargetBarFixedButtonVisual` 曲线与 `SuppressHoverUntilPointerMove`；点击：有动作（新版本）→ 执行动作；否则无副作用（仅视觉反馈）。
  - 多个 Docked：点击任一 → Fan 展开；Fan 中点圆形 → 选中展开其胶囊（新版本圆形改为执行跳转并收起 Fan）；再点同一个或其他 → 收起选中；点主按钮 / 主栏其他区域 / 3s 无圆形交互 → 收起整个 Fan。
  - 指针会话与笔触觉遵循 `ui-pointer-session.md`、`pen-haptics.md`。

## 8. 状态源接线

| 源 | 注册 | 注销/更新 | 线程 |
|---|---|---|---|
| PPT/WPS | `Bar::PublishPptSession` 收到真实开始边沿（`changed`）时，按 `ppt_software` 选 Kind/颜色，key = `localSession` | 结束边沿 → Retire；同会话重新锚定不触发 | PptInfo 线程 → 队列 |
| 超级置顶 | 启动完成、Bar 就绪后若 `hasSuperTop` → Publish 一次 | 无（进程内常驻） | 主线程 |
| 新版本 | 渲染线程无法直接得知状态变化 → 新增轻量轮询器：Bar 侧按 1s 截止唤醒读取 `AutomaticUpdateState`（只读；该全局非原子，读取时容忍撕裂：只用于展示，异常值忽略）；`UpdateNew`/`UpdateRestart`/`UpdateDownloading(>0%)` → Publish；回到 `Latest`/`NotStarted` → Retire | 进度见 §5 | Bar 渲染线程（只读） |
| 调试 | 设置调试页按钮 → 导出函数入队 | 删除最新持久调试条目 | 设置线程 → 队列 |

- 新版本是否"静默"：读取 `GetUpdateTargetSnapshot().enableAutoUpdate`（已有 `setlistUpdateMutex` 保护）。
- 新版本动作：点击 → `Inkeys::UI::Setting::ShowSoftwareVersion()`。

## 9. 设置 / 配置 / i18n

- `main.json` schema（`Other.Config.cppm`，UI3 分组下新增 `StatusIndicator` 组）：`Enable`、`PptSlideshow`、`SuperTop`、`NewVersion`，默认 true。
- 迁移：`ReadAll` 后若 `StatusIndicator.SuperTop` 键缺失且旧 `deploy.json` 读到 `PlugIn.SuperTop.Indicator`，则用旧值初始化一次并写回 main.json；旧键之后不再读写（`SetListStruct` 字段保留以兼容文件读取，不再使用）。迁移需能区分"键缺失"与"默认值"——在 `Inkeys::Config` 读取路径上记录该键是否存在（实施时确认现有 schema 是否已有 presence 信息，没有则在迁移点直接检查 JSON）。
- 启动同步：`IdtMain.cpp` 在 `ReadAll` 后调用 `Bar::SetStatusIndicatorOptions(...)`（同 `SetThemeMode` 区域）。
- 设置页：`GeneralDraft` 末尾追加 `statusIndicator`、`std::array<bool,3> statusIndicatorTypes`；`GeneralEvents` 追加对应 changed；`RenderGeneral` Appearance 分节末尾 `SectionHeader` + `ToggleRow` + 条件 `CheckboxGroup`；apply：写 config → `SetStatusIndicatorOptions` → `QueueConfigWrite()`。
- 插件页超级置顶：删除 `##super-top-indicator` 展示行（及其 i18n 键若无其他引用）。
- 调试页：`SectionHeader` + `ButtonGroupRow`（添加随机 / 删除一个；满 25 时添加禁用，0 个调试条目时删除禁用）。
- 跳转：`SettingOpenTarget::SoftwareVersion`、`SettingOpenNavigation::Action::SoftwareVersion`，`Consume` 接受两种目标并记录目标；dispatch `navigateTo({PageId::Version})`；`fadeOnly` 的"仍在目标页"判定按记录的目标匹配；导出 `ShowSoftwareVersion()`。扩展 `CheckSettingOpenNavigation` 测试。
- i18n：`UI.Bar.StatusIndicator.{Slideshow,SuperTop,NewVersion}`、`SettingsUI.Regular.Appearance.StatusIndicator.{Section,N,E,Types,TypePpt,TypeSuperTop,TypeNewVersion}`、`SettingsUI.DebugSoftware.StatusIndicator.{Section,N,E,Add,Remove}`；zh-CN → `i18n.ps1 sync` → 补全 10 种语言 → `check`。

## 10. 兼容性与风险

| 风险 | 对策 |
|---|---|
| 主按钮形状每帧变化 → 环几何缓存抖动 | 独立缓存按 (w,h,n) 量化到 0.25 DIP；进度段由完整路径截取 |
| 正常态 / Fan 切换导致视口跳变或残影 | Fan 进出场登记包络；过渡帧旧新视口并集全量脏区；回归调试页"脏区可视化" |
| 指示器拦截主按钮点击 | 命中仅用呈现快照中的圆形/胶囊精确形状；Fan 收起时 50% 透明的小圆形区域外不拦截 |
| `AutomaticUpdateState` 非原子 | Bar 只读、只用于展示；不改更新模块写入方 |
| 空闲功耗 | 呼吸/滚动/进度只在其显示期内请求帧；Docked 稳态零帧需求，仅截止唤醒 |
| 25 个条目性能 | 文本布局缓存（`formatCache`）+ 圆形批量绘制；以调试页 25 个压测帧时间 |

回滚：功能整体受总开关控制；代码以单独提交分阶段落地（见 implement.md），可按阶段回退。

## v2 重设计（2026-11-04，取代上文 §4 布局、§5 线宽、§6 绘制与 §7 中单圆形规则）

依据 prd “Decisions v2” 与已确认预览稿 `research/indicator-v2-preview.html`。

### 布局（`Bar.StatusIndicator.h`，纯函数）
- 输入新增 `mainN`（超椭圆指数）与 `below`（0 向上 / 1 向下，倒转跟随 `widgetPosition.primaryBar`，拖动中锁存，换边由 `orientation_` 弹簧插值）。
- 胶囊：高 30、圆角 6、左侧 18 DIP 图标方块、图文间距 6、13 DIP 粗体、内边距 (30-17)/2、宽上限 160；水平居中主按钮，竖直中心 = `StatusIndicatorEdgeY`（上/下边框）。后排与队首等宽（`width` 弹簧），向外每层错开 4 DIP，最多 3 层。
- 停靠方块：18 DIP、圆角 4、图标 12；中心落在超椭圆边框 `x = 右端 - 14` 处（`StatusIndicatorSuperHalfHeight`），旧的向左错开 6，夹入 baseBounds；越界（着陆轻弹）时计为 overflow。
- 同心弧：方块 28 DIP 紧挨（间距 4），首层半径 = 超椭圆 50° 外径 + 6 + 14，层距 32，单层跨度 130°，以正上/正下为中心对称，index 0 最新在最右；工作区不足时镜像。弧上选中以方块为中心展开为胶囊。
- 输出每项圆角矩形 `bounds/radius`、图标中心与尺寸、文字区/滚动/两端渐隐标记、整项 `opacity`、`role`（Capsule/Dock/Fan）与 z；`reserveBounds` 为出现回弹峰值、飞行弧线与同心弧（含选中胶囊）的首帧预留包络。

### 收起动画
- `morph`（Morph 弹簧 ω20 ζ0.62）0→1：宽度向左端图标收缩，文字随之淡出；`flight`（Flight 弹簧 ω13 ζ0.74）延迟 0.1s 后 0→1：沿二次贝塞尔飞向槽 0，控制点向外抬起 12 DIP，尺寸 30→18、圆角 6→4，透明度在后 45% 降到 0.5。
- 越过 0.97 才“着陆”：此刻分配 dockSerial 占槽，其余方块随之让位，并给 `bump` 弹簧（ω26 ζ0.35）一次冲量形成轻弹。延迟在帧内到期只推进剩余时间，30/120Hz 同一时刻几何一致。
- 单方块点击 `ClickDocked` → 重新展开：flight 立即→0、morph 延迟→0（逆过程）；到达锚点后为可点击胶囊，再点、点外部或 3s 后按收起曲线返回。

### 绘制（`Bar.StatusIndicator.cpp`）
- 表面：`BarUiShapeClass`（themeSurface + residentSurface + 浅色缩放），与主按钮同一材质权重；用户背景透明度与 PPT 闲置淡化由常驻表面规则统一处理（指示器绘制移入主按钮 idle fade 作用域）。
- 边框：1 DIP 强调显示色 `DrawPointLightFrame`（点光同主栏），透明度 0.9×常驻内容透明度（含闲置淡化）。强调显示色 = OKLCH 按材质校正（深色 L≥0.72、浅色 L≤0.52）。
- 遮挡式组合成：每项用 `rect EXCLUDE ∪上层外形` 几何遮罩 + 整项 opacity 单次 PushLayer；下层外露部分完整绘制，无逐元素半透明叠加。
- 文字：几何遮罩 = 胶囊内侧圆角，滚动时线性渐变 opacity brush 两端渐隐；图标/文字乘常驻内容透明度。
- 悬停/按压：方块内 hover 填充 + 按压缩放，复用主栏按钮曲线；胶囊无按钮式反馈。
- 环/进度环：1.5 DIP；仅 `BarUiEdgeLightingEnabled` 时以几何预模糊遮罩 + 强调色实色画刷绘制光晕，再叠点光细线。
