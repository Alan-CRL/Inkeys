# 交接：C1.4 + C1.5 联合任务（UInk 1.1 适配与冷页加载提速）

- 更新：2026-11-25（规划完成，等待用户放行实施）
- 任务目录：`.trellis/tasks/11-24-uink-1-1-adaptation/`（主任务）；`.trellis/tasks/11-24-whiteboard-uink-load-speed/`（C1.5，已并入，只作历史与计时工具来源）
- 父任务：`.trellis/tasks/11-16-inkeys3-midterm-refactor/`
- 分支：`refactoring/draw`

## 1. 现在在哪一步

- **2026-11-25 用户已放行实施，`task.json` 为 `in_progress`。** S0、S1、S1b（单遍流式解码，用户批准、Astra 单次授权）、S2、S3、S4（a/b/c）、S5（a/b/c）、S6（a/b/c，2026-11-26）已完成并通过验收（每步：Codex 写 → Claude 构建两个 solution/测试/自检/变异 → Codex 只读审查 → 修复 → 复验）；逐步结果写在 `implement.md` 各步“进度”行，基线见 `research/s0-baseline-20261125.md`。各步回退补丁 `%TEMP%\inkeys-codex\c14\after-S*.diff`。**全部未提交。**
- **S8 审查与修复已完成（2026-11-26）**：结果见 `implement.md` S8 进度行，最新补丁 `after-S8.diff`；spec 已改写（权威合同 `.trellis/spec/native-desktop/uink-storage.md`，见 `implement.md`）。**下一步：用户按 `research/s8-manual-test-checklist.md` 实机验证（进行中）** → 父任务地图与 C2 交接 → 用户授权后提交。**2026-11-26 更新：实机（白板重页 Release/Debug、PPT、桌面 Clear）全部通过，父任务地图与 C2 交接已完成；只剩用户授权后提交。**
- **S7 已全部完成（2026-11-26）**：S7a 统一日志、S7b 计时与基准、S7c 空闲整理（含修复），S7d 足迹迁移裁定为本任务不做；最新累积补丁 `after-S7.diff`，逐步结果见 `implement.md` 各进度行，基准数据 `research/s7b-load-benchmark-20261126.md`。**下一步 S8**：Claude 完整独立审查 → Codex 完整只读审查 → 修复 → 全量门禁 → 用户实机（重页翻页前后 `[page-load]` 对比、PPT、桌面 Clear、损坏文件投放、C09/C10 真实夹具、hidden 触摸重连偶发项观察）→ spec 改写（§7 所列）→ 父任务地图与 C2 交接 → 用户授权后提交。变异脚本 `%TEMP%\inkeys-codex\c14\mutate.sh <名> <文件> <原串> <变异串> [测试开关]`（单行匹配；CRLF 文件不要用多行原串）。以下为 S7 进行中时的旧说明：`Module::UInk` 日志（`Log.h` 枚举 + `Log.cpp` 名称表）、把 `[Draw3.WhiteboardStore]` 等诊断前缀与 C1.5 计时插桩统一到日志系统、Release 计时实测、整理策略（初值 50% 且 8 MiB，含损坏段所在页有新版本后触发）。旧存储代码、私有字段、冻结参考解码器已在 S6b 删除，存储根已在 S6c 切到 `Inkeys\Save\Ink`。headless 置顶类失败数在 S5 起为 9（S4 期间 2），headless 不编入改动文件，S8 请用户在干净桌面复测；hidden 自检两条触摸重连用例偶发失败、重跑通过（S4c、S6b 各见一次）。最新累积补丁 `after-S6.diff`。
- 构建/测试脚本：`%TEMP%\inkeys-codex\c14\build.sh`、`build-uk.sh`（模型器 solution）、`selftest.sh`、`run-codex.sh`（Sol）、`run-codex-astra.sh`（仅用户授权时）、`crlfcheck.py`。Codex 沙箱构建后常残留 `mspdbsrv.exe`，构建前 `taskkill //F //IM mspdbsrv.exe`。简报避免“恶意/攻击/注入”等措辞（会触发 OpenAI 网络安全过滤中断会话）。
- 以下为规划阶段的原始说明（保留）：规划**已完成并经两轮 Codex 审查修订**。
- 先读顺序：本文件 → `prd.md`“第三轮决定” → `design.md` → `implement.md`。

