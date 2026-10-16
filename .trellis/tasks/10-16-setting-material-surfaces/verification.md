# 验证结果
- 完整 InkeysRepo.sln Debug | ARM64 构建通过，ARM64 原生 MSBuild /m:1 /nr:false，退出码 0。构建前同一 PowerShell invocation 按 AGENTS 规范移除 Env:PATH，并设 MSBUILDDISABLENODEREUSE=1；15分钟上限。
- InkeysHeadlessTests.exe --no-window --setting-home-only 通过，退出码 0，stderr 为空。
- Solid/Mica/Acrylic 三模式实际 ComboBox 绘制与交互，7种 DPI（1/1.25/1.5/1.875/2/3/4）；材质 alpha/RGB、前景与边框不变、纯色回退及浮层不透明断言均通过。
- 浅/深主题各6类材质、每类3个缓存阶段，WARP cached/full 精确 RGBA 比较通过，含真实分层 Mica/Acrylic 调色板。
- 子代理静态审查发现共享 SolidBgQuarternary 浮层覆盖风险，已保留实色并补实际 popup DrawData 回归；复查无剩余确定问题。
- git diff --check 通过，既有修改文件 UTF8/CRLF 保留，任务开始时 PptCOM.dll/tlb 字节已恢复。
- 无 GUI 验证，视觉效果待用户人工验收；未新增模糊 shader/壁纸模拟/依赖，未改变 ImGui 或 vendor。
- 用户已批准提交本次改动并结束任务；按既有 SSH 签名配置提交工作变更，再归档任务并记录会话，不执行 push。
- 日志：%TEMP%/inkeys-material-surfaces-20261016/build-final.log、tests.log、tests-stderr.log。
