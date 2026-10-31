# Windows 7 SP1 + KB2670838 兼容性调查

调查日期：2026-09-01

## 1. Confirmed Project Target

用户明确要求 UInk 主文件能力以 Windows 7 SP1 + KB2670838 为最低运行平台，不能因新增代码或依赖导致该系统无法启动。

该要求与 `.trellis/spec/native/platform-and-resources.md` 一致。项目规范区分：

- 项目级目标：Windows 7 SP1 + KB2670838；
- 当前代码路径：源码存在的兼容处理；
- 实测能力：必须记录具体 OS/补丁/架构/环境，未实测不能宣称通过。

当前开发构建使用 Windows SDK `10.0.26100.0`、MSVC v143 和 x86/x64/ARM64 vcpkg triplets。较新 SDK 是编译环境，不等于可以静态依赖 Win8+ runtime API。

## 2. Existing Repository Pattern

`inkStrokeModelerTest/draw3/win7_compat.cpp` 已处理一个直接相关的问题：

- `GetSystemTimePreciseAsFileTime` 仅在运行时通过 `GetProcAddress` 查找；
- Win7 缺失时回退 `GetSystemTimeAsFileTime`；
- 提供导入指针别名，避免 v143 生成的代码在 Win7 loader 阶段因 Win8 API 缺失而启动失败；
- x86 另处理 stdcall decorated symbol。

因此 UInk 实现不能只检查自己是否显式调用新 API，还要检查标准库、模板实例化和第三方 header 最终产生的 imports。发现 Win8+ import 时应优先使用 Win7 API；确需新 API 时沿用“动态解析 + 明确 fallback”，不能静态链接后等调用时才处理。

## 3. File APIs Suitable For The Task

微软官方文档列出的最低客户端：

| Operation | Candidate | Minimum client | UInk use |
| --- | --- | --- | --- |
| 原子替换既有文件 | `ReplaceFileW` | Windows XP | 同卷临时 `.uink` 替换目标 |
| 截断到安全追加边界 | `SetFilePointerEx` + `SetEndOfFile` | Win7 可用；`SetEndOfFile` 文档列 Windows XP | 恢复后显式 append transaction |
| 刷写文件缓存 | `FlushFileBuffers` | Windows XP | 完整保存临时文件和默认 append durability |
| UTC 时间 | `GetSystemTimeAsFileTime` | Win7 可用 | Header Unix seconds |

官方来源：

- `ReplaceFileW`: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
- `SetEndOfFile`: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setendoffile
- `FlushFileBuffers`: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers

`ReplaceFileW` 要求 replacement 与目标在同卷，这与设计中“目标同目录临时文件”一致。API 的失败状态并非全部保证两个名称完全不变，因此实现必须按具体 error code 报告和清理，不能把一句“原子替换”简化为所有失败都毫无文件系统变化。成功提交前的临时文件仍需完整自检。

## 4. APIs And Behaviors To Avoid

- `GetSystemTimePreciseAsFileTime` 最低客户端为 Windows 8，官方文档： https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimepreciseasfiletime 。UInk Header 只有秒精度，无需使用它。
- 不使用 `GetTempPath2W`、`CopyFile2`、`PathCch*` 或 Win8+ 的 file rename information class。
- 不依赖 Windows 10 1607 的 long-path manifest opt-in；需要长路径时使用 Win7 已支持的规范化绝对 UTF-16 path 形式，并在 API 边界明确限制/诊断。
- 不使用只在新系统成立的 `std::filesystem::rename` 行为假设作为事务保证；主事务由明确的 Win32 API 和错误码驱动。
- 不通过新版 shell、Storage API、WinRT 或 packaged-app API 处理路径。
- 不要求安装 Universal CRT 或其他本任务新增的系统组件；沿用项目既有部署前提。

## 5. Proposed File Layer Contract

公共 API 接受 UTF-16 path view 或项目已有 path 类型，第三方和 Win32 handle 不外泄。内部 RAII 管理 `HANDLE`，所有失败返回项目结果类型和 Win32 error context，不引入跨模块异常边界。

建议操作：

```text
Read:
  CreateFileW -> GetFileSizeEx -> bounded ReadFile

Full save:
  create unpredictable sibling temp
  -> bounded WriteFile
  -> FlushFileBuffers
  -> CloseHandle
  -> strict self-read validation
  -> ReplaceFileW(existing) or Win7-compatible rename(new target)

Recovered append:
  CreateFileW(read/write)
  -> verify identity/length
  -> SetFilePointerEx(safe offset)
  -> SetEndOfFile
  -> WriteFile(pre-encoded batch)
  -> default FlushFileBuffers (explicit Buffered may skip)
```

读取永不截断。只有调用方提交已验证 append plan 后才以写权限打开并执行恢复截断。

