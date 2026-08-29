# PPT 翻页控件主栏化：实施计划

阶段 0–10 记录既有实现与设备验收历史；本轮启动输入、键盘式长按与触摸转译修正由阶段 11 覆盖，不回写已完成阶段的勾选状态。

## 0. 开工门禁与基线

- [x] 在开始产品代码前重新阅读收敛后的 `prd.md`、`design.md` 和 `.trellis/spec/native-desktop/rendering-and-ui.md` PageControl 合同。
- [x] 为主栏当前 `oneOne/twoOne/twoTwo` 布局建立共享 metrics 基线，并在重构前运行 ARM64 `--no-window`；hover/press/draw/hit 通过 Main Bar 与 Scene 的同一产品入口结构性复用，不复制输出模型。
- [x] 静态记录并清理当前错误所有权：`BarSurfaceScene` 的本地 hover/pressed、SVG/文字偏移、独立 draw/hit，以及 PageControl 的 Bar 障碍/Whiteboard drag 路径。

**Rollback 0**：仅增加等价基线测试；若测试无法稳定描述主栏现状，停止抽取并先补齐可测接口，不修改 PageControl。

## 1. 抽取主栏唯一按钮/背景运行时

- [x] 从 `Bar.RenderLoop.cpp`、`Bar.Interaction.cpp` 和现有 Rendering/Button 类型中抽取共享 metrics、hover/press、draw 与圆角 hit；动画值继续使用同一 `BarUi*` advance，宿主只保留坐标映射和 present damage union。
- [x] 抽取主栏背景 draw，并让主题、圆角、边框、光源参数直接读取主栏单一来源；Main Bar 和 PageControl 保持独立资源。
- [x] 先把 Main Bar 迁移到共享入口，并以共享 metrics/headless、完整构建和静态调用点证明 `oneOne/twoOne/twoTwo`、5 秒 hover、按压衔接与 draw 合同保持。
- [x] PageControl 接入前后均执行完整 ARM64 构建和 headless 测试；Main Bar 与 Scene 最终调用相同符号。

主要文件：`Bar.Button.cppm/.cpp`、`Bar.Layout.cppm`、`Bar.Interaction.cpp`、`Bar.RenderLoop.cpp`、`Bar.Rendering.cppm/.cpp`、必要的新 Bar module、Bar headless tests。

**Rollback 1**：共享入口与 Main Bar 迁移作为独立检查点；若等价断言失败，只回滚该层，不触碰 PageControl/PPT/Whiteboard。

## 2. PageControl 稳定按钮与内容接入

- [x] 每个 surface 建立稳定 Previous/Page/Next `BarButtonClass`；Bottom 跨 PptCompact/WhiteboardExpanded 保持实例，Middle 仅切 Hidden/PptCompact。
- [x] PageControl 只计算 surface 拓扑、横竖/镜像锚点和 PPT 专属 Page 内容请求；标准按钮 metrics、hover/press、draw 和圆角 hit 委托共享 Bar 运行时，宿主自行映射 visual damage。
- [x] 实现横向混合字重页码整体测量、竖向上下页码和 Whiteboard 标准 `2x2` 内容槽，删除 PageControl `±13` 等固定偏移，并保留 PPT `-`/`/-` 占位与 Bottom/Middle `9999/999` 显示上限。
- [x] Previous/Next 保留同一 `barMore` SVG；仅 Arrow/Add 真实语义变化时调用共享内容转换，并把几何、SVG、标签加入同一批次。
- [x] PageControl 外层背景改用共享主栏背景入口；外框/按钮圆角、边框和内边距直接读取主栏单一来源。
- [x] 将 `BarSurfaceScene` 收窄为资源/拓扑/输入适配器：移除本地 hover/pressed visual state、专用 draw 和矩形按钮 hit，按钮行为委托共享 Bar 入口；PPT-only DragHandle 与 Whiteboard Freeze 保持独立语义。

