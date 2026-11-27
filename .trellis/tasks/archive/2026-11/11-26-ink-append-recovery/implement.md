# C1.6b 实施计划

分工（用户 2026-11-26 指定）：**Claude 写代码，Codex 审查**（Sol High 只读）。收尾顺序：Claude 独立审查 → Codex 审查 → Claude 修复。子代理不并行。

前置：C1.6a 完成（`CrashRecoverInk`、`TeachingSafetyMode` 三档、`-CrashTry`）；C1.6c 完成后做实机联合验证。

构建：`InkeysRepo.sln` `Debug|ARM64`；L2 改动另跑 `inkStrokeModelerTest.sln`（复用 `%TEMP%\inkeys-codex\c14\build-uk.sh`）。超时 ≥ 5 分钟。源文件 UTF-8 BOM + CRLF。

## 步骤

- [ ] **P0 预研（只读 + 临时测量，测完还原）**
  - 测 953 笔真实重页 `CaptureCanvasTail` 的 Release / Debug 耗时，确定 design §4.2 可让出截取的批大小。
  - 核对跨文件原样复制页段的规范与代码合规性（design §8.2）。
  - 摸清“装入白板会话”（重建 `InkCanvasCollection`、初始化 writer 目录与 `ConfirmedPage`、`RequestWhiteboardActive` 进入后显式加载目标页）与 PPT `Adopt` 先于 `LoadLane` 的改动面，写成附录 A。
- [x] **S1 L2：`AppendContent`、`RelocatePage`、前缀读取**（design §4.3、§5.4；2026-11-26 完成，`--uink-store-only` 通过。`ScanUInkPagedFile` 改为 `ReadUInkStoreFilePrefix`，见 design §13）
  - 模型器测试工程单元测试覆盖 design §11 的 L2 项与对象边界截断。
- [x] **S2 诊断异步化**（判定现状已满足，不改；见 design §13）（design §10 最后一条）
  - 定长数值记录 + 无锁环形缓冲 + 日志 worker 格式化；迁移 `lift-path`、`page-save`、`page-load`。
- [x] **S3 `RecoverySession`**（`Draw3.RecoverySession.h/.cpp`；Host 持有、适配钩子、正常停止写 clean）：存活锁、认领锁、会话记录、写前登记与 FileId**（design §5、§6）
  - Host 持有；开关切换；正常结束写 `clean`；写前登记接口供场景 worker 同步调用。
- [x] **S4 白板与 PPT 追加**（`PagedInkRequestKind::Append`；绘制线程 `submitStrokeAppend`；离场/退出 `RequestOrderedCompact`；空闲补整页）（design §4.1、§4.2、§4.4）
  - 抬笔、图形安装处交接 `Append`（带修订身份）；撤回等交接 `Full`（可让出截取）；worker 按“最后一个 Canvas”与基准修订分流；有界队列与补写；离开与退出时 `CompactOrdered`；翻页写会话事件。
- [x] **S5 桌面活动区间文件**（Desktop 类 `PagedInkStore` + `RotateFile`，`desktop-live\`；保留 C1.4 快照与冷读，见 design §13.1）（design §3.1、§4.5）
  - 删除“启用桌面画布自动保存”设置项、i18n 与 `Drawing.AutoSave.Enable`，“保存”子页留空；`AutoSaveEnabled()` 门控改为恒真；同步 `Bar.Presentation.Test.cpp:629`。
  - Clear 后首笔 `Create`、之后追加；已封存区间与按范围冷读；Undo/Redo Clear、停放；开关关闭时保持 C1.4 快照。
- [x] **S6 启动扫描、结算与恢复**（Host `RunRecoveryScan` 独立线程；简化见 design §13.2）（design §7、§8）
  - 先登记待恢复 PPT；选会话与认领；§5.4 判定；弹窗线程与 i18n；按场景结算；PPT 条目跨启动保留与过期；桌面、白板装入；PPT `Adopt`。
- [x] **S7 验证**（构建、L2/L4 单元测试、C1.4 隐藏自检、supervisor 32/32 通过；2026-11-27 第二轮修复后与第 4、5 轮修复后两次实机通过（用户确认）；落笔延迟对比、Win7/兆芯转入中期重构最终集成验收）
  - 构建；模型器测试与 `InkeysHeadlessTests` 全部通过；C1.4 的白板、PPT、桌面隐藏自检通过（开关关闭时字节级行为不变）。
  - 落笔交接耗时分布与 C1.4 基线对比（Release 重页）；慢盘、日志队列满。
  - 实机：与 C1.6c 联合，三场景 × 三档 × 三种触发；结束进程后手动启动。
- [x] **S8 审查**：Claude 逐行审 diff → Codex 只读审查（7 个阻断项，处理见 design §14）→ 修复 → Debug/Release 构建、`--uink-store-only` / `--paged-ink-store-only`（新增 `AppendRecoveryRegistrationFailures`）与 9 项 C1.4 隐藏自检通过（2026-11-27）。

## 回滚点

S1–S3 不改变产品行为（新接口与新服务未接入），可单独合入。S4、S5、S6 分别接入一个场景或恢复流程，都受开关控制；出问题时可以关闭开关回到 C1.4 行为，或单独撤回某一步。

## 附录 A：白板冷页批量建立（P0 填写）
