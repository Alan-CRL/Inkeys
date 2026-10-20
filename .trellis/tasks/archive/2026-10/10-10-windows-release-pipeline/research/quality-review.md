# 独立质量复核记录

复核角色：trellis-check。范围：父任务 R1–R8，使用用户最新修正（随附 DLL/TLB 保持 Git 跟踪、维护者手动同步）。没有执行 MSBuild、GUI、远程写入或 Git 提交，避免与主会话串行验证互相干扰。

## Findings (fixed)

- File: `.github/workflows/build-windows.yml` / `.github/scripts/test-release-artifacts.ps1`
  - Issue: 最终 Package 已上传后，如果多个 EXE 中间 artifact 清理只完成第一项、后续 API 失败，Package job 会失败；重跑该失败 job 已缺构建输入。
  - Fix: 经主会话确认，由 pipeline 实施者仅给末尾非关键 EXE cleanup 加 `continue-on-error: true`，catch 记录 warning 和 summary，再抛出使 step 的实际失败可见。测试执行真实 cleanup 块并模拟部分删除/后续失败；成功清理后重跑 Package 需重跑上游 build 或完整链路。

## Findings (not fixed)

没有待修复的源码或流水线配置问题。以下是已明确的验证范围和操作边界，不作为未实现缺陷：

- 真正 Windows 2022 三架构发布链路需要通过 Actions 验收；本次不触发 workflow。
- 本机实际 C++ 编译/运行由主会话负责，复核过程中仍在串行执行。该记录的静态通过不能代替 Win32 Harness off/on、ARM64 Debug Solution 与 headless 的最终退出码。
- 为沿用成功后的中间产物清理行为，成功清理后的 Package 单独重跑不能重新生成已删除输入；需包含上游 build。

## Verification

- Lint: **pass** — 最终 `git -c diff.ignoreSubmodules=all diff --check` 无新增尾空白；raw bytes 检查原有 BOM 保持、所有修改的实现/文档文件为纯 CRLF。Git blob 因 autocrlf 存 LF，未将 blob 行尾误用为原工作区格式。
- TypeCheck: **PowerShell / XML / YAML 静态 pass；C++ 构建由主会话记录** — 两份 PowerShell helper 与 47 个 workflow PowerShell 块全部经 Parser 检查；工程 XML 解析通过；YAML 与完整 job DAG 检查通过。
- Tests: **产物校验模拟 pass（退出 0）** — 独立复跑 `.github/scripts/test-release-artifacts.ps1`，核实缺构建输入 fatal、精确 ID/跨 run/重跑替换 ID 和部分 cleanup 失败。REST 调用使用 mock。

### 行为路径复核

- 六个 native 配置统一定义 Harness 宏；Debug 默认 true、Release 默认 false，显式参数可覆盖。runner、授权测试、固定轨迹同一条件剔除，剩余产品 stub 与原 no-fixture 分支一致。
- `wWinMain` 在配置、单实例、任何 HWND 初始化前调用真实 early 解析函数；关闭时内部参数返回 81，普通参数不变。新增 headless 测试直接编译该禁用分支并调用产品函数，没有复制解析实现。
- solution 仅移除测试项目三个 Release 的 Build.0，Debug 映射保留；native 两处 PptCOM ProjectReference 和 solution 依赖已移除；native 独立 SolutionDir fallback 指向根 Build。
- PptCOM target 保留自身 TLB 导出、删除 DLL/TLB Copy；随附 DLL 仍 Git 跟踪、不匹配 ignore，RC/#import 路径未变。
- Check → PptCOM → native matrix → Package → Upload；所有 Windows job 为 Windows 2022。C# job 不初始化 vcpkg/子模块，native 不做 .NET4/NuGet restore，只编译 vcxproj，并显式 x64 host/Harness=false。
- DLL/TLB 分别 artifact，构建与 artifact 上传/下载失败必须使 job 失败。
- 所有 artifact 上传设置 no-files:error 与 overwrite；下载指定 ID 并合并至指定目录。清理按精确 ID 并再核当前 run/名称，不删除别的 run。
- 每个 native 产物与最终选择均校验内嵌 DLL SHA-256，LoadLibraryEx 使用数据映射，不执行入口。
- 中英文编译说明、native/PPT Trellis 合同与最新需求一致。未扩大 COM/渲染/依赖升级范围。

## 主会话补充：实际质量门

- PptCOM Release/AnyCPU、Win32 Release Harness 默认关闭/显式开启、ARM64 Debug 完整 solution 均退出 0。
- ARM64 headless --no-window 在允许测试临时文件操作的环境退出 0；新增参数回归通过。沙箱 C1902/分页资源/临时文件权限问题均由执行环境复核解决，未污染源码。
- 新 Win32 两种 EXE 及新 ARM64 Debug EXE 的内嵌 DLL SHA-256 与随附 DLL 一致；本地跟踪 DLL/TLB 内容未变。
- 实际命令、工具路径、warnings 和外部验收范围见同目录 build-verification.md。

本任务本地质量门通过。真实三架构 Windows 2022 发布链路仍属于外部验收；未触发 workflow，未执行 GUI 测试。
