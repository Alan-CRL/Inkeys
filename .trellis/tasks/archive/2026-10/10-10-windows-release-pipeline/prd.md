# Windows 发布流水线拆分与 Win32 Harness 修复

## 目标
只发布产品项目，按 PptCOM → Inkeys 三架构构建 → Package/Upload 传递同次构建产物。
## 要求及验收
R1：Release 默认剔除 UI3 内嵌 Harness，Debug和显式测试构建保留，Win32 64位宿主构建无 C1002；内部测试参数在关闭时非零early退出。
R2：解除 Inkeys/PptCOM solution依赖与两处ProjectReference；Release不默认编译两个独立测试工程；独立native构建仍输出根Build目录。
R3：PptCOM保留自身输出目录TLB导出，取消复制；Inkeys/PptCOM.dll继续Git跟踪且不忽略，由维护者手动同步；本地文件和RC不动；同步中英文编译流程。
R4：PptCOM Release/AnyCPU仅一次，无vcpkg/submodule；native Windows2022矩阵仅vcxproj，无.NET4/NuGet初始化。
R5：DLL 与同次 TLB 独立传递；选定 artifact ID 及 TLB ID 传给三个 native 构建并覆盖原资源路径。
R6：所有产物清理限定当前 run 的精确 ID。
R7：构建、上传及下载失败必须使 job 失败。
R8：流水线配置说明集中在 YAML 中。
## 子任务
- 10-10-win32-harness-build：R1及native工程配置部分；拥有Inkeys.vcxproj与Harness源/头。
- 10-10-pptcom-split-signing：R4-R8的workflow与校验脚本。
- 主会话：R2 solution部分、R3 PptCOM工程、随附DLL跟踪及编译流程、规范同步、集成验证。
## 不在范围内
COM ABI/Office兼容、渲染重构、依赖升级、远程触发及GUI测试。
