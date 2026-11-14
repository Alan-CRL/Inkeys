# 编译说明更新

## Goal

按 dev 实际构建流程改写编译说明：Inkeys 与 PptCOM 解耦、手动拷贝 PptCOM.dll、.NET Framework 4.0 目标与升级说明

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.

## 调查（2026-11-09）

- Codex `gpt-6.1-sol` High（service_tier=default，只读沙箱，exit 0，工作区无改动）。简报：`research/codex-brief.md`；报告：`research/codex-build-audit.md`。
- Claude 独立核对：sln 中 Inkeys↔PptCOM 无依赖；`Inkeys.rc:174` 嵌入 DLL，`IdtPlug-in.cpp:317` `#import "PptCOM.tlb"`；DLL/TLB 均受 Git 跟踪；`Package/` 不受跟踪，需 nuget restore；`ActionsRes/v4.0.zip` 内为 `v4.0/`（598 项），CI 复制到 Reference Assemblies；App.config sku 为 v4.0；manifest runtimeVersion v4.0.30319。

## 实施

- 改写 `GithubRes/CompilationProcess_zh-CN.md`（UTF-8 无 BOM、CRLF）：解耦说明、整个解决方案生成会带上 PptCOM 的提示、解决方案配置表、.NET 4.0 两种方式（安装参考程序集 / 本地升级目标框架，manifest 不改）、NuGet 还原、DLL+TLB 手动同步表；`.\vcpkg` → `.\Vcpkg`。
- 用户要求（2026-11-09）：.NET 部分只写「需要 .NET Framework 4.0」，不写非 4.0 时的升级或安装方法；已删去两种方式。
- 保留：纯英文路径要求、Windows 应用程序开发工作负荷（Codex 认为证据不足，作者经验，未删）。
- 待定：`PptCOM/PptCOM.cs` 文件头的切换指南是否同步更新（源码注释，等用户决定）。
- 用户确认保留「生成解决方案也会编 PptCOM」提示及解决方案配置表（2026-11-09）。

## 定稿（2026-11-09，用户手动精简）

`GithubRes/CompilationProcess_zh-CN.md` 以用户精简后的版本为准，不再追加细节。英文版按此版翻译。删掉了以下内容：
- 解耦说明及「生成解决方案也会编 PptCOM」提示
- 解决方案配置表
- 命令行 msbuild 及 Harness / PreferredToolArchitecture 说明
- VS2026 保持 v143 的提示
- 首次 vcpkg 较慢的提示
- PptCOM 的 msbuild 命令、TLB 导出说明，以及手动同步 DLL/TLB 一节

保留：两个工程简介、主程序环境与步骤（`.\Vcpkg`、清单模式、子模块）、FAQ，以及 PptCOM 的环境（只写 .NET Framework 4.0）和 NuGet 还原步骤。
