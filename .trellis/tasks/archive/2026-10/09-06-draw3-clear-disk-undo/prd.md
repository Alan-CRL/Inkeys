# Draw3 Clear 磁盘回撤与内存释放

## Goal

安全完成 `origin/chore/draw3` 到 `draw` 的语义合并，并把 Clear 重构为区间边界。Desktop 在现有自动保存开关开启时从最近一次 Clear 前 UInk 恢复，关闭或失败时保留最近一个内存旧画布；PPT 始终把每个 Slide 的所有 Clear 区间保存在同一个 canonical `.uink` 中，连续 Undo 可以逐级回到该页最初区间，同时不把全部历史常驻 controller 内存。Whiteboard 本期只形成后续规划。

## Confirmed Facts

- 合并已开始但尚未提交，冲突位于 `.trellis/workspace/AlanCRL/index.md` 和 `Draw3.DrawingController.cpp`。
- `chore/draw3` 的 `b44dec66` 使用内存 Clear history barrier，因此不会释放旧 Stroke；其清空按钮单击/双击与选择模式修复可独立保留。
- 当前 `draw` 已有 Desktop 不可变 UInk 历史、PPT canonical UInk、严格反向导入、三类独立 document slot、PPT SlideID 拓扑重映射及 scene-stamped completion。
- 当前代码让 Desktop 与 PPT 共用 `saveSetting.enable`；用户现已明确该开关只控制 Desktop。PPT 当前阶段必须始终保存/恢复，未来再增加独立 PPT 开关。
- 普通 Stroke Redo 已有实现和 CPU 测试，但 Redo 按钮入口及跨磁盘区间 Redo 不在本任务修复范围。
- UInk v10 仍为未冻结 Beta；此前 Type ID `0-5` 只定义 Header、HeaderExtension、Canvas、Ink、Media、Shape，Canvas 内容没有 Clear 操作。多 layer 会同时合成，`extra` 不能改变通用 reader 的基础呈现，未知 Type ID 会被旧 reader 跳过，因此本任务把 Type ID `6` 正式注册为当前 version 10 基线的一部分。

## Requirements

### R1. 语义合并

- 保留当前 `draw` 的 Desktop/PPT 自动保存、文档 slot、稳定 SlideID、retained canvas、三态 ready、Laser 与 presenter 恢复，不得整文件采用 `ours` 或 `theirs`。
- 保留并适配来源分支 Clear 点击决策：有内容时清空，空内容时进入选择；只有首击 Clear 被 FIFO 接受后，双击第二击才进入选择；Clear 不进入 toggle 点击合并。
- 来源分支的内存 Clear barrier、透明 history operator 及 standalone 镜像由区间恢复模型替代。
- 来源任务归档与 journal 历史保留；活动 spec 只描述最终合同。

### R2. 通用 Clear 事务

- 非空当前页 Clear 时生成唯一 clear/interval identity，保留 pageGuid、viewport 与场景身份，创建空的当前区间；空 Clear 为 no-op。
- Clear 立即清理当前页 GPU history/cache、composition 维护、恢复计划、trusted L2、Laser/粒子/光标/瞬态层，并发布完整透明帧与无内容状态。
- I/O 不在绘制线程执行；保存完成前由 worker snapshot 或单个 memory fallback 保证不会因失败丢失刚清空内容。

### R3. Desktop 最近边界

- `saveSetting.enable` 只控制 Desktop。开启时复用现有 Clear 不可变历史 UInk，并补齐 durable completion/path 与严格读取通道；成功后释放旧 page/runtime，失败则保留 memory fallback。
- 关闭时不创建恢复文件，只保留最近一次有效 Clear 前的完整 page/runtime；下一次有效 Clear 丢弃更老恢复点并轮换当前区间。
- Desktop 连续 Undo 先撤当前区间 Stroke，再恢复最近一次 Clear 前状态；恢复内容成为 undoFloor，不能继续加载更旧 Desktop 文件。

### R4. PPT canonical 多区间历史

- PPT 保存不受 Desktop 开关控制。每个 PresentationKey 始终维护 canonical `.uink`，Clear、普通 mutation、页面离开和退出按现有 revision/worker 生命周期收敛。
- 同一 Slide/Page 的 `A → Clear1 → B → Clear2 → C` 必须完整保存在同一 `.uink`；当前显示 C，连续跨区间 Undo 按 `C → B → A` 逐级恢复。
- controller 只常驻当前区间与必要的短期 fallback；历史 Stroke/Clear 保存在 UInk。跨边界 Undo 按 page/SlideID 和区间位置读取目标区间，完成后释放解析快照。
- 普通同区间 latest-wins 保存可以合并；每个 Clear boundary 都是不可丢失的有序事务，不能被后续 snapshot 替换。
- 重新进入 PPT 放映后仍能从 canonical UInk 恢复当前可见区间和全部 Clear 导航边界。

### R5. UInk version 10 Clear 表达

