# 2026-10-03 Win7 输入 / ULW 像素诊断验证

## 结论与范围

继续同一个 Win7 主画布任务。新日志已证明一段鼠标悬停通过光标过滤、生成橡皮 visual 并提交帧；这段不能归因于缺少 Win8 Pointer API。普通落笔经过独立 RTS contact 链，尚无现场回调或实际源像素证据，不能宣称唯一根因或实机恢复。

本轮三个生产文件的增量：

- `Draw3.RealtimeStylus.cpp`：Cursor 开关提供运行期 Down/Up 到达、解码、发布或取消结果，Packets 原子限频；复用有界队列，保持 producer 输入行为。
- `Draw3.TransparentPresentation.cpp`：Draw3 开关提供 dirty 源与最终 DIB 的 alpha/RGB、bounds、预乘与 ULW 结果；复用已有读回，保持拷贝、底层和窗口策略；已有 copy benchmark 增加八项统计断言。
- `IdtMain.cpp`：保留采集启动器的文件/管道重定向；双路重定向不创建控制台。普通直接启动控制台策略保留。

详细证据、源码链和官方 API 资料见 [调查说明](research/win7-input-presentation-2026-10-03.md)，独立检查见 [检查报告](research/check-2026-10-03.md)。

## 候选身份

源码基线 HEAD：`d03713bdf456c9da80adb6e0df0f27ebd1399765`，分支 `chore/publish`；诊断包含本轮未提交增量，不等同纯 HEAD 构建。具体生产补丁及各源文件 SHA-256 与最终 EXE SHA-256 保存在候选 manifest；不能仅凭原 build identifier 推断 Git SHA。

