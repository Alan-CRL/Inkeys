# 阶段四正常颜色现场诊断审查

2026-10-13。Active task: `10-13-draw3-tail-release-validation`，基准阶段三提交 `71858b0b`。用户已确认未绘制长斜线在抬笔时突然出现；当前人工验收失败。本代理为已分派的 trellis-check，直接审查与修复，没有再派代理、没有运行 GUI、没有修改求解算法、没有提交或归档任务。

## Findings (fixed)

### 断触接续的输入归属歧义

- File: `Draw3.InkPrediction.cppm`、`Draw3.DrawingController.cpp`、`Draw3.StrokeGeometry.cpp`。
- Issue: 原诊断保留整笔初始 Down 的 sequence/QPC/position，却在接续时覆盖唯一的 handle/generation/tcid/cid/source。日志容易让初始 Down 被误解为来自最新 contact；旧 contact 的 terminal/model-Up 数值也会留到下一次抬笔。
- Fix: 固定诊断结构额外保留初始 contact 身份与最新 contact Down。接续累计 counters 和初始 Down 不丢失，当前 contact 的序列重新计数，terminal/model-Up 字段清零；下一个物理终态再写入。日志输出 `input_initial_identity`、`input_contact_down` 与明确 scope。`CheckTailInputDiagnosticContract` 追加真实 coordinator 接续、旧终态清理、初始/当前身份和 Reset 断言；结构仍受 trivially-copyable / sizeof<=512 静态检查。

### PID 复用混入旧现场日志

- File: `Draw3.StrokeGeometry.cpp::WriteTailDebugRecord`。
- Issue: 原 `OPEN_ALWAYS` 固定 PID 文件可能追加到上个使用相同 PID 的进程遗留文件，旧 header/EXE 构建信息可误导调查，并使每个文件的记录数超过当前进程上限。
- Fix: 第一次写入以 `CREATE_NEW` 建立主文件；只有名称已存在时，使用 `GetSystemTimeAsFileTime` 时间戳加最多8个候选后缀 `CREATE_NEW`。当前进程锁存成功路径，后续仅 `OPEN_EXISTING`。旧文件不覆盖、不删除、不追加。沿用 SRW lock、128次记录及2MiB字节硬预算，创建/短写/失败仍不能改变返回的 Stored。没有引入 Win7 基线之外的系统 API。

## Findings (not fixed)

- **真实异常根因仍未知（缺现场数据）**：既有启动日志没有该笔的原始输入或完成点列。没有证据证明驱动 Up、消费者合并 Move、完成拟合或 GPU 中某一种就是根因。此次诊断基础可以交付复现，不能声称修复长斜线或阶段四通过；未更改阈值、真实 Up、Modeler 或 GPU 上传。
- **诊断裁剪边界（有意保留的资源合同）**：model/completed/stored 每次最多512点、raw64、boundary128；完整笔划只遍历生成首尾、bbox、弧长、最大边和位置 hash。超出已输出范围时，不能凭尾窗不存在长线推断整条 Stored 不含长线。若后续异常位于被裁剪前缀且摘要不足，需要有针对性的异常区间或 GPU 提交证据，不能无界复制整笔原始输入。
- **阶段粒度（无需改变算法的已核对边界）**：`model` 是拟合前 realPoints，`completed` 是拟合后加 taper/公切线处理的完成副本，`stored` 是叠加 canvas offset 后的正式点列，没有另行序列化 post-fit/pre-taper。已核对 taper 仅改变半径或在线段内插值点、公切线安全仅改半径，因此两者不会凭空增加位置连接；日志足以区分原模型已有跨度、完成态位置重构和存储坐标变换，但不能把 completed 称为纯求解器输出。
- **真实渲染与人工验收（范围/设备限制）**：无窗口 probe 验证 CPU 完成几何和正常样式；不验证 D3D 实际上传、GPU 像素、实体笔/触控或 Windows7。阶段四 Release/跨架构最终验收在现场问题修复后继续，当前保持 in_progress。

## 完整范围合同核对

