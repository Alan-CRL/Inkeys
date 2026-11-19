# 实施与验证顺序

1. 用户批准后，复核阶段 C 工作区的最终改动。共享热点为 Draw3.Host、TransparentPresentation、DrawingController、Setting 和 i18n；以现有文件增量修改。
2. 在 Other.Config 的 Draw3 schema 和读取迁移处接入两个枚举，在性能页接入两个下拉框、重启提示与翻译。验证新装、旧 true/false、新旧键并存、未知值和配置往返。
3. 从 IdtMain 的预置 HWND 样式与启动回退到 Host、Presenter 的初始化和恢复，共用选路逻辑。添加无窗口用例覆盖自动永不尝试 Win7Dwm、手动条件、平台与颜色状态检查、候选顺序和严格测试模式。
4. 实现第一版 DWM 呈现与两种交换链。验证创建、首次完整帧、Resize、合成状态变化、成功 Present 后的回退和输入恢复。调整隐藏窗口测试原有“两版均禁用”断言，保留第二版与非目标系统的门禁。
5. 发布实际后端、链、设备与失败原因，接入阶段 C 性能签名和旧账本读取测试。确认用户在视觉异常时可通过设置或配置文件重启回到 ULW。
6. 以仓库规定的原生 ARM64 MSBuild，在同一 PowerShell 调用中规范化 PATH、关闭节点复用，构建 InkeysRepo.sln 的 Debug|ARM64，至少允许 5 分钟；运行 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window。补充现有 Win32/x64 配置的编译及最终 PE 导入检查。最后核对 git diff --check、编码、换行。环境限制单列。
7. 实现后在 Win7 SP1 + 仅 KB2670838 的 x86/x64 机器上分别覆盖 Aero 透明、Aero 着色不透明、Basic；记录实际链与驱动，检查停笔看视频、荧光笔、清屏、拖影、选区交接、合成关闭和重启。对照同机两种链和阶段 B ULW 的提交耗时、尾延迟及视觉表现。由用户进行 Win7 实机对照测试，按实际生效的链归类，未执行项记待验收。等待用户反馈；若确认 FLIP 在 Win7Dwm 下不稳定或不可用，后续仅对 Win7Dwm 禁用 FLIP，所有 Win7Dwm 请求统一使用 SEQUENTIAL。

## 本机验证 · 2026-11-19

- Debug|ARM64 原生 MSBuild 构建及 InkeysHeadlessTests.exe --no-window 通过。增量链接曾触发 LNK1000，本次命令使用完整链接。配置迁移、后端选择条件、链创建回退、账本读写及 i18n 校验已通过。
- Release|x64、Release|Win32 已使用现有 solution 编译并生成 Build/Win7DwmTest 下的两个测试 ZIP。经用户批准，将 Draw3.Product 和 SelectionOverlay 中触发 C4996 的 shared_ptr 原子读写迁到 C++20 atomic 特化，保留原内存顺序。
- 两个 PE 的架构分别为 x64、x86，子系统版本均为 6.01；未静态导入 dcomp.dll。已核对内嵌 PptCOM 与源 DLL 的 SHA-256，逐包校验 Inkeys.exe 解压内容与原始产物一致。
- Win7 SP1 + 仅 KB2670838 的透明效果、动态背景、运行期交接和 FLIP 稳定性等待用户实机结果。

## 设置与回退提示修订

两个下拉使用完整名称；初始化信息仅在软件调试页展示，呈现方式与交换链的降级警告分别位于当前状态块上方。DirectComposition 实际尝试所选交换链。本机硬件及 WARP 的无窗口实测中，单/双缓冲 SEQUENTIAL 均返回 DXGI_ERROR_INVALID_CALL，双缓冲 FLIP_SEQUENTIAL 成功完成创建及 visual commit。Win7 实际显示仍由用户验收。

ARM64 Debug、Win32/x64 Release 构建、完整 --no-window 与 --draw3-control-fence-test 通过。Build/Win7DwmTest 中带 -v2 后缀的 ZIP 已核对 PE 架构、子系统版本、相关导入、内嵌 PptCOM 及包内 EXE 哈希。
