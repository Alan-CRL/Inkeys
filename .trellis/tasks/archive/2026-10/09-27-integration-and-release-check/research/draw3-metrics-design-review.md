# E04 Draw3 生产采样设计独立审查

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。审查对象为 `draw3-metrics-implementation-design.md`、`performance-sampling-postcommit-contract.md` 和当前生产 `RuntimeMetrics`、Controller、Host；只审设计，未实施/编译/采样。

## 分单元结论

- **U1 Session 有条件 GREEN**：可先由唯一 owner 修改 `Draw3.RuntimeMetrics.cppm/.cpp`，用真实 Session 测试有界采样、失败 Present、完成 contact pending、非法 QPC、分母与 JSON。现 `BeginFrame` 和失败 `CommitStagedLandings` 清候选，`RecordPresent` 无条件算一次，`RecordActiveFrame` 仅覆盖活跃子集；`ToolName` 把整数 1/2/3 错当 Highlighter/Eraser/Laser。`Draw3.WindowControl.cppm:29–41` 的实际枚举为 0 Pen、1 HardPen、2 Highlighter、3 Eraser、4 Laser、5–8 形状；U1 应导入真正 `window_control` 的符号 enum。当前 `window_control` 导入 contact/auto_save/pen_cursor/presentation_auto_save，未见其反向导入 `runtime_metrics`，模块图上无直接环；编译仍是验收门。U1 的绿色只证明 Session，不证明产品已开启采样。
- **U2 暂不随 U1 一并批准改 Controller**：先冻结 `RuntimeMetricsLandingProof` 的字段和 producer。`Controller::PresentFrame` 当前无条件记 attempt，必须在实际 `presentation_.Present` 返回后立即锁存 success/QPC；但同页成功并不足以证明之前已回收笔迹仍在成功画布。Stored 要关联 `CommitRuntimeStoredStrokeCpu` 的 RenderItem、history 可见性、raster/replay token 和 canvas/generation；Live/Laser 分别按当前实际 layer/sequence 证明。Up 终帧、失败 raster、Cancel、Clear、Undo、页换代、设备/输出代次改变各有反例。若保守 proof 因追加笔迹失效，记 `SupersededProof`/unpresented 与缺口，不把成功样本群体默默缩小。U2 修改 `Draw3.DrawingController.cppm/.cpp` 前 root 须解除 E03 对此文件的冻结并确认唯一写入者。
- **U3 独立交接**：Host 当前 `HostStartOptions` 未构造 Session；需要 owner 在每次 opt-in Start 建立新 run，绘制线程借用/结束、join 后导出，HiddenWindowTest/root CLI 使用生产 Host/RTS/renderer/presenter。应在 U2 代码审查后接线，不能用旧 standalone demo 或 CPU helper 的成功代替软件成功 Present。指标默认关闭，启用失败不得改变生产启动、输入、画质、保存与帧节拍。

## U1 开工前的小合同调整

1. **旧窄接口不得继续产出正式成功样本。** 设计允许旧 `StageLanding(record,generation,...)` 作为红夹具/过渡；在没有 canvas/item/raster/output proof 时只能标 legacy/unverified，不能在 U1 的新 JSON `success`/首发分位数中混入。Controller 尚未 U2 迁移期间，产品也未 Host opt-in；这不阻止 U1 开始。
2. **容量分母与最慢样本。** 联系人注册应在真实 Down 准入时一次计 seen；容量满仍计 dropped/pendingOverflow/invalid/unpresented，保留固定容量和热路径无扩容。`maximumSamples` 既有 32768、stage 上限 64，不能把容量满后的前缀 P95/P99 当整段。首次失败 Present 后的 Up pending 必须持纯值且经同 canvas 权威内容再确认；新 contact 或下一页成功不能借用其 Down。
3. **报告口径。** P99 对每群有效事件少于 1000 时为 null/探索性，必须输出 count、median/P95、覆盖/截断、失败/未呈现数和设备/工具/场景身份。总 wall 不叫线程 CPU，软件 PresentReturn 不叫光学可见。`MeetsStrictThresholds` 的 200 混合 demo 门只作 legacy，不可用 `strictPass` 给 Inkeys3 首发下结论。速度/固定橡皮须另列 scenario 或真实宽度模式，不能仅按 `DrawingTool::Eraser` 合并。
4. **输出安全和成本。** 当前 `WriteUtf8File` 用 `CREATE_ALWAYS`；新离线输出限隔离目录及不覆盖既有文件，且 JSON 不写指针、页面 GUID、文档路径、NaN/Inf。预分配预算按 `sizeof` 与乘法边界检查；构造失败将诊断标 unavailable，默认产品不创建会话。热路径不排序/格式化/写盘；报告在 owner 停止后封口。

## 验收

U1 用真正 Session 的失败→同身份成功、同指针新 generation、小容量、负/逆时 QPC、工具 0–8、无样本/小样本、JSON 完整性测试取得红→绿。测试只比较已知数值/状态，不复制一套百分位或 landing 算法。U2 用真正 Controller 共用候选验证 helper，覆盖 Down+Up 同帧完成后首 Present 失败、同画布恢复、Undo/Clear/页切换与输出代次反例；success token 必须源于实际生产栅格和成功返回。U3 同轨迹 metrics off/on 对比笔数、像素/完成语义与资源，最终 Release 同设备三轮按工具分群；采样期间停止构建及其它基准。Win7、真笔、HC/H2、光学时间仍为人工/独立门禁。

## Verification

已只读核对 `check.jsonl` 引用、PRD/design/implement、生产枚举/Session/Controller/Host 代码和性能合同。Lint/TypeCheck/Build/Tests：按分工未运行；以上均为设计评价，当前性能门没有因此通过。
