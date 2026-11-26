# 实施计划：UInk 1.1 适配与冷页加载提速（C1.4 + C1.5 联合）

需求见 `prd.md`，设计见 `design.md`。规划阶段不改代码；用户明确放行后再按本计划执行。

## 协作流程（每一步都一样）

1. Claude 写简报（用 Write 落盘到 `%TEMP%\inkeys-codex\c14\`，不进仓库）：目标、允许改的文件、禁止项、验收、设计章节、编码要求。
2. Codex 写入：`run-codex.sh <步骤> workspace-write <简报>`（`gpt-6.2-sol`、High、`service_tier=default`、`HTTPS_PROXY=127.0.0.1:7890`）。
3. Claude 验收：
   - 完整构建 `InkeysRepo.sln` Debug|ARM64（ARM64 原生 MSBuild，`-m:1 -p:CL_MPCount=3`，超时 ≥ 5 分钟）；涉及 `UK\` 或模型器测试时**另外构建 `inkStrokeModelerTest.sln`**（产品 solution 不含模型器测试工程）。
   - 跑本步相关的自检与测试；逐行读差异；核对 Codex 报告里所有“顺手改动”。
   - 单点变异验证。
   - 检查 BOM 与 CRLF，`git diff --stat` 确认没有整文件差异。
4. Codex 只读审查（另起会话，`read-only`）→ Claude 逐条判断是否属实、是否改动前既有 → 修复（细节修复由 Claude 直接改）→ 复核。
5. 本步通过后才进入下一步；不提交，等用户授权。

### 每步的硬性门槛

- **每一步结束时，两个 solution 都能构建，已有的相关测试与自检全部通过**。不允许“这一步先坏着、下一步再修”。
- 因此旧接口**分阶段删除**：前面的步骤只新增接口，旧接口留给尚未迁移的调用方；某个场景迁移完后，在同一步删除只被它使用的旧接口与旧测试。
- 测试与自检一律注入临时根目录，不写真实的 `Inkeys\Save\Ink`。

### 简报固定要求

- 源文件 UTF-8 BOM + CRLF；不用 `sed -i` 改源码。
- 改签名后全仓搜索调用点：`inkStrokeModelerTestTests`、`InkeysHeadlessTests`、`IdtMain.cpp` 自检入口、`Draw3.Host.cpp`、`Draw3.HiddenWindowTest.cpp`、`Draw3.FailedCleanupFixture.cpp`、`contact_input_tests.cpp` 等。
- 增删源文件时同步 `Inkeys.vcxproj`、`inkStrokeModelerTest.vcxproj`、`inkStrokeModelerTestTests.vcxproj` 及对应 `.filters`。
- 新引用的类型、枚举、函数逐个 grep 确认存在及命名空间；共享头里用 `(std::min)`。
- 只做本步范围；发现相邻问题写进报告，不修。
- 不得用删除、跳过或放宽测试来换取通过；删除旧机制的测试时必须写明它对应的新行为测试。
- 测试先验证初始化成功再断言，不能整段被 `if` 包住而空转。
- 冲突即停：设计与代码事实矛盾时零改动返回并说明。

## 步骤

### S0 基线（Claude）

- [ ] 构建两个 solution，跑现有 UInk、白板、PPT、桌面相关测试与自检，记录通过情况与耗时（作为每步“已有测试全部通过”的对照）。
- [ ] 保留 C1.5 计时代码（工作区未提交）作为对比工具；记录 Release 构建下同一重页面的基线（需要用户配合实测，或先用自检生成的重页面）。

### S1 模型与编解码（L0/L1）

文件：`UK\uink_model.*`、`UK\uink_codec*.cpp(m)`、模型器 UInk 测试；为保持编译，可对 `UK\uink_draw3_*` 做最小跟随改动。
- [ ] 模型补 `nib`、`cap`、Shape `lineCap`/`lineJoin`、Canvas `background`/`pageState`/`pageKind`/`binding`/`viewport`、Shape `object`（首部件与后续部件两种）。
- [ ] 编码写 1.1（design §3.1）：`version = 11`、精确类型、Clear 独占组、`workspaceGuid`、隐式 Device。
- [ ] 解码按 design §3.2 容错与作用域；去掉整页拒绝；`double` 累加位移；保留有界预检（§3.3）。
- [ ] 读取诊断 `UInkReadReport`（§8）。
- [ ] **过渡**：codec 继续支持读写 `extra` 与现有私有字段（旧场景还在用），只新增 1.1 官方字段；私有字段在 S6 旧路径全部迁移后才删除。版本号改为 11 后，旧存储的严格检查若因此失败，在本步一并放宽到接受 10–19（只改版本判断，不动其他逻辑）。
- [ ] 实测解码耗时；达不到 design §3.2 目标时单独提出读取器改造，经用户同意后另开子步。
- 验收：写出字节夹具、编解码往返、容错与资源上限语料（§10）；变异验证。
- **进度（2026-11-25）**：S0 基线见 `research/s0-baseline-20261125.md`（模型器测试工程原本编不过，已在测试 vcxproj 最小修复）。S1 由 Codex 实现 + 只读审查 + 修复一轮：两个 solution 构建通过，模型器测试只剩 3 个基线既有失败，9 个相关自检结论与基线一致；变异验证 2 处（CANVAS-22 回退、`lineCap` 位宽）均被抓到；顺带修了一处既有测试越界（`uink_tests.cpp` 坏 viewport 断言未判空）。补丁 `%TEMP%\inkeys-codex\c14\after-S1.diff`。**解码耗时未达标**：Release 250 笔×120 点一页 14.6 ms（目标 ≤5 ms），热点为每点 `ValidateMap`/`FindField`/`ChildPath` 字符串构造与 `ScanValue`+`msgpack::unpack` 两遍，待用户决定是否开读取器改造子步。
- **S1b 单遍流式解析（用户批准，2026-11-25，Astra High 单次授权）**：独立基准证明库只占约 3 ms、其余是自身处理，不换库。Astra 把解码器改为单遍流式解析；冻结 S1 解码器为测试专用参考实现（`inkStrokeModelerTestTests/uink_reference_decoder.cpp`，S6 随旧路径删除）+ 21938 例确定性变异差分。Sol 补快路径定向用例（我的变异验证发现快路径校验原先未被覆盖，补后可抓到）；Sol 只读审查后，我把 8 处按声明数量的 `reserve` 改为首次最多约 1 MiB 的有界预留并补用例。结果：Release 一页 1.85 ms（参考 13.96 ms），两个 solution 构建通过，测试与自检与基线一致。补丁 `after-S1b.diff`。S2 需遵守审查给出的两条要求（已写入 design §4.2 `ReadPage`）。

### S2 文件层 `uink_store`（L2）

文件：新增 `UK\uink_store.*`、对应测试；三个 vcxproj 与 `.filters`。
- [ ] `UInkPagedFileWriter`（Create / AppendPage / Compact / Close）、页目录、`ReadPage`、`ReadUInkFile`（design §4）。
- [ ] 失败路径：短写截断回原末尾、截断失败封闭写入器、发布不覆盖已有文件。
- [ ] **过渡**：旧 `uink_file` 保存机制与 `uink_page_file` 原样保留（场景还在用），S4–S6 随迁移删除。
- 验收：按页追加、替代、目录、`ReadPage` 与整读一致、整理前后一致、失败路径。
- **进度（2026-11-25）**：Sol 实现 + 只读审查 + 修复一轮。新增 `draw3.uink_store`（`UInkPagedFileWriter`、`UInkPageDirectory`、`ReadUInkPage`、`ReadUInkStoreFile`）与编解码层 `DecodeUInkStream`（整文件/页两种模式，只做逐块容错、不做跨 Canvas 校验；`DecodeUInk` = 共享循环 + 原有后置校验，差分仍全过）、`EncodeUInkPage`、`EncodeUInkHeaderPrefix`。审查修复：绝对路径、写盘前内存解码自检、编码器长度上限与解码器对齐、回退后刷盘、尾部未确认标记、页段第二个 Canvas 一律拒绝、发布后重开失败带回路径。两个 solution 构建通过；存储/页文件测试 0 失败，其余与基线一致；变异 3/4 被抓到（第 4 处为与解码器重复的防御检查）。只用 Win7 可用的 Win32 文件 API。补丁 `after-S2.diff`。

### S3 Draw3 适配（L3）：新增 1.1 适配路径，旧路径不动

文件：`UK\uink_draw3_export.*`、`uink_draw3_import.*`、`uink_draw3_capture.*`、`shape_uink_*.inl`（以**新增**函数或新文件的方式，新路径的命名在简报中确定）。

旧的三个场景仍依赖私有字段（旧 PPT 保存前校验要求私有 Canvas 绑定与私有 Workspace 类型、导入要求私有绑定与显式 `viewport`，`Draw3.PresentationAutoSave.cpp:1008-1046`、`uink_draw3_import.cpp:415-465`；旧白板靠 `uink_page_file` 的严格自校验与 `inkeysWhiteboardHistory`，`uink_page_file.cpp:63-133`、`DC:833,868,18558-18584`）。所以 S3 **不改旧适配路径、不删私有字段**，只新增一条只用官方字段的 1.1 路径并单独测试：
- [ ] 橡皮与荧光笔换算（design §3.1 表）。
- [ ] 页面元数据（白板背景、PPT 绑定/保留页/结束页、视口）写入与读取 Canvas 官方字段。
- [ ] 页内日志：上一区间 + Clear + 当前区间，最多一个 Clear；导入允许编号空洞与同组多笔。
- [ ] 图形对象层映射与读取降级（design §6，含 `Kind::Compound`）。
- [ ] 外来内容就近回退（design §7）。
- 验收：21 个预设 + 旧版 4 种 + `Kind::Compound` 往返；橡皮换算往返在浮点精度内；荧光笔宽度原值往返；不完整对象层降级为独立图形；回退语料。旧路径的现有测试保持原样通过。
- **进度（2026-11-25）**：Sol 实现 + 只读审查（3×P1、4×P2、2×P3 全部属实）+ 修复一轮。新增 `draw3.uink_draw3_v11`（`ExportDraw3PageToUInkV11`、`MakeDraw3UInkV11FileHeader`、`ImportUInkV11Page`、`ImportDraw3UInkV11File`）、`shape_uink_v11.inl`；`Draw3UInkCanvasSnapshot` 新增 `background`/`endScreen`/`binding`。Claude 裁定：旧矩形圆角与当前缩放计算值一致则恢复旧矩形否则 `Compound`；对象 `style` 颜色按 uint32 写出；导出超冷读预算的单个图形只跳过该项（不让整页失败，Claude 直接改）；多图层上层橡皮跳过；外来荧光笔笔尖保持 1:8 近似；每页几何工作预算 2^26（Release 最坏构造页约 205 ms，**S4/S5 必须在后台线程做解码与 `ImportUInkV11Page`，绘制线程只安装结果**）。两个 solution 构建通过，测试与自检与基线一致，变异 2/2 被抓到。补丁 `after-S3.diff`。

### S4 白板（L4）

文件：新增 `D3\` 下的 `PagedInkStore`（文件名在简报中确定），替换 `D3\Draw3.WhiteboardStore.*`；`DC` 白板相关路径；`Draw3.Host.cpp` 白板接线。
- [ ] 白板改用 `PagedInkStore` 与 S3 的新适配路径：根目录由调用方注入（此时仍取现有根，S6 统一切换）。
- [ ] Clear 写入、翻页/离开/退出写入、写入状态与驱逐条件、上一区间已释放时的拼接（design §5.2）。
- [ ] 冷读后的撤回状态（design §5.2 四条）。
- [ ] 失败出口（design §5.5）。
- [ ] 删除白板历史 JSON、journal、`current` 与版本文件、`uink_page_file` 及只被白板使用的旧适配代码与旧测试；离页序列化移到后台。
- 验收：产品自检覆盖翻页冷读、Clear 写入、§5.2 冷读撤回验收场景、写失败不驱逐、退出保存。
- **进度（2026-11-25）**：开工核对（Sol 只读，`%TEMP%\inkeys-codex\c14\s4-precheck-report.md`）后 Claude 裁定 10 条写入 design §5.2 末尾，并把 S4 切为 S4a/S4b/S4c。**S4a 完成**：新增 `Draw3.PagedInkStore`（单后台线程、完成槽位预留、Clear FIFO、同段 Full 合并、`PendingWrite`、文件失败锁定、按区间记录降级 + 写前冷读复核、唤醒回调寿命保证、保留设备名）与 `SpliceUInkV11PreviousInterval`；Sol 实现 + 只读审查（1×P1、1×P2、1×P3）+ 修复。测试 0 失败、其余与基线一致；变异 3/3 被抓到。补丁 `after-S4a.diff`。产品白板仍走旧路径。
- **S4b 完成（2026-11-26）**：DC 白板全部存储调用切到 `PagedInkStore`（无分配控制唤醒、离页只截快照、Clear 写入与确认后释放上一区间、整页写入/盘上拼接、后台解码导入 + 绘制线程安装、逐笔撤回、`LoadPreviousInterval`、两处驱逐共用 `IsPageEvictable`、退出保存活动/停放白板、C1.5 计时字段更新）。Sol 实现 + 只读审查（2×P2、2×P3）+ 修复 + 复核（2×P3）+ 修复。复核证明修复轮在生产 Clear 事务里加的栅格令牌改写在正常路径上等值、只为测试夹具所需，已撤回并改为修夹具。`--draw3-whiteboard-storage-test` 通过，其余自检与基线一致；变异 2/2 被抓到。headless 的一条置顶失败与本任务无关（headless 工程不编入任何改动文件，判为环境偶发）。补丁 `after-S4b.diff`。
- **S4c 完成（2026-11-26）**：删除 `Draw3.WhiteboardStore.*`、`uink_page_file.*` 与 `uink_page_file_tests.cpp`、DC 中白板私有历史 JSON 编解码与可逆校验、`CanvasCommandType::WhiteboardPersistenceCompleted`（后续命令编号固定为 14 起）；旧 `WhiteboardStore` 快照寿命测试迁到 `paged_ink_store_tests.cpp`。Sol 给出删除断言 → 承接用例对照（见 `%TEMP%\inkeys-codex\c14\s4c-report.md`）。Claude 补回 `uink_tests.cpp` 的 `import draw3.uink_file;`（此前经 `uink_page_file` 间接导入）。全仓源码只剩新代码沿用的日志前缀 `[Draw3.WhiteboardStore]`。两个 solution 构建通过，测试与自检与基线一致（`--draw3-hidden-test` 的触摸重连两条偶发失败，重跑通过，与存储无关）。补丁 `after-S4.diff`。**S4 全部完成。**

### S5 PPT（L4）

文件：`D3\Draw3.PresentationAutoSave.*`、`DC` PPT 相关路径、`Draw3.Host.cpp`、`presentation_autosave_tests.cpp` 等。
- [ ] 改用 `PagedInkStore` 与新适配路径：一文稿一文件夹、每进程一个文件、同进程再进入续写；槽按 `(key, bindingMode)` 隔离、`binding` 字段区分（design §5.2 PPT 部分）。
- [ ] SlideID 重排、页数变化、保留页、结束页、页码回退；`PreviousInterval` 读单页；停放槽退出保存。
- [ ] 删除索引、多轨、会话轨、跨进程冲突检测、私有 Workspace 类型 `128` 与私有绑定字段，以及只被 PPT 使用的旧适配代码与旧保存接口；测试迁移到新行为。
- 验收：同进程再进入、两种绑定、重排、页数变化、保留页再出现、结束页、Clear 后跨 Clear 撤回、Clear 写入先进先出不合并。
- **进度（2026-11-26）**：开工核对（`%TEMP%\inkeys-codex\c14\s5-precheck-report.md`）后 Claude 裁定写入 design §5.2 “S5 开工核对后的裁定”11 条，切为 S5a/S5b/S5c。**S5a 完成**：`PagedInkStore` 新增确认页清单与 `LoadLane`（按有效 binding 批量加载，含保留/结束页，同 lane 待写则 `PendingWrite`）；按页读取容错（可显示部分照常返回 + 前缀/后段分别标记读取降级；身份错误、临时身份、Clear 与确认边界不符一律失败）；热页盘上前缀损坏时写“可读前缀 + Clear + 内存当前区间”的恢复版本；白板读取降级写 Warn 且不驱逐；新增 `Draw3.PresentationPages`（`BuildPresentationPageMetadata`、`ProjectPresentationLane` 纯函数）与测试。Sol 实现 + 审查（3×P1、2×P2）+ 修复；Claude 修测试工程编译（显式 `operator==` 与 `<array>`）。两个 solution 构建通过，测试与自检与基线一致（Sol 沙箱中的 69 处失败为沙箱文件权限环境问题，本机不复现），变异 2/2 被抓到。补丁 `after-S5a.diff`。产品 PPT 仍走旧 `PresentationAutoSave`。
- **S5b 完成（2026-11-26）**：产品 PPT 全部切到 `PagedInkStore`：Host 按文稿 key 懒建存储（与旧 PAS 同持有者/寿命），回执经 Host 泵 + WindowController 预留槽保序投递；逐页持久化状态随 GUID 移动，热重排/冷投影用 `BuildPresentationPageMetadata` 推进修订；`LoadLane` 成组安装 + 4 档退避；Clear 与跨 Clear 撤回同白板；四处驱逐共用谓词；页序 lane 跨驱逐保留可信页数；退出保存活动与停放 lane。PPT 自检迁入新文件 `Draw3.PresentationStorageTests.inl`，`HiddenWindowTest`/`FailedCleanupFixture`/`ShutdownSupervisor` 的 PPT 部分迁移。Sol 实现（沙箱无法构建）→ Claude 修 3 处编译问题（误删 `#include "Draw3.PresentationState.h"`、误删 `ReconnectManualTestRange`、新 `.inl` 末尾未闭合）→ 只读审查（1×P1、1×P2、2×P3）→ 修复两轮（C10 回执 `localDate`、Host 实际准入计数与逐请求对账、计时口径；Claude 去掉 3 个原无 BOM 文件被误加的 BOM）。7 个 PPT 自检、白板与其余自检通过；模型器测试仅 3 个基线失败；变异 2/2 被抓到。真实故障清理夹具（C09/C10 real）在本环境授权阶段即返回（C09 未改同样），留 S8 实机验证。headless 置顶类失败数波动（2→9），headless 不编入任何改动文件，判为桌面环境干扰。补丁 `after-S5b.diff`。
- **S5c 完成（2026-11-26）**：删除 `Draw3.PresentationAutoSave.*`、`presentation_autosave_tests.cpp`、私有 Workspace 128 与私有绑定/页状态/结束页字段的写入与 PPT 专用导入（`ImportApplicationOwnedPresentation` 等）、owned version 系列、`MergeDraw3UInkCanvasTail`；旧桌面导出不再附带无读取方的 `inkeysPageState=active`。Sol 给出旧测试群 → 承接用例/退役对照（`%TEMP%\inkeys-codex\c14\s5c-report.md`）。Sol 越界删除了 Trellis 归档 `archive/2026-10/10-14-canvas-freeze-investigation/research/` 下 4 个旧探针文件，Claude 已 `git checkout` 恢复。两个 solution 首次构建通过，全部测试与自检与 S5b 一致。补丁 `after-S5.diff`。**S5 全部完成。** L1 `InkeysPageKind` 私有回退留 S6。

