# 本机内存分析工具盘点

调查日期：2026-11-04（Asia/Shanghai）。分支：`diagnostic/memery`。本轮只读取安装信息、PE 文件和源码，并执行 CLI 帮助/查询；未启动 Inkeys、VS/WPA GUI，未开始追踪、修改 IFEO 或安装工具。

## 结论与能力边界

已有工具足够进入第一轮基线采样和原生堆/虚拟分配调查，不需要先安装软件。已验证的是文件存在、ARM64 二进制/代理、CLI 启动和配置查询；尚未验证真实目标采集、调用栈符号解析、快照对比或 ETL 导出。

| 工具 | 本机证据 | 已验证 / 仍待验证 |
| --- | --- | --- |
| Visual Studio Community 2026 | `vswhere`：18.11.12306.390，安装目录 `C:\Program Files\Microsoft Visual Studio\18\Community` | 已安装完整实例；未打开 IDE，也未测试 Memory Usage UI |
| ARM64 MSBuild | 从上述安装动态定位 `MSBuild\Current\Bin\arm64\MSBuild.exe`，18.11.0.47511，PE Machine `0xAA64` | 文件与架构已核实；本轮无产品代码修改，不执行构建 |
| VS Diagnostics | `Team Tools\DiagnosticsHub\Collector\VSDiagnostics.exe`，18.11.37803.1；`Agents\NativeMemoryCollectionAgent.dll` 和 `MemoryProfilerCollectionAgent.dll` 均为 ARM64 | 沙箱外 `start /help` 退出 0；实际原生采集、快照操作和符号解析仍待目标实测 |
| 系统 WPR | `C:\Windows\System32\wpr.exe`，10.0.26100.9711，ARM64 | `-profiles` / `-status` / `-profiledetails` 成功；检查时未在录制 |
| WPT WPR / xperf | `C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\`，10.0.26100.8249，均为 ARM64 | 文件和 PE 架构确认；优先固定同一个 WPR 版本，避免 PATH 混用 |
| WPA | 同上目录 `wpa.exe`，11.7.395.48728，ARM64 | 仅检查文件、版本和 PE；未启动 GUI |
| WPAExporter | 同上目录 `wpaexporter.exe`，11.7.395.48728，ARM64 Console | 沙箱外 `-h` 退出 0，支持 profile、CSV/XML 和区间导出；真实 ETL 导出未执行 |
| VMMap / ProcDump / WinDbg / UMDH / GFlags | PATH、常用 `C:\Tools` / `D:\Tools` / Sysinternals 目录、WinGet 包目录和 Windows Kits Debuggers 未发现可用入口 | 是有限范围搜索结果，不宣称全盘未安装；本轮不需要补齐 |

VS 的 Memory Usage 工具可查看 C++ 原生堆及混合托管/原生快照；这只是官方功能说明，不是本机目标已验证的结论。[Microsoft Memory Usage](https://learn.microsoft.com/en-us/visualstudio/profiling/memory-usage?view=visualstudio)。VS CLI 的一般采集接口可启动/附加目标并输出 `.diagsession`；不能从帮助命令成功推导出原生快照完全可用。[Microsoft VS CLI](https://learn.microsoft.com/en-us/visualstudio/profiling/profile-apps-from-command-line?view=visualstudio)。

VMMap 可补充进程提交内存类别和工作集分布，但现有 WPR/WPA 路线并不以安装它为前提。[Microsoft VMMap](https://learn.microsoft.com/en-us/sysinternals/downloads/vmmap)。

## WPR 查询结果与采集开销

本机配置列表包括 `Heap`、`HeapSnapshot`、`VirtualAllocation`、`ResidentSet`、`ReferenceSet`、`GPU`、`Handle`、`DotNET`。

- `Heap.Verbose.Memory` 配置描述为 System Collector 798 × 1024 KiB、Internal Heap Collector 64 × 1024 KiB、Event Collector 20 × 1024 KiB。
- `Heap.Verbose.File` 查询描述为上述三个 collector 分别 20、64、20 个 1024 KiB buffer。
- 这些是录制配置，不是本轮实际分配量；本轮没有开始录制。后续短时追踪优先考虑 file mode，单独记录录制器开销，并先采不带 profiler 的基线。
- `wpr -help heap` 确认有按进程名配置 heap tracing、按 PID/name 启用 heap snapshot、触发单次快照的命令。

完整分配调用栈需要在目标分配前启用追踪；按进程名的 HeapSnapshot 配置影响之后启动的实例，按 PID 配置只覆盖配置后的分配。启用按名称快照会使用 IFEO，结束后需恢复原配置。[Microsoft HeapSnapshot](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/record-heap-snapshot)。常规原生 Heap 录制同样需要目标进程配置，不能只执行 `-start Heap` 就假定拿到了完整目标历史。[Microsoft Heap Analysis](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/recording-for-heap-analysis)。

WPAExporter 能按现有 `.wpaProfile` 将 ETL 表格输出到 CSV；本机具体堆表 profile、真实导出和符号栈仍需测试。[Microsoft Exporter](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/exporter)。

## Codex 环境限制与已完成排查

1. 默认 `python` 命中 WindowsApps 别名，在沙箱内无法启动。复用了本机 `C:\Program Files\KiCad\10.0\bin\python.exe` 运行 Trellis；未安装 Python 或修改 PATH。
2. 沙箱内 WPAExporter 启动报 registry `SecurityException`；沙箱外只运行 `-h` 成功，退出 0。仍打印缺少 `wpaexporter.deps.json` 的提示，但没有阻止帮助执行，不据此修复/升级工具。
3. 沙箱内 VS Diagnostics 的一般 help 初始化 COM 时拒绝访问；沙箱外 `start /help` 成功，退出 0。没有 session ID、attach 或 launch 参数，未开始采集。
4. `Get-CimInstance Win32_ComputerSystem` 在沙箱内不可用；ARM64 结论来自 RuntimeInformation 和 PE Machine，未为读取硬件信息扩大权限。
5. 本轮实际 ETW 开启与目标附加未执行；后续是否还需 Windows 提权须以具体采集结果为准，沙箱外执行并不自动等同管理员权限。

## 已有程序与符号

`Build\ARM64\Debug\Inkeys.exe` 是 ARM64 GUI 程序，文件 103086080 B（98.31 MiB）；`Inkeys.pdb` 为 163803136 B（156.21 MiB）。`InkeysHeadlessTests.exe` 也已存在。本轮没有构建、运行或验证 EXE/PDB 的 GUID/Age 匹配，也没有证明它们对应当前 HEAD。

现有 Inkeys PE 的主要节：`.text` 30.57 MiB、`.rdata` 5.62 MiB、`.rsrc` 虚拟大小约 60.37 MiB、`.data` 虚拟大小约 0.73 MiB。它们是文件/虚拟映像结构；资源节大小和 PDB 文件体积都不能直接当作进程 Private Bytes 或物理常驻占用。

检查时没有发现运行中的 `Inkeys*` 进程，因此当前没有真实运行时内存总量或场景增量。

## 后续构建约束

若采样前需要刷新二进制，按用户规则动态定位 ARM64 MSBuild，构建 `InkeysRepo.sln` 的 `Debug|ARM64`，允许至少 5 分钟。MSBuild 所在的同一次 PowerShell invocation 先执行 `Remove-Item Env:PATH -ErrorAction SilentlyContinue` 并设置 `MSBUILDDISABLENODEREUSE=1`。不改工程、SDK、工具链或依赖。

源码编译验证优先遵循上述 Debug 规则；要给出代表发布版本的优化收益，还需另列同一源码/配置的发布构建测量，不能混用 Debug 与 Release 数值。[Microsoft Release Memory Profiling](https://learn.microsoft.com/en-us/visualstudio/profiling/memory-usage-without-debugging2?view=visualstudio)。
