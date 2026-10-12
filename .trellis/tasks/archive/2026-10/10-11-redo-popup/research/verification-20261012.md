# 2026-10-12 修复验证

## 结果

- `InkeysRepo.sln Debug|ARM64` 完整构建：退出 0，0 Error，374 Warning，耗时 4 分 18 秒。主程序与测试程序均重新生成；本轮 7 个修改的源码/测试文件没有编译警告。现有警告来自未改动的类型转换、第三方代码和其他测试，不做无关清理。
- 新生成 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：退出 0，stderr 为空，末尾 `PASS animation correctness`；`[RecallAttribute] failures=0`、`[EraserAttribute] failures=0`、`[SpeedEraser] failures=0`。
- 独立 trellis-check：两项发现（真实面板固定 Undo 锚点关闭、旧灰色点击注释）已修复并复核，没有剩余源码发现；详见 `check-20261012.md`。
- `git diff --ignore-submodules=all --check` 通过；7 个源码/测试文件原 BOM 标记保留、工作区 CRLF、零 bare LF；无工程配置、依赖、资源或 Draw3 历史改动。
- 未执行 GUI / Computer Use / 实际 HWND 连点视觉验收。任务保留 in_progress，原任务的手工视觉验收未冒称完成；没有 commit/push。

## 构建与执行环境

通过当前 `vswhere` 定位 VS ARM64 原生 MSBuild，使用现有 solution：

~~~text
MSBuild.exe InkeysRepo.sln /m:1 /t:Build /p:Configuration=Debug /p:Platform=ARM64
InkeysHeadlessTests.exe --no-window
~~~

构建前在同一个 PowerShell invocation 删除 `Env:PATH`，恢复单一 canonical `Path`（实际 v143 CL 目录在前），设置 `MSBUILDDISABLENODEREUSE=1`；只有当前子进程环境变化，没有修改全局配置。构建允许 10 分钟，测试允许 5 分钟；后台启动均为 Hidden，不显示产品窗口。

普通 sandbox 首次完整构建退出 1，首错 C1902 在未改动的标准库 header unit/Surface 等处，共 24 处。实际工具链为 HostArm64/arm64 的 v143 CL 19.44.35229，mspdb140/core/srv 均为 14.44.35229，安装目录组件版本匹配。无 header、全新 PDB 的最小 `/Zi /FS` 探针在普通 sandbox 同样退出 2/C1902，规范化 Path 后仍失败；完全相同探针经 require_escalated 自动审批在 sandbox 外退出 0。完整构建在同样 sandbox 外环境通过，证据指向 Codex sandbox 的 PDB 服务/IPC 执行边界，不是本轮源码错误或旧项目 PDB 缓存。

[C1902 的微软说明](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-1/fatal-error-c1902?view=msvc-170)描述的是 PDB 数据库管理组件问题；此处 sandbox 归因来自上述本机对照探针，不能仅由错误文本推出。

普通 sandbox 的 headless 退出 1：本轮 Recall 回归已经为 0，但有 11 项未改动的临时配置写入/原子替换失败。相同 EXE、相同 `--no-window` 在 sandbox 外退出 0，所有这些失败消失，stderr 空；没有改生产代码或测试来绕过环境。

## 证据文件

- `Build/recall-fix-debug-arm64-20261012.log`：初次 sandbox C1902。
- `Build/recall-fix-debug-arm64-20261012-unrestricted.log`：完整构建通过。
- `Build/recall-fix-headless-20261012.log` / `.stderr.log`：普通 sandbox 测试与文件写入失败。
- `Build/recall-fix-headless-20261012-unrestricted.log` / `.stderr.log`：整体 headless 通过。
- `InkeysHeadlessTests/recall_attribute_tests.cpp`：生产状态策略、真实颜色动画和 Advance 源级接线断言；源级断言运行时需要能访问仓库源码。
