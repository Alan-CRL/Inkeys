# Research: 白板主题权威状态与颜色范围同步

- Query: 白板浅/深背景颜色记忆应依据什么状态切换，怎样覆盖翻页/主题失败而避免 UI 与 Draw3 使用不同颜色。
- Scope: internal
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar
- 本轮仅调查；工具族适用范围及荧光/激光默认颜色待用户确认，不在本文代为决定。

## Findings

### 权威状态与 UI 展示（事实）

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WhiteboardModel.h:6` 的 WhiteboardTheme 包含 RGB 和纹理；默认深绿，Valid 只校验24位 RGB/纹理枚举，不限制为六个预设。
- `Draw3.DrawingController.cpp:10346` 的 whiteboardThemes 是绘制 owner 持有的每页主题状态；`:109` 首张读取已注入且合法的偏好，`:114` 新页继承当前页主题。
- `UI/Whiteboard/Whiteboard.Theme.h:12` 定义预览色板顺序；`UI/PageControl/PageControl.cpp:865` 点击只用按下时背景、纹理、generation、currentPage 构造 changeTheme 请求，没有乐观更新当前页主题。UI 样本/色板不是有效背景分类权威。
- `IdtState.cpp:1425` RequestWhiteboardTheme 经产品接口投递；`Draw3.Product.cpp:21` 先检查产品运行；Host `Draw3.Host.cpp:3230` 再发布桥接命令。投递 Accepted 只代表入队，不代表绘制 owner 接受或背景成功呈现。
- `Draw3.DrawingController.cpp:119` ApplyWhiteboardThemeCommand 验证当前 workspace=Whiteboard、非loading、generation/page准确、theme合法；`:128` 真正改变当前主题并增页 revision 后才发 whiteboardThemeEdited。重复合法请求返回 true 但不重复编辑/保存；无效请求保留原主题。
- `Draw3.DrawingController.cpp:15309` 仅真正接受命令才 publishWhiteboard；`:10590` 的 publishWhiteboard 从当前页读取主题并一次发布主题/generation/loading/unsaved/page/count/content。
- `Draw3.Host.cpp:1198` ObserveWhiteboard 同 whiteboardMutex 存这些值，之后调整输入闸门并发布 runtime revision；`:2617` RuntimeSnapshot 在同锁内读取页码、主题、代次和加载状态。
- `IdtState.cpp:3367` 稳态将 Host 快照交给 Whiteboard::PublishPageSnapshot；工作区进入准备时 `:3483` 也发布主题，但 changing=true。`Whiteboard.cpp:370` PublishPageSnapshot 在 pageStateMutex 内同时提交页号/主题/身份；主题改变会请求背景重绘。`:70` 再将这些状态发给 PageControl。

结论：颜色分类应来自 **Controller 接受的当前页主题及完整身份**，沿同源回执传播到业务和 UI；不能依据预览、请求中的 RGB 或最后一次保存的主题偏好推断。

### 页切换、加载与失败（事实）

- `Draw3.DrawingController.cpp:15710` 翻页先扩展新页主题（继承当前页），保存 dirty 页。若目标 cold，`:15715` 记录 loading 目标/代次并发 Load；队列拒绝则清 loading、标 readFailed，并发布仍然当前页的主题，**此时 currentPageIndex 尚未变**。
- `Draw3.DrawingController.cpp:10701` 起消费 cold load，只接受仍匹配 loading 目标和 generation 的结果；`:10723` 验证并解码目标页历史主题，`:10728` 成功安装目标页内容和主题，然后投 SetPage。安装失败 `:10740` 清 loading，保持旧 currentPage/旧主题，不创建空白覆盖。
- `Draw3.DrawingController.cpp:15723` 非cold/已安装的目标通过后增加 whiteboardGeneration，`:15725` 才切 currentPageIndex；`:15774` 附近 publishCurrentPageContent 再发布新当前页主题。冷加载成功安装但 SetPage 尚未消费时，不应提前按目标页主题切色。
- `Draw3.DrawingController.cpp:15755` 起恢复 GPU 内容，失败会挂 viewportRefreshPending；逻辑页/主题接受不等于像素呈现成功。不能把 Host whiteboardChanged 当作背景 Present 成功凭据。
- `Draw3.DrawingController.cpp:10659` 保存每页主题到 history extra；保存失败只标 unsaved，当前已接受主题不回滚。最后成功主题偏好由 `IdtMain.cpp:3720` 回调排入现有配置 FIFO，不做绘制线程 I/O；它不负责页切换、冷恢复或首次进入的颜色分类通知。

### 已有深浅分类（事实与建议分开）

- 未发现白板公共 IsDark 分类函数。`Whiteboard.Theme.h:21` 纹理色用 `299R+587G+114B > 128000` 判浅，浅背景用黑纹理，否则白纹理。现有六色对应前三深色、后三浅色。
- `Bar.Scene.cpp:612` 主题预览文字也使用128000阈值；`Bar.Layout.cppm:441` 则是另一种线性亮度阈值，仅用于对比文字。Setting 的 IsDarkMode 是应用主题，不能用于白板纸张分类。
- 推荐：明确一个无状态公共背景分类策略，优先与现有白板纹理128000语义一致；业务/UI/Draw3共用，按实际 RGB 分类，纹理模式不影响颜色范围。不要按“色板索引<3”推断，因为模型合法 RGB 并不只限六色。阈值相等属于深色应保留为明确边界。
- 浅背景默认红/深背景默认白是用户已给的方向；具体是否覆盖所有工具族、荧光/激光默认值仍待主会话询问结果。

### 最小同步接点与风险（推荐，未实施）

1. 使用覆盖 **首次进入、实际页切换、主题接纳、冷加载完成** 的 publishWhiteboard/ObserveWhiteboard 结果作为分类源；不能只挂 whiteboardThemeEdited（其只发真实主题编辑，不发翻页或恢复）。携带 hostGeneration/workspace identity/whiteboardGeneration/page，防迟到结果改掉另一工作区颜色。
2. 主会话可在 `IdtState.cpp:3367` 稳态回执和 `:3483` 进入准备的共同业务处理路径接纳该分类，在同状态锁内选择颜色范围并一次发布 ProductState 与主栏快照；最好共用一个幂等条件处理入口，避免同一类背景重复发布修订或重置记忆。
3. **仅异步刷新业务 getter 不足以保证下一笔正确**：Controller 更新主题/页后，IdtState 尚未回读 Host 前，绘制线程可能接受新 Down 并锁存旧 ProductVisualStyle。需设计一个有身份的样式同步/准入门或在绘制 owner 的主题/页接纳事务里完成有效色选择，并让 UI 消费同源结果；不能笼统写“回调更新一下颜色”就宣称无窗口期。不得在 Draw3 回调持 Host 锁时直接进入重型 Sync/窗口事务造成锁循环。
4. 业务接纳主题与背景 Present 成功是两个阶段。规划需明确以逻辑已接纳主题选色，并在背景/样式尚未同步时阻止新笔，或将它们纳入同一已有 ready/ACK 条件；不应新增假成功 ACK，也不应拿一个 themeEdited 持久化通知当视觉提交凭据。
5. stale/invalid/Load失败：保持旧分类和旧有效色；保存失败：保留已生效主题/当前颜色，显示未保存；切回旧浅/深页只选择对应记忆，不再次覆盖为默认色。全局粗细独立于本分类，页切换不能恢复旧 widthSelection。

### Files found / Related specs / External references

- `Draw3.WhiteboardModel.h`、`Draw3.DrawingController.cpp`：主题模型、页生命周期及权威接纳。
- `Draw3.Host.cpp`、`Draw3.Product.cpp`：Host 回执快照和主题请求入口。
- `IdtState.cpp`：工作区/页 UI 协调与推荐业务接纳点。
- `UI/Whiteboard/Whiteboard.cpp`、`Whiteboard.Theme.h`、`UI/PageControl/PageControl.cpp`：页面展示、背景绘制、预览和点击。
- `.trellis/spec/native-desktop/whiteboard.md`：按页主题、冷加载失败保持原页、三工作区交接合同。
- 无外部 API/版本争议，本轮未使用外部资料。

## Caveats / Not Found

- 只进行静态源码调查；未改代码、未运行 Git/GUI/build/测试。文中逻辑接纳不可表述为真实像素已呈现。
- 不替用户决定工具族范围、荧光/激光默认色，也不把三范围颜色自动理解为跨进程启动恢复。
- 待设计进一步明确同步机制后，应验证：明暗同类/跨类翻页、主题切换后立即落笔、cold load成功/失败/stale、主题重复/过期/无效请求、后台主题回执、最小化重入、保存失败，以及 UI 显示色与下一笔捕获色一致。

## 收敛追加：唯一推荐的最小同步机制（待实施，2026-11-02）

用户已明确：只有 Brush（硬笔/软笔/形状）采用 Desktop/PPT、白板浅色、白板深色三范围颜色；Brush 粗细全球共享。Highlighter 与 Laser 各自有一套全球颜色/粗细，二者不合并。本节替代上文工具族仍待确认的状态，仅推荐以下一个实现机制。

采用 **独立白板样式 token + 复用 Host 新 Down 准入门 + 绘制 owner 应用 ACK**。这是对现有协调链路的小范围扩展，不另造线程，不等待 UI 操作，也不借用 workspaceTransitionEpoch。

1. 在 `Draw3.Host.cpp:1198` ObserveWhiteboard 接收 Controller 真正接受的当前页/主题时，为 `(hostGeneration, whiteboardGeneration, pageIndex, backgroundRgb)` 建立幂等样式 token（可以是 Host 局部单调 styleEpoch 加完整身份）。同页改主题不会推进 whiteboardGeneration，因此不能只用 generation；同一组重复发布不重开 pending。变化时在向外发布 RuntimeSnapshot 前同步设置 `whiteboardStylePending`，由现有 `RefreshPresentationInputGateLocked` 合并独立原因。现成合并位置 `Host.cpp:873`，`:904` 调 `input.SetAdmissionBlocked(blocked)`；已有 loading 原因继续保留，不互相清除。
2. Host RuntimeSnapshot 携带该期望 token；业务在既有 `IdtState.cpp:3367`/`:3483` 回执处理处，校验当前 Host/工作区身份，按唯一分类策略选择 Brush 颜色范围，将 token 与有效色放入同一个 ProductState 发布。最小新增载荷是样式身份/token，不新增一套颜色值来源；普通后续工具/宽色发布也保留当前已接纳 token。Highlighter/Laser 使用各自全球值，但仍遵守同一场景输入门。
3. `Host.cpp:1688` PumpBridgeState 已负责真正应用目标样式，`:1766` 调 `window.SetProductVisualStyle(...)`；`:1817` 起在 presentationTargetMutex 下记录 `appliedProductRevision`/`appliedTool`。就在这个 owner 应用点，且仅当本次应用 ProductState 的 token 等于 Host 当前期望 token 时清 `whiteboardStylePending` 并重算合并准入门。**不能在 PublishState 入队时清门，也不能只比较 appliedProductRevision**；前者尚未应用，后者不足以证明属于该页/主题。新的 token 替换旧 pending，迟到旧状态无法开门。
4. UI 从业务已接纳的同源样式/颜色快照呈现；下一笔必须等待 owner 匹配 ACK 后才准入。既有 `Host.cpp:2953` 注释及 `:3039` 起生产探针证明该准入门只拦新 Down，Move/真实终态/ControlWake 仍推进，已排队旧 Down 的 admission 身份会失效；Controller `DrawingController.cpp:12809` 实际验证 ContactAdmitted。因此无需暂停绘制线程等业务回信，也不能将 pending 当成功释放。
5. 加载失败仍发布旧页/旧主题身份，不选择失败目标颜色；真正页切换/同页换色都统一走上述 token。保存失败不回滚已接受主题。Host重启必须清 pending/token 并用新 hostGeneration 拒绝旧 ACK。离开白板由既有工作区门保护，独立白板样式原因只在其当前责任范围阻挡，不使 Desktop/PPT 被退休白板 token 永久锁住。

锁顺序与落地约束：

- 复用 Host 的 `presentationTargetMutex -> bridge.Snapshot/PublishState -> input.SetAdmissionBlocked` 顺序（`Host.cpp:873`、`:2924`）。ObserveWhiteboard 可先在 presentationTargetMutex 下更新期望 token/关门，释放后再以 whiteboardMutex 存回执并 PublishRuntimeRevision，**不同时持这两个锁**；现有 ObserveWhiteboard 本就分别使用 whiteboardMutex 和 presentationTargetMutex。
- 业务仅在 stateModeTransitionMutex 下更新颜色范围/有效快照，发布 ProductState 后释放；不持 Host 的 whiteboardMutex/presentationTargetMutex 回调业务，不从 ObserveWhiteboard 直接调用重型 SyncDraw3State、窗口操作或等待 ACK，避免绘制线程与窗口 owner 互等。
- PumpBridgeState 在绘制 owner 完成 SetProductVisualStyle 后才短持 presentationTargetMutex 记录 ACK/清门；不把业务状态锁塞进该锁内。桥接 ProductState 与 token 必须同快照；不能用独立无版本 atomics 拼接颜色和 token。
- 新增范围仅为一个 Host pending/token 状态及桥接/回执载荷和真实准入合并；现有 workspaceTransitionPending（`Host.cpp:2934`）的 epoch 属于工作区切换，不能为普通主题/翻页借用，否则会与工作区协调相互退休。

此建议确保逻辑主题与新笔样式的顺序，不宣称背景像素已成功呈现；已有工作区 BackgroundPrepared/Present/恢复门继续独立生效。若需要保证“新背景已显示才允许下一笔”，须把现有背景完成凭据纳入这个 token 的准入条件，不能用样式 ACK 假冒背景 ACK。本轮不扩展该产品承诺。