主要文件：`PageControl.cppm/.cpp`、`Bar.Scene.cppm/.cpp`、Bar shared runtime、`page_control_tests.cpp`、`whiteboard_ui_tests.cpp`。

**Rollback 2**：PageControl 接入可单独回滚到共享运行时已存在、Main Bar 已迁移的状态；不得恢复分页专用按钮状态机作为兼容分支。

## 3. 输入与工作区状态机

- [x] DragHandle 改为 PPT-only 独立 shape/hit region；Whiteboard 不创建该命中，Page 永远 no-op。
- [x] 实现 Enter 起始 capture 撤销/命中关闭、DragHandle 槽位同批收拢淡出，以及 Exit 同批展开淡入并在成功稳定呈现后恢复命中。
- [x] PPT 保留 drag/scale/persist/long-press/wheel/keyboard flash；Whiteboard 只保留普通 click/tap 与标准 hover/press，拒绝 PPT 专属输入。
- [x] Whiteboard switching 只关闭 interactive，锁存未变化的 Previous/Arrow/Add/文字视觉；反向重入从当前背景、按钮、DragHandle、内容和 HWND bounds 重定向。
- [x] PPT 底栏可见时从当前实际位置同步移动/形变到 Whiteboard 固定位置；底栏不可见时直接使用最终几何渐显；Exit 返回 PPT 最新运行时位置。

主要文件：`PageControl.cppm/.cpp`、`Whiteboard.cppm/.cpp`、`Ppt.cppm/.cpp`、page/whiteboard input tests。

**Rollback 3**：保留稳定按钮和共享运行时，单独回滚工作区事务；不得重新引入双 renderer owner 或隐藏中间 owner。

## 4. PPT 碰撞与位置隔离

- [x] 从 `ResolveRuntimePageControlLayout` 及调用方删除 `mainBarObstacle` 参数、`MainBarObstacle()`、Bar HWND 查询和 Bar 布局变化通知。
- [x] Whiteboard 固定布局不再读取 `PptLayoutState`；删除 Whiteboard drag clamp、persist 和碰撞测试。
- [x] 手动拖动只约束命中 pair 与另一 pair，碰撞时保留最近 feasible candidate，不推动另一 pair。
- [x] 自动纠偏固定 bottom 优先、middle 最近位置/极端运行时缩放回退；断言输入快照和保存配置不变。

主要文件：`PageControl.cppm/.cpp`、可能的 `Bar.WindowGeometry.h`/通知调用、`page_control_tests.cpp`。

**Rollback 4**：碰撞重构独立于按钮视觉；失败时恢复 PPT-only 四控件旧求解，不得恢复 Bar 障碍。

## 5. 清理、规范与回归

- [x] 删除 PageControl 对 WidgetSpec 标准按钮 offset/size 的赋值、local hover/pressed visual state、专用 draw、矩形 button hit 和独立 `240ms` duration；保留 generic WidgetSpec 作为共享 metrics 的 surface adapter。
- [x] 更新 `.trellis/spec/native-desktop/rendering-and-ui.md`、任务 PRD/design/implement 与代码中的稳定注释；静态确认不再存在 Whiteboard drag 或 main bar obstacle 产品合同。
- [x] 更新测试：共享 Bar metrics、SVG 语义、输入矩阵、固定 Whiteboard 位置、divider Drag 间距和 PPT-only 碰撞；既有 EndShow/A2、状态反向与窗口生命周期测试继续执行。
- [x] 回归确认 `PptExitShow` 不返回生产路径，EndShow/A2、旧 JSON、COM/WPS、`PptInfoStateBuffer`、页级墨迹与 owner/Z 序不变。

## 6. 质量门

