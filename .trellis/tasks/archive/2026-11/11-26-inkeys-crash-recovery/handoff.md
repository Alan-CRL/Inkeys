# 交接：C1.6 教学安全模式与崩溃自动恢复墨迹

- 更新：2026-11-26（规划完成；用户要求压缩会话后**一次性完成 C1.6 全部三个子任务**，与 Codex 协作）
- 父任务：`.trellis/tasks/11-26-inkeys-crash-recovery/`（本目录），上级为中期重构 `.trellis/tasks/11-16-inkeys3-midterm-refactor/`
- 子任务：
  - C1.6a `.trellis/tasks/11-26-teaching-safety-restart/`
  - C1.6c `.trellis/tasks/11-26-debug-abort-button/`
  - C1.6b `.trellis/tasks/11-26-ink-append-recovery/`
- 分支：`refactoring/draw`（与 `origin` 同步；C1.4 及之前已 push）
- **任务文件全部未提交**（4 个新任务目录 + 中期重构 `prd.md`、`task.json` 的修改）。代码零改动。

## 1. 现在在哪一步

- 三个子任务 `prd.md` 已写完并经三轮用户确认；C1.6a、C1.6b 有 `design.md`（均为经 Codex 对抗审查后的第 2 版）和 `implement.md`；C1.6c 只有 `prd.md`（含实施要点）。
- 用户最后一句（2026-11-26）：“写好 handoff，然后一次性完成 C1.6，和 codex 协作。”**这句话就是实施放行**：压缩会话后，按 §4 顺序直接开工，不必再问是否开始。仍需遵守：不 commit、不 push，除非用户另行授权。
- 开工第一件事：`python ./.trellis/scripts/task.py start .trellis/tasks/11-26-teaching-safety-restart`（状态 → `in_progress`）。每个子任务完成后再 start 下一个。父任务不 start。

## 2. 先读顺序

1. 本文件
2. 当前子任务的 `prd.md`（“用户确认”各节是现行决定）→ `design.md` → `implement.md`
3. `implement.jsonl` 列出的 spec（已登记）
4. 需要时看 `research/`：Codex 调查报告与设计审查记录

## 3. 用户已定、不得再议的决定

**C1.6a**
1. 设置“常规 → 基础”分组“安全与恢复”改名“教学安全模式”；“崩溃处理方式”标题不变，三档：提示并重启（默认，值 0）、静默重启（1）、直接退出（2）；越界回 0，旧配置不迁移。
2. 新开关“崩溃重启后自动恢复墨迹”`Application.CrashRecoverInk`，默认开。
3. 两类路径都按档位处理：Windows 崩溃回调；软件内致命错误与卡死（例：内存满多指书写导致 Draw3 停止；主栏资源失败无限重试导致卡死）。
4. **只做进程内拦截**，沿用现有 UEF + 监督进程，不加常驻守护进程。`__fastfail`、栈溢出、堆损坏、被强制结束不保证重启。
5. 卡死阈值：真实故障持续 **10 秒**；UI3 / Draw3 单个工作单元 **15 秒**无进展。

**C1.6b**
6. 只在 `CrashRecoverInk` 开启时追加写入；关闭时保持 C1.4 行为。三场景都做。
7. 桌面：Clear 后不建文件；Clear 后第一笔抬起才建文件并写入，之后逐笔追加。
8. 所有写入和已有性能记录都必须异步，不得延迟下一次落笔。
9. PPT / 白板：回翻页上落笔时把该页放到文件末尾再追加；翻到新页不整理；离开白板或 PPT 时按正序整理。回翻要记录当前页。
10. 崩溃重启与非正常结束（断电、被强制结束等）都恢复。
11. 恢复规则（崩溃场景 = 崩溃时所在工作区）：
    - 桌面 / 白板是崩溃场景且有墨迹 → GDI `MessageBox` YesNo 弹窗，给出场景与最后保存时间；选“是”恢复（白板回到白板并停在记录的页）。
    - 不是崩溃场景但有墨迹 → 静默恢复，界面留在桌面。
    - PPT 永不弹窗；崩溃后的**下一次启动期间**再次识别到同一文稿时，按同进程方式按页码恢复，不跳页；这次没打开就不再恢复。
    - 静默重启档（带 `-CrashTry`）不弹窗直接恢复；非正常结束后手动启动一律按上表弹窗。
