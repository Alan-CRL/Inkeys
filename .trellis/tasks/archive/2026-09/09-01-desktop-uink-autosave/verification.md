# 桌面批注 UInk 自动保存验证记录

## 需求证据

- 场景：Bridge、Host 和 DrawingController 使用 Desktop、Whiteboard、Presentation 三态；PPT 访问 epoch 防止 latest-state 合并遗漏 `PptTouched`。
- 触发：仅 Desktop Clear 前和正常退出屏障调用快照；Undo/Redo 没有自动保存调用。
- 设置：`saveSetting.enable` 恢复可见并同步到 Draw3；关闭时不捕获、不入队、不创建目录。`saveDays` 未连接到新 UInk。
- 文件：固定写入 `<exe>/Inkeys/AutoSave/desktop/YYYY-MM-DD/`，使用稳定请求/file/session GUID、create-new 和确定性碰撞后缀。
- 索引：UInk durable/self-validation 在前，版本化每日 JSON 在 named mutex 内重读、自校验和原子替换；索引失败保留孤儿。
- 生命周期：owned 串行 worker，不使用 detached thread；退出固定屏障不受业务队列满影响，无放弃超时 drain 后才销毁 controller。
- 保留：产品代码只删除本次未发布的已知 `.tmp`；无历史自动删除、容量淘汰、未知 UInk 清扫或 `saveDays` 裁剪。

## 自动化结果

- ARM64 原生 MSBuild 执行 `InkeysRepo.sln /t:Rebuild /p:Configuration=Debug /p:Platform=ARM64 /m`：通过，`317 Warning(s), 0 Error(s)`；PptCOM 与产品工程整体构建成功，警告为既存数值转换/第三方警告。
- ARM64 原生 MSBuild 执行 `inkStrokeModelerTest.sln /t:Rebuild /p:Configuration=Debug /p:Platform=ARM64 /m`：通过，`58 Warning(s), 0 Error(s)`；本次新增自动保存源码没有编码警告，C4819 仅来自既存第三方 `absl/base/nullability.h`。
- `ARM64\Debug\inkStrokeModelerTestTests.exe`：通过；覆盖同毫秒碰撞、幂等、两个独立 writer、跨日期、慢写 drain、UInk/索引故障、孤儿重试、主备恢复、损坏引用/时间字段和未知文件保留。
- `Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window`：通过；包含 Draw3 Bridge 三态、PPT visit epoch、保存开关发布、FIFO 退出屏障和满队列屏障测试。
- `git diff --check`：无空白错误；精确核对的 31 个本次 C++/project 文件均为纯 CRLF，并保持原有 BOM 状态（新增 `Draw3.AutoSave.cpp` 使用 UTF-8 BOM）。
- `trellis-check`：规范、跨层数据流、触发点、设置门控、owned worker、关闭顺序、重复实现和测试覆盖均已复核；补强每日索引的真实日期/时间、规范 GUID、无符号序号及重复身份/路径校验后，完整重复上述门禁并通过。
- Trellis Phase 3.3：已把三态门控、快照/worker 所有权、文件与索引事务、错误矩阵、测试点和退出屏障沉淀到 `native-desktop/draw3-integration.md`；人工产品验证仍待执行，任务继续保持 active。

## 受限验证

- 未启动产品 GUI，也未操控桌面，符合项目约束。
- 创建 HWND 的完整 headless 模式曾在当前受控桌面环境稳定出现两条与本任务无关的既存 Z-order 断言失败；本任务未修改 Window Service，最终门禁使用明确允许的 `--no-window` 集合。
- 不创建 commit；构建生成的 PptCOM DLL 变化已恢复，4 个 shader `.cso` 已删除且可由重建恢复，均不纳入任务修改。
- `trellis-finish-work` 要求先提交当前任务并会为归档/日志创建提交；与本轮不提交约束冲突，因此任务保持 active、未归档，也未写入 session journal。
