# 实施与验证结果

日期：2026-11-01。用户已批准实施；源码与文档保留在工作树，未提交、未推送、未自动归档。

本文件记录初轮实现。用户后续显示调整与粒子强制禁用的验证及测试限制见 `validation-refinement.md`。

## 已实现

- 性能页：共享界面 30/60 FPS（默认60），Draw3 画布 30/60/120/240 FPS（默认120）；迁入硬件加速、DirectComposition、兆芯兼容三项，原配置键保持。
- 绘制“通用”：多指绘图、抬笔笔锋修正默认开启；墨迹预测默认精准，可选快速/关闭，显式映射 Kalman/StrokeEnd/Disabled。新笔锁存，旧笔及成功断触接续不被修改设置影响。
- 激光笔：多指和粒子默认关闭、说明包含 BETA；全局多指关闭时隐藏激光多指行，保存偏好不清空；粒子沿现有激光批次结束后生效。
- 软件调测：真实图形设备、adapter/driver/内存、主/辅助呈现、交换链、RTS/鼠标兼容摘要；只在 owner 初始化/恢复/Resize 边界发布缓存。
- 新增配置与运行合同已写入 `.trellis/spec/native-desktop/settings-performance-and-drawing.md` 并加入索引。

## 最终验证

| 检查 | 结果 | 记录 |
| --- | --- | --- |
| 完整 InkeysRepo.sln Debug / ARM64 | 通过，退出码0；原生 ARM64 MSBuild | validation/build-arm64-clean-link.log、.exitcode |
| InkeysHeadlessTests.exe --no-window | 通过，退出码0；末尾 PASS animation correctness，stderr为空 | validation/headless-final.stdout.log、.stderr.log、.exitcode |
| Inkeys.exe --config-boundary-test | 通过，退出码0；真实磁盘默认/白名单/往返/子偏好 | validation/config.* |
| Inkeys.exe --draw3-pen-taper-test | 通过，退出码0；334120断言，0失败 | validation/pen-taper.* |
| Inkeys.exe --draw3-parked-desktop-exit-test | 通过，退出码0；包含通用偏好/真实模型/忽略触点回收生产probe | validation/parked-desktop.* |
| Inkeys.exe --draw3-control-fence-test | 通过，退出码0；真实接续、Up/Cancel同槽换代、精确手势身份回收 | validation/control-fence.* |
| Scripts/i18n.ps1 check | en-US、zh-TW 均559/559，通过 | 本轮工具输出；生成文件已同步 |
| git diff --check | 通过 | 本轮工具输出 |
| BOM与换行 | 28个已跟踪修改文本的BOM均保持，源码/头/i18n保持CRLF | 本轮脚本对HEAD与工作树逐项核对 |

测试程序均通过已确认在产品窗口初始化前退出的自检入口或 `--no-window` 入口，隐藏启动、标准流重定向。未运行应用主窗口、设置窗口、Computer Use 或 hidden HWND fixture。

无窗口套件生成的10个橡皮演示PNG已从其旧任务输出路径归入本任务 `validation/headless-setting-artifacts/`，保留验证产物，不留下额外旧任务目录。

## 复核修正

- 将新增单指拒收放在真实断触匹配、模型更新和接管成功返回之后，避免旧笔等待接续时被新偏好误拒。未匹配/更新失败的新独立触点仍拒收。
- 拒收前保存已登记的手势key；Discard后producer可能Up并复用槽，不再读取旧record身份。确定性交错回归验证Up/Cancel及同址新generation。
- 新候选辅助函数补齐正确前置声明，正常编译通过。
- 第七个导航根项使原600 DIP夹具的236 DIP内容变成276 DIP。测试改按真实内容范围与可用高度判断overflow；thumb只在真实可见轨道检查，保留颜色/宽度/动画断言。未改生产导航来满足旧假设。

## 构建环境问题与处理

1. 沙箱内MSBuild子节点无法正常通信，持续产生低CPU空闲节点、未进入实际CL编译。超过约8分钟后只停止本任务已核验的MSBuild root及其子节点；保留 `validation/build-arm64.log`。在正常Windows环境用相同solution、Debug/ARM64、ARM64原生MSBuild重试后正常进入编译。
2. 一轮源码编译完成后，MSVC 14.44链接器在 IncrBuildImage 中 C0000005 / LNK1000。这是本机增量链接缓存/工具链问题。只核验并删除生成文件 `Build/ARM64/Debug/Inkeys.ilk`，未改源码、工程链接选项或依赖来绕过。相同配置重新构建成功，见最终clean-link记录。
3. PATH规范化后vcpkg先探测pwsh.exe失败，随后自动使用系统Windows PowerShell执行既有applocal.ps1并成功。未修改全局PATH、项目或vcpkg脚本。

最终构建0错误，17条警告来自已有第三方数值转换、匿名结构及既有生产probe的nodiscard调用；未为清零警告修改无关代码。

## 实际验证边界

- 无窗口的图形/布局回归、真实模型及触点/mailbox测试通过，不等同实体多点触控、激光粒子视觉或真实设备故障回退验收。
- 画布信息缓存的发布、合并、换代、停止标记有生产probe；真实DXGI/RTS信息来源由编译与静态owner链复核，未通过打开GUI或主动制造驱动故障动态验收。
- GUI、真实笔/触摸、真实Hardware→WARP或DComp→ULW驱动恢复未获本轮授权，均未执行。没有扩大平台支持承诺。
