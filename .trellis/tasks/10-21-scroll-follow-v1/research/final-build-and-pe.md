# 最终构建、跨架构短回归与 PE 审查

2026-10-21，`feature/move` / `3ace726b` 之后的未提交工作树。保存独立 check 冻结后串行执行；没有进一步源码改动，没有应用普通启动、可见窗口或 Computer Use。

## 完整 solution 构建

两种架构都使用现有 `InkeysRepo.sln`、Debug 和 vswhere 定位的当前 ARM64 原生 MSBuild。每个构建在同一 PowerShell invocation 中规范为单一 Path，设置 `MSBUILDDISABLENODEREUSE=1`，使用 `/m:1 /nr:false /p:BuildInParallel=false /p:PreferredToolArchitecture=arm64`。normal host；不升级依赖/SDK，不改项目或链接参数。

| 构建 | 结果 | 原始日志，均在 Build/ScrollFollow |
| --- | --- | --- |
| 最终 Debug ARM64 | exit0，外部 8,143ms，MSBuild 7.89s，最终重链 0 warning/0 error | `motion-tracker-build-persistence-check-exact-relink*` |
| Debug x64 首次完整尝试 | exit1，109,839ms；主程序成功生成，测试程序链接报 LNK1285 PDB 损坏 | `solution-build-x64-final*` |
| 同配置 x64 重试 | exit0，6,791ms，MSBuild 6.43s，0 warning/0 error；完整 solution 成功 | `solution-build-x64-final-pdb-relink*` |

ARM64 在改动测试 TU 后遇到既有增量链接器 `IncrBuildImage C0000005/LNK1000`，检查者仅核实并删除生成的 `Build/ARM64/Debug/Inkeys.ilk` 后原配置重链成功，详保存 check-current。

x64 首个有意义错误是 `InkeysHeadlessTests.pdb` 的 LNK1285，不是源码编译错误或依赖缺失。主会话核实它位于工作区、是被 Git ignore 的普通生成文件、父路径均非 reparse 后，将 106,618,980B 的损坏 PDB 移到同目录 `InkeysHeadlessTests.pdb.scroll-follow-final-corrupt` 留档，让原链接步骤重新生成；没有删除其它缓存或改变 PDB 选项。原文件 SHA、路径、长度及原因在 `x64-pdb-quarantine.json`。同配置重试立即成功。首次 x64 编译的既有 warning 保留在日志，不能把最后增量重链 0 warning 当作全冷编译没有 warning。

## x64 纯命令行复验

仅运行已审查的严格早退入口，所有子进程 hidden，未创建显示窗口或操作桌面。配置用例使用原测试内部私有根；TEMP/TMP 明确设为工作区私有目录。

| 命令 | Exit | 外部耗时 |
| --- | ---: | ---: |
| Inkeys `--scroll-follow-protocol-test`（含 Business/真实 Tracker 控制矩阵） | 0 | 893ms |
| Inkeys `--state-mode-production-test` | 0 | 187ms |
| Inkeys `--bar-view-adjust-config-test` | 0 | 252ms |
| Headless `--scroll-follow-only` | 0 | 713ms |
| Headless `--freeze-capture-only` | 0 | 2,178ms |

汇总为 `x64-final-targets.json`，对应输出 `x64-final-*.log`。这是 Windows 11 ARM64 主机上执行 x64 产物的结果，不是 x64 硬件或旧系统验收。没有在 x64 重复长算法矩阵或不可见 Host/真实文件矩阵；后两者的最终 ARM64 实际结果见保存 check-current。

## PE 与可选功能平台边界

使用当前 VS 工具版本的 ARM64 原生 dumpbin，分别读取主程序 `/headers /imports /dependents`；日志 `pe-arm64-final-*`、`pe-x64-final-*`。

- machine 分别为 ARM64 AA64、x64 8664，目标架构正确。
- 两个主程序都把 `MAGNIFICATION.dll` 放在 delay-load 区，没有新增 OpenCV/highgui/FFmpeg 动态依赖，没有 WGC 依赖。
- 本功能的 `RtlGetVersion` 用 GetModuleHandle/GetProcAddress 查询，PE 没有该硬导入；不使用 WGC/CreateForWindow 或 SetWindowDisplayAffinity。
- x64 PE 没有 WaitOnAddress 或 `api-ms-win-core-synch-l1-2-0.dll` 硬导入。ARM64 产物含该架构运行库的 WaitOnAddress/WakeByAddress 入口，不能用 ARM64 导入表证明 Win7 运行。没有为兼容检查改动现有运行库或工具链。
- 此检查只确认本次可选能力的导入边界，未运行 Win7/8.1/WOW64 或 Release/harness-off；不能把静态检查写成完整程序在旧系统已验。

## 最终产物标识

| 产物 | SHA-256 |
| --- | --- |
| ARM64 Inkeys | `28B8E8291E28F341656681956D5C6E2B74895086B73B05067C04E9FBD9A63488` |
| ARM64 Headless | `ED614767D28D07A27D60B0197F964097645B321A5229BD6502BA7E7E6727456A` |
| x64 Inkeys | `9C15F1F6259C49EBE862ED54434782EE955D96A6B9BA670FAF234F30C5B2DE30` |
| x64 Headless | `F1AC4B3E4BC3C151562EF86692CD034F429FA24E48D39E928101DACEB9D1D1DE` |

产品阶段完整 `--no-window` 和退出失败接管通过的结果及依赖关联，以产品/保存两份 check-current 为准。最后只加强主程序 HiddenWindowTest 断言，Headless 的最终字节未再变化；未宣称对最终 Headless 重新跑过全量矩阵。

剩余必要步骤是正式 Mag 实屏连续图像、系统窗口交接及真实内容/输入验收，见 usage-and-live-acceptance.md。遵守本轮权限，在此停止，不自动 commit/push，不标完整功能已通过。