append 默认刷盘已确认；若完整 batch 写入后 `FlushFileBuffers` 失败，返回 `WrittenNotDurable` 和写入后的 revision，而不是允许盲目重试。完整保存始终刷新临时文件后再关闭、自检和替换。

## 6. Dependency Rules

- msgpack-cxx 是 header-only，但仍需审计其模板实例化和所用 C++ 标准库路径。
- 当前 7.0.0 先做 build/import spike；允许因明确必要性升级 baseline，但升级后必须比较所有端口版本变化。
- 新依赖不得静态要求 Win8+ DLL/API；发现时必须选择兼容版本、增加运行时探测回退或放弃该依赖。
- 首版不实现 `.uink.extra`，因此不引入 ZIP/压缩 DLL，也减少了一组 Win7 和安全风险。
- MessagePack、文件服务公共接口不暴露第三方 ABI，便于在兼容性失败时替换实现。

## 7. Validation Plan

### Static/Build Validation

- 构建完整 solution 的 Debug|ARM64；按风险补充 Release 和 x86/x64 构建。
- 用 `dumpbin /imports` 或等价静态工具审计测试程序及最终 Inkeys 接入二进制的新增 imports。
- 特别检查 Kernel32 静态入口点、额外 DLL、C++ runtime 和 vcpkg 连带依赖。
- 搜索源码中的 Win8+ API 名称不能代替 import audit，因为调用可能来自标准库或第三方 header。
- 任何动态解析 API 都测试“符号存在”和“符号缺失”两条无窗口路径。

### Target-System Validation

- 可用时在 Windows 7 SP1 + KB2670838 x86/x64 环境运行：空文件拒绝、公开 fixture 读取、完整保存、截断尾恢复后 append、替换失败和 flush 错误测试。
- 记录 OS/补丁、架构、文件系统、测试二进制和结果。
- ARM64 Windows 11 结果只能证明当前开发配置，不是 Win7 实测。
- 若任务结束时没有 Win7 环境，报告“项目目标和静态/API/import 审计通过，Win7 目标机待验证”，不能写成已经运行通过。

## 8. Acceptance Consequence

Win7 兼容不是一个可选优化。以下任一情况阻止任务通过：

- 新增无 fallback 的 Win8+ 静态 import；
- 新增只支持 Win8+ 的必需 DLL；
- full-save/append 依赖 Win8+ 文件事务 API；
- baseline 升级后未审计连带依赖和 imports；
- 把 Windows 11 ARM64 测试结果表述为 Win7 兼容实测。

## 9. Implementation Verification (2026-09-01)

本轮实现未升级 builtin baseline，仅在根 `vcpkg.json` 增加 header-only `msgpack`；实际解析版本为 msgpack-cxx `7.0.0`，公共 module API 不暴露第三方类型。

| Configuration | Full solution build | Headless test executable |
| --- | --- | --- |
| `Debug|ARM64` | Pass（ARM64 原生 MSBuild） | Pass |
| `Release|x64` | Pass | Pass |
| `Release|x86` | Pass | Pass |

三套测试均覆盖完整既有测试集合和 UInk persistence 测试；最终 `Debug|ARM64` 在源码格式归一化后再次构建、运行通过。

静态二进制审计结果：

- x64/x86 PE machine 与目标架构一致，OS/subsystem version 均为 `6.00`；
- 主程序依赖保持为项目既有的 `KERNEL32`、`USER32`、`GDI32`、`ole32`、`WINMM`、`d3d11`、`dwmapi`；测试程序新增使用系统 `bcrypt`，msgpack 不产生运行时 DLL；
- x64/x86 测试产物实际导入 `CreateFileW`、`ReadFile`、`WriteFile`、`FlushFileBuffers`、`ReplaceFileW`、`MoveFileExW`、`SetEndOfFile`、`SetFilePointerEx`、`GetFileInformationByHandle`、`GetFullPathNameW`、`CompareStringOrdinal`、`DeleteFileW`、`IsNormalizedString` 与 BCrypt 哈希/随机数 API；
- `WaitOnAddress`、`WakeByAddress*`、`GetTempPath2*`、`CopyFile2`、`CreateFile2`、`GetSystemTimePreciseAsFileTime`、`SetFileInformationByHandle`、`PathCch*` 均为零静态导入；
- 文件层仅使用 UTF-16 Win32 API，不接触 `.uink.extra`，Media path 校验不解析或打开外部路径。

当前没有 Windows 7 SP1 + KB2670838 x86/x64 目标机环境。因此结论严格限定为：代码、API、PE 和静态 import 兼容审计通过；目标系统运行验证待完成，未将 Windows 11 构建/测试结果表述为 Win7 实测。
