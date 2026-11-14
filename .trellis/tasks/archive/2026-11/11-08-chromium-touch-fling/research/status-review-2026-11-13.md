> 本文为清理前HEAD `241f1f44`的只读调查快照。用户后续已授权收尾清理、提交与归档，当前依据见 [收尾验证](../cleanup-validation-2026-11-13.md)；这里的代码行号、产物哈希、只读限制和in_progress状态仅对应当时，不代表清理后的接口或最终状态。

# 白板拖动任务完成情况与审查（2026-11-13）

## 结论与本轮范围

**基础拖动/惯性及 Mouse、Touch、Pen、PenTail 多设备实现已交付并通过适用自动测试；修复后的真实设备验收尚未完成。PTP 双指与系统惯性明确延期，不能算当前可用功能。** 两项既有任务保持 `in_progress`，不新建任务、不归档。

本轮由用户授权只读审查代码和更新 Trellis；未修改任何产品/测试/工程代码，未启动交互 GUI/Computer Use，未 commit/push。上传路线图仅作为未来需求和规划记录，不执行其实施步骤，也不延续旧任务中的 commit 授权。

审查基线：`feature/whiteboard3`，HEAD `241f1f44d23175e05fc1da4ecd1e29da67722414`（2026-11-12）。初始工作区无未提交产品修改；普通 `git status` 因 HiMsg 子模块元数据访问受限失败，使用 `--ignore-submodules=all` 检查主仓库，未把它当成产品故障或修改子模块。

## 任务关系与完成矩阵

| 范围 | 代码/已有验证 | 仍未完成或已被替代 |
| --- | --- | --- |
| `11-06-whiteboard-canvas-pan-inertia` 基础平移 | `708cb6bc` 已接通白板移动模式、DIP 换算、惯性、设备仲裁、停止钩子 | 原摩擦耦合/Slipping/hover 制动合同已被后续任务取代，不再按旧 AC 判当前产品 |
| 每页视角与背景 | 已实现每页视角记忆及冷页恢复；Freeze 背景跟随，Draw3 保留编译期开关后备 | 真实背景错位/4K 性能、DWM、功耗及完整多屏/Win7 验证仍缺 |
| `11-08-chromium-touch-fling` 手感迭代 | `7b213d90` 移植参考模型；`4b4edb91` 改产品释放/向量摩擦/残余 boost；`961ffdf8` 修鼠标帧节拍 | 任务名是历史名称，当前产品不等于完整复刻 Chromium 曲线；物理屏幕尺度/profile 尚在设计 |
| 四类设备与独立惯性设置 | `241f1f44` 包含 Pen/PenTail 真实准入、hover 规则、六 bool 偏好/热更新、设置与二十语言 | 修复后真实 Mouse/Touch/Pen/PenTail 拖动→回绘制仍待用户验收 |
| 输入采样与唤醒 | 导航保留回调级 2048 有界事件历史；Down/终态/控制 wake 可提前唤醒；普通 Move/位置流保持活动节拍 | RTS 仍只读每次 Packets 回调批末点，用接收 QPC；批内全样本与硬件时间未实现 |
| 触控板扩展 | 已用 quarantine 在 DM COM/Activate 之前禁用，保留偏好和停用实现的回归测试 | PTP 双指/系统惯性所有平台均不交付；未来按新路线规划，无本轮实现授权 |

本轮已在两任务入口和状态备注增加当前依据；保留旧 PRD/设计/清单用于追溯，没有把旧阶段所有 checkbox 机械勾成完成。

## 当前可用行为

- 仅白板移动（Selection）模式允许 MouseLeft/Touch/Pen/PenTail 导航；MouseRight 不参与，桌面/PPT/绘制模式不新增漫游。
- 惯性总开关默认开；Mouse 关、Touch 开、Pen 开、PenTail 关；Touchpad 偏好默认开但前端停用。关闭惯性仍保留真实拖动和 Move/Up 闭合，热开启不复活旧速度。
- 移动模式笔尖/笔尾 hover 不急停；允许接管的实际 Down 取消旧运动。来源互斥、准入失败吞到终态及跨来源不继承 boost 仍适用。
- 起步 15 DIP slop；产品释放使用可靠尾段估速与向量摩擦，速度上限 12000 DIP/s。没有新增缩放/旋转、通用滚轮平移或绘制中的双指漫游。