## 2. 文件地图

| 文件 | 内容 |
|---|---|
| `prd.md` | 需求与验收。**“第三轮决定（2026-11-25）”一节是现行决定**；前两轮（11-24）是历史，与第三轮冲突时以第三轮为准 |
| `design.md` | 五层结构、1.1 字段映射与橡皮换算、读取容错与资源上限、`uink_store`、场景服务（白板/PPT 共用 `PagedInkStore`、桌面快照）、退出与失败出口、图形对象层、外来内容回退、日志、性能预算、测试 |
| `implement.md` | 协作流程、每步硬门槛、简报固定要求、S0–S8 步骤、回滚点 |
| `research/uink-v11-spec.md` | UInk 1.1 Beta 单文件规范副本（规则编号如 SAVE-10、NIB-3 都指它） |
| `research/codex-investigation-20261125.md` | Codex 现状调查：架构、字段差距、严格拒绝与崩溃面、场景存储、测试清单（带 file:line） |
| `research/codex-plan-review-20261125.md`、`research/codex-plan-rereview-20261125.md` | 两轮规划审查与处理依据 |
| `research/uink-1-1-draw3-requirements.md` | 11-24 给规范作者的需求整理（历史，现状描述仍有参考价值） |
| C1.5 `research/load-timing-logs.md` | 计时日志格式与第一次实测基线（冷页 290–370 ms 的分段） |

## 3. 用户已定、不得再议的决定（摘要，原文见 prd）

1. 只写 1.1；读 version 10 按 1.1 规则尽力读；不做兼容导出；不识别旧 Inkeys 私有字段。
2. 私有片段全部改官方字段（`nib`、Canvas `background/pageState/pageKind/binding/viewport`、Shape `object`）。
3. 橡皮尺寸只在读写边界换算：写 `H = (s/2)·√(π/aspect)`，读 `s = 2H·√(aspect/π)`；矩形 aspect 0.6666667，方形 1；荧光笔 aspect 0.125，宽度**原值**往返。
4. 去 SHA、回读、历史 JSON、索引、多轨；整文件坏才失败，个别坏跳过并写 warning；读取接入 `Inkeys::Log`（产品边界输出，库返回诊断）。
5. 存储根 `<exe>\Inkeys\Save\Ink\`；旧 `Inkeys\AutoSave\` 不访问、不清理。
6. 白板与 PPT **同一种结构**、每进程新建文件；PPT 一文稿一文件夹，身份沿用现有规范化路径 key（只保证同进程），同进程再进入续写；以后再考虑 pptx Tag 或嵌入。
7. 页写入：**Clear 时**追加“上一区间 + Clear”；**翻页/离开/退出时**追加该页完整记录（能撤回到上一区间时含上一区间 + Clear，最多一个 Clear）；翻走再翻回仍能跨 Clear 撤回（保持原有行为）；**不做任何修正**（退出也不修正）。
8. 桌面：每次 Clear 只写当前区间快照（不带 Clear），退出写当前区间，不修正。
9. 每进程只写自己新建的唯一文件，不需要 JSON 状态文件。
10. 外来文件：自产无损，外来就近回退，不扩展渲染器。
11. GPU 冷重建留给 C2。
12. 为 C1.6（抬笔立即追加、崩溃恢复、以后实时追加直接新建文件）留好接口；扫描恢复本身属于 C1.6。
13. 分工：**Codex 写大部分代码并做独立审查，Claude 规划、简报、构建、读差异、变异验证、验收**；重点防偷懒、未达要求、越界改动。

我（Claude）自定、用户可否决的：整理阈值初值 50% 且 8 MiB（S7 实测定）；外来预设图形以重建结果显示（有意偏离 SHAPE-15）；退出等待不加超时（记入父任务“异常退出与卡死兜底”）。

## 4. Codex 调用方式（本机已验证）

- 脚本：`%TEMP%\inkeys-codex\c14\run-codex.sh <步骤名> <read-only|workspace-write> <简报文件...>`；报告 `<步骤名>-report.md`、日志 `<步骤名>.log`，同目录。
- 内容：`codex.exe exec -C D:\Project\Inkeys\Repo\Inkeys -m gpt-6.2-sol -c model_reasoning_effort=high -c service_tier=default --sandbox <...> -o <report> -`，简报从 stdin。
- **必须设 `HTTPS_PROXY=HTTP_PROXY=http://127.0.0.1:7890`**（脚本已设）；不设会 `workspace routing discovery timed out`。
- 本任务 Fast 与 Astra 都不可用，一律 `gpt-6.2-sol` + High + `service_tier=default`。
- 简报一律用 Write 工具落盘再 `cat | codex exec -`（Bash heredoc 会吃反斜杠、撇号出错）。
- 一次调查约 6–10 分钟，规划审查约 5 分钟；在后台运行并等待通知，不要轮询。
- Codex 在沙箱里无法构建，常见毛病：臆造枚举/函数、改签名漏测试调用点、顺手扩大范围、测试被 `if` 包住空转、共享头 `std::min` 宏冲突。验收时逐条核对它报告里的 “also/additionally”。

