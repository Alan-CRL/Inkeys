# 诊断包 2 验证记录

## 范围与安全边界

- 用户已授权完成包2规划、实现与最终交付；不commit/push。
- 保留既有v1工作树变更；外部兆芯源目录只读，分析副本位于Build。
- 不启动产品主窗口、设置、RTS、隐藏HWND或Computer Use。独立ResourceProbe经源码检查无HWND/swapchain/Host/config依赖后，才执行CLI离屏D3D。
- 编译统一使用InkeysRepo.sln与动态定位的ARM64原生MSBuild；同PowerShell invocation移除Env:PATH并禁用node reuse。复用已有依赖/串行配置，45分钟超时，不改SDK/工具链。

## Native 构建与CLI

- 初轮Debug|ARM64：exit1，首有效源码错误Performance.cpp测试变量small与Windows SDK rpcndr.h的small宏冲突。只将测试局部变量改名probeCapacities；未undef宏或修改SDK。
- 第二轮Debug|ARM64：exit1，唯一源码错误ResourceProbe.cpp的DWORD PID直接赋Json::Value重载歧义；改显式Json::UInt。
- 第三轮Debug|ARM64（build-debug-arm64-final2.json）：exit0，未超时。
- 独立check随后收紧session.graphics_detail_complete的原始文件导出成功gate；最终快照Debug|ARM64（build-debug-arm64-final3.json）：exit0，8.44s。
- 最终早期CLI（cli-debug-arm64-final3/results.json）：performance/launch/config/resource-test/resource-list均exit0；未知resource后缀、大小写变体、错误参数数量均GUI前exit2，8/8符合预期。
- --draw3-performance-test：recorder failures=0、production proof failures=0；实际payload=32627128B，budget33554432B，frame_row800B、call_row128B、group_row112B、resource_row96B。
- launch probe：14类完成状态，包含full/slow/resource/group/site覆盖分离。
- InkeysHeadlessTests.exe --no-window（cli-debug-arm64-final/results.json）：exit0，72.05s；输出包含EraserAttribute layouts216/failures0、RecallAttribute failures0、PASS animation correctness。

## 真实离屏协议验证（本机，不是兆芯复现）

命令：Build/Draw3PerformanceV2Validation/Test-Offscreen.ps1 -Configuration Debug -Platform ARM64 -RunTag final。

- offscreen-debug-arm64-final/validation-summary.json：40/40 complete，40/40 identity_verified，10.627s，脚本自然exit0。
- 各worker实际EXE/PID/精确FILETIME/session/case/目录/backend/result/exit与父持有进程核验；matrix使用最终Debug EXE hash及读锁。
- native-evidence-summary.json：共6296条原始调用，0drop、0invalid；240条GPU记录中204 ready、36 not_started（CPU-only路径不可填0时间）。
- 无HWND与原调用语义静态验证：review-native-static.json比较7个TU的309处原D3D调用，顺序/参数保持；ULW两个互斥Unmap分支人工核对。历史未注册resource明确unknown，不伪造实例。
- 本机HW/WARP结果只用来验证协议和实现；构建负载/本机设备与现场不同，不拿这些耗时替代兆芯性能。

## 脚本、兼容与封装

## Release 与最终交付

- Release|ARM64：build-release-arm64-final.json，自然exit0，4m22s；Release|x64：build-release-x64-final.json，自然exit0，4m13s。
- 两个Release版本各8个早期CLI均通过；实际独立离屏矩阵各40/40 complete+身份确认（ARM64约8.70s，x64约11.46s，本机ARM64兼容运行）。
- 最终冻结分析器重新消费Debug ARM64、Release ARM64、Release x64三套真实raw：各40cases/20pairs/0issues，均exit0；包含新增逐轮 raw Draw=definition=GPU draw_calls 的完整性门。
- Native警告主要来自既有third-party转换、UI数值转换及v1 probe未消费nodiscard返回；没有为消除无关warning修改代码。两个源码编译错误均已定点修复，非SDK/PATH环境故障。

## 脚本与兼容

- Windows PowerShell 5.1.26100.9711与pwsh7.6.5：原完整回归各225项通过、自然exit0。证据ps51-delivery-final.log/pwsh-delivery-final.log及script-freeze-final.json。
- 最后的Draw分母保护、缺Draw/错GPU次数负fixture与相关v2行为：两个runtime各100项定向回归通过、自然exit0。证据ps51-final-targeted.log/pwsh-final-targeted.log。
- 输入批次数单位修正后，两个runtime对真实v1副本与v2-success再分析均exit0，明确callback batches/frame，不再混入ms分布。证据ps51-count-unit-final.json/pwsh-count-unit-final.json。
- script-final-static-check.json：BOM/CRLF、CMD ASCII、AST和PS2禁API静态门通过；实际Win7/PS2仍未运行。
- v1兆芯副本分析complete，169循环/26Draw/24主画布/20墨迹，主画布work8754.652ms、Map4186.707ms、ownerCPU8593.75ms，GPU9条全无实际Draw覆盖；不再由空循环P50约0.054ms误判绘制性能。
- 最终独立Trellis check PASS，无遗留阻断项，见research/check-v2.md。已列单repo受影响native-desktop/native层并复核其质量与CPU/GPU合同；没有改HLSL或PPT接口。

## 封装及SHA

最终交付只使用下列两ZIP（中间版本不作为交付）：

- x64：Build/Draw3PerformancePackages/Draw3Performance-v2-x64-20261015-072452-7d554992b51947d382885126b42e1491.zip
  - ZIP SHA256：0eb4e503beaf05da678661e4c8d77dd5c2720a621a89be6207d51beadc172b95
  - EXE SHA256：7b512fdd6ea2a8aea9d4dbf7e52582b86ec972f975ce84e5f31356dd55ea53b7
- ARM64：Build/Draw3PerformancePackages/Draw3Performance-v2-arm64-20261015-072455-95c87de767e347e9815b521eaa030fc6.zip
  - ZIP SHA256：b4a4ddd3e8dfd1c992a14ed86bdd2aa6dc3bfdc9fe1d1eb60f5db29965446416
  - EXE SHA256：ad64a46b16fca47880d3c59bdb8db2d7af3fd7b76c1ba1761cb18320ca3d00f9

Verify-Packages.py对每个ZIP逐stream核验，精确7个用户文件，ZIP/目录/manifest/最终源脚本/ReleaseEXE全部SHA一致；PE架构、V2精确marker、各raw schema及40case声明通过。最终5用户文件哈希与script-freeze-final.json一致。packages.json保存完整路径及每个entry证据。

输出默认保持CMD所在目录/Inkeys-Draw3-Diagnostics；最终用户按正常退出完成导出后等待离屏矩阵，日志保留原始文件和中文报告。目标兆芯现场运行是下一轮采集，不是未完成的包实现步骤。


## 格式与局限

- git diff --check已通过；独立检查44个baseline原文件BOM/bare-LF/EOF保持。最终交付前再次复核。
- Win7 PS2真实系统、目标兆芯硬件、真实GUI/RTS绘制链尚未本机测试；脚本兼容性静态检查和Win11上的双runtime不能冒充这些现场结果。
- 软件Present成功不是屏幕光学完成。GPU命令流区间含CPU提交空隙；probe专有一次Flush/有界drain不属于生产行为。