- Header version 保持 `10`，注册 Type ID `6` 的 Clear Map；version 10 仍为未冻结 Beta，当前规范基线取代较早同版本草案。
- Clear 与 Ink/Shape/Media 共享 `contentId/undoId`，独占自己的 undo group；渲染时清除同一 Canvas 中此前全部合成结果，后续内容从透明状态继续。
- 仍有效但被后续 Clear 隐藏的早期区间必须在完整保存中保留；真正撤回且不保留 Redo 的内容按现有完整保存规则移除。
- 当前兼容 reader 必须理解 Type ID 6；只会跳过未知 Clear、进而错误叠加全部区间的早期 version 10 reader 不属于当前 Beta 基线。
- Desktop 现有 Stroke-only version 10 历史保持兼容；PPT 在相同 version 10 下使用 Clear operation history。

### R6. Undo 边界与导入

- 当前 interval history 在 undoFloor 之上有可撤项时先走普通 Stroke Undo；到区间根后，Desktop 消费唯一恢复点，PPT 读取前一个 Clear 区间。
- 导入的区间 Stroke 用于 composition/replay，但密封为 undoFloor；在其上新增内容仍可普通 Undo。
- Desktop 恢复后再次 Undo 为 no-op。PPT 在还有更早 Clear 时可继续逐级读取，直到该 Slide 的最初区间。
- 重复 Undo 不得并发重复加载；损坏/不支持/身份失配时保持当前权威画面并报告诊断。

### R7. PPT、页面与三态兼容

- 只替换目标 SlideID/page interval，不得回退同一 PPT 其他页在之后发生的修改。
- StableSlideId 重排、插入、删除/retained/reappear 时，区间历史随 pageGuid/SlideID 归属，不依赖旧 pageIndex；fallback 限相同 session/source/binding revision。
- A/B、active/parked slot、load/save completion 以 PresentationKey、page identity、interval revision 与 Host/target generation 隔离。
- Clear、恢复 pending、成功、失败和场景切换继续遵守 content revision、clean frame、identity-ready 和 Primary/Presentation/Hidden 双 surface 互斥顺序。

### R8. Redo 范围

- 本任务不修复 Redo 按钮入口，也不实现从旧 UInk interval 跨 Clear Redo 到更新区间。
- 普通 Stroke Redo 不得退化；UInk Clear 的 redo 文档语义和 interval parent/position 必须为未来前进导航保留，但产品入口延期。

### R9. Whiteboard 后续规划

- 不新增 Whiteboard 持久化代码；规划未来 workspace key、canonical/history 文件布局、Clear 操作、恢复 ready、故障与测试矩阵。
- 当前不可达 Whiteboard slot 不得删除、并入 Desktop 或错误接入 PPT 文件。

### R10. 质量与报告

- 提供 `chore/draw3` 修改的保留、改写、舍弃清单，以及 UInk 规范修改清单。
- 只做任务所需修改，保持原文件 UTF-8 BOM/CRLF 和中文关键注释；不启动可见窗口，不提交 commit。

## Acceptance Criteria

- [ ] AC1（R1）：语义合并无冲突标记，Clear 单击/双击/选择决策测试通过，当前 draw 新功能未回退。
- [ ] AC2（R2-R3）：Desktop 开关开启时磁盘恢复最近 Clear，关闭/失败时只保留一个内存恢复点；第二次 Clear 正确轮换。
- [ ] AC3（R2）：Clear 后 GPU/瞬态资源失效、全帧透明且内容状态为空；空 Clear 不产生区间。
- [ ] AC4（R4-R6）：PPT `A/Clear/B/Clear/C` 退出并重入后仍显示 C，连续 Undo 依次恢复 B、A，到最初后停止。
- [ ] AC5（R4-R5）：每个 Clear boundary 按序 durable，latest-wins 不丢边界；version 10 Type 6 codec/export/import/完整保存测试通过，既有 Type 0-5 fixtures 保持兼容。
- [ ] AC6（R6-R7）：pending、重复 Undo、损坏块、stale completion、A/B、parked、SlideID 重排/插入/删除/retained/reappear 不串场景或部分恢复。
- [ ] AC7（R7）：Primary/Presentation/Hidden 在 Clear 和恢复生命周期中无旧帧闪现，content/clean/ready revision 顺序正确。
- [ ] AC8（R8）：普通 Stroke Undo/Redo 测试继续通过；没有新增 Redo UI 或跨磁盘 Redo 产品行为。
- [ ] AC9（R9-R10）：Whiteboard 后续规划与 UInk 文档修改清单完整，当前产品无 Whiteboard I/O。
- [ ] AC10（R10）：ARM64 `Debug|ARM64` 完整 `InkeysRepo.sln` 构建、相关 CPU/headless 测试和 `git diff --check` 通过；只运行无窗口测试。

## Out of Scope

- Desktop 多文件历史连续 Undo/Redo、历史画布 UI及跨重启 Desktop runtime recovery cursor。
- Redo 按钮入口和跨 UInk Clear 的产品 Redo 导航。
- 本期实现 Whiteboard 自动保存或恢复。
- 修改 PPT/WPS COM ABI、扩大 Office/WPS 支持矩阵，或迁移/重写既有 Desktop version 10 历史文件。
- 与合并、Clear/UInk 生命周期、PPT 隔离或三态显示无关的重构和优化。

## Approval Gate

- 产品决策已收敛，UInk version 10 Type ID 6 Clear 文档已落地并通过网站构建；等待用户审阅本次更新后的最终规划摘要并再次批准，才能启动 Inkeys 产品实现。