隔离输出根目录：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-input-pixels`。

最终 x64 候选：[诊断包](C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-input-pixels/Inkeys-Win7-Input-Pixels-x64-Release.zip)。EXE SHA-256：`1E41D0D5EAAD4FB5D98D0FB0E840398A4F380F4A0DAB0FBE6E2DE2DF007BF7FA`；ZIP SHA-256：`905B0B39E896F7190FB0E9A07F8183C66869959778DF5ECF46D5B640C7FBA215`。PE machine 为 0x8664，二进制已确认包含新增 RTS 和 ULW 诊断字符串。

## 构建与测试

通过 vswhere 定位当前 Visual Studio 的 ARM64 原生 MSBuild。两种配置均使用 `InkeysRepo.sln /m:1 /nr:false`；在同一 PowerShell invocation 先移除重复 PATH、设置 `MSBUILDDISABLENODEREUSE=1`。隔离 OutDir/ZhjOutputDir，保留当前依赖、SDK、工程和工具链。每次构建允许 900 秒，未启动交互式界面。

| 验证 | 结果 / 限制 |
| --- | --- |
| 完整 Solution Debug / ARM64 | 首轮与包含 IdtMain 修正的增量均退出 0；输出 ARM64-Debug/build 与 rebuild 的 stdout/stderr |
| 完整 Solution Release / x64 | 首轮与包含 IdtMain 修正的最终增量均退出 0；输出 x64-Release/build 与 rebuild 的 stdout/stderr |
| ARM64 `InkeysHeadlessTests.exe --no-window` | 退出 0，末尾 PASS animation correctness；无窗口纯逻辑回归 |
| ARM64 `Inkeys.exe --draw3-ulw-copy-benchmark` | 退出 0，`pixel_diagnostics=PASS cases=8`，各原有复制场景完成 |
| x64 `Inkeys.exe --draw3-ulw-copy-benchmark` | 最终候选退出 0，新增八项 PASS；Win11 ARM64 的 x64 执行不代替 Win7 |
| InitializeDebugConsole 提取函数测试 | ARM64 模拟 36 种句柄组合、重复初始化均通过；源码及 build/run 记录在 console-check |
| InitializeDebugConsole 实际 CRT 测试 | ARM64 Windows subsystem；真实 GetStdHandle/GetFileType/CRT，AllocConsole 截获而非真正分配；双路重定向下 cout/cerr、fprintf、WriteFile 全部存在；编译、运行、校验退出 0 |
| 采集脚本 | 当前 PowerShell parser 通过，元数据表达式为五项；静态采用 PS2 API/语法，未在 Win7 PS2 执行 |
| 编码、diff 与原始文件 | 三份生产源码 UTF-8 BOM + CRLF；diff check 通过；原始现场包、四份用户 Demo cso、四份生产 shader、原 .gitignore 哈希保持 |
| Win7 实际输入与桌面可见性 | 未验证；本机 Win11 ARM64 构建及无窗口检查不能代替 Win7 验收 |

已有第三方数值转换及未改动 controller/settings/test 的 warning 保留；Debug 的 LNK4075 来自本次禁用增量链接与原 EditAndContinue 设置，未修改工程来消除。没有把编译警告或本机测试通过写为 Win7 已恢复。

RTS 的 `published=1` 仅表示 producer 发布，仍需 controller 消费与真实像素证据。测试证明诊断统计和关闭诊断时拷贝输出一致，不能证明 D3D shader 在 Win7 一定写入可见墨迹。

## Win7 最短取证步骤

1. 退出现有 Inkeys；保留旧 EXE。将候选的 Inkeys.exe 和 Collect-Win7.ps1/.cmd 放入原测试目录，沿用原配置；开启 `ConsoleOutput.Draw3` 与 `ConsoleOutput.Cursor` 并重启才生效。
2. 双击 Collect-Win7.cmd。脚本会启动应用并等待正常退出，将 stdout/stderr 从启动完整写入文件；本轮保留重定向的修正避免输出被重新绑定 CONOUT$。
3. 在空白页画布中部，用鼠标按下并持续画线至少 3 秒后抬起；切橡皮，在画布中部连续移动至少 3 秒；再切激光笔持续移动/按下至少 3 秒。不要仅静止悬停：像素采样只在实际 dirty/full 提交时发生。若有真实笔，可另记录同样操作，但鼠标复现不依赖设备笔。
4. 正常退出。回传脚本打印的整个 `Win7-Diagnostics/<时间戳>` 目录，含两份完整 console、identity.txt 和本次 IDT log，并简述各阶段是否看到笔迹/光标。

采集脚本不改配置、不删除原日志、不自动覆盖旧 EXE；只创建新时间戳输出目录和复制本次日志。本机没有启动该采集脚本或应用主窗口。

## 下一步判别

| 现场证据 | 下一个定位边界 |
| --- | --- |
| Mouse Down 到达，但 RTS Down arrival 缺席 | 核对诊断开关、完整采集和 dropped；证据完整后调查 RTS/鼠标 tablet/HWND 接收 |
| RTS Down 到达但解码或发布失败 | 按具体 reason、tcid/cid、属性数、device 追踪输入分支 |
| RTS 发布正常，连续操作时源 alpha 仍为零 | controller 准入、渲染上传、shader、viewport、混合/纹理输出 |
| 源有预期像素，最终 DIB 异常 | staging/stride/格式/拷贝与底层合成 |
| 源与 DIB 正常，ULW 失败 | Win32 error、真实窗口参数与生命周期 |
| 源与 DIB 正常，ULW 成功但仍不可见 | Win7 USER32/DWM 的 dirty 更新与窗口层级；再做受控全量提交对照 |

保持任务 `in_progress`，等待 Win7 实际恢复证据；未 commit、push、发布或归档任务。

## Win7 采集脚本实机兼容性修正

用户新附件 console 报告 SHA256Managed 的直接 Dispose() MethodNotFound，位置 Collect-Win7.ps1:13；错误在 Start-Process 之前，故本次尚未取得应用 RTS/像素数据。旧 CLR 的 Dispose 是显式 IDisposable 实现，脚本改用 public HashAlgorithm.Clear()（.NET Framework 1.1 起可用）；FileStream.Dispose() 保留。官方说明：[Clear](https://learn.microsoft.com/en-us/dotnet/api/system.security.cryptography.hashalgorithm.clear)、[旧 Framework IDisposable.Dispose](https://learn.microsoft.com/en-us/dotnet/api/system.security.cryptography.hashalgorithm.system-idisposable-dispose)。

单行脚本修正保留 ASCII/CRLF；本机 parser、默认 SHA256 provider 和显式 SHA256Managed 的哈希/清理检查通过；未运行应用、未构建、未在 Win7 PS2 实机验证。原完整包脚本有此遗漏，请用 [脚本修正包](C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-input-pixels/Collect-Win7-PS2-fix.zip) 替换 Collect-Win7.ps1；EXE 不需要更换。修正脚本 SHA256：6057F0D8D073F5383379EBA4D1B92D85CFC06B22946A2E22A0B203503D1D0A32。