### S6 桌面、清理旧路径与根目录切换（L4）

文件：`D3\Draw3.AutoSave.*`、`DC` 桌面相关路径、`IdtMain.cpp` 根目录、`desktop_autosave_tests.cpp`、`Draw3.HiddenWindowTest.cpp`、`Draw3.FailedCleanupFixture.cpp`、`UK\` 中剩余的旧代码。
- [ ] Clear 时写当前区间快照、退出写当前区间（含停放的桌面）；去掉 `index.json`、索引 mutex、保存后回读。
- [ ] 跨 Clear 撤回读快照文件。
- [ ] 旧路径已无调用方：删除旧适配路径、私有字段（`eraserShape`、`inkeysShape` v7 等）、`uink_file` 中的旧保存机制（SHA revision、自读、owned version、追加计划）与对应测试；codec 读取时改为跳过 `extra` 不复制。
- [ ] **根目录切换**：`IdtMain.cpp` 改为 `...\Inkeys\Save\Ink`；确认全仓不再有代码访问 `Inkeys\AutoSave`。
- 验收：桌面测试与自检；全仓搜索 `AutoSave` 路径、旧接口名与 `inkeys` 私有键为零（配置项 `config.Drawing.AutoSave.Enable` 等名称除外）。
- **进度（2026-11-26）**：开工核对（`%TEMP%\inkeys-codex\c14\s6-precheck-report.md`）后 Claude 裁定写入 design §5.3 “S6 开工核对后的裁定”9 条，切为 S6a/S6b/S6c。**S6a 完成**：L2 新增 `PublishUInkSnapshotFile`（一次性发布，发布即提交，写盘前页段 + 整流内存自检）；新服务 `Draw3.DesktopInkSnapshots`（Host 持有，回执经预留槽保序；按请求身份登记发布路径；冷读核验识别到的 Canvas 数 == 1）；DC Clear/Redo Clear 改为清前截取一次、CPU Clear 成功后提交；C09/C11 夹具协议 v2 传递发布路径；L3 导出统一从 0 重编号撤回组。Sol 实现 + 审查（1×P1、2×P2、2×P3）+ 修复；Claude 补 `DecodeUInkStream` 整文件重载传出识别 Canvas 数（Sol 白名单外）。两个 solution 构建通过，测试与自检与基线一致，变异被抓到。补丁 `after-S6a.diff`。旧 `Draw3.AutoSave.*`、`uink_file`、旧 L3 仍保留（S6b 删）。
- **S6b 完成（2026-11-26）**：Sol 删除 `Draw3.AutoSave.*` 与 `desktop_autosave_tests.cpp`（保留目标迁入 `desktop_ink_snapshots_tests.cpp`）、旧 L3 导出/导入/捕获（快照类型迁入 `uink_draw3_v11`）、私有图形元数据 `shape_uink_metadata.inl`/`shape_uink_import.inl`、`uink_file` 整对与 `EncodeUInkAppendObjects`、L1 私有橡皮字段与 `InkeysPageKind`、冻结参考解码器与差分测试；codec 对 `extra` 做有界 wire 遍历跳过，模型不再保存 `extra`，新用例 `TestExtraWireSkipping`。Claude 补 `uink_tests.cpp` 缺的 `import draw3.ink_document;`。两个 solution 构建通过；模型器测试只剩基线 `highlighter:663`（基线另两处 `uink_tests` 租约失败随旧文件层退役）；新路径专项 0 失败；12 个自检与 S6a 一致（hidden 两条触摸重连偶发失败重跑通过，S4c 出现过同样情况）；headless 仍 9。变异：去掉 `extra` 免计费 → 用例失败；去掉 `unchargedBytes_` 窗口记账 → 无用例失败（只影响是否重放，性能路径）。全仓搜不到简报列出的旧名字与 `"inkeys*"` 私有键（`[Draw3.WhiteboardStore]` 仅为日志前缀，S7 统一日志时处理）。补丁 `after-S6b.diff`。Sol 只读审查：1×P2（`devices`/`workspaces` 注册表免计费按键名识别，任意根对象都能触发）、4×P3（外来无对象层矩形矩阵、同页复合图形 + Clear 编号、成品字节 `extra` 缺席、官方夹具 SHA-256 与资源包清单、`UnreadableEncodedPageRejected` 断言过宽）。S6b-fix（Sol）：注册表条目 `extra` 的费用先暂记，根 Map 闭合时只有唯一的 Header Extension 类型才免计费，否则补计并重查窗口与上限（`type` 在前在后结果相同）；新增 `UInkReadResult::replayedObjectCount`（只在已有重放分支计数）并补快路径/重放对跑一致性用例；P3 全部补测试（夹具 SHA-256 用测试侧 BCrypt）。变异：去掉补计分支、去掉 `unchargedBytes_` 窗口记账、Clear `contentId` 改用 `undoId`，三处均被用例抓到。
- **S6c 完成（2026-11-26，Claude 实现）**：`IdtMain.cpp` 存储根改为 `<exe>\Inkeys\Save\Ink`，程序目录取不到时根为空（关闭持久化，不再拼出盘符根路径）；C09/C10 夹具 `StorageRoot` 改为 `artifacts\C09|C10\Save\Ink`；`ShutdownSupervisor::RemoveCleanupPptStorage` 链路加入中间目录 `Save`（同样检查重解析点、清空后移除）；`Draw3.Host.h` 注释同步。两个服务都逐级建目录。全仓（除 `.trellis/`、`*.md`）固定字符串搜索 `\AutoSave`/`AutoSave\` 只剩 IdtMain 中说明“旧目录不再访问”的注释；`AutoSave` 其余出现均为配置项与命令类型名。两个 solution 构建通过，模型器测试只剩基线 `highlighter:663`，新路径专项 0 失败，12 个自检与 S6b 一致，headless 同样 9 项置顶类失败。C09/C10 真实夹具在本机仍停在授权阶段，留 S8 实机。补丁 `after-S6.diff`。**S6 全部完成。**

### S7 日志与计时、CPU 优化

- [ ] `Module::UInk` 与读写日志（design §8）。
- [ ] 调整 C1.5 计时字段；Release 实测；按数据决定足迹是否移到后台、整理阈值取值（design §4.1、§9）。
- 验收：日志格式自检；前后对比数据写入 `research/`。
- **进度（2026-11-26）**：Sol 开工核对（`%TEMP%\inkeys-codex\c14\s7-precheck-report.md`）后 Claude 裁定写入 design §8 末尾“S7 开工核对后的裁定”5 条，切为 S7a 日志 → S7b 计时与基准 → S7c 空闲整理 → S7d 足迹迁移（视数据）。
- **S7a 完成（2026-11-26）**：`Module::UInk`；L4 worker 在每次实际读盘终态输出一行 `[UInkRead]`（冷读、`load_previous`、PPT lane 每页、`splice_prefix`、桌面回读），写入失败/降级输出 `[UInkWrite]`（带 `sealed`/`tailUnconfirmed`/回滚阶段与错误码），已接纳但读盘前失败输出 `[UInkQueue]`；判级：完整 Debug，跳过/L3 回退/拆撤回组/历史降级（`storedDegraded=1`）Warn，失败 Error；`splice_prefix` 前缀校验或导入失败记 Error（`stage=prefix_validate|prefix_import`）；`strokes` 统一按逻辑笔画；格式化异常计入日志丢弃数（`Log::RecordDropped`）；DC/Host 重复的读写回执消息删除，独有的准入/安装/快照/退出消息保留；白板真实日志自检迁到 `[WARN ][UInk]` 并按整行匹配恰好一行。测试日志捕获用 `DRAW3_TESTING` 下的 sink（只在模型器测试工程编译）。Claude：测试工程 4 个引用 `Log.h` 的文件补单文件 `/utf-8`（仍有 `_UTF8` 模块导入 C5050 警告，构建与测试正常）；`version=` 改十进制。Sol 审查 1×P2 + 4×P3，全部修复。变异 6 处（L3 回退判级、`split_undo` 判级、拼接成功读日志、历史降级判级、`prefix_validate` 阶段、回滚错误码）均被抓到。两个 solution 构建通过，模型器只剩基线 `highlighter:663`，12 个自检与 S6c 一致，headless 9。hidden 两条触摸重连用例在 S7a 中途又偶发一次（累计 3 次，均重跑通过，S8 提醒用户关注）。补丁 `after-S7a.diff`。
- **S7b 完成（2026-11-26）**：L2 `ReadRange` 新增 `ioMs`（单页与整读）；L4 页结果带 `ioMs`、`workerMs`；`[UInkRead]` 加 `ioMs`；`[Draw3Diag][page-load]` 加 `ioMs/decodeMs/importMs/footprintMs/installMs/workerMs/pageBytes`，只在采到接手时刻时输出 `readyMs/handoffMs`；`[page-save]` 区分 `written`（含 `success`）、`merged`、`canceled`、`file_failed`（被清退的排队请求），补 `pageBytes`。新增无窗口基准 `--draw3-whiteboard-load-benchmark [--rounds N]`（真实 `PagedInkStore` + 生产安装路径，固定种子合成 98/246 笔重页，校验 GUID/笔数/Clear 前缀撤回状态）。Sol 审查 1×P2 + 2×P3 已修复，变异被抓到。数据见 `research/s7b-load-benchmark-20261126.md`：Release 冷读一页 CPU 合计 p50 约 8.3 ms（worker 1.5–1.7 ms，绘制线程足迹 5–6 ms + 安装约 1 ms），Debug 约 40 ms（C1.5 旧链路同段约 140–200 ms）。**S7d 裁定：本任务不把足迹移到后台**（理由见该文档），C2 后再评估。headless 本轮回到 S0 基线（只剩 `closing minimized whiteboard`），印证此前多出的置顶类失败是桌面环境干扰。补丁 `after-S7b.diff`。
- **S7c 完成（2026-11-26）**：L2 `Compact` 结果区分 `replacementCommitted` 与重开状态/错误码，失败出口用 `WithTempCleanup`（删除失败附 `temp.cleanup`）；L4 worker 在队列空、无在途业务、未关闭、写入器健康且无进行中的驱逐许可时整理，阈值“被替代 ≥ 8 MiB 且 ≥ 50%”（测试可注入）；按 `pageGuid`+`writeSequence`+`offset`+`length` 记录有独立证据的损坏（纯资源限额不算），同页新版本确认后低于阈值也整理，仍有最新坏页时推迟并记 Debug `[UInkCompact]`，提交后清除坏段记录；失败后等下次成功写入再查；`Close` 取消未开始的整理并等待进行中的；驱逐与整理用短期许可互斥（`TryBeginEviction`/`EndEviction` + DC 白板与 PPT 释放点的 RAII 守卫）；L4 读页新增按确认目录/记录核验 `pageIndex` 与稳定绑定页 `slideId`（S5 起的读取缺口），失配整页失败并记坏。Sol 审查 2×P2 + 2×P3 + 1 待实测，全部修复。变异 7 处（阈值 `&&`→`||`、最新坏页判定、坏段清除 ×2 轮、驱逐许可两侧、`pageIndex` 核验）均被抓到（首轮“不清坏段”存活，补用例后抓到）。两个 solution 构建通过；模型器只剩基线 `highlighter:663`；12 个自检与 S7a 一致；headless 只剩 S0 基线 `closing minimized whiteboard`；基准正常。补丁 `after-S7.diff`。**S7 全部完成。**

### S8 收尾

- [ ] Claude 完整独立审查 → Codex 完整只读审查 → 修复 → 复核。
- **S8 审查与修复进度（2026-11-26）**：Claude 独立审查与 Sol 全量只读审查（`%TEMP%\inkeys-codex\c14\s8-review-report.md`）同时指出 P1：design §3.3 的瓦片足迹预算从未实现，外来/损坏页可在绘制线程安装时产生数十亿瓦片。三轮修复与复核（S8-fix/fix2/fix3）后收敛为 L3 四重上限（每页 2^28 次访问、单项 2^25 次访问、单项包围盒 2^18 块、每页包围盒合计 2^23 块），超限单项跳过并 Warn，保存侧做同一导入预检（会降级或预检失败的页保留驻留副本）；另修：大视口回退默认视口、`pageIndex`/`slideId` 身份核验（S7c-fix 已做）、追加写入与驱逐互斥、被拒驱逐在后续回执/整理结束时重试、驱逐空壳分配失败局部捕获、关闭后拒绝许可、临时文件叶名不长于目标名、整理刷新 Header 时间、桌面路径登记随恢复点退休释放、桌面回读补 `readMs/importMs`、栅格异常与正常失败共用结算、三个安装入口异常边界。Claude 调整：初版 400 万页预算会跳过正常自产内容（13 万点长笔），改为上述上限（design §3.3 “S8 调整”）。已知限制记入 design §11（PPT 整槽累计预算、128 MiB 整读、中段坏页整文件可读性、整理不压缩页序、不做 `\\?\` 长路径）。变异 13 处均被抓到。最终门禁：两个 solution 构建通过；模型器只剩基线 `highlighter:663`；12 个自检一致；headless 只剩 S0 基线 `closing minimized whiteboard`；Release 冷读一页 CPU p50 约 8.3–9.0 ms（后台导入因足迹估算增加约 0.25 ms）。补丁 `after-S8.diff`。用户实机清单 `research/s8-manual-test-checklist.md`。
- [ ] 全量门禁：两个 solution 构建、全部 UInk/白板/PPT/桌面测试、产品自检。
- [x] 用户实机：重页面翻页前后对比、PPT、桌面 Clear、损坏文件投放。
- **用户实机进度（2026-11-26，Release|ARM64）**：白板重页（953 笔、2.7 MB）冷读 19 次全部完整，后台约 22 ms、绘制线程足迹+安装约 44 ms、GPU 冷重建约 240 ms（78%，属 C2），数据记入 `research/s7b-load-benchmark-20261126.md`；白板 Clear + 盘上前缀拼接、跨 Clear 撤回、桌面快照跨 Clear 撤回、退出后新进程新建文件均正常，无 Warn/Error。损坏文件投放：运行期写句柄独占（`FILE_SHARE_READ`）且新进程不读旧文件，按设计无法从外部触发，改由自动化用例覆盖。PPT 场景尚未实测；HEAD 无 C1.5 计时插桩，同配置前后对比只能用新版本 Debug 对照 C1.5 Debug 基线。真实重页足迹 35–60 ms，S7d“不迁移”维持，但须写入 C2 交接。
- **用户实机补测（2026-11-26，Debug|ARM64）**：重页（923 笔、2.5 MB）冷读 9 次全部完整：后台 worker p50 48 ms（旧链路 98/246 笔页读盘+解析 117–158 ms），绘制线程足迹 95 ms + 安装 14 ms，GPU 冷重建 289 ms（64%），合计 453 ms（旧链路小得多的页 290–370 ms）。**PPT 通过**：Clear 写入与前缀拼接、跨 Clear 撤回读文件、退出放映再进入续写同一文件并成组读回 2 页，无 Warn/Error；重进时 lane 首页 `ioMs=367`（推测为新文件首次被另一句柄打开时的系统扫描），暂不处理。数据见 `research/s7b-load-benchmark-20261126.md`。同轮实测发现多指绘图抬笔/落笔绑定错误（相距很远的同时抬落接触之间出现连线），与本任务无关，用户定为 C2.2 修复，已登记父任务地图。
- [ ] 更新 `.trellis/spec/`（UInk 存储与读取约定）；更新父任务地图；C2 `prd.md` 记录接手 GPU 冷重建；父任务“异常退出与卡死兜底”待办补记退出等待无超时。
- **spec 已改写（2026-11-26）**：新建权威合同 `.trellis/spec/native-desktop/uink-storage.md`（五层结构与写入格式、读取容错与足迹预算与日志、白板/PPT/桌面服务与整理、已知限制），并改写 `draw3-integration.md`（桌面快照、PPT 按页保存两节重写，删除 `uink_file` sibling 与旧白板合同）、`whiteboard.md`（存储与冷读撤回、视角持久化、读取失败口径）、`runtime-and-rendering.md`（Clear 封存与跨 Clear 读取）、`shapes.md`（UInk 1.1 图形对象层，私有 v1–v7 条款标历史）、`eraser-attributes.md`（橡皮笔尖换算）、`errors-logging-and-resources.md`（`Module::UInk`、`RecordDropped`）、`canvas-freeze-boundaries.md`（F2 与终态交接改用现行服务，命名锁条款标历史）、`ppt-interop/native-session-ui3.md`（结束页 `pageKind`）与索引。父任务地图（C1.4 状态、C2.1/C2.2 登记、“异常退出与卡死兜底”补记退出等待无超时）与 C2 `prd.md` 交接（GPU 冷重建新基线、足迹迁移评估）已于 2026-11-26 完成。
- [ ] 用户授权后提交（不带 AI 署名；签名失败即停）。

## 回滚点

- 每步结束时的工作区差异另存为仓库外补丁（`%TEMP%\inkeys-codex\c14\after-Sn.diff`）。
- 因为每步结束时都能构建、测试全过，任何一步出问题都可以回退到上一步的补丁。
- S1–S3 只动共享库（三个场景仍走旧存储与旧适配路径），S4 起才逐个切换场景，S6 最后清理旧路径并切换根目录。