12. 去掉设置项“启用桌面画布自动保存”和 `Drawing.AutoSave.Enable`，桌面始终保存（以后会加按天数自动清理）；“绘制 → 保存”子页**保留并直接留空**（像“显示器”页），不删、不加说明。

**C1.6c**
13. 调试页三个按钮：野指针崩溃（**不用真 `abort()`**）、模拟 Draw3 致命错误、模拟 UI3 卡死。默认每个按钮先弹 OkCancel 确认。

**协作**
14. **Claude 写代码，Codex 审查**（只读）。收尾顺序：Claude 独立审查 → Codex 审查 → Claude 修复。子代理不并行。

## 4. 实施顺序与每步门槛

顺序：**C1.6a → C1.6c → C1.6b**（b、c 都依赖 a；c 的按钮用于 a、b 的实机验证）。

每个子任务按其 `implement.md` 的步骤执行；每步：
1. Claude 改代码（Edit 或 `perl -pi`；**不要用 Git Bash `sed -i`**，会把 CRLF 改成 LF）。
2. 构建通过，跑相关测试与自检。
3. 用 `git diff --stat` 与 CRLF / BOM 检查确认无整文件差异（我改动的 `.cpp/.cppm/.h` 必须 UTF-8 BOM + CRLF）。
4. 每个子任务结束时：Claude 逐行审 diff → Codex 只读审查（简报附 design.md）→ 修复 → 复验。
5. 子任务完成后：更新 `implement.md` 进度行；按 Trellis 3.3 更新 spec（C1.6b 主要改 `.trellis/spec/native-desktop/uink-storage.md`；C1.6a 改 `errors-logging-and-resources.md`、`configuration-i18n-and-assets.md`）；**不提交**，等用户授权。

C1.6b 先做 P0 预研（见其 `implement.md`）：测 953 笔真实重页 `CaptureCanvasTail` 耗时、核对跨文件原样复制页段、摸清白板冷页批量建立与 PPT `Adopt` 改动面。预研中的临时测量改码，测完必须还原。

实机验证（三档 × 三种触发 × 三场景、结束进程后手动启动）需要用户操作：全部代码完成后，整理一份实机清单交给用户。

## 5. 设计要点速记（细节以 design.md 为准）

**C1.6a**
- 所有致命情况汇入现有 UEF：CRT 入口（`SIGABRT` 覆盖 `terminate`、invalid parameter、purecall）和新增看门狗线程都抛自定义 SEH `0xE1430100`，进入 UEF。
- 新模块 `Helper/Helper.Fatal`：`Report`、`RaiseNow`、`Beat/Idle`（卡死）、`RecordFailure/RecordSuccess/ClearFailure`（持续失败）；热路径接口只写原子量。
- 看门狗在启动门禁完成后启动；意图槽非 0、UEF 处理中或调试器附加时不触发。
- **持续失败只在各客户端的真实故障分支记录**，不能把调度器的 `Retry` 当失败（`Retry` 也用于延期、退避、等回执；例如设置窗口被遮挡时一直 `Retry`）。
- Draw3 意外停止：`IdtMain.cpp:4265–4290` 改为直接 `RaiseNow`，**不能先 `SetOffSignal(1)`**。
- 运行期已知失败强退链（`PublishFatalFailedCleanupNoWait`）按档位抢 `CrashRestart`。
- 提示档：`Show()` 返回 `Failed` 也重启；报告阶段 15 s 超时直接结束是已知限制。
- 自定义 SEH 可达性必须用隔离子进程逐入口实测（Debug + Release ARM64）。