- 默认 `kDebugCompletedTailVisibility=false`；环境变量精确为 `INKEYS_DRAW3_TAIL_TRACE=1` 才启用记录，首次查询锁存。开关不进入求解参数，也不控制实际尾段发布，RGB/alpha及瞬态 debug marker 保持正常默认值。
- Down 锁存 enabled，Move/Up 仅在该 enabled 时维护常数大小数值状态；Note 在 lastInput/lastConsumed 覆盖前保存已消费位置，重复 deferred Up 不增加输入计数。接续的不同 contact 序列不相减。
- 即时与延后实际 kUp 两条路径都记录实际模型 time/status、产生/接纳/几何新增数量、fallback/pin。临时计数不回写真实输入、不延长 ContactRecord 寿命、不进入 UInk。
- `WriteTailDebugRecord` 只读取 const ActiveStroke/完成 span/Stored span，日志在正式几何、taper、offset 构造及 IsValid 判定后执行。创建、写入、短写或容量不足不改变几何；缺 end 不可解析为完整记录。默认关闭时没有轨迹 I/O。
- 正常颜色 probe 用位置相对未拟合但同 taper 的基线比较确认实际 Apply，计时区外检查每轮实际位置/r/time/样式一致，计时区关闭诊断 I/O。没有用关闭的反色 marker 当作 Apply 证据。
- 新 lifecycle 测试消费实际 Finalize 的变宽/taper 修正笔划，经 history Undo/Redo 与 composition tile 真值、UInk capture/export/file/read/import 精确往返；接续/等待预览不修改活动几何或有限 raw/model 历史，Reset 新联系不携带旧尾窗。调用的是既有模块/保存实现，不复制求解器。
- 本次没有第三方源、依赖、ABI、工程 flags、shader 或文档格式变更；4个未跟踪 cso 是生成文件，应排除提交。

## Verification

- Lint / 静态：`git -c diff.ignoreSubmodules=all diff --check` 退出0；没有独立 C++ lint 命令。逐字节核对5个变更源码与HEAD的 BOM 一致，全部 CRLF，无裸LF或双CR。搜索并核对所有新诊断引用和提前返回路径。
- TypeCheck / 构建：主会话负责完整原生 ARM64 `InkeysRepo.sln Debug|ARM64`，本代理不重复启动并发构建。最终碰撞处理修复后的重新构建与 --draw3-control-fence-test 结果由主会话补充；本报告暂不把修复前结果写成修复后通过。
- Tests / 已有证据：读取主会话 tests-ARM64-Debug/results.json，adaptive、128412基线、integration、uink-file 最终均退出0；baseline SHA256仍为 `0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646`。integration 显示 finalized persistence 与 local tail 0失败。未由本代理重复执行。
- 诊断证据解析：本代理对碰撞加固前、初始身份修复后的现有 trace 副本进行独立解析：64个完整 end 记录、1,845,792字节，model/completed/stored最大512，raw最大21，boundary最大21；共同输出范围内 completed 与 offset Stored 的位置/宽度对应一致。证据 SHA256 `dd659caa372e66c1efcf90df2f9e03ee443201d6d90e3f5fbcd2d8f76d6218b6`。该解析不是最终加固后新程序或实体异常回放，完整机器记录在忽略的 `Build/modeler-baseline/phase4-evidence/phantom-trace/reviewer-static.json`。

## Spec 同步建议

主会话完成最终构建和诊断证据后，在 native terminal-tail-reconstruction 合同追加阶段四默认正常颜色/显式 env trace、初始与当前联系归属、create-new PID碰撞后缀、model/pre-fit 与 completed/post-taper、512裁剪解释。旧阶段三临时反色及人工状态属于历史记录，不能由本轮诊断成功改写为当前人工通过。

## 最终自动验证补充（主会话执行，本代理核对证据）

- 最终 create-new 碰撞加固源码的完整 `InkeysRepo.sln Debug|ARM64` 原生 MSBuild 构建退出0，14.844秒；产品 SHA256 `494f645b0289a89f41607ab8b7cffe4b3a41ef204913d1226b04d538dabd2221`。上述 TypeCheck 待执行项已补足。
- 同一最终 EXE 的默认关闭和显式启用两次 `--draw3-pen-taper-test` 均退出0，normal inverse=0，334115项断言、0失败；默认无新TEMP轨迹。主会话比较标准化CLI结果一致，回归案例与真实修正断言一致；不能外推成任意硬件输入的数学证明。
- 最终 `--draw3-control-fence-test` 无窗口退出0，补足初始/当前contact归属、接续终态清理与 Reset 新断言。
- 本代理再次解析最终有界诊断：64完整记录、1,845,792字节、每笔model/completed/stored<=512、raw<=64、boundary<=128，初始身份/最新Down行齐全，共同输出范围中的completed与offset Stored位置/宽度对应一致。最终trace SHA256 `48aaf4e970ddaa51179ba32eede782387775e95fd03ef2ee2fea8640745be7c3`；机器报告已更新为最终EXE证据。
- 当前CLI成功使用主PID文件名，没有用伪造旧PID文件运行时间戳后缀分支；create-new fallback 的旧证据保留和有界循环已静态核对，该碰撞分支不得写成已实际触发。
- 阶段四完整Headless本轮已由主会话执行通过，当前加固未改Headless业务；最终Release/其他架构、真实异常现场、GPU和正常颜色实体验收仍待后续，不以本次自动结果结束任务。
