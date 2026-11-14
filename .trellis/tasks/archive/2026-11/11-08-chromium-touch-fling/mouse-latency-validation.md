# Mouse拖动延迟修复：验证记录（2026-11-08）

## 实现与审查

基线4b4edb91；仅4个生产文件：Draw3.WindowControl.cpp/.cppm和Draw3.DrawingController.cpp/.cppm。CanvasPanMode中的Mouse纯位置发布仍更新mailbox，但跳过自绘光标control wake；valid/contact边沿、clear/取消、owner/suppression/模式变化和非Pan行为保持。未改Touch/Pen、动力学、屏幕尺度、工程文件或依赖。

新增--draw3-control-fence-test中的真实生产publisher接线回归：1000次burst保持latest且不产生control wake/pending/render；并发持续位置发布不打断真实WaitForFrameDeadline；Down/Up/valid/clear/退出Pan和非Pan位置仍urgent。Sleep(1)只是测试持续流，不保证1kHz硬件采样率。没有创建HWND或模拟USER32离窗/捕获事件。

独立trellis-check通过，未发现需要修改的源码问题；建议修正文档1kHz措辞已完成。详见research/mouse-latency-review.md。

## 构建

- 动态vswhere定位当前Visual Studio原生ARM64 MSBuild；完整InkeysRepo.sln，Debug|ARM64，/m:1 /nr:false，Python subprocess总超时1800秒，同PowerShell规范PATH并禁node reuse。
- 仅移除已核实的生成缓存Build/ARM64/Debug/Inkeys.ilk，避免此前记录的增量链接器内部崩溃；没有改工程/工具链配置。
- exit0，28.58秒。主程序、PptCOM、PptCOM.Tests和Headless均完成。原始日志Build/Logs/mouse-latency-build.log，结果JSON为mouse-latency-build-result.json。
- 编译输出的第三方精度转换和既有测试nodiscard警告不属本轮新增。PATH规范后vcpkg先找pwsh.exe的提示仍存在，既有targets使用绝对路径回退，最终构建及实际CLI均成功；未为此修改源码或环境全局配置。

## 运行结果

| 测试 | 退出码 | 耗时/证据 |
| --- | ---: | --- |
| --draw3-control-fence-test | 0 | 1.33秒；包含新增Mouse真实publisher→Coordinator→deadline回归 |
| --draw3-canvas-pan-feel-test | 0 | 2.39秒；2112行，failures=0，no_gpu=1 no_hwnd=1 |
| --draw3-diagnostic-output-test | 0 | 1.78秒；真实queue/pipe、gate与LastError，无HWND |
| Headless --no-window --canvas-pan-only | 0 | 1.24秒；产品/Chromium参考/背景/Freeze通过 |
| 完整Headless --no-window | 0 | 431.39秒；末尾PASS animation correctness，相关Mouse/来源过滤等完整回归通过 |

原始日志均位于Build/Logs/mouse-latency-*.log；逐项退出码和耗时在mouse-latency-test-results.json，产物及4源码SHA256在mouse-latency-artifact-hashes.json。

## 格式和范围

- 4源码及canvas-pan.md严格UTF8解码；BOM与HEAD一致，全部CRLF，bare LF=0；git diff --check通过。没有整文件格式变化。
- 新日志使用既有CanvasPan gate和4096字节有界输出；关闭gate没有新增诊断取时/格式化，空Idle不刷render。signature/attempt/ok仅反映快照CPU签名、调用和实际返回，未改变合成失败策略。
- 此前已有未提交的scale-stage-plan/research及PRD/design/implement/jsonl/task.json完全保留，不覆盖、不当成本轮实现。
- 验证阶段未启动GUI、使用Computer Use、commit/push或归档任务。既有任务继续in_progress，屏幕适配仍延后。配置/国际化未改，本轮不重复i18n生成。

## 实测与日志边界

现有源码和回归证明已消除Mouse额外控制唤醒、恢复抢占和活动deadline破坏；没有测得GPU积压或光学延迟数值，不能据此认定现场唯一原因或宣称手感验收。

产物Build/ARM64/Debug/Inkeys.exe。原“白板平移与惯性诊断”开关开启并重启后，新[CanvasPan] render行包含tiles-recovered/pending、yield、visible-clear、fallback-signature/attempt/ok、recovery/before-composite/composite/work/frame/present-ms、submit-qpc/interval和sample-to-submit-ms。Present返回是软件呈现入口结果，先于observer/ready，不等于屏幕扫描。用户手动拖动时可据该行区分输入消费之后的恢复、合成及软件提交阶段。
## 用户确认与提交范围

2026-11-08：用户明确“确认鼠标正确”，授权提交本次鼠标延迟修复，但不结束任务。本次提交包含4个源码、相关规范、研究/审查/验证及交接记录；此前屏幕适配规划不混入。此反馈仅代表当前鼠标实测确认，不扩大为跨设备全部验收。任务保持in_progress，不运行finish-work/archive，不push；本次commit授权不能延续到后续工作。