**C1.6b**
- 抬笔从已提交的 `storedStroke` 生成单笔快照 → 有界入队；worker 用 `AppendContent` 追加到最后一个 Canvas；回翻页首次落笔先 `RelocatePage`；新页 `AppendPage`。
- 每个请求带基准 / 目标修订，失配转 `Full`；`Append` 不推进 Clear 前缀身份。
- 撤回等用可让出的整页截取 + `Full`；不在任何输入路径上 `Drain`。
- 桌面“活动区间文件”；跨 Clear 冷读改为“已封存区间 + 按范围 `ReadUInkPage`”（现有 `LoadSnapshot` 要求单 Canvas 且 Header GUID 等于请求 ID，不能复用）。
- 会话记录 `Inkeys\Save\Ink\session\`：先 `CREATE_NEW` 拿 DELETE_ON_CLOSE 存活锁再发布 JSON；恢复认领锁；写前登记 `{fileId, start, end}` 判定断电半页；NTFS FileId 识别整理后的文件。
- 恢复“读旧写新”：页段按字节复制进新文件，沿用旧 Workspace GUID；按场景在落盘且装入成功后才结算。
- PPT 懒建存储时先 `Adopt` 再放行首次 `LoadLane`；白板装入需重建 `InkCanvasCollection` 并保留页号空洞，经 `RequestWhiteboardActive` 进入后显式加载目标页。
- 诊断（`lift-path`、`page-save`、`page-load`）改为定长数值记录 + 无锁环形缓冲，日志 worker 格式化。

**C1.6c**：`Setting.cpp` `settingTabEnum::tab9` 新分组 + `Design::ButtonGroupRow`；野指针在设置线程写 `volatile` 空指针；模拟致命错误 = Draw3 调试注入标志使 `Run` 抛异常；模拟卡死 = `RenderPipeline::PostControl` 投递 `Sleep(20000)`。

## 6. 环境与工具

- **构建**：`InkeysRepo.sln` `Debug|ARM64`，ARM64 原生 MSBuild：`C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe`，超时 ≥ 5 分钟。可复用 `%TEMP%\inkeys-codex\c14\build.sh <日志名>`（完整 solution）与 `build-uk.sh`（`inkStrokeModelerTest.sln`，L2 改动需要）。构建前若有残留 `mspdbsrv.exe`：`taskkill //F //IM mspdbsrv.exe`。偶发 LNK1104（exe 被占用）重试即可。
- **自检**：`%TEMP%\inkeys-codex\c14\selftest.sh <前缀> <参数...>`；自检参数全等匹配，用 `& exe | Out-File` 方式运行，避免尾空格误启动产品。
- **i18n**：只改 zh-CN，再用 **pwsh 7** 跑 `Scripts/i18n.ps1 sync`（Windows PowerShell 5.1 解析失败），其余语言补译（可交 Codex Terra，每路 ≤ 5 个文件）。
- **设置页**：改设置页结构可能打破 `setting_design_tests` 导航用例；本任务不改侧栏结构，仍需跑一次导航测试。
- **Codex 调用**（本会话已用、均成功）：
  ```
  cat constraints.md brief.md | "$LOCALAPPDATA/Programs/OpenAI/Codex/bin/codex.exe" exec \
    -C "D:\Project\Inkeys\Repo\Inkeys" -m gpt-6.2-sol -c model_reasoning_effort=high \
    -c service_tier=fast --sandbox read-only -o report.md - > run.log 2>&1
  ```
  - codex-cli 0.171.0；简报与报告放 `%TEMP%\inkeys-codex\c16\`（已有 `constraints.md` 可复用），不进仓库；后台运行、等通知，不轮询。
  - 本会话用户未另说明 Fast 可用性，沿用 11-26 早些时候的“Sol fast 可用”；若用户说明不可用改 `service_tier=default`。
  - Codex 沙箱不能起子进程（不能 git / 构建），审查让它直接读工作区；简报要求“FACTS / INFERENCES / UNKNOWN”、severity + file:line + 修正建议；避免“恶意 / 攻击 / 注入”措辞（可能触发过滤中断）。
  - 旧脚本 `c14\run-codex.sh` 设置了 `HTTPS_PROXY=127.0.0.1:7890`；本会话不设代理也能运行。

## 7. 文件地图

| 文件 | 内容 |
|---|---|
| 本目录 `prd.md` | 用户原始需求、任务地图、跨子任务验收 |
| `11-26-teaching-safety-restart/prd.md` | C1.6a 需求、现状、第一轮确认 |
| `11-26-teaching-safety-restart/design.md` | C1.6a 第 2 版设计（§7 是 Codex 审查处理记录） |
| `11-26-teaching-safety-restart/implement.md` | S1–S6 步骤；附录待填 S4 客户端故障分支清单 |
| `11-26-teaching-safety-restart/research/codex-fatal-paths-20261126.md` | 全部致命退出与卡死路径调查（带 file:line） |
| `11-26-teaching-safety-restart/research/codex-design-review-20261126.md` | C1.6a 第 1 版设计审查 |
| `11-26-debug-abort-button/prd.md` | C1.6c 需求与实施要点 |
| `11-26-ink-append-recovery/prd.md` | C1.6b 需求与三轮确认、默认处理 |
| `11-26-ink-append-recovery/design.md` | C1.6b 第 2 版设计（§12 是审查处理记录） |
| `11-26-ink-append-recovery/implement.md` | P0、S1–S8；附录 A 待 P0 填写 |
| `11-26-ink-append-recovery/research/codex-write-chain-20261126.md` | 三场景写入链调查 |
| `11-26-ink-append-recovery/research/codex-design-review-20261126.md` | C1.6b 第 1 版设计审查 |
| `11-26-ink-append-recovery/research/uink-v11-spec.md` | UInk 1.1 规范副本（规则编号如 SAVE-7 都指它） |
| C1.4 归档 `../archive/2026-11/11-24-uink-1-1-adaptation/design.md` | 存储分层、`uink_store`、`PagedInkStore`、桌面快照的设计依据 |
| `.trellis/spec/native-desktop/uink-storage.md` | UInk 存储唯一权威合同（C1.6b 完成后要更新） |

## 8. 已核实的关键事实（实施前仍按行号复核）

- 现有崩溃模式 2/3 的中文界面与实现对调（2 实为静默结束、3 实为交给系统）；改三档后消失。
- 未注册 `set_terminate` / `SIGABRT`；`_set_purecall_handler`、`_set_invalid_parameter_handler` 设为空。
- 项目默认 `/EHsc`，产品代码无 `__except`、无 VEH。
- 主栏 `EnsureDeviceResources` 失败返回 `Continue`→`Retry`，无上限（`Bar.RenderLoop.cpp:9350–9371`）。
- Draw3 停止后主循环 `SetOffSignal(1)`、退出码 6，不重启（`IdtMain.cpp:4265–4290`）。
- 正常退出已有 15 秒监督强退（`IdtMain.cpp:358`）。
- 内置 `MessageBox` 有 `Buttons::YesNo`；`MakeOkRequest` 的关闭结果也是 `Ok`。
- 橡皮导出为可追加的 `UInkInkKind::Erase`；`Compact` 已按 `(pageIndex, writeSequence)` 正序。
- `PagedInkStore` 准入锁内有节点分配与队列扫描，编码与写盘在锁外；`LoadLane` 遇空 `confirmed` 会确认空槽；Host 懒建 PPT 存储时新生成 Workspace GUID。
- 桌面 `LoadSnapshot` 要求整读 `canvasCount == 1` 且 Header GUID 等于保存请求 ID。
- “绘制 → 保存”子页目前只有“启用桌面画布自动保存”一个开关（`Setting.cpp:3898–3913`）。

## 9. 风险与开放点

- C1.6a：合法长任务超过 15 秒被误判卡死（用低端设备实测确认）；漏掉某个客户端的真实故障分支（S4 列清单交 Codex 复核）。
- C1.6b：跨文件原样复制页段的合规性（P0 核对，不合规改为重新编码）；白板冷页批量建立是最大的 Draw3 改动；整页截取在重页上的耗时；Canvas 字段（视口、背景）只随整页写入更新。
- Win7、兆芯、Win32/x64 构建留到中期重构最终集成时验证，届时须提醒用户。