## 审查问题与证据

### A. 运行中四类输入：未发现可确定的新代码缺陷

主会话核对背景/页面/产物与日志，独立 `trellis-check` 子代理全程只读核对生产输入、准入、惯性热开关、hover/mailbox 和停止封口，未发现当前四类链路中可确定的新缺陷。这不是无缺陷保证；尤其 OS/驱动输入递送不在无窗口探针覆盖范围内。

关键代码锚点（均基于本次 HEAD）：

- `Draw3.ContactInput.cpp:78`：Touch/MouseLeft/Pen 共用导航 producer predicate，倒置笔保留完整生命周期。
- `Draw3.DrawingController.cpp:2916,3141`：实际历史顺序、来源互斥、Down 倒置锁存与惯性热切换；合法最后 Move/Up 不被 Stop 丢弃。
- `Draw3.DrawingController.cpp:16069`：合法 panManaged Pen 不会因持续 inContact 每帧 Stop 自己，移动模式 hover 不急停。
- `Draw3.WindowControl.cpp:46,380-386`：quarantine=true 且在窗口写入、COM/viewport/Activate 前返回，当前不能创建可用 PTP producer。
- `Draw3.DrawingController.cpp:3247,13286,13456,13702`：视角侧表及冷页恢复；`Draw3.WhiteboardModel.h:27`：背景默认归 Freeze。

### B. P2：触控板设置仍可勾选，却未提示功能当前不可用（本轮不修）

`Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:890-906` 仍无条件呈现 Touchpad 子项，`Inkeys/src/i18n/zh-CN.jsonc:633-642` 只有“触控板”和按输入类型调整的通用文案；对应前端却被 quarantine。用户勾选它不会得到 PTP 双指导航，容易误认为设置失效。偏好保留符合此前决定，但未来产品收尾应单独决定不可用提示/禁用态；本次只记录，不改 UI、配置或文案。

### C. 已知性能风险与验收缺口（不当作新复现故障）

前置任务 S8/`spec/native-desktop/whiteboard.md` 已记录：屏外 WARP、无 DWM、60 Hz 节拍下只有约 22–35 个新纹理像素帧/s、滞后最多 3–4 个视口样本；4K/100% 点阵 D2D+GetDC 约 32–39 ms/次，且不含 ULW/DWM。高速拖动可能有背景/墨迹错位及共享 UI3 线程成本，**本轮没有重测真实显示，不能把旧屏外数据说成真机故障或帧率验收通过**。Draw3 后备仍有启动 DPI 锁存等已知边界。

修复后四类真实设备、RTS 拖出窗口/捕获变化、DPI/多屏、实际手感与背景一致性、Win7/其他架构均未因此验收。2026-11-08 的“当前没问题”只覆盖当时已确认体验基线，不能外推 2026-11-12 多设备回归已恢复。

### D. 任务记录的完成状态存在历史叠加（本轮已澄清入口）

- 前置任务 S6/S7 未勾选，与 S8 已实施/已验证共存；旧合同还要求摩擦耦合及 Pen hover 制动。
- 后续任务“当前续接修复”、已勾选 DM 接线、旧五类支持和旧 commit 授权混在同一入口，容易误认为 PTP 仍启用或当前可直接继续实施。
- 本轮在 PRD/设计/执行计划/阶段摘要、task notes 和 implement/check context 顶部增加当前结论及本报告/未来路线链接。历史正文保留，当前未验收项不虚报完成。

### E. 既有 Trellis context 大文件截断告警

两任务 `task.py validate` 均 exit0，但原有多个 spec/research 超过 32768 字节，自动注入会截断；这属于上下文完整性风险，不是本次源码失败。本轮两份新增摘要均在阈值内并置于后续任务 context 前部；未来仍需按索引直接读取相关大文件章节，不应只依赖注入片段。本轮未修改全局阈值或重构规范。

## 本轮实际验证