## 5. 构建与验证要点

- 产品：`InkeysRepo.sln` Debug|ARM64，ARM64 原生 MSBuild：`"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64 -m:1 -p:CL_MPCount=3 -nologo -v:m`，`MSBUILDDISABLENODEREUSE=1`，超时 ≥ 5 分钟。已复制好脚本：`%TEMP%\inkeys-codex\c14\build.sh <日志名>`（会先挪走 `Build/ARM64/Debug/Inkeys.ilk`）。内存紧张时并行编译会 C1060，所以用 `-m:1 -p:CL_MPCount=3`。
- 无窗口自检：`%TEMP%\inkeys-codex\c14\selftest.sh <前缀> <开关...>`（逐个运行并输出退出码与 fail 行数）。与本任务相关的开关：`--draw3-whiteboard-storage-test`、`--draw3-parked-desktop-exit-test`、`--draw3-parked-ppt-retained-test`、`--draw3-loaded-retained-install-test`、`--draw3-pending-topology-load-test`、`--draw3-fallback-stable-controller-test`、`--draw3-fallback-stable-lane-test`、`--draw3-ppt-current-load-retry-test`、`--draw3-ppt-completion-delivery-test`、`--draw3-hidden-test`、`--draw3-shape-core-test`、`--draw3-history-benchmark`（全部开关见 `IdtMain.cpp` 中 `L"--draw3-` 开头的字符串）。
- 模型器测试工程 `inkStrokeModelerTestTests` **只在 `inkStrokeModelerTest.sln` 里**，产品 solution 不含；涉及 `inkStrokeModelerTest/draw3/` 的步骤两个都要构建、都要跑。
- UInk 库源码在 `inkStrokeModelerTest/draw3/uink_*` 与 `shape_uink_*.inl`，同时编进 `Inkeys.vcxproj`、`inkStrokeModelerTest.vcxproj`、`inkStrokeModelerTestTests.vcxproj`；增删文件三处及 `.filters` 同步。
- 产品自检入口在 `Inkeys/IdtMain.cpp`（约 2107–2134 行一带）与 `Draw3.DrawingController.cpp` 内；启动自检用 `& exe 参数 | Out-File`，参数全等匹配，`Start-Process` 带尾空格会误启动产品。
- 改过的 `.cpp/.cppm/.h` 必须 **UTF-8 BOM + CRLF**；不要用 `sed -i`（会把 CRLF 改成 LF），用 Edit 或 `perl -pi`；改完 `git diff --stat` 确认没有整文件差异。
- 每步做单点变异验证（故意改坏 → 用例失败 → 还原）。

## 6. 工作区现状（开工前不要误提交或误还原）

