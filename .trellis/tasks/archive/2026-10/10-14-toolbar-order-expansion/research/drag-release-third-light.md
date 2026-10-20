# Research: 主按钮拖动松手时第三光源闪烁

- Query: 主按钮拖动完成抬手瞬间，动态边缘光影（第三光源）闪烁后恢复；调查坐标链和最小修复、无窗口回归。
- Scope: internal
- Date: 2026-10-14
- Active task: `.trellis/tasks/10-14-toolbar-order-expansion`
- Branch/baseline: 主会话提供 `feature/mode` / `35afa7ee`；本研究角色未执行 Git 操作。

## Findings

### 已确认的源码链

1. `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:351` 的 `BarScreenToLayout` 读取最后成功呈现 `BottomDockPresentedSnapshot()`，用该快照的 `monitorOrigin` 和 `directTranslation` 转换光标屏幕点。
2. `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h:264` 的纯转换合同为 `layoutPoint = screenPoint - monitorOrigin - presentedTranslation`；layout 坐标仍为物理像素，不在这里除以 zoom。
3. `Bar.Interaction.cpp:6310` / `:6372` 的 Inside / Grace 更新在 `borderCursorLightMutex` 内同时缓存 `borderCursorLightPoint`（以上旧呈现坐标域的局部点）、`borderCursorLightScreenPoint`（真实物理屏幕点）和 serial。位置只因自然 WM_MOUSEMOVE/WM_INPUT 及激活更新；界面布局重基准没有使缓存自动重算。
4. `Bar.Interaction.cpp:6714` 的 Seek 领取直移 phase；`:7176` 在 directWindowDragMutex 内通过 SetWindowPos 直接移动最后成功位图，更新成功快照和接受区屏幕缓存，不修改第三光源缓存局部点。`:7333` 左右发布 release tuple / `bottomDockDragActive=false`，`:7338` 释放直移所有权并请求渲染吸收。
5. `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:13516` 的 render 接管分支 exchange 目标 translation 为零，把主按钮几何加 `translation / zoom`，同步 displayCenter，调用 PositionUpdate，并平移 committed dirty/visible 缓存。`:13565` 将成功快照 translation 重基准为 `presentedBeforeAbsorb - absorbedTranslation`，保留实际 HWND 尚未追上目标时的残差。
6. 同文件 `:13647` 起重新采集同一偶数 bottomDockTransitionSerial 的完整输入 tuple；`AdvanceLayoutAndInherit` 的 `:7591` 将 `frame.bottomDockTransitionTranslation` 保存到 `state.bottomDockFrameTransitionTranslation`，后续几何、viewport、提交都消费同帧基准。
7. `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:436` / `:624` 的 `PrepareFrameLighting` 当前直接将缓存 `borderCursorLightPoint` 赋给 `frameCursorLight`，同时读取 screenPoint，但没有依据当前布局坐标域重算。Render 接管已经移动几何而光标局部缓存仍处于旧域，构成实际坐标失配。
8. `Bar.RenderLoop.cpp:10148` 根据该 frameCursorLight 对主体、刚性抓手、刚性覆盖层作光源逆映射；这些映射不能纠正一个错误的基准输入。PointLight 绘制及裁剪在 `Bar.Rendering.cpp:2370` 依据光源与扩展控件外框相交，失配大于半径时可直接令贡献消失。
9. 新鼠标消息随后经 `BarScreenToLayout` 使用重基准后的成功快照，更新缓存并恢复正确光源落点，与用户所见“抬手时闪一下随后正常”吻合。

### 因果置信度与具体反例

- **已确认缺陷**：缓存局部光标点跨布局坐标重基准被直接复用，不需要 D2D 失败、停靠、换边、五秒生命周期或新排序设置即可发生。
- **高可信用户症状解释，尚未实机视觉确认**：松手吸收位移的第一帧把光源错放，随后系统产生的移动或 Raw Input 再校正。
- 用 X 轴简单例子即可建立确定性测试：monitorOrigin=0，旧几何中心=600，已呈现 directTranslation=300，光标停在屏幕900。输入缓存 layoutLight=600。吸收后新几何中心=900、translation=0；现实现仍绘制光源600，屏幕落点也变成600，距目标300，超过240px半径。下一次输入则缓存900。**即使没有下一条鼠标消息，正确帧也应在屏幕900；现实现可以持续错误直到新输入。**
- 原因是坐标而非生命周期：release 没有 SuspendBorderCursorTracking，也没有清零强度；禁止用暂时关闭第三光源或重启淡入作为修复。

### 建议的最小产品修复

1. 让真实 `PrepareFrameLighting` 显式取得本帧的布局屏幕原点参数，例如 `POINT layoutScreenOrigin`；生产调用在 `PrepareLightingAndDemand` 提供：
   `state.monitorOrigin + state.bottomDockFrameTransitionTranslation`。
   此值在渲染几何推进后、光影和 dirty 计算前已确定，来自同一稳定偶数 tuple；不要重新读取最后成功呈现快照来替换它，也不要独立读取 live translation 原子来拼帧。
