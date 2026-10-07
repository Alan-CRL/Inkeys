# 技术设计与跨子任务合同

## 证据模型

- H0 冻结在 `8b156fca59f0337a6afc6d722941666fcf143080` 和任务创建前的空工作区。HF 每次交接记录 HEAD、tracked/untracked 清单及内容指纹；只有 HEAD 不足以标识未提交成果。
- HC 从 release/tag、构建元数据和实际二进制来源定位；H2 从 Inkeys2 发布记录、tag 与配置定位。设备、构建、效果和输入轨迹可比时才得出体验对照结论。对不可测 GPU/ULW/真实笔输入，使用生产模块 headless 成本数据并保留人工门禁。
- `execution-ledger.md` 是阶段与 owner 账本；`audit-coverage.tsv` 是逐提交集合；`findings.md`、`performance.md`、`validation.md`、`handoff.md` 保存证据与恢复入口。状态为：已验证通过、已确认失败、已修复待验证、未验证、需要人工、假设被排除、基线已有问题、不适用（附理由）。

## 依赖与文件所有权

```text
baseline-and-acceptance (冻结口径、回归表、commit 集合)
  → code-and-state-unification (状态合同与迁移表)
  → ui3-performance (UI3 生产链路)
  → draw3-performance (Draw3 生产链路)
  → commit-and-security-audit + crash-restart-recovery (只读风险调查可提前)
  → integration-and-release-check (全量复审、HF、发布矩阵)
```

前一阶段的完整 Solution 构建与适用测试是后一写入阶段的入口；安全风险调查可提前并行。主 agent 唯一写入跨域接口、`IdtState.*`、公共 `RenderPipeline`、`WindowService`、工程文件及公共测试入口；其他写入先冻结调用合同与文件所有权。所有 agent 共享工作树，性能采样与构建/扫描串行。

## 状态与线程合同

- 按业务意图列 authoritative、requested/target、applied/echo 三类状态，记录读写线程和副作用；不能按字段名合并。UI/消息线程发布命令和不可变快照；render/document/history/GPU 的 owner 执行资源操作。
- 核对 `ChangeStateModeTo*`、`SetPenWidth/SetPenColor`、`SyncDraw3State`、`ReconcileDraw3Presentation`、`StateModeTransitionRevision` 当前语义。统一入口覆盖 UI 高亮、光标、Draw3 bridge、窗口显隐/穿透和配置副作用；旧异步结果需 revision/generation 条件。
- UI3 在适用层复用控件/命中、capture、颜色/DPI、动画时钟、dirty/present、SVG/path 不变资源；保留 ImGui 设置、Bar 控件、专用物理动画与 Draw3 的职责差异。不引入第三套控件框架。

## 性能与呈现合同

- UI3 依 `input/wake → snapshot → target/layout → animation → resource → draw → GetDC/ULW/EndDraw/present → wait` 分段。计时区分 callback、attempt、success、idle、合法 Retry。共享 scheduler 其他客户端、锁等待和 GPU 同步纳入归因，避免重复计时。
- 动画使用同帧时钟，验证 raw dt、animation dt、Rebase、clamp、快速反向和失败呈现事务。无视觉变化不重绘；缓存按内容/尺寸/DPI/主题/stroke/transform/device epoch 正确失效且有容量边界；脏区包含旧区域、阴影与 AA。
- Draw3 保持输入队列顺序、Down/Up/Cancel、多接触和必要采样；单独记录采样、消费、成功 Present 率及 Down→首 Present、Up→稳定结果。L0/L1/L2、history、PPT 页身份、CPU/HLSL 布局、premultiplied alpha、resource unbind、device-lost 和 durable save 均是正确性合同。
- 正式可选 presenter 限定 DComp→ULW，两种 DWM 透明模式从自动回退及强制选择中禁用。Win7 SP1+KB2670838 无 DComp，Draw3/UI3 分别核 11.1 列表 E_INVALIDARG→11.0、Hardware 失败→WARP；保持 ULW 现有 FLIP_SEQUENTIAL，不增加 bitblt/swap effect 回退。用户报告该目标环境已实测支持 FLIP；微软文档的通用描述与之冲突，保留实测来源和待补设备细节。
- 只有可比前后测量超过噪声且行为不退化时，性能改动才计作提升；小收益复杂改动撤销或简化。正确性改动可留，但不能标作性能收益。

## 审计与安全边界

- 审计下界 2026-08-10 00:00 +08:00，终点 H0。列举可获得 refs，组合 author/committer 时间、拓扑、主线集成边界、merge 对比和 patch 等价，逐项审实际 diff；老日期新合入与 squash/import 单独追踪。未合入分支仅标风险，不自动合入。
- 严重软件错误审线程、资源、文件事务、COM、窗口与 device epoch；安全威胁模型审配置/UInk/SVG/图片、路径、下载/更新、DLL/进程/权限/IPC、日志/dump、依赖 advisory。finding 用触发条件、实际影响、可达性、当前证据、最小修复和验证定级。
- 崩溃链路画异常→报告→旧进程终止→重启者→新进程/单实例→恢复。正式默认不注入崩溃；隔离进程测试与真机 GUI 测试分开。最后已提交恢复点不能因半写入被覆盖。

## 验证与交接

- 先确认 Solution/项目脚本；构建 `InkeysRepo.sln`，优先 ARM64 原生 MSBuild，同一 PowerShell invocation 清除重复 `PATH` 并设 `MSBUILDDISABLENODEREUSE=1`，至少 5 分钟超时。构建输出目录只能有一个进程使用；环境问题不靠改源码绕过。
- 每单元先记证据和假设，再做最小补丁、相关生产逻辑测试、独立 review、修复复验。回退只撤销本任务明确改动，不能整文件覆盖用户编辑。
- 不执行 commit、push、归档或 journal 自动提交。若需 journal，使用 `add_session.py --no-commit`；Trellis 常规 Phase 3 commit/归档建议由用户在本任务明确覆盖。