使用现有 Debug|ARM64 产物，以 `CREATE_NO_WINDOW` 启动既有提前返回入口；逐项 timeout 180 秒，检查实际退出码。配置与设置测试沿现有隔离/屏外路径，没有启动产品主窗、设置窗或浏览器。

| 验证 | exit | 秒 |
| --- | --- | --- |
| `--no-window --canvas-pan-only` | 0 | 0.781 |
| `--draw3-control-fence-test` | 0 | 0.703 |
| `--draw3-canvas-pan-feel-test` | 0 | 2.563 |
| `--config-boundary-test` | 0 | 1.766 |
| `--no-window --setting-general-only` | 0 | 36.375 |
| `--draw3-diagnostic-output-test` | 0 | 1.829 |

pan-feel 末行：`PASS failures=0 rows=2112 no_gpu=1 no_hwnd=1`。设置专项 exit0；diagnostic-output 为真实队列/pipe，`hwnd=0`。配置边界专项通过，但本轮没有单独重跑完整配置元数据套件；六 bool 的往返/非法类型/NoUpload 等证据沿用同一产物已通过的完整 Headless 和既有代码审查。

命令、各产物 SHA、stdout SHA、退出码、耗时及输出见 [本轮验证记录](review-2026-11-13/results.json)；pan-feel 为避免重复存放 2112 行而只保留头尾摘录，完整原始输出另存 results.json 所列本机临时路径。其他五项保留完整输出。

- `Inkeys.exe` SHA256：`f12e5853586cd0be65f128b9b0df57d0151fe749fe797daa87e692bc4d0dd339`。
- `InkeysHeadlessTests.exe` SHA256：`658462f08ac14615cc91b89da04ee249720dbb400bcbc21febff6ccf53f309f4`。
- `Draw3.WindowControl.cpp` SHA256：`1b535190e51347d2ba23684dd6d6abfca09039c028176ac6b6b292e2d37f2ea2`。

三项均与 2026-11-12 最终 quarantine 验证记录一致。复核 `Build/Logs/dm-rts-quarantine-final-build.log` 为完整 `InkeysRepo.sln Debug|ARM64` 构建成功、0 errors；`dm-rts-quarantine-headless-all.log` 末行 `PASS animation correctness`。本轮仅文档改动，不重建也不重复约 494 秒的完整 Headless；历史完整构建/回归不是本轮新执行。更早回归曾有未修改调度测试的偶发失败，原始失败与同产物重跑通过记录保留在 `multi-input-regression-validation.md`，不因本次专项通过而抹除。

当前 Python 默认 WindowsApps 别名无法启动；使用已安装 `C:/Program Files/KiCad/10.0/bin/python.exe` 完成上下文和无窗口验证编排，未改全局环境、依赖或工具链。这是本机命令入口问题，与代码缺陷无关。

## 未来规划已记录，当前不实施

- [接纳记录与 P0–P4 待办](input-backend-future-plan-2026-11-13.md)；[上传文档全文快照](input-backend-roadmap-2026-11-12.md)。
- 未来主线：RTS / WM_POINTER 可选后端；教师大屏优先 RTS、Win8+ 笔记本优先 Pointer、Win7 继续 RTS/鼠标；Pointer 模式再接 DM/PTP，Win11+RTS 特殊 API 独立实验。缩放和性能优化独立设计。
- 特别记录上传文档审阅基线与当前代码的差异：绘制 latest 与导航 2048 历史必须同时保留；新后端复用现有 Publish*，不能为了“每帧最新点”丢掉导航估速历史。旧“移动模式切换 RTS/DM”仅保留历史候选，不再作为主线。
- 未来 P0–P4 均未勾选；没有新增后端、API 绑定、DM 启用或任务创建。RTS/DM 冲突证据和解除隔离前的真实系统/硬件测试门槛继续有效。

## Trellis 文件验证

两任务 context manifest 验证、JSON/JSONL 解析、局部链接和 `git diff --check` 均通过；既有文件只增加入口说明或替换 notes，原正文、BOM、UTF-8/CRLF 保持，测试原始输出保留其自身格式。所有已跟踪/新增改动均位于两个既有任务目录，未改产品代码、spec 或工作流配置。大文件截断告警见上文 E。