`git status` 中与本任务相关的未提交内容：
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`、`Draw3.WhiteboardStore.cpp/.cppm`：**C1.5 的冷页计时插桩**（`[Draw3Diag][page-load]` / `[page-save]`，说明见 C1.5 `research/load-timing-logs.md`）。保留作对比工具，S4 改白板存储时字段要随之调整，S7 再定是否长期保留。
- `.trellis/tasks/11-16-inkeys3-midterm-refactor/prd.md`、`task.json`：父任务地图已登记 C1.3/C1.4/C1.5/C2 与第7步文档。
- 未跟踪：`11-24-uink-1-1-adaptation/`、`11-24-whiteboard-uink-load-speed/`、`11-24-draw3-phase-c2-canvas-tile-cache/`、父任务 research 里的第7步调研文档。
- 都未提交；提交需要用户授权，提交信息不得带 AI 署名，签名失败即停。

## 7. 现有 spec 中将被本任务替换的旧合同

实施时读这些章节只取“行为目标”（撤回语义、退出屏障、slot 生命周期、失败可见性），**存储格式、索引、SHA、多轨、私有字段等内容以本任务 design.md 为准**；S8 统一改写这些 spec：
- `.trellis/spec/native-desktop/draw3-integration.md`：“Desktop UInk 自动保存事务与退出屏障”“PPT 三态 slot、UInk 自动保存与当前进程恢复”“选择模式、当前页内容与可撤回 Clear”
- `.trellis/spec/native-desktop/whiteboard.md`：“存储、历史与内存”“场景：冷页读取失败的原页补呈现”“移动模式的画布平移与每页视角”
- `.trellis/spec/native/runtime-and-rendering.md`：“Ink Document Persistence”“Runtime Undo/Redo Cache And Page Restore”
- `.trellis/spec/native-desktop/shapes.md`（图形 UInk 元数据部分）、`eraser-attributes.md` / `palm-eraser.md`（橡皮形状存储部分）

## 8. 已知代码事实（实施时容易踩的）

- 白板现在翻走再翻回能跨 Clear 撤回：历史 JSON 存了 `undoBoundary` / `boundary` / `continuation`（`DC:922`、`:1069`）。PPT 页常驻，跨 Clear 撤回经 `PreviousInterval` 从文件读（`DC:24257`）。桌面从内存或快照文件恢复。
- 白板视角存在会话侧表 `whiteboardViewports`，冷读后侧表优先（`DC:18267`、`:18589`）；新文件里写 `viewport {x,y,1}`，侧表仍优先。
- Draw3 已有无预设对象组 `Kind::Compound`，现有测试要求往返（`DC:14147`、`inkStrokeModelerTestTests/shape_uink_tests.inl:38`）。
- 旧 PPT 保存前校验与导入依赖私有绑定字段与显式 viewport（`Draw3.PresentationAutoSave.cpp:1008-1046`、`uink_draw3_import.cpp:415-465`）；旧白板依赖 `uink_page_file` 严格自校验与历史 JSON。所以 S3 只**新增** 1.1 适配路径，旧路径到 S4–S6 各场景迁移时才删。
- 箭头、立体投影常量、闭合虚线算法已与规范 PRESET-9/14/21–23 一致，不要改算法。
- Host 退出等待无超时（`Draw3.Host.cpp:2850-2894`）；白板完成队列 `push_back` 与唤醒在 try/catch 外（`Draw3.WhiteboardStore.cpp:98-142`），新服务要按 design §5.5 修正。
- 荧光笔 Draw3 渲染半高下限 0.5px（`Draw3.HighlighterNib.h`），文件里写原值。
- 日志模块枚举在 `Inkeys/Inkeys/Log/Log.h`，名称表在 `Log.cpp:96` 一带且有 `static_assert`，新增 `Module::UInk` 两处同步。

## 9. 相关记忆（用户级 auto memory）

`phase-c14-uink11-state`（本任务决定摘要）、`codex-delegation-experience`、`claude-implements-codex-reviews`、`codex-fast-mode-terra-sol`、`planning-phase-no-code-changes`、`source-files-utf8-bom`、`gitbash-sed-strips-crlf`、`inkeys-selftest-launch-pitfall`、`abnormal-exit-hang-fallback-todo`、`uink-eraser-shape-official-field`（本任务完成后该项即落实）。