2. 保持在 borderCursorLightMutex 内获取真实 screenPoint、serial、available/ready，随后用本帧参数计算 `frameCursorLight = frameCursorScreenLight - layoutScreenOrigin`。屏幕坐标、半径、生命周期强度和输入 serial 不因布局重基准而修改。
3. 原 `FinishLightingFrame` (`Bar.Rendering.cpp:482`) 已比较上一/当前局部光源点并标记 CursorLight changed；即使输入 serial 不变，坐标域改变仍能产生正确 damage。现有 GetFrameCursorLightDamageBounds 和 tracker 旧新边界并集继续适用。
4. 继续让主体/抓手/覆盖层在绘制阶段调用现有逆映射；不要把 inverse transform 移入输入缓存。坐标转换不额外除以 zoom；zoom 仍只负责几何及240px半径。
5. 不建议只在 absorb 分支给输入缓存加 translation：与输入线程的新点发布会发生先后竞争，也无法覆盖 monitorOrigin/DPI 过渡，不是稳定的 owner 边界。也不建议调用 GetCursorPos 逐帧补救，Dormant 不应新增全局轮询。
6. 原 borderCursorLightPoint 可以暂时保留以保持最小 diff，输入路径仍按既有同步发布；本次不要求删除缓存、重构所有消息或接口。

### 呈现 race 的边界（审查时须保留）

- `Bar.RenderLoop.cpp:12961` 的最终提交在 directWindowDragMutex 内重验稳定发布；`Bar.BottomDock.h:1389` / ResolveBarBottomDockFramePresentation 可能选择 frameTranslation（未提交形态屏障）、最新同帧 translation、或已经直移成功的 presentedTranslation（普通过期帧）。遇更新形态或奇数发布会 defer。
- 建议光影参数必须使用与本帧几何一起捕获的 translation，不读取提交时的 live translation 去改变已经绘制的光源。此次确定性松手例在没有新输入时 tuple 稳定，frameTranslation 与最终提交基准一致。
- 对“计算后输入再次直移导致普通过期帧使用 presentedTranslation”的完整呈现一致性应检查现有断言，避免新修复破坏既有屏障/成功快照合同；本研究没有实证该 race 是此次独立闪烁的根因，不扩大修改呈现策略。

### PageControl / 分页共享快照

