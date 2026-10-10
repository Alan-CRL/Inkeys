# F041 Storage R4：旧 v1 大写 workspace GUID 兼容续存

日期：2026-09-28。来源：R2 测试夹具暴露的剩余条件兼容缺口。改动范围限于 `Draw3.PresentationAutoSave.cpp` 的 Load/Save 身份检查与新 v2 index 字段，以及 `presentation_autosave_tests.cpp` 的真实 Service 回归。未修改 Controller、schema/路径白名单、三轨 selector、旧 fallback ordinal、自动 GC 或其它任务文件。

## 事实与最小修复

- 旧 schema v1 index 接受合法大写 `workspaceGuid`。测试从本项目生产 Service 先创建含完整 header/revision 的 UInk，再将同一文件改为旧固定物理路径，只将 v1 index 的 workspace GUID 文本变为大写；file GUID、相对路径、session、key、source revision 和物理文件保持匹配。这使测试只针对工作区 GUID 的大小写差异。
- 旧 `LoadPresentation` 在严格 UInk 导入成功后，将规范小写 `FormatUInkGuid(imported.snapshot->workspaceGuid)` 与索引原文比较，因大小写返回 `Invalid`；续存 Save 也用原文与请求规范文本比较，返回 `SourceChanged`。
- 修补：Load 和 Save 均先解析 index workspace GUID，再按 GUID 值比较，不降低 header/source revision/importer 校验；新物理版本写成后才把待发布 v2 index 的 `workspaceGuid` 设为规范小写。旧 v1 相对路径照原样读取，旧 UInk 不原位覆写。若 index 发布失败，主/备索引事务仍走既有 F044 逻辑。

## 红绿证据

| 阶段 | 构建/进程结果 | 直接断言与原始日志 |
|---|---|
| 红 | standalone `inkStrokeModelerTest.sln Debug|ARM64` Build exit 0；沙箱外完整 `inkStrokeModelerTestTests.exe` exit 1 | 原 R2 大写 file GUID 分支仍通过；新增大写 workspace GUID 分支唯一失败为旧 Load 期望 Loaded，实际 `Invalid`。`TestResults/release-hardening/f041-uppercase-workspace-red-build-debug-arm64.log`、`f041-uppercase-workspace-red-debug-arm64.{stdout,stderr}.log`。 |
| 绿 | 相同构建 exit 0；完整 Tests exit 0 | 红 1 项转绿；旧 Load 到一笔，修改后 Save Committed/Base/gen21，index v2 的 file/workspace GUID 与新版本路径规范小写，旧 v1 UInk 字节不变，backup 字节等于旧 v1 index，fresh Service 严格 Load 新内容。`TestResults/release-hardening/f041-uppercase-workspace-green-build-debug-arm64.log`、`f041-uppercase-workspace-green-debug-arm64.{stdout,stderr}.log`。 |

两个分支使用各自独立的临时根：`uppercaseWorkspace=false` 是 R2 的大写 file GUID 续存回归，`true` 是本轮 R4。旧 Load 失败时该测试停止续存步骤，因此红证不会混入下一步 Save 的连锁失败。

## 限制与后续审查点

- 本轮验证是本机 Debug ARM64 的真实 Storage 路径，未运行最新主 `InkeysRepo.sln` Debug/Release、Office GUI、Win7 SP1 + KB2670838 或断电测试。构建和运行证据分开，不能据此宣称发布就绪。
- R4 会为旧索引创建新的物理 UInk，保留旧文件与索引备份；自动 GC 仍禁用，长期磁盘增长沿用 F044 已知 P2 容量风险，没有测得性能收益。
- 静态待核：若大写 workspace GUID 的 v1 主索引在新 UInk durable 写成后 index 提交失败，pending entry 已规范化，但 `LoadPresentation` 的 pending/主索引 workspace 文本比较仍区分大小写。它可能使同 Service 的 Load 回退旧已提交版本；本轮未加该故障注入，不能标记为确认失败或已通过。Save 重试另有 pending 路径，需独立验证后再判断影响。
- 两个修改文件保持 UTF-8 BOM/CRLF；`git diff --check` 无报错；未提交、未启动 GUI。
