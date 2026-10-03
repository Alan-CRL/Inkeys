# Win7 GPU 分界诊断交付验证（2026-10-03）

继续原 win7-main-canvas 任务；用户明确要求补调试输出并继续 Win7 实测。上一份完整 Win7 日志证明 mouse 接受、RTS 下/上与 controller 13 次 contact，连续笔/橡皮 ULW staging 源全零；具体 GPU 根因尚未证明。本轮只增加判别能力，未改 shader、输入策略、swapchain 或呈现后端。

## 修改与结果

- HiddenWindowTest.cpp/.h、IdtMain.cpp：早退 `--draw3-renderer-pixel-test`，不加载配置或进入单实例/产品窗口。WARP FL11_0、128×128、永不显示 layered fixture HWND、普通 FLIP_SEQUENTIAL，复用正式 renderer/shader。四项正式 Clear/Cursor 像素检查决定退出码；两项关闭混合只是对照。
- RendererPrimitives.cpp：首个可见光标请求及每秒最多一次 cursor-gpu；资源缺失、Map HRESULT、上传完成、Draw 前后、实际 viewport、RTV/backbuffer canonical identity、VS/PS/CB/SRV 绑定和设备状态。Draw(void) 已发出不代表像素成功，保持正式行为。
- Collect-Win7.ps1：先跑独立 probe、最多 30 秒、仅终止自己创建的超时子进程；保存 probe 输出和退出信息，即使失败仍采正常应用。保留 PS2 public HashAlgorithm.Clear() 修正；脚本不修改配置。
- input-and-ink.md：新增七节分界诊断合同；检查报告见 [独立审查](research/check-gpu-probe-2026-10-03.md)。

## 构建与验证

使用 vswhere 定位 ARM64 原生 MSBuild，完整 InkeysRepo.sln，/m:1 /nr:false，隔离 OutDir/ZhjOutputDir；每个构建允许 900 秒。同一 PowerShell invocation 清理重复 PATH 并设置 MSBUILDDISABLENODEREUSE=1。未启动产品主窗口或交互 GUI。

| 检查 | 结果 |
| --- | --- |
| 完整 solution Debug/ARM64 | 退出 0 |
| 完整 solution Release/x64 | 退出 0 |
| ARM64 renderer-pixel-test | 退出 0；四项正式 + 两项对照 PASS；hidden visible=0 |
| x64 renderer-pixel-test | 退出 0；四项正式 + 两项对照 PASS；hidden visible=0；在 Win11 ARM64 运行 |
| 两架构清屏像素 | 16384 非透明/非零 RGB，BGRA=(64,32,16,128)，clear_mismatch=0 |
| 两架构正式 Cursor | 848 非透明/非零 RGB，中心 BGRA=(128,128,128,128)，角像素零；VS invocation=6/PS=1368 |
| ARM64 Headless --no-window | 退出 0，PASS animation correctness |
| Runtime GPU 输出 | upload-success、before-draw、after-draw-issued；实际目标身份和 shader/CB/SRV match=1 |
| Collector | parser 及成功/非零退出/超时/启动异常四条模拟通过；所有路径继续正常采集；模拟未启动应用 |
| 格式/边界 | git diff --check 通过；生产 UTF8/CRLF 且保持各文件原 BOM 状态、脚本 ASCII/CRLF；原 .gitignore 与八份保留 cso 哈希保持 |
| 包完整性 | ZIP testzip 通过，包内 EXE SHA 与构建一致；PE 0x8664；CLI/cursor-gpu/PixelTest 字符串存在 |
| Win7 GPU / PS2 / 可见恢复 | 待同一问题设备实测，不能用本机结果代替 |

已有第三方/IdtMain 数值转换警告、Debug LNK4075 保留；未修改项目或工具链。Pipeline query HRESULT 仅辅助诊断；四项正式检查通过不能掩盖真实产品帧问题。

## 候选身份与实测步骤

源码基线 d03713bdf456c9da80adb6e0df0f27ebd1399765，包含未提交诊断增量；manifest 精确记录六份源码和包内文件 SHA。EXE 43823616 字节，SHA256 `06CD58C7E1E1F01042D489902457C18FF3E32DAF843197386AE7BB7B283DF8AE`；ZIP SHA256 `4D4F3E9B868DECEA8909E8AC6DBF740B3606678C03DF52A85FD75F97B474774E`。

[新 x64 测试包](C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-gpu-probe/Inkeys-Win7-Gpu-Probe-x64-Release.zip)。保留上轮包，未覆盖 G: 现场文件。

1. 退出现有 Inkeys、备份旧 EXE。将本包 EXE、PptCOM 三份运行文件和两份 Collect 脚本放入原测试目录，沿用原配置。
2. 开启 ConsoleOutput.Draw3/ConsoleOutput.Cursor，双击 Collect-Win7.cmd；自动自检后进入应用。
3. 空白页中部鼠标连续画线至少 3 秒；橡皮连续移动至少 3 秒；激光笔连续移动/按下至少 3 秒，记录分别是否显示。
4. 正常退出，回传整个 Win7-Diagnostics/时间戳目录，包含两份 pixel-test、两份 console、identity 和本次 idt log。

Clear 与正式 Draw 的对比会定位设备/读回、生产管线或产品实时绑定；关闭混合对照不能直接作为正式修复。任务保持 in_progress；没有 commit/push/归档。