- `Bar.RenderLoop.cpp:8127` 发布 `SnapshotFrameLighting()`；cursorScreenX/Y 直接取 `cursorScreenLight`，不是从 localLight 加 live translation 反推。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp:226` 的 ResolveBarSurfaceScreenPoint 从共享屏幕光源点与各自逻辑 HWND 外框/呈现 outset 计算分页局部点，分别映射到 Surface，使用共享radius/intensity。
- 因此原缺陷直接影响 Main Bar localLight；分页的 cursorScreenLight 原本已有正确坐标，不应因为主栏吸收位移而变化。本修复应保留共享位置、半径、强度、可见性，不新增分页独立输入或生命周期。
- 验证“主栏重基准后 screenLight 仍固定、分页局部映射与光圈不跳变”足以防止修复引入分页回归；不要把分页说成原本同样错误的已确认症状。

### 可执行的无窗口回归建议

- 入口：`Inkeys.exe --bar-eraser-offscreen-test`，该 CLI 在初始化正常 HWND 之前进入现有 `Bar.EraserAttribute.Test.cpp:109` 测试；现有共享WARP/D2D离屏初始化可复用。
- `BarUISetClass` 的第三光源输入字段为 protected (`Bar.Main.cppm:872` 至 `:955`)。可在测试内定义小型派生 fixture 只为 SeedCursor 方法暴露测试数据，然后直接调用 fixture.spec 的真实 PrepareFrameLighting。不需要添加公共产品注入 API，也不需要启动 HWND/输入线程。对象较大时放堆上。
- 先用原实现编译/运行真实测试，断言失败；再补产品修复并再次构建/运行，确认转为通过。仅测新纯 helper 不能证明真实 PrepareFrameLighting 没有继续用旧缓存。
- 先让第三光源完成淡入到稳定强度，再冻结 screenPoint、input serial、ready/available；分别按拖动前、带translation的拖动、松手几何吸收后的新origin调用。断言localLight + frameOrigin ==固定screenPoint，强度/半径/可见性不变；整个过程不送新WM_MOUSEMOVE/WM_INPUT。
- 至少覆盖：(1)零/正/负translation的两轴；(2)负monitorOrigin、切monitorOrigin；(3)0.6/1/1.5/2实际zoom与240×zoom半径；(4)目标位移与实际已呈现位移不同，吸收后残差不为零；(5)无input serial变化的连续120帧不反复重启fade；(6)总光影关/动态关/Dormant及动画关沿旧门禁；(7)PageControl共享screenLight/radius/intensity保持；(8)light changed/dirty覆盖旧新局部位置。
- 像素证据建议：固定同一控件的同一边框点，第三光源稳定亮度、关闭该shape第一光（framePrimaryLightEnabled=false），对吸收前后D2D离屏像素比较。真实frameOrigin变化时控件局部几何也按同位移平移，可通过D2D目标平移将采样边框映回同点；比较radius/intensity/中心及允许AA误差。不能用直接SetFrameLightingSnapshot代替真实Prepare入口来冒充验证。
- 后续主会话执行完整solution Debug|ARM64和headless --no-window；此research角色不构建、不运行GUI。

### 函数调用点及补充边界

- 当前 PrepareFrameLighting 全部调用点为 `Bar.RenderLoop.cpp:8120` 的生产调用，以及 `Bar.EraserAttribute.Test.cpp:536`、`:623`、`:715` 的已有离屏调用。生产必须显式传同帧origin；旧离屏只用原点场景，可传 `{}`；新增静止光标回归必须调用真实Prepare且传真实变化origin，不使用纯helper或SetFrameLightingSnapshot代替。
- `Bar.Rendering.cpp:482` 的 FinishLightingFrame 在当前/上一光源可见时比较局部PointChanged；即使 serial 一直不变，吸收后local变动也会置frameCursorLightChanged。`Bar.RenderLoop.cpp:8207` 把该结果转成稳定CursorLight视觉键；`:9041` 获取影响圈，`:9433` Observe当前光源damage；tracker管理旧/新联合。沿用此路径，避免新增第二套脏区计算。
- 取消/失败手势覆盖：正常Seek release、touch cancel、SetWindowPos失败回滚（`Bar.Interaction.cpp:7278` 起使用最后成功tuple）均走生产frameOrigin。回归可固定同一screenPoint/serial，分别设置失败后真实呈现残差和撤销translation，验证不会把目标translation误当实际已显示位移；不需要在各退出分支补写缓存。
- 证据不足的普通stale presentation仅作为审查/回归边界：基于既有ResolveBarBottomDockFramePresentation断言其translation选择与屏障行为保持；不要因此改变呈现策略或声称另一个race也已修复。

## Files Found

| File | Purpose |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | 鼠标第三光源唯一生命周期及缓存发布；Seek直接窗口拖动/松手发布。 |
| `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h` | screen/layout物理像素转换和吸收后残差纯公式。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 位移吸收、同帧tuple、光影调用/共享快照、dirty与最终ULW提交。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` / `.cppm` | 第三光源帧状态、渐变画刷、inverse落点、强度/damage判断。 |
| `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` | 偶数事务、release defer及最终呈现translation策略。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | 第三光源受保护输入数据和成功呈现快照。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp` | 分页消费屏幕点的共享光影映射。 |
| `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp` | 已有真实Bar/WARP无窗口离屏测试入口，可覆盖真实Prepare入口。 |
| `InkeysHeadlessTests/window_geometry_tests.cpp` / `bar_bottom_dock_tests.cpp` | 已有screen/layout/残差、提交策略及刚性/主体光源逆映射纯回归。 |

## Related Specs

- `.trellis/workflow.md`: 研究持久化、按当前任务延续；此research角色只写research。
- `.trellis/spec/native-desktop/cpp-conventions.md`: 同业务/帧快照、共享数据唯一同步owner、最小diff及原编码/CRLF。
- `.trellis/spec/native-desktop/rendering-and-ui.md:946`: 同偶数tuple与direct-drag吸收/release帧合同。
- 同规范 `:954`: 绘制、dirty、PointLight逆映射及第三光源接受区按主体/抓手分类。
- 同规范 `:1072`: 画刷位置/半径/透明度每帧更新，不冻结布局或最终光影帧。
- 同规范 `:1176`: Dormant/Inside/Grace唯一状态机，240×zoom光圈、共享屏幕快照、接受区不得改变亮度。
- 同规范 `:1291`: 总开关/动态开关/动画门禁保持，不能用禁用第三光源规避问题。

## External References

- 无。本轮为内部源码链调查，没有引入外部API/版本假设或依赖变更。

## Caveats / Not Found

- 没有启动GUI或Computer Use，未声称实机闪烁已视觉重现；“已确认缺陷”指确定的坐标失配，用户瞬时恢复的消息时序为高可信推断。
- 本角色未修改产品源码、spec、任务根工件，也未构建/运行测试；真实先失败后通过由implement/check和主会话执行并记录。
- role-isolated research不读取implement.jsonl/check.jsonl；读了task PRD/design/implement和相关spec完整段落。
- 引用行号以本研究时源码为准，后续implement改动可能移动。