- [x] `git diff --check`。
- [x] 静态 `rg` 确认 PageControl 不再设置标准按钮 icon/text offset，BarSurface 无本地 hover/pressed visual state，碰撞路径不读取 Bar HWND。
- [x] 使用 ARM64 版 `MSBuild.exe` 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64`，超时不少于 5 分钟。
- [x] 运行 `Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window`。
- [x] 记录未执行的真实 PowerPoint/WPS、鼠标/触摸、光影、DPI 与连续工作区切换设备验收；按仓库约束未启动 GUI。

## 7. 设备验收后修正

- [x] 修复共享 draw 的隐藏父缓存前置条件：显式 `inherit` 同步按钮父坐标，SVG 与主/次文字通过同一轻量计算解析；Main Bar dirty 同样调用该入口。
- [x] 删除 Surface 本地第三光源 prepare/reset，将 Main Bar 的最终 cursor 坐标、半径、强度和可见性纳入共享光源快照。
- [x] 四个 PageControl HWND 加入 Main Bar 唯一接收窗口和可见区域集合；真实鼠标进入/离开通知既有 `Dormant/Inside/Grace`，成功窗口提交才发布边界。
- [x] 增加非零父原点与屏幕光源映射 headless 回归；重新通过完整 `Debug|ARM64` Solution 构建和 ARM64 `--no-window`。

## 8. 第二轮设备验收修正

- [x] 删除分页背景对 `BarButtonCursorLightIntensity` 的误用，使 PageControl 外框和 Main Bar 外框保持相同第三光源强度比例。
- [x] 恢复 PPT DragHandle、Page 和非箭头背景的成对拖动；Previous/Next 保持纯按钮，Page 在越过系统 drag threshold 后取消 press 并转为拖动，Whiteboard 输入不变。
- [x] owner WndProc 对 `presentationMutex` 只允许 `try_lock`；每次移动先发布 latest-wins 绝对候选并请求命中 pair，锁失败后由渲染提交继续消费，禁止依赖下一条可能被合并的 `WM_MOUSEMOVE`，也禁止与同步 Window Service 提交形成等待环。
- [x] 拖动 mailbox headless 注入一次锁竞争，覆盖候选覆盖、双 HWND 确认、松手所有权与取消回滚；PageControl 临时控制台日志无开关记录按下、发布、锁忙、owner/render 消费、成对直移和持久化结果。
- [x] 为 Page 数值启用共享 Scene 即时内容策略，取消旧 transition 并同帧更新字符串/测量槽；Arrow/Add SVG 与标签仍走共享转换。
- [x] 将 PPT 页状态发布改为 COM 最多 `50ms` 有界检查 + Draw3 `runtimeRevision` 事件等待；revision publish/stop notify 与 waiter 使用同一 mutex 建立握手，保持 `PptInfoStateBuffer` 和 COM ABI 不变。
- [x] 补齐背景强度、拖动分类/阈值、即时文字和 Draw3 runtime revision 回归；再次通过 `git diff --check`、ARM64 完整构建和 `--no-window`。

## 9. 第三轮设备验收拖动卡顿修正

- [x] 删除 `BeginDeferWindowPos/DeferWindowPos`，改为顺序 `SetWindowPos` 两窗；第二窗失败时回滚第一窗并记录 first/second/rollback error。
- [x] 纯平移由 candidate logical bounds 与稳定 presentation outset 直接生成绝对目标；成功路径只更新 bounds/mailbox/revision/共享光源接收区，不调用 `ApplySceneBounds` 或请求 pair。
- [x] publication 不自动请求 pair；仅锁竞争、非纯平移或窗口移动失败保留 latest-wins pending 并显式请求 render fallback，松手一次 `RequestAll` 吸收最终 Scene/layout。
- [x] mailbox/layout/bounds 先于 release revision 发布，所有 revision 写点由 `dragCommitMutex` 串行；渲染在 `ConfigureSurface/PresentScene` 前及窗口提交后执行两道 stale gate。
- [x] 通过 `git diff --check`、ARM64 完整构建和 `--no-window`，且未启动 GUI。

## 10. 原始交互与 Draw3 绘制活动回归修正

- [x] 为 `PptCallbacks` 恢复 `ViewShow`，Page 阈值内短按打开预览；DragHandle/Page/真实圆角背景的拖动区域及 Arrow 纯按钮边界保持。
- [x] 恢复 Arrow Down 立即翻页、`400ms` 后按 `15ms` 检查节拍长按重复，并由 PPT 快照传播 `EnablePageButtonLongPress`；移出/Up/cancel/切换立即停止。
- [x] 删除键盘 Hook 与滚轮合成的 Arrow pressed 闪按及其 external press 状态/测试；真实 Pointer hover/press 不变。
- [x] 有效 PPT Pointer Down 调用 `PromotePptWindow`，维持目标高于其他 PPT、低于 Bar且不激活。
- [x] 为 Draw3 controller/Host 增加物理 contact 聚合活动通知，覆盖 Down 后提前 continue、多 contact 去重，以及 stop/异常 active 清理。
- [x] 产品层将 Draw3 Started/Ended 转发给 Bar；首次 Started 收起主栏次级界面但不改变主栏 fold/PageControl，并无条件让第三光源进入带 wait-for-leave 门禁的 `Dormant`。
- [x] 更新 PageControl/Draw3 headless 回归，执行 `git diff --check`、ARM64 `InkeysRepo.sln` `Debug|ARM64` 完整构建和 ARM64 `InkeysHeadlessTests.exe --no-window`；不启动 GUI。

## 11. 启动输入、键盘式长按与触摸转译修正

- [x] Hidden 初始 Scene 直接提交 opacity `0`；可见/退场 transition deadline 纳入 PageControl `Continue` 判定，保证没有 Bar/光源外部请求时到期帧仍自行重算并解除 `inputLocked`。
- [x] Arrow Down 快照 `SPI_GETKEYBOARDDELAY/SPI_GETKEYBOARDSPEED`，按 `250ms * (delay + 1)` 与 `2.5..30 次/秒` 解析 timing；保留立即一次、配置门禁、capture/hit 停止和 COM outstanding 合并，不追赶迟到节拍。
- [x] PageControl WndProc 返回与 Bar 一致的 Tablet 手势禁用标志；补齐 primary 缺失 fallback、活动 id 替换 cancel、primary/最后坐标锁存和单触点 Move/Up，复用现有 mouse press/drag/long-press 路径。
- [x] Headless 覆盖 timing 边界、transition deadline 续帧和触摸锁状态；静态确认四个 HWND 已注册 touch/禁用边缘手势且系统兼容 mouse 去重仍存在。
- [x] 对照更新后的 code-spec 执行 `git diff --check`、ARM64 `InkeysRepo.sln` `Debug|ARM64` 完整构建和 ARM64 `InkeysHeadlessTests.exe --no-window`；不启动 GUI、不提交 commit。

## 12. EndShow 图标与 PPT 结束页内容切换

- [x] 新增符合主栏 `24x24`、圆角端点/连接和相近线宽的 `barEndShow` SVG，注册为 `UI` 资源；Main Bar A2 EndShow 改用该 SVG 和主题 `TextPrimary`，删除该按钮的 `ppt3` PNG 特例。
- [x] 在 PageControl 单一内容策略中识别 `totalPage > 0 && currentPage < 0`，让 Bottom/Middle 的稳定 Next 实例使用 `barEndShow` 与 `0°`；有效页恢复后回到各 surface 的 `barMore` 角度，点击/长按继续调用 NextPage。
- [x] 复用 `BarSurfaceScene::SetWidgetState` 既有 `TransitionToResource` 中点动画，不新增分页专用动画；补齐结束页判定、资源/角度矩阵、未知状态负向和稳定回调回归。
- [x] 更新 code-spec，执行 `git diff --check`、EOL/BOM 审计、ARM64 `InkeysRepo.sln` `Debug|ARM64` 完整构建和 ARM64 `InkeysHeadlessTests.exe --no-window`；不启动 GUI。
